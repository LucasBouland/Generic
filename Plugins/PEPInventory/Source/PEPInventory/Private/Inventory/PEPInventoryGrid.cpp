#include "Inventory/PEPInventoryGrid.h"

namespace PEPInventoryGrid
{
	FPEPItemLocation MakeGridLocation(
		const FPEPItemId ParentItemId,
		const FName SlotName,
		const FIntPoint Position,
		const bool bRotated)
	{
		FPEPItemLocation Location = FPEPItemLocation::MakeItemContainer(ParentItemId, SlotName);
		SetGridPlacement(Location, Position, bRotated);
		return Location;
	}

	const FPEPGridPlacement* FindGridPlacement(const FPEPItemLocation& Location)
	{
		return Location.Placement.GetPtr<FPEPGridPlacement>();
	}

	FPEPGridPlacement GetGridPlacement(const FPEPItemLocation& Location)
	{
		const FPEPGridPlacement* Placement = FindGridPlacement(Location);
		return Placement ? *Placement : FPEPGridPlacement();
	}

	bool SetGridPlacement(FPEPItemLocation& Location, const FIntPoint Position, const bool bRotated)
	{
		if (Location.Type != EPEPItemLocationType::ItemContainer)
		{
			return false;
		}

		const FPEPGridPlacement Placement(Position, bRotated);
		if (Placement.IsOrigin())
		{
			/* One spelling per cell: the origin is no placement, as a location built without one. */
			Location.Placement.Reset();
		}
		else if (FPEPGridPlacement* Existing = Location.Placement.GetMutablePtr<FPEPGridPlacement>())
		{
			*Existing = Placement;
		}
		else
		{
			Location.Placement = FInstancedStruct::Make(Placement);
		}
		return true;
	}

	bool IsPlacementWellFormed(const FPEPItemLocation& Location)
	{
		if (!Location.Placement.IsValid())
		{
			return true;
		}

		const FPEPGridPlacement* Placement = FindGridPlacement(Location);
		return Placement
			&& Placement->Position.X >= 0
			&& Placement->Position.Y >= 0;
	}
}

FPEPItemLocation UPEPInventoryGridLibrary::MakeGridLocation(
	const FPEPItemId ParentItemId,
	const FName SlotName,
	const FIntPoint Position,
	const bool bRotated)
{
	return PEPInventoryGrid::MakeGridLocation(ParentItemId, SlotName, Position, bRotated);
}

bool UPEPInventoryGridLibrary::GetGridPlacement(
	const FPEPItemLocation& Location,
	FIntPoint& OutPosition,
	bool& bOutRotated)
{
	const FPEPGridPlacement Placement = PEPInventoryGrid::GetGridPlacement(Location);
	OutPosition = Placement.Position;
	bOutRotated = Placement.bRotated;

	return Location.Type == EPEPItemLocationType::ItemContainer
		&& (!Location.Placement.IsValid() || PEPInventoryGrid::FindGridPlacement(Location) != nullptr);
}

bool UPEPInventoryGridLibrary::SetGridPlacement(
	FPEPItemLocation& Location,
	const FIntPoint Position,
	const bool bRotated)
{
	return PEPInventoryGrid::SetGridPlacement(Location, Position, bRotated);
}
