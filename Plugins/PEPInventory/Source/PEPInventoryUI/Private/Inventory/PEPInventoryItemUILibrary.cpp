#include "Inventory/PEPInventoryItemUILibrary.h"

#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemQuery.h"
#include "Inventory/PEPInventoryGrid.h"
#include "Inventory/PEPInventorySettings.h"
#include "Item/Fragments/PEPItemFragment_InventoryShape.h"

FPEPItemLocation UPEPInventoryItemUILibrary::MakeItemContainerLocation(FPEPItemId ParentItemId,
	FName ContainerSlotName, FIntPoint GridPosition, bool bRotated)
{
	/* The unrotated origin carries no placement, so the same call also addresses a single slot. */
	return PEPInventoryGrid::MakeGridLocation(ParentItemId, ContainerSlotName, GridPosition, bRotated);
}

FPEPItemLocation UPEPInventoryItemUILibrary::MakeItemNamedSlotLocation(FGameplayTag SlotTag)
{
	return FPEPItemLocation::MakeNamedSlot(SlotTag);
}

bool UPEPInventoryItemUILibrary::GetContainerSlotDefinition(const UPEPItemDefinition* Definition,
	FName SlotName, FPEPContainerSlotDefinition& OutSlot)
{
	OutSlot = FPEPContainerSlotDefinition();
	const auto* Container = Definition ? Definition->GetDefinitionFragment<FPEPItemFragment_Container>() : nullptr;
	const auto* Slot = Container ? Container->FindSlot(SlotName) : nullptr;
	if (!Slot)
	{
		return false;
	}
	OutSlot = *Slot;
	return true;
}

TArray<FPEPContainerSlotDefinition> UPEPInventoryItemUILibrary::GetContainerContentSlots(const UPEPItemDefinition* Definition)
{
	TArray<FPEPContainerSlotDefinition> Result;
	const auto* Container = Definition ? Definition->GetDefinitionFragment<FPEPItemFragment_Container>() : nullptr;
	if (Container)
	{
		for (const auto& Slot : Container->Slots)
		{
			if (Slot.Purpose == EPEPContainerSlotPurpose::Content)
			{
				Result.Add(Slot);
			}
		}
	}
	return Result;
}

bool UPEPInventoryItemUILibrary::CalculateContainerPixelSize(const FPEPContainerSlotDefinition& Slot,
	float CellSize, FVector2D& OutPixelSize)
{
	OutPixelSize = FVector2D::ZeroVector;
	const FIntPoint Size = Slot.Mode == EPEPContainerSlotMode::Single ? FIntPoint(1, 1) : Slot.GridSize;
	if (!FMath::IsFinite(CellSize) || CellSize <= 0.f || Size.X <= 0 || Size.Y <= 0)
	{
		return false;
	}
	OutPixelSize = FVector2D(Size.X, Size.Y) * static_cast<double>(CellSize);
	return true;
}

bool UPEPInventoryItemUILibrary::BuildContainerItemViewData(const FPEPItemInstance& Item,
	const FPEPItemLocation& Location, float CellSize, FPEPContainerItemViewData& OutData)
{
	OutData = FPEPContainerItemViewData();
	OutData.ItemId = Item.ItemId;
	OutData.DefinitionId = Item.DefinitionId;
	const UPEPItemDefinition* Definition = Item.GetDefinitionIfLoaded();
	OutData.bDefinitionReady = Definition != nullptr;
	if (!Item.ItemId.IsValid() || !Definition || Location.Type != EPEPItemLocationType::ItemContainer
		|| !Location.IsStructurallyValid() || !FMath::IsFinite(CellSize) || CellSize <= 0.f)
	{
		return false;
	}
	FIntPoint Size;
	bool bCanRotate = false;
	if (!PEPInventorySettings().GetEffectiveInventoryShape(
		Definition->GetDefinitionFragment<FPEPItemFragment_InventoryShape>(), Size, bCanRotate)
		|| Size.X <= 0 || Size.Y <= 0)
	{
		return false;
	}
	/* A location without a grid placement is the grid's origin, unrotated, as the inventory reads it. */
	const FPEPGridPlacement Cell = PEPInventoryGrid::GetGridPlacement(Location);
	if (Cell.bRotated)
	{
		Swap(Size.X, Size.Y);
	}
	PEPItemQuery::GetDisplayDataFromDefinition(Definition, OutData.DisplayData);
	OutData.GridPosition = Cell.Position;
	OutData.GridSize = Size;
	OutData.bRotated = Cell.bRotated;
	OutData.PixelPosition = FVector2D(Cell.Position.X, Cell.Position.Y) * static_cast<double>(CellSize);
	OutData.PixelSize = FVector2D(Size.X, Size.Y) * static_cast<double>(CellSize);
	return true;
}
