#include "Inventory/PEPInventoryQuery.h"

#include "Inventory/PEPInventorySettings.h"
#include "Item/PEPItemContainerQuery.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Item/Fragments/PEPItemFragment_InventoryShape.h"

namespace PEPInventoryQuery
{
	bool GetInventoryShapeData(const FPEPItemInstance& Item, FPEPItemInventoryShapeData& OutData)
	{
		OutData = FPEPItemInventoryShapeData();
		if (!Item.IsValid())
		{
			return false;
		}

		const FPEPItemFragment_InventoryShape* Shape =
			Item.GetDefinitionFragment<FPEPItemFragment_InventoryShape>();
		if (!PEPInventorySettings().GetEffectiveInventoryShape(Shape, OutData.Size, OutData.bCanRotate))
		{
			OutData = FPEPItemInventoryShapeData();
			return false;
		}

		OutData.bUsesDefaultShape = Shape == nullptr;
		OutData.bValid = true;
		return true;
	}
}

bool UPEPInventoryQueryLibrary::GetItemInventoryShapeData(
	const FPEPItemInstance& Item,
	FPEPItemInventoryShapeData& OutData)
{
	return PEPInventoryQuery::GetInventoryShapeData(Item, OutData);
}

bool UPEPInventoryQueryLibrary::GetDefinitionInventoryShapeData(
	const UPEPItemDefinition* Definition,
	FIntPoint& OutSize,
	bool& bOutCanRotate)
{
	/* No definition is no item, which has no footprint — not the project default one. */
	if (!Definition)
	{
		OutSize = FIntPoint::ZeroValue;
		bOutCanRotate = false;
		return false;
	}

	return PEPInventorySettings().GetEffectiveInventoryShape(
		Definition->GetDefinitionFragment<FPEPItemFragment_InventoryShape>(),
		OutSize,
		bOutCanRotate);
}

bool UPEPInventoryQueryLibrary::DoesItemHaveContainer(const FPEPItemInstance& Item)
{
	return PEPItemContainerQuery::IsContainer(Item);
}

bool UPEPInventoryQueryLibrary::DoesItemHaveContentSlots(const FPEPItemInstance& Item)
{
	const FPEPItemFragment_Container* Container = Item.GetDefinitionFragment<FPEPItemFragment_Container>();
	return Container && Container->Slots.ContainsByPredicate([](const FPEPContainerSlotDefinition& Slot)
	{
		return Slot.Purpose == EPEPContainerSlotPurpose::Content;
	});
}

bool UPEPInventoryQueryLibrary::DoesItemHaveAttachmentSlots(const FPEPItemInstance& Item)
{
	const FPEPItemFragment_Container* Container = Item.GetDefinitionFragment<FPEPItemFragment_Container>();
	return Container && Container->Slots.ContainsByPredicate([](const FPEPContainerSlotDefinition& Slot)
	{
		return Slot.Purpose == EPEPContainerSlotPurpose::Attachment;
	});
}

bool UPEPInventoryQueryLibrary::GetItemDefinitionContainerCapabilities(
	const UPEPItemDefinition* Definition,
	bool& bOutHasContentSlots,
	bool& bOutHasAttachmentSlots)
{
	bOutHasContentSlots = false;
	bOutHasAttachmentSlots = false;
	const FPEPItemFragment_Container* Container = Definition
		? Definition->GetDefinitionFragment<FPEPItemFragment_Container>()
		: nullptr;
	if (!Container)
	{
		return false;
	}
	for (const FPEPContainerSlotDefinition& Slot : Container->Slots)
	{
		bOutHasContentSlots |= Slot.Purpose == EPEPContainerSlotPurpose::Content;
		bOutHasAttachmentSlots |= Slot.Purpose == EPEPContainerSlotPurpose::Attachment;
		if (bOutHasContentSlots && bOutHasAttachmentSlots)
		{
			break;
		}
	}
	return true;
}
