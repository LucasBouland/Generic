#pragma once

#include "CoreMinimal.h"

class AActor;
class AController;
class APEPItemWorldActor;
class UPEPInventoryComponent;
struct FPEPItemResult;

/**
 * The store-side half of picking an item off the ground: checking authority, validating the
 * pickup tree against a store, and applying it. Deliberately free of any interaction vocabulary
 * — PEPInventory must not depend on PEPInteraction — so a project, or a thin integration plugin,
 * wires this to whatever triggers a pickup (a look-and-press interaction, an overlap, a quest
 * script) without re-deriving this logic each time.
 */
namespace PEPItemPickup
{
	/**
	 * The store an item picked up by Interactor should go into. Searched on the interactor and
	 * then on its controller, so a pawn-held inventory and one kept on the controller to survive
	 * respawns both resolve. A caller with a different place to look (player state, a vehicle)
	 * should resolve its own and call the overloads below directly instead.
	 */
	PEPINVENTORY_API UPEPInventoryComponent* ResolveInteractorInventory(AActor* Interactor);

	/** The identity the store will judge; never asserted by the caller. */
	PEPINVENTORY_API const AController* ResolveInteractorController(const AActor* Interactor);

	/**
	 * Whether Inventory could receive WorldItem right now, on behalf of RequestingController.
	 * Authority only: reads the item's private tree, which is empty rather than absent when read
	 * off a client.
	 */
	PEPINVENTORY_API FPEPItemResult CanPickupWorldItem(
		const APEPItemWorldActor* WorldItem,
		UPEPInventoryComponent* Inventory,
		const AController* RequestingController);

	/**
	 * Re-validates and applies the pickup. Does not touch WorldItem beyond reading its tree — the
	 * caller decides what happens to the actor once its contents are in the store.
	 */
	PEPINVENTORY_API FPEPItemResult ExecutePickupWorldItem(
		APEPItemWorldActor* WorldItem,
		UPEPInventoryComponent* Inventory,
		const AController* RequestingController);
}
