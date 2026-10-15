#include "Item/PEPItemLocation.h"

bool FPEPItemLocation::IsStructurallyValid() const
{
	if (GridPosition.X < 0 || GridPosition.Y < 0)
	{
		return false;
	}

	// Each kind requires its own fields and forbids the others', so a location built for one kind
	// and retyped into another is rejected rather than silently carrying stale data.
	switch (Type)
	{
	case EPEPItemLocationType::ItemContainer:
		return !LocationTag.IsValid()
			&& !ContainerSlotName.IsNone()
			&& ParentItemId.IsValid();

	case EPEPItemLocationType::NamedSlot:
		return LocationTag.IsValid()
			&& ContainerSlotName.IsNone()
			&& !ParentItemId.IsValid()
			&& GridPosition == FIntPoint::ZeroValue
			&& !bRotated;

	case EPEPItemLocationType::None:
	default:
		return false;
	}
}

FPEPItemLocation FPEPItemLocation::MakeItemContainer(
	const FPEPItemId InParentItemId,
	const FName InContainerSlotName,
	const FIntPoint Position,
	const bool bInRotated)
{
	FPEPItemLocation Location;
	Location.Type = EPEPItemLocationType::ItemContainer;
	Location.ParentItemId = InParentItemId;
	Location.ContainerSlotName = InContainerSlotName;
	Location.GridPosition = Position;
	Location.bRotated = bInRotated;
	return Location;
}

FPEPItemLocation FPEPItemLocation::MakeNamedSlot(const FGameplayTag SlotTag)
{
	FPEPItemLocation Location;
	Location.Type = EPEPItemLocationType::NamedSlot;
	Location.LocationTag = SlotTag;
	return Location;
}

bool operator==(const FPEPItemLocation& A, const FPEPItemLocation& B)
{
	if (A.Type != B.Type)
	{
		return false;
	}

	switch (A.Type)
	{
	case EPEPItemLocationType::ItemContainer:
		return A.ParentItemId == B.ParentItemId
			&& A.ContainerSlotName == B.ContainerSlotName
			&& A.GridPosition == B.GridPosition
			&& A.bRotated == B.bRotated;

	// A named slot holds one item, so position and rotation are not part of its identity.
	case EPEPItemLocationType::NamedSlot:
		return A.LocationTag == B.LocationTag;

	case EPEPItemLocationType::None:
	default:
		return true;
	}
}

uint32 GetTypeHash(const FPEPItemLocation& Location)
{
	uint32 Hash = GetTypeHash(static_cast<uint8>(Location.Type));

	switch (Location.Type)
	{
	case EPEPItemLocationType::ItemContainer:
		Hash = HashCombine(Hash, GetTypeHash(Location.ParentItemId));
		Hash = HashCombine(Hash, GetTypeHash(Location.ContainerSlotName));
		Hash = HashCombine(Hash, GetTypeHash(Location.GridPosition));
		break;

	case EPEPItemLocationType::NamedSlot:
		Hash = HashCombine(Hash, GetTypeHash(Location.LocationTag));
		break;

	default:
		break;
	}

	return Hash;
}
