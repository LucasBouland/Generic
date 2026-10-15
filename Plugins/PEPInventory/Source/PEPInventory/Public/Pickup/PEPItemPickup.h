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
	 *
	 * A pickup always answers to someone: a null RequestingController — an unpossessed pawn, an
	 * interactor with no controller — is refused with Reason_InvalidTarget rather than run with no
	 * identity. Server code moving a world item on nobody's behalf goes through the store directly
	 * with FPEPItemAccessContext::System(), where the bypass is written out.
	 */
	PEPINVENTORY_API FPEPItemResult CanPickupWorldItem(
		const APEPItemWorldActor* WorldItem,
		UPEPInventoryComponent* Inventory,
		const AController* RequestingController);

	/**
	 * Re-validates and applies the pickup, with the same refusals as CanPickupWorldItem.
	 *
	 * On success the item exists in Inventory only. A container actor's own store gives the tree
	 * up as part of the transfer; any other actor is emptied before the item is added, and given
	 * its tree back only if the store refuses it, so the item is never in both places — not even
	 * while the store announces the arrival. What happens to the now empty actor stays the
	 * caller's decision: it is not destroyed here.
	 */
	PEPINVENTORY_API FPEPItemResult ExecutePickupWorldItem(
		APEPItemWorldActor* WorldItem,
		UPEPInventoryComponent* Inventory,
		const AController* RequestingController);
}
