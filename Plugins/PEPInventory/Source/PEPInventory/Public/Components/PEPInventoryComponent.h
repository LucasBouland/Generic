#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/PEPInventoryRequest.h"
#include "Components/PEPInventoryTypes.h"
#include "Components/PEPInventoryValidation.h"
#include "Item/PEPContainerAccessTypes.h"
#include "Item/PEPItemAccess.h"
#include "Item/PEPItemStore.h"
#include "Net/PEPInventoryNetUtility.h"

#include "PEPInventoryComponent.generated.h"

class AController;
class APawn;
class APEPItemWorldActor;
class UPEPLoadoutPreset;
class UPEPItemDefinition;
struct FPEPContainerSlotDefinition;
/* Defined beside the only code that plans or reads one; see PEPInventoryComponent.cpp. */
struct FPEPInventoryStackMerge;

/**
 * A container-tree store of items, for anything that must be lootable.
 *
 * Items form a hierarchy: an ItemContainer location points at a parent item's slot, so a rig inside
 * a backpack inside a corpse is one tree. InventoryView is both the authoritative store and the
 * replication payload — one representation, so nothing can drift out of step with it.
 */
UCLASS(ClassGroup=(PEP), meta=(BlueprintSpawnableComponent))
class PEPINVENTORY_API UPEPInventoryComponent
	: public UActorComponent
	, public IPEPItemStore
{
	GENERATED_BODY()

	/*
	 * The replicated array reports its own deltas, and the entries reach back into the component to
	 * turn them into store events. That reach is theirs alone: raising a replication event by hand
	 * announces a change no delta made, so the entry points are closed to everyone else rather than
	 * left public because the serializer happens to need them.
	 */
	friend struct FPEPInventoryEntry;
	friend struct FPEPInventoryView;
	friend class APEPContainerItemWorldActor;
	/* The session component is the player's window into another store; it reads this one directly. */
	friend class UPEPInventorySessionComponent;
	bool RestoreWorldItemTree(FPEPItemInstanceTree Tree, FGameplayTag RootTag);

public:
	UPEPInventoryComponent();

	virtual void PostInitProperties() override;
	virtual void OnRegister() override;

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---- IPEPItemStore ----------------------------------------------------------------------

	virtual const FPEPItemInstance* FindItem(const FPEPItemId& ItemId) const override;

	virtual bool FindItemLocation(
		const FPEPItemId& ItemId,
		FPEPItemLocation& OutLocation) const override;

	virtual bool FindItemAtLocation(
		const FPEPItemLocation& Location,
		FPEPItemId& OutItemId) const override;

	virtual void GetDirectChildren(
		const FPEPItemId& ParentItemId,
		TArray<FPEPItemId>& OutChildItemIds) const override;

	/** Children sitting in the parent's Attachment slots. */
	virtual void GetPublicChildren(
		const FPEPItemId& ParentItemId,
		TArray<FPEPItemId>& OutChildItemIds) const override;

	virtual bool HasGrantedGameplayAccess(
		const FPEPItemId& ItemId,
		FGameplayTag ActionTag) const override;

	virtual FPEPItemResult AddItem(
		const FPEPItemInstance& Item,
		const FPEPItemLocation& Destination,
		const FPEPItemAccessContext& Access) override;

	virtual FPEPItemResult MoveItem(
		const FPEPItemId& ItemId,
		const FPEPItemLocation& Destination,
		const FPEPItemAccessContext& Access) override;

	virtual FPEPItemResult CanSwapItems(
		const FPEPItemId& FirstItemId,
		const FPEPItemId& SecondItemId,
		const FPEPItemAccessContext& Access) const override;

	virtual FPEPItemResult SwapItems(
		const FPEPItemId& FirstItemId,
		const FPEPItemId& SecondItemId,
		const FPEPItemAccessContext& Access) override;

	virtual FPEPItemResult RemoveItem(
		const FPEPItemId& ItemId,
		const FPEPItemAccessContext& Access) override;

	/** Atomic: the subtree is validated in full before a single entry leaves the store. */
	virtual FPEPItemResult RemoveItemTree(
		const FPEPItemId& ItemId,
		const FPEPItemAccessContext& Access) override;

	/**
	 * Applies Mutation to a working copy and commits it on success. If the result is depleted —
	 * a stack at zero, a resource at zero — the item is removed from the store and a Removed
	 * change is raised instead of StateChanged.
	 */
	virtual FPEPItemResult MutateItem(
		const FPEPItemId& ItemId,
		const FPEPItemStoreMutation& Mutation,
		const FPEPItemAccessContext& Access) override;

	/**
	 * Claims RootLocationTag for Validator. A tag already claimed by a live delegate bound to a
	 * different object is refused and left as it is: two domains answering for one slot is a setup
	 * fault, and the second silently replacing the first would hide it. The object that holds the
	 * claim may register again, which replaces its own binding — a component re-initializing.
	 */
	virtual bool RegisterLocationValidator(
		FGameplayTag RootLocationTag,
		FPEPItemLocationValidator Validator,
		bool bAllowAutoPlacement) override;

	virtual void UnregisterLocationValidator(
		FGameplayTag RootLocationTag,
		const UObject* BoundObject) override;

	virtual FOnPEPItemStoreChanged& OnItemStoreChanged() override
	{
		return ItemStoreChanged;
	}

	/** Auto-places a world container tree using the same pickup policy as a loose item. */
	FPEPItemResult CanTransferPickupToInventory(const UPEPInventoryComponent* TargetInventory,
		FPEPItemId ItemId, const FPEPItemAccessContext& Access) const;
	FPEPItemResult TransferPickupToInventory(UPEPInventoryComponent* TargetInventory,
		FPEPItemId ItemId, const FPEPItemAccessContext& Access);

	// ---- Access -----------------------------------------------------------------------------

	/**
	 * The controller this store answers to, or null when it answers to none — an unpossessed
	 * corpse, a crate on the ground. Both cases are deliberate: with no owning controller every
	 * requester resolves to External, which is exactly the policy a lootable body should get.
	 *
	 * Virtual so a persistent stash can keep naming its owner while that player possesses nothing.
	 */
	virtual const AController* GetOwningController() const;

	/**
	 * Owner when Access names this store's own controller, External for everyone else. Callers
	 * supply an identity and this decides the role; nothing outside the store may assert one.
	 */
	EPEPAccessRole ResolveAccessRole(const FPEPItemAccessContext& Access) const;

	/**
	 * How ResolveAccessRole treats everyone but the owner. Server policy: clients never read it
	 * from the remote store, only through the session's replicated copy.
	 *
	 * Read-only to Blueprint: a client-side script flipping it would change nothing the server
	 * decides, and a server-side one goes through SetExternalAccess, which says it is authority-only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Inventory|Access")
	EPEPExternalAccessMode ExternalAccess = EPEPExternalAccessMode::SlotRules;

	/** Changes the store's outsider policy. A session already open keeps the mode it sampled. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Inventory|Access")
	void SetExternalAccess(EPEPExternalAccessMode InExternalAccess);

	/**
	 * Presentation hint this inventory hands to whoever opens it: which loot panel the UI should
	 * build. Sampled when the session opens, replicated to the looter only. It grants no permission
	 * and names no vocabulary of its own — the project defines the tags and maps them to widgets.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PEP|Inventory|Session",
		meta=(Categories="Inventory.LootPanel"))
	FGameplayTag LootPanelTag;

	/**
	 * The requester behind a call made through this store's own owner: a listen server's local
	 * player, or server code acting for the store. It names the owning controller as a fact about
	 * how the call arrived, not as an assumption about who asked, which is why it is never taken
	 * from the caller.
	 *
	 * With no owning controller, the authority is the store acting for itself — a world container
	 * running its own pickup — and gets System(). A client in that position is nobody and gets a
	 * null requester, which resolves to External. Requests that arrived over the wire never take
	 * the System branch; see the server RPC implementations.
	 */
	FPEPItemAccessContext GetOwnerRequestContext() const;

	// ---- Inventory-specific -----------------------------------------------------------------

	/**
	 * Where this item goes when the player asks for it to be put away without saying where.
	 *
	 * The slot it already occupies is never the answer, and that has to be said explicitly:
	 * placement ignores the item itself when it looks for a conflict, so an equipped rifle
	 * validated its own slot, was moved there, and the call reported success having moved nothing.
	 */
	FPEPItemResult CanMoveItemAuto(
		FPEPItemId ItemId,
		FPEPItemLocation& OutDestination,
		const FPEPItemAccessContext& Access) const;

	FPEPItemResult MoveItemAuto(
		FPEPItemId ItemId,
		const FPEPItemAccessContext& Access);

	/* CREATE */
	FPEPItemResult CanAddItem(
		const FPEPItemInstance& Item,
		const FPEPItemLocation& Location,
		const FPEPItemAccessContext& Access) const;

	/**
	 * Adds an item the way taking one off the ground does: compatible stacks already carried are
	 * topped up first, and only what no stack could take needs a place of its own.
	 *
	 * That merging is the whole difference from AddItem, and it is why this pair does not simply
	 * resolve a location and defer. A pickup absorbed entirely into stacks needs no free cell, so a
	 * full inventory still accepts cartridges — asking for a location first would refuse them.
	 */
	FPEPItemResult CanAddPickupItem(
		const FPEPItemInstance& Item,
		const FPEPItemAccessContext& Access) const;

	/** Validates the pickup in full — identity, instance, catalogue, placement — before any write. */
	FPEPItemResult AddPickupItem(
		FPEPItemInstance Item,
		const FPEPItemAccessContext& Access);

	/**
	 * The same pickup for something that arrives whole: a rifle with its attachments, a preset, the
	 * contents of a carrier lying in the world.
	 *
	 * A tree of one takes the AddPickupItem path, stack merging included — a lone box of cartridges
	 * is an ordinary pickup however it reached the ground. A tree with children never merges: a rig
	 * holding things is not a quantity of rigs.
	 */
	FPEPItemResult CanAddPickupItemTree(
		const TArray<FPEPInventoryEntry>& ItemTree,
		const FPEPItemAccessContext& Access) const;

	/** Accepts the store-agnostic tree produced by an item preset or a world item actor. */
	FPEPItemResult CanAddPickupItemTree(
		const FPEPItemInstanceTree& ItemTree,
		const FPEPItemAccessContext& Access) const;

	FPEPItemResult AddPickupItemTree(
		TArray<FPEPInventoryEntry> ItemTree,
		const FPEPItemAccessContext& Access);

	FPEPItemResult AddPickupItemTree(
		FPEPItemInstanceTree ItemTree,
		const FPEPItemAccessContext& Access);

	/**
	 * Adds trees as one transaction: each is validated against a simulation already holding the
	 * trees before it, and nothing is written unless every one of them passes. On success the
	 * result names an item only when the call carried exactly one tree.
	 *
	 * Calling it once per tree is a different operation, and the difference is silent. Each call
	 * would validate against the live store, so two trees claiming the same cell each pass on their
	 * own and the second simply lands on the first. Restoring a saved inventory is the case this
	 * exists for: half a loadout committed is items lost or duplicated, with no way back.
	 */
	FPEPItemResult CanAddItemTrees(
		const TArray<FPEPInventoryTreePlacement>& Placements,
		const FPEPItemAccessContext& Access) const;

	FPEPItemResult AddItemTrees(
		TArray<FPEPInventoryTreePlacement> Placements,
		const FPEPItemAccessContext& Access);

	/**
	 * The store as a forest, one tree per root entry with every descendant it holds, each keeping
	 * the location its root occupies. The exact inverse of AddItemTrees: exported from one store and
	 * added to an empty one, entries and locations come back as they were. This is what a save
	 * writes; the id allocator state is saved beside it so restored ids stay unique.
	 *
	 * Copies every entry, so it is an executed node rather than a pure one a graph would re-run.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure=false, BlueprintAuthorityOnly, Category="PEP|Inventory|Tree")
	void ExportItemTrees(TArray<FPEPInventoryTreePlacement>& OutPlacements) const;

	/**
	 * Whether this store could wear the whole kit, without wearing any of it.
	 *
	 * Answers two different questions with one call, and says which: a recipe that cannot be built
	 * at all is a content fault, while a kit that builds but does not fit is a store answer. Both
	 * come back as a failure, so a caller checking before offering a loadout needs neither.
	 */
	FPEPItemResult CanApplyLoadout(
		const UPEPLoadoutPreset* Loadout,
		const FPEPItemAccessContext& Access) const;

	/**
	 * Outfits this store from a designer-authored loadout. Fresh instances every time, so the same
	 * asset can kit out a hundred pawns without any two sharing an item.
	 *
	 * All or nothing, because that is what a starting kit means: a rig on and the backpack refused
	 * is a loadout nobody authored, and the player would be the one to discover it. Handing out a
	 * kit on top of what someone already carries is a different operation and is not this one.
	 *
	 * Applies only once the named slots exist, which is to say after the component that claims
	 * them has registered — equipment does that when it initializes. Applied before, every root
	 * fails as an invalid location, and nothing says the slot simply was not there yet.
	 */
	FPEPItemResult ApplyLoadout(
		const UPEPLoadoutPreset* Loadout,
		const FPEPItemAccessContext& Access);

	/** The same application, acting for this store's own owner. See DropItemTreeForOwner. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Inventory|Loadout")
	FPEPItemResult ApplyLoadoutForOwner(const UPEPLoadoutPreset* Loadout);

	/**
	 * The one-tree case of AddItemTrees, which is what it delegates to: with a single tree there
	 * is nothing for a shared simulation to catch, so the two are equivalent.
	 *
	 * It places the tree at the location its root is to occupy rather than wherever it fits, which
	 * is what separates a restore from a pickup — a rig saved on the back that comes back on the
	 * chest is a different loadout, and nothing would say so.
	 */
	FPEPItemResult CanAddItemTreeAt(
		const FPEPItemInstanceTree& Tree,
		const FPEPItemLocation& RootLocation,
		const FPEPItemAccessContext& Access) const;

	FPEPItemResult AddItemTreeAt(
		FPEPItemInstanceTree Tree,
		const FPEPItemLocation& RootLocation,
		const FPEPItemAccessContext& Access);

	/*
	 * Pruning a saved forest of items whose definitions have gone lives in PEPLoadoutMutation. It
	 * reads no store, checks no authority and belongs to no instance, which is the line this
	 * plugin's item layer already draws between its namespaces and its components.
	 */

	/* READ — authoritative state. */
	bool ContainsItem(FPEPItemId ItemId) const;

	const FPEPInventoryEntry* FindEntry(FPEPItemId ItemId) const;

	/**
	 * Inventory entries for direct children, in the order they entered the parent. Answered from
	 * the parent-to-children index, so it costs what the parent holds rather than what the store
	 * does. Returned pointers remain valid until mutation.
	 */
	void GetDirectChildEntries(
		FPEPItemId ParentItemId,
		TArray<const FPEPInventoryEntry*>& OutChildren) const;

	/* Direct children installed in Single slots only. */
	void GetDirectSingleSlotChildren(
		FPEPItemId ParentItemId,
		TArray<const FPEPInventoryEntry*>& OutChildren) const;

	/* Intended for Single slots; Grid slots may contain several entries. */
	const FPEPInventoryEntry* FindItemInContainerSlot(
		FPEPItemId ParentItemId,
		FName SlotName) const;

	const FPEPInventoryView& GetInventoryView() const
	{
		return InventoryView;
	}

	/**
	 * Ids carry no nested allocation; entries do. Enumerating a store this way and resolving what
	 * is actually drawn costs one array, where copying the entries costs one allocation per item.
	 *
	 * Still one array the size of the store, so it is an executed node: a pure one is re-run for
	 * every pin that reads it.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category="PEP|Inventory")
	virtual void GetItemIds(TArray<FPEPItemId>& OutItemIds) const override;

	/** Copies the entry, nested state included; executed rather than pure for the same reason. */
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category="PEP|Inventory")
	bool FindInventoryEntry(FPEPItemId ItemId, FPEPInventoryEntry& OutEntry) const;

	/** Returns the loaded definition of a local inventory item, or null if absent/unloaded. Never loads assets. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory", meta=(DisplayName="Get Item Definition By Id"))
	const UPEPItemDefinition* GetItemDefinitionById(FPEPItemId ItemId) const;

	/**
	 * The occupants of one container slot — what a single panel draws. Answered from the slot
	 * index, so it costs what the slot holds rather than what the store does. Ordered by cell,
	 * row first, so a panel drawing them twice draws them the same way.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category="PEP|Inventory")
	void GetEntriesInContainerSlot(
		FPEPItemId ParentItemId,
		FName SlotName,
		TArray<FPEPInventoryEntry>& OutEntries) const;

	/* UPDATE — movement inside the same inventory. */
	UFUNCTION(BlueprintCallable, Category="PEP|Inventory")
	EPEPRequestResult RequestMoveItem(
		FPEPItemId ItemId,
		FPEPItemLocation Destination,
		int32& OutRequestId);

	/** Local-only validation for drag/drop previews. It never mutates replicated state. */
	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|Preview")
	FPEPItemResult PreviewMoveItem(
		FPEPItemId ItemId,
		FPEPItemLocation Destination) const;

	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|Stack")
	EPEPRequestResult RequestSplitStack(
		FPEPItemId SourceItemId,
		int32 SplitCount,
		FPEPItemLocation Destination,
		int32& OutRequestId);

	/** Allocates no id: the would-be stack carries PEPItemMutation's reserved preview id. */
	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|Preview")
	FPEPItemResult PreviewSplitStack(
		FPEPItemId SourceItemId,
		int32 SplitCount,
		FPEPItemLocation Destination) const;

	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|Stack")
	EPEPRequestResult RequestMergeStacks(
		FPEPItemId SourceItemId,
		FPEPItemId TargetItemId,
		int32 TransferCount,
		int32& OutRequestId);

	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|Preview")
	FPEPItemResult PreviewMergeStacks(
		FPEPItemId SourceItemId,
		FPEPItemId TargetItemId,
		int32 TransferCount) const;

	/** Allocates no id; the result names the source stack. Only SplitStack allocates one. */
	FPEPItemResult CanSplitStack(
		FPEPItemId SourceItemId,
		int32 SplitCount,
		const FPEPItemLocation& Destination,
		const FPEPItemAccessContext& Access) const;

	FPEPItemResult SplitStack(
		FPEPItemId SourceItemId,
		int32 SplitCount,
		const FPEPItemLocation& Destination,
		const FPEPItemAccessContext& Access);

	FPEPItemResult CanMergeStacks(
		FPEPItemId SourceItemId,
		FPEPItemId TargetItemId,
		int32 TransferCount,
		const FPEPItemAccessContext& Access) const;

	FPEPItemResult MergeStacks(
		FPEPItemId SourceItemId,
		FPEPItemId TargetItemId,
		int32 TransferCount,
		const FPEPItemAccessContext& Access);

	FPEPItemResult CanMoveItem(
		const FPEPItemId& ItemId,
		const FPEPItemLocation& Destination,
		const FPEPItemAccessContext& Access) const override;

	/* EXTRACT / DELETE */
	FPEPItemResult CanExtractItem(
		FPEPItemId ItemId,
		const FPEPItemAccessContext& Access) const;

	FPEPItemResult ExtractItem(
		FPEPItemId ItemId,
		FPEPInventoryEntry& OutEntry,
		const FPEPItemAccessContext& Access);

	FPEPItemResult CanExtractItemTree(
		FPEPItemId ItemId,
		const FPEPItemAccessContext& Access) const;

	FPEPItemResult ExtractItemTree(
		FPEPItemId ItemId,
		TArray<FPEPInventoryEntry>& OutEntries,
		const FPEPItemAccessContext& Access);

	FPEPItemResult DestroyItem(
		FPEPItemId ItemId,
		const FPEPItemAccessContext& Access);

	/**
	 * Everything a drop needs, decided without spawning anything: a carrier class to resolve, a
	 * world to put it in, and a subtree this requester may take.
	 *
	 * No transform, because where an item lands has no bearing on whether it may leave — the
	 * carrier is spawned with collision handling that never refuses a position.
	 */
	FPEPItemResult CanDropItemTree(
		FPEPItemId ItemId,
		const FPEPItemAccessContext& Access,
		TSubclassOf<APEPItemWorldActor> OverrideActorClass = nullptr) const;

	/**
	 * Takes an item and everything it holds out of the store and puts it into the world.
	 *
	 * The carrier class comes from UPEPInventorySettings unless one is named here — a corpse bag or a
	 * quest crate is a different actor, the item inside it is not.
	 *
	 * Ordered so that a failure cannot eat the item: the class, the world and the whole subtree are
	 * checked, then the actor is spawned, and only then does the store give the item up. A spawn
	 * that fails leaves the inventory untouched; an extraction that somehow fails after it destroys
	 * the actor rather than the items.
	 */
	FPEPItemResult DropItemTree(
		FPEPItemId ItemId,
		const FTransform& WorldTransform,
		const FPEPItemAccessContext& Access,
		APEPItemWorldActor*& OutSpawnedActor,
		TSubclassOf<APEPItemWorldActor> OverrideActorClass = nullptr);

	/**
	 * The same drop, acting for this store's own owner rather than an identity the caller supplies.
	 *
	 * The ForOwner suffix is the whole difference and marks every such pair on this component: the
	 * plain name takes an access context, the suffixed one derives it. A Blueprint cannot name a
	 * requester at all — FPEPItemAccessContext is unreflected precisely so it cannot — so the two
	 * forms exist for that reason and no other.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Inventory")
	FPEPItemResult DropItemTreeForOwner(
		FPEPItemId ItemId,
		FTransform WorldTransform,
		APEPItemWorldActor*& OutDroppedActor,
		TSubclassOf<APEPItemWorldActor> OverrideActorClass = nullptr);

	/**
	 * Destroys the item and everything it holds. Unlike DestroyItem this accepts a loaded
	 * container, which is what a despawning corpse or an ending raid has to be able to clear.
	 *
	 * Permission is read once, on the root, exactly as extracting the subtree does: whoever may
	 * take the rig may take what is inside it.
	 */
	FPEPItemResult CanDestroyItemTree(
		FPEPItemId ItemId,
		const FPEPItemAccessContext& Access) const;

	FPEPItemResult DestroyItemTree(
		FPEPItemId ItemId,
		const FPEPItemAccessContext& Access);

	UPROPERTY(BlueprintAssignable, Category="PEP|Inventory|Request")
	FOnPEPInventoryRequestResolved OnRequestResolved;

	UPROPERTY(BlueprintAssignable, Category="PEP|Inventory")
	FOnPEPInventoryChangedBP OnInventoryChanged;

	/**
	 * One identity, two stores, two roles: a player looting a corpse is External to the source and
	 * Owner of the destination. Each side resolves its own role from the same Access, which is why
	 * the requester is passed as an identity rather than as a role the caller would have to get
	 * right twice. Validation covers the complete subtree before either store is mutated.
	 */
	FPEPItemResult CanTransferItemToInventory(
		const UPEPInventoryComponent* TargetInventory,
		FPEPItemId ItemId,
		const FPEPItemLocation& Destination,
		const FPEPItemAccessContext& Access) const;

	FPEPItemResult TransferItemToInventory(
		UPEPInventoryComponent* TargetInventory,
		FPEPItemId ItemId,
		const FPEPItemLocation& Destination,
		const FPEPItemAccessContext& Access);

protected:
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_RequestMoveItem(
		FPEPItemId ItemId,
		FPEPItemLocation Destination,
		int32 RequestId);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_RequestSplitStack(
		FPEPItemId SourceItemId,
		int32 SplitCount,
		FPEPItemLocation Destination,
		int32 RequestId);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_RequestMergeStacks(
		FPEPItemId SourceItemId,
		FPEPItemId TargetItemId,
		int32 TransferCount,
		int32 RequestId);

	UFUNCTION(Client, Reliable)
	void Client_ResolveInventoryRequest(FPEPInventoryRequestResult RequestResult);

	/*
	 * Everything below is implementation. Nothing in the repository derives from this component,
	 * and the types that need to reach in — the fast array, the session, the world container — are
	 * named friends at the top of the class; a subclass would otherwise inherit the ability to
	 * write the store without validating, which is exactly what the Commit functions are.
	 */
private:
	// ---- Storage and the indices over it ----------------------------------------------------

	/**
	 * Authoritative store and replication payload at once. Replicated to the owner only; other
	 * viewers are served by PEPEquipment's public projection, or by an explicit loot session.
	 */
	UPROPERTY(Replicated)
	FPEPInventoryView InventoryView;

	/*
	 * The indices are maintained write by write on the authority: an add, a move and a removal each
	 * touch the handful of index entries the item appears in, so a subtree of K items costs K small
	 * updates rather than K rebuilds of everything. Only the location indices are keyed by
	 * location; everything that names an entry names it by id, so a removal that swaps the last
	 * entry into the hole has exactly one position to repair — in EntryIndex.
	 *
	 * The two flags are the wholesale fallback: whatever rewrites Entries without passing through
	 * the writers below — a replication receive, a scratch copy, a reset — sets them, and the next
	 * read rebuilds from scratch. While a flag is set the writers leave its indices alone.
	 */

	/** ItemId to position in InventoryView.Entries. */
	mutable TMap<FPEPItemId, int32> EntryIndex;

	/**
	 * Occupants of each container slot. Placement asks "what else is in this slot" for every
	 * candidate cell, which would otherwise walk the whole store each time.
	 */
	mutable TMap<FPEPContainerSlotKey, TArray<FPEPItemId>> ContainerSlotIndex;

	/**
	 * Occupants of each named slot. Equipment asks "what is in this slot" for every slot on every
	 * store change, and every placement onto a named slot asks whether it is free. An array per
	 * tag, so a slot that ever holds two items still answers right.
	 */
	mutable TMap<FGameplayTag, TArray<FPEPItemId>> NamedSlotIndex;

	/**
	 * Direct children of each container item, in the order they entered it. Trees are walked
	 * downwards — snapshots, exports, transfers, drops — and each step asks only this.
	 */
	mutable TMap<FPEPItemId, TArray<FPEPItemId>> ChildIndex;

	/**
	 * Which item sits on each cell of a grid slot. Placement asks "is this footprint free" once
	 * per candidate cell, and reading cells beats re-deriving every occupant's shape for each one.
	 *
	 * Built per slot on first use and dropped whenever that slot's occupants change, so a write
	 * costs nothing here and the next read rebuilds one slot, never the store.
	 *
	 * Unknown when an occupant's shape could not be resolved: the caller then falls back to the
	 * overlap test, which re-resolves on every call and fails the way it always did.
	 */
	struct FPEPGridOccupancy
	{
		FIntPoint GridSize = FIntPoint::ZeroValue;
		/** Occupant per cell, row-major; an invalid id when free. */
		TArray<FPEPItemId> CellOccupants;
		int32 FreeCells = 0;
		bool bUnknown = false;

		FPEPItemId OccupantAt(const FIntPoint Cell) const
		{
			return CellOccupants[Cell.Y * GridSize.X + Cell.X];
		}
	};
	mutable TMap<FPEPContainerSlotKey, FPEPGridOccupancy> GridOccupancy;

	/**
	 * The slot's cell map, built on demand. Null for a slot with no occupants, or one that is not a
	 * grid. The pointer is valid until the next call for a different slot or the next write.
	 */
	const FPEPGridOccupancy* FindGridOccupancy(FPEPItemId ParentItemId, FName SlotName) const;

	/** Whether Size at Position lies inside the map and covers no cell held by anyone not ignored. */
	static bool IsGridFootprintFree(
		const FPEPGridOccupancy& Occupancy,
		FIntPoint Position,
		FIntPoint Size,
		FPEPItemId IgnoredItemId,
		FPEPItemId AlsoIgnoredItemId);

	/** Named slots open to automatic placement, sorted. Changes only when a validator does. */
	mutable TArray<FGameplayTag> AutoPlacementSlotTags;
	mutable bool bAutoPlacementSlotTagsDirty = true;
	const TArray<FGameplayTag>& GetAutoPlacementSlotTags() const;

	mutable bool bEntryIndexDirty = true;
	mutable bool bContainerSlotIndexDirty = true;

	/*
	 * Entries.Num() as the indices last saw it. A store whose size changed without the writers
	 * hearing of it — a delta applied in an order the receive batch did not expect — is rebuilt
	 * rather than trusted, the way the session guards its own mirror.
	 */
	mutable int32 IndexedEntryCount = 0;

	/** Rebuilds whichever indices are flagged, in a single pass over the store. */
	void RefreshIndices() const;

	/** Flags every index for a rebuild. For writes that bypass the writers below. */
	void MarkAllIndicesDirty() const;

	/** EntryIndex lookup without the freshness check; for loops that have already refreshed. */
	const FPEPInventoryEntry* FindIndexedEntry(FPEPItemId ItemId) const;

	/*
	 * The location indices, raw. The Index/Unindex pair below is what writers call: it does nothing
	 * while a rebuild is pending, since the rebuild will see the write anyway.
	 */
	void AddToLocationIndices(FPEPItemId ItemId, const FPEPItemLocation& Location) const;
	void RemoveFromLocationIndices(FPEPItemId ItemId, const FPEPItemLocation& Location) const;
	void IndexEntryLocation(FPEPItemId ItemId, const FPEPItemLocation& Location) const;
	void UnindexEntryLocation(FPEPItemId ItemId, const FPEPItemLocation& Location) const;

	/*
	 * The ways the store is written, and the only ones. Anything that reaches Entries or an entry's
	 * location outside these leaves an index describing a store that no longer exists.
	 */

	/** Appends an entry and indexes it. The reference is valid until the next write. */
	FPEPInventoryEntry& AppendEntry(FPEPInventoryEntry&& Entry);

	/** Removes the entry at Index by swapping the last one into its place, and repairs the indices. */
	void RemoveEntryAtSwap(int32 Index, FPEPInventoryEntry& OutEntry);

	/** Empties the store wholesale. */
	void ResetEntries();

	/** Moves an entry. Separate from a state write, which invalidates no index at all. */
	void SetEntryLocation(FPEPInventoryEntry& Entry, const FPEPItemLocation& Location);

	/** Entries sharing a container slot, by id, in no particular order. Null when the slot is free. */
	const TArray<FPEPItemId>* FindContainerSlotOccupants(
		FPEPItemId ParentItemId,
		FName SlotName) const;

	FPEPInventoryEntry* FindMutableEntry(FPEPItemId ItemId);

	TArray<FPEPLocationValidatorBinding> LocationValidators;

	// ---- Requests and replication -----------------------------------------------------------

	bool HasMutationAuthority() const;
	int32 AllocateRequestId();

	/**
	 * The requester behind a server RPC. The engine routes those only from the connection owning
	 * this actor, so the owning controller is who sent it — and when there is none, the sender is
	 * nobody this store answers to. Unlike GetOwnerRequestContext it never falls back to System():
	 * a request that crossed the wire is a player's, never the server's own.
	 */
	FPEPItemAccessContext GetRemoteRequestContext() const;

	/**
	 * Answers a request: by Client RPC when this component has an owning connection to answer on,
	 * by local broadcast otherwise — a standalone game, or an unowned store whose RPC would be
	 * dropped for want of a connection and leave the caller waiting.
	 */
	void ResolveInventoryRequest(
		int32 RequestId,
		EPEPInventoryRequestKind Kind,
		const FPEPItemResult& Result);

	/*
	 * Reached only by the fast array, through the friendship declared at the top of this class.
	 *
	 * A receive applies the whole delta to Entries before the first callback runs, and nothing
	 * changes them again until the removals are applied after the last one. So the indices are
	 * flagged once, on the first callback of a receive, rebuilt by whichever read comes first, and
	 * read as they are by every callback after it; the receive closes in PostReplicatedReceive,
	 * once the removals have reshaped the array. Flagging on every callback was a full rebuild per
	 * entry — quadratic on the initial join of a full inventory.
	 */
	void BeginReplicatedReceive();
	bool bReplicatedReceiveOpen = false;
	/* Frame the receive opened on; a receive left open by a lost PostReplicatedReceive expires. */
	uint64 ReplicatedReceiveFrame = 0;

	void HandleReplicatedEntryAdded(const FPEPInventoryEntry& Entry);

	void HandleReplicatedEntryChanged(
		const FPEPInventoryEntry& Entry,
		const FPEPItemLocation& PreviousLocation);

	void HandleReplicatedEntryRemoved(const FPEPInventoryEntry& Entry);
	void QueueReplicatedEntryRemoved(const FPEPInventoryEntry& Entry);
	void FlushReplicatedEntryRemovals();

	int32 NextRequestId = 1;
	TArray<FPEPInventoryEntry> PendingReplicatedRemovals;
	TMap<FPEPItemId, FGameplayTag> PendingRemovedRootSlotTags;
	// Previous complete replication graph; refreshed after the delta's callbacks finish.
	TMap<FPEPItemId, FPEPItemLocation> LastReplicatedLocations;

	FOnPEPItemStoreChanged ItemStoreChanged;

	// ---- Plans: decide everything, write nothing ---------------------------------------------

	/**
	 * Never allocates an id: the would-be stack is built by PEPItemMutation::PreviewSplitStack and
	 * carries the reserved preview id, so a preview, a hover or a refused request consumes nothing.
	 * The result names the source stack. SplitStack alone performs the allocating split, after
	 * this has approved it.
	 */
	FPEPItemResult BuildSplitStackPlan(
		FPEPItemId SourceItemId,
		int32 SplitCount,
		const FPEPItemLocation& Destination,
		const FPEPItemAccessContext& Access,
		FPEPItemInstance& OutSourceItem,
		FPEPItemInstance& OutSplitItem) const;

	FPEPItemResult BuildMergeStacksPlan(
		FPEPItemId SourceItemId,
		FPEPItemId TargetItemId,
		int32 TransferCount,
		const FPEPItemAccessContext& Access,
		FPEPItemInstance& OutSourceItem,
		FPEPItemInstance& OutTargetItem,
		int32& OutTransferredCount) const;

	/**
	 * The whole pickup decision, shared by CanAddPickupItem and AddPickupItem so the two cannot
	 * disagree: the item itself (valid, well-formed, not already stored, catalogue ready), then an
	 * attachment mount, then stack top-ups, then a place for whatever is left.
	 *
	 * @param OutRemainder      What would enter the store as a new entry.
	 * @param OutDestination    Where it would go; meaningful only when bOutAddRemainder.
	 * @param bOutAddRemainder  False when the merges absorb the pickup whole.
	 */
	FPEPItemResult PlanPickupItem(
		const FPEPItemInstance& Item,
		EPEPAccessRole Role,
		FPEPItemInstance& OutRemainder,
		TArray<FPEPInventoryStackMerge>& OutMerges,
		FPEPItemLocation& OutDestination,
		bool& bOutAddRemainder) const;

	/**
	 * Fills existing compatible stacks from Item, writing nothing. Item is left holding whatever no
	 * stack could take, so a caller that finds it depleted knows the pickup needs no room at all.
	 * Stacks are offered in item-id order, so the same store tops up the same stacks every time.
	 */
	void PlanPickupStackMerges(
		FPEPItemInstance& Item,
		EPEPAccessRole Role,
		TArray<FPEPInventoryStackMerge>& OutMerges) const;

	/**
	 * CanTransferItemToInventory, keeping the subtree it validated so the commit that follows moves
	 * exactly that and need not take a second snapshot.
	 */
	FPEPItemResult ValidateTransfer(
		const UPEPInventoryComponent* TargetInventory,
		FPEPItemId ItemId,
		const FPEPItemLocation& Destination,
		const FPEPItemAccessContext& Access,
		TArray<FPEPInventoryEntry>* OutItemTree) const;

	/** The pickup placement in TargetInventory, resolved once, and the transfer to it validated. */
	FPEPItemResult ResolvePickupTransfer(
		const UPEPInventoryComponent* TargetInventory,
		FPEPItemId ItemId,
		const FPEPItemAccessContext& Access,
		FPEPItemLocation& OutDestination,
		TArray<FPEPInventoryEntry>* OutItemTree) const;

	// ---- Validation -------------------------------------------------------------------------

	FPEPItemResult ValidateMoveSourceAccess(
		const FPEPInventoryEntry& Entry,
		const FPEPItemLocation& Destination,
		EPEPAccessRole Role,
		bool& bOutInternalContainerMove) const;

	/*
	 * The validation helpers take an already-resolved role rather than the access context: the
	 * translation from identity to role happens once, at the public entry point, so no helper can
	 * reach a different verdict about the same request.
	 */

	/**
	 * @param AlsoIgnoredItemId  A second occupant that does not count, besides Item itself. A swap
	 *                           validates each item at the other's location: once exchanged,
	 *                           neither sits where it did, so the other's old cells are free.
	 */
	FPEPItemResult ValidateLocationForItem(
		const FPEPItemInstance& Item,
		const FPEPItemLocation& Location,
		EPEPAccessRole Role,
		bool bInternalContainerMove = false,
		FPEPItemId AlsoIgnoredItemId = FPEPItemId()) const;

	const FPEPContainerSlotDefinition* ResolveContainerSlot(
		const FPEPItemLocation& Location,
		bool& bOutLocked) const;

	FPEPItemResult ValidateCanTakeFromLocation(
		const FPEPInventoryEntry& Entry,
		EPEPAccessRole Role) const;

	/**
	 * Whether an outsider can reach Entry at all: every container above it shows its contents to
	 * Role, every slot on the way shows its occupants, and none of those containers is locked.
	 * The slot rules only ever looked at the slot an item sits in, so an item in a hidden pocket,
	 * or in an open pouch inside a locked backpack, was takeable by a looter who could not even
	 * see it. Owners are not restricted here; their access is the slot rules alone.
	 *
	 * Walked once, upwards, bounded by the store size so a corrupt cycle ends rather than hangs.
	 * Invisible answers InvalidTarget, so a refusal does not confirm the item exists; locked
	 * answers BlockedByState.
	 */
	FPEPItemResult ValidateExternalReach(
		const FPEPInventoryEntry& Entry,
		EPEPAccessRole Role) const;

	/**
	 * Whether Role may add to what already sits at Location. Topping up a stack in place puts items
	 * somewhere just as surely as placing a new one does, but it reaches no free cell and so never
	 * passes through ValidateLocationForItem; without this it would answer to no permission at all.
	 */
	bool CanDepositIntoLocation(
		const FPEPItemLocation& Location,
		EPEPAccessRole Role) const;

	FPEPItemResult ValidateExternalLocation(
		const FPEPItemInstance& Item,
		const FPEPItemLocation& Location) const;

	/**
	 * Where this store puts an item nobody named a destination for: a compatible named slot first,
	 * then the grids of what it already holds, breadth-first so a rig's own grid is offered before
	 * that of a pouch inside it — and so the pouch is offered at all.
	 *
	 * Every step is ordered explicitly. The entry array is not: RemoveAtSwap moves survivors
	 * around, so walking it directly would let the same item land in a different pouch depending on
	 * what had been taken out of the inventory earlier.
	 *
	 * @param ExcludedLocation  A slot the answer may not be in, whichever cell of it. An automatic
	 *                          move passes the location the item is leaving; a pickup leaves
	 *                          nowhere and passes the default, which excludes nothing.
	 */
	bool TryResolveAutoPlacement(
		const FPEPItemInstance& Item,
		FPEPItemLocation& OutLocation,
		EPEPAccessRole Role,
		const FPEPItemLocation& ExcludedLocation = FPEPItemLocation()) const;

	/** Direct attachment slots on equipped roots; validates tags, occupancy and access. */
	bool TryResolveAttachmentPlacement(
		const FPEPItemInstance& Item,
		FPEPItemLocation& OutLocation,
		EPEPAccessRole Role) const;

	/** Pickup-only priority: attachment slots on named-slot occupants, then ordinary placement. */
	bool TryResolvePickupPlacement(
		const FPEPItemInstance& Item,
		FPEPItemLocation& OutLocation,
		EPEPAccessRole Role) const;

	/** Named-slot occupants, ordered by slot tag: where an automatic placement starts looking. */
	void AppendOrderedNamedSlotItems(TArray<FPEPItemId>& OutItemIds) const;

	/** Direct children of ParentItemId, ordered by the slot and the cell they occupy. */
	void AppendOrderedChildItems(
		FPEPItemId ParentItemId,
		TArray<FPEPItemId>& OutItemIds) const;

	bool WouldCreateCycle(
		FPEPItemId MovingItemId,
		FPEPItemId ParentItemId) const;

	bool HasChildren(FPEPItemId ParentItemId) const;

	/**
	 * Every descendant of ParentItemId, depth-first with each parent before its children. Walked
	 * with an explicit stack and bounded by the store size, so neither a deep tree nor a corrupt
	 * cycle can exhaust anything.
	 */
	void AppendDescendantEntries(
		FPEPItemId ParentItemId,
		TArray<const FPEPInventoryEntry*>& OutEntries) const;

	bool BuildItemTreeSnapshot(
		FPEPItemId RootItemId,
		TArray<FPEPInventoryEntry>& OutEntries) const;

	FPEPItemResult ValidatePickupItemTree(
		const TArray<FPEPInventoryEntry>& ItemTree,
		FPEPItemLocation& OutRootLocation,
		const FPEPItemAccessContext& Access) const;

	/**
	 * Validates a complete tree while replacing only its root location. Never mutates this store.
	 * A tree of one is decided against the live store directly — with no siblings or children to
	 * simulate, a copy would answer exactly the same — so only a real tree pays for a scratch.
	 */
	FPEPItemResult ValidateItemTreeAtRootLocation(
		const TArray<FPEPInventoryEntry>& ItemTree,
		const FPEPItemLocation& RootLocation,
		const FPEPItemAccessContext& Access,
		bool bValidateChildPlacement = true) const;

	/**
	 * A throwaway copy of this store, used to decide where things land before anything is written.
	 * It carries the entries and the location validators, and nothing else: it has no owner, so it
	 * can resolve no access role of its own, which is why callers resolve the role here and pass it
	 * in. Held through a strong pointer by every caller — an unreferenced UObject is collectable,
	 * and one that lives across several validations would eventually be collected mid-use.
	 *
	 * One per operation at most; a forest shares one. It stays a full copy rather than an overlay
	 * of pending entries on the live store, deliberately. The location validators are domain
	 * callbacks this store cannot see into, and every placement query — cycles, slot occupancy,
	 * cell maps, named slots — would need an overlay-aware twin, a second implementation of the
	 * validation it exists to reuse. A copy runs the one implementation on a store that simply
	 * holds more; the cost is paid only by trees, never by single items.
	 *
	 * @param bCopyEntries  False for an empty simulation that keeps only the validators.
	 */
	UPEPInventoryComponent* CreateValidationScratch(bool bCopyEntries = true) const;

	/**
	 * The tree validation proper, against a caller-owned simulation. Reusing one scratch across
	 * several trees is what makes a forest a single decision: each tree sees the ones before it.
	 *
	 * On success Scratch holds the tree, so a caller validating more trees keeps going with it.
	 */
	FPEPItemResult ValidateItemTreeAgainstScratch(
		UPEPInventoryComponent& Scratch,
		const TArray<FPEPInventoryEntry>& ItemTree,
		const FPEPItemLocation& RootLocation,
		EPEPAccessRole Role,
		bool bValidateChildPlacement) const;

	/**
	 * Whether Item could enter Store as a new entry at all, before any placement: a valid,
	 * well-formed instance whose id Store does not already hold. Shared by the tree paths so a
	 * lone item and a tree member are judged alike.
	 */
	static FPEPItemResult ValidateIncomingItem(
		const UPEPInventoryComponent& Store,
		const FPEPItemInstance& Item);

	bool IsNamedSlotOccupied(
		FGameplayTag SlotTag,
		FPEPItemId IgnoredItemId,
		FPEPItemId AlsoIgnoredItemId = FPEPItemId()) const;

	bool IsItemContainerSlotOccupied(
		FPEPItemId ParentItemId,
		FName SlotName,
		FPEPItemId IgnoredItemId,
		FPEPItemId AlsoIgnoredItemId = FPEPItemId()) const;

	// ---- Commits: write what a plan approved -------------------------------------------------

	/**
	 * Writes an already-validated tree, parents before children. Validates nothing: the caller has
	 * already earned the write.
	 *
	 * Appends what it wrote rather than announcing it, so a caller committing several trees can
	 * finish the whole transaction before any observer looks. An observer reading the store from
	 * an Added raised halfway through a forest would see a loadout that never existed.
	 */
	void CommitAddItemTree(
		TArray<FPEPInventoryEntry>& ItemTree,
		const FPEPItemLocation& RootLocation,
		TArray<TPair<FPEPItemId, FPEPItemLocation>>& OutAdded);

	void EmitAddedEntries(
		const TArray<TPair<FPEPItemId, FPEPItemLocation>>& Added);

	/** The move half of MoveItem, MoveItemAuto and a same-store transfer, once one has validated it. */
	FPEPItemResult CommitValidatedMove(
		FPEPItemId ItemId,
		const FPEPItemLocation& Destination);

	/** Moves an already-validated subtree, as snapshotted by ValidateTransfer, into TargetInventory. */
	FPEPItemResult CommitValidatedTransfer(
		UPEPInventoryComponent* TargetInventory,
		FPEPItemId ItemId,
		const FPEPItemLocation& Destination,
		TArray<FPEPInventoryEntry>&& ItemTree);

	/** Flattens a store-agnostic tree into entries, root first. */
	static TArray<FPEPInventoryEntry> FlattenItemTree(FPEPItemInstanceTree&& Tree);

	/**
	 * The inverse, for entries that just left the store. The root's location goes nowhere: a tree
	 * has no store location, which is exactly what makes it something a world actor can hold.
	 */
	static FPEPItemInstanceTree MakeItemTreeFromEntries(TArray<FPEPInventoryEntry>&& Entries);

	/*
	 * The only places an item-store change is raised. An authoritative mutation and a replication
	 * callback both funnel through these, so the two machines cannot drift into different event
	 * vocabularies — which is how StateChanged came to be a server-only event that no client ever
	 * saw, while every state change reached them labelled Moved.
	 *
	 * Which location field carries meaning follows FPEPItemStoreChange: Removed reports where the
	 * item was, not where it is.
	 */
	void EmitAdded(
		FPEPItemId ItemId,
		const FPEPItemLocation& At, const FPEPItemInstance* Snapshot = nullptr);

	void EmitMoved(
		FPEPItemId ItemId,
		const FPEPItemLocation& From,
		const FPEPItemLocation& To, const FPEPItemInstance* Snapshot = nullptr,
		const FGameplayTag* PreviousRootSlotTag = nullptr);

	void EmitStateChanged(
		FPEPItemId ItemId,
		const FPEPItemLocation& At, const FPEPItemInstance* Snapshot = nullptr);

	void EmitRemoved(
		const FPEPItemInstance& ItemInstance,
		const FPEPItemLocation& From, const FGameplayTag* PreviousRootSlotTag = nullptr);

	/** Resolve a named-slot ancestor, optionally against the pre-delta location graph. */
	FGameplayTag ResolveRootSlotTag(const FPEPItemLocation& Location,
		const TMap<FPEPItemId, FPEPItemLocation>* Locations = nullptr) const;

	void BroadcastChange(const FPEPItemStoreChange& Change);

	/*
	 * Post-validation mutations. They never broadcast. Each refuses rather than asserts when the
	 * invariant its caller established does not hold: a check() is compiled out of a shipping
	 * server, which would then store the same id twice instead of stopping.
	 */
	bool CommitAddItem(
		FPEPItemInstance&& Item,
		const FPEPItemLocation& Location);

	bool CommitMoveItem(
		FPEPItemId ItemId,
		const FPEPItemLocation& Destination);

	bool CommitExtractItem(
		FPEPItemId ItemId,
		FPEPInventoryEntry& OutEntry);

	/**
	 * Removes a whole subtree and raises one Removed per member. Deliberately unvalidated, like
	 * every other Commit: the two callers have already earned the write by different routes — one
	 * checked take permission on the root, the other is the tail of an authorized mutation that
	 * emptied the item — and re-deciding it here would give the second one a permission question
	 * it has no business answering.
	 *
	 * @return false when the root is not in the store; nothing is removed in that case.
	 */
	bool CommitExtractItemTree(
		FPEPItemId RootItemId,
		TArray<FPEPInventoryEntry>& OutEntries);
};
