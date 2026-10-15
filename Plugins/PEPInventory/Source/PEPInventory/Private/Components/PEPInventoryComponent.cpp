#include "Components/PEPInventoryComponent.h"

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

namespace
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

void UPEPInventoryComponent::HandleReplicatedEntryAdded(
    const FPEPInventoryEntry& Entry)
{
    // The replicated array changed shape or content under us; nothing cached about it is trusted.
    bEntryIndexDirty = true;
    bContainerSlotIndexDirty = true;

    EmitAdded(Entry.Item.ItemId, Entry.Location, &Entry.Item);
}

void UPEPInventoryComponent::HandleReplicatedEntryChanged(
    const FPEPInventoryEntry& Entry,
    const FPEPItemLocation& PreviousLocation)
{
    // The replicated array changed shape or content under us; nothing cached about it is trusted.
    bEntryIndexDirty = true;
    bContainerSlotIndexDirty = true;

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
    // The replicated array changed shape or content under us; nothing cached about it is trusted.
    bEntryIndexDirty = true;
    bContainerSlotIndexDirty = true;

    EmitRemoved(Entry.Item, Entry.Location);
}

void UPEPInventoryComponent::QueueReplicatedEntryRemoved(
    const FPEPInventoryEntry& Entry)
{
    PendingReplicatedRemovals.Add(Entry);
    PendingRemovedRootSlotTags.Add(Entry.Item.ItemId, ResolveRootSlotTag(Entry.Location,
        LastReplicatedLocations.IsEmpty() ? nullptr : &LastReplicatedLocations));
}

void UPEPInventoryComponent::FlushReplicatedEntryRemovals()
{
    bEntryIndexDirty = true;
    bContainerSlotIndexDirty = true;
    TArray<FPEPInventoryEntry> Removed = MoveTemp(PendingReplicatedRemovals);
    PendingReplicatedRemovals.Reset();

    for (const FPEPInventoryEntry& Entry : Removed)
    {
        EmitRemoved(Entry.Item, Entry.Location, PendingRemovedRootSlotTags.Find(Entry.Item.ItemId));
    }
    PendingRemovedRootSlotTags.Reset();
    LastReplicatedLocations.Reset();
    for (const FPEPInventoryEntry& Entry : InventoryView.Entries)
    {
        LastReplicatedLocations.Add(Entry.Item.ItemId, Entry.Location);
    }
}





bool UPEPInventoryComponent::RegisterLocationValidator(
    const FGameplayTag RootLocationTag,
    FPEPItemLocationValidator InValidator,
    const bool bAllowAutoPlacement)
{
    if (!RootLocationTag.IsValid() || !InValidator.IsBound())
    {
        return false;
    }

    for (FPEPLocationValidatorBinding& Existing : LocationValidators)
    {
        if (Existing.RootLocationTag == RootLocationTag)
        {
            Existing.Validator = MoveTemp(InValidator);
            Existing.bAllowAutoPlacement = bAllowAutoPlacement;
            bAutoPlacementSlotTagsDirty = true;
            return true;
        }
    }

    FPEPLocationValidatorBinding& Binding =
        LocationValidators.AddDefaulted_GetRef();
    Binding.RootLocationTag = RootLocationTag;
    Binding.Validator = MoveTemp(InValidator);
    Binding.bAllowAutoPlacement = bAllowAutoPlacement;
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

    return MoveItem(
        ItemId,
        Destination,
        Access);
}

FGameplayTag UPEPInventoryComponent::ResolveRootSlotTag(
    const FPEPItemLocation& Location,
    const TMap<FPEPItemId, FPEPItemLocation>* Locations) const
{
    FPEPItemLocation Current = Location;
    TSet<FPEPItemId> Visited;
    while (Current.Type == EPEPItemLocationType::ItemContainer)
    {
        if (!Current.ParentItemId.IsValid() || Visited.Contains(Current.ParentItemId))
        {
            return FGameplayTag();
        }
        Visited.Add(Current.ParentItemId);
        if (Locations)
        {
            const FPEPItemLocation* ParentLocation = Locations->Find(Current.ParentItemId);
            if (!ParentLocation)
            {
                return FGameplayTag();
            }
            Current = *ParentLocation;
        }
        else
        {
            const FPEPInventoryEntry* Parent = FindEntry(Current.ParentItemId);
            if (!Parent)
            {
                return FGameplayTag();
            }
            Current = Parent->Location;
        }
    }
    return Current.Type == EPEPItemLocationType::NamedSlot ? Current.LocationTag : FGameplayTag();
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
    return FPEPItemAccessContext::From(GetOwningController());
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

    const FPEPItemState_ContainerRuntime* RuntimeState =
        ParentEntry->Item.GetState<
            FPEPItemState_ContainerRuntime>();

    bOutLocked = RuntimeState
        ? RuntimeState->bLocked
        : ContainerFragment->bStartsLocked;

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

    return FPEPItemResult::Success(
        Entry.Item.ItemId,
        PEPCoreTags::Reason_None);
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

FPEPItemResult UPEPInventoryComponent::CanAddItem(
    const FPEPItemInstance& Item,
    const FPEPItemLocation& Location,
    const FPEPItemAccessContext& Access) const
{
	if (!IsItemCatalogueReadyForInventoryMutation(*this))
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
    CommitAddItem(MoveTemp(StoredItem), Location);
    EmitAdded(ItemId, Location);

    return FPEPItemResult::Success(
        ItemId,
        PEPCoreTags::Reason_None);
}

void UPEPInventoryComponent::RefreshIndices() const
{
    const bool bRebuildEntries = bEntryIndexDirty;
    const bool bRebuildSlots = bContainerSlotIndexDirty;

    if (!bRebuildEntries && !bRebuildSlots)
    {
        return;
    }

    if (bRebuildEntries)
    {
        EntryIndex.Reset();
        EntryIndex.Reserve(InventoryView.Entries.Num());
    }

    if (bRebuildSlots)
    {
        ContainerSlotIndex.Reset();
        NamedSlotIndex.Reset();
    }

    for (int32 Index = 0; Index < InventoryView.Entries.Num(); ++Index)
    {
        const FPEPInventoryEntry& Entry = InventoryView.Entries[Index];

        if (bRebuildEntries)
        {
            EntryIndex.Add(Entry.Item.ItemId, Index);
        }

        if (!bRebuildSlots)
        {
            continue;
        }
        switch (Entry.Location.Type)
        {
        case EPEPItemLocationType::ItemContainer:
            ContainerSlotIndex.FindOrAdd(
                FPEPContainerSlotKey(
                    Entry.Location.ParentItemId,
                    Entry.Location.ContainerSlotName)).Add(Index);
            break;
        case EPEPItemLocationType::NamedSlot:
            NamedSlotIndex.FindOrAdd(Entry.Location.LocationTag).Add(Index);
            break;
        default:
            break;
        }
    }

    bEntryIndexDirty = false;
    bContainerSlotIndexDirty = false;

    if (bRebuildSlots)
    {
        RebuildGridOccupancy();
    }
}

void UPEPInventoryComponent::RebuildGridOccupancy() const
{
    GridOccupancy.Reset();
    for (const TPair<FPEPContainerSlotKey, TArray<int32>>& Pair : ContainerSlotIndex)
    {
        const int32* ParentIndex = EntryIndex.Find(Pair.Key.Key);
        const FPEPItemFragment_Container* Container = ParentIndex
            ? InventoryView.Entries[*ParentIndex].Item.GetDefinitionFragment<FPEPItemFragment_Container>()
            : nullptr;
        const FPEPContainerSlotDefinition* Slot = Container ? Container->FindSlot(Pair.Key.Value) : nullptr;
        if (!Slot || Slot->Mode != EPEPContainerSlotMode::Grid
            || Slot->GridSize.X <= 0 || Slot->GridSize.Y <= 0)
        {
            continue;
        }
        FPEPGridOccupancy& Occupancy = GridOccupancy.Add(Pair.Key);
        Occupancy.GridSize = Slot->GridSize;
        Occupancy.CellOccupants.Init(INDEX_NONE, Slot->GridSize.X * Slot->GridSize.Y);
        Occupancy.FreeCells = Occupancy.CellOccupants.Num();
        for (const int32 Index : Pair.Value)
        {
            const FPEPInventoryEntry& Entry = InventoryView.Entries[Index];
            FPEPItemInventoryShapeData Shape;
            if (!PEPInventoryQuery::GetInventoryShapeData(Entry.Item, Shape))
            {
                Occupancy.bUnknown = true;
                continue;
            }
            const FIntPoint Size = PEPInventoryValidation::OrientedSize(Shape.Size, Entry.Location.bRotated);
            const FIntPoint From = Entry.Location.GridPosition;
            /* Clipped: an entry hanging off the grid is a corrupt store, not a reason to crash. */
            for (int32 Y = FMath::Max(0, From.Y); Y < FMath::Min(Slot->GridSize.Y, From.Y + Size.Y); ++Y)
            {
                for (int32 X = FMath::Max(0, From.X); X < FMath::Min(Slot->GridSize.X, From.X + Size.X); ++X)
                {
                    int32& Cell = Occupancy.CellOccupants[Y * Slot->GridSize.X + X];
                    if (Cell == INDEX_NONE)
                    {
                        Cell = Index;
                        --Occupancy.FreeCells;
                    }
                }
            }
        }
    }
}

const UPEPInventoryComponent::FPEPGridOccupancy* UPEPInventoryComponent::FindGridOccupancy(
    const FPEPItemId ParentItemId,
    const FName SlotName) const
{
    RefreshIndices();
    return GridOccupancy.Find(FPEPContainerSlotKey(ParentItemId, SlotName));
}

TArray<FPEPInventoryEntry>& UPEPInventoryComponent::MutableEntries()
{
    /*
     * A change of shape moves entries, so it invalidates both: the id map because positions shift,
     * and the slot map because it holds those same positions.
     */
    bEntryIndexDirty = true;
    bContainerSlotIndexDirty = true;

    return InventoryView.Entries;
}

void UPEPInventoryComponent::SetEntryLocation(
    FPEPInventoryEntry& Entry,
    const FPEPItemLocation& Location)
{
    Entry.Location = Location;

    /* The entry has not moved in the array, so only the cache keyed by location is stale. */
    bContainerSlotIndexDirty = true;
}

const TArray<int32>* UPEPInventoryComponent::FindContainerSlotOccupants(
    const FPEPItemId ParentItemId,
    const FName SlotName) const
{
    RefreshIndices();

    return ContainerSlotIndex.Find(
        FPEPContainerSlotKey(ParentItemId, SlotName));
}

bool UPEPInventoryComponent::ContainsItem(
    const FPEPItemId ItemId) const
{
    return FindEntry(ItemId) != nullptr;
}

const FPEPInventoryEntry* UPEPInventoryComponent::FindEntry(
    const FPEPItemId ItemId) const
{
    RefreshIndices();

    const int32* Index = EntryIndex.Find(ItemId);
    return Index ? &InventoryView.Entries[*Index] : nullptr;
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
    const TArray<int32>* Candidates = Location.Type == EPEPItemLocationType::NamedSlot
        ? NamedSlotIndex.Find(Location.LocationTag)
        : ContainerSlotIndex.Find(
            FPEPContainerSlotKey(Location.ParentItemId, Location.ContainerSlotName));
    if (!Candidates)
    {
        return false;
    }
    for (const int32 Index : *Candidates)
    {
        const FPEPInventoryEntry& Pair = InventoryView.Entries[Index];
        if (Pair.Location == Location)
        {
            OutItemId = Pair.Item.ItemId;
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

    for (const FPEPInventoryEntry& Pair : InventoryView.Entries)
    {
        const FPEPItemLocation& Location = Pair.Location;
        if (Location.Type == EPEPItemLocationType::ItemContainer
            && Location.ParentItemId == ParentItemId)
        {
            OutChildItemIds.Add(Pair.Item.ItemId);
        }
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

	TSet<FPEPItemId> Visited;
	while (Entry->Location.Type == EPEPItemLocationType::ItemContainer)
	{
		const FPEPItemId ParentItemId = Entry->Location.ParentItemId;
		if (!ParentItemId.IsValid() || Visited.Contains(ParentItemId))
		{
			return false;
		}
		Visited.Add(ParentItemId);

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

    for (const FPEPInventoryEntry& Pair : InventoryView.Entries)
    {
        const FPEPItemLocation& Location = Pair.Location;
        if (Location.Type == EPEPItemLocationType::ItemContainer
            && Location.ParentItemId == ParentItemId)
        {
            OutChildren.Add(&Pair);
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

    for (const FPEPInventoryEntry& Pair : InventoryView.Entries)
    {
        const FPEPItemLocation& Location = Pair.Location;
        if (Location.Type != EPEPItemLocationType::ItemContainer
            || Location.ParentItemId != ParentItemId)
        {
            continue;
        }

        const FPEPContainerSlotDefinition* Slot =
            ContainerFragment->FindSlot(Location.ContainerSlotName);

        if (Slot && Slot->Mode == EPEPContainerSlotMode::Single)
        {
            OutChildren.Add(&Pair);
        }
    }
}

const FPEPInventoryEntry*
UPEPInventoryComponent::FindItemInContainerSlot(
    const FPEPItemId ParentItemId,
    const FName SlotName) const
{
    for (const FPEPInventoryEntry& Pair : InventoryView.Entries)
    {
        const FPEPItemLocation& Location = Pair.Location;
        if (Location.Type == EPEPItemLocationType::ItemContainer
            && Location.ParentItemId == ParentItemId
            && Location.ContainerSlotName == SlotName)
        {
            return &Pair;
        }
    }

    return nullptr;
}

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
        ? MoveItem(ItemId, Destination, GetOwnerRequestContext())
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
		GetOwnerRequestContext());
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
		GetOwnerRequestContext());
	ResolveInventoryRequest(RequestId, EPEPInventoryRequestKind::MergeStacks, Result);
}

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
	return FPEPItemResult::Success(Entry.Item.ItemId, PEPCoreTags::Reason_None);
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

    /* Read before the commit overwrites it; CanMoveItem has already established the entry exists. */
    const FPEPItemLocation PreviousLocation = FindEntry(ItemId)->Location;
    const FGameplayTag PreviousRoot = ResolveRootSlotTag(PreviousLocation);

    CommitMoveItem(ItemId, Destination);
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

    FPEPInventoryEntry* Entry =
        FindMutableEntry(ItemId);

    if (!Entry || !Mutation)
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }

    FPEPItemInstance WorkingCopy =
        Entry->Item;

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

    CommitExtractItem(ItemId, OutEntry);
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
    CommitExtractItem(ItemId, DiscardedEntry);
    EmitRemoved(DiscardedEntry.Item, DiscardedEntry.Location);

    return FPEPItemResult::Success(
        ItemId,
        PEPCoreTags::Reason_None);
}






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

	OutSourceItem = SourceEntry->Item;
	const FPEPItemResult SplitResult = PEPItemMutation::SplitStack(
		OutSourceItem,
		SplitCount,
		OutSplitItem);
	if (!SplitResult.bSuccess)
	{
		return SplitResult;
	}

	return ValidateLocationForItem(
		OutSplitItem,
		Destination,
		Role,
		bInternalContainerMove);
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

	FPEPItemInstance SourceCopy;
	FPEPItemInstance SplitItem;
	const FPEPItemResult Validation = BuildSplitStackPlan(
		SourceItemId,
		SplitCount,
		Destination,
		Access,
		SourceCopy,
		SplitItem);
	if (!Validation.bSuccess)
	{
		return Validation;
	}

	FPEPInventoryEntry* SourceEntry = FindMutableEntry(SourceItemId);
	if (!SourceEntry)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, SourceItemId);
	}
	const FPEPItemLocation SourceLocation = SourceEntry->Location;
	SourceEntry->Item = MoveTemp(SourceCopy);
	InventoryView.MarkItemDirty(*SourceEntry);
	const FPEPItemId SplitItemId = SplitItem.ItemId;
	CommitAddItem(MoveTemp(SplitItem), Destination);

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

	const FPEPInventoryEntry* SourceBefore = FindEntry(SourceItemId);
	const FPEPInventoryEntry* TargetBefore = FindEntry(TargetItemId);
	if (!SourceBefore || !TargetBefore)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, SourceItemId);
	}
	const FPEPItemLocation SourceLocation = SourceBefore->Location;
	const FPEPItemLocation TargetLocation = TargetBefore->Location;
	const bool bRemoveSource = SourceCopy.IsDepleted();
	const FPEPItemInstance RemovedSourceSnapshot = SourceBefore->Item;

	if (bRemoveSource)
	{
		FPEPInventoryEntry RemovedEntry;
		CommitExtractItem(SourceItemId, RemovedEntry);
	}
	else
	{
		FPEPInventoryEntry* MutableSource = FindMutableEntry(SourceItemId);
		MutableSource->Item = MoveTemp(SourceCopy);
		InventoryView.MarkItemDirty(*MutableSource);
	}

	FPEPInventoryEntry* MutableTarget = FindMutableEntry(TargetItemId);
	check(MutableTarget);
	MutableTarget->Item = MoveTemp(TargetCopy);
	InventoryView.MarkItemDirty(*MutableTarget);

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
	 * neither sits where it did, and those old cells are the only thing the two could collide on
	 * — they were disjoint before, so they stay disjoint after. No simulated copy of the store is
	 * needed for that, which matters: this runs on every drag hover over an equipment slot.
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
	return ValidateLocationForItem(
		SecondEntry->Item,
		FirstEntry->Location,
		Role,
		bSecondInternal,
		FirstItemId);
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

	const FPEPItemLocation FirstLocation = FindEntry(FirstItemId)->Location;
	const FPEPItemLocation SecondLocation = FindEntry(SecondItemId)->Location;
	const FGameplayTag FirstRoot = ResolveRootSlotTag(FirstLocation);
	const FGameplayTag SecondRoot = ResolveRootSlotTag(SecondLocation);
	CommitMoveItem(FirstItemId, SecondLocation);
	CommitMoveItem(SecondItemId, FirstLocation);
	EmitMoved(FirstItemId, FirstLocation, SecondLocation, nullptr, &FirstRoot);
	EmitMoved(SecondItemId, SecondLocation, FirstLocation, nullptr, &SecondRoot);
	return FPEPItemResult::Success(FirstItemId, PEPCoreTags::Reason_None);
}














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

UPEPItemDefinition* UPEPInventoryComponent::GetItemDefinitionById(const FPEPItemId ItemId) const
{
    const FPEPInventoryEntry* Entry = ItemId.IsValid() ? FindEntry(ItemId) : nullptr;
    return Entry ? const_cast<UPEPItemDefinition*>(Entry->Item.GetDefinitionIfLoaded()) : nullptr;
}


void UPEPInventoryComponent::GetEntriesInContainerSlot(
    const FPEPItemId ParentItemId,
    const FName SlotName,
    TArray<FPEPInventoryEntry>& OutEntries) const
{
    OutEntries.Reset();

    const TArray<int32>* Occupants = FindContainerSlotOccupants(ParentItemId, SlotName);
    if (!Occupants)
    {
        return;
    }

    OutEntries.Reserve(Occupants->Num());
    for (const int32 Index : *Occupants)
    {
        OutEntries.Add(InventoryView.Entries[Index]);
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

    AActor* Owner = GetOwner();
    if (Owner && Owner->HasAuthority() && Owner->GetNetMode() != NM_Standalone)
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

FPEPItemResult
UPEPInventoryComponent::CanTransferItemToInventory(
    const UPEPInventoryComponent* TargetInventory,
    const FPEPItemId ItemId,
    const FPEPItemLocation& Destination,
    const FPEPItemAccessContext& Access) const
{
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

    TArray<FPEPInventoryEntry> ItemTree;
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

    const FPEPItemResult Validation =
        CanTransferItemToInventory(
            TargetInventory,
            ItemId,
            Destination,
            Access);

    if (!Validation.bSuccess)
    {
        return Validation;
    }

    if (TargetInventory == this)
    {
        const FPEPItemLocation PreviousLocation = FindEntry(ItemId)->Location;
        const FGameplayTag PreviousRoot = ResolveRootSlotTag(PreviousLocation);

        CommitMoveItem(ItemId, Destination);
        EmitMoved(ItemId, PreviousLocation, Destination, nullptr, &PreviousRoot);

        return FPEPItemResult::Success(
            ItemId,
            PEPCoreTags::Reason_None);
    }

    TArray<FPEPInventoryEntry> ItemTree;
    if (!BuildItemTreeSnapshot(ItemId, ItemTree))
    {
        return FPEPItemResult::Failure(
            PEPCoreTags::Reason_InvalidTarget,
            ItemId);
    }

    TMap<FPEPItemId, FGameplayTag> RemovedRoots;
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
        TargetInventory->CommitAddItem(MoveTemp(Entry.Item), AddedLocation);
        AddedEntries.Emplace(AddedItemId, AddedLocation);
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
		if (Location.ParentItemId == Item.ItemId)
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
			if (Location.GridPosition !=
					FIntPoint::ZeroValue
				|| Location.bRotated)
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

		if (Location.bRotated
			&& (!ContainerSlot->bAllowRotation
				|| !ShapeData.bCanRotate))
		{
			return FPEPItemResult::Failure(
				PEPCoreTags::Reason_BlockedByState,
				Item.ItemId);
		}

		const FIntPoint ItemSize =
			PEPInventoryValidation::OrientedSize(ShapeData.Size, Location.bRotated);

		if (!PEPInventoryValidation::IsFootprintInsideGrid(
			Location.GridPosition,
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
			for (int32 Y = Location.GridPosition.Y; Y < Location.GridPosition.Y + ItemSize.Y; ++Y)
			{
				for (int32 X = Location.GridPosition.X; X < Location.GridPosition.X + ItemSize.X; ++X)
				{
					const int32 OccupantIndex = Occupancy->OccupantAt(FIntPoint(X, Y));
					if (OccupantIndex == INDEX_NONE)
					{
						continue;
					}
					const FPEPItemId OccupantId = InventoryView.Entries[OccupantIndex].Item.ItemId;
					if (OccupantId == Item.ItemId || OccupantId == AlsoIgnoredItemId)
					{
						continue;
					}
					return FPEPItemResult::Failure(
						PEPCoreTags::Reason_NoFreeSlot,
						Item.ItemId);
				}
			}
			return FPEPItemResult::Success(
				Item.ItemId,
				PEPCoreTags::Reason_None);
		}

		const TArray<int32>* Occupants =
			FindContainerSlotOccupants(
				Location.ParentItemId,
				Location.ContainerSlotName);

		// Reference, not a ternary yielding a prvalue: that would copy the array on every call.
		static const TArray<int32> EmptySlot;
		const TArray<int32>& SlotOccupants = Occupants ? *Occupants : EmptySlot;

		for (const int32 OccupantIndex : SlotOccupants)
		{
			const FPEPInventoryEntry& Existing =
				InventoryView.Entries[OccupantIndex];

			if (Existing.Item.ItemId == Item.ItemId
				|| Existing.Item.ItemId == AlsoIgnoredItemId)
			{
				continue;
			}

			FPEPItemInventoryShapeData
				ExistingShapeData;

			if (!PEPInventoryQuery::GetInventoryShapeData(
				Existing.Item,
				ExistingShapeData))
			{
				return FPEPItemResult::Failure(
					PEPCoreTags::Reason_BlockedByState,
					Item.ItemId);
			}

			const FIntPoint ExistingSize = PEPInventoryValidation::OrientedSize(
				ExistingShapeData.Size,
				Existing.Location.bRotated);

			if (PEPInventoryValidation::DoFootprintsOverlap(
				Location.GridPosition,
				ItemSize,
				Existing.Location.GridPosition,
				ExistingSize))
			{
				return FPEPItemResult::Failure(
					PEPCoreTags::Reason_NoFreeSlot,
					Item.ItemId);
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
    int32 BestSpecificity = INDEX_NONE;

    for (const FPEPLocationValidatorBinding& Binding : LocationValidators)
    {
        if (!Binding.Validator.IsBound()
            || !Location.LocationTag.MatchesTag(Binding.RootLocationTag))
        {
            continue;
        }

        const int32 Specificity =
            Binding.RootLocationTag.ToString().Len();

        if (!BestBinding || Specificity > BestSpecificity)
        {
            BestBinding = &Binding;
            BestSpecificity = Specificity;
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

namespace
{
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
}

void UPEPInventoryComponent::AppendOrderedNamedSlotItems(
	TArray<FPEPItemId>& OutItemIds) const
{
	TArray<const FPEPInventoryEntry*> NamedSlotEntries;

	for (const FPEPInventoryEntry& Entry : InventoryView.Entries)
	{
		if (Entry.Location.Type == EPEPItemLocationType::NamedSlot)
		{
			NamedSlotEntries.Add(&Entry);
		}
	}

	NamedSlotEntries.Sort(
		[](const FPEPInventoryEntry& First, const FPEPInventoryEntry& Second)
		{
			return First.Location.LocationTag.GetTagName().LexicalLess(
				Second.Location.LocationTag.GetTagName());
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

	/* Children of one parent occupy distinct locations, so this is a total order. */
	Children.Sort(
		[](const FPEPInventoryEntry& First, const FPEPInventoryEntry& Second)
		{
			if (First.Location.ContainerSlotName != Second.Location.ContainerSlotName)
			{
				return First.Location.ContainerSlotName.LexicalLess(
					Second.Location.ContainerSlotName);
			}

			if (First.Location.GridPosition.Y != Second.Location.GridPosition.Y)
			{
				return First.Location.GridPosition.Y < Second.Location.GridPosition.Y;
			}

			return First.Location.GridPosition.X < Second.Location.GridPosition.X;
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

		if (AreLocationsInSameSlot(Candidate, ExcludedLocation))
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
	 */
	TArray<FPEPItemId> PendingParents;
	AppendOrderedNamedSlotItems(PendingParents);

	TSet<FPEPItemId> VisitedParents;

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

		const FPEPItemState_ContainerRuntime* Runtime =
			ParentEntry->Item.GetState<
				FPEPItemState_ContainerRuntime>();

		const bool bLocked =
			Runtime
				? Runtime->bLocked
				: Container->bStartsLocked;

		/* A locked container keeps its contents shut in, so nothing nested in it is reachable. */
		if (bLocked)
		{
			continue;
		}

		for (const FPEPContainerSlotDefinition& Slot
			: Container->Slots)
		{
			/* Never automatically into magazine_socket, optic_socket and the like. */
			if (!Slot.IsValid()
				|| Slot.Mode !=
					EPEPContainerSlotMode::Grid
				|| !Slot.GetPolicyFor(Role).bCanPut)
			{
				continue;
			}

			/* The whole slot is excluded, so none of its cells will do either. */
			if (AreLocationsInSameSlot(
				FPEPItemLocation::MakeItemContainer(ParentItemId, Slot.SlotName),
				ExcludedLocation))
			{
				continue;
			}

			/*
			 * Two prunings, both necessary conditions of what ValidateLocationForItem decides: a
			 * grid with fewer free cells than the footprint cannot take it, and a footprint cannot
			 * start on a cell someone else holds. Neither applies while the map is unknown.
			 */
			const FPEPGridOccupancy* Occupancy = FindGridOccupancy(ParentItemId, Slot.SlotName);
			const bool bUseOccupancy = Occupancy && !Occupancy->bUnknown;
			FPEPItemInventoryShapeData Shape;
			if (bUseOccupancy
				&& (!PEPInventoryQuery::GetInventoryShapeData(Item, Shape)
					|| Occupancy->FreeCells < Shape.Size.X * Shape.Size.Y))
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

				for (int32 Y = 0;
					Y < Slot.GridSize.Y;
					++Y)
				{
					for (int32 X = 0;
						X < Slot.GridSize.X;
						++X)
					{
						if (bUseOccupancy)
						{
							const int32 OccupantIndex = Occupancy->OccupantAt(FIntPoint(X, Y));
							if (OccupantIndex != INDEX_NONE
								&& InventoryView.Entries[OccupantIndex].Item.ItemId != Item.ItemId)
							{
								continue;
							}
						}

						const FPEPItemLocation Candidate =
							FPEPItemLocation::
							MakeItemContainer(
								ParentItemId,
								Slot.SlotName,
								FIntPoint(X, Y),
								bRotated);

						if (ValidateLocationForItem(
							Item,
							Candidate,
							Role).bSuccess)
						{
							OutLocation = Candidate;
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
				ParentItemId, Slot.SlotName, FIntPoint::ZeroValue, false);
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

void UPEPInventoryComponent::PlanPickupStackMerges(
	FPEPItemInstance& Item,
	const EPEPAccessRole Role,
	TArray<FPEPInventoryStackMerge>& OutMerges) const
{
	OutMerges.Reset();

	for (const FPEPInventoryEntry& Entry : InventoryView.Entries)
	{
		if (!PEPItemRules::AreStacksCompatible(Entry.Item, Item))
		{
			continue;
		}

		/*
		 * Merging bypasses placement entirely, so without this a sealed slot would still accept
		 * ammunition as long as a compatible stack already sat inside it.
		 */
		if (!CanDepositIntoLocation(Entry.Location, Role))
		{
			continue;
		}

		int32 TransferredCount = 0;
		FPEPItemInstance MergedItem = Entry.Item;

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
		Planned.TargetItemId = Entry.Item.ItemId;
		Planned.MergedItem = MoveTemp(MergedItem);
		Planned.Location = Entry.Location;

		if (Item.IsDepleted())
		{
			break;
		}
	}
}

FPEPItemResult
UPEPInventoryComponent::CanAddPickupItem(
	const FPEPItemInstance& Item,
	const FPEPItemAccessContext& Access) const
{
	if (!Item.IsValid())
	{
		return FPEPItemResult::Failure(
			PEPCoreTags::Reason_InvalidTarget,
			Item.ItemId);
	}

	const EPEPAccessRole Role = ResolveAccessRole(Access);

	// Mount the complete pickup before considering any storage, including stack merging.
	FPEPItemLocation AttachmentLocation;
	if (TryResolveAttachmentPlacement(Item, AttachmentLocation, Role))
	{
		return CanAddItem(Item, AttachmentLocation, Access);
	}

	/*
	 * The merges are planned here for the same reason AddPickupItem plans them: a pickup the
	 * carried stacks would swallow whole needs no cell, and asking for one would refuse a pickup
	 * that in fact succeeds. The copy is what keeps this const — nothing carries over to the add.
	 */
	FPEPItemInstance Remainder = Item;
	TArray<FPEPInventoryStackMerge> PlannedMerges;
	PlanPickupStackMerges(Remainder, Role, PlannedMerges);

	if (Remainder.IsDepleted())
	{
		return FPEPItemResult::Success(
			Item.ItemId,
			PEPCoreTags::Reason_None);
	}

	FPEPItemLocation Destination;

	if (!TryResolveAutoPlacement(
		Remainder,
		Destination,
		Role))
	{
		return FPEPItemResult::Failure(
			PEPCoreTags::Reason_NoFreeSlot,
			Item.ItemId);
	}

	return CanAddItem(
		Remainder,
		Destination,
		Access);
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

	if (!Item.IsValid())
	{
		return FPEPItemResult::Failure(
			PEPCoreTags::Reason_InvalidTarget,
			Item.ItemId);
	}

	const EPEPAccessRole Role = ResolveAccessRole(Access);
	const FPEPItemId PickedUpItemId = Item.ItemId;

	FPEPItemLocation AttachmentLocation;
	if (TryResolveAttachmentPlacement(Item, AttachmentLocation, Role))
	{
		return AddItem(MoveTemp(Item), AttachmentLocation, Access);
	}

	/*
	 * Top up the stacks already carried before looking for a place: picking up cartridges must
	 * fill the ones on the rig rather than claim a new cell.
	 *
	 * Planned first and written afterwards, so a remainder that turns out to have nowhere to go
	 * leaves the carried stacks exactly as they were.
	 */
	TArray<FPEPInventoryStackMerge> PlannedMerges;
	PlanPickupStackMerges(Item, Role, PlannedMerges);

	/* Absorbed whole: the picked-up item never enters the store. */
	const bool bAddRemainder = !Item.IsDepleted();

	FPEPItemLocation Destination;

	if (bAddRemainder
		&& !TryResolveAutoPlacement(Item, Destination, Role))
	{
		return FPEPItemResult::Failure(
			PEPCoreTags::Reason_NoFreeSlot,
			PickedUpItemId);
	}

	for (FPEPInventoryStackMerge& Planned : PlannedMerges)
	{
		FPEPInventoryEntry* Target = FindMutableEntry(Planned.TargetItemId);
		check(Target);

		Target->Item = MoveTemp(Planned.MergedItem);
		InventoryView.MarkItemDirty(*Target);
	}

	if (bAddRemainder)
	{
		CommitAddItem(MoveTemp(Item), Destination);
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

    CommitAddItem(
        MoveTemp(ItemTree[0].Item),
        RootLocation);
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

            CommitAddItem(
                MoveTemp(PendingEntry.Item),
                ChildLocation);

            AddedIds.Add(ChildItemId);
            OutAdded.Emplace(
                ChildItemId,
                ChildLocation);

            PendingIndices.RemoveAtSwap(PendingIndex);
            bMadeProgress = true;
        }

        checkf(
            bMadeProgress,
            TEXT("Validated item tree could not be inserted parent-first."));

        if (!bMadeProgress)
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
    const TSubclassOf<APEPItemWorldActor> ActorClass = OverrideActorClass
        ? OverrideActorClass
        : PEPInventorySettings().ResolveWorldItemActorClassForDefinition(
            FindItem(ItemId) ? FindItem(ItemId)->GetDefinitionIfLoaded() : nullptr);

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
    const TSubclassOf<APEPItemWorldActor> ActorClass = OverrideActorClass
        ? OverrideActorClass
        : PEPInventorySettings().ResolveWorldItemActorClassForDefinition(
            FindItem(ItemId) ? FindItem(ItemId)->GetDefinitionIfLoaded() : nullptr);

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

    TArray<FPEPInventoryEntry> Extracted;
    const FPEPItemResult Removed = ExtractItemTree(ItemId, Extracted, Access);

    if (!Removed.bSuccess)
    {
        /* Validated a moment ago, so this is a bug rather than a refusal — lose the carrier, not the items. */
        Carrier->Destroy();
        return Removed;
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

namespace
{
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

FPEPItemResult UPEPInventoryComponent::CanAddItemTreeAt(
    const FPEPItemInstanceTree& Tree,
    const FPEPItemLocation& RootLocation,
    const FPEPItemAccessContext& Access) const
{
    FPEPItemInstanceTree TreeCopy = Tree;

    return CanAddItemTrees(
        MakeSingleTreePlacement(MoveTemp(TreeCopy), RootLocation),
        Access);
}

FPEPItemResult UPEPInventoryComponent::AddItemTreeAt(
    FPEPItemInstanceTree Tree,
    const FPEPItemLocation& RootLocation,
    const FPEPItemAccessContext& Access)
{
    return AddItemTrees(
        MakeSingleTreePlacement(MoveTemp(Tree), RootLocation),
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
        SingleTreeItemId(Placements),
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
            SingleTreeItemId(Placements));
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

    /* A root is whatever is not inside another item; store order is kept so a save is stable. */
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
    TSet<FPEPItemId> Visited;

    while (ParentItemId.IsValid())
    {
        if (ParentItemId == MovingItemId || Visited.Contains(ParentItemId))
        {
            return true;
        }

        Visited.Add(ParentItemId);

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
    for (const FPEPInventoryEntry& Pair : InventoryView.Entries)
    {
        const FPEPItemLocation& Location = Pair.Location;
        if (Location.Type == EPEPItemLocationType::ItemContainer
            && Location.ParentItemId == ParentItemId)
        {
            return true;
        }
    }

    return false;
}

void UPEPInventoryComponent::AppendDescendantEntries(
    const FPEPItemId ParentItemId,
    TArray<const FPEPInventoryEntry*>& OutEntries) const
{
    TArray<const FPEPInventoryEntry*> Children;
    GetDirectChildEntries(
        ParentItemId,
        Children);

    for (const FPEPInventoryEntry* Child : Children)
    {
        if (!Child)
        {
            continue;
        }

        OutEntries.Add(Child);
        AppendDescendantEntries(
            Child->Item.ItemId,
            OutEntries);
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

UPEPInventoryComponent* UPEPInventoryComponent::CreateValidationScratch() const
{
    UPEPInventoryComponent* Scratch =
        NewObject<UPEPInventoryComponent>(GetTransientPackage());

    Scratch->InventoryView.Entries = InventoryView.Entries;
    Scratch->LocationValidators = LocationValidators;
    Scratch->bEntryIndexDirty = true;
    Scratch->bContainerSlotIndexDirty = true;

    return Scratch;
}

FPEPItemResult UPEPInventoryComponent::ValidateItemTreeAtRootLocation(
    const TArray<FPEPInventoryEntry>& ItemTree,
    const FPEPItemLocation& RootLocation,
    const FPEPItemAccessContext& Access,
    const bool bValidateChildPlacement) const
{
    const TStrongObjectPtr<UPEPInventoryComponent> Scratch(CreateValidationScratch());

    return ValidateItemTreeAgainstScratch(
        *Scratch,
        ItemTree,
        RootLocation,
        ResolveAccessRole(Access),
        bValidateChildPlacement);
}

FPEPItemResult UPEPInventoryComponent::ValidateItemTreeAgainstScratch(
    UPEPInventoryComponent& Scratch,
    const TArray<FPEPInventoryEntry>& ItemTree,
    const FPEPItemLocation& RootLocation,
    const EPEPAccessRole Role,
    const bool bValidateChildPlacement) const
{
	if (!IsItemCatalogueReadyForInventoryMutation(*this))
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

        if (!Entry.Item.IsValid()
            || !EntryItemId.IsValid()
            || TreeEntriesById.Contains(EntryItemId)
            || Scratch.ContainsItem(EntryItemId))
        {
            return FPEPItemResult::Failure(
                PEPCoreTags::Reason_InvalidTarget,
                EntryItemId);
        }

		FPEPItemInstanceValidationResult InstanceValidation;
		if (!Entry.Item.ValidateInstance(InstanceValidation))
		{
			return FPEPItemResult::Failure(
				PEPCoreTags::Reason_InvalidTarget,
				EntryItemId);
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

        TSet<FPEPItemId> VisitedParents;
        FPEPItemId Cursor = Entry.Location.ParentItemId;
        bool bReachedRoot = false;

        while (Cursor.IsValid())
        {
            if (Cursor == RootItemId)
            {
                bReachedRoot = true;
                break;
            }

            if (VisitedParents.Contains(Cursor))
            {
                return FPEPItemResult::Failure(
                    PEPCoreTags::Reason_BlockedByState,
                    Entry.Item.ItemId);
            }

            VisitedParents.Add(Cursor);

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
    Scratch.CommitAddItem(MoveTemp(RootCopy), RootLocation);

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
            Scratch.CommitAddItem(MoveTemp(ChildCopy), PendingEntry.Location);
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
    const TArray<int32>* Occupants = NamedSlotIndex.Find(SlotTag);
    if (!Occupants)
    {
        return false;
    }
    for (const int32 Index : *Occupants)
    {
        const FPEPItemId OccupantId = InventoryView.Entries[Index].Item.ItemId;
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
    const TArray<int32>* Occupants =
        FindContainerSlotOccupants(ParentItemId, SlotName);

    if (!Occupants)
    {
        return false;
    }

    for (const int32 Index : *Occupants)
    {
        const FPEPItemId OccupantId = InventoryView.Entries[Index].Item.ItemId;
        if (OccupantId != IgnoredItemId && OccupantId != AlsoIgnoredItemId)
        {
            return true;
        }
    }

    return false;
}

void UPEPInventoryComponent::CommitAddItem(
    FPEPItemInstance&& Item,
    const FPEPItemLocation& Location)
{
    const FPEPItemId ItemId = Item.ItemId;

    check(Item.IsValid());
    check(ItemId.IsValid());
    check(!ContainsItem(ItemId));
    check(Location.IsStructurallyValid());

    FPEPInventoryEntry Entry;
    Entry.Item = MoveTemp(Item);
    Entry.Location = Location;

    FPEPInventoryEntry& Added = MutableEntries().Add_GetRef(MoveTemp(Entry));
    InventoryView.MarkItemDirty(Added);
}

void UPEPInventoryComponent::CommitMoveItem(
    const FPEPItemId ItemId,
    const FPEPItemLocation& Destination)
{
    FPEPInventoryEntry* Entry = FindMutableEntry(ItemId);
    check(Entry);
    check(Destination.IsStructurallyValid());

    SetEntryLocation(*Entry, Destination);

    InventoryView.MarkItemDirty(*Entry);
}

void UPEPInventoryComponent::CommitExtractItem(
    const FPEPItemId ItemId,
    FPEPInventoryEntry& OutEntry)
{
    const FPEPInventoryEntry* Entry = FindEntry(ItemId);
    check(Entry);

    OutEntry = *Entry;

    // RemoveAtSwap is safe: the fast array keys entries by replication id, not by position.
    MutableEntries().RemoveAtSwap(EntryIndex.FindChecked(ItemId));
    InventoryView.MarkArrayDirty();
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
    for (const FPEPInventoryEntry& Entry : OutEntries)
    {
        RemovedRoots.Add(Entry.Item.ItemId, ResolveRootSlotTag(Entry.Location));
    }

    /*
     * The snapshot lists a parent before its children, so it is consumed backwards: the deepest
     * item leaves first and no entry ever names a parent the store has already dropped.
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


FPEPItemResult UPEPInventoryComponent::CanTransferPickupToInventory(
    const UPEPInventoryComponent* TargetInventory, FPEPItemId ItemId, const FPEPItemAccessContext& Access) const
{
    const FPEPItemInstance* Item = FindItem(ItemId);
    FPEPItemLocation Destination;
    if (!TargetInventory || !Item) return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
    if (!TargetInventory->TryResolvePickupPlacement(*Item, Destination, TargetInventory->ResolveAccessRole(Access)))
        return FPEPItemResult::Failure(PEPCoreTags::Reason_NoFreeSlot, ItemId);
    return CanTransferItemToInventory(TargetInventory, ItemId, Destination, Access);
}
FPEPItemResult UPEPInventoryComponent::TransferPickupToInventory(
    UPEPInventoryComponent* TargetInventory, FPEPItemId ItemId, const FPEPItemAccessContext& Access)
{
    const FPEPItemResult Validation = CanTransferPickupToInventory(TargetInventory, ItemId, Access);
    if (!Validation.bSuccess) return Validation;
    FPEPItemLocation Destination;
    if (!TargetInventory->TryResolvePickupPlacement(*FindItem(ItemId), Destination, TargetInventory->ResolveAccessRole(Access)))
        return FPEPItemResult::Failure(PEPCoreTags::Reason_NoFreeSlot, ItemId);
    return TransferItemToInventory(TargetInventory, ItemId, Destination, Access);
}

bool UPEPInventoryComponent::RestoreWorldItemTree(FPEPItemInstanceTree Tree, FGameplayTag RootTag)
{
    if (!HasMutationAuthority() || !Tree.IsStructurallyValid()) return false;
    TArray<FPEPInventoryEntry> Entries = FlattenItemTree(MoveTemp(Tree));
    const FPEPItemLocation RootLocation = FPEPItemLocation::MakeNamedSlot(RootTag);
    const TStrongObjectPtr<UPEPInventoryComponent> Scratch(CreateValidationScratch());
    Scratch->MutableEntries().Reset();
    // Descendant layout was already authored or validated by its source store. Locks must not
    // turn restoring/dropping a complete tree into an attempt to put items into its locked slots.
    if (!ValidateItemTreeAgainstScratch(*Scratch, Entries, RootLocation,
        EPEPAccessRole::Owner, false).bSuccess) return false;
    const TArray<FPEPInventoryEntry> PreviousEntries = InventoryView.Entries;
    MutableEntries().Reset();
    InventoryView.MarkArrayDirty();
    TArray<TPair<FPEPItemId, FPEPItemLocation>> AddedEntries;
    CommitAddItemTree(Entries, RootLocation, AddedEntries);
    for (const FPEPInventoryEntry& Previous : PreviousEntries)
    {
        EmitRemoved(Previous.Item, Previous.Location);
    }
    EmitAddedEntries(AddedEntries);
    return true;
}
