#include "Item/PEPItemMutation.h"
#include "Item/PEPItemResultTypes.h"
#include "Core/PEPCoreTags.h"

#include "Item/Internal/PEPItemMutableAccess.h"
#include "Item/PEPItemQuery.h"
#include "Item/PEPItemScalar.h"
#include "Item/PEPItemValueTags.h"

struct FPEPItemMutationAccess
{
	static bool TryDuplicateWithNewId(const FPEPItemInstance& Item, FPEPItemInstance& OutItem)
	{
		return Item.TryDuplicateWithNewId(OutItem);
	}
};

namespace PEPItemMutation
{
	FPEPItemResult MergeStacks(
		FPEPItemInstance& SourceItem,
		FPEPItemInstance& TargetItem,
		const int32 RequestedTransferCount,
		int32& OutTransferCount)
	{
		const FPEPItemResult RuleResult = PEPItemRules::CanStackItems(
			SourceItem, TargetItem, RequestedTransferCount, OutTransferCount);
		if (!RuleResult.bSuccess)
		{
			return RuleResult;
		}

		FPEPItemState_Stack* SourceStack = FPEPItemMutableAccess::GetMutableState<
			FPEPItemState_Stack>(SourceItem);
		FPEPItemState_Stack* TargetStack = FPEPItemMutableAccess::GetMutableState<
			FPEPItemState_Stack>(TargetItem);
		if (!SourceStack || !TargetStack)
		{
			OutTransferCount = 0;
			return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, SourceItem.ItemId);
		}

		// The rule clamped the transfer against the live counts just above, so the target cannot
		// overflow its maximum and the source cannot go negative.
		SourceStack->Count -= OutTransferCount;
		TargetStack->Count += OutTransferCount;
		return FPEPItemResult::Success(TargetItem.ItemId, PEPCoreTags::Reason_None);
	}

	FPEPItemResult SplitStack(
		FPEPItemInstance& SourceItem,
		const int32 SplitCount,
		FPEPItemInstance& OutSplitItem)
	{
		OutSplitItem = FPEPItemInstance();

		const FPEPItemResult RuleResult = PEPItemRules::CanSplitStack(SourceItem, SplitCount);
		if (!RuleResult.bSuccess)
		{
			return RuleResult;
		}

		if (!FPEPItemMutationAccess::TryDuplicateWithNewId(SourceItem, OutSplitItem))
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, SourceItem.ItemId);
		}

		FPEPItemState_Stack* SourceStack = FPEPItemMutableAccess::GetMutableState<
			FPEPItemState_Stack>(SourceItem);
		FPEPItemState_Stack* SplitStack = FPEPItemMutableAccess::GetMutableState<
			FPEPItemState_Stack>(OutSplitItem);
		if (!SourceStack || !SplitStack)
		{
			OutSplitItem = FPEPItemInstance();
			return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, SourceItem.ItemId);
		}

		SourceStack->Count -= SplitCount;
		SplitStack->Count = SplitCount;
		return FPEPItemResult::Success(OutSplitItem.ItemId, PEPCoreTags::Reason_None);
	}

	bool DuplicateItemWithNewId(
		const FPEPItemInstance& SourceItem,
		FPEPItemInstance& OutDuplicateItem)
	{
		return FPEPItemMutationAccess::TryDuplicateWithNewId(SourceItem, OutDuplicateItem);
	}

	FPEPItemResult SetStackCount(
		FPEPItemInstance& Item,
		const int32 NewCount)
	{
		return PEPItemScalarMutation::SetIntValue(
			Item,
			PEPItemValueTags::Stack_Count,
			NewCount);
	}

	FPEPItemResult ConsumeFromStack(
		FPEPItemInstance& Item,
		const int32 Count,
		int32& OutRemaining)
	{
		return PEPItemScalarMutation::ConsumeIntValue(
			Item,
			PEPItemValueTags::Stack_Count,
			Count,
			OutRemaining);
	}

	FPEPItemResult ConsumeResource(
		FPEPItemInstance& Item,
		const int32 Amount,
		int32& OutRemaining)
	{
		return PEPItemScalarMutation::ConsumeIntValue(
			Item,
			PEPItemValueTags::Resource,
			Amount,
			OutRemaining);
	}

	FPEPItemResult SetDurability(
		FPEPItemInstance& Item,
		const float NewDurability)
	{
		return PEPItemScalarMutation::SetFloatValue(
			Item,
			PEPItemValueTags::Durability,
			NewDurability);
	}

	namespace Private
	{
		FPEPItemResult OffsetDurability(
			FPEPItemInstance& Item,
			const float Amount,
			const bool bIsRepair)
		{
			float TargetDurability = 0.0f;
			const FPEPItemResult OffsetResult = PEPItemRules::ResolveDurabilityOffset(
				Item, Amount, bIsRepair, TargetDurability);

			return OffsetResult.bSuccess
				? SetDurability(Item, TargetDurability)
				: OffsetResult;
		}
	}

	FPEPItemResult DamageDurability(
		FPEPItemInstance& Item,
		const float DamageAmount)
	{
		return Private::OffsetDurability(Item, DamageAmount, false);
	}

	FPEPItemResult RepairDurability(
		FPEPItemInstance& Item,
		const float RepairAmount)
	{
		return Private::OffsetDurability(Item, RepairAmount, true);
	}
}
