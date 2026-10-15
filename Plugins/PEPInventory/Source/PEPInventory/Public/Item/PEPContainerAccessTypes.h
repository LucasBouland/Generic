#pragma once

#include "CoreMinimal.h"
#include "Item/PEPItemAccess.h"
#include "PEPContainerAccessTypes.generated.h"

UENUM(BlueprintType)
enum class EPEPContainerSlotVisibility : uint8
{
	Hidden,
	Visible,
	ViewOnly,
	Lootable,
	OwnerOnly,
	RequiresSearch
};

UENUM(BlueprintType)
enum class EPEPContainerLootVisibility : uint8
{
	NotAContainer,
	Hidden,
	ShowContainerOnly,
	ShowContents,
	SearchThenShowContents,
	OwnerOnly
};

/**
 * How a store treats a requester that is not its owner — one rule for the whole store, so a shared
 * chest does not have to repeat the owner's permissions on the bOther* flags of every slot it holds.
 */
UENUM(BlueprintType)
enum class EPEPExternalAccessMode : uint8
{
	/** Each slot's bOther* flags decide. The policy a lootable body should get. */
	SlotRules,
	/** Every live requester resolves to Owner: a crate on the ground, a dropped bag. */
	SameAsOwner
};

/**
 * What one slot permits one role. Resolved through FPEPContainerSlotDefinition::GetPolicyFor so
 * that the owner/other choice is made in a single place; spread across the validation sites it
 * would only need to be wrong once to hand an outsider the owner's permissions.
 */
struct FPEPSlotAccessPolicy
{
	bool bCanTake = false;
	bool bCanPut = false;
	bool bCanMoveInside = false;
	EPEPContainerSlotVisibility Visibility = EPEPContainerSlotVisibility::Hidden;
};
