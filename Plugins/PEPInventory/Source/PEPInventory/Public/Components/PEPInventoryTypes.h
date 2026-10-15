#pragma once

#include "CoreMinimal.h"
#include "Core/PEPTypes.h"
#include "UObject/PrimaryAssetId.h"
#include "Item/PEPItemPresetTypes.h"
#include "Item/PEPItemStore.h"

#include "PEPInventoryTypes.generated.h"

/** Transfer sessions can mutate their remote store; inspection sessions expose an owner-only, read-only projection. */
UENUM(BlueprintType)
enum class EPEPInventorySessionMode : uint8
{
	None,
	Transfer,
	Inspect
};

/** Identifies one container slot: the owning item plus the slot's name on that item. */
using FPEPContainerSlotKey = TPair<FPEPItemId, FName>;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnPEPInventoryChangedBP,
	const FPEPItemStoreChange&,
	Change);

/**
 * Kept here with its sibling rather than beside the session state it announces: both are component
 * delegates, and the session type itself has nothing reflected in it.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPEPInventorySessionViewChanged);

/**
 * One subtree and the location its root is to occupy. This is the unit a restore works in: an
 * inventory is a forest of these, one per equipment slot, and the tree carries its own internal
 * layout so only the root ever needs placing.
 */
USTRUCT(BlueprintType)
struct PEPINVENTORY_API FPEPInventoryTreePlacement
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Inventory|Tree")
	FPEPItemLocation RootLocation;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Inventory|Tree")
	FPEPItemInstanceTree Tree;
};

/**
 * What a prune took out. It exists so the loss is reported rather than discovered: an item dropped
 * here is gone for good the moment the caller saves again, and nothing else will ever mention it.
 */
USTRUCT(BlueprintType)
struct PEPINVENTORY_API FPEPInventoryPruneReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="PEP|Inventory|Tree")
	TArray<FPEPItemId> RemovedItemIds;

	/** Each definition that could not be found, listed once however many items referenced it. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Inventory|Tree")
	TArray<FPrimaryAssetId> MissingDefinitions;

	bool IsEmpty() const
	{
		return RemovedItemIds.IsEmpty();
	}
};
