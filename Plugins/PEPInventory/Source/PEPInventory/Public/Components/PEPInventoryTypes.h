#pragma once

#include "CoreMinimal.h"
#include "Core/PEPTypes.h"
#include "Engine/AssetManagerTypes.h"
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PEP|Inventory|Tree")
	FPEPItemLocation RootLocation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PEP|Inventory|Tree")
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

/**
 * One stack a pickup would top up, and what that stack would hold afterwards.
 *
 * Planned before anything is written, so that deciding whether a pickup fits and performing it are
 * the same computation. Deciding with one and committing with another is how a full inventory came
 * to refuse ammunition it would in fact have absorbed.
 *
 * Plain C++ with no reflection: none of this reaches the wire or an editor, and it never outlives
 * the call that built it.
 */
struct FPEPInventoryStackMerge
{
	/** Named rather than indexed: the plan outlives the lookup, and an index would not survive it. */
	FPEPItemId TargetItemId;

	FPEPItemInstance MergedItem;

	/** Where the topped-up stack sits, which is what its StateChanged has to report. */
	FPEPItemLocation Location;
};
