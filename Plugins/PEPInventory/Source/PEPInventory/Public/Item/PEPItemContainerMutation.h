#pragma once

#include "CoreMinimal.h"
#include "Core/PEPTypes.h"
#include "Item/PEPItemResultTypes.h"
#include "Item/PEPItemContainerRules.h"
#include "Item/PEPItemInstance.h"

/**
 * One shot: each entry point re-runs its rule against live state, then writes.
 *
 * Deliberately C++ only, like PEPItemMutation: these mutate item state with no authority check
 * of their own. The owning component checks authority and calls them.
 */
namespace PEPItemContainerMutation
{
	/** @param KeyItem  Optional; consulted only when the container declares unlock key tags. */
	PEPINVENTORY_API FPEPItemResult UnlockContainer(
		FPEPItemInstance& ContainerItem,
		const FPEPItemInstance* KeyItem);

	PEPINVENTORY_API FPEPItemResult LockContainer(
		FPEPItemInstance& ContainerItem);
}
