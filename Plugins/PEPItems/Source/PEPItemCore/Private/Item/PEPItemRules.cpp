#include "Item/PEPItemRules.h"
#include "Item/PEPItemResultTypes.h"
#include "Core/PEPCoreTags.h"

#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemQuery.h"
#include "Item/PEPItemScalar.h"
#include "Item/PEPItemValueTags.h"

namespace PEPItemRules
{
	bool AreSameDefinition(
		const FPEPItemInstance& FirstItem,
		const FPEPItemInstance& SecondItem)
	{
		return FirstItem.DefinitionId.IsValid() && FirstItem.DefinitionId == SecondItem.DefinitionId;
	}

	namespace Private
	{
		bool IsValidStackOperand(const FPEPItemInstance& Item)
		{
			FPEPItemInstanceValidationResult Validation;
			return Item.ValidateInstance(Validation);
		}

		/* Stack-identity markers are rare; the inline buffer keeps merge checks off the heap. */
		using FStackIdentityStates = TArray<const FInstancedStruct*, TInlineAllocator<4>>;

		void GatherStackIdentityStates(
			const FPEPItemInstance& Item,
			FStackIdentityStates& OutStates)
		{
			for (const FInstancedStruct& Entry : Item.GetStateFragments())
			{
				const FPEPItemStateFragment* State = Entry.GetPtr<FPEPItemStateFragment>();
				if (State && State->GetStackability() == EPEPStateStackability::StackIdentity)
				{
					OutStates.Add(&Entry);
				}
			}
		}
	}

	bool AreStacksCompatible(
		const FPEPItemInstance& FirstItem,
		const FPEPItemInstance& SecondItem)
	{
		if (!AreSameDefinition(FirstItem, SecondItem))
		{
			return false;
		}

		Private::FStackIdentityStates FirstStates;
		Private::FStackIdentityStates SecondStates;
		Private::GatherStackIdentityStates(FirstItem, FirstStates);
		Private::GatherStackIdentityStates(SecondItem, SecondStates);

		// A marker present on one side only is as disqualifying as one holding a different value.
		if (FirstStates.Num() != SecondStates.Num())
		{
			return false;
		}

		for (const FInstancedStruct* FirstEntry : FirstStates)
		{
			const UScriptStruct* StateStruct = FirstEntry->GetScriptStruct();
			bool bMatched = false;

			for (const FInstancedStruct* SecondEntry : SecondStates)
			{
				if (SecondEntry->GetScriptStruct() != StateStruct)
				{
					continue;
				}
				bMatched = StateStruct->CompareScriptStruct(
					FirstEntry->GetMemory(),
					SecondEntry->GetMemory(),
					PPF_None);
				break;
			}

			if (!bMatched)
			{
				return false;
			}
		}

		return true;
	}

	bool DoesDefinitionMatchRequiredBlockedTags(
		const UPEPItemDefinition* Definition,
		const FGameplayTagContainer& RequiredTags,
		const FGameplayTagContainer& BlockedTags)
	{
		return PEPItemQuery::DoesDefinitionMatchRequiredBlockedTags(Definition, RequiredTags, BlockedTags);
	}

	bool DoesItemMatchRequiredBlockedTags(
		const FPEPItemInstance& Item,
		const FGameplayTagContainer& RequiredTags,
		const FGameplayTagContainer& BlockedTags)
	{
		return PEPItemQuery::DoesItemMatchRequiredBlockedTags(Item, RequiredTags, BlockedTags);
	}

	FPEPItemResult CanStackItems(
		const FPEPItemInstance& SourceItem,
		const FPEPItemInstance& TargetItem,
		const int32 RequestedTransferCount,
		int32& OutTransferCount)
	{
		OutTransferCount = 0;

		if (!SourceItem.IsValid() || !TargetItem.IsValid() || SourceItem.ItemId == TargetItem.ItemId)
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, SourceItem.ItemId);
		}
		if (RequestedTransferCount < 0)
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, SourceItem.ItemId);
		}
		// Cheap identity checks first: callers scanning a store for merge targets reject most
		// candidates here, before paying for two full instance validations.
		if (!AreSameDefinition(SourceItem, TargetItem))
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, SourceItem.ItemId);
		}
		// Validate both operands at the mutation boundary. This also rejects runtime instances
		// whose stackable definition carries per-unit state that a merge would duplicate or lose.
		if (!Private::IsValidStackOperand(SourceItem) || !Private::IsValidStackOperand(TargetItem))
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, SourceItem.ItemId);
		}
		if (!AreStacksCompatible(SourceItem, TargetItem))
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_StateMismatch, SourceItem.ItemId);
		}

		FPEPItemStackData SourceStack;
		FPEPItemStackData TargetStack;
		if (!PEPItemQuery::GetStackData(SourceItem, SourceStack) ||
			!PEPItemQuery::GetStackData(TargetItem, TargetStack))
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, SourceItem.ItemId);
		}

		if (SourceStack.Count <= 0)
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, SourceItem.ItemId);
		}
		if (TargetStack.FreeSpace <= 0)
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_NoFreeSlot, TargetItem.ItemId);
		}

		const int32 DesiredTransferCount = RequestedTransferCount > 0 ? RequestedTransferCount : SourceStack.Count;
		const int32 TransferCount = FMath::Min(DesiredTransferCount,
		                                       FMath::Min(SourceStack.Count, TargetStack.FreeSpace));
		if (TransferCount <= 0)
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_NoFreeSlot, TargetItem.ItemId);
		}

		OutTransferCount = TransferCount;
		return FPEPItemResult::Success(TargetItem.ItemId, PEPCoreTags::Reason_None);
	}

	FPEPItemResult CanSplitStack(
		const FPEPItemInstance& SourceItem,
		const int32 RequestedSplitCount)
	{
		if (!SourceItem.IsValid() || RequestedSplitCount <= 0)
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, SourceItem.ItemId);
		}
		// Split duplicates all state except the stack count; reject invalid per-unit state before
		// creating a second instance that would inherit the same durability/resource value.
		if (!Private::IsValidStackOperand(SourceItem))
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, SourceItem.ItemId);
		}

		FPEPItemStackData SourceStack;
		if (!PEPItemQuery::GetStackData(SourceItem, SourceStack) || SourceStack.Count <= 1)
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, SourceItem.ItemId);
		}

		if (RequestedSplitCount >= SourceStack.Count)
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, SourceItem.ItemId);
		}

		return FPEPItemResult::Success(SourceItem.ItemId, PEPCoreTags::Reason_None);
	}

	FPEPItemResult CanSetDurability(
		const FPEPItemInstance& Item,
		const float NewDurability)
	{
		return PEPItemScalarRules::CanSetFloatValue(
			Item,
			PEPItemValueTags::Durability,
			NewDurability);
	}

	FPEPItemResult ResolveDurabilityOffset(
		const FPEPItemInstance& Item,
		const float Amount,
		const bool bIsRepair,
		float& OutTargetDurability)
	{
		OutTargetDurability = 0.0f;

		// NaN slips through a plain `< 0` test and FMath::Clamp then maps it to a bound: a NaN repair
		// would fully restore the item. Infinities would do the same. Refuse anything not finite.
		if (!FMath::IsFinite(Amount) || Amount < 0.0f)
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, Item.ItemId);
		}

		FPEPItemDurabilityData DurabilityData;
		if (!PEPItemQuery::GetDurabilityData(Item, DurabilityData))
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, Item.ItemId);
		}

		const float SignedAmount = bIsRepair ? Amount : -Amount;
		OutTargetDurability = FMath::Clamp(DurabilityData.Durability + SignedAmount, 0.0f, 1.0f);
		return FPEPItemResult::Success(Item.ItemId, PEPCoreTags::Reason_None);
	}

	namespace Private
	{
		FPEPItemResult CanOffsetDurability(
			const FPEPItemInstance& Item,
			const float Amount,
			const bool bIsRepair)
		{
			float TargetDurability = 0.0f;
			const FPEPItemResult OffsetResult = ResolveDurabilityOffset(
				Item, Amount, bIsRepair, TargetDurability);

			return OffsetResult.bSuccess
				? CanSetDurability(Item, TargetDurability)
				: OffsetResult;
		}
	}

	FPEPItemResult CanDamageDurability(
		const FPEPItemInstance& Item,
		const float DamageAmount)
	{
		return Private::CanOffsetDurability(Item, DamageAmount, false);
	}

	FPEPItemResult CanRepairDurability(
		const FPEPItemInstance& Item,
		const float RepairAmount)
	{
		return Private::CanOffsetDurability(Item, RepairAmount, true);
	}
}

FPEPItemResult UPEPItemRulesLibrary::CanStackItems(
	const FPEPItemInstance& SourceItem,
	const FPEPItemInstance& TargetItem,
	const int32 RequestedTransferCount,
	int32& OutTransferCount)
{
	return PEPItemRules::CanStackItems(SourceItem, TargetItem, RequestedTransferCount, OutTransferCount);
}

FPEPItemResult UPEPItemRulesLibrary::CanSplitStack(
	const FPEPItemInstance& SourceItem,
	const int32 RequestedSplitCount)
{
	return PEPItemRules::CanSplitStack(SourceItem, RequestedSplitCount);
}

FPEPItemResult UPEPItemRulesLibrary::CanSetDurability(
	const FPEPItemInstance& Item,
	const float NewDurability)
{
	return PEPItemRules::CanSetDurability(Item, NewDurability);
}

FPEPItemResult UPEPItemRulesLibrary::CanDamageDurability(
	const FPEPItemInstance& Item,
	const float DamageAmount)
{
	return PEPItemRules::CanDamageDurability(Item, DamageAmount);
}

FPEPItemResult UPEPItemRulesLibrary::CanRepairDurability(
	const FPEPItemInstance& Item,
	const float RepairAmount)
{
	return PEPItemRules::CanRepairDurability(Item, RepairAmount);
}
