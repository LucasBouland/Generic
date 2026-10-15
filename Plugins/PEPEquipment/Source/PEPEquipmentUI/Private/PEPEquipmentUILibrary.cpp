#include "PEPEquipmentUILibrary.h"

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
