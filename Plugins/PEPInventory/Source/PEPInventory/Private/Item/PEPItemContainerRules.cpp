#include "Item/PEPItemContainerRules.h"
#include "Item/PEPItemResultTypes.h"
#include "Core/PEPCoreTags.h"

#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Item/PEPItemContainerQuery.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemRules.h"

namespace PEPItemContainerRules
{
	FPEPItemResult CanUnlockContainer(
		const FPEPItemInstance& ContainerItem,
		const FPEPItemInstance* KeyItem)
	{
		FPEPItemContainerData Data;
		if (!PEPItemContainerQuery::GetContainerData(ContainerItem, Data))
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ContainerItem.ItemId);
		}

		if (!Data.bLocked)
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, ContainerItem.ItemId);
		}

		const FPEPItemFragment_Container* Fragment = ContainerItem.GetDefinitionFragment<
			FPEPItemFragment_Container>();
		if (Fragment && !Fragment->RequiredUnlockKeyTags.IsEmpty())
		{
			if (!KeyItem || !PEPItemRules::DoesItemMatchRequiredBlockedTags(
				*KeyItem, Fragment->RequiredUnlockKeyTags, FGameplayTagContainer()))
			{
				return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, ContainerItem.ItemId);
			}
		}

		return FPEPItemResult::Success(ContainerItem.ItemId, PEPCoreTags::Reason_None);
	}

	bool DoesSlotRevealOccupants(
		const FPEPContainerSlotDefinition& Slot,
		const EPEPAccessRole Role)
	{
		/* Named for the view it governs, so it is the outsider's view it withholds. */
		if (Role == EPEPAccessRole::External && !Slot.bIncludeChildrenInExternalView)
		{
			return false;
		}

		switch (Slot.GetPolicyFor(Role).Visibility)
		{
		case EPEPContainerSlotVisibility::Visible:
		case EPEPContainerSlotVisibility::ViewOnly:
		case EPEPContainerSlotVisibility::Lootable:
			return true;

		case EPEPContainerSlotVisibility::OwnerOnly:
			return Role == EPEPAccessRole::Owner;

		/*
		 * Nothing models searching yet, so this withholds. Revealing instead would publish, on the
		 * strength of a mechanic that does not exist, exactly the contents a designer asked to keep
		 * back until it did — and a privacy setting that fails open is worse than one that fails
		 * shut. Whoever adds searching consults its state here.
		 */
		case EPEPContainerSlotVisibility::RequiresSearch:
		case EPEPContainerSlotVisibility::Hidden:
		default:
			return false;
		}
	}

	bool DoesContainerRevealContents(
		const FPEPItemInstance& ContainerItem,
		const EPEPAccessRole Role)
	{
		const FPEPItemFragment_Container* Fragment =
			ContainerItem.GetDefinitionFragment<FPEPItemFragment_Container>();

		if (!Fragment)
		{
			return false;
		}

		/* The owner's view of their own store is not a projection of it; it is the store. */
		if (Role == EPEPAccessRole::Owner)
		{
			return true;
		}

		switch (Fragment->ExternalLootVisibility)
		{
		case EPEPContainerLootVisibility::ShowContents:
			return true;

		/* Every other value withholds the contents, for its own reason. See RequiresSearch above. */
		case EPEPContainerLootVisibility::NotAContainer:
		case EPEPContainerLootVisibility::Hidden:
		case EPEPContainerLootVisibility::ShowContainerOnly:
		case EPEPContainerLootVisibility::SearchThenShowContents:
		case EPEPContainerLootVisibility::OwnerOnly:
		default:
			return false;
		}
	}

	FPEPItemResult CanLockContainer(
		const FPEPItemInstance& ContainerItem)
	{
		FPEPItemContainerData Data;
		if (!PEPItemContainerQuery::GetContainerData(ContainerItem, Data))
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ContainerItem.ItemId);
		}

		if (Data.bLocked || !Data.bCanRelock)
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, ContainerItem.ItemId);
		}

		return FPEPItemResult::Success(ContainerItem.ItemId, PEPCoreTags::Reason_None);
	}
}

FPEPItemResult UPEPItemContainerRulesLibrary::CanUnlockContainer(
	const FPEPItemInstance& ContainerItem,
	const FPEPItemInstance& KeyItem,
	const bool bUseKeyItem)
{
	return PEPItemContainerRules::CanUnlockContainer(ContainerItem, bUseKeyItem ? &KeyItem : nullptr);
}

FPEPItemResult UPEPItemContainerRulesLibrary::CanLockContainer(
	const FPEPItemInstance& ContainerItem)
{
	return PEPItemContainerRules::CanLockContainer(ContainerItem);
}

TArray<FPEPContainerSlotDefinition> UPEPItemContainerRulesLibrary::GetContainerSlots(
	const UPEPItemDefinition* Definition)
{
	const FPEPItemFragment_Container* Fragment = Definition
		? Definition->GetDefinitionFragment<FPEPItemFragment_Container>()
		: nullptr;
	return Fragment ? Fragment->Slots : TArray<FPEPContainerSlotDefinition>();
}

bool UPEPItemContainerRulesLibrary::CanPlaceItemInSlot(
	const UPEPItemDefinition* ContainerDefinition,
	const FName SlotName,
	const UPEPItemDefinition* CandidateItem,
	FText& OutError)
{
	if (!ContainerDefinition || !CandidateItem)
	{
		OutError = FText::FromString(TEXT("Missing container definition or candidate item."));
		return false;
	}

	const FPEPItemFragment_Container* Container =
		ContainerDefinition->GetDefinitionFragment<FPEPItemFragment_Container>();
	const FPEPContainerSlotDefinition* Slot = Container ? Container->FindSlot(SlotName) : nullptr;
	if (!Slot || !Slot->IsValid())
	{
		OutError = FText::FromString(FString::Printf(
			TEXT("'%s' has no valid slot named '%s'."),
			*ContainerDefinition->GetName(),
			*SlotName.ToString()));
		return false;
	}

	if (!CandidateItem->HasAllOwnedTags(Container->RequiredContainedItemTags)
		|| CandidateItem->HasAnyOwnedTags(Container->BlockedContainedItemTags)
		|| !CandidateItem->HasAllOwnedTags(Slot->RequiredItemTags)
		|| CandidateItem->HasAnyOwnedTags(Slot->BlockedItemTags))
	{
		OutError = FText::FromString(FString::Printf(
			TEXT("'%s' is rejected by slot '%s' tag rules."),
			*CandidateItem->GetName(),
			*SlotName.ToString()));
		return false;
	}

	OutError = FText::GetEmpty();
	return true;
}
