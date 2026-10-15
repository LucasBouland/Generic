#include "PEPEquipmentUILibrary.h"

#include "Components/PEPEquipmentComponent.h"

bool UPEPEquipmentUILibrary::FilterEquipmentSlotChange(
	const FPEPItemStoreChange& Change,
	const FGameplayTag SlotTag,
	EPEPEquipmentSlotItemUpdate& ItemUpdate)
{
	ItemUpdate = EPEPEquipmentSlotItemUpdate::None;
	if (!SlotTag.IsValid())
	{
		return false;
	}

	bool bUsePrevious = false;
	bool bUseCurrent = false;
	switch (Change.ChangeType)
	{
	case EPEPItemStoreChangeType::Added:
	case EPEPItemStoreChangeType::StateChanged:
		bUseCurrent = true;
		break;
	case EPEPItemStoreChangeType::Removed:
		bUsePrevious = true;
		break;
	case EPEPItemStoreChangeType::Moved:
		bUsePrevious = true;
		bUseCurrent = true;
		break;
	default:
		return false;
	}

	const bool bPreviousDirect = bUsePrevious
		&& Change.PreviousLocation.Type == EPEPItemLocationType::NamedSlot
		&& Change.PreviousLocation.LocationTag == SlotTag;
	const bool bCurrentDirect = bUseCurrent
		&& Change.CurrentLocation.Type == EPEPItemLocationType::NamedSlot
		&& Change.CurrentLocation.LocationTag == SlotTag;

	if (bCurrentDirect)
	{
		ItemUpdate = EPEPEquipmentSlotItemUpdate::Refresh;
	}
	else if (bPreviousDirect)
	{
		ItemUpdate = EPEPEquipmentSlotItemUpdate::Clear;
	}

	return bPreviousDirect || bCurrentDirect
		|| (bUsePrevious && Change.PreviousRootSlotTag == SlotTag)
		|| (bUseCurrent && Change.CurrentRootSlotTag == SlotTag);
}

FPEPEquipmentSlotDisplayData UPEPEquipmentUILibrary::GetEquipmentSlotDisplayData(
	const UPEPEquipmentComponent* Equipment,
	const FGameplayTag SlotTag)
{
	FPEPEquipmentSlotDisplayData Data;
	Data.SlotTag = SlotTag;
	FPEPEquipmentSlotDefinition Slot;
	if (Equipment && Equipment->FindEquipmentSlotDefinition(SlotTag, Slot))
	{
		Data.bDeclared = true;
		Data.Hand = Slot.Hand;
		Data.bShowPublicly = Slot.bShowPublicly;
		Data.Label = Slot.DisplayName;
	}
	if (Data.Label.IsEmpty())
	{
		Data.Label = MakeDefaultSlotLabel(SlotTag);
	}
	return Data;
}

FText UPEPEquipmentUILibrary::MakeDefaultSlotLabel(const FGameplayTag SlotTag)
{
	if (!SlotTag.IsValid())
	{
		return FText::GetEmpty();
	}
	FString Label = SlotTag.ToString();
	int32 LastDot = INDEX_NONE;
	if (Label.FindLastChar(TEXT('.'), LastDot))
	{
		Label = Label.RightChop(LastDot + 1);
	}
	return FText::FromString(Label);
}

bool UPEPEquipmentUILibrary::MakeEquippedItemSnapshot(
	const UPEPEquipmentComponent* Equipment,
	const FPEPEquippedItemInfo& Node,
	FPEPItemInstance& OutItem)
{
	OutItem = FPEPItemInstance();
	if (!Node.ItemId.IsValid())
	{
		return false;
	}
	/* The authority and the owning client hold the item itself: stack, durability and all. */
	if (const IPEPItemStore* ItemStore = Equipment
		? Cast<IPEPItemStore>(Equipment->GetItemStoreObject())
		: nullptr)
	{
		if (const FPEPItemInstance* Held = ItemStore->FindItem(Node.ItemId))
		{
			OutItem = *Held;
			return true;
		}
	}
	/* Anyone else knows what the public node says, and nothing more. */
	OutItem.ItemId = Node.ItemId;
	OutItem.DefinitionId = Node.DefinitionId;
	return OutItem.IsValid();
}

bool UPEPEquipmentUILibrary::BuildEquippedItemViewData(
	const UPEPEquipmentComponent* Equipment,
	const FPEPEquippedItemInfo& Node,
	FPEPItemViewData& OutViewData)
{
	OutViewData = FPEPItemViewData();
	FPEPItemInstance Item;
	return MakeEquippedItemSnapshot(Equipment, Node, Item)
		&& UPEPItemUILibrary::BuildItemViewData(Item, OutViewData);
}

EPEPUIDropTargetVisualState UPEPEquipmentUILibrary::GetDropTargetVisualState(
	const bool bHasCandidate,
	const FPEPItemResult& Verdict)
{
	if (!bHasCandidate)
	{
		return EPEPUIDropTargetVisualState::Hovered;
	}
	return Verdict.bSuccess
		? EPEPUIDropTargetVisualState::Valid
		: EPEPUIDropTargetVisualState::Invalid;
}
