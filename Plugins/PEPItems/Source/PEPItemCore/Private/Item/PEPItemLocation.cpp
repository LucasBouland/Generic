#include "Item/PEPItemLocation.h"

bool FPEPItemLocation::IsStructurallyValid() const
{
	// Each kind requires its own fields and forbids the others', so a location built for one kind
	// and retyped into another is rejected rather than silently carrying stale data.
	switch (Type)
	{
	case EPEPItemLocationType::ItemContainer:
		// The placement is the slot owner's to interpret, but it has to be a placement: any other
		// struct is refused here, so that no store has to wonder what it was handed.
		return !LocationTag.IsValid()
			&& !ContainerSlotName.IsNone()
			&& ParentItemId.IsValid()
			&& (!Placement.IsValid() || Placement.GetPtr<FPEPItemPlacement>() != nullptr);

	// A named slot holds one item, so there is no position within it to describe.
	case EPEPItemLocationType::NamedSlot:
		return LocationTag.IsValid()
			&& ContainerSlotName.IsNone()
			&& !ParentItemId.IsValid()
			&& !Placement.IsValid();

	case EPEPItemLocationType::None:
	default:
		return false;
	}
}

FPEPItemLocation FPEPItemLocation::MakeItemContainer(
	const FPEPItemId InParentItemId,
	const FName InContainerSlotName)
{
	FPEPItemLocation Location;
	Location.Type = EPEPItemLocationType::ItemContainer;
	Location.ParentItemId = InParentItemId;
	Location.ContainerSlotName = InContainerSlotName;
	return Location;
}

FPEPItemLocation FPEPItemLocation::MakeItemContainer(
	const FPEPItemId InParentItemId,
	const FName InContainerSlotName,
	const FInstancedStruct& InPlacement)
{
	FPEPItemLocation Location = MakeItemContainer(InParentItemId, InContainerSlotName);
	Location.Placement = InPlacement;
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
	// Placements compare by type, then through the type's own comparison; none equals none only.
	case EPEPItemLocationType::ItemContainer:
		return A.ParentItemId == B.ParentItemId
			&& A.ContainerSlotName == B.ContainerSlotName
			&& A.Placement == B.Placement;

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
	// Placement is left out (see the declaration): equal locations still hash equal.
	case EPEPItemLocationType::ItemContainer:
		Hash = HashCombine(Hash, GetTypeHash(Location.ParentItemId));
		Hash = HashCombine(Hash, GetTypeHash(Location.ContainerSlotName));
		break;

	case EPEPItemLocationType::NamedSlot:
		Hash = HashCombine(Hash, GetTypeHash(Location.LocationTag));
		break;

	default:
		break;
	}

	return Hash;
}
