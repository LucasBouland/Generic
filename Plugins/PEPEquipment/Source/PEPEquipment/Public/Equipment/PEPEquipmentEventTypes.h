#pragma once

#include "CoreMinimal.h"
#include "Equipment/PEPEquipmentTypes.h"
#include "GameplayTagContainer.h"

#include "PEPEquipmentEventTypes.generated.h"

UENUM(BlueprintType)
enum class EPEPEquipmentChangeType : uint8
{
	Equipped,
	Unequipped
};

/**
 * One public node entering or leaving the equipment projection, as seen by this machine.
 *
 * Derived by diffing the projection before and after a change, never sent over the wire: the
 * authority raises it when it rebuilds the projection, every client when the Fast Array lands.
 * Attachments are reported too; a consumer interested only in what occupies a slot filters on
 * Node.IsAttachment() (ParentItemId valid).
 */
USTRUCT(BlueprintType)
struct PEPEQUIPMENT_API FPEPEquipmentChange
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="PEP|Equipment|Event")
	EPEPEquipmentChangeType ChangeType = EPEPEquipmentChangeType::Equipped;

	/** The node as it is now for Equipped, as it was for Unequipped. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Equipment|Event")
	FPEPEquippedItemInfo Node;

	/**
	 * Equipment slot of the root this node hangs under: Node.SlotTag for a root, the ancestor's
	 * slot for an attachment. Lets an attachment consumer reason about where it ends up without
	 * walking the projection itself.
	 */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Equipment|Event", meta=(Categories="Equipment.Slot"))
	FGameplayTag RootSlotTag;

	/** Depth below the root: 0 for a slot occupant, 1 for its direct attachments, and so on. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Equipment|Event")
	int32 Depth = 0;
};

namespace PEPEquipmentEvents
{
	/**
	 * Whether two public nodes describe the same equipped state: same item and definition in the
	 * same slot or under the same parent slot. Everything a local visual is built from, too.
	 */
	PEPEQUIPMENT_API bool IsSameEquippedNode(
		const FPEPEquippedItemInfo& A,
		const FPEPEquippedItemInfo& B);

	/**
	 * Turns two projections into the changes between them, deterministically.
	 *
	 * A node whose slot, parent or definition changed counts as leaving then entering. That
	 * cascades: when a node leaves, every descendant it carried leaves with it, and when it enters,
	 * every descendant it carries enters, so an optic on a rifle moved from hand to back is
	 * re-announced under the rifle's new slot rather than staying silently attached to stale state.
	 *
	 * OutUnequipped is ordered deepest first (attachments before the root they hang from), then by
	 * ItemId; OutEquipped is ordered roots first, then by ItemId. The order depends only on the two
	 * projections, never on array order, so every machine announces the same change identically.
	 * Both outputs are reset.
	 */
	PEPEQUIPMENT_API void DiffPublicProjection(
		const TArray<FPEPEquippedItemInfo>& Previous,
		const TArray<FPEPEquippedItemInfo>& Current,
		TArray<FPEPEquipmentChange>& OutUnequipped,
		TArray<FPEPEquipmentChange>& OutEquipped);
}
