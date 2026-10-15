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

	virtual bool RegisterLocationValidator(
		FGameplayTag RootLocationTag,
		FPEPItemLocationValidator Validator,
		bool bAllowAutoPlacement = true) override;

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
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PEP|Inventory|Access")
	EPEPExternalAccessMode ExternalAccess = EPEPExternalAccessMode::SlotRules;

	/**
	 * Presentation hint this inventory hands to whoever opens it: which loot panel the UI should
	 * build. Sampled when the session opens, replicated to the looter only. It grants no permission
	 * and names no vocabulary of its own — the project defines the tags and maps them to widgets.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PEP|Inventory|Session",
		meta=(Categories="Inventory.LootPanel"))
	FGameplayTag LootPanelTag;

	/**
	 * The requester behind a call that reached this component through its own owner: a server RPC
	 * the engine only routes from the connection owning this actor, or a listen server's local
	 * player. It names that controller as a fact about how the call arrived, not as an assumption
	 * about who asked, which is why it is never taken from the caller.
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
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Inventory|Tree")
	void ExportItemTrees(TArray<FPEPInventoryTreePlacement>& OutPlacements) const;

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

	/* Inventory entries for direct children. Returned pointers remain valid until mutation. */
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
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory")
	virtual void GetItemIds(TArray<FPEPItemId>& OutItemIds) const override;

	UFUNCTION(BlueprintPure, Category="PEP|Inventory", meta=(DisplayName="Get Inventory Item Ids"))
	void GetInventoryItemIds(TArray<FPEPItemId>& OutItemIds) const
	{
		GetItemIds(OutItemIds);
	}

	UFUNCTION(BlueprintPure, Category="PEP|Inventory")
	bool FindInventoryEntry(FPEPItemId ItemId, FPEPInventoryEntry& OutEntry) const;

	/** Returns the loaded definition of a local inventory item, or null if absent/unloaded. Never loads assets. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory", meta=(DisplayName="Get Item Definition By Id"))
	UPEPItemDefinition* GetItemDefinitionById(FPEPItemId ItemId) const;

	/**
	 * The occupants of one container slot — what a single panel draws. Answered from the slot
	 * index, so it costs what the slot holds rather than what the store does.
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory")
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
	 * Takes an item and everything it holds out of the store and puts it into the world.
	 *
	 * The carrier class comes from UPEPItemSettings unless one is named here — a corpse bag or a
	 * quest crate is a different actor, the item inside it is not.
	 *
	 * Ordered so that a failure cannot eat the item: the class, the world and the whole subtree are
	 * checked, then the actor is spawned, and only then does the store give the item up. A spawn
	 * that fails leaves the inventory untouched; an extraction that somehow fails after it destroys
	 * the actor rather than the items.
	 */
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
	UFUNCTION(Server, Reliable)
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

	bool HasMutationAuthority() const;
	int32 AllocateRequestId();
	void ResolveInventoryRequest(
		int32 RequestId,
		EPEPInventoryRequestKind Kind,
		const FPEPItemResult& Result);
	/* Reached only by the fast array, through the friendship declared at the top of this class. */
	void HandleReplicatedEntryAdded(const FPEPInventoryEntry& Entry);

	void HandleReplicatedEntryChanged(
		const FPEPInventoryEntry& Entry,
		const FPEPItemLocation& PreviousLocation);

	void HandleReplicatedEntryRemoved(const FPEPInventoryEntry& Entry);
	void QueueReplicatedEntryRemoved(const FPEPInventoryEntry& Entry);
	void FlushReplicatedEntryRemovals();

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

	/**
	 * Fills existing compatible stacks from Item, writing nothing. Item is left holding whatever no
	 * stack could take, so a caller that finds it depleted knows the pickup needs no room at all.
	 */
	void PlanPickupStackMerges(
		FPEPItemInstance& Item,
		EPEPAccessRole Role,
		TArray<FPEPInventoryStackMerge>& OutMerges) const;

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

	/** Validates a complete tree while replacing only its root location. Never mutates this store. */
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
	 */
	UPEPInventoryComponent* CreateValidationScratch() const;

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

	/** Flattens a store-agnostic tree into entries, root first. */
	static TArray<FPEPInventoryEntry> FlattenItemTree(FPEPItemInstanceTree&& Tree);

	/**
	 * The inverse, for entries that just left the store. The root's location goes nowhere: a tree
	 * has no store location, which is exactly what makes it something a world actor can hold.
	 */
	static FPEPItemInstanceTree MakeItemTreeFromEntries(TArray<FPEPInventoryEntry>&& Entries);

	bool IsNamedSlotOccupied(
		FGameplayTag SlotTag,
		FPEPItemId IgnoredItemId,
		FPEPItemId AlsoIgnoredItemId = FPEPItemId()) const;

	bool IsItemContainerSlotOccupied(
		FPEPItemId ParentItemId,
		FName SlotName,
		FPEPItemId IgnoredItemId,
		FPEPItemId AlsoIgnoredItemId = FPEPItemId()) const;

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

	/* Post-validation mutations. They never broadcast. */
	void CommitAddItem(
		FPEPItemInstance&& Item,
		const FPEPItemLocation& Location);

	void CommitMoveItem(
		FPEPItemId ItemId,
		const FPEPItemLocation& Destination);

	void CommitExtractItem(
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

	FPEPInventoryEntry* FindMutableEntry(FPEPItemId ItemId);

	/**
	 * Authoritative store and replication payload at once. Replicated to the owner only; other
	 * viewers are served by PEPEquipment's public projection, or by an explicit loot session.
	 */
	UPROPERTY(Replicated)
	FPEPInventoryView InventoryView;

	/*
	 * Both caches hold positions into InventoryView.Entries, and each is invalidated by a different
	 * thing. Keeping one flag for both is what let a move — which shifts no position and so leaves
	 * the id map perfectly valid — quietly rot the slot map, which is keyed by the very thing a
	 * move changes. A flag now names exactly the cache it guards, so answering "does this need
	 * setting?" cannot be right about one cache and wrong about the other.
	 */

	/** ItemId to index. Only the array changing shape moves an entry, so only that invalidates it. */
	mutable TMap<FPEPItemId, int32> EntryIndex;

	/**
	 * Occupants of each container slot. Placement asks "what else is in this slot" for every
	 * candidate cell, which would otherwise walk the whole store each time.
	 *
	 * Keyed by location, so a move invalidates it even though every position stays put.
	 */
	mutable TMap<FPEPContainerSlotKey, TArray<int32>> ContainerSlotIndex;

	/**
	 * Occupants of each named slot. Equipment asks "what is in this slot" for every slot on every
	 * store change, and every placement onto a named slot asks whether it is free. Rebuilt with
	 * ContainerSlotIndex under the same flag: both are keyed by location, so whatever stales one
	 * stales the other. An array per tag, so a slot that ever holds two items still answers right.
	 */
	mutable TMap<FGameplayTag, TArray<int32>> NamedSlotIndex;

	/**
	 * Which entry sits on each cell of a grid slot. Placement asks "is this footprint free" once
	 * per candidate cell, and reading cells beats re-deriving every occupant's shape for each one.
	 * Rebuilt with the location indices, so it can never outlive a move.
	 *
	 * Unknown when an occupant's shape could not be resolved: the caller then falls back to the
	 * overlap test, which re-resolves on every call and fails the way it always did.
	 */
	struct FPEPGridOccupancy
	{
		FIntPoint GridSize = FIntPoint::ZeroValue;
		/** Entry index per cell, row-major, INDEX_NONE when free. */
		TArray<int32> CellOccupants;
		int32 FreeCells = 0;
		bool bUnknown = false;

		int32 OccupantAt(const FIntPoint Cell) const
		{
			return CellOccupants[Cell.Y * GridSize.X + Cell.X];
		}
	};
	mutable TMap<FPEPContainerSlotKey, FPEPGridOccupancy> GridOccupancy;
	void RebuildGridOccupancy() const;
	const FPEPGridOccupancy* FindGridOccupancy(FPEPItemId ParentItemId, FName SlotName) const;

	/** Named slots open to automatic placement, sorted. Changes only when a validator does. */
	mutable TArray<FGameplayTag> AutoPlacementSlotTags;
	mutable bool bAutoPlacementSlotTagsDirty = true;
	const TArray<FGameplayTag>& GetAutoPlacementSlotTags() const;

	mutable bool bEntryIndexDirty = true;
	mutable bool bContainerSlotIndexDirty = true;

	/** Rebuilds whichever cache is dirty, in a single pass when both are. */
	void RefreshIndices() const;

	/*
	 * The two ways the store is written, and the only ones. Anything that reaches Entries or an
	 * entry's location outside these will be the next bug of the kind above.
	 */

	/** Writable handle on the store. Taking it declares the array is about to change shape. */
	TArray<FPEPInventoryEntry>& MutableEntries();

	/** Moves an entry. Separate from a state write, which invalidates no cache at all. */
	void SetEntryLocation(FPEPInventoryEntry& Entry, const FPEPItemLocation& Location);

	/** Entries sharing a container slot, in no particular order. Empty when the slot is free. */
	const TArray<int32>* FindContainerSlotOccupants(
		FPEPItemId ParentItemId,
		FName SlotName) const;

	TArray<FPEPLocationValidatorBinding> LocationValidators;

	int32 NextRequestId = 1;
	TArray<FPEPInventoryEntry> PendingReplicatedRemovals;
	TMap<FPEPItemId, FGameplayTag> PendingRemovedRootSlotTags;
	// Previous complete replication graph; refreshed after the delta's callbacks finish.
	TMap<FPEPItemId, FPEPItemLocation> LastReplicatedLocations;

	FOnPEPItemStoreChanged ItemStoreChanged;
};
