#pragma once

#include "CoreMinimal.h"
#include "Components/PEPInventoryTypes.h"

/**
 * Operations on saved item data, before any store has seen it.
 *
 * Deliberately C++ only, and deliberately free of any store: nothing here reads an inventory,
 * checks an authority or touches a component. It rewrites the placements a caller is holding, and
 * the caller decides what to do with the result — which is the same split PEPItemMutation states
 * for item state, one layer down.
 */
namespace PEPLoadoutMutation
{
	/**
	 * Drops every item whose definition no longer exists, and everything it was holding.
	 *
	 * A saved inventory outlives the assets it names. Without this a single removed definition
	 * fails a whole restore, and fails it as a placement error — a store cannot read a shape or a
	 * container slot off a definition it does not have, so it reports the location as invalid and
	 * never says why. Run this first and what remains only fails for real reasons.
	 *
	 * Destructive: an item dropped here is gone the moment the caller saves again, which is why
	 * the report exists and why nothing calls this on a caller's behalf.
	 *
	 * A missing definition is established against the asset registry, never against whether the
	 * asset happens to be loaded — those are different questions, and answering the second one
	 * would let a streaming hiccup delete a stash. When the registry cannot be consulted at all
	 * nothing is pruned, because absence of proof is not proof of absence.
	 */
	PEPINVENTORY_API void PruneMissingDefinitions(
		TArray<FPEPInventoryTreePlacement>& Placements,
		FPEPInventoryPruneReport& OutReport);
}
