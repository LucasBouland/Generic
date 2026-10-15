#pragma once

#include "CoreMinimal.h"
#include "Core/PEPTypes.h"
#include "GameplayTagContainer.h"
#include "Item/PEPItemPlacement.h"
#include "StructUtils/InstancedStruct.h"

#include "PEPItemLocation.generated.h"

/**
 * Where an item sits inside a store. The vocabulary is deliberately generic: a store knows about
 * ownership slots, never about what they mean. Domain plugins give NamedSlot tags their semantics —
 * equipment, crafting stations, vehicle mounts — by registering a validator with the store.
 */
UENUM(BlueprintType)
enum class EPEPItemLocationType : uint8
{
	None,
	/** A container slot owned by another item, identified by ParentItemId + ContainerSlotName. */
	ItemContainer,
	/** A single named slot whose meaning belongs to a domain plugin, identified by LocationTag. */
	NamedSlot
};

USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemLocation
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item|Location")
	EPEPItemLocationType Type = EPEPItemLocationType::None;

	/**
	 * NamedSlot: Equipment.Slot.Head, Crafting.Slot.Input, ...
	 * ItemContainer uses ParentItemId + ContainerSlotName instead and leaves this unset.
	 */
	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item|Location")
	FGameplayTag LocationTag;

	/** Slot owned by ParentItemId when Type == ItemContainer. */
	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item|Location")
	FName ContainerSlotName;

	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item|Location")
	FPEPItemId ParentItemId;

	/**
	 * Where inside the container slot, in the terms of the plugin that owns the slot (a grid cell
	 * and orientation for PEPInventory). ItemCore stores, copies and compares it without reading
	 * it; the store holding the slot validates it. Empty when the slot needs no position, and
	 * always empty for a NamedSlot. Must derive from FPEPItemPlacement.
	 */
	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item|Location",
		meta=(BaseStruct="/Script/PEPItemCore.PEPItemPlacement"))
	FInstancedStruct Placement;

	/**
	 * Whether the fields are exactly those Type requires — a location carrying data belonging to
	 * another kind is rejected, not ignored. A placement is allowed on an ItemContainer location
	 * only, and only if it derives from FPEPItemPlacement; what it says is the slot owner's to
	 * judge. Says nothing about the slot actually existing.
	 */
	bool IsStructurallyValid() const;

	static FPEPItemLocation MakeItemContainer(
		FPEPItemId InParentItemId,
		FName InContainerSlotName);

	/** As above, with a placement for the store that owns the slot to interpret. */
	static FPEPItemLocation MakeItemContainer(
		FPEPItemId InParentItemId,
		FName InContainerSlotName,
		const FInstancedStruct& InPlacement);

	static FPEPItemLocation MakeNamedSlot(FGameplayTag SlotTag);
};

// Declared at namespace scope rather than as friends: a friend declaration carries no dll export,
// so modules outside PEPItemCore would fail to link against the comparison.
PEPITEMCORE_API bool operator==(const FPEPItemLocation& A, const FPEPItemLocation& B);

inline bool operator!=(const FPEPItemLocation& A, const FPEPItemLocation& B)
{
	return !(A == B);
}

/**
 * Consistent with operator==, but Placement is left out: ItemCore cannot hash a struct it does not
 * know. Equal locations still hash equal; locations differing only by placement share a hash,
 * which costs a set or map nothing but bucket sharing.
 */
PEPITEMCORE_API uint32 GetTypeHash(const FPEPItemLocation& Location);
