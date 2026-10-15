#include "Components/PEPInventorySessionComponent.h"

#include "Components/PEPInventoryComponent.h"
#include "Core/PEPCoreTags.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Item/PEPItemContainerQuery.h"
#include "Item/PEPItemContainerRules.h"
#include "Item/PEPItemDefinition.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UPEPInventorySessionComponent::UPEPInventorySessionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	SessionView.SessionOwner = this;
	SessionView.bIsSessionView = true;
}

void UPEPInventorySessionComponent::PostInitProperties()
{
	Super::PostInitProperties();
	// Native struct copies from the CDO/template may overwrite the constructor's local pointers.
	SessionView.SessionOwner = this;
	SessionView.bIsSessionView = true;
}

void UPEPInventorySessionComponent::OnRegister()
{
	// Rebind after loading/duplicating Blueprint components, before any replication callbacks.
	SessionView.SessionOwner = this;
	SessionView.bIsSessionView = true;
	Super::OnRegister();
}

void UPEPInventorySessionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bHasTransferSession && HasAuthority())
	{
		CloseTransferSession();
	}
	Super::EndPlay(EndPlayReason);
}

void UPEPInventorySessionComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	/*
	 * A controller already reaches its owner alone; the condition says so in the one place a
	 * project might put this component on something that does not.
	 */
	DOREPLIFETIME_CONDITION(ThisClass, SessionView, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ThisClass, bHasTransferSession, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ThisClass, TransferSessionId, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ThisClass, SessionExternalAccess, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ThisClass, SessionLootPanelTag, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ThisClass, SessionMode, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ThisClass, SessionRootItemId, COND_OwnerOnly);
}

UPEPInventorySessionComponent* UPEPInventorySessionComponent::FindSessionComponent(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	if (UPEPInventorySessionComponent* Own = Actor->FindComponentByClass<UPEPInventorySessionComponent>())
	{
		return Own;
	}
	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		if (const AController* Controller = Pawn->GetController())
		{
			return Controller->FindComponentByClass<UPEPInventorySessionComponent>();
		}
	}
	return nullptr;
}

void UPEPInventorySessionComponent::SetLocalInventory(UPEPInventoryComponent* InInventory)
{
	ExplicitLocalInventory = InInventory;
}

UPEPInventoryComponent* UPEPInventorySessionComponent::GetLocalInventory() const
{
	if (UPEPInventoryComponent* Explicit = ExplicitLocalInventory.Get())
	{
		return Explicit;
	}
	AActor* Owner = GetOwner();
	if (const AController* Controller = Cast<AController>(Owner))
	{
		if (const APawn* Pawn = Controller->GetPawn())
		{
			if (UPEPInventoryComponent* Inventory = Pawn->FindComponentByClass<UPEPInventoryComponent>())
			{
				return Inventory;
			}
		}
		if (UPEPInventoryComponent* Inventory = Controller->FindComponentByClass<UPEPInventoryComponent>())
		{
			return Inventory;
		}
		return Controller->PlayerState
			? Controller->PlayerState->FindComponentByClass<UPEPInventoryComponent>()
			: nullptr;
	}
	return Owner ? Owner->FindComponentByClass<UPEPInventoryComponent>() : nullptr;
}

bool UPEPInventorySessionComponent::HasAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

FPEPItemAccessContext UPEPInventorySessionComponent::GetRequestContext() const
{
	if (const UPEPInventoryComponent* Local = GetLocalInventory())
	{
		return Local->GetOwnerRequestContext();
	}
	return FPEPItemAccessContext::From(Cast<AController>(GetOwner()));
}

const AActor* UPEPInventorySessionComponent::GetLocationActor() const
{
	if (const UPEPInventoryComponent* Local = GetLocalInventory())
	{
		return Local->GetOwner();
	}
	if (const AController* Controller = Cast<AController>(GetOwner()))
	{
		if (const APawn* Pawn = Controller->GetPawn())
		{
			return Pawn;
		}
	}
	return GetOwner();
}

// ---- The mirror ----------------------------------------------------------------------------

void UPEPInventorySessionComponent::GetSessionItemIds(TArray<FPEPItemId>& OutItemIds) const
{
	OutItemIds.Reset(SessionView.Entries.Num());
	for (const FPEPInventoryEntry& Entry : SessionView.Entries)
	{
		OutItemIds.Add(Entry.Item.ItemId);
	}
}

bool UPEPInventorySessionComponent::FindSessionEntry(
	const FPEPItemId ItemId,
	FPEPInventoryEntry& OutEntry) const
{
	const FPEPInventoryEntry* Entry = SessionView.Entries.FindByPredicate(
		[ItemId](const FPEPInventoryEntry& Candidate)
		{
			return Candidate.Item.ItemId == ItemId;
		});
	if (Entry)
	{
		OutEntry = *Entry;
		return true;
	}
	OutEntry = FPEPInventoryEntry();
	return false;
}

UPEPItemDefinition* UPEPInventorySessionComponent::GetSessionItemDefinitionById(const FPEPItemId ItemId) const
{
	if (!ItemId.IsValid())
	{
		return nullptr;
	}
	const FPEPInventoryEntry* Entry = SessionView.Entries.FindByPredicate(
		[ItemId](const FPEPInventoryEntry& Candidate)
		{
			return Candidate.Item.ItemId == ItemId;
		});
	return Entry ? const_cast<UPEPItemDefinition*>(Entry->Item.GetDefinitionIfLoaded()) : nullptr;
}

void UPEPInventorySessionComponent::HandleReplicatedSessionViewChanged()
{
	NotifySessionViewChanged();
}

void UPEPInventorySessionComponent::NotifySessionViewChanged()
{
	bSessionPreviewScratchDirty = true;
	if (!bHasTransferSession)
	{
		SessionPreviewScratch = nullptr;
	}
	OnSessionViewChanged.Broadcast();
}

void UPEPInventorySessionComponent::OnRep_TransferSessionState()
{
	NotifySessionViewChanged();
}

const UPEPInventoryComponent* UPEPInventorySessionComponent::GetSessionPreviewScratch() const
{
	if (!SessionPreviewScratch)
	{
		SessionPreviewScratch = NewObject<UPEPInventoryComponent>(GetTransientPackage());
		bSessionPreviewScratchDirty = true;
	}
	if (bSessionPreviewScratchDirty)
	{
		SessionPreviewScratch->InventoryView.Entries = SessionView.Entries;
		if (const UPEPInventoryComponent* Local = GetLocalInventory())
		{
			SessionPreviewScratch->LocationValidators = Local->LocationValidators;
		}
		else
		{
			SessionPreviewScratch->LocationValidators.Reset();
		}
		SessionPreviewScratch->bAutoPlacementSlotTagsDirty = true;
		SessionPreviewScratch->bEntryIndexDirty = true;
		SessionPreviewScratch->bContainerSlotIndexDirty = true;
		bSessionPreviewScratchDirty = false;
	}
	// Set on every read: it replicates as its own property and can land after the view does.
	SessionPreviewScratch->ExternalAccess = SessionExternalAccess;
	return SessionPreviewScratch;
}

// ---- Opening and closing ---------------------------------------------------------------------

FPEPItemResult UPEPInventorySessionComponent::CanOpenTransferSession(
	const UPEPInventoryComponent* RemoteInventory,
	const float MaxDistance) const
{
	if (!HasAuthority())
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_NotAuthority);
	}
	if (!IsValid(RemoteInventory)
		|| RemoteInventory == GetLocalInventory()
		|| !IsValid(GetOwner())
		|| !IsValid(RemoteInventory->GetOwner())
		|| MaxDistance <= 0.0f)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget);
	}
	if (!RemoteInventory->HasMutationAuthority())
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_NotAuthority);
	}
	if (!IsWithinSessionDistance(*RemoteInventory, MaxDistance))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_OutOfRange);
	}
	return FPEPItemResult::Success();
}

FPEPItemResult UPEPInventorySessionComponent::CanOpenInspectionSession(
	const UPEPInventoryComponent* RemoteInventory,
	const FPEPItemId InspectionRootItemId,
	const float MaxDistance) const
{
	if (!InspectionRootItemId.IsValid())
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, InspectionRootItemId);
	}
	const FPEPItemResult BaseValidation = CanOpenTransferSession(RemoteInventory, MaxDistance);
	if (!BaseValidation.bSuccess)
	{
		return BaseValidation;
	}
	const FPEPInventoryEntry* RootEntry = RemoteInventory->FindEntry(InspectionRootItemId);
	if (!RootEntry)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, InspectionRootItemId);
	}
	const FPEPItemFragment_Container* Container =
		RootEntry->Item.GetDefinitionFragment<FPEPItemFragment_Container>();
	const bool bHasAttachmentSlot = Container && Container->Slots.ContainsByPredicate(
		[](const FPEPContainerSlotDefinition& Slot)
		{
			return Slot.Purpose == EPEPContainerSlotPurpose::Attachment;
		});
	return bHasAttachmentSlot
		? FPEPItemResult::Success(InspectionRootItemId)
		: FPEPItemResult::Failure(PEPCoreTags::Reason_UnsupportedMode, InspectionRootItemId);
}

bool UPEPInventorySessionComponent::OpenTransferSession(
	UPEPInventoryComponent* RemoteInventory,
	const float MaxDistance)
{
	// Refused before closing the current session: a failed open leaves the old one intact.
	if (!CanOpenTransferSession(RemoteInventory, MaxDistance).bSuccess)
	{
		return false;
	}
	return OpenSessionInternal(RemoteInventory, MaxDistance, EPEPInventorySessionMode::Transfer, FPEPItemId());
}

bool UPEPInventorySessionComponent::OpenInspectionSession(
	UPEPInventoryComponent* RemoteInventory,
	const FPEPItemId InspectionRootItemId,
	const float MaxDistance)
{
	if (!CanOpenInspectionSession(RemoteInventory, InspectionRootItemId, MaxDistance).bSuccess)
	{
		return false;
	}
	return OpenSessionInternal(RemoteInventory, MaxDistance, EPEPInventorySessionMode::Inspect, InspectionRootItemId);
}

bool UPEPInventorySessionComponent::OpenSessionInternal(
	UPEPInventoryComponent* RemoteInventory,
	const float MaxDistance,
	const EPEPInventorySessionMode Mode,
	const FPEPItemId InspectionRootItemId)
{
	if (!IsValid(RemoteInventory)
		|| (Mode == EPEPInventorySessionMode::Inspect && !InspectionRootItemId.IsValid()))
	{
		return false;
	}
	CloseTransferSession();

	TransferSession.RemoteInventory = RemoteInventory;
	TransferSession.MaxDistance = MaxDistance;
	TransferSession.RemoteChangeHandle = RemoteInventory->OnItemStoreChanged().AddUObject(
		this,
		&UPEPInventorySessionComponent::HandleRemoteStoreChanged);
	if (AActor* RemoteOwner = RemoteInventory->GetOwner())
	{
		TransferSession.RemoteOwner = RemoteOwner;
		RemoteOwner->OnEndPlay.AddUniqueDynamic(this, &ThisClass::HandleSessionRemoteEndPlay);
	}
	// The session was granted to the player in this body; a new body is a new situation.
	if (APlayerController* Controller = Cast<APlayerController>(GetOwner()))
	{
		Controller->OnPossessedPawnChanged.AddUniqueDynamic(this, &ThisClass::HandlePossessedPawnChanged);
	}

	TransferSessionId = FGuid::NewGuid();
	SessionExternalAccess = RemoteInventory->ExternalAccess;
	SessionLootPanelTag = RemoteInventory->LootPanelTag;
	SessionMode = Mode;
	SessionRootItemId = Mode == EPEPInventorySessionMode::Inspect ? InspectionRootItemId : FPEPItemId();
	bHasTransferSession = true;
	RefreshSessionView();

	if (!IsTransferSessionValid())
	{
		CloseTransferSession();
		return false;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			TransferSession.ValidationTimer,
			this,
			&UPEPInventorySessionComponent::ValidateActiveTransferSession,
			// Distance only, everything else ends the session by event. Menus hold the looter in
			// place, so only a push, a teleport or death moves them; half a second is ample.
			0.5f,
			true);
	}
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
	return true;
}

void UPEPInventorySessionComponent::CloseTransferSession()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TransferSession.ValidationTimer);
	}
	if (UPEPInventoryComponent* RemoteInventory = TransferSession.GetRemote())
	{
		RemoteInventory->OnItemStoreChanged().Remove(TransferSession.RemoteChangeHandle);
	}
	if (AActor* RemoteOwner = TransferSession.RemoteOwner.Get(/*bEvenIfPendingKill*/ true))
	{
		RemoteOwner->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleSessionRemoteEndPlay);
	}
	if (APlayerController* Controller = Cast<APlayerController>(GetOwner()))
	{
		Controller->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandlePossessedPawnChanged);
	}
	TransferSession.Reset();

	bHasTransferSession = false;
	TransferSessionId.Invalidate();
	SessionExternalAccess = EPEPExternalAccessMode::SlotRules;
	SessionLootPanelTag = FGameplayTag();
	SessionMode = EPEPInventorySessionMode::None;
	SessionRootItemId.Reset();
	SessionView.Entries.Reset();
	SessionView.MarkArrayDirty();
	NotifySessionViewChanged();

	if (AActor* Owner = GetOwner(); Owner && Owner->HasAuthority())
	{
		Owner->ForceNetUpdate();
	}
}

EPEPRequestResult UPEPInventorySessionComponent::RequestCloseTransferSession(const FGuid ExpectedSessionId)
{
	AActor* Owner = GetOwner();
	if (!Owner || !ExpectedSessionId.IsValid() || ExpectedSessionId != GetTransferSessionId())
	{
		return EPEPRequestResult::Failed;
	}
	if (Owner->HasAuthority())
	{
		CloseTransferSession();
		return EPEPRequestResult::Executed;
	}
	if (!Owner->HasLocalNetOwner())
	{
		return EPEPRequestResult::Failed;
	}
	Server_RequestCloseTransferSession(ExpectedSessionId);
	return EPEPRequestResult::Submitted;
}

void UPEPInventorySessionComponent::Server_RequestCloseTransferSession_Implementation(const FGuid ExpectedSessionId)
{
	if (ExpectedSessionId.IsValid() && ExpectedSessionId == GetTransferSessionId())
	{
		CloseTransferSession();
	}
}

void UPEPInventorySessionComponent::HandleSessionRemoteEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason)
{
	if (bHasTransferSession)
	{
		CloseTransferSession();
	}
}

void UPEPInventorySessionComponent::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	if (bHasTransferSession)
	{
		CloseTransferSession();
	}
}

void UPEPInventorySessionComponent::ValidateActiveTransferSession()
{
	if (!IsTransferSessionValid())
	{
		CloseTransferSession();
	}
}

bool UPEPInventorySessionComponent::IsTransferSessionValid() const
{
	const UPEPInventoryComponent* RemoteInventory = TransferSession.GetRemote();
	if (!bHasTransferSession
		|| !RemoteInventory
		|| !IsValid(GetOwner())
		|| !IsValid(RemoteInventory->GetOwner())
		|| TransferSession.MaxDistance <= 0.0f)
	{
		return false;
	}
	if (SessionMode == EPEPInventorySessionMode::Inspect
		&& (!SessionRootItemId.IsValid() || !RemoteInventory->FindEntry(SessionRootItemId)))
	{
		return false;
	}
	return IsWithinSessionDistance(*RemoteInventory, TransferSession.MaxDistance);
}

bool UPEPInventorySessionComponent::IsWithinSessionDistance(
	const UPEPInventoryComponent& RemoteInventory,
	const float MaxDistance) const
{
	const AActor* Body = GetLocationActor();
	const AActor* RemoteOwner = RemoteInventory.GetOwner();
	return Body && RemoteOwner
		&& FVector::DistSquared(Body->GetActorLocation(), RemoteOwner->GetActorLocation())
			<= FMath::Square(MaxDistance);
}

// ---- Visibility -----------------------------------------------------------------------------

bool UPEPInventorySessionComponent::IsItemVisibleInExternalView(
	const UPEPInventoryComponent& Store,
	const FPEPItemId ItemId,
	const EPEPAccessRole Role)
{
	FPEPItemId Cursor = ItemId;
	TSet<FPEPItemId> VisitedAncestors;
	while (true)
	{
		const FPEPInventoryEntry* Entry = Store.FindEntry(Cursor);
		if (!Entry)
		{
			return false;
		}
		if (Entry->Location.Type == EPEPItemLocationType::NamedSlot)
		{
			return true;
		}
		if (Entry->Location.Type != EPEPItemLocationType::ItemContainer)
		{
			return false;
		}
		/* Acyclic by construction; guarded anyway, because the cost of being wrong here is a hang. */
		bool bAlreadyVisited = false;
		VisitedAncestors.Add(Cursor, &bAlreadyVisited);
		if (bAlreadyVisited)
		{
			return false;
		}
		const FPEPInventoryEntry* ParentEntry = Store.FindEntry(Entry->Location.ParentItemId);
		if (!ParentEntry
			|| !PEPItemContainerRules::DoesContainerRevealContents(ParentEntry->Item, Role))
		{
			return false;
		}
		const FPEPItemFragment_Container* Container =
			ParentEntry->Item.GetDefinitionFragment<FPEPItemFragment_Container>();
		const FPEPContainerSlotDefinition* Slot = Container
			? Container->FindSlot(Entry->Location.ContainerSlotName)
			: nullptr;
		if (!Slot || !PEPItemContainerRules::DoesSlotRevealOccupants(*Slot, Role))
		{
			return false;
		}
		Cursor = Entry->Location.ParentItemId;
	}
}

bool UPEPInventorySessionComponent::IsItemInInspectionProjection(
	const UPEPInventoryComponent& Store,
	const FPEPItemId ItemId,
	const FPEPItemId InspectionRootItemId)
{
	if (!InspectionRootItemId.IsValid())
	{
		return false;
	}
	if (ItemId == InspectionRootItemId)
	{
		return Store.FindEntry(InspectionRootItemId) != nullptr;
	}
	FPEPItemId Cursor = ItemId;
	TSet<FPEPItemId> VisitedAncestors;
	while (true)
	{
		const FPEPInventoryEntry* Entry = Store.FindEntry(Cursor);
		if (!Entry || Entry->Location.Type != EPEPItemLocationType::ItemContainer)
		{
			return false;
		}
		bool bAlreadyVisited = false;
		VisitedAncestors.Add(Cursor, &bAlreadyVisited);
		if (bAlreadyVisited)
		{
			return false;
		}
		const FPEPItemId ParentItemId = Entry->Location.ParentItemId;
		if (ParentItemId == InspectionRootItemId)
		{
			const FPEPInventoryEntry* RootEntry = Store.FindEntry(InspectionRootItemId);
			const FPEPItemFragment_Container* RootContainer = RootEntry
				? RootEntry->Item.GetDefinitionFragment<FPEPItemFragment_Container>()
				: nullptr;
			const FPEPContainerSlotDefinition* Slot = RootContainer
				? RootContainer->FindSlot(Entry->Location.ContainerSlotName)
				: nullptr;
			return Slot && Slot->Purpose == EPEPContainerSlotPurpose::Attachment;
		}
		Cursor = ParentItemId;
	}
}

EPEPAccessRole UPEPInventorySessionComponent::ResolveSessionRole() const
{
	const UPEPInventoryComponent* RemoteInventory = TransferSession.GetRemote();
	/*
	 * The remote store resolves the role, from this player's identity. It is the side that owns
	 * the question — a player looting a corpse is External to it and Owner of their own stash —
	 * and asking it here rather than asserting a role is the same rule every other two-store
	 * operation follows.
	 */
	return RemoteInventory
		? RemoteInventory->ResolveAccessRole(GetRequestContext())
		: EPEPAccessRole::External;
}

void UPEPInventorySessionComponent::RefreshSessionView()
{
	SessionView.Entries.Reset();
	if (const UPEPInventoryComponent* RemoteInventory = TransferSession.GetRemote())
	{
		/*
		 * Mirrored selectively. This snapshot is replicated to the looting client, so an entry
		 * copied here is an entry that client has: the visibility rules have to be applied while
		 * building it, not by whatever draws it afterwards.
		 */
		const EPEPAccessRole Role = ResolveSessionRole();
		SessionView.Entries.Reserve(RemoteInventory->InventoryView.Entries.Num());
		for (const FPEPInventoryEntry& RemoteEntry : RemoteInventory->InventoryView.Entries)
		{
			if (!IsItemVisibleInExternalView(*RemoteInventory, RemoteEntry.Item.ItemId, Role))
			{
				continue;
			}
			if (SessionMode == EPEPInventorySessionMode::Inspect
				&& !IsItemInInspectionProjection(*RemoteInventory, RemoteEntry.Item.ItemId, SessionRootItemId))
			{
				continue;
			}
			FPEPInventoryEntry& Snapshot = SessionView.Entries.AddDefaulted_GetRef();
			Snapshot.Item = RemoteEntry.Item;
			Snapshot.Location = RemoteEntry.Location;
			SessionView.MarkItemDirty(Snapshot);
		}
	}
	SessionView.MarkArrayDirty();
	NotifySessionViewChanged();
}

void UPEPInventorySessionComponent::HandleRemoteStoreChanged(const FPEPItemStoreChange& Change)
{
	if (!IsTransferSessionValid())
	{
		CloseTransferSession();
		return;
	}
	if (!HasAuthority())
	{
		return;
	}
	ApplySessionViewChange(Change);
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

void UPEPInventorySessionComponent::ApplySessionViewChange(const FPEPItemStoreChange& Change)
{
	const UPEPInventoryComponent* Remote = TransferSession.GetRemote();
	if (!Remote)
	{
		RefreshSessionView();
		return;
	}
	/* Linear: a lootable container holds tens of entries, and this runs once per change. */
	const int32 Index = SessionView.Entries.IndexOfByPredicate(
		[&Change](const FPEPInventoryEntry& Entry)
		{
			return Entry.Item.ItemId == Change.ItemId;
		});
	if (Change.ChangeType == EPEPItemStoreChangeType::Removed)
	{
		if (Index != INDEX_NONE)
		{
			// Safe: the fast array keys entries by replication id, not by position.
			SessionView.Entries.RemoveAtSwap(Index);
			SessionView.MarkArrayDirty();
			NotifySessionViewChanged();
		}
		return;
	}
	/*
	 * Added, Moved and StateChanged all mean the same thing to a mirror: re-read the authority.
	 * Trusting the change payload instead would give the snapshot a second way to be wrong.
	 */
	const FPEPInventoryEntry* RemoteEntry = Remote->FindEntry(Change.ItemId);
	if (!RemoteEntry)
	{
		RefreshSessionView();
		return;
	}
	/*
	 * A container that moved carries the visibility of everything beneath it, and a mirror updated
	 * one entry at a time cannot see that far: a pouch dragged out of a sealed pocket reveals its
	 * whole contents, none of which raised a change of its own. This is the case the full rebuild
	 * exists for.
	 */
	if (Change.ChangeType == EPEPItemStoreChangeType::Moved
		&& PEPItemContainerQuery::IsContainer(RemoteEntry->Item))
	{
		RefreshSessionView();
		return;
	}
	/*
	 * Re-decided on every change rather than once when the session opened: an item moved into a
	 * pocket the looter may not see has to leave the mirror, and nothing else would say so.
	 */
	if (!IsItemVisibleInExternalView(*Remote, Change.ItemId, ResolveSessionRole())
		|| (SessionMode == EPEPInventorySessionMode::Inspect
			&& !IsItemInInspectionProjection(*Remote, Change.ItemId, SessionRootItemId)))
	{
		if (Index != INDEX_NONE)
		{
			SessionView.Entries.RemoveAtSwap(Index);
			SessionView.MarkArrayDirty();
			NotifySessionViewChanged();
		}
		return;
	}
	if (Index != INDEX_NONE)
	{
		FPEPInventoryEntry& Snapshot = SessionView.Entries[Index];
		Snapshot.Item = RemoteEntry->Item;
		Snapshot.Location = RemoteEntry->Location;
		SessionView.MarkItemDirty(Snapshot);
	}
	else
	{
		FPEPInventoryEntry& Snapshot = SessionView.Entries.AddDefaulted_GetRef();
		Snapshot.Item = RemoteEntry->Item;
		Snapshot.Location = RemoteEntry->Location;
		SessionView.MarkItemDirty(Snapshot);
	}
	NotifySessionViewChanged();
}

// ---- Requests -------------------------------------------------------------------------------

int32 UPEPInventorySessionComponent::AllocateRequestId()
{
	if (NextRequestId <= 0)
	{
		NextRequestId = 1;
	}
	return NextRequestId++;
}

void UPEPInventorySessionComponent::ResolveInventoryRequest(
	const int32 RequestId,
	const EPEPInventoryRequestKind Kind,
	const FPEPItemResult& Result)
{
	FPEPInventoryRequestResult RequestResult;
	RequestResult.RequestId = RequestId;
	RequestResult.Kind = Kind;
	RequestResult.Result = Result;
	AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority() && Owner->GetNetMode() != NM_Standalone && Owner->HasNetOwner())
	{
		Client_ResolveInventoryRequest(RequestResult);
		return;
	}
	OnRequestResolved.Broadcast(RequestResult);
}

void UPEPInventorySessionComponent::Client_ResolveInventoryRequest_Implementation(
	const FPEPInventoryRequestResult RequestResult)
{
	OnRequestResolved.Broadcast(RequestResult);
}

EPEPRequestResult UPEPInventorySessionComponent::RequestTakeFromSession(
	const FGuid ExpectedSessionId,
	const FPEPItemId RemoteItemId,
	const FPEPItemLocation LocalDestination,
	int32& OutRequestId)
{
	OutRequestId = 0;
	if (!ExpectedSessionId.IsValid() || ExpectedSessionId != GetTransferSessionId()
		|| IsTransferSessionReadOnly())
	{
		return EPEPRequestResult::Failed;
	}
	AActor* Owner = GetOwner();
	if (!Owner || !RemoteItemId.IsValid() || !LocalDestination.IsStructurallyValid())
	{
		return EPEPRequestResult::Failed;
	}
	OutRequestId = AllocateRequestId();
	if (Owner->HasAuthority())
	{
		UPEPInventoryComponent* Local = GetLocalInventory();
		const FPEPItemResult Result = !Local
			? FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, RemoteItemId)
			: IsTransferSessionValid()
				? TransferSession.RemoteInventory->TransferItemToInventory(
					Local, RemoteItemId, LocalDestination, GetRequestContext())
				: FPEPItemResult::Failure(PEPCoreTags::Reason_OutOfRange, RemoteItemId);
		ResolveInventoryRequest(OutRequestId, EPEPInventoryRequestKind::TakeFromSession, Result);
		return Result.bSuccess ? EPEPRequestResult::Executed : EPEPRequestResult::Failed;
	}
	if (!Owner->HasLocalNetOwner())
	{
		OutRequestId = 0;
		return EPEPRequestResult::Failed;
	}
	Server_RequestTakeFromSession(ExpectedSessionId, RemoteItemId, LocalDestination, OutRequestId);
	return EPEPRequestResult::Submitted;
}

FPEPItemResult UPEPInventorySessionComponent::PreviewTakeFromSession(
	const FPEPItemId RemoteItemId,
	const FPEPItemLocation LocalDestination) const
{
	if (IsTransferSessionReadOnly())
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_UnsupportedMode, RemoteItemId);
	}
	const FPEPInventoryEntry* SourceEntry = SessionView.Entries.FindByPredicate(
		[RemoteItemId](const FPEPInventoryEntry& Entry)
		{
			return Entry.Item.ItemId == RemoteItemId;
		});
	const UPEPInventoryComponent* Local = GetLocalInventory();
	if (!bHasTransferSession || !SourceEntry || !Local)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, RemoteItemId);
	}
	/* Destination preview is exact; source permissions remain server-authoritative. */
	return Local->CanAddItem(SourceEntry->Item, LocalDestination, GetRequestContext());
}

EPEPRequestResult UPEPInventorySessionComponent::RequestPutIntoSession(
	const FGuid ExpectedSessionId,
	const FPEPItemId LocalItemId,
	const FPEPItemLocation RemoteDestination,
	int32& OutRequestId)
{
	OutRequestId = 0;
	if (!ExpectedSessionId.IsValid() || ExpectedSessionId != GetTransferSessionId()
		|| IsTransferSessionReadOnly())
	{
		return EPEPRequestResult::Failed;
	}
	AActor* Owner = GetOwner();
	if (!Owner || !LocalItemId.IsValid() || !RemoteDestination.IsStructurallyValid())
	{
		return EPEPRequestResult::Failed;
	}
	OutRequestId = AllocateRequestId();
	if (Owner->HasAuthority())
	{
		UPEPInventoryComponent* Local = GetLocalInventory();
		const FPEPItemResult Result = !Local
			? FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, LocalItemId)
			: IsTransferSessionValid()
				? Local->TransferItemToInventory(
					TransferSession.GetRemote(), LocalItemId, RemoteDestination, GetRequestContext())
				: FPEPItemResult::Failure(PEPCoreTags::Reason_OutOfRange, LocalItemId);
		ResolveInventoryRequest(OutRequestId, EPEPInventoryRequestKind::PutIntoSession, Result);
		return Result.bSuccess ? EPEPRequestResult::Executed : EPEPRequestResult::Failed;
	}
	if (!Owner->HasLocalNetOwner())
	{
		OutRequestId = 0;
		return EPEPRequestResult::Failed;
	}
	Server_RequestPutIntoSession(ExpectedSessionId, LocalItemId, RemoteDestination, OutRequestId);
	return EPEPRequestResult::Submitted;
}

FPEPItemResult UPEPInventorySessionComponent::PreviewPutIntoSession(
	const FPEPItemId LocalItemId,
	const FPEPItemLocation RemoteDestination) const
{
	if (IsTransferSessionReadOnly())
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_UnsupportedMode, LocalItemId);
	}
	const UPEPInventoryComponent* Local = GetLocalInventory();
	const FPEPInventoryEntry* SourceEntry = Local ? Local->FindEntry(LocalItemId) : nullptr;
	if (!bHasTransferSession || !SourceEntry)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, LocalItemId);
	}
	const FPEPItemResult SourceValidation = Local->CanExtractItemTree(LocalItemId, GetRequestContext());
	if (!SourceValidation.bSuccess)
	{
		return SourceValidation;
	}
	const UPEPInventoryComponent* Scratch = GetSessionPreviewScratch();
	return Scratch->ValidateLocationForItem(
		SourceEntry->Item,
		RemoteDestination,
		Scratch->ResolveAccessRole(GetRequestContext()));
}

void UPEPInventorySessionComponent::Server_RequestTakeFromSession_Implementation(
	const FGuid ExpectedSessionId,
	const FPEPItemId RemoteItemId,
	const FPEPItemLocation LocalDestination,
	const int32 RequestId)
{
	const bool bReadOnlySession = IsTransferSessionReadOnly();
	const bool bSessionMatches = ExpectedSessionId.IsValid() && ExpectedSessionId == GetTransferSessionId();
	UPEPInventoryComponent* Local = GetLocalInventory();
	const FPEPItemResult Result = RequestId > 0 && bSessionMatches && !bReadOnlySession
		&& Local && IsTransferSessionValid()
		? TransferSession.RemoteInventory->TransferItemToInventory(
			Local, RemoteItemId, LocalDestination, GetRequestContext())
		: FPEPItemResult::Failure(
			bReadOnlySession ? PEPCoreTags::Reason_UnsupportedMode
				: (bSessionMatches && Local ? PEPCoreTags::Reason_OutOfRange : PEPCoreTags::Reason_InvalidTarget),
			RemoteItemId);
	ResolveInventoryRequest(RequestId, EPEPInventoryRequestKind::TakeFromSession, Result);
}

void UPEPInventorySessionComponent::Server_RequestPutIntoSession_Implementation(
	const FGuid ExpectedSessionId,
	const FPEPItemId LocalItemId,
	const FPEPItemLocation RemoteDestination,
	const int32 RequestId)
{
	const bool bReadOnlySession = IsTransferSessionReadOnly();
	const bool bSessionMatches = ExpectedSessionId.IsValid() && ExpectedSessionId == GetTransferSessionId();
	UPEPInventoryComponent* Local = GetLocalInventory();
	const FPEPItemResult Result = RequestId > 0 && bSessionMatches && !bReadOnlySession
		&& Local && IsTransferSessionValid()
		? Local->TransferItemToInventory(
			TransferSession.GetRemote(), LocalItemId, RemoteDestination, GetRequestContext())
		: FPEPItemResult::Failure(
			bReadOnlySession ? PEPCoreTags::Reason_UnsupportedMode
				: (bSessionMatches && Local ? PEPCoreTags::Reason_OutOfRange : PEPCoreTags::Reason_InvalidTarget),
			LocalItemId);
	ResolveInventoryRequest(RequestId, EPEPInventoryRequestKind::PutIntoSession, Result);
}

EPEPRequestResult UPEPInventorySessionComponent::RequestMoveInSession(
	const FGuid ExpectedSessionId, const FPEPItemId RemoteItemId,
	const FPEPItemLocation RemoteDestination, int32& OutRequestId)
{
	OutRequestId = 0;
	AActor* Owner = GetOwner();
	if (!Owner || !ExpectedSessionId.IsValid() || ExpectedSessionId != GetTransferSessionId()
		|| IsTransferSessionReadOnly() || !bHasTransferSession
		|| !RemoteItemId.IsValid() || !RemoteDestination.IsStructurallyValid())
	{
		return EPEPRequestResult::Failed;
	}
	if (!Owner->HasAuthority() && !Owner->HasLocalNetOwner())
	{
		return EPEPRequestResult::Failed;
	}
	OutRequestId = AllocateRequestId();
	if (Owner->HasAuthority())
	{
		const FPEPItemResult Result = IsTransferSessionValid()
			? TransferSession.GetRemote()->MoveItem(RemoteItemId, RemoteDestination, GetRequestContext())
			: FPEPItemResult::Failure(PEPCoreTags::Reason_OutOfRange, RemoteItemId);
		ResolveInventoryRequest(OutRequestId, EPEPInventoryRequestKind::MoveInSession, Result);
		return Result.bSuccess ? EPEPRequestResult::Executed : EPEPRequestResult::Failed;
	}
	Server_RequestMoveInSession(ExpectedSessionId, RemoteItemId, RemoteDestination, OutRequestId);
	return EPEPRequestResult::Submitted;
}

FPEPItemResult UPEPInventorySessionComponent::PreviewMoveInSession(
	const FPEPItemId RemoteItemId, const FPEPItemLocation RemoteDestination) const
{
	if (IsTransferSessionReadOnly())
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_UnsupportedMode, RemoteItemId);
	}
	if (!bHasTransferSession || !RemoteItemId.IsValid() || !RemoteDestination.IsStructurallyValid())
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, RemoteItemId);
	}
	if (HasAuthority())
	{
		return IsTransferSessionValid()
			? TransferSession.GetRemote()->CanMoveItem(RemoteItemId, RemoteDestination, GetRequestContext())
			: FPEPItemResult::Failure(PEPCoreTags::Reason_OutOfRange, RemoteItemId);
	}
	// Uses the replicated access mode and the authorized view; hidden occupancy is checked on the server.
	return GetSessionPreviewScratch()->CanMoveItem(RemoteItemId, RemoteDestination, GetRequestContext());
}

void UPEPInventorySessionComponent::Server_RequestMoveInSession_Implementation(
	const FGuid SessionId, const FPEPItemId RemoteItemId,
	const FPEPItemLocation RemoteDestination, const int32 RequestId)
{
	const bool bReadOnlySession = IsTransferSessionReadOnly();
	const bool bSessionMatches = SessionId.IsValid() && SessionId == GetTransferSessionId();
	/* Same reasons as Take/Put: a stale session id is a wrong target, not a distance failure. */
	const FPEPItemResult Result = RequestId > 0 && bSessionMatches
		&& !bReadOnlySession && IsTransferSessionValid()
		? TransferSession.GetRemote()->MoveItem(RemoteItemId, RemoteDestination, GetRequestContext())
		: FPEPItemResult::Failure(
			bReadOnlySession ? PEPCoreTags::Reason_UnsupportedMode
				: (bSessionMatches ? PEPCoreTags::Reason_OutOfRange : PEPCoreTags::Reason_InvalidTarget),
			RemoteItemId);
	ResolveInventoryRequest(RequestId, EPEPInventoryRequestKind::MoveInSession, Result);
}
