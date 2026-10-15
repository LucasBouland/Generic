#include "Item/PEPItemQuery.h"

#include "Item/PEPItemSettings.h"
#include "Item/PEPItemLogChannels.h"

namespace PEPItemQuery
{
	const UPEPItemDefinition* GetDefinition(
		const FPEPItemInstance& Item)
	{
		return Item.GetDefinitionIfLoaded();
	}

	bool GetDisplayDataFromDefinition(
		const UPEPItemDefinition* Definition,
		FPEPItemDisplayData& OutData)
	{
		OutData = FPEPItemDisplayData();
		if (!Definition)
		{
			return false;
		}

		OutData.bValid = true;
		OutData.DefinitionId = Definition->GetPrimaryAssetId();
		OutData.DisplayName = Definition->DisplayName;
		OutData.Description = Definition->Description;
		OutData.Icon = Definition->ItemIcon;
		OutData.OwnedTags = Definition->OwnedTags;
		return true;
	}

	bool GetDisplayData(
		const FPEPItemInstance& Item,
		FPEPItemDisplayData& OutData)
	{
		return GetDisplayDataFromDefinition(GetDefinition(Item), OutData);
	}

	bool HasOwnedTag(
		const FPEPItemInstance& Item,
		const FGameplayTag Tag)
	{
		const UPEPItemDefinition* Definition = GetDefinition(Item);
		return Definition && Definition->HasOwnedTag(Tag);
	}

	bool HasOwnedTagExact(
		const FPEPItemInstance& Item,
		const FGameplayTag Tag)
	{
		const UPEPItemDefinition* Definition = GetDefinition(Item);
		return Definition && Definition->HasOwnedTagExact(Tag);
	}

	bool HasAnyOwnedTags(
		const FPEPItemInstance& Item,
		const FGameplayTagContainer& Tags)
	{
		const UPEPItemDefinition* Definition = GetDefinition(Item);
		return Definition && Definition->HasAnyOwnedTags(Tags);
	}

	bool HasAllOwnedTags(
		const FPEPItemInstance& Item,
		const FGameplayTagContainer& Tags)
	{
		const UPEPItemDefinition* Definition = GetDefinition(Item);
		return Definition && Definition->HasAllOwnedTags(Tags);
	}

	bool GetOwnedTags(
		const FPEPItemInstance& Item,
		FGameplayTagContainer& OutTags)
	{
		OutTags.Reset();
		const UPEPItemDefinition* Definition = GetDefinition(Item);
		if (!Definition)
		{
			PEPItemLog::LogQueryFailure(TEXT("PEPItemQuery.GetOwnedTags"), Item);
			return false;
		}
		OutTags = Definition->OwnedTags;
		return true;
	}

	bool DoesItemMatchRequiredBlockedTags(
		const FPEPItemInstance& Item,
		const FGameplayTagContainer& RequiredTags,
		const FGameplayTagContainer& BlockedTags)
	{
		// Fail closed even when no filters are supplied. An empty filter means "any valid item",
		// not an invalid instance or one whose definition is unavailable.
		const UPEPItemDefinition* Definition = GetDefinition(Item);
		if (!Item.IsValid() || !Definition)
		{
			return false;
		}
		if (!Definition->HasAllOwnedTags(RequiredTags))
		{
			return false;
		}
		if (!BlockedTags.IsEmpty() && Definition->HasAnyOwnedTags(BlockedTags))
		{
			return false;
		}
		return true;
	}

	bool GetStackData(
		const FPEPItemInstance& Item,
		FPEPItemStackData& OutData)
	{
		OutData = FPEPItemStackData();
		const FPEPItemFragment_Stackable* Fragment = GetFragment<FPEPItemFragment_Stackable>(Item);
		if (!Fragment)
		{
			PEPItemLog::LogQueryFailure(TEXT("PEPItemQuery.GetStackData"), Item);
			return false;
		}

		const FPEPItemState_Stack* State = GetState<FPEPItemState_Stack>(Item);
		OutData.bValid = true;
		OutData.MaxStackSize = PEPItemSettings().GetEffectiveMaxStackSize(
			Fragment->MaxStackSize, Fragment->bUseProjectDefaultMaxStackSize);
		OutData.Count = FMath::Clamp(State ? State->Count : 1, 0, OutData.MaxStackSize);
		OutData.FreeSpace = FMath::Max(0, OutData.MaxStackSize - OutData.Count);
		OutData.bFull = OutData.Count >= OutData.MaxStackSize;
		return true;
	}

	bool GetDurabilityData(
		const FPEPItemInstance& Item,
		FPEPItemDurabilityData& OutData)
	{
		OutData = FPEPItemDurabilityData();
		const FPEPItemFragment_Durability* Fragment = GetFragment<FPEPItemFragment_Durability>(
			Item);
		if (!Fragment)
		{
			PEPItemLog::LogQueryFailure(TEXT("PEPItemQuery.GetDurabilityData"), Item);
			return false;
		}

		const FPEPItemState_Durability* State = GetState<FPEPItemState_Durability>(Item);
		if (!State
			|| !FMath::IsFinite(State->Durability)
			|| State->Durability < 0.0f
			|| State->Durability > 1.0f)
		{
			PEPItemLog::LogQueryFailure(TEXT("PEPItemQuery.GetDurabilityData"), Item);
			return false;
		}

		OutData.bValid = true;
		OutData.Durability = State->Durability;
		OutData.bBroken = OutData.Durability <= 0.0f;
		return true;
	}

	bool GetResourceData(
		const FPEPItemInstance& Item,
		FPEPItemResourceData& OutData)
	{
		OutData = FPEPItemResourceData();
		const FPEPItemFragment_Resource* Fragment = GetFragment<FPEPItemFragment_Resource>(Item);
		if (!Fragment)
		{
			PEPItemLog::LogQueryFailure(TEXT("PEPItemQuery.GetResourceData"), Item);
			return false;
		}

		const FPEPItemState_Resource* State = GetState<FPEPItemState_Resource>(Item);
		if (!State
			|| Fragment->MaxResource <= 0
			|| State->Current < 0
			|| State->Current > Fragment->MaxResource)
		{
			PEPItemLog::LogQueryFailure(TEXT("PEPItemQuery.GetResourceData"), Item);
			return false;
		}

		OutData.bValid = true;
		OutData.MaxResource = Fragment->MaxResource;
		OutData.Current = State->Current;
		OutData.bDepleted = OutData.Current <= 0;
		return true;
	}

	bool GetWeightData(
		const FPEPItemInstance& Item,
		FPEPItemWeightData& OutData)
	{
		OutData = FPEPItemWeightData();
		if (!Item.IsValid())
		{
			PEPItemLog::LogQueryFailure(TEXT("PEPItemQuery.GetWeightData"), Item);
			return false;
		}

		const FPEPItemFragment_Weight* Fragment = GetFragment<FPEPItemFragment_Weight>(Item);
		OutData.bUsesDefaultWeight = Fragment == nullptr;
		OutData.UnitWeight = Fragment
			                     ? Fragment->GetEffectiveUnitWeight()
			                     : PEPItemSettings().GetEffectiveUnitWeight(0.0f, true);

		// A stack weighs its unit weight times its count; anything else counts as a single unit.
		// Read the state directly rather than through GetStackData, which would log a query
		// failure for every non-stackable item.
		const FPEPItemState_Stack* StackState = GetState<FPEPItemState_Stack>(Item);
		const int32 UnitCount = StackState ? FMath::Max(0, StackState->Count) : 1;
		OutData.StackWeight = OutData.UnitWeight * static_cast<float>(UnitCount);

		OutData.bValid = true;
		return true;
	}

	bool GetWorldPresentationData(
		const FPEPItemInstance& Item,
		FPEPItemWorldPresentationData& OutData)
	{
		OutData = FPEPItemWorldPresentationData();
		const FPEPItemFragment_WorldPresentation* Fragment = GetFragment<FPEPItemFragment_WorldPresentation>(
			Item);
		if (!PEPItemSettings().GetEffectiveWorldPresentation(Fragment, OutData))
		{
			PEPItemLog::LogQueryFailure(TEXT("PEPItemQuery.GetWorldPresentationData"), Item);
			return false;
		}
		return true;
	}
}

UPEPItemDefinition* UPEPItemQueryLibrary::GetItemDefinition(
	const FPEPItemInstance& Item)
{
	return const_cast<UPEPItemDefinition*>(PEPItemQuery::GetDefinition(Item));
}

bool UPEPItemQueryLibrary::GetItemDisplayData(
	const FPEPItemInstance& Item,
	FPEPItemDisplayData& OutData)
{
	return PEPItemQuery::GetDisplayData(Item, OutData);
}

bool UPEPItemQueryLibrary::ItemHasOwnedTag(
	const FPEPItemInstance& Item,
	const FGameplayTag Tag)
{
	return PEPItemQuery::HasOwnedTag(Item, Tag);
}

bool UPEPItemQueryLibrary::ItemHasOwnedTagExact(
	const FPEPItemInstance& Item,
	const FGameplayTag Tag)
{
	return PEPItemQuery::HasOwnedTagExact(Item, Tag);
}

bool UPEPItemQueryLibrary::ItemHasAnyOwnedTags(
	const FPEPItemInstance& Item,
	const FGameplayTagContainer& Tags)
{
	return PEPItemQuery::HasAnyOwnedTags(Item, Tags);
}

bool UPEPItemQueryLibrary::ItemHasAllOwnedTags(
	const FPEPItemInstance& Item,
	const FGameplayTagContainer& Tags)
{
	return PEPItemQuery::HasAllOwnedTags(Item, Tags);
}

bool UPEPItemQueryLibrary::GetItemOwnedTags(
	const FPEPItemInstance& Item,
	FGameplayTagContainer& OutTags)
{
	return PEPItemQuery::GetOwnedTags(Item, OutTags);
}

bool UPEPItemQueryLibrary::DoesItemMatchRequiredBlockedTags(
	const FPEPItemInstance& Item,
	const FGameplayTagContainer& RequiredTags,
	const FGameplayTagContainer& BlockedTags)
{
	return PEPItemQuery::DoesItemMatchRequiredBlockedTags(Item, RequiredTags, BlockedTags);
}

bool UPEPItemQueryLibrary::IsItemStackable(
	const FPEPItemInstance& Item)
{
	return PEPItemQuery::IsStackable(Item);
}

bool UPEPItemQueryLibrary::DoesItemHaveDurability(
	const FPEPItemInstance& Item)
{
	return PEPItemQuery::HasDurability(Item);
}

bool UPEPItemQueryLibrary::GetItemStackData(
	const FPEPItemInstance& Item,
	FPEPItemStackData& OutData)
{
	return PEPItemQuery::GetStackData(Item, OutData);
}

bool UPEPItemQueryLibrary::GetItemDurabilityData(
	const FPEPItemInstance& Item,
	FPEPItemDurabilityData& OutData)
{
	return PEPItemQuery::GetDurabilityData(Item, OutData);
}

bool UPEPItemQueryLibrary::GetItemWorldPresentationData(
	const FPEPItemInstance& Item,
	FPEPItemWorldPresentationData& OutData)
{
	return PEPItemQuery::GetWorldPresentationData(Item, OutData);
}

bool UPEPItemQueryLibrary::GetItemResourceData(
	const FPEPItemInstance& Item,
	FPEPItemResourceData& OutData)
{
	return PEPItemQuery::GetResourceData(Item, OutData);
}

bool UPEPItemQueryLibrary::GetItemWeightData(
	const FPEPItemInstance& Item,
	FPEPItemWeightData& OutData)
{
	return PEPItemQuery::GetWeightData(Item, OutData);
}

bool UPEPItemQueryLibrary::IsItemDepleted(
	const FPEPItemInstance& Item)
{
	return Item.IsDepleted();
}
