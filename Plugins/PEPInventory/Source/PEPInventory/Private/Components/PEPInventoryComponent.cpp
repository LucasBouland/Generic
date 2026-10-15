#include "Components/PEPInventoryComponent.h"

#include "CoreGlobals.h"
#include "Core/PEPCoreTags.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Item/Actor/PEPItemWorldActor.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Item/PEPItemContainerQuery.h"
#include "Item/PEPItemContainerRules.h"
#include "Item/PEPItemLogChannels.h"
#include "Inventory/PEPInventoryGrid.h"
#include "Inventory/PEPInventorySettings.h"
#include "Inventory/PEPInventoryQuery.h"
#include "Loadout/PEPLoadoutPreset.h"
#include "Item/PEPItemMutation.h"
#include "Item/PEPItemQuery.h"
#include "Item/PEPItemRules.h"
#include "Item/PEPItemQueryTypes.h"
#include "Item/PEPItemCoreSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"

/**
 * One stack a pickup would top up, and what that stack would hold afterwards.
 *
 * Planned before anything is written, so that deciding whether a pickup fits and performing it are
 * the same computation. Deciding with one and committing with another is how a full inventory came
 * to refuse ammunition it would in fact have absorbed.
 *
 * Plain C++ with no reflection, and defined here rather than in a public header: none of this
 * reaches the wire, an editor or another module, and it never outlives the call that built it.
 */
struct FPEPInventoryStackMerge
{
	/** Named rather than indexed: the plan outlives the lookup, and an index would not survive it. */
	FPEPItemId TargetItemId;

	FPEPItemInstance MergedItem;

	/** Where the topped-up stack sits, which is what its StateChanged has to report. */
	FPEPItemLocation Location;
};

/* Named rather than anonymous, so a unity build cannot fold these into a neighbour's helpers. */
namespace PEPInventoryComponentPrivate
{
	bool IsItemCatalogueReadyForInventoryMutation(const UPEPInventoryComponent& Inventory)
	{
		const UWorld* World = Inventory.GetWorld();
		if (!World || !World->IsGameWorld())
		{
			return true;
		}

		const UGameInstance* GameInstance = World->GetGameInstance();
		if (!GameInstance)
		{
			// Editor previews and isolated automation worlds may not have a game instance.
			return true;
		}

		const UPEPItemCoreSubsystem* ItemCore = GameInstance->GetSubsystem<UPEPItemCoreSubsystem>();
		if (!ItemCore)
		{
			return false;
		}

		// Disabled means the host opted out of global preloading; instance-level validation still
		// requires each item's own definition to be loaded before it enters the store.
		return ItemCore->AreItemDefinitionsLoaded()
			|| ItemCore->GetItemDefinitionPreloadState() == EPEPItemDefinitionPreloadState::Disabled;
	}

	/** A container's lock: its runtime state when it carries one, its definition's default otherwise. */
	bool IsContainerItemLocked(
		const FPEPItemInstance& ContainerItem,
		const FPEPItemFragment_Container& Container)
	{
		const FPEPItemState_ContainerRuntime* RuntimeState =
			ContainerItem.GetState<FPEPItemState_ContainerRuntime>();

		return RuntimeState
			? RuntimeState->bLocked
			: Container.bStartsLocked;
	}

	/**
	 * Whether two locations name the same slot, the cell aside.
	 *
	 * A named slot holds one item, so the tag is its whole identity. A container slot holds many,
	 * and two cells of one grid are the same place as far as putting something away goes.
	 */
	bool AreLocationsInSameSlot(
		const FPEPItemLocation& First,
		const FPEPItemLocation& Second)
	{
		if (First.Type != Second.Type)
		{
			return false;
		}

		switch (First.Type)
		{
		case EPEPItemLocationType::NamedSlot:
			return First.LocationTag == Second.LocationTag;

		case EPEPItemLocationType::ItemContainer:
			return First.ParentItemId == Second.ParentItemId
				&& First.ContainerSlotName == Second.ContainerSlotName;

		default:
			return false;
		}
	}

	/**
	 * Negative, zero or positive as First's cell comes before, with or after Second's, row by row.
	 * A location without a grid placement reads as the origin, exactly as validation reads it.
	 */
	int32 CompareGridCells(
		const FPEPItemLocation& First,
		const FPEPItemLocation& Second)
	{
		const FIntPoint FirstCell = PEPInventoryGrid::GetGridPlacement(First).Position;
		const FIntPoint SecondCell = PEPInventoryGrid::GetGridPlacement(Second).Position;

		if (FirstCell.Y != SecondCell.Y)
		{
			return FirstCell.Y < SecondCell.Y ? -1 : 1;
		}

		if (FirstCell.X != SecondCell.X)
		{
			return FirstCell.X < SecondCell.X ? -1 : 1;
		}

		return 0;
	}

	/** Drops one id from an id-list index, and the list itself once it is empty. */
	template <typename MapType, typename KeyType>
	void RemoveIdFromIndex(MapType& Index, const KeyType& Key, const FPEPItemId ItemId)
	{
		if (TArray<FPEPItemId>* Ids = Index.Find(Key))
		{
			/*
			 * Order-preserving, since children are reported in the order they arrived. Searched
			 * from the back: a subtree leaves in reverse of the order it was written, so the id
			 * is usually the last one and nothing has to shift.
			 */
			const int32 Found = Ids->FindLast(ItemId);
			if (Found != INDEX_NONE)
			{
				Ids->RemoveAt(Found);
			}

			if (Ids->IsEmpty())
			{
				Index.Remove(Key);
			}
		}
	}

	/** Segments in a tag's name: A.B.C is three deep. */
	int32 CountTagDepth(const FGameplayTag Tag)
	{
		const FString TagString = Tag.ToString();
		int32 Depth = TagString.IsEmpty() ? 0 : 1;

		for (int32 Index = 0; Index < TagString.Len(); ++Index)
		{
			if (TagString[Index] == TEXT('.'))
			{
				++Depth;
			}
		}

		return Depth;
	}

	/*
	 * FPEPItemResult names one item, so a result about several can only name one of them
	 * arbitrarily — and a caller would then be entitled to read it. It stays unset instead.
	 * A failure is a different matter: it comes from the validator and names the item that caused
	 * it, which is exactly what a caller needs.
	 */
	FPEPItemId SingleTreeItemId(const TArray<FPEPInventoryTreePlacement>& Placements)
	{
		return Placements.Num() == 1
			? Placements[0].Tree.RootItem.ItemId
			: FPEPItemId();
	}

	TArray<FPEPInventoryTreePlacement> MakeSingleTreePlacement(
		FPEPItemInstanceTree&& Tree,
		const FPEPItemLocation& RootLocation)
	{
		TArray<FPEPInventoryTreePlacement> Placements;
		FPEPInventoryTreePlacement& Placement = Placements.AddDefaulted_GetRef();
		Placement.RootLocation = RootLocation;
		Placement.Tree = MoveTemp(Tree);
		return Placements;
	}
}

UPEPInventoryComponent::UPEPInventoryComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
    InventoryView.OwnerComponent = this;
}

void UPEPInventoryComponent::PostInitProperties()
{
    Super::PostInitProperties();
    // Native struct copies from the CDO/template may overwrite the constructor's local pointers.
    InventoryView.OwnerComponent = this;
    InventoryView.bIsSessionView = false;
}

void UPEPInventoryComponent::OnRegister()
{
    // Rebind after loading/duplicating Blueprint components, before any replication callbacks.
    InventoryView.OwnerComponent = this;
    InventoryView.bIsSessionView = false;
    Super::OnRegister();
}

void UPEPInventoryComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    /*
     * Pawn inventories are private. Public equipment is replicated by
     * PEPEquipment. Shared containers should use an explicit access/view
     * session rather than exposing every entry to every relevant client.
     */
    DOREPLIFETIME_CONDITION(
        UPEPInventoryComponent,
        InventoryView,
        COND_OwnerOnly);
}

// ---- Replication ----------------------------------------------------------------------------

void UPEPInventoryComponent::BeginReplicatedReceive()
{
    /*
     * Open already, in this frame: an earlier callback of the same receive flagged the indices,
     * and the array has not changed since. The frame is checked as well as the flag so that a
     * receive whose PostReplicatedReceive never arrived cannot leave the next one trusting indices
     * built before it.
     */
    if (bReplicatedReceiveOpen && ReplicatedReceiveFrame == GFrameCounter)
    {
        return;
    }

    bReplicatedReceiveOpen = true;
    ReplicatedReceiveFrame = GFrameCounter;

    // The replicated array changed shape or content under us; nothing cached about it is trusted.
    MarkAllIndicesDirty();
}

void UPEPInventoryComponent::HandleReplicatedEntryAdded(
    const FPEPInventoryEntry& Entry)
{
    BeginReplicatedReceive();

    EmitAdded(Entry.Item.ItemId, Entry.Location, &Entry.Item);
}

void UPEPInventoryComponent::HandleReplicatedEntryChanged(
    const FPEPInventoryEntry& Entry,
    const FPEPItemLocation& PreviousLocation)
{
    BeginReplicatedReceive();

    /*
     * One callback covers both kinds of change, so the entry is asked which one it was. A single
     * delta can carry a move and a state change together; the move is the structural fact and is
     * what gets reported, and any change event means the item should be re-read either way.
     */
    if (PreviousLocation != Entry.Location)
    {
        const FGameplayTag PreviousRoot = ResolveRootSlotTag(PreviousLocation,
            LastReplicatedLocations.IsEmpty() ? nullptr : &LastReplicatedLocations);
        EmitMoved(Entry.Item.ItemId, PreviousLocation, Entry.Location, &Entry.Item, &PreviousRoot);
        return;
    }

    EmitStateChanged(Entry.Item.ItemId, Entry.Location, &Entry.Item);
}

void UPEPInventoryComponent::HandleReplicatedEntryRemoved(
    const FPEPInventoryEntry& Entry)
{
    BeginReplicatedReceive();

    EmitRemoved(Entry.Item, Entry.Location);
}

void UPEPInventoryComponent::QueueReplicatedEntryRemoved(
    const FPEPInventoryEntry& Entry)
{
    BeginReplicatedReceive();

    PendingReplicatedRemovals.Add(Entry);
    PendingRemovedRootSlotTags.Add(Entry.Item.ItemId, ResolveRootSlotTag(Entry.Location,
        LastReplicatedLocations.IsEmpty() ? nullptr : &LastReplicatedLocations));
}

void UPEPInventoryComponent::FlushReplicatedEntryRemovals()
{
    /*
     * The end of a receive: the fast array has just applied its removals, the one reshaping of
     * Entries it performs after the callbacks. Flagged once more for that, and closed, so the next
     * receive flags again on its first callback.
     */
    bReplicatedReceiveOpen = false;
    MarkAllIndicesDirty();

    TArray<FPEPInventoryEntry> Removed = MoveTemp(PendingReplicatedRemovals);
    PendingReplicatedRemovals.Reset();

    for (const FPEPInventoryEntry& Entry : Removed)
    {
        EmitRemoved(Entry.Item, Entry.Location, PendingRemovedRootSlotTags.Find(Entry.Item.ItemId));
    }
    PendingRemovedRootSlotTags.Reset();
    LastReplicatedLocations.Reset();
    LastReplicatedLocations.Reserve(InventoryView.Entries.Num());
    for (const FPEPInventoryEntry& Entry : InventoryView.Entries)
    {
        LastReplicatedLocations.Add(Entry.Item.ItemId, Entry.Location);
    }
}

// ---- Location validators --------------------------------------------------------------------

bool UPEPInventoryComponent::RegisterLocationValidator(
    const FGameplayTag RootLocationTag,
    FPEPItemLocationValidator InValidator,
    const bool bAllowAutoPlacement)
{
    if (!RootLocationTag.IsValid() || !InValidator.IsBound())
    {
        return false;
    }

    /* Null for a delegate bound to no object, which is then its own claimant class. */
    const UObject* const Claimant = InValidator.GetUObject();

    for (FPEPLocationValidatorBinding& Existing : LocationValidators)
    {
        if (Existing.RootLocationTag != RootLocationTag)
        {
            continue;
        }

        /*
         * A tag belongs to the domain that claimed it. A second object registering the same root
         * would silently take over every slot beneath it, and the first would go on believing it
         * decides them. A claim whose object has since gone is no claim at all and is replaced.
         */
        if (Existing.Validator.IsBound()
            && Existing.Validator.GetUObject() != Claimant)
        {
            UE_LOG(
                LogPEPItems,
                Warning,
                TEXT("'%s' already claims location tag '%s' on '%s'; the registration by '%s' was refused."),
                *GetNameSafe(Existing.Validator.GetUObject()),
                *RootLocationTag.ToString(),
                *GetName(),
                *GetNameSafe(Claimant));

            return false;
        }

        Existing.Validator = MoveTemp(InValidator);
        Existing.bAllowAutoPlacement = bAllowAutoPlacement;
        Existing.TagDepth = PEPInventoryComponentPrivate::CountTagDepth(RootLocationTag);
        bAutoPlacementSlotTagsDirty = true;
        return true;
    }

    FPEPLocationValidatorBinding& Binding =
        LocationValidators.AddDefaulted_GetRef();
    Binding.RootLocationTag = RootLocationTag;
    Binding.Validator = MoveTemp(InValidator);
    Binding.bAllowAutoPlacement = bAllowAutoPlacement;
    Binding.TagDepth = PEPInventoryComponentPrivate::CountTagDepth(RootLocationTag);
    bAutoPlacementSlotTagsDirty = true;
    return true;
}

void UPEPInventoryComponent::UnregisterLocationValidator(
    const FGameplayTag RootLocationTag,
    const UObject* BoundObject)
{
    LocationValidators.RemoveAll(
        [RootLocationTag, BoundObject](
            const FPEPLocationValidatorBinding& Binding)
        {
            if (Binding.RootLocationTag != RootLocationTag)
            {
                return false;
            }

            return !BoundObject
                || Binding.Validator.IsBoundToObject(BoundObject);
        });
    bAutoPlacementSlotTagsDirty = true;
}

const TArray<FGameplayTag>& UPEPInventoryComponent::GetAutoPlacementSlotTags() const
{
    if (!bAutoPlacementSlotTagsDirty)
    {
        return AutoPlacementSlotTags;
    }
    /*
     * Inventory knows nothing of PEPEquipment. It uses the roots claimed in LocationValidators,
     * whichever domain claimed them, and every child tag beneath each root.
     */
    TSet<FGameplayTag> CandidateSlotTags;
    for (const FPEPLocationValidatorBinding& Binding : LocationValidators)
    {
        if (!Binding.bAllowAutoPlacement
            || !Binding.RootLocationTag.IsValid()
            || !Binding.Validator.IsBound())
        {
            continue;
        }
        CandidateSlotTags.Add(Binding.RootLocationTag);
        TArray<FGameplayTag> ChildTags;
        UGameplayTagsManager::Get()
            .RequestGameplayTagChildren(Binding.RootLocationTag)
            .GetGameplayTagArray(ChildTags);
        for (const FGameplayTag ChildTag : ChildTags)
        {
            CandidateSlotTags.Add(ChildTag);
        }
    }
    /* A TSet has no stable order. Sorting keeps one item from picking a different slot per run. */
    AutoPlacementSlotTags = CandidateSlotTags.Array();
    AutoPlacementSlotTags.Sort(
        [](const FGameplayTag First, const FGameplayTag Second)
        {
            return First.GetTagName().LexicalLess(Second.GetTagName());
        });
    bAutoPlacementSlotTagsDirty = false;
    return AutoPlacementSlotTags;
}

FPEPItemResult
UPEPInventoryComponent::CanMoveItemAuto(
    const FPEPItemId ItemId,
    FPEPItemLocation& OutDestination,
    const FPEPItemAccessContext& Access) const
{
	OutDestination = FPEPItemLocation();

	const FPEPInventoryEntry* Entry = FindEntry(ItemId);

	if (!Entry)
	{
		return FPEPItemResult::Failure(
			PEPCoreTags::Reason_InvalidTarget,
			ItemId);
	}

	/*
	 * The search a pickup uses, minus the slot the item is already in: a compatible named slot
	 * first, then the grids of what this store holds.
	 */
	if (!TryResolveAutoPlacement(
		Entry->Item,
		OutDestination,
		ResolveAccessRole(Access),
		Entry->Location))
	{
		return FPEPItemResult::Failure(
			PEPCoreTags::Reason_NoFreeSlot,
			ItemId);
	}

	return CanMoveItem(
		ItemId,
		OutDestination,
		Access);
}

FPEPItemResult
UPEPInventoryComponent::MoveItemAuto(
    const FPEPItemId ItemId,
    const FPEPItemAccessContext& Access)
{
    if (!HasMutationAuthority())
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_NotAuthority,
            ItemId);
    }

    FPEPItemLocation Destination;

    const FPEPItemResult Validation =
        CanMoveItemAuto(
            ItemId,
            Destination,
            Access);

    if (!Validation.bSuccess)
    {
        return Validation;
    }

    /* CanMoveItemAuto ended in CanMoveItem for exactly this destination, and nothing ran since. */
    return CommitValidatedMove(
        ItemId,
        Destination);
}

// ---- Events ---------------------------------------------------------------------------------

FGameplayTag UPEPInventoryComponent::ResolveRootSlotTag(
    const FPEPItemLocation& Location,
    const TMap<FPEPItemId, FPEPItemLocation>* Locations) const
{
    /*
     * Bounded by the graph's size rather than by a visited set: a chain of distinct items cannot be
     * longer than the graph holds, so a longer walk has met a cycle and names no root. Nothing is
     * allocated, and this runs for every event raised.
     */
    const int32 MaxSteps = Locations ? Locations->Num() : InventoryView.Entries.Num();
    const FPEPItemLocation* Current = &Location;

    for (int32 Step = 0; Current->Type == EPEPItemLocationType::ItemContainer; ++Step)
    {
        if (!Current->ParentItemId.IsValid() || Step > MaxSteps)
        {
            return FGameplayTag();
        }
        if (Locations)
        {
            const FPEPItemLocation* ParentLocation = Locations->Find(Current->ParentItemId);
            if (!ParentLocation)
            {
                return FGameplayTag();
            }
            Current = ParentLocation;
        }
        else
        {
            const FPEPInventoryEntry* Parent = FindEntry(Current->ParentItemId);
            if (!Parent)
            {
                return FGameplayTag();
            }
            Current = &Parent->Location;
        }
    }
    return Current->Type == EPEPItemLocationType::NamedSlot ? Current->LocationTag : FGameplayTag();
}

void UPEPInventoryComponent::EmitAdded(
    const FPEPItemId ItemId,
    const FPEPItemLocation& At, const FPEPItemInstance* Snapshot)
{
    FPEPItemStoreChange Change;
    Change.ItemId = ItemId;
    if (const FPEPItemInstance* Item = Snapshot ? Snapshot : FindItem(ItemId))
    {
        Change.ItemInstance = *Item;
    }
    Change.ChangeType = EPEPItemStoreChangeType::Added;
    Change.CurrentLocation = At;
    Change.CurrentRootSlotTag = ResolveRootSlotTag(At);
    BroadcastChange(Change);
}

void UPEPInventoryComponent::EmitMoved(
    const FPEPItemId ItemId,
    const FPEPItemLocation& From,
    const FPEPItemLocation& To, const FPEPItemInstance* Snapshot,
    const FGameplayTag* PreviousRootSlotTag)
{
    FPEPItemStoreChange Change;
    Change.ItemId = ItemId;
    if (const FPEPItemInstance* Item = Snapshot ? Snapshot : FindItem(ItemId))
    {
        Change.ItemInstance = *Item;
    }
    Change.ChangeType = EPEPItemStoreChangeType::Moved;
    Change.PreviousLocation = From;
    Change.CurrentLocation = To;
    Change.PreviousRootSlotTag = PreviousRootSlotTag ? *PreviousRootSlotTag : ResolveRootSlotTag(From);
    Change.CurrentRootSlotTag = ResolveRootSlotTag(To);
    BroadcastChange(Change);
}

void UPEPInventoryComponent::EmitStateChanged(
    const FPEPItemId ItemId,
    const FPEPItemLocation& At, const FPEPItemInstance* Snapshot)
{
    FPEPItemStoreChange Change;
    Change.ItemId = ItemId;
    if (const FPEPItemInstance* Item = Snapshot ? Snapshot : FindItem(ItemId))
    {
        Change.ItemInstance = *Item;
    }
    Change.ChangeType = EPEPItemStoreChangeType::StateChanged;
    Change.CurrentLocation = At;
    Change.CurrentRootSlotTag = ResolveRootSlotTag(At);
    BroadcastChange(Change);
}

void UPEPInventoryComponent::EmitRemoved(
    const FPEPItemInstance& ItemInstance,
    const FPEPItemLocation& From, const FGameplayTag* PreviousRootSlotTag)
{
    /*
     * The location goes to PreviousLocation: a removed item has no current one, and putting where
     * it was into CurrentLocation left PreviousLocation empty on exactly the event whose whole
     * point is to say where the item used to be.
     */
    FPEPItemStoreChange Change;
    Change.ItemId = ItemInstance.ItemId;
    Change.ItemInstance = ItemInstance;
    Change.ChangeType = EPEPItemStoreChangeType::Removed;
    Change.PreviousLocation = From;
    Change.PreviousRootSlotTag = PreviousRootSlotTag ? *PreviousRootSlotTag : ResolveRootSlotTag(From);
    BroadcastChange(Change);
}

void UPEPInventoryComponent::BroadcastChange(
    const FPEPItemStoreChange& Change)
{
    AActor* Owner = GetOwner();
    const bool bHasAuthority =
        Owner && Owner->HasAuthority();

    ItemStoreChanged.Broadcast(Change);
    OnInventoryChanged.Broadcast(Change);

    if (bHasAuthority)
    {
        Owner->ForceNetUpdate();
    }
}

// ---- Access ---------------------------------------------------------------------------------

bool UPEPInventoryComponent::HasMutationAuthority() const
{
    const AActor* Owner = GetOwner();
    return Owner && Owner->HasAuthority();
}

const AController* UPEPInventoryComponent::GetOwningController() const
{
    AActor* Owner = GetOwner();

    if (!Owner)
    {
        return nullptr;
    }

    /*
     * A pawn's controller is null once it is unpossessed, which is precisely what a corpse is.
     * Falling through to null is the intended answer there, not a failure to find one.
     */
    if (const APawn* OwningPawn = Cast<APawn>(Owner))
    {
        return OwningPawn->GetController();
    }

    if (const AController* OwningController = Cast<AController>(Owner))
    {
        return OwningController;
    }

    /* Reached when the inventory lives on the player state so that it survives respawns. */
    if (const APlayerState* OwningPlayerState = Cast<APlayerState>(Owner))
    {
        return OwningPlayerState->GetOwningController();
    }

    return nullptr;
}

FPEPItemAccessContext UPEPInventoryComponent::GetOwnerRequestContext() const
{
    if (const AController* OwningController = GetOwningController())
    {
        return FPEPItemAccessContext::From(OwningController);
    }

    /*
     * No controller: a world container, a corpse, a stash whose player possesses nothing. On the
     * authority, a call made through the store's own owner is the server acting for the store —
     * a container running its own pickup — and that is what System() exists to name. A client in
     * the same position is nobody, and a null requester resolves to External.
     */
    return HasMutationAuthority()
        ? FPEPItemAccessContext::System()
        : FPEPItemAccessContext::From(nullptr);
}

FPEPItemAccessContext UPEPInventoryComponent::GetRemoteRequestContext() const
{
    /*
     * Never System(), even with no owning controller: an RPC still in flight when the pawn was
     * unpossessed must not reach the corpse with the server's permissions.
     */
    return FPEPItemAccessContext::From(GetOwningController());
}

void UPEPInventoryComponent::SetExternalAccess(const EPEPExternalAccessMode InExternalAccess)
{
    /* Server policy. BlueprintAuthorityOnly guards Blueprint callers only; this guards the rest. */
    if (!HasMutationAuthority())
    {
        return;
    }

    ExternalAccess = InExternalAccess;
}

EPEPAccessRole UPEPInventoryComponent::ResolveAccessRole(
    const FPEPItemAccessContext& Access) const
{
    if (Access.IsSystem())
    {
        return EPEPAccessRole::Owner;
    }

    /*
     * Both pointers are checked before comparing them. A requester destroyed since the request was
     * built resolves to null, and so does the controller of an unowned container; comparing them
     * without the guard would call those two absences a match and grant owner permissions over a
     * corpse to whoever asked last.
     */
    const AController* Requester = Access.Requester.Get();
    const AController* Owning = GetOwningController();

    if (Requester && Owning && Requester == Owning)
    {
        return EPEPAccessRole::Owner;
    }

    /* A requester destroyed mid-request stays External, whatever the store grants the living. */
    return Requester && ExternalAccess == EPEPExternalAccessMode::SameAsOwner
        ? EPEPAccessRole::Owner
        : EPEPAccessRole::External;
}

const FPEPContainerSlotDefinition*
UPEPInventoryComponent::ResolveContainerSlot(
    const FPEPItemLocation& Location,
    bool& bOutLocked) const
{
    bOutLocked = false;

    if (Location.Type != EPEPItemLocationType::ItemContainer)
    {
        return nullptr;
    }

    const FPEPInventoryEntry* ParentEntry =
        FindEntry(Location.ParentItemId);

    if (!ParentEntry)
    {
        return nullptr;
    }

    const FPEPItemFragment_Container* ContainerFragment =
        ParentEntry->Item.GetDefinitionFragment<
            FPEPItemFragment_Container>();

    if (!ContainerFragment)
    {
        return nullptr;
    }

    bOutLocked = PEPInventoryComponentPrivate::IsContainerItemLocked(
        ParentEntry->Item,
        *ContainerFragment);

    return ContainerFragment->FindSlot(
        Location.ContainerSlotName);
}

FPEPItemResult
UPEPInventoryComponent::ValidateCanTakeFromLocation(
    const FPEPInventoryEntry& Entry,
    const EPEPAccessRole Role) const
{
    if (Entry.Location.Type !=
        EPEPItemLocationType::ItemContainer)
    {
        return FPEPItemResult::Success(
            Entry.Item.ItemId,
            PEPCoreTags::Reason_None);
    }

    bool bLocked = false;

    const FPEPContainerSlotDefinition* Slot =
        ResolveContainerSlot(
            Entry.Location,
            bLocked);

    if (!Slot || !Slot->IsValid())
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            Entry.Item.ItemId);
    }

    if (bLocked || !Slot->GetPolicyFor(Role).bCanTake)
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_BlockedByState,
            Entry.Item.ItemId);
    }

    /* The slot allows it; what holds the slot has to as well. */
    return ValidateExternalReach(Entry, Role);
}

FPEPItemResult UPEPInventoryComponent::ValidateExternalReach(
    const FPEPInventoryEntry& Entry,
    const EPEPAccessRole Role) const
{
    if (Role != EPEPAccessRole::External)
    {
        return FPEPItemResult::Success(
            Entry.Item.ItemId,
            PEPCoreTags::Reason_None);
    }

    /*
     * The same visibility rule the session applies when it mirrors a store, so an outsider can act
     * on exactly what it is shown; locks are read on the way, from every container above.
     */
    bool bBehindLock = false;
    const FPEPInventoryEntry* Current = &Entry;
    const int32 MaxSteps = InventoryView.Entries.Num();

    for (int32 Step = 0; Step <= MaxSteps; ++Step)
    {
        if (Current->Location.Type == EPEPItemLocationType::NamedSlot)
        {
            /* A named slot carries no visibility vocabulary; what a body wears is on show. */
            return bBehindLock
                ? FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, Entry.Item.ItemId)
                : FPEPItemResult::Success(Entry.Item.ItemId, PEPCoreTags::Reason_None);
        }

        if (Current->Location.Type != EPEPItemLocationType::ItemContainer)
        {
            break;
        }

        const FPEPInventoryEntry* Parent = FindEntry(Current->Location.ParentItemId);
        const FPEPItemFragment_Container* Container = Parent
            ? Parent->Item.GetDefinitionFragment<FPEPItemFragment_Container>()
            : nullptr;
        const FPEPContainerSlotDefinition* Slot = Container
            ? Container->FindSlot(Current->Location.ContainerSlotName)
            : nullptr;

        if (!Slot
            || !PEPItemContainerRules::DoesContainerRevealContents(Parent->Item, Role)
            || !PEPItemContainerRules::DoesSlotRevealOccupants(*Slot, Role))
        {
            break;
        }

        bBehindLock = bBehindLock
            || PEPInventoryComponentPrivate::IsContainerItemLocked(Parent->Item, *Container);
        Current = Parent;
    }

    /* Hidden, orphaned, or caught in a cycle: to this requester the item is not there. */
    return FPEPItemResult::Failure(
        PEPCoreTags::Reason_InvalidTarget,
        Entry.Item.ItemId);
}

bool UPEPInventoryComponent::CanDepositIntoLocation(
    const FPEPItemLocation& Location,
    const EPEPAccessRole Role) const
{
    switch (Location.Type)
    {
    case EPEPItemLocationType::ItemContainer:
    {
        bool bLocked = false;

        const FPEPContainerSlotDefinition* Slot =
            ResolveContainerSlot(Location, bLocked);

        return Slot
            && Slot->IsValid()
            && !bLocked
            && Slot->GetPolicyFor(Role).bCanPut;
    }

    /*
     * A named slot's meaning belongs to whichever plugin claimed the tag, and none of them has
     * been asked about outsiders yet. Refusing here costs the owner nothing — they reach the same
     * item through the ordinary put path — and avoids granting a right nobody has defined.
     */
    case EPEPItemLocationType::NamedSlot:
        return Role == EPEPAccessRole::Owner;

    default:
        return false;
    }
}

// ---- Create ---------------------------------------------------------------------------------

FPEPItemResult UPEPInventoryComponent::CanAddItem(
    const FPEPItemInstance& Item,
    const FPEPItemLocation& Location,
    const FPEPItemAccessContext& Access) const
{
	if (!PEPInventoryComponentPrivate::IsItemCatalogueReadyForInventoryMutation(*this))
	{
		return FPEPItemResult::Failure(
			PEPCoreTags::Reason_BlockedByState,
			Item.ItemId);
	}

    if (!Item.IsValid())
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            Item.ItemId);
    }

	FPEPItemInstanceValidationResult InstanceValidation;
	if (!Item.ValidateInstance(InstanceValidation))
	{
		return FPEPItemResult::Failure(
			PEPCoreTags::Reason_InvalidTarget,
			Item.ItemId);
	}

    if (ContainsItem(Item.ItemId))
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_BlockedByState,
            Item.ItemId);
    }

    return ValidateLocationForItem(
        Item,
        Location,
        ResolveAccessRole(Access));
}

FPEPItemResult UPEPInventoryComponent::AddItem(
    const FPEPItemInstance& Item,
    const FPEPItemLocation& Location,
    const FPEPItemAccessContext& Access)
{
    if (!HasMutationAuthority())
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_NotAuthority,
            Item.ItemId);
    }

    const FPEPItemResult Validation =
        CanAddItem(Item, Location, Access);

    if (!Validation.bSuccess)
    {
        return Validation;
    }

    const FPEPItemId ItemId = Item.ItemId;
    FPEPItemInstance StoredItem = Item;
    if (!CommitAddItem(MoveTemp(StoredItem), Location))
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }
    EmitAdded(ItemId, Location);

    return FPEPItemResult::Success(
        ItemId,
        PEPCoreTags::Reason_None);
}

// ---- Indices --------------------------------------------------------------------------------

void UPEPInventoryComponent::MarkAllIndicesDirty() const
{
    bEntryIndexDirty = true;
    bContainerSlotIndexDirty = true;
}

void UPEPInventoryComponent::RefreshIndices() const
{
    /* A size the indices never saw means a write that bypassed the writers: rebuild, do not trust. */
    if (IndexedEntryCount != InventoryView.Entries.Num())
    {
        MarkAllIndicesDirty();
    }

    const bool bRebuildEntries = bEntryIndexDirty;
    const bool bRebuildLocations = bContainerSlotIndexDirty;

    if (!bRebuildEntries && !bRebuildLocations)
    {
        return;
    }

    const TArray<FPEPInventoryEntry>& Entries = InventoryView.Entries;

    if (bRebuildEntries)
    {
        EntryIndex.Reset();
        EntryIndex.Reserve(Entries.Num());
    }

    if (bRebuildLocations)
    {
        ContainerSlotIndex.Reset();
        NamedSlotIndex.Reset();
        ChildIndex.Reset();
        /* Cell maps are built per slot on demand; dropping them all is their whole rebuild. */
        GridOccupancy.Reset();
    }

    for (int32 Index = 0; Index < Entries.Num(); ++Index)
    {
        const FPEPInventoryEntry& Entry = Entries[Index];

        if (bRebuildEntries)
        {
            EntryIndex.Add(Entry.Item.ItemId, Index);
        }

        if (bRebuildLocations)
        {
            AddToLocationIndices(Entry.Item.ItemId, Entry.Location);
        }
    }

    bEntryIndexDirty = false;
    bContainerSlotIndexDirty = false;
    IndexedEntryCount = Entries.Num();
}

const FPEPInventoryEntry* UPEPInventoryComponent::FindIndexedEntry(
    const FPEPItemId ItemId) const
{
    /* Confirmed rather than assumed: an index naming another entry answers nothing, never wrongly. */
    const int32* Index = EntryIndex.Find(ItemId);
    const TArray<FPEPInventoryEntry>& Entries = InventoryView.Entries;
    return Index
        && Entries.IsValidIndex(*Index)
        && Entries[*Index].Item.ItemId == ItemId
        ? &Entries[*Index]
        : nullptr;
}

void UPEPInventoryComponent::AddToLocationIndices(
    const FPEPItemId ItemId,
    const FPEPItemLocation& Location) const
{
    switch (Location.Type)
    {
    case EPEPItemLocationType::ItemContainer:
    {
        const FPEPContainerSlotKey Key(Location.ParentItemId, Location.ContainerSlotName);
        ContainerSlotIndex.FindOrAdd(Key).Add(ItemId);
        ChildIndex.FindOrAdd(Location.ParentItemId).Add(ItemId);
        /* The slot's cell map no longer describes it; the next read builds a fresh one. */
        GridOccupancy.Remove(Key);
        break;
    }
    case EPEPItemLocationType::NamedSlot:
        NamedSlotIndex.FindOrAdd(Location.LocationTag).Add(ItemId);
        break;
    default:
        break;
    }
}

void UPEPInventoryComponent::RemoveFromLocationIndices(
    const FPEPItemId ItemId,
    const FPEPItemLocation& Location) const
{
    switch (Location.Type)
    {
    case EPEPItemLocationType::ItemContainer:
    {
        const FPEPContainerSlotKey Key(Location.ParentItemId, Location.ContainerSlotName);
        PEPInventoryComponentPrivate::RemoveIdFromIndex(ContainerSlotIndex, Key, ItemId);
        PEPInventoryComponentPrivate::RemoveIdFromIndex(ChildIndex, Location.ParentItemId, ItemId);
        GridOccupancy.Remove(Key);
        break;
    }
    case EPEPItemLocationType::NamedSlot:
        PEPInventoryComponentPrivate::RemoveIdFromIndex(NamedSlotIndex, Location.LocationTag, ItemId);
        break;
    default:
        break;
    }
}

void UPEPInventoryComponent::IndexEntryLocation(
    const FPEPItemId ItemId,
    const FPEPItemLocation& Location) const
{
    if (!bContainerSlotIndexDirty)
    {
        AddToLocationIndices(ItemId, Location);
    }
}

void UPEPInventoryComponent::UnindexEntryLocation(
    const FPEPItemId ItemId,
    const FPEPItemLocation& Location) const
{
    if (!bContainerSlotIndexDirty)
    {
        RemoveFromLocationIndices(ItemId, Location);
    }
}

const UPEPInventoryComponent::FPEPGridOccupancy* UPEPInventoryComponent::FindGridOccupancy(
    const FPEPItemId ParentItemId,
    const FName SlotName) const
{
    RefreshIndices();

    const FPEPContainerSlotKey Key(ParentItemId, SlotName);
    if (const FPEPGridOccupancy* Cached = GridOccupancy.Find(Key))
    {
        return Cached;
    }

    /* An empty slot has nothing to map; callers fall back to an overlap test with no occupants. */
    const TArray<FPEPItemId>* Occupants = ContainerSlotIndex.Find(Key);
    const FPEPInventoryEntry* ParentEntry = Occupants ? FindIndexedEntry(ParentItemId) : nullptr;
    const FPEPItemFragment_Container* Container = ParentEntry
        ? ParentEntry->Item.GetDefinitionFragment<FPEPItemFragment_Container>()
        : nullptr;
    const FPEPContainerSlotDefinition* Slot = Container ? Container->FindSlot(SlotName) : nullptr;
    if (!Slot || Slot->Mode != EPEPContainerSlotMode::Grid
        || Slot->GridSize.X <= 0 || Slot->GridSize.Y <= 0)
    {
        return nullptr;
    }

    FPEPGridOccupancy Occupancy;
    Occupancy.GridSize = Slot->GridSize;
    Occupancy.CellOccupants.Init(FPEPItemId(), Slot->GridSize.X * Slot->GridSize.Y);
    Occupancy.FreeCells = Occupancy.CellOccupants.Num();

    for (const FPEPItemId OccupantId : *Occupants)
    {
        const FPEPInventoryEntry* Entry = FindIndexedEntry(OccupantId);
        FPEPItemInventoryShapeData Shape;
        if (!Entry || !PEPInventoryQuery::GetInventoryShapeData(Entry->Item, Shape))
        {
            Occupancy.bUnknown = true;
            continue;
        }
        const FPEPGridPlacement Placement = PEPInventoryGrid::GetGridPlacement(Entry->Location);
        const FIntPoint Size = PEPInventoryValidation::OrientedSize(Shape.Size, Placement.bRotated);
        const FIntPoint From = Placement.Position;
        /* Clipped: an entry hanging off the grid is a corrupt store, not a reason to crash. */
        for (int32 Y = FMath::Max(0, From.Y); Y < FMath::Min(Slot->GridSize.Y, From.Y + Size.Y); ++Y)
        {
            for (int32 X = FMath::Max(0, From.X); X < FMath::Min(Slot->GridSize.X, From.X + Size.X); ++X)
            {
                FPEPItemId& Cell = Occupancy.CellOccupants[Y * Slot->GridSize.X + X];
                if (!Cell.IsValid())
                {
                    Cell = OccupantId;
                    --Occupancy.FreeCells;
                }
            }
        }
    }

    return &GridOccupancy.Add(Key, MoveTemp(Occupancy));
}

bool UPEPInventoryComponent::IsGridFootprintFree(
    const FPEPGridOccupancy& Occupancy,
    const FIntPoint Position,
    const FIntPoint Size,
    const FPEPItemId IgnoredItemId,
    const FPEPItemId AlsoIgnoredItemId)
{
    if (!PEPInventoryValidation::IsFootprintInsideGrid(Position, Size, Occupancy.GridSize))
    {
        return false;
    }

    for (int32 Y = Position.Y; Y < Position.Y + Size.Y; ++Y)
    {
        for (int32 X = Position.X; X < Position.X + Size.X; ++X)
        {
            const FPEPItemId OccupantId = Occupancy.OccupantAt(FIntPoint(X, Y));
            if (OccupantId.IsValid()
                && OccupantId != IgnoredItemId
                && OccupantId != AlsoIgnoredItemId)
            {
                return false;
            }
        }
    }

    return true;
}

FPEPInventoryEntry& UPEPInventoryComponent::AppendEntry(FPEPInventoryEntry&& Entry)
{
    const int32 NewIndex = InventoryView.Entries.Add(MoveTemp(Entry));
    FPEPInventoryEntry& Added = InventoryView.Entries[NewIndex];

    if (!bEntryIndexDirty)
    {
        EntryIndex.Add(Added.Item.ItemId, NewIndex);
        IndexedEntryCount = InventoryView.Entries.Num();
    }
    IndexEntryLocation(Added.Item.ItemId, Added.Location);

    return Added;
}

void UPEPInventoryComponent::RemoveEntryAtSwap(
    const int32 Index,
    FPEPInventoryEntry& OutEntry)
{
    TArray<FPEPInventoryEntry>& Entries = InventoryView.Entries;
    FPEPInventoryEntry& Removed = Entries[Index];
    const FPEPItemId RemovedId = Removed.Item.ItemId;

    UnindexEntryLocation(RemovedId, Removed.Location);

    /* Cell maps of the item's own slots would otherwise outlive the parent they describe. */
    if (!bContainerSlotIndexDirty)
    {
        if (const FPEPItemFragment_Container* Container =
            Removed.Item.GetDefinitionFragment<FPEPItemFragment_Container>())
        {
            for (const FPEPContainerSlotDefinition& Slot : Container->Slots)
            {
                GridOccupancy.Remove(FPEPContainerSlotKey(RemovedId, Slot.SlotName));
            }
        }
    }

    OutEntry = MoveTemp(Removed);

    const int32 LastIndex = Entries.Num() - 1;

    // RemoveAtSwap is safe: the fast array keys entries by replication id, not by position.
    Entries.RemoveAtSwap(Index);
    InventoryView.MarkArrayDirty();

    if (!bEntryIndexDirty)
    {
        EntryIndex.Remove(RemovedId);

        /* The last entry filled the hole, and is the only one whose position changed. */
        if (Index != LastIndex && Entries.IsValidIndex(Index))
        {
            EntryIndex.Add(Entries[Index].Item.ItemId, Index);
        }

        IndexedEntryCount = Entries.Num();
    }
}

void UPEPInventoryComponent::ResetEntries()
{
    InventoryView.Entries.Reset();
    InventoryView.MarkArrayDirty();
    MarkAllIndicesDirty();
}

void UPEPInventoryComponent::SetEntryLocation(
    FPEPInventoryEntry& Entry,
    const FPEPItemLocation& Location)
{
    /* The entry keeps its position in the array; only the indices keyed by location change. */
    UnindexEntryLocation(Entry.Item.ItemId, Entry.Location);
    Entry.Location = Location;
    IndexEntryLocation(Entry.Item.ItemId, Entry.Location);
}

const TArray<FPEPItemId>* UPEPInventoryComponent::FindContainerSlotOccupants(
    const FPEPItemId ParentItemId,
    const FName SlotName) const
{
    RefreshIndices();

    return ContainerSlotIndex.Find(
        FPEPContainerSlotKey(ParentItemId, SlotName));
}

// ---- Read -----------------------------------------------------------------------------------

bool UPEPInventoryComponent::ContainsItem(
    const FPEPItemId ItemId) const
{
    return FindEntry(ItemId) != nullptr;
}

const FPEPInventoryEntry* UPEPInventoryComponent::FindEntry(
    const FPEPItemId ItemId) const
{
    RefreshIndices();

    return FindIndexedEntry(ItemId);
}

FPEPInventoryEntry* UPEPInventoryComponent::FindMutableEntry(
    const FPEPItemId ItemId)
{
    return const_cast<FPEPInventoryEntry*>(FindEntry(ItemId));
}

const FPEPItemInstance* UPEPInventoryComponent::FindItem(
    const FPEPItemId& ItemId) const
{
    const FPEPInventoryEntry* Entry = FindEntry(ItemId);
    return Entry ? &Entry->Item : nullptr;
}

bool UPEPInventoryComponent::FindItemLocation(
    const FPEPItemId& ItemId,
    FPEPItemLocation& OutLocation) const
{
    const FPEPInventoryEntry* Entry = FindEntry(ItemId);
    if (!Entry)
    {
        OutLocation = FPEPItemLocation();
        return false;
    }

    OutLocation = Entry->Location;
    return true;
}

bool UPEPInventoryComponent::FindItemAtLocation(
    const FPEPItemLocation& Location,
    FPEPItemId& OutItemId) const
{
    OutItemId = FPEPItemId();

    if (!Location.IsStructurallyValid())
    {
        return false;
    }

    /*
     * Answered from the location indices: a named slot holds one item, a container slot a few,
     * and the whole store is neither. Equipment asks this once per slot per store change.
     */
    RefreshIndices();
    const TArray<FPEPItemId>* Candidates = Location.Type == EPEPItemLocationType::NamedSlot
        ? NamedSlotIndex.Find(Location.LocationTag)
        : ContainerSlotIndex.Find(
            FPEPContainerSlotKey(Location.ParentItemId, Location.ContainerSlotName));
    if (!Candidates)
    {
        return false;
    }
    for (const FPEPItemId CandidateId : *Candidates)
    {
        const FPEPInventoryEntry* Candidate = FindIndexedEntry(CandidateId);
        if (Candidate && Candidate->Location == Location)
        {
            OutItemId = CandidateId;
            return true;
        }
    }

    return false;
}

FPEPItemResult UPEPInventoryComponent::RemoveItem(
    const FPEPItemId& ItemId,
    const FPEPItemAccessContext& Access)
{
    return DestroyItem(ItemId, Access);
}

void UPEPInventoryComponent::GetDirectChildren(
    const FPEPItemId& ParentItemId,
    TArray<FPEPItemId>& OutChildItemIds) const
{
    OutChildItemIds.Reset();

    RefreshIndices();
    if (const TArray<FPEPItemId>* Children = ChildIndex.Find(ParentItemId))
    {
        OutChildItemIds.Append(*Children);
    }
}

void UPEPInventoryComponent::GetPublicChildren(
    const FPEPItemId& ParentItemId,
    TArray<FPEPItemId>& OutChildItemIds) const
{
    OutChildItemIds.Reset();
    const FPEPInventoryEntry* ParentEntry = FindEntry(ParentItemId);
    const FPEPItemFragment_Container* Container = ParentEntry
        ? ParentEntry->Item.GetDefinitionFragment<FPEPItemFragment_Container>()
        : nullptr;
    if (!Container)
    {
        return;
    }
    TArray<const FPEPInventoryEntry*> Children;
    GetDirectChildEntries(ParentItemId, Children);
    for (const FPEPInventoryEntry* Child : Children)
    {
        const FPEPContainerSlotDefinition* Slot =
            Container->FindSlot(Child->Location.ContainerSlotName);
        if (Slot && Slot->Purpose == EPEPContainerSlotPurpose::Attachment)
        {
            OutChildItemIds.Add(Child->Item.ItemId);
        }
    }
}

bool UPEPInventoryComponent::HasGrantedGameplayAccess(
	const FPEPItemId& ItemId,
	const FGameplayTag ActionTag) const
{
	const FPEPInventoryEntry* Entry = FindEntry(ItemId);
	if (!Entry || !ActionTag.IsValid())
	{
		return false;
	}

	/* Bounded by the store size instead of a visited set: a longer chain has met a cycle. */
	const int32 MaxSteps = InventoryView.Entries.Num();
	for (int32 Step = 0; Entry->Location.Type == EPEPItemLocationType::ItemContainer; ++Step)
	{
		const FPEPItemId ParentItemId = Entry->Location.ParentItemId;
		if (!ParentItemId.IsValid() || Step > MaxSteps)
		{
			return false;
		}

		const FPEPInventoryEntry* ParentEntry = FindEntry(ParentItemId);
		const FPEPItemFragment_Container* Container = ParentEntry
			? ParentEntry->Item.GetDefinitionFragment<FPEPItemFragment_Container>()
			: nullptr;
		if (!Container || !Container->GrantedGameplayAccess.HasTag(ActionTag))
		{
			return false;
		}
		Entry = ParentEntry;
	}
	return true;
}

void UPEPInventoryComponent::GetDirectChildEntries(
    const FPEPItemId ParentItemId,
    TArray<const FPEPInventoryEntry*>& OutChildren) const
{
    OutChildren.Reset();

    RefreshIndices();
    const TArray<FPEPItemId>* Children = ChildIndex.Find(ParentItemId);
    if (!Children)
    {
        return;
    }

    OutChildren.Reserve(Children->Num());
    for (const FPEPItemId ChildId : *Children)
    {
        if (const FPEPInventoryEntry* Child = FindIndexedEntry(ChildId))
        {
            OutChildren.Add(Child);
        }
    }
}

void UPEPInventoryComponent::GetDirectSingleSlotChildren(
    const FPEPItemId ParentItemId,
    TArray<const FPEPInventoryEntry*>& OutChildren) const
{
    OutChildren.Reset();

    const FPEPInventoryEntry* ParentEntry = FindEntry(ParentItemId);
    const FPEPItemFragment_Container* ContainerFragment =
        ParentEntry
            ? ParentEntry->Item.GetDefinitionFragment<
                FPEPItemFragment_Container>()
            : nullptr;

    if (!ContainerFragment)
    {
        return;
    }

    const TArray<FPEPItemId>* Children = ChildIndex.Find(ParentItemId);
    if (!Children)
    {
        return;
    }

    for (const FPEPItemId ChildId : *Children)
    {
        const FPEPInventoryEntry* Child = FindIndexedEntry(ChildId);
        if (!Child)
        {
            continue;
        }

        const FPEPContainerSlotDefinition* Slot =
            ContainerFragment->FindSlot(Child->Location.ContainerSlotName);

        if (Slot && Slot->Mode == EPEPContainerSlotMode::Single)
        {
            OutChildren.Add(Child);
        }
    }
}

const FPEPInventoryEntry*
UPEPInventoryComponent::FindItemInContainerSlot(
    const FPEPItemId ParentItemId,
    const FName SlotName) const
{
    const TArray<FPEPItemId>* Occupants =
        FindContainerSlotOccupants(ParentItemId, SlotName);

    if (!Occupants)
    {
        return nullptr;
    }

    for (const FPEPItemId OccupantId : *Occupants)
    {
        if (const FPEPInventoryEntry* Occupant = FindIndexedEntry(OccupantId))
        {
            return Occupant;
        }
    }

    return nullptr;
}

// ---- Requests -------------------------------------------------------------------------------

EPEPRequestResult UPEPInventoryComponent::RequestMoveItem(
    const FPEPItemId ItemId,
    const FPEPItemLocation Destination,
    int32& OutRequestId)
{
    OutRequestId = 0;
    AActor* Owner = GetOwner();
    if (!Owner
        || !ItemId.IsValid()
        || !Destination.IsStructurallyValid())
    {
        return EPEPRequestResult::Failed;
    }

    OutRequestId = AllocateRequestId();

    if (Owner->HasAuthority())
    {
        const FPEPItemResult Result =
            MoveItem(ItemId, Destination, GetOwnerRequestContext());

        ResolveInventoryRequest(
            OutRequestId,
            EPEPInventoryRequestKind::Move,
            Result);

        return Result.bSuccess
            ? EPEPRequestResult::Executed
            : EPEPRequestResult::Failed;
    }

    if (!Owner->HasLocalNetOwner())
    {
        OutRequestId = 0;
        return EPEPRequestResult::Failed;
    }

    Server_RequestMoveItem(ItemId, Destination, OutRequestId);
    return EPEPRequestResult::Submitted;
}

FPEPItemResult UPEPInventoryComponent::PreviewMoveItem(
    const FPEPItemId ItemId,
    const FPEPItemLocation Destination) const
{
	return CanMoveItem(ItemId, Destination, GetOwnerRequestContext());
}

EPEPRequestResult UPEPInventoryComponent::RequestSplitStack(
	const FPEPItemId SourceItemId,
	const int32 SplitCount,
	const FPEPItemLocation Destination,
	int32& OutRequestId)
{
	OutRequestId = 0;
	AActor* Owner = GetOwner();
	if (!Owner || !SourceItemId.IsValid() || SplitCount <= 0
		|| !Destination.IsStructurallyValid())
	{
		return EPEPRequestResult::Failed;
	}

	OutRequestId = AllocateRequestId();
	if (Owner->HasAuthority())
	{
		const FPEPItemResult Result = SplitStack(
			SourceItemId,
			SplitCount,
			Destination,
			GetOwnerRequestContext());
		ResolveInventoryRequest(OutRequestId, EPEPInventoryRequestKind::SplitStack, Result);
		return Result.bSuccess ? EPEPRequestResult::Executed : EPEPRequestResult::Failed;
	}

	if (!Owner->HasLocalNetOwner())
	{
		OutRequestId = 0;
		return EPEPRequestResult::Failed;
	}

	Server_RequestSplitStack(SourceItemId, SplitCount, Destination, OutRequestId);
	return EPEPRequestResult::Submitted;
}

FPEPItemResult UPEPInventoryComponent::PreviewSplitStack(
	const FPEPItemId SourceItemId,
	const int32 SplitCount,
	const FPEPItemLocation Destination) const
{
	return CanSplitStack(
		SourceItemId,
		SplitCount,
		Destination,
		GetOwnerRequestContext());
}

EPEPRequestResult UPEPInventoryComponent::RequestMergeStacks(
	const FPEPItemId SourceItemId,
	const FPEPItemId TargetItemId,
	const int32 TransferCount,
	int32& OutRequestId)
{
	OutRequestId = 0;
	AActor* Owner = GetOwner();
	if (!Owner || !SourceItemId.IsValid() || !TargetItemId.IsValid()
		|| SourceItemId == TargetItemId || TransferCount < 0)
	{
		return EPEPRequestResult::Failed;
	}

	OutRequestId = AllocateRequestId();
	if (Owner->HasAuthority())
	{
		const FPEPItemResult Result = MergeStacks(
			SourceItemId,
			TargetItemId,
			TransferCount,
			GetOwnerRequestContext());
		ResolveInventoryRequest(OutRequestId, EPEPInventoryRequestKind::MergeStacks, Result);
		return Result.bSuccess ? EPEPRequestResult::Executed : EPEPRequestResult::Failed;
	}

	if (!Owner->HasLocalNetOwner())
	{
		OutRequestId = 0;
		return EPEPRequestResult::Failed;
	}

	Server_RequestMergeStacks(SourceItemId, TargetItemId, TransferCount, OutRequestId);
	return EPEPRequestResult::Submitted;
}

FPEPItemResult UPEPInventoryComponent::PreviewMergeStacks(
	const FPEPItemId SourceItemId,
	const FPEPItemId TargetItemId,
	const int32 TransferCount) const
{
	return CanMergeStacks(
		SourceItemId,
		TargetItemId,
		TransferCount,
		GetOwnerRequestContext());
}

bool UPEPInventoryComponent::Server_RequestMoveItem_Validate(
    const FPEPItemId ItemId,
    const FPEPItemLocation Destination,
    const int32 RequestId)
{
    /* Malformed rather than refused: RequestMoveItem never sends any of these. */
    return ItemId.IsValid()
        && Destination.IsStructurallyValid()
        && RequestId > 0;
}

void UPEPInventoryComponent::Server_RequestMoveItem_Implementation(
    const FPEPItemId ItemId,
    const FPEPItemLocation Destination,
    const int32 RequestId)
{
    /*
     * The engine routes a component's server RPC only from the connection owning its actor, so the
     * owning controller is who sent this. Deriving it here rather than accepting it as a parameter
     * is what keeps a client from naming someone else.
     */
    const FPEPItemResult Result = RequestId > 0
        ? MoveItem(ItemId, Destination, GetRemoteRequestContext())
        : FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
	ResolveInventoryRequest(RequestId, EPEPInventoryRequestKind::Move, Result);
}

bool UPEPInventoryComponent::Server_RequestSplitStack_Validate(
	const FPEPItemId SourceItemId,
	const int32 SplitCount,
	const FPEPItemLocation Destination,
	const int32 RequestId)
{
	return SourceItemId.IsValid() && SplitCount > 0
		&& Destination.IsStructurallyValid() && RequestId > 0;
}

void UPEPInventoryComponent::Server_RequestSplitStack_Implementation(
	const FPEPItemId SourceItemId,
	const int32 SplitCount,
	const FPEPItemLocation Destination,
	const int32 RequestId)
{
	const FPEPItemResult Result = SplitStack(
		SourceItemId,
		SplitCount,
		Destination,
		GetRemoteRequestContext());
	ResolveInventoryRequest(RequestId, EPEPInventoryRequestKind::SplitStack, Result);
}

bool UPEPInventoryComponent::Server_RequestMergeStacks_Validate(
	const FPEPItemId SourceItemId,
	const FPEPItemId TargetItemId,
	const int32 TransferCount,
	const int32 RequestId)
{
	return SourceItemId.IsValid() && TargetItemId.IsValid()
		&& SourceItemId != TargetItemId && TransferCount >= 0 && RequestId > 0;
}

void UPEPInventoryComponent::Server_RequestMergeStacks_Implementation(
	const FPEPItemId SourceItemId,
	const FPEPItemId TargetItemId,
	const int32 TransferCount,
	const int32 RequestId)
{
	const FPEPItemResult Result = MergeStacks(
		SourceItemId,
		TargetItemId,
		TransferCount,
		GetRemoteRequestContext());
	ResolveInventoryRequest(RequestId, EPEPInventoryRequestKind::MergeStacks, Result);
}

// ---- Move -----------------------------------------------------------------------------------

FPEPItemResult UPEPInventoryComponent::ValidateMoveSourceAccess(
	const FPEPInventoryEntry& Entry,
	const FPEPItemLocation& Destination,
	const EPEPAccessRole Role,
	bool& bOutInternalContainerMove) const
{
	bOutInternalContainerMove =
		Entry.Location.Type == EPEPItemLocationType::ItemContainer
		&& Destination.Type == EPEPItemLocationType::ItemContainer
		&& Entry.Location.ParentItemId == Destination.ParentItemId;

	if (!bOutInternalContainerMove)
	{
		return ValidateCanTakeFromLocation(Entry, Role);
	}

	bool bSourceLocked = false;
	const FPEPContainerSlotDefinition* SourceSlot =
		ResolveContainerSlot(Entry.Location, bSourceLocked);
	if (!SourceSlot || !SourceSlot->IsValid())
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, Entry.Item.ItemId);
	}
	if (bSourceLocked || !SourceSlot->GetPolicyFor(Role).bCanMoveInside)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, Entry.Item.ItemId);
	}

	/* Rearranging inside a container is still reaching into it. */
	return ValidateExternalReach(Entry, Role);
}

FPEPItemResult UPEPInventoryComponent::CanMoveItem(
    const FPEPItemId& ItemId,
    const FPEPItemLocation& Destination,
    const FPEPItemAccessContext& Access) const
{
    const FPEPInventoryEntry* Entry =
        FindEntry(ItemId);

    if (!Entry)
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }

	const EPEPAccessRole Role = ResolveAccessRole(Access);
	bool bInternalContainerMove = false;
	const FPEPItemResult SourceValidation = ValidateMoveSourceAccess(
		*Entry,
		Destination,
		Role,
		bInternalContainerMove);
	if (!SourceValidation.bSuccess)
	{
		return SourceValidation;
	}

    return ValidateLocationForItem(
        Entry->Item,
        Destination,
        Role,
        bInternalContainerMove);
}

FPEPItemResult UPEPInventoryComponent::MoveItem(
    const FPEPItemId& ItemId,
    const FPEPItemLocation& Destination,
    const FPEPItemAccessContext& Access)
{
    if (!HasMutationAuthority())
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_NotAuthority,
            ItemId);
    }

    const FPEPItemResult Validation =
        CanMoveItem(ItemId, Destination, Access);

    if (!Validation.bSuccess)
    {
        return Validation;
    }

    return CommitValidatedMove(ItemId, Destination);
}

FPEPItemResult UPEPInventoryComponent::CommitValidatedMove(
    const FPEPItemId ItemId,
    const FPEPItemLocation& Destination)
{
    const FPEPInventoryEntry* Entry = FindEntry(ItemId);
    if (!Entry)
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }

    /* Read before the commit overwrites it. */
    const FPEPItemLocation PreviousLocation = Entry->Location;
    const FGameplayTag PreviousRoot = ResolveRootSlotTag(PreviousLocation);

    if (!CommitMoveItem(ItemId, Destination))
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }
    EmitMoved(ItemId, PreviousLocation, Destination, nullptr, &PreviousRoot);

    return FPEPItemResult::Success(
        ItemId,
        PEPCoreTags::Reason_None);
}

FPEPItemResult UPEPInventoryComponent::MutateItem(
    const FPEPItemId& ItemId,
    const FPEPItemStoreMutation& Mutation,
    const FPEPItemAccessContext& Access)
{
    if (!HasMutationAuthority())
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_NotAuthority,
            ItemId);
    }

    const FPEPInventoryEntry* Original =
        FindEntry(ItemId);

    if (!Original || !Mutation)
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }

    FPEPItemInstance WorkingCopy =
        Original->Item;

    const FPrimaryAssetId ExpectedDefinitionId =
        WorkingCopy.DefinitionId;

    if (!Mutation(WorkingCopy))
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_BlockedByState,
            ItemId);
    }

    if (!WorkingCopy.IsValid()
        || WorkingCopy.ItemId != ItemId
        || WorkingCopy.DefinitionId != ExpectedDefinitionId)
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_BlockedByState,
            ItemId);
    }

    /*
     * Looked up again rather than kept: the mutation is caller code, and a pointer into the store
     * taken before it ran is not one to write through after.
     */
    FPEPInventoryEntry* Entry =
        FindMutableEntry(ItemId);

    if (!Entry)
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }

    Entry->Item = MoveTemp(WorkingCopy);

    /*
     * A mutation that empties an item destroys it: a medkit at zero charges, a stack at zero, has
     * no existence left. It happens here so no caller can forget it and leave a ghost behind.
     *
     * The whole subtree goes, not just the item. Nothing forbids a definition from being both a
     * container and depletable, and removing such an item alone would leave its contents naming a
     * parent the store no longer holds — entries reachable by nothing, replicated forever.
     */
    if (Entry->Item.IsDepleted())
    {
        TArray<FPEPInventoryEntry> Discarded;
        CommitExtractItemTree(ItemId, Discarded);

        return FPEPItemResult::Success(
            ItemId,
            PEPCoreTags::Reason_None);
    }

    // A state-only change leaves identity and location untouched, but the entry still has to be
    // flagged or the owning client keeps the value it was sent when the item was added.
    InventoryView.MarkItemDirty(*Entry);

    EmitStateChanged(ItemId, Entry->Location);

    return FPEPItemResult::Success(
        ItemId,
        PEPCoreTags::Reason_None);
}

// ---- Extract / delete -----------------------------------------------------------------------

FPEPItemResult UPEPInventoryComponent::CanExtractItem(
    const FPEPItemId ItemId,
    const FPEPItemAccessContext& Access) const
{
    const FPEPInventoryEntry* Entry =
        FindEntry(ItemId);

    if (!Entry)
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }

    const FPEPItemResult AccessValidation =
        ValidateCanTakeFromLocation(*Entry, ResolveAccessRole(Access));

    if (!AccessValidation.bSuccess)
    {
        return AccessValidation;
    }

    /*
     * ponytail: le transfert récursif d'un sous-arbre reste
     * une opération séparée.
     */
    if (HasChildren(ItemId))
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_BlockedByState,
            ItemId);
    }

    return FPEPItemResult::Success(
        ItemId,
        PEPCoreTags::Reason_None);
}

FPEPItemResult UPEPInventoryComponent::ExtractItem(
    const FPEPItemId ItemId,
    FPEPInventoryEntry& OutEntry,
    const FPEPItemAccessContext& Access)
{
    if (!HasMutationAuthority())
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_NotAuthority,
            ItemId);
    }

    OutEntry = FPEPInventoryEntry();

    const FPEPItemResult Validation =
        CanExtractItem(ItemId, Access);

    if (!Validation.bSuccess)
    {
        return Validation;
    }

    if (!CommitExtractItem(ItemId, OutEntry))
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }
    EmitRemoved(OutEntry.Item, OutEntry.Location);

    return FPEPItemResult::Success(
        ItemId,
        PEPCoreTags::Reason_None);
}

FPEPItemResult UPEPInventoryComponent::CanExtractItemTree(
    const FPEPItemId ItemId,
    const FPEPItemAccessContext& Access) const
{
    const FPEPInventoryEntry* Entry =
        FindEntry(ItemId);

    if (!Entry)
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }

    return ValidateCanTakeFromLocation(*Entry, ResolveAccessRole(Access));
}

FPEPItemResult UPEPInventoryComponent::ExtractItemTree(
    const FPEPItemId ItemId,
    TArray<FPEPInventoryEntry>& OutEntries,
    const FPEPItemAccessContext& Access)
{
    if (!HasMutationAuthority())
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_NotAuthority,
            ItemId);
    }

    OutEntries.Reset();

    const FPEPItemResult Validation =
        CanExtractItemTree(ItemId, Access);

    if (!Validation.bSuccess)
    {
        return Validation;
    }

    if (!CommitExtractItemTree(ItemId, OutEntries))
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }

    return FPEPItemResult::Success(
        ItemId,
        PEPCoreTags::Reason_None);
}

FPEPItemResult UPEPInventoryComponent::RemoveItemTree(
    const FPEPItemId& ItemId,
    const FPEPItemAccessContext& Access)
{
    return DestroyItemTree(ItemId, Access);
}

FPEPItemResult UPEPInventoryComponent::CanDestroyItemTree(
    const FPEPItemId ItemId,
    const FPEPItemAccessContext& Access) const
{
    return CanExtractItemTree(ItemId, Access);
}

FPEPItemResult UPEPInventoryComponent::DestroyItemTree(
    const FPEPItemId ItemId,
    const FPEPItemAccessContext& Access)
{
    /*
     * Destroying is extracting without a caller who wants the items back, so it routes through the
     * same path rather than growing a second traversal that could disagree with it about ordering
     * or events. The snapshot is not waste: the locations it carries are what Removed reports.
     */
    TArray<FPEPInventoryEntry> Discarded;
    return ExtractItemTree(ItemId, Discarded, Access);
}

FPEPItemResult UPEPInventoryComponent::DestroyItem(
    const FPEPItemId ItemId,
    const FPEPItemAccessContext& Access)
{
    /*
     * Every other mutation gated on authority and this one did not, so IPEPItemStore::RemoveItem —
     * the path a consumed item takes — also removed the entry on the owning client, which then
     * held a store the server had never been told to change.
     */
    if (!HasMutationAuthority())
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_NotAuthority,
            ItemId);
    }

    const FPEPItemResult Validation =
        CanExtractItem(ItemId, Access);

    if (!Validation.bSuccess)
    {
        return Validation;
    }

    FPEPInventoryEntry DiscardedEntry;
    if (!CommitExtractItem(ItemId, DiscardedEntry))
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }
    EmitRemoved(DiscardedEntry.Item, DiscardedEntry.Location);

    return FPEPItemResult::Success(
        ItemId,
        PEPCoreTags::Reason_None);
}

// ---- Stacks ---------------------------------------------------------------------------------

FPEPItemResult UPEPInventoryComponent::BuildSplitStackPlan(
	const FPEPItemId SourceItemId,
	const int32 SplitCount,
	const FPEPItemLocation& Destination,
	const FPEPItemAccessContext& Access,
	FPEPItemInstance& OutSourceItem,
	FPEPItemInstance& OutSplitItem) const
{
	OutSourceItem.Reset();
	OutSplitItem.Reset();
	const FPEPInventoryEntry* SourceEntry = FindEntry(SourceItemId);
	if (!SourceEntry || SplitCount <= 0 || !Destination.IsStructurallyValid())
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, SourceItemId);
	}

	const EPEPAccessRole Role = ResolveAccessRole(Access);
	bool bInternalContainerMove = false;
	const FPEPItemResult SourceValidation = ValidateMoveSourceAccess(
		*SourceEntry,
		Destination,
		Role,
		bInternalContainerMove);
	if (!SourceValidation.bSuccess)
	{
		return SourceValidation;
	}

	/*
	 * The non-allocating split: a preview that consumed an id per hover would drain the allocator
	 * from a client merely dragging a stack about, and a refused request would burn one too.
	 */
	const FPEPItemResult SplitResult = PEPItemMutation::PreviewSplitStack(
		SourceEntry->Item,
		SplitCount,
		OutSourceItem,
		OutSplitItem);
	if (!SplitResult.bSuccess)
	{
		return SplitResult;
	}

	/* The would-be stack is checked where it would go; the answer is about the source. */
	const FPEPItemResult Placement = ValidateLocationForItem(
		OutSplitItem,
		Destination,
		Role,
		bInternalContainerMove);

	return Placement.bSuccess
		? FPEPItemResult::Success(SourceItemId, PEPCoreTags::Reason_None)
		: FPEPItemResult::Failure(Placement.ReasonTag, SourceItemId);
}

FPEPItemResult UPEPInventoryComponent::CanSplitStack(
	const FPEPItemId SourceItemId,
	const int32 SplitCount,
	const FPEPItemLocation& Destination,
	const FPEPItemAccessContext& Access) const
{
	FPEPItemInstance SourceCopy;
	FPEPItemInstance SplitCopy;
	return BuildSplitStackPlan(
		SourceItemId,
		SplitCount,
		Destination,
		Access,
		SourceCopy,
		SplitCopy);
}

FPEPItemResult UPEPInventoryComponent::SplitStack(
	const FPEPItemId SourceItemId,
	const int32 SplitCount,
	const FPEPItemLocation& Destination,
	const FPEPItemAccessContext& Access)
{
	if (!HasMutationAuthority())
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_NotAuthority, SourceItemId);
	}

	FPEPItemInstance PreviewSource;
	FPEPItemInstance PreviewSplit;
	const FPEPItemResult Validation = BuildSplitStackPlan(
		SourceItemId,
		SplitCount,
		Destination,
		Access,
		PreviewSource,
		PreviewSplit);
	if (!Validation.bSuccess)
	{
		return Validation;
	}

	const FPEPInventoryEntry* SourceEntry = FindEntry(SourceItemId);
	if (!SourceEntry)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, SourceItemId);
	}

	/*
	 * Only now, with the split approved, is an id allocated: the same rule re-run on the live
	 * stack, producing what the preview described under a real identity.
	 */
	FPEPItemInstance SourceAfter = SourceEntry->Item;
	FPEPItemInstance SplitItem;
	const FPEPItemResult SplitResult = PEPItemMutation::SplitStack(
		SourceAfter,
		SplitCount,
		SplitItem);
	if (!SplitResult.bSuccess)
	{
		return SplitResult;
	}

	const FPEPItemLocation SourceLocation = SourceEntry->Location;
	const FPEPItemId SplitItemId = SplitItem.ItemId;

	/* The new stack first: if it cannot be stored, the source has not been touched either. */
	if (!CommitAddItem(MoveTemp(SplitItem), Destination))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, SourceItemId);
	}

	/* Looked up after the append, which may have moved the array. */
	if (FPEPInventoryEntry* MutableSource = FindMutableEntry(SourceItemId))
	{
		MutableSource->Item = MoveTemp(SourceAfter);
		InventoryView.MarkItemDirty(*MutableSource);
	}

	EmitStateChanged(SourceItemId, SourceLocation);
	EmitAdded(SplitItemId, Destination);
	return FPEPItemResult::Success(SplitItemId, PEPCoreTags::Reason_None);
}

FPEPItemResult UPEPInventoryComponent::BuildMergeStacksPlan(
	const FPEPItemId SourceItemId,
	const FPEPItemId TargetItemId,
	const int32 TransferCount,
	const FPEPItemAccessContext& Access,
	FPEPItemInstance& OutSourceItem,
	FPEPItemInstance& OutTargetItem,
	int32& OutTransferredCount) const
{
	OutSourceItem.Reset();
	OutTargetItem.Reset();
	OutTransferredCount = 0;
	const FPEPInventoryEntry* SourceEntry = FindEntry(SourceItemId);
	const FPEPInventoryEntry* TargetEntry = FindEntry(TargetItemId);
	if (!SourceEntry || !TargetEntry || SourceItemId == TargetItemId || TransferCount < 0)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, SourceItemId);
	}

	const EPEPAccessRole Role = ResolveAccessRole(Access);
	bool bInternalContainerMove = false;
	const FPEPItemResult SourceValidation = ValidateMoveSourceAccess(
		*SourceEntry,
		TargetEntry->Location,
		Role,
		bInternalContainerMove);
	if (!SourceValidation.bSuccess)
	{
		return SourceValidation;
	}

	if (bInternalContainerMove)
	{
		bool bTargetLocked = false;
		const FPEPContainerSlotDefinition* TargetSlot =
			ResolveContainerSlot(TargetEntry->Location, bTargetLocked);
		if (!TargetSlot || !TargetSlot->IsValid())
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, TargetItemId);
		}
		if (bTargetLocked || !TargetSlot->GetPolicyFor(Role).bCanMoveInside)
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, TargetItemId);
		}
	}
	else if (!CanDepositIntoLocation(TargetEntry->Location, Role))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, TargetItemId);
	}

	/* The stack being topped up is a destination, and an outsider has to be able to reach it. */
	const FPEPItemResult TargetReach = ValidateExternalReach(*TargetEntry, Role);
	if (!TargetReach.bSuccess)
	{
		return FPEPItemResult::Failure(TargetReach.ReasonTag, TargetItemId);
	}

	OutSourceItem = SourceEntry->Item;
	OutTargetItem = TargetEntry->Item;
	return PEPItemMutation::MergeStacks(
		OutSourceItem,
		OutTargetItem,
		TransferCount,
		OutTransferredCount);
}

FPEPItemResult UPEPInventoryComponent::CanMergeStacks(
	const FPEPItemId SourceItemId,
	const FPEPItemId TargetItemId,
	const int32 TransferCount,
	const FPEPItemAccessContext& Access) const
{
	FPEPItemInstance SourceCopy;
	FPEPItemInstance TargetCopy;
	int32 TransferredCount = 0;
	return BuildMergeStacksPlan(
		SourceItemId,
		TargetItemId,
		TransferCount,
		Access,
		SourceCopy,
		TargetCopy,
		TransferredCount);
}

FPEPItemResult UPEPInventoryComponent::MergeStacks(
	const FPEPItemId SourceItemId,
	const FPEPItemId TargetItemId,
	const int32 TransferCount,
	const FPEPItemAccessContext& Access)
{
	if (!HasMutationAuthority())
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_NotAuthority, SourceItemId);
	}

	FPEPItemInstance SourceCopy;
	FPEPItemInstance TargetCopy;
	int32 TransferredCount = 0;
	const FPEPItemResult Validation = BuildMergeStacksPlan(
		SourceItemId,
		TargetItemId,
		TransferCount,
		Access,
		SourceCopy,
		TargetCopy,
		TransferredCount);
	if (!Validation.bSuccess)
	{
		return Validation;
	}

	/* Both looked up before anything is written, so a missing one refuses with nothing changed. */
	const FPEPInventoryEntry* SourceBefore = FindEntry(SourceItemId);
	FPEPInventoryEntry* MutableTarget = FindMutableEntry(TargetItemId);
	if (!SourceBefore || !MutableTarget)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, SourceItemId);
	}
	const FPEPItemLocation SourceLocation = SourceBefore->Location;
	const FPEPItemLocation TargetLocation = MutableTarget->Location;
	const bool bRemoveSource = SourceCopy.IsDepleted();
	const FPEPItemInstance RemovedSourceSnapshot = bRemoveSource
		? SourceBefore->Item
		: FPEPItemInstance();

	/* The target first, while both pointers are still good: removing the source moves entries. */
	MutableTarget->Item = MoveTemp(TargetCopy);
	InventoryView.MarkItemDirty(*MutableTarget);

	if (bRemoveSource)
	{
		FPEPInventoryEntry RemovedEntry;
		CommitExtractItem(SourceItemId, RemovedEntry);
	}
	else if (FPEPInventoryEntry* MutableSource = FindMutableEntry(SourceItemId))
	{
		MutableSource->Item = MoveTemp(SourceCopy);
		InventoryView.MarkItemDirty(*MutableSource);
	}

	if (bRemoveSource)
	{
		EmitRemoved(RemovedSourceSnapshot, SourceLocation);
	}
	else
	{
		EmitStateChanged(SourceItemId, SourceLocation);
	}
	EmitStateChanged(TargetItemId, TargetLocation);
	return FPEPItemResult::Success(TargetItemId, PEPCoreTags::Reason_None);
}

// ---- Swap -----------------------------------------------------------------------------------

FPEPItemResult UPEPInventoryComponent::CanSwapItems(
	const FPEPItemId& FirstItemId,
	const FPEPItemId& SecondItemId,
	const FPEPItemAccessContext& Access) const
{
	const FPEPInventoryEntry* FirstEntry = FindEntry(FirstItemId);
	const FPEPInventoryEntry* SecondEntry = FindEntry(SecondItemId);
	if (!FirstEntry || !SecondEntry || FirstItemId == SecondItemId)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, FirstItemId);
	}

	const EPEPAccessRole Role = ResolveAccessRole(Access);
	bool bFirstInternal = false;
	bool bSecondInternal = false;
	FPEPItemResult Validation = ValidateMoveSourceAccess(
		*FirstEntry,
		SecondEntry->Location,
		Role,
		bFirstInternal);
	if (!Validation.bSuccess)
	{
		return Validation;
	}
	Validation = ValidateMoveSourceAccess(
		*SecondEntry,
		FirstEntry->Location,
		Role,
		bSecondInternal);
	if (!Validation.bSuccess)
	{
		return Validation;
	}

	/*
	 * Each item is validated at the other's location with the other ignored. Once exchanged,
	 * neither sits where it did, so the cells the other leaves are free to it — and when the two
	 * locations are different slots, that is the whole answer. No simulated copy of the store is
	 * needed for it, which matters: this runs on every drag hover over an equipment slot.
	 */
	Validation = ValidateLocationForItem(
		FirstEntry->Item,
		SecondEntry->Location,
		Role,
		bFirstInternal,
		SecondItemId);
	if (!Validation.bSuccess)
	{
		return Validation;
	}
	Validation = ValidateLocationForItem(
		SecondEntry->Item,
		FirstEntry->Location,
		Role,
		bSecondInternal,
		FirstItemId);
	if (!Validation.bSuccess)
	{
		return Validation;
	}

	/*
	 * Within one grid slot it is not the whole answer. Each check above ignored the other item,
	 * so neither saw where the other lands: footprints of different sizes trading origins can
	 * claim a common cell — a 1x1 and a 2x1 swapped in a row of three both take the middle one.
	 * Each lands on the other's origin, in the other's orientation, and the two must stay apart.
	 */
	if (FirstEntry->Location.Type == EPEPItemLocationType::ItemContainer
		&& PEPInventoryComponentPrivate::AreLocationsInSameSlot(
			FirstEntry->Location,
			SecondEntry->Location))
	{
		bool bLocked = false;
		const FPEPContainerSlotDefinition* Slot =
			ResolveContainerSlot(FirstEntry->Location, bLocked);

		if (Slot && Slot->Mode == EPEPContainerSlotMode::Grid)
		{
			FPEPItemInventoryShapeData FirstShape;
			FPEPItemInventoryShapeData SecondShape;
			if (!PEPInventoryQuery::GetInventoryShapeData(FirstEntry->Item, FirstShape)
				|| !PEPInventoryQuery::GetInventoryShapeData(SecondEntry->Item, SecondShape))
			{
				return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, FirstItemId);
			}

			const FPEPGridPlacement FirstPlacement =
				PEPInventoryGrid::GetGridPlacement(FirstEntry->Location);
			const FPEPGridPlacement SecondPlacement =
				PEPInventoryGrid::GetGridPlacement(SecondEntry->Location);

			if (PEPInventoryValidation::DoFootprintsOverlap(
				SecondPlacement.Position,
				PEPInventoryValidation::OrientedSize(FirstShape.Size, SecondPlacement.bRotated),
				FirstPlacement.Position,
				PEPInventoryValidation::OrientedSize(SecondShape.Size, FirstPlacement.bRotated)))
			{
				return FPEPItemResult::Failure(PEPCoreTags::Reason_NoFreeSlot, FirstItemId);
			}
		}
	}

	return Validation;
}

FPEPItemResult UPEPInventoryComponent::SwapItems(
	const FPEPItemId& FirstItemId,
	const FPEPItemId& SecondItemId,
	const FPEPItemAccessContext& Access)
{
	if (!HasMutationAuthority())
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_NotAuthority, FirstItemId);
	}
	const FPEPItemResult Validation = CanSwapItems(FirstItemId, SecondItemId, Access);
	if (!Validation.bSuccess)
	{
		return Validation;
	}

	/* Both resolved before either moves: a location write never reshapes the array. */
	FPEPInventoryEntry* FirstEntry = FindMutableEntry(FirstItemId);
	FPEPInventoryEntry* SecondEntry = FindMutableEntry(SecondItemId);
	if (!FirstEntry || !SecondEntry)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, FirstItemId);
	}

	const FPEPItemLocation FirstLocation = FirstEntry->Location;
	const FPEPItemLocation SecondLocation = SecondEntry->Location;
	const FGameplayTag FirstRoot = ResolveRootSlotTag(FirstLocation);
	const FGameplayTag SecondRoot = ResolveRootSlotTag(SecondLocation);

	SetEntryLocation(*FirstEntry, SecondLocation);
	InventoryView.MarkItemDirty(*FirstEntry);
	SetEntryLocation(*SecondEntry, FirstLocation);
	InventoryView.MarkItemDirty(*SecondEntry);

	EmitMoved(FirstItemId, FirstLocation, SecondLocation, nullptr, &FirstRoot);
	EmitMoved(SecondItemId, SecondLocation, FirstLocation, nullptr, &SecondRoot);
	return FPEPItemResult::Success(FirstItemId, PEPCoreTags::Reason_None);
}

// ---- Blueprint reads ------------------------------------------------------------------------

void UPEPInventoryComponent::GetItemIds(TArray<FPEPItemId>& OutItemIds) const
{
    OutItemIds.Reset(InventoryView.Entries.Num());

    for (const FPEPInventoryEntry& Entry : InventoryView.Entries)
    {
        OutItemIds.Add(Entry.Item.ItemId);
    }
}

bool UPEPInventoryComponent::FindInventoryEntry(
    const FPEPItemId ItemId,
    FPEPInventoryEntry& OutEntry) const
{
    if (const FPEPInventoryEntry* Entry = FindEntry(ItemId))
    {
        OutEntry = *Entry;
        return true;
    }

    OutEntry = FPEPInventoryEntry();
    return false;
}

const UPEPItemDefinition* UPEPInventoryComponent::GetItemDefinitionById(const FPEPItemId ItemId) const
{
    const FPEPInventoryEntry* Entry = ItemId.IsValid() ? FindEntry(ItemId) : nullptr;
    return Entry ? Entry->Item.GetDefinitionIfLoaded() : nullptr;
}

void UPEPInventoryComponent::GetEntriesInContainerSlot(
    const FPEPItemId ParentItemId,
    const FName SlotName,
    TArray<FPEPInventoryEntry>& OutEntries) const
{
    OutEntries.Reset();

    const TArray<FPEPItemId>* Occupants = FindContainerSlotOccupants(ParentItemId, SlotName);
    if (!Occupants)
    {
        return;
    }

    TArray<const FPEPInventoryEntry*> Ordered;
    Ordered.Reserve(Occupants->Num());
    for (const FPEPItemId OccupantId : *Occupants)
    {
        if (const FPEPInventoryEntry* Occupant = FindIndexedEntry(OccupantId))
        {
            Ordered.Add(Occupant);
        }
    }

    /* Cell order, then id: the same slot reads the same way however its occupants arrived. */
    Ordered.Sort(
        [](const FPEPInventoryEntry& First, const FPEPInventoryEntry& Second)
        {
            const int32 CellOrder =
                PEPInventoryComponentPrivate::CompareGridCells(First.Location, Second.Location);
            if (CellOrder != 0)
            {
                return CellOrder < 0;
            }

            return First.Item.ItemId.Value < Second.Item.ItemId.Value;
        });

    OutEntries.Reserve(Ordered.Num());
    for (const FPEPInventoryEntry* Occupant : Ordered)
    {
        OutEntries.Add(*Occupant);
    }
}

int32 UPEPInventoryComponent::AllocateRequestId()
{
    if (NextRequestId <= 0)
    {
        NextRequestId = 1;
    }
    return NextRequestId++;
}

void UPEPInventoryComponent::ResolveInventoryRequest(
    const int32 RequestId,
    const EPEPInventoryRequestKind Kind,
    const FPEPItemResult& Result)
{
    FPEPInventoryRequestResult RequestResult;
    RequestResult.RequestId = RequestId;
    RequestResult.Kind = Kind;
    RequestResult.Result = Result;

    /*
     * A Client RPC needs a connection to travel on. Without an owning one — a crate, a corpse —
     * the engine drops it, and the answer would reach nobody; it is broadcast here instead.
     */
    AActor* Owner = GetOwner();
    if (Owner
        && Owner->HasAuthority()
        && Owner->GetNetMode() != NM_Standalone
        && Owner->HasNetOwner())
    {
        Client_ResolveInventoryRequest(RequestResult);
        return;
    }

    OnRequestResolved.Broadcast(RequestResult);
}

void UPEPInventoryComponent::Client_ResolveInventoryRequest_Implementation(
    const FPEPInventoryRequestResult RequestResult)
{
    OnRequestResolved.Broadcast(RequestResult);
}

// ---- Transfer -------------------------------------------------------------------------------

FPEPItemResult UPEPInventoryComponent::ValidateTransfer(
    const UPEPInventoryComponent* TargetInventory,
    const FPEPItemId ItemId,
    const FPEPItemLocation& Destination,
    const FPEPItemAccessContext& Access,
    TArray<FPEPInventoryEntry>* OutItemTree) const
{
    if (OutItemTree)
    {
        OutItemTree->Reset();
    }

    if (!TargetInventory)
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }

    if (TargetInventory == this)
    {
        return CanMoveItem(ItemId, Destination, Access);
    }

    /*
     * The same Access reaches both stores and each resolves its own role from it. Looting a corpse
     * evaluates the source under External and the destination under Owner without either side
     * being told which it is, so the two verdicts cannot be wired the wrong way round.
     */
    const FPEPItemResult ExtractValidation =
        CanExtractItemTree(ItemId, Access);
    if (!ExtractValidation.bSuccess)
    {
        return ExtractValidation;
    }

    TArray<FPEPInventoryEntry> LocalItemTree;
    TArray<FPEPInventoryEntry>& ItemTree = OutItemTree ? *OutItemTree : LocalItemTree;
    if (!BuildItemTreeSnapshot(ItemId, ItemTree))
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }

    return TargetInventory->ValidateItemTreeAtRootLocation(
        ItemTree,
        Destination,
        Access,
        false);
}

FPEPItemResult
UPEPInventoryComponent::CanTransferItemToInventory(
    const UPEPInventoryComponent* TargetInventory,
    const FPEPItemId ItemId,
    const FPEPItemLocation& Destination,
    const FPEPItemAccessContext& Access) const
{
    return ValidateTransfer(
        TargetInventory,
        ItemId,
        Destination,
        Access,
        nullptr);
}

FPEPItemResult UPEPInventoryComponent::TransferItemToInventory(
    UPEPInventoryComponent* TargetInventory,
    const FPEPItemId ItemId,
    const FPEPItemLocation& Destination,
    const FPEPItemAccessContext& Access)
{
    if (!HasMutationAuthority())
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_NotAuthority,
            ItemId);
    }

    if (!TargetInventory || !TargetInventory->HasMutationAuthority())
    {
        return FPEPItemResult::Failure(
            TargetInventory
                ? PEPCoreTags::Reason_NotAuthority
                : PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }

    TArray<FPEPInventoryEntry> ItemTree;
    const FPEPItemResult Validation =
        ValidateTransfer(
            TargetInventory,
            ItemId,
            Destination,
            Access,
            &ItemTree);

    if (!Validation.bSuccess)
    {
        return Validation;
    }

    if (TargetInventory == this)
    {
        return CommitValidatedMove(ItemId, Destination);
    }

    return CommitValidatedTransfer(
        TargetInventory,
        ItemId,
        Destination,
        MoveTemp(ItemTree));
}

FPEPItemResult UPEPInventoryComponent::CommitValidatedTransfer(
    UPEPInventoryComponent* TargetInventory,
    const FPEPItemId ItemId,
    const FPEPItemLocation& Destination,
    TArray<FPEPInventoryEntry>&& ItemTree)
{
    if (!TargetInventory
        || TargetInventory == this
        || ItemTree.IsEmpty()
        || ItemTree[0].Item.ItemId != ItemId)
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }

    TMap<FPEPItemId, FGameplayTag> RemovedRoots;
    RemovedRoots.Reserve(ItemTree.Num());
    TArray<TPair<FPEPItemInstance, FPEPItemLocation>> RemovedEntries;
    RemovedEntries.Reserve(ItemTree.Num());
    for (const FPEPInventoryEntry& Entry : ItemTree)
    {
        RemovedEntries.Emplace(Entry.Item, Entry.Location);
        RemovedRoots.Add(Entry.Item.ItemId, ResolveRootSlotTag(Entry.Location));
    }

    /* Children leave first so the source store never contains an orphan during the commit. */
    for (int32 Index = ItemTree.Num() - 1; Index >= 0; --Index)
    {
        FPEPInventoryEntry DiscardedEntry;
        CommitExtractItem(ItemTree[Index].Item.ItemId, DiscardedEntry);
    }

    ItemTree[0].Location = Destination;

    TArray<TPair<FPEPItemId, FPEPItemLocation>> AddedEntries;
    AddedEntries.Reserve(ItemTree.Num());
    for (FPEPInventoryEntry& Entry : ItemTree)
    {
        const FPEPItemId AddedItemId = Entry.Item.ItemId;
        const FPEPItemLocation AddedLocation = Entry.Location;
        if (TargetInventory->CommitAddItem(MoveTemp(Entry.Item), AddedLocation))
        {
            AddedEntries.Emplace(AddedItemId, AddedLocation);
        }
    }

    /* Both authoritative stores are final before observers or replication are notified. */
    for (const TPair<FPEPItemInstance, FPEPItemLocation>& RemovedEntry : RemovedEntries)
    {
        EmitRemoved(RemovedEntry.Key, RemovedEntry.Value, RemovedRoots.Find(RemovedEntry.Key.ItemId));
    }
    for (const TPair<FPEPItemId, FPEPItemLocation>& AddedEntry : AddedEntries)
    {
        TargetInventory->EmitAdded(AddedEntry.Key, AddedEntry.Value);
    }

    return FPEPItemResult::Success(
        ItemId,
        PEPCoreTags::Reason_None);
}

// ---- Placement ------------------------------------------------------------------------------

FPEPItemResult
UPEPInventoryComponent::ValidateLocationForItem(
	const FPEPItemInstance& Item,
	const FPEPItemLocation& Location,
	const EPEPAccessRole Role,
	const bool bInternalContainerMove,
	const FPEPItemId AlsoIgnoredItemId) const
{
	if (!Item.IsValid()
		|| !Location.IsStructurallyValid())
	{
		return FPEPItemResult::Failure(
			PEPCoreTags::Reason_InvalidTarget,
			Item.ItemId);
	}

	switch (Location.Type)
	{
	case EPEPItemLocationType::ItemContainer:
	{
		/*
		 * The placement on its own, before any slot is looked at: none, or a grid cell that is not
		 * before a grid's first. Another domain's placement means nothing to this store, and a
		 * negative cell is malformed rather than merely out of room.
		 */
		if (Location.ParentItemId == Item.ItemId
			|| !PEPInventoryGrid::IsPlacementWellFormed(Location))
		{
			return FPEPItemResult::Failure(
				PEPCoreTags::Reason_InvalidTarget,
				Item.ItemId);
		}

		const FPEPInventoryEntry* ParentEntry =
			FindEntry(Location.ParentItemId);

		if (!ParentEntry)
		{
			return FPEPItemResult::Failure(
				PEPCoreTags::Reason_InvalidTarget,
				Item.ItemId);
		}

		if (WouldCreateCycle(
			Item.ItemId,
			Location.ParentItemId))
		{
			return FPEPItemResult::Failure(
				PEPCoreTags::Reason_BlockedByState,
				Item.ItemId);
		}

		bool bLocked = false;

		const FPEPContainerSlotDefinition* ContainerSlot =
			ResolveContainerSlot(
				Location,
				bLocked);

		if (!ContainerSlot || !ContainerSlot->IsValid())
		{
			return FPEPItemResult::Failure(
				PEPCoreTags::Reason_InvalidTarget,
				Item.ItemId);
		}

		const FPEPSlotAccessPolicy Policy =
			ContainerSlot->GetPolicyFor(Role);

		const bool bCanPlace =
			bInternalContainerMove
				? Policy.bCanMoveInside
				: Policy.bCanPut;

		if (bLocked || !bCanPlace)
		{
			return FPEPItemResult::Failure(
				PEPCoreTags::Reason_BlockedByState,
				Item.ItemId);
		}

		/*
		 * The slot's own container was checked just above; what holds that container was not. An
		 * outsider putting into an open pouch inside a locked backpack, or into a container they
		 * cannot see, is refused here.
		 */
		const FPEPItemResult ParentReach =
			ValidateExternalReach(*ParentEntry, Role);

		if (!ParentReach.bSuccess)
		{
			return FPEPItemResult::Failure(
				ParentReach.ReasonTag,
				Item.ItemId);
		}

		if (!PEPItemQuery::
			DoesItemMatchRequiredBlockedTags(
				Item,
				ContainerSlot->RequiredItemTags,
				ContainerSlot->BlockedItemTags))
		{
			return FPEPItemResult::Failure(
				PEPCoreTags::Reason_BlockedByState,
				Item.ItemId);
		}

		if (ContainerSlot->Mode ==
			EPEPContainerSlotMode::Single)
		{
			/* One item, no position inside the slot: any placement, a grid cell included, is wrong. */
			if (Location.Placement.IsValid())
			{
				return FPEPItemResult::Failure(
					PEPCoreTags::Reason_InvalidTarget,
					Item.ItemId);
			}

			if (IsItemContainerSlotOccupied(
				Location.ParentItemId,
				Location.ContainerSlotName,
				Item.ItemId,
				AlsoIgnoredItemId))
			{
				return FPEPItemResult::Failure(
					PEPCoreTags::Reason_NoFreeSlot,
					Item.ItemId);
			}

			return FPEPItemResult::Success(
				Item.ItemId,
				PEPCoreTags::Reason_None);
		}

		FPEPItemInventoryShapeData ShapeData;

		if (!PEPInventoryQuery::GetInventoryShapeData(
			Item,
			ShapeData))
		{
			return FPEPItemResult::Failure(
				PEPCoreTags::Reason_InvalidTarget,
				Item.ItemId);
		}

		/* Well formed (checked above), so this is a grid placement or the unrotated origin. */
		const FPEPGridPlacement Cell =
			PEPInventoryGrid::GetGridPlacement(Location);

		if (Cell.bRotated
			&& (!ContainerSlot->bAllowRotation
				|| !ShapeData.bCanRotate))
		{
			return FPEPItemResult::Failure(
				PEPCoreTags::Reason_BlockedByState,
				Item.ItemId);
		}

		const FIntPoint ItemSize =
			PEPInventoryValidation::OrientedSize(ShapeData.Size, Cell.bRotated);

		if (!PEPInventoryValidation::IsFootprintInsideGrid(
			Cell.Position,
			ItemSize,
			ContainerSlot->GridSize))
		{
			return FPEPItemResult::Failure(
				PEPCoreTags::Reason_NoFreeSlot,
				Item.ItemId);
		}

		/*
		 * The cell map answers in O(footprint). It is unavailable while an occupant's shape cannot
		 * be resolved; the overlap test below then re-resolves on every call, as it always did,
		 * and fails the same way.
		 */
		const FPEPGridOccupancy* Occupancy =
			FindGridOccupancy(Location.ParentItemId, Location.ContainerSlotName);
		if (Occupancy && !Occupancy->bUnknown)
		{
			return IsGridFootprintFree(
				*Occupancy,
				Cell.Position,
				ItemSize,
				Item.ItemId,
				AlsoIgnoredItemId)
				? FPEPItemResult::Success(Item.ItemId, PEPCoreTags::Reason_None)
				: FPEPItemResult::Failure(PEPCoreTags::Reason_NoFreeSlot, Item.ItemId);
		}

		const TArray<FPEPItemId>* Occupants =
			FindContainerSlotOccupants(
				Location.ParentItemId,
				Location.ContainerSlotName);

		if (Occupants)
		{
			for (const FPEPItemId OccupantId : *Occupants)
			{
				if (OccupantId == Item.ItemId
					|| OccupantId == AlsoIgnoredItemId)
				{
					continue;
				}

				const FPEPInventoryEntry* Existing = FindIndexedEntry(OccupantId);
				if (!Existing)
				{
					continue;
				}

				FPEPItemInventoryShapeData
					ExistingShapeData;

				if (!PEPInventoryQuery::GetInventoryShapeData(
					Existing->Item,
					ExistingShapeData))
				{
					return FPEPItemResult::Failure(
						PEPCoreTags::Reason_BlockedByState,
						Item.ItemId);
				}

				const FPEPGridPlacement ExistingCell =
					PEPInventoryGrid::GetGridPlacement(Existing->Location);
				const FIntPoint ExistingSize = PEPInventoryValidation::OrientedSize(
					ExistingShapeData.Size,
					ExistingCell.bRotated);

				if (PEPInventoryValidation::DoFootprintsOverlap(
					Cell.Position,
					ItemSize,
					ExistingCell.Position,
					ExistingSize))
				{
					return FPEPItemResult::Failure(
						PEPCoreTags::Reason_NoFreeSlot,
						Item.ItemId);
				}
			}
		}

		return FPEPItemResult::Success(
			Item.ItemId,
			PEPCoreTags::Reason_None);
	}

	case EPEPItemLocationType::NamedSlot:
		if (IsNamedSlotOccupied(
			Location.LocationTag,
			Item.ItemId,
			AlsoIgnoredItemId))
		{
			return FPEPItemResult::Failure(
				PEPCoreTags::Reason_NoFreeSlot,
				Item.ItemId);
		}

		return ValidateExternalLocation(
			Item,
			Location);

	default:
		return FPEPItemResult::Failure(
			PEPCoreTags::Reason_InvalidTarget,
			Item.ItemId);
	}
}

FPEPItemResult UPEPInventoryComponent::ValidateExternalLocation(
    const FPEPItemInstance& Item,
    const FPEPItemLocation& Location) const
{
    const FPEPLocationValidatorBinding* BestBinding = nullptr;

    /* The deepest claim covering the tag decides; depths were counted when the tags were claimed. */
    for (const FPEPLocationValidatorBinding& Binding : LocationValidators)
    {
        if (!Binding.Validator.IsBound()
            || !Location.LocationTag.MatchesTag(Binding.RootLocationTag))
        {
            continue;
        }

        if (!BestBinding || Binding.TagDepth > BestBinding->TagDepth)
        {
            BestBinding = &Binding;
        }
    }

    if (!BestBinding)
    {
        /*
         * The store gives named slots no meaning of its own, so this is not "the slot is full" or
         * "the item is wrong" — it is "nobody ever claimed this tag here". Named, because the tag
         * alone travels a long way from the component that failed to register.
         */
        UE_LOG(
            LogPEPItems,
            Warning,
            TEXT("No location validator claims '%s' on '%s'. %d tag(s) are claimed here. A named "
                "slot only exists once a domain component registers it — equipment does that when "
                "it initializes, and only if it found this store."),
            *Location.LocationTag.ToString(),
            *GetName(),
            LocationValidators.Num());

        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_UnsupportedMode,
            Item.ItemId);
    }

    return BestBinding->Validator.Execute(Item, Location);
}

void UPEPInventoryComponent::AppendOrderedNamedSlotItems(
	TArray<FPEPItemId>& OutItemIds) const
{
	RefreshIndices();

	/* Read from the named-slot index rather than by walking every entry for the few worn ones. */
	TArray<const FPEPInventoryEntry*> NamedSlotEntries;
	NamedSlotEntries.Reserve(NamedSlotIndex.Num());

	for (const TPair<FGameplayTag, TArray<FPEPItemId>>& Pair : NamedSlotIndex)
	{
		for (const FPEPItemId OccupantId : Pair.Value)
		{
			if (const FPEPInventoryEntry* Entry = FindIndexedEntry(OccupantId))
			{
				NamedSlotEntries.Add(Entry);
			}
		}
	}

	NamedSlotEntries.Sort(
		[](const FPEPInventoryEntry& First, const FPEPInventoryEntry& Second)
		{
			if (First.Location.LocationTag != Second.Location.LocationTag)
			{
				return First.Location.LocationTag.GetTagName().LexicalLess(
					Second.Location.LocationTag.GetTagName());
			}

			return First.Item.ItemId.Value < Second.Item.ItemId.Value;
		});

	OutItemIds.Reserve(OutItemIds.Num() + NamedSlotEntries.Num());

	for (const FPEPInventoryEntry* Entry : NamedSlotEntries)
	{
		OutItemIds.Add(Entry->Item.ItemId);
	}
}

void UPEPInventoryComponent::AppendOrderedChildItems(
	const FPEPItemId ParentItemId,
	TArray<FPEPItemId>& OutItemIds) const
{
	TArray<const FPEPInventoryEntry*> Children;
	GetDirectChildEntries(ParentItemId, Children);

	/*
	 * Children of one parent occupy distinct locations, so slot and cell are already a total
	 * order; the id settles a corrupt store holding two on one cell the same way every time.
	 */
	Children.Sort(
		[](const FPEPInventoryEntry& First, const FPEPInventoryEntry& Second)
		{
			if (First.Location.ContainerSlotName != Second.Location.ContainerSlotName)
			{
				return First.Location.ContainerSlotName.LexicalLess(
					Second.Location.ContainerSlotName);
			}

			const int32 CellOrder =
				PEPInventoryComponentPrivate::CompareGridCells(First.Location, Second.Location);
			if (CellOrder != 0)
			{
				return CellOrder < 0;
			}

			return First.Item.ItemId.Value < Second.Item.ItemId.Value;
		});

	OutItemIds.Reserve(OutItemIds.Num() + Children.Num());

	for (const FPEPInventoryEntry* Child : Children)
	{
		OutItemIds.Add(Child->Item.ItemId);
	}
}

bool UPEPInventoryComponent::TryResolveAutoPlacement(
	const FPEPItemInstance& Item,
	FPEPItemLocation& OutLocation,
	const EPEPAccessRole Role,
	const FPEPItemLocation& ExcludedLocation) const
{
	OutLocation = FPEPItemLocation();

	if (!Item.IsValid())
	{
		return false;
	}

	/* 1. Wearing it: every registered named slot is a candidate. */
	for (const FGameplayTag SlotTag : GetAutoPlacementSlotTags())
	{
		const FPEPItemLocation Candidate = FPEPItemLocation::MakeNamedSlot(SlotTag);

		if (PEPInventoryComponentPrivate::AreLocationsInSameSlot(Candidate, ExcludedLocation))
		{
			continue;
		}

		if (ValidateLocationForItem(
			Item,
			Candidate,
			Role).bSuccess)
		{
			OutLocation = Candidate;
			return true;
		}
	}

	/*
	 * 2. Stowing it: the grid slots of what this store already holds.
	 *
	 * Breadth-first from the equipped containers, so a rig's own grid is offered before that of a
	 * pouch inside it — and so the pouch is offered at all, which is the ordinary arrangement and
	 * used to be unreachable.
	 *
	 * The queue is walked by index while it grows, which is what makes this level by level: the
	 * children of everything at one depth are appended behind everything still to try at that
	 * depth.
	 *
	 * Only grids take a stowed item, and a grid refuses an item without a footprint on every one
	 * of its cells, so a shapeless item has nowhere further to go.
	 */
	FPEPItemInventoryShapeData ItemShape;
	if (!PEPInventoryQuery::GetInventoryShapeData(Item, ItemShape))
	{
		return false;
	}

	TArray<FPEPItemId> PendingParents;
	AppendOrderedNamedSlotItems(PendingParents);

	TSet<FPEPItemId> VisitedParents;

	/*
	 * One candidate serves every cell tried: its slot and its placement are rewritten in place, so
	 * walking a grid allocates nothing. The location handed back is built once, for the cell
	 * chosen, in the form MakeGridLocation gives it.
	 */
	FPEPItemLocation CellCandidate = FPEPItemLocation::MakeItemContainer(FPEPItemId(), NAME_None);
	CellCandidate.Placement.InitializeAs(FPEPGridPlacement::StaticStruct());
	FPEPGridPlacement* CandidateCell = CellCandidate.Placement.GetMutablePtr<FPEPGridPlacement>();
	if (!CandidateCell)
	{
		return false;
	}

	for (int32 Cursor = 0; Cursor < PendingParents.Num(); ++Cursor)
	{
		const FPEPItemId ParentItemId = PendingParents[Cursor];

		/* An item cannot be stowed inside itself, and its own contents travel with it. */
		if (ParentItemId == Item.ItemId)
		{
			continue;
		}

		bool bAlreadyVisited = false;
		VisitedParents.Add(ParentItemId, &bAlreadyVisited);

		if (bAlreadyVisited)
		{
			continue;
		}

		const FPEPInventoryEntry* ParentEntry = FindEntry(ParentItemId);

		if (!ParentEntry)
		{
			continue;
		}

		const FPEPItemFragment_Container* Container =
			ParentEntry->Item.GetDefinitionFragment<
				FPEPItemFragment_Container>();

		if (!Container)
		{
			continue;
		}

		/* A locked container keeps its contents shut in, so nothing nested in it is reachable. */
		if (PEPInventoryComponentPrivate::IsContainerItemLocked(ParentEntry->Item, *Container))
		{
			continue;
		}

		/*
		 * What ValidateLocationForItem would decide identically for every cell of this container:
		 * whether the item may enter it at all. Asked once here rather than once per cell. A
		 * refusal skips the slots but not the descent below, exactly as the per-cell refusals did.
		 */
		const bool bParentAccepts =
			!WouldCreateCycle(Item.ItemId, ParentItemId)
			&& ValidateExternalReach(*ParentEntry, Role).bSuccess;

		for (const FPEPContainerSlotDefinition& Slot
			: Container->Slots)
		{
			if (!bParentAccepts)
			{
				break;
			}

			/* Never automatically into magazine_socket, optic_socket and the like. */
			if (!Slot.IsValid()
				|| Slot.Mode !=
					EPEPContainerSlotMode::Grid
				|| !Slot.GetPolicyFor(Role).bCanPut)
			{
				continue;
			}

			/* The whole slot is excluded, so none of its cells will do either. */
			if (PEPInventoryComponentPrivate::AreLocationsInSameSlot(
				FPEPItemLocation::MakeItemContainer(ParentItemId, Slot.SlotName),
				ExcludedLocation))
			{
				continue;
			}

			/* Slot-wide, so decided once for all its cells. */
			if (!PEPItemQuery::DoesItemMatchRequiredBlockedTags(
				Item,
				Slot.RequiredItemTags,
				Slot.BlockedItemTags))
			{
				continue;
			}

			/*
			 * Pruning, every step a necessary condition of what ValidateLocationForItem decides: a
			 * grid with fewer free cells than the footprint cannot take it, a footprint cannot
			 * start where it would hang off the grid, and it cannot cover a cell someone else
			 * holds. The cell map is unavailable while unknown; the per-cell validation then
			 * decides alone, as it always did.
			 */
			const FPEPGridOccupancy* Occupancy = FindGridOccupancy(ParentItemId, Slot.SlotName);
			const bool bUseOccupancy = Occupancy && !Occupancy->bUnknown;
			if (bUseOccupancy
				&& Occupancy->FreeCells < ItemShape.Size.X * ItemShape.Size.Y)
			{
				continue;
			}

			const int32 RotationCount =
				Slot.bAllowRotation ? 2 : 1;

			for (int32 RotationIndex = 0;
				RotationIndex < RotationCount;
				++RotationIndex)
			{
				const bool bRotated =
					RotationIndex == 1;

				/* A rotation the item does not allow is refused on every cell. */
				if (bRotated && !ItemShape.bCanRotate)
				{
					continue;
				}

				const FIntPoint Size =
					PEPInventoryValidation::OrientedSize(ItemShape.Size, bRotated);

				/* Past these the footprint hangs off the grid; an oversized item gets no cell. */
				const int32 LastY = Slot.GridSize.Y - Size.Y;
				const int32 LastX = Slot.GridSize.X - Size.X;

				for (int32 Y = 0;
					Y <= LastY;
					++Y)
				{
					for (int32 X = 0;
						X <= LastX;
						++X)
					{
						const FIntPoint Position(X, Y);

						if (bUseOccupancy
							&& !IsGridFootprintFree(
								*Occupancy,
								Position,
								Size,
								Item.ItemId,
								FPEPItemId()))
						{
							continue;
						}

						CellCandidate.ParentItemId = ParentItemId;
						CellCandidate.ContainerSlotName = Slot.SlotName;
						CandidateCell->Position = Position;
						CandidateCell->bRotated = bRotated;

						/*
						 * The full decision, for a cell the pruning could not rule out. An explicit
						 * origin reads exactly as no placement does, so the candidate's spelling
						 * changes nothing here.
						 */
						if (ValidateLocationForItem(
							Item,
							CellCandidate,
							Role).bSuccess)
						{
							OutLocation = PEPInventoryGrid::MakeGridLocation(
								ParentItemId,
								Slot.SlotName,
								Position,
								bRotated);
							return true;
						}
					}
				}
			}
		}

		/* Nothing here, but what this container holds may itself have room. */
		AppendOrderedChildItems(ParentItemId, PendingParents);
	}

	return false;
}

bool UPEPInventoryComponent::TryResolveAttachmentPlacement(
	const FPEPItemInstance& Item,
	FPEPItemLocation& OutLocation,
	const EPEPAccessRole Role) const
{
	OutLocation = FPEPItemLocation();
	if (!Item.IsValid())
	{
		return false;
	}

	// Inventory stays independent of Equipment: named-slot occupants are the equipped roots.
	// Finish this pass across all roots before considering any grid or free named slot.
	TArray<FPEPItemId> EquippedItemIds;
	AppendOrderedNamedSlotItems(EquippedItemIds);
	for (const FPEPItemId ParentItemId : EquippedItemIds)
	{
		if (ParentItemId == Item.ItemId)
		{
			continue;
		}

		const FPEPInventoryEntry* Parent = FindEntry(ParentItemId);
		const FPEPItemFragment_Container* Container = Parent
			? Parent->Item.GetDefinitionFragment<FPEPItemFragment_Container>()
			: nullptr;
		if (!Container)
		{
			continue;
		}

		for (const FPEPContainerSlotDefinition& Slot : Container->Slots)
		{
			if (!Slot.IsValid()
				|| Slot.Purpose != EPEPContainerSlotPurpose::Attachment
				|| Slot.Mode != EPEPContainerSlotMode::Single)
			{
				continue;
			}

			const FPEPItemLocation Candidate = FPEPItemLocation::MakeItemContainer(
				ParentItemId, Slot.SlotName);
			if (ValidateLocationForItem(Item, Candidate, Role).bSuccess)
			{
				OutLocation = Candidate;
				return true;
			}
		}
	}

	return false;
}

bool UPEPInventoryComponent::TryResolvePickupPlacement(
	const FPEPItemInstance& Item,
	FPEPItemLocation& OutLocation,
	const EPEPAccessRole Role) const
{
	return TryResolveAttachmentPlacement(Item, OutLocation, Role)
		|| TryResolveAutoPlacement(Item, OutLocation, Role);
}

// ---- Pickup ---------------------------------------------------------------------------------

void UPEPInventoryComponent::PlanPickupStackMerges(
	FPEPItemInstance& Item,
	const EPEPAccessRole Role,
	TArray<FPEPInventoryStackMerge>& OutMerges) const
{
	OutMerges.Reset();

	/*
	 * Candidates first, then filled in item-id order. Walking the entry array and filling as it
	 * went made which stacks a pickup topped up depend on where RemoveAtSwap had last moved them.
	 */
	TArray<const FPEPInventoryEntry*> Candidates;

	for (const FPEPInventoryEntry& Entry : InventoryView.Entries)
	{
		if (!PEPItemRules::AreStacksCompatible(Entry.Item, Item))
		{
			continue;
		}

		/*
		 * Merging bypasses placement entirely, so without this a sealed slot would still accept
		 * ammunition as long as a compatible stack already sat inside it — and an outsider could
		 * top up a stack they cannot see, or one behind a lock further up.
		 */
		if (!CanDepositIntoLocation(Entry.Location, Role)
			|| !ValidateExternalReach(Entry, Role).bSuccess)
		{
			continue;
		}

		Candidates.Add(&Entry);
	}

	Candidates.Sort(
		[](const FPEPInventoryEntry& First, const FPEPInventoryEntry& Second)
		{
			return First.Item.ItemId.Value < Second.Item.ItemId.Value;
		});

	for (const FPEPInventoryEntry* Entry : Candidates)
	{
		int32 TransferredCount = 0;
		FPEPItemInstance MergedItem = Entry->Item;

		/* Refuses a transfer of nothing, so a full stack never enters the plan as a no-op. */
		if (!PEPItemMutation::MergeStacks(
			Item,
			MergedItem,
			0,
			TransferredCount).bSuccess)
		{
			continue;
		}

		FPEPInventoryStackMerge& Planned = OutMerges.AddDefaulted_GetRef();
		Planned.TargetItemId = Entry->Item.ItemId;
		Planned.MergedItem = MoveTemp(MergedItem);
		Planned.Location = Entry->Location;

		if (Item.IsDepleted())
		{
			break;
		}
	}
}

FPEPItemResult UPEPInventoryComponent::PlanPickupItem(
	const FPEPItemInstance& Item,
	const EPEPAccessRole Role,
	FPEPItemInstance& OutRemainder,
	TArray<FPEPInventoryStackMerge>& OutMerges,
	FPEPItemLocation& OutDestination,
	bool& bOutAddRemainder) const
{
	OutMerges.Reset();
	OutDestination = FPEPItemLocation();
	bOutAddRemainder = false;

	/*
	 * The item itself first, whatever happens to it next. A pickup absorbed whole into carried
	 * stacks never needed a cell, and so never passed through CanAddItem: an id this store already
	 * held, or a malformed instance, went straight to the commit, whose only guard was a check()
	 * that a shipping server does not have.
	 */
	if (!PEPInventoryComponentPrivate::IsItemCatalogueReadyForInventoryMutation(*this))
	{
		return FPEPItemResult::Failure(
			PEPCoreTags::Reason_BlockedByState,
			Item.ItemId);
	}

	if (!Item.IsValid())
	{
		return FPEPItemResult::Failure(
			PEPCoreTags::Reason_InvalidTarget,
			Item.ItemId);
	}

	FPEPItemInstanceValidationResult InstanceValidation;
	if (!Item.ValidateInstance(InstanceValidation))
	{
		return FPEPItemResult::Failure(
			PEPCoreTags::Reason_InvalidTarget,
			Item.ItemId);
	}

	if (ContainsItem(Item.ItemId))
	{
		return FPEPItemResult::Failure(
			PEPCoreTags::Reason_BlockedByState,
			Item.ItemId);
	}

	OutRemainder = Item;

	// Mount the complete pickup before considering any storage, including stack merging.
	if (TryResolveAttachmentPlacement(Item, OutDestination, Role))
	{
		bOutAddRemainder = true;
		return FPEPItemResult::Success(
			Item.ItemId,
			PEPCoreTags::Reason_None);
	}

	/*
	 * Top up the stacks already carried before looking for a place: picking up cartridges must
	 * fill the ones on the rig rather than claim a new cell. A pickup the carried stacks swallow
	 * whole needs no cell, and asking for one would refuse a pickup that in fact succeeds.
	 */
	PlanPickupStackMerges(OutRemainder, Role, OutMerges);

	if (OutRemainder.IsDepleted())
	{
		return FPEPItemResult::Success(
			Item.ItemId,
			PEPCoreTags::Reason_None);
	}

	if (!TryResolveAutoPlacement(
		OutRemainder,
		OutDestination,
		Role))
	{
		OutMerges.Reset();
		return FPEPItemResult::Failure(
			PEPCoreTags::Reason_NoFreeSlot,
			Item.ItemId);
	}

	bOutAddRemainder = true;
	return FPEPItemResult::Success(
		Item.ItemId,
		PEPCoreTags::Reason_None);
}

FPEPItemResult
UPEPInventoryComponent::CanAddPickupItem(
	const FPEPItemInstance& Item,
	const FPEPItemAccessContext& Access) const
{
	/* The same plan AddPickupItem commits; the copies it works on are what keep this const. */
	FPEPItemInstance Remainder;
	TArray<FPEPInventoryStackMerge> PlannedMerges;
	FPEPItemLocation Destination;
	bool bAddRemainder = false;

	return PlanPickupItem(
		Item,
		ResolveAccessRole(Access),
		Remainder,
		PlannedMerges,
		Destination,
		bAddRemainder);
}

FPEPItemResult
UPEPInventoryComponent::CanAddPickupItemTree(
    const TArray<FPEPInventoryEntry>& ItemTree,
    const FPEPItemAccessContext& Access) const
{
    /* A tree of one is an ordinary pickup and gets the ordinary answer, stack merging included. */
    if (ItemTree.Num() == 1)
    {
        return CanAddPickupItem(ItemTree[0].Item, Access);
    }

    FPEPItemLocation RootLocation;
    return ValidatePickupItemTree(
        ItemTree,
        RootLocation,
        Access);
}

FPEPItemResult
UPEPInventoryComponent::CanAddPickupItemTree(
    const FPEPItemInstanceTree& ItemTree,
    const FPEPItemAccessContext& Access) const
{
    FPEPItemInstanceTree TreeCopy = ItemTree;

    return CanAddPickupItemTree(FlattenItemTree(MoveTemp(TreeCopy)), Access);
}

FPEPItemResult
UPEPInventoryComponent::AddPickupItem(
	FPEPItemInstance Item,
	const FPEPItemAccessContext& Access)
{
	if (!HasMutationAuthority())
	{
		return FPEPItemResult::Failure(
			PEPCoreTags::Reason_NotAuthority,
			Item.ItemId);
	}

	const FPEPItemId PickedUpItemId = Item.ItemId;

	/*
	 * Planned first and written afterwards, so a remainder that turns out to have nowhere to go
	 * leaves the carried stacks exactly as they were. The plan is the whole validation: nothing
	 * between it and the writes below can run.
	 */
	FPEPItemInstance Remainder;
	TArray<FPEPInventoryStackMerge> PlannedMerges;
	FPEPItemLocation Destination;
	bool bAddRemainder = false;

	const FPEPItemResult Plan = PlanPickupItem(
		Item,
		ResolveAccessRole(Access),
		Remainder,
		PlannedMerges,
		Destination,
		bAddRemainder);

	if (!Plan.bSuccess)
	{
		return Plan;
	}

	/* Every target confirmed before any is written: a pickup is all of its writes or none. */
	for (const FPEPInventoryStackMerge& Planned : PlannedMerges)
	{
		if (!ContainsItem(Planned.TargetItemId))
		{
			return FPEPItemResult::Failure(
				PEPCoreTags::Reason_InvalidTarget,
				PickedUpItemId);
		}
	}

	if (bAddRemainder
		&& !CommitAddItem(MoveTemp(Remainder), Destination))
	{
		return FPEPItemResult::Failure(
			PEPCoreTags::Reason_InvalidTarget,
			PickedUpItemId);
	}

	for (FPEPInventoryStackMerge& Planned : PlannedMerges)
	{
		if (FPEPInventoryEntry* Target = FindMutableEntry(Planned.TargetItemId))
		{
			Target->Item = MoveTemp(Planned.MergedItem);
			InventoryView.MarkItemDirty(*Target);
		}
	}

	/*
	 * Announced only once every write is done: BroadcastChange runs a multicast, and a subscriber
	 * that adds or removes an item would move the entries under a half-finished pickup.
	 */
	for (const FPEPInventoryStackMerge& Planned : PlannedMerges)
	{
		EmitStateChanged(Planned.TargetItemId, Planned.Location);
	}

	if (bAddRemainder)
	{
		EmitAdded(PickedUpItemId, Destination);
	}

	return FPEPItemResult::Success(PickedUpItemId, PEPCoreTags::Reason_None);
}

FPEPItemResult
UPEPInventoryComponent::AddPickupItemTree(
    TArray<FPEPInventoryEntry> ItemTree,
    const FPEPItemAccessContext& Access)
{
    if (!HasMutationAuthority())
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_NotAuthority,
            FPEPItemId());
    }

    /*
     * A tree of one is an ordinary pickup and takes the ordinary path, stack merging included:
     * a box of cartridges lying on the ground is still a box of cartridges. Anything with
     * children never merges — a rig holding things is not a quantity of rigs.
     */
    if (ItemTree.Num() == 1)
    {
        return AddPickupItem(MoveTemp(ItemTree[0].Item), Access);
    }

    FPEPItemLocation RootLocation;
    const FPEPItemResult Validation =
        ValidatePickupItemTree(
            ItemTree,
            RootLocation,
            Access);

    if (!Validation.bSuccess)
    {
        return Validation;
    }

    const FPEPItemId RootItemId = ItemTree[0].Item.ItemId;

    TArray<TPair<FPEPItemId, FPEPItemLocation>> AddedEntries;
    CommitAddItemTree(ItemTree, RootLocation, AddedEntries);
    EmitAddedEntries(AddedEntries);

    return FPEPItemResult::Success(
        RootItemId,
        PEPCoreTags::Reason_None);
}

void UPEPInventoryComponent::CommitAddItemTree(
    TArray<FPEPInventoryEntry>& ItemTree,
    const FPEPItemLocation& RootLocation,
    TArray<TPair<FPEPItemId, FPEPItemLocation>>& OutAdded)
{
    if (ItemTree.IsEmpty())
    {
        return;
    }

    OutAdded.Reserve(OutAdded.Num() + ItemTree.Num());

    const FPEPItemId RootItemId = ItemTree[0].Item.ItemId;

    if (!CommitAddItem(
        MoveTemp(ItemTree[0].Item),
        RootLocation))
    {
        UE_LOG(
            LogPEPItems,
            Error,
            TEXT("CommitAddItemTree: root %d could not be stored; its %d descendant(s) were dropped with it."),
            RootItemId.Value,
            ItemTree.Num() - 1);
        return;
    }
    OutAdded.Emplace(
        RootItemId,
        RootLocation);

    TSet<FPEPItemId> AddedIds;
    AddedIds.Add(RootItemId);

    TArray<int32> PendingIndices;
    PendingIndices.Reserve(FMath::Max(0, ItemTree.Num() - 1));

    for (int32 Index = 1; Index < ItemTree.Num(); ++Index)
    {
        PendingIndices.Add(Index);
    }

    /*
     * Bounded by the number of entries rather than by progress alone: every pass places at least
     * one item, so more passes than there are items is impossible. The guard is structural because
     * the alternative was a checkf, and a checkf is compiled out of a shipping build — leaving a
     * loop whose only exit was an assertion that no longer exists, on the server.
     */
    for (int32 Pass = 0; Pass < ItemTree.Num() && !PendingIndices.IsEmpty(); ++Pass)
    {
        bool bMadeProgress = false;

        for (int32 PendingIndex = PendingIndices.Num() - 1;
            PendingIndex >= 0;
            --PendingIndex)
        {
            const int32 TreeIndex = PendingIndices[PendingIndex];
            FPEPInventoryEntry& PendingEntry = ItemTree[TreeIndex];

            if (!AddedIds.Contains(PendingEntry.Location.ParentItemId))
            {
                continue;
            }

            const FPEPItemId ChildItemId = PendingEntry.Item.ItemId;
            const FPEPItemLocation ChildLocation = PendingEntry.Location;

            /* A refused child is dropped from the pass either way; its own children stay pending. */
            if (CommitAddItem(
                MoveTemp(PendingEntry.Item),
                ChildLocation))
            {
                AddedIds.Add(ChildItemId);
                OutAdded.Emplace(
                    ChildItemId,
                    ChildLocation);
            }

            PendingIndices.RemoveAtSwap(PendingIndex);
            bMadeProgress = true;
        }

        if (!ensureMsgf(
            bMadeProgress,
            TEXT("Validated item tree could not be inserted parent-first.")))
        {
            break;
        }
    }

    /*
     * Unreachable through the public entry points, which all validate the tree first. Reported
     * rather than ignored because the alternative is items that were accepted and then quietly
     * never stored.
     */
    if (!PendingIndices.IsEmpty())
    {
        UE_LOG(
            LogPEPItems,
            Error,
            TEXT("CommitAddItemTree: %d entry/entries could not be placed under a parent and were dropped."),
            PendingIndices.Num());
    }
}

void UPEPInventoryComponent::EmitAddedEntries(
    const TArray<TPair<FPEPItemId, FPEPItemLocation>>& Added)
{
    for (const TPair<FPEPItemId, FPEPItemLocation>& AddedEntry : Added)
    {
        EmitAdded(AddedEntry.Key, AddedEntry.Value);
    }
}

// ---- Loadout --------------------------------------------------------------------------------

FPEPItemResult UPEPInventoryComponent::CanApplyLoadout(
    const UPEPLoadoutPreset* Loadout,
    const FPEPItemAccessContext& Access) const
{
	if (!HasMutationAuthority())
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_NotAuthority);
	}

    if (!Loadout)
    {
        return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget);
    }

    TArray<FPEPInventoryTreePlacement> Placements;
    FText Error;

	if (!Loadout->CreatePlacements(GetOwner(), Placements, Error))
    {
        return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget);
    }

    return CanAddItemTrees(Placements, Access);
}

FPEPItemResult UPEPInventoryComponent::ApplyLoadout(
    const UPEPLoadoutPreset* Loadout,
    const FPEPItemAccessContext& Access)
{
    if (!HasMutationAuthority())
    {
        return FPEPItemResult::Failure(PEPCoreTags::Reason_NotAuthority);
    }

    if (!Loadout)
    {
        return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget);
    }

    TArray<FPEPInventoryTreePlacement> Placements;
    FText Error;

	if (!Loadout->CreatePlacements(GetOwner(), Placements, Error))
    {
        /*
         * A recipe that cannot be built is a content fault, not a refusal by the store, and the
         * reason tag alone would not say which entry or why.
         */
        UE_LOG(
            LogPEPItems,
            Warning,
            TEXT("ApplyLoadout failed to build '%s': %s"),
            *Loadout->GetName(),
            *Error.ToString());

        return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget);
    }

    return AddItemTrees(MoveTemp(Placements), Access);
}

FPEPItemResult UPEPInventoryComponent::ApplyLoadoutForOwner(const UPEPLoadoutPreset* Loadout)
{
    return ApplyLoadout(Loadout, GetOwnerRequestContext());
}

// ---- Drop -----------------------------------------------------------------------------------

FPEPItemInstanceTree UPEPInventoryComponent::MakeItemTreeFromEntries(
    TArray<FPEPInventoryEntry>&& Entries)
{
    FPEPItemInstanceTree Tree;

    if (Entries.IsEmpty())
    {
        return Tree;
    }

    /* BuildItemTreeSnapshot puts the root first, and the children keep the layout they had. */
    Tree.RootItem = MoveTemp(Entries[0].Item);
    Tree.Children.Reserve(Entries.Num() - 1);

    for (int32 Index = 1; Index < Entries.Num(); ++Index)
    {
        FPEPItemInstanceTreeEntry& Child = Tree.Children.AddDefaulted_GetRef();
        Child.Item = MoveTemp(Entries[Index].Item);
        Child.Location = MoveTemp(Entries[Index].Location);
    }

    return Tree;
}

FPEPItemResult UPEPInventoryComponent::CanDropItemTree(
    const FPEPItemId ItemId,
    const FPEPItemAccessContext& Access,
    const TSubclassOf<APEPItemWorldActor> OverrideActorClass) const
{
    if (!GetWorld())
    {
        return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
    }

    /*
     * Resolved rather than merely tested for emptiness, so this and the drop itself can never
     * disagree. It loads the class on first use; that happens once for the life of the process.
     */
    const FPEPItemInstance* Item = FindItem(ItemId);
    const TSubclassOf<APEPItemWorldActor> ActorClass = OverrideActorClass
        ? OverrideActorClass
        : PEPInventorySettings().ResolveWorldItemActorClassForDefinition(
            Item ? Item->GetDefinitionIfLoaded() : nullptr);

    if (!ActorClass || ActorClass->HasAnyClassFlags(CLASS_Abstract))
    {
        return FPEPItemResult::Failure(PEPCoreTags::Reason_UnsupportedMode, ItemId);
    }

    return CanExtractItemTree(ItemId, Access);
}

FPEPItemResult UPEPInventoryComponent::DropItemTree(
    const FPEPItemId ItemId,
    const FTransform& WorldTransform,
    const FPEPItemAccessContext& Access,
    APEPItemWorldActor*& OutSpawnedActor,
    const TSubclassOf<APEPItemWorldActor> OverrideActorClass)
{
    OutSpawnedActor = nullptr;

    if (!HasMutationAuthority())
    {
        return FPEPItemResult::Failure(PEPCoreTags::Reason_NotAuthority, ItemId);
    }

    /* Asked before anything moves: a refusal here must leave the store exactly as it was. */
    const FPEPItemResult CanDrop = CanDropItemTree(ItemId, Access, OverrideActorClass);
    if (!CanDrop.bSuccess)
    {
        return CanDrop;
    }

    UWorld* World = GetWorld();
    const FPEPItemInstance* Item = FindItem(ItemId);
    const TSubclassOf<APEPItemWorldActor> ActorClass = OverrideActorClass
        ? OverrideActorClass
        : PEPInventorySettings().ResolveWorldItemActorClassForDefinition(
            Item ? Item->GetDefinitionIfLoaded() : nullptr);

    /*
     * Spawned deferred so the tree is in place before BeginPlay. An actor that began play empty
     * would fall through to its spawn preset and build a second, unrelated item.
     */
    APEPItemWorldActor* Carrier = World->SpawnActorDeferred<APEPItemWorldActor>(
        ActorClass,
        WorldTransform,
        nullptr,
        nullptr,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

    if (!Carrier)
    {
        return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
    }

    TArray<FPEPInventoryEntry> Snapshot;
    if (!BuildItemTreeSnapshot(ItemId, Snapshot)
        || !Carrier->SetItemTree(MakeItemTreeFromEntries(MoveTemp(Snapshot))))
    {
        Carrier->Destroy();
        return FPEPItemResult::Failure(PEPCoreTags::Reason_UnsupportedMode, ItemId);
    }

    /*
     * Asked again, and not out of habit: unlike every other Do here, this one ran foreign code —
     * the carrier's construction and SetItemTree — between its decision and its write. The check
     * is a few lookups; a stale decision here would put the items in two places.
     */
    const FPEPItemResult StillTakeable = CanExtractItemTree(ItemId, Access);
    if (!StillTakeable.bSuccess)
    {
        Carrier->Destroy();
        return StillTakeable;
    }

    TArray<FPEPInventoryEntry> Extracted;
    if (!CommitExtractItemTree(ItemId, Extracted))
    {
        /* Validated a moment ago, so this is a bug rather than a refusal — lose the carrier, not the items. */
        Carrier->Destroy();
        return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
    }

    Carrier->FinishSpawning(WorldTransform);

    OutSpawnedActor = Carrier;

    return FPEPItemResult::Success(ItemId, PEPCoreTags::Reason_None);
}

FPEPItemResult UPEPInventoryComponent::DropItemTreeForOwner(
    const FPEPItemId ItemId,
    const FTransform WorldTransform,
    APEPItemWorldActor*& OutDroppedActor,
    const TSubclassOf<APEPItemWorldActor> OverrideActorClass)
{
    return DropItemTree(
        ItemId,
        WorldTransform,
        GetOwnerRequestContext(),
        OutDroppedActor,
        OverrideActorClass);
}

TArray<FPEPInventoryEntry> UPEPInventoryComponent::FlattenItemTree(
    FPEPItemInstanceTree&& Tree)
{
    TArray<FPEPInventoryEntry> Entries;
    Entries.Reserve(1 + Tree.Children.Num());

    FPEPInventoryEntry& RootEntry = Entries.AddDefaulted_GetRef();
    RootEntry.Item = MoveTemp(Tree.RootItem);

    for (FPEPItemInstanceTreeEntry& Child : Tree.Children)
    {
        FPEPInventoryEntry& Entry = Entries.AddDefaulted_GetRef();
        Entry.Item = MoveTemp(Child.Item);
        Entry.Location = MoveTemp(Child.Location);
    }

    return Entries;
}

// ---- Trees ----------------------------------------------------------------------------------

FPEPItemResult UPEPInventoryComponent::CanAddItemTreeAt(
    const FPEPItemInstanceTree& Tree,
    const FPEPItemLocation& RootLocation,
    const FPEPItemAccessContext& Access) const
{
    FPEPItemInstanceTree TreeCopy = Tree;

    return CanAddItemTrees(
        PEPInventoryComponentPrivate::MakeSingleTreePlacement(MoveTemp(TreeCopy), RootLocation),
        Access);
}

FPEPItemResult UPEPInventoryComponent::AddItemTreeAt(
    FPEPItemInstanceTree Tree,
    const FPEPItemLocation& RootLocation,
    const FPEPItemAccessContext& Access)
{
    return AddItemTrees(
        PEPInventoryComponentPrivate::MakeSingleTreePlacement(MoveTemp(Tree), RootLocation),
        Access);
}

FPEPItemResult UPEPInventoryComponent::CanAddItemTrees(
    const TArray<FPEPInventoryTreePlacement>& Placements,
    const FPEPItemAccessContext& Access) const
{
    if (Placements.IsEmpty())
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget);
    }

    /*
     * One tree has no other tree to collide with, so it is decided the way any single tree is —
     * and a lone item, the common restore of a loose slot, without copying the store at all.
     */
    if (Placements.Num() == 1)
    {
        FPEPItemInstanceTree TreeCopy = Placements[0].Tree;

        const FPEPItemResult Validation = ValidateItemTreeAtRootLocation(
            FlattenItemTree(MoveTemp(TreeCopy)),
            Placements[0].RootLocation,
            Access,
            /*bValidateChildPlacement*/ true);

        return Validation.bSuccess
            ? FPEPItemResult::Success(
                PEPInventoryComponentPrivate::SingleTreeItemId(Placements),
                PEPCoreTags::Reason_None)
            : Validation;
    }

    /*
     * One simulation for the whole forest. Each tree is validated against this inventory plus the
     * trees already accepted, which is the only way two of them claiming the same cell — or
     * carrying the same item id — can be caught at all: validated separately, each is legal.
     */
    const TStrongObjectPtr<UPEPInventoryComponent> Scratch(CreateValidationScratch());
    const EPEPAccessRole Role = ResolveAccessRole(Access);

    for (const FPEPInventoryTreePlacement& Placement : Placements)
    {
        FPEPItemInstanceTree TreeCopy = Placement.Tree;

        const FPEPItemResult Validation = ValidateItemTreeAgainstScratch(
            *Scratch,
            FlattenItemTree(MoveTemp(TreeCopy)),
            Placement.RootLocation,
            Role,
            /*bValidateChildPlacement*/ true);

        if (!Validation.bSuccess)
        {
            return Validation;
        }
    }

    return FPEPItemResult::Success(
        PEPInventoryComponentPrivate::SingleTreeItemId(Placements),
        PEPCoreTags::Reason_None);
}

FPEPItemResult UPEPInventoryComponent::AddItemTrees(
    TArray<FPEPInventoryTreePlacement> Placements,
    const FPEPItemAccessContext& Access)
{
    if (!HasMutationAuthority())
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_NotAuthority,
            PEPInventoryComponentPrivate::SingleTreeItemId(Placements));
    }

    const FPEPItemResult Validation = CanAddItemTrees(Placements, Access);

    if (!Validation.bSuccess)
    {
        return Validation;
    }

    /*
     * Validation ran over the entire forest before this point, so the commits below cannot fail.
     * They are not interleaved with it: a tree written while a later one is still being decided
     * would have to be taken back out, and there is no undo for a store that has already replicated.
     */
    TArray<TPair<FPEPItemId, FPEPItemLocation>> AddedEntries;

    for (FPEPInventoryTreePlacement& Placement : Placements)
    {
        TArray<FPEPInventoryEntry> Entries = FlattenItemTree(MoveTemp(Placement.Tree));
        CommitAddItemTree(Entries, Placement.RootLocation, AddedEntries);
    }

    EmitAddedEntries(AddedEntries);

    return Validation;
}

void UPEPInventoryComponent::ExportItemTrees(
    TArray<FPEPInventoryTreePlacement>& OutPlacements) const
{
    OutPlacements.Reset();

    /*
     * A root is whatever is not inside another item; store order is kept so a save is stable.
     * Each tree is walked through the child index, so the whole forest costs one pass over the
     * store rather than one per root.
     */
    for (const FPEPInventoryEntry& Entry : InventoryView.Entries)
    {
        if (Entry.Location.Type == EPEPItemLocationType::ItemContainer)
        {
            continue;
        }

        TArray<FPEPInventoryEntry> TreeEntries;
        if (!BuildItemTreeSnapshot(Entry.Item.ItemId, TreeEntries))
        {
            continue;
        }

        FPEPInventoryTreePlacement& Placement = OutPlacements.AddDefaulted_GetRef();
        Placement.RootLocation = Entry.Location;
        Placement.Tree = MakeItemTreeFromEntries(MoveTemp(TreeEntries));
    }
}

FPEPItemResult
UPEPInventoryComponent::AddPickupItemTree(
    FPEPItemInstanceTree ItemTree,
    const FPEPItemAccessContext& Access)
{
    return AddPickupItemTree(FlattenItemTree(MoveTemp(ItemTree)), Access);
}

bool UPEPInventoryComponent::WouldCreateCycle(
    const FPEPItemId MovingItemId,
    FPEPItemId ParentItemId) const
{
    /*
     * Bounded by the store size instead of a visited set: a chain of distinct ancestors cannot be
     * longer than the store, so a longer walk has met a cycle — which is a cycle either way.
     */
    const int32 MaxSteps = InventoryView.Entries.Num();

    for (int32 Step = 0; ParentItemId.IsValid(); ++Step)
    {
        if (ParentItemId == MovingItemId || Step > MaxSteps)
        {
            return true;
        }

        const FPEPInventoryEntry* ParentEntry = FindEntry(ParentItemId);
        if (!ParentEntry
            || ParentEntry->Location.Type !=
                EPEPItemLocationType::ItemContainer)
        {
            return false;
        }

        ParentItemId = ParentEntry->Location.ParentItemId;
    }

    return false;
}

bool UPEPInventoryComponent::HasChildren(
    const FPEPItemId ParentItemId) const
{
    RefreshIndices();

    /* The index drops a parent's list once it empties, so presence alone answers. */
    return ChildIndex.Contains(ParentItemId);
}

void UPEPInventoryComponent::AppendDescendantEntries(
    const FPEPItemId ParentItemId,
    TArray<const FPEPInventoryEntry*>& OutEntries) const
{
    RefreshIndices();

    /*
     * Depth-first, each parent before its children, with an explicit stack: children are pushed
     * in reverse so they come off in the order they arrived, which is the order the recursion this
     * replaces produced.
     *
     * Cycle-safe without a visited set. Each item names one parent, so from a start outside any
     * cycle the walk can never enter one, and from a start inside one the first item met twice is
     * the start itself — which is never pushed. The store size bounds the output regardless.
     */
    TArray<FPEPItemId, TInlineAllocator<32>> Pending;

    const auto PushChildren = [this, ParentItemId, &Pending](const FPEPItemId Id)
    {
        if (const TArray<FPEPItemId>* Children = ChildIndex.Find(Id))
        {
            for (int32 Index = Children->Num() - 1; Index >= 0; --Index)
            {
                if ((*Children)[Index] != ParentItemId)
                {
                    Pending.Add((*Children)[Index]);
                }
            }
        }
    };

    PushChildren(ParentItemId);

    const int32 MaxEntries = InventoryView.Entries.Num();
    int32 Appended = 0;

    while (!Pending.IsEmpty() && Appended < MaxEntries)
    {
        const FPEPItemId ChildId = Pending.Pop();
        const FPEPInventoryEntry* Child = FindIndexedEntry(ChildId);
        if (!Child)
        {
            continue;
        }

        OutEntries.Add(Child);
        ++Appended;
        PushChildren(ChildId);
    }
}

bool UPEPInventoryComponent::BuildItemTreeSnapshot(
    const FPEPItemId RootItemId,
    TArray<FPEPInventoryEntry>& OutEntries) const
{
    OutEntries.Reset();

    const FPEPInventoryEntry* RootEntry = FindEntry(RootItemId);
    if (!RootEntry)
    {
        return false;
    }

    TArray<const FPEPInventoryEntry*> TreeEntries;
    TreeEntries.Add(RootEntry);
    AppendDescendantEntries(RootItemId, TreeEntries);

    OutEntries.Reserve(TreeEntries.Num());
    for (const FPEPInventoryEntry* TreeEntry : TreeEntries)
    {
        if (TreeEntry)
        {
            OutEntries.Add(*TreeEntry);
        }
    }

    return !OutEntries.IsEmpty();
}

FPEPItemResult UPEPInventoryComponent::ValidatePickupItemTree(
    const TArray<FPEPInventoryEntry>& ItemTree,
    FPEPItemLocation& OutRootLocation,
    const FPEPItemAccessContext& Access) const
{
    OutRootLocation = FPEPItemLocation();

    if (ItemTree.IsEmpty())
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget);
    }

    const FPEPInventoryEntry& RootEntry = ItemTree[0];
    const FPEPItemId RootItemId = RootEntry.Item.ItemId;

    if (!RootEntry.Item.IsValid())
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            RootItemId);
    }

    if (!TryResolvePickupPlacement(
        RootEntry.Item,
        OutRootLocation,
        ResolveAccessRole(Access)))
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_NoFreeSlot,
            RootItemId);
    }

    return ValidateItemTreeAtRootLocation(ItemTree, OutRootLocation, Access);
}

UPEPInventoryComponent* UPEPInventoryComponent::CreateValidationScratch(
    const bool bCopyEntries) const
{
    UPEPInventoryComponent* Scratch =
        NewObject<UPEPInventoryComponent>(GetTransientPackage());

    if (bCopyEntries)
    {
        Scratch->InventoryView.Entries = InventoryView.Entries;
    }
    Scratch->LocationValidators = LocationValidators;
    Scratch->ExternalAccess = ExternalAccess;
    Scratch->bAutoPlacementSlotTagsDirty = true;
    Scratch->MarkAllIndicesDirty();

    return Scratch;
}

FPEPItemResult UPEPInventoryComponent::ValidateItemTreeAtRootLocation(
    const TArray<FPEPInventoryEntry>& ItemTree,
    const FPEPItemLocation& RootLocation,
    const FPEPItemAccessContext& Access,
    const bool bValidateChildPlacement) const
{
    const EPEPAccessRole Role = ResolveAccessRole(Access);

    /*
     * A lone item has no siblings and no parent inside the tree to simulate, so the live store
     * answers exactly what a copy of it would: the same checks, in the same order, as the scratch
     * path below makes for a tree of one. Looting a single item no longer copies the looter's
     * whole inventory to decide it.
     */
    if (ItemTree.Num() == 1)
    {
        const FPEPItemInstance& Item = ItemTree[0].Item;

        if (!PEPInventoryComponentPrivate::IsItemCatalogueReadyForInventoryMutation(*this))
        {
            return FPEPItemResult::Failure(
                PEPCoreTags::Reason_BlockedByState,
                Item.ItemId);
        }

        if (!RootLocation.IsStructurallyValid())
        {
            return FPEPItemResult::Failure(
                PEPCoreTags::Reason_InvalidTarget);
        }

        const FPEPItemResult Incoming = ValidateIncomingItem(*this, Item);
        if (!Incoming.bSuccess)
        {
            return Incoming;
        }

        return ValidateLocationForItem(
            Item,
            RootLocation,
            Role);
    }

    const TStrongObjectPtr<UPEPInventoryComponent> Scratch(CreateValidationScratch());

    return ValidateItemTreeAgainstScratch(
        *Scratch,
        ItemTree,
        RootLocation,
        Role,
        bValidateChildPlacement);
}

FPEPItemResult UPEPInventoryComponent::ValidateIncomingItem(
    const UPEPInventoryComponent& Store,
    const FPEPItemInstance& Item)
{
    const FPEPItemId ItemId = Item.ItemId;

    if (!Item.IsValid()
        || !ItemId.IsValid()
        || Store.ContainsItem(ItemId))
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }

    FPEPItemInstanceValidationResult InstanceValidation;
    if (!Item.ValidateInstance(InstanceValidation))
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }

    return FPEPItemResult::Success(
        ItemId,
        PEPCoreTags::Reason_None);
}

FPEPItemResult UPEPInventoryComponent::ValidateItemTreeAgainstScratch(
    UPEPInventoryComponent& Scratch,
    const TArray<FPEPInventoryEntry>& ItemTree,
    const FPEPItemLocation& RootLocation,
    const EPEPAccessRole Role,
    const bool bValidateChildPlacement) const
{
	if (!PEPInventoryComponentPrivate::IsItemCatalogueReadyForInventoryMutation(*this))
	{
		return FPEPItemResult::Failure(
			PEPCoreTags::Reason_BlockedByState,
			ItemTree.IsEmpty() ? FPEPItemId() : ItemTree[0].Item.ItemId);
	}

    if (ItemTree.IsEmpty() || !RootLocation.IsStructurallyValid())
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget);
    }

    const FPEPInventoryEntry& RootEntry = ItemTree[0];
    const FPEPItemId RootItemId = RootEntry.Item.ItemId;

    TMap<FPEPItemId, const FPEPInventoryEntry*> TreeEntriesById;
    TreeEntriesById.Reserve(ItemTree.Num());

    for (const FPEPInventoryEntry& Entry : ItemTree)
    {
        const FPEPItemId EntryItemId = Entry.Item.ItemId;

        if (TreeEntriesById.Contains(EntryItemId))
        {
            return FPEPItemResult::Failure(
                PEPCoreTags::Reason_InvalidTarget,
                EntryItemId);
        }

        const FPEPItemResult Incoming = ValidateIncomingItem(Scratch, Entry.Item);
        if (!Incoming.bSuccess)
        {
            return Incoming;
        }

        TreeEntriesById.Add(
            EntryItemId,
            &Entry);
    }

    for (int32 Index = 1; Index < ItemTree.Num(); ++Index)
    {
        const FPEPInventoryEntry& Entry = ItemTree[Index];

        if (Entry.Location.Type != EPEPItemLocationType::ItemContainer
            || !Entry.Location.ParentItemId.IsValid()
            || Entry.Location.ParentItemId == Entry.Item.ItemId
            || !TreeEntriesById.Contains(Entry.Location.ParentItemId))
        {
            return FPEPItemResult::Failure(
                PEPCoreTags::Reason_InvalidTarget,
                Entry.Item.ItemId);
        }

        /*
         * Up to the root, bounded by the tree's size: a chain of distinct members is shorter than
         * the tree, so a longer one has met a cycle. No set per member.
         */
        FPEPItemId Cursor = Entry.Location.ParentItemId;
        bool bReachedRoot = false;

        for (int32 Step = 0; Cursor.IsValid(); ++Step)
        {
            if (Cursor == RootItemId)
            {
                bReachedRoot = true;
                break;
            }

            if (Step >= ItemTree.Num())
            {
                return FPEPItemResult::Failure(
                    PEPCoreTags::Reason_BlockedByState,
                    Entry.Item.ItemId);
            }

            const FPEPInventoryEntry* const* ParentEntry =
                TreeEntriesById.Find(Cursor);

            if (!ParentEntry
                || !*ParentEntry
                || (*ParentEntry)->Location.Type != EPEPItemLocationType::ItemContainer)
            {
                break;
            }

            Cursor = (*ParentEntry)->Location.ParentItemId;
        }

        if (!bReachedRoot)
        {
            return FPEPItemResult::Failure(
                PEPCoreTags::Reason_InvalidTarget,
                Entry.Item.ItemId);
        }
    }

    /*
     * Every placement is decided against the simulation, which holds this inventory plus whatever
     * has already been accepted into it — the earlier trees of a forest, then the parents of this
     * one. That is what catches invalid slot names, locks, tag rules, grid bounds and overlap,
     * between siblings and between trees alike, without touching the replicated serializer.
     */
    const FPEPItemResult RootValidation = Scratch.ValidateLocationForItem(
        RootEntry.Item,
        RootLocation,
        Role);
    if (!RootValidation.bSuccess)
    {
        return RootValidation;
    }

    FPEPItemInstance RootCopy = RootEntry.Item;
    if (!Scratch.CommitAddItem(MoveTemp(RootCopy), RootLocation))
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            RootItemId);
    }

    TSet<FPEPItemId> AddedIds;
    AddedIds.Add(RootItemId);
    TArray<int32> PendingIndices;
    for (int32 Index = 1; Index < ItemTree.Num(); ++Index)
    {
        PendingIndices.Add(Index);
    }

    while (!PendingIndices.IsEmpty())
    {
        bool bMadeProgress = false;
        for (int32 PendingIndex = PendingIndices.Num() - 1; PendingIndex >= 0; --PendingIndex)
        {
            const FPEPInventoryEntry& PendingEntry = ItemTree[PendingIndices[PendingIndex]];
            if (!AddedIds.Contains(PendingEntry.Location.ParentItemId))
            {
                continue;
            }

            if (bValidateChildPlacement)
            {
                const FPEPItemResult ChildValidation = Scratch.ValidateLocationForItem(
                    PendingEntry.Item,
                    PendingEntry.Location,
                    Role);
                if (!ChildValidation.bSuccess)
                {
                    return ChildValidation;
                }
            }

            FPEPItemInstance ChildCopy = PendingEntry.Item;
            if (!Scratch.CommitAddItem(MoveTemp(ChildCopy), PendingEntry.Location))
            {
                return FPEPItemResult::Failure(
                    PEPCoreTags::Reason_InvalidTarget,
                    PendingEntry.Item.ItemId);
            }
            AddedIds.Add(PendingEntry.Item.ItemId);
            PendingIndices.RemoveAtSwap(PendingIndex);
            bMadeProgress = true;
        }

        if (!bMadeProgress)
        {
            return FPEPItemResult::Failure(
                PEPCoreTags::Reason_InvalidTarget,
                RootItemId);
        }
    }

    return RootValidation;
}

bool UPEPInventoryComponent::IsNamedSlotOccupied(
    const FGameplayTag SlotTag,
    const FPEPItemId IgnoredItemId,
    const FPEPItemId AlsoIgnoredItemId) const
{
    RefreshIndices();
    const TArray<FPEPItemId>* Occupants = NamedSlotIndex.Find(SlotTag);
    if (!Occupants)
    {
        return false;
    }
    for (const FPEPItemId OccupantId : *Occupants)
    {
        if (OccupantId != IgnoredItemId && OccupantId != AlsoIgnoredItemId)
        {
            return true;
        }
    }

    return false;
}

bool UPEPInventoryComponent::IsItemContainerSlotOccupied(
    const FPEPItemId ParentItemId,
    const FName SlotName,
    const FPEPItemId IgnoredItemId,
    const FPEPItemId AlsoIgnoredItemId) const
{
    const TArray<FPEPItemId>* Occupants =
        FindContainerSlotOccupants(ParentItemId, SlotName);

    if (!Occupants)
    {
        return false;
    }

    for (const FPEPItemId OccupantId : *Occupants)
    {
        if (OccupantId != IgnoredItemId && OccupantId != AlsoIgnoredItemId)
        {
            return true;
        }
    }

    return false;
}

// ---- Commits --------------------------------------------------------------------------------

bool UPEPInventoryComponent::CommitAddItem(
    FPEPItemInstance&& Item,
    const FPEPItemLocation& Location)
{
    const FPEPItemId ItemId = Item.ItemId;

    if (!ensureMsgf(
        Item.IsValid()
            && ItemId.IsValid()
            && Location.IsStructurallyValid()
            && !ContainsItem(ItemId),
        TEXT("CommitAddItem: item %d reached the store without the validation its caller owed it; refused."),
        ItemId.Value))
    {
        return false;
    }

    FPEPInventoryEntry Entry;
    Entry.Item = MoveTemp(Item);
    Entry.Location = Location;

    FPEPInventoryEntry& Added = AppendEntry(MoveTemp(Entry));
    InventoryView.MarkItemDirty(Added);
    return true;
}

bool UPEPInventoryComponent::CommitMoveItem(
    const FPEPItemId ItemId,
    const FPEPItemLocation& Destination)
{
    FPEPInventoryEntry* Entry = FindMutableEntry(ItemId);

    if (!ensureMsgf(
        Entry && Destination.IsStructurallyValid(),
        TEXT("CommitMoveItem: item %d cannot be moved there; refused."),
        ItemId.Value))
    {
        return false;
    }

    SetEntryLocation(*Entry, Destination);

    InventoryView.MarkItemDirty(*Entry);
    return true;
}

bool UPEPInventoryComponent::CommitExtractItem(
    const FPEPItemId ItemId,
    FPEPInventoryEntry& OutEntry)
{
    RefreshIndices();

    const int32* FoundIndex = EntryIndex.Find(ItemId);
    const int32 Index = FoundIndex ? *FoundIndex : INDEX_NONE;

    if (!ensureMsgf(
        InventoryView.Entries.IsValidIndex(Index),
        TEXT("CommitExtractItem: item %d is not in the store; nothing removed."),
        ItemId.Value))
    {
        return false;
    }

    RemoveEntryAtSwap(Index, OutEntry);
    return true;
}

bool UPEPInventoryComponent::CommitExtractItemTree(
    const FPEPItemId RootItemId,
    TArray<FPEPInventoryEntry>& OutEntries)
{
    if (!BuildItemTreeSnapshot(RootItemId, OutEntries))
    {
        return false;
    }

    TMap<FPEPItemId, FGameplayTag> RemovedRoots;
    RemovedRoots.Reserve(OutEntries.Num());
    for (const FPEPInventoryEntry& Entry : OutEntries)
    {
        RemovedRoots.Add(Entry.Item.ItemId, ResolveRootSlotTag(Entry.Location));
    }

    /*
     * The snapshot lists a parent before its children, so it is consumed backwards: the deepest
     * item leaves first and no entry ever names a parent the store has already dropped. Each
     * removal repairs the indices it touches, so a subtree of any size rebuilds nothing.
     */
    for (int32 Index = OutEntries.Num() - 1; Index >= 0; --Index)
    {
        FPEPInventoryEntry RemovedEntry;
        CommitExtractItem(
            OutEntries[Index].Item.ItemId,
            RemovedEntry);
    }

    /* Events only once the store is settled, so no observer reads a half-removed tree. */
    for (const FPEPInventoryEntry& RemovedEntry : OutEntries)
    {
        EmitRemoved(RemovedEntry.Item, RemovedEntry.Location, RemovedRoots.Find(RemovedEntry.Item.ItemId));
    }

    return true;
}

// ---- Pickup transfer ------------------------------------------------------------------------

FPEPItemResult UPEPInventoryComponent::ResolvePickupTransfer(
    const UPEPInventoryComponent* TargetInventory,
    const FPEPItemId ItemId,
    const FPEPItemAccessContext& Access,
    FPEPItemLocation& OutDestination,
    TArray<FPEPInventoryEntry>* OutItemTree) const
{
    OutDestination = FPEPItemLocation();

    const FPEPItemInstance* Item = FindItem(ItemId);
    if (!TargetInventory || !Item)
    {
        return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
    }

    if (!TargetInventory->TryResolvePickupPlacement(
        *Item,
        OutDestination,
        TargetInventory->ResolveAccessRole(Access)))
    {
        return FPEPItemResult::Failure(PEPCoreTags::Reason_NoFreeSlot, ItemId);
    }

    return ValidateTransfer(TargetInventory, ItemId, OutDestination, Access, OutItemTree);
}

FPEPItemResult UPEPInventoryComponent::CanTransferPickupToInventory(
    const UPEPInventoryComponent* TargetInventory, FPEPItemId ItemId, const FPEPItemAccessContext& Access) const
{
    FPEPItemLocation Destination;
    return ResolvePickupTransfer(TargetInventory, ItemId, Access, Destination, nullptr);
}

FPEPItemResult UPEPInventoryComponent::TransferPickupToInventory(
    UPEPInventoryComponent* TargetInventory, FPEPItemId ItemId, const FPEPItemAccessContext& Access)
{
    if (!HasMutationAuthority())
    {
        return FPEPItemResult::Failure(PEPCoreTags::Reason_NotAuthority, ItemId);
    }

    if (!TargetInventory || !TargetInventory->HasMutationAuthority())
    {
        return FPEPItemResult::Failure(
            TargetInventory
                ? PEPCoreTags::Reason_NotAuthority
                : PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }

    /* Placement resolved and the transfer validated once; the commit reuses both. */
    FPEPItemLocation Destination;
    TArray<FPEPInventoryEntry> ItemTree;
    const FPEPItemResult Validation =
        ResolvePickupTransfer(TargetInventory, ItemId, Access, Destination, &ItemTree);
    if (!Validation.bSuccess)
    {
        return Validation;
    }

    if (TargetInventory == this)
    {
        return CommitValidatedMove(ItemId, Destination);
    }

    return CommitValidatedTransfer(TargetInventory, ItemId, Destination, MoveTemp(ItemTree));
}

bool UPEPInventoryComponent::RestoreWorldItemTree(FPEPItemInstanceTree Tree, FGameplayTag RootTag)
{
    if (!HasMutationAuthority() || !Tree.IsStructurallyValid()) return false;
    TArray<FPEPInventoryEntry> Entries = FlattenItemTree(MoveTemp(Tree));
    const FPEPItemLocation RootLocation = FPEPItemLocation::MakeNamedSlot(RootTag);
    /* The tree replaces the whole store, so it is validated against an empty one. */
    const TStrongObjectPtr<UPEPInventoryComponent> Scratch(CreateValidationScratch(false));
    // Descendant layout was already authored or validated by its source store. Locks must not
    // turn restoring/dropping a complete tree into an attempt to put items into its locked slots.
    if (!ValidateItemTreeAgainstScratch(*Scratch, Entries, RootLocation,
        EPEPAccessRole::Owner, false).bSuccess) return false;
    const TArray<FPEPInventoryEntry> PreviousEntries = InventoryView.Entries;
    ResetEntries();
    TArray<TPair<FPEPItemId, FPEPItemLocation>> AddedEntries;
    CommitAddItemTree(Entries, RootLocation, AddedEntries);
    for (const FPEPInventoryEntry& Previous : PreviousEntries)
    {
        EmitRemoved(Previous.Item, Previous.Location);
    }
    EmitAddedEntries(AddedEntries);
    return true;
}
