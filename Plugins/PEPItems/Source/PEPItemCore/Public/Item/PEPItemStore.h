#pragma once

#include "CoreMinimal.h"
#include "Core/PEPTypes.h"
#include "Item/PEPItemAccess.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemLocation.h"
#include "Item/PEPItemResultTypes.h"
#include "UObject/Interface.h"

#include "PEPItemStore.generated.h"

UENUM(BlueprintType)
enum class EPEPItemStoreChangeType : uint8
{
	Added,
	Removed,
	Moved,
	/** The item stayed where it was; its runtime state changed. */
	StateChanged
};

USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemStoreChange
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Store")
	FPEPItemId ItemId;

	/** Snapshot after Added/Moved/StateChanged, or immediately before removal for Removed.
	 * Local notification data, not a mutable reference to the store and not an extra replicated payload.
	 */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Store")
	FPEPItemInstance ItemInstance;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Store")
	EPEPItemStoreChangeType ChangeType = EPEPItemStoreChangeType::StateChanged;

	/** Meaningful for Removed and Moved. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Store")
	FPEPItemLocation PreviousLocation;

	/** Meaningful for Added, Moved and StateChanged. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Store")
	FPEPItemLocation CurrentLocation;

	/** Named-slot ancestor before removal or movement; invalid if there was no named-slot root. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Store")
	FGameplayTag PreviousRootSlotTag;

	/** Named-slot ancestor after addition, movement or state change. Local notification metadata. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Store")
	FGameplayTag CurrentRootSlotTag;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnPEPItemStoreChanged, const FPEPItemStoreChange& /*Change*/);

/**
 * Decides whether an item may occupy a location the store does not own the meaning of.
 * Registered per root tag by the domain plugin that gives that tag its semantics.
 */
DECLARE_DELEGATE_RetVal_TwoParams(
	FPEPItemResult,
	FPEPItemLocationValidator,
	const FPEPItemInstance& /*Item*/,
	const FPEPItemLocation& /*Location*/);

/**
 * Applied to a working copy of an item held by the store. Returning false, or changing the item's
 * identity, aborts the write. This is how domain code reaches PEPItemMutation on a stored item
 * without the store handing out a mutable pointer it can no longer track.
 */
using FPEPItemStoreMutation = TFunction<bool(FPEPItemInstance& /*Item*/)>;

UINTERFACE(MinimalAPI)
class UPEPItemStore : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything that owns items at locations: a grid inventory, a container actor, a one-slot mannequin.
 *
 * This is the whole contract equipment, weapons and UI need. Depending on it rather than on a
 * concrete inventory component keeps those plugins usable wherever a store exists — a preview
 * mannequin or a turret has no grid, no shapes and no weight budget, yet still holds items.
 *
 * Implementers are responsible for authority and for flagging replication; the read accessors are
 * safe anywhere, the write ones assume the caller has already established authority.
 */
class PEPITEMCORE_API IPEPItemStore
{
	GENERATED_BODY()

public:
	/** @return null when the store holds no such item. Valid only until the next write. */
	virtual const FPEPItemInstance* FindItem(const FPEPItemId& ItemId) const = 0;

	virtual bool FindItemLocation(const FPEPItemId& ItemId, FPEPItemLocation& OutLocation) const = 0;

	virtual bool FindItemAtLocation(const FPEPItemLocation& Location, FPEPItemId& OutItemId) const = 0;

	/**
	 * Returns the IDs of items whose location directly names ParentItemId.
	 * The output is reset first and never includes deeper descendants.
	 */
	virtual void GetDirectChildren(
		const FPEPItemId& ParentItemId,
		TArray<FPEPItemId>& OutChildItemIds) const = 0;

	/**
	 * The direct children the store is willing to show to everyone: what an equipment projection
	 * or a world actor may expose. Which slots are public is the store's own knowledge — in
	 * PEPInventory, a container's Attachment slots — so a consumer never has to read a container
	 * vocabulary it does not own. A store with no such notion exposes nothing. Output is reset.
	 */
	virtual void GetPublicChildren(
		const FPEPItemId& ParentItemId,
		TArray<FPEPItemId>& OutChildItemIds) const
	{
		OutChildItemIds.Reset();
	}

	virtual void GetItemIds(TArray<FPEPItemId>& OutItemIds) const
	{
		OutItemIds.Reset();
	}

	/** Whether ActionTag may reach ItemId through every containing item in this store. */
	virtual bool HasGrantedGameplayAccess(
		const FPEPItemId& ItemId,
		FGameplayTag ActionTag) const
	{
		return ItemId.IsValid() && ActionTag.IsValid();
	}

	/*
	 * Every write carries the identity it is made on behalf of. Access::System() is the server
	 * acting for no player — loot generation, a quest reward, an ability spending a charge — and
	 * anything answering to a player names that player, letting the store decide what it may do.
	 *
	 * No default argument, deliberately: a default on a virtual binds to the static type of the
	 * call, so an override could quietly supply a different one. The requester is always written
	 * out at the call site.
	 */

	virtual FPEPItemResult AddItem(
		const FPEPItemInstance& Item,
		const FPEPItemLocation& Destination,
		const FPEPItemAccessContext& Access) = 0;

	/** Performs every move rule without mutating the store. Intended for UI and placement previews. */
	virtual FPEPItemResult CanMoveItem(
		const FPEPItemId& ItemId,
		const FPEPItemLocation& Destination,
		const FPEPItemAccessContext& Access) const = 0;

	virtual FPEPItemResult MoveItem(
		const FPEPItemId& ItemId,
		const FPEPItemLocation& Destination,
		const FPEPItemAccessContext& Access) = 0;

	/** Validates exchanging the current locations of two items without mutating the store. */
	virtual FPEPItemResult CanSwapItems(
		const FPEPItemId& FirstItemId,
		const FPEPItemId& SecondItemId,
		const FPEPItemAccessContext& Access) const = 0;

	/** Atomically exchanges two current locations; observers see only the final state. */
	virtual FPEPItemResult SwapItems(
		const FPEPItemId& FirstItemId,
		const FPEPItemId& SecondItemId,
		const FPEPItemAccessContext& Access) = 0;

	/**
	 * Removes one item and nothing else. This is the destruction path for a depleted item, which
	 * by construction holds nothing: an implementer is expected to refuse an item that still has
	 * children rather than orphan them, so a loaded rig or a full corpse goes through
	 * RemoveItemTree instead.
	 */
	virtual FPEPItemResult RemoveItem(
		const FPEPItemId& ItemId,
		const FPEPItemAccessContext& Access) = 0;

	/**
	 * Removes the item together with every descendant. This is what a corpse despawning, a raid
	 * ending or a container being consumed needs, and the reason no caller has to write the
	 * traversal itself.
	 *
	 * The default walks the tree and removes leaves first, so no intermediate state ever points
	 * at a parent the store has already dropped. It is best effort: with nothing but RemoveItem
	 * to build on it cannot roll back, so it stops at the first refusal and reports it. A store
	 * that can validate the whole subtree up front should override this and be atomic.
	 */
	virtual FPEPItemResult RemoveItemTree(
		const FPEPItemId& ItemId,
		const FPEPItemAccessContext& Access);

	/**
	 * Runs Mutation against a working copy and commits it if the callback succeeds, then raises
	 * StateChanged. Callers pass a PEPItemMutation call here rather than writing state directly.
	 *
	 * Access is carried for attribution but not yet consulted: changing an item's own state is a
	 * different question from reaching into the slot holding it, and whether an outsider may do it
	 * belongs to the domain that defines the mutation rather than to container permissions.
	 */
	virtual FPEPItemResult MutateItem(
		const FPEPItemId& ItemId,
		const FPEPItemStoreMutation& Mutation,
		const FPEPItemAccessContext& Access) = 0;

	/**
	 * Hands responsibility for locations under RootLocationTag to a domain plugin.
	 * The store keeps owning storage; the registrant decides what may go where.
	 *
	 * No default for bAllowAutoPlacement, for the same reason as the access context above: a default
	 * on a virtual binds to the static type of the call. Whether automatic placement may pick these
	 * locations is the registrant's decision and is always written out at the call site.
	 *
	 * @return false when the tag is invalid or already claimed by another registrant.
	 */
	virtual bool RegisterLocationValidator(
		FGameplayTag RootLocationTag,
		FPEPItemLocationValidator Validator,
		bool bAllowAutoPlacement) = 0;

	/** @param BoundObject  Only releases the tag if the registered delegate is bound to this object. */
	virtual void UnregisterLocationValidator(FGameplayTag RootLocationTag, const UObject* BoundObject) = 0;

	virtual FOnPEPItemStoreChanged& OnItemStoreChanged() = 0;
};
