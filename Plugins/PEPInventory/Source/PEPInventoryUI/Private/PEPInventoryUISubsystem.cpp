#include "PEPInventoryUISubsystem.h"
#include "PEPInventoryWindow.h"

#include "PEPInventoryUIDragOperation.h"
#include "PEPInventoryUISettings.h"
#include "Blueprint/DragDropOperation.h"
#include "Blueprint/WidgetTree.h"

#include "Components/PEPInventoryComponent.h"
#include "Components/PEPInventorySessionComponent.h"
#include "Core/PEPCoreTags.h"
#include "Inventory/PEPInventoryTags.h"
#include "Player/PEPPlayerMenuHost.h"
#include "Engine/AssetManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

void UPEPInventoryUISubsystem::Deinitialize()
{
	if (APlayerController* PC = ObservedController.Get())
		PC->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandlePawnChanged);
	if (IPEPPlayerMenuHost* MenuHost = Cast<IPEPPlayerMenuHost>(ObservedController.Get()))
		MenuHost->GetOnPlayerInputStateChanged().RemoveDynamic(this, &ThisClass::HandlePlayerInputStateChanged);
	CancelSessionWindowLoad();
	// The inventory first: detaching it re-reads the session presentation, through the controller.
	DetachInventory();
	DetachSession();
	// Forgotten too, or a late GetSessionComponent would find and bind its component again.
	ObservedController.Reset();
	InventoryScreen = nullptr;
	Super::Deinitialize();
}

void UPEPInventoryUISubsystem::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	const bool bControllerChanged = NewPlayerController != ObservedController.Get();
	if (APlayerController* Previous = ObservedController.Get())
		Previous->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandlePawnChanged);
	if (IPEPPlayerMenuHost* Previous = Cast<IPEPPlayerMenuHost>(ObservedController.Get()))
		Previous->GetOnPlayerInputStateChanged().RemoveDynamic(this, &ThisClass::HandlePlayerInputStateChanged);
	DetachSession();
	ObservedController = NewPlayerController;
	AttachSession(NewPlayerController);
	if (bControllerChanged)
	{
		/*
		 * Whatever was presented belonged to the previous controller's session. Withdrawn, so the
		 * loot side hides and the new controller's next session is presented as new, not skipped
		 * as the one already on screen.
		 */
		CancelSessionWindowLoad();
		if (PresentedSessionId.IsValid())
		{
			PresentedSessionId.Invalidate();
			OnTransferSessionPresented.Broadcast(FPEPInventoryUIContext());
		}
	}
	if (NewPlayerController)
		NewPlayerController->OnPossessedPawnChanged.AddUniqueDynamic(this, &ThisClass::HandlePawnChanged);
	if (IPEPPlayerMenuHost* MenuHost = Cast<IPEPPlayerMenuHost>(NewPlayerController))
		MenuHost->GetOnPlayerInputStateChanged().AddUniqueDynamic(this, &ThisClass::HandlePlayerInputStateChanged);

	Super::PlayerControllerChanged(NewPlayerController);
	// The initial controller notification can arrive after Blueprint registered the inventory.
	// Preserve its delegates and context identity when it belongs to this controller.
	if (NewPlayerController)
	{
		const UPEPInventoryComponent* Inventory = PlayerInventory.Get();
		const AActor* InventoryOwner = Inventory ? Inventory->GetOwner() : nullptr;
		if (IsValid(InventoryOwner))
		{
			const APawn* Pawn = Cast<APawn>(InventoryOwner);
			const APlayerState* PlayerState = Cast<APlayerState>(InventoryOwner);
			if (InventoryOwner == NewPlayerController
				|| (Pawn && Pawn->GetController() == NewPlayerController)
				|| (PlayerState && PlayerState->GetOwningController() == NewPlayerController)
				|| InventoryOwner->IsOwnedBy(NewPlayerController))
			{
				return;
			}
		}
	}
	UnregisterInventoryComponent();
	if (NewPlayerController) HandlePawnChanged(nullptr, NewPlayerController->GetPawn());
}

bool UPEPInventoryUISubsystem::RegisterInventoryComponent(UPEPInventoryComponent* InventoryComponent)
{
	if (!IsValid(InventoryComponent) || !IsValid(InventoryComponent->GetOwner())
		|| InventoryComponent->GetOwner()->GetNetOwningPlayerAnyRole() != GetLocalPlayer())
	{
		return false;
	}
	if (PlayerInventory.Get() == InventoryComponent)
	{
		return true;
	}

	DetachInventory();
	PlayerInventory = InventoryComponent;
	InventoryBindingId = FGuid::NewGuid();
	InventoryComponent->OnInventoryChanged.AddUniqueDynamic(this, &ThisClass::HandleInventoryChanged);
	InventoryComponent->OnRequestResolved.AddUniqueDynamic(this, &ThisClass::HandleRequestResolved);
	OnInventoryBindingChanged.Broadcast();
	HandleSessionViewChanged();
	return true;
}

void UPEPInventoryUISubsystem::DetachInventory()
{
	ClearActiveDrag();
	if (UPEPInventoryComponent* Inventory = PlayerInventory.Get())
	{
		Inventory->OnInventoryChanged.RemoveDynamic(this, &ThisClass::HandleInventoryChanged);
		Inventory->OnRequestResolved.RemoveDynamic(this, &ThisClass::HandleRequestResolved);
	}
	PlayerInventory.Reset();
	InventoryBindingId.Invalidate();
	UpdateSessionWindow();
}

void UPEPInventoryUISubsystem::UnregisterInventoryComponent()
{
	const bool bWasBound = InventoryBindingId.IsValid();
	DetachInventory();
	if (bWasBound)
	{
		OnInventoryBindingChanged.Broadcast();
		OnSessionViewChanged.Broadcast();
	}
}

UPEPInventoryComponent* UPEPInventoryUISubsystem::GetInventoryComponent() const
{
	UPEPInventoryComponent* Inventory = PlayerInventory.Get();
	return IsValid(Inventory) && IsValid(Inventory->GetOwner())
		&& Inventory->GetOwner()->GetNetOwningPlayerAnyRole() == GetLocalPlayer() ? Inventory : nullptr;
}

UPEPInventorySessionComponent* UPEPInventoryUISubsystem::GetSessionComponent() const
{
	if (UPEPInventorySessionComponent* Session = ObservedSession.Get(); IsValid(Session))
	{
		return Session;
	}
	const APlayerController* PC = ObservedController.Get();
	UPEPInventorySessionComponent* Found = PC ? PC->FindComponentByClass<UPEPInventorySessionComponent>() : nullptr;
	if (!IsValid(Found))
	{
		return nullptr;
	}
	/*
	 * Added after the controller was announced: by a Blueprint at BeginPlay, a game feature, a
	 * replicated component arriving late. Bound now, or the sessions it opens would never reach
	 * this subsystem's events. Binding changes no answer a caller can read, so a reader may do it.
	 */
	const_cast<ThisClass*>(this)->BindSession(Found);
	return Found;
}

void UPEPInventoryUISubsystem::AttachSession(APlayerController* Controller)
{
	BindSession(Controller ? Controller->FindComponentByClass<UPEPInventorySessionComponent>() : nullptr);
}

void UPEPInventoryUISubsystem::BindSession(UPEPInventorySessionComponent* Session)
{
	if (ObservedSession.Get() == Session)
	{
		return;
	}
	DetachSession();
	ObservedSession = Session;
	if (Session)
	{
		Session->OnSessionViewChanged.AddUniqueDynamic(this, &ThisClass::HandleSessionViewChanged);
		Session->OnRequestResolved.AddUniqueDynamic(this, &ThisClass::HandleRequestResolved);
	}
}

void UPEPInventoryUISubsystem::DetachSession()
{
	if (UPEPInventorySessionComponent* Session = ObservedSession.Get())
	{
		Session->OnSessionViewChanged.RemoveDynamic(this, &ThisClass::HandleSessionViewChanged);
		Session->OnRequestResolved.RemoveDynamic(this, &ThisClass::HandleRequestResolved);
	}
	ObservedSession.Reset();
}

FPEPInventoryUIContext UPEPInventoryUISubsystem::GetPlayerInventoryContext() const
{
	FPEPInventoryUIContext Context;
	if (GetInventoryComponent())
	{
		Context.Type = EPEPInventoryUIContextType::PlayerInventory;
		Context.InventoryBindingId = InventoryBindingId;
	}
	return Context;
}

FPEPInventoryUIContext UPEPInventoryUISubsystem::GetTransferSessionContext() const
{
	FPEPInventoryUIContext Context;
	const UPEPInventorySessionComponent* Session = GetSessionComponent();
	if (GetInventoryComponent() && Session)
	{
		const FGuid SessionId = Session->GetTransferSessionId();
		const EPEPInventorySessionMode SessionMode = Session->GetTransferSessionMode();
		if (SessionId.IsValid() && SessionMode != EPEPInventorySessionMode::None)
		{
			Context.Type = EPEPInventoryUIContextType::TransferSession;
			Context.InventoryBindingId = InventoryBindingId;
			Context.TransferSessionId = SessionId;
			Context.SessionMode = SessionMode;
		}
	}
	return Context;
}

FGameplayTag UPEPInventoryUISubsystem::GetTransferSessionPanelTag() const
{
	const UPEPInventorySessionComponent* Session = GetSessionComponent();
	return Session ? Session->GetTransferSessionPanelTag() : FGameplayTag();
}

EPEPInventorySessionMode UPEPInventoryUISubsystem::GetTransferSessionMode() const
{
	const UPEPInventorySessionComponent* Session = GetSessionComponent();
	return Session ? Session->GetTransferSessionMode() : EPEPInventorySessionMode::None;
}

bool UPEPInventoryUISubsystem::IsTransferSessionReadOnly() const
{
	return GetTransferSessionMode() == EPEPInventorySessionMode::Inspect;
}

bool UPEPInventoryUISubsystem::IsContextCurrent(const FPEPInventoryUIContext& Context) const
{
	const UPEPInventoryComponent* Inventory = GetInventoryComponent();
	if (!Inventory || !Context.InventoryBindingId.IsValid()
		|| Context.InventoryBindingId != InventoryBindingId)
	{
		return false;
	}
	if (Context.Type == EPEPInventoryUIContextType::PlayerInventory)
	{
		return !Context.TransferSessionId.IsValid()
			&& Context.SessionMode == EPEPInventorySessionMode::None;
	}
	const UPEPInventorySessionComponent* Session = GetSessionComponent();
	return Session
		&& Context.Type == EPEPInventoryUIContextType::TransferSession
		&& Context.TransferSessionId.IsValid()
		&& Context.TransferSessionId == Session->GetTransferSessionId()
		&& Context.SessionMode == Session->GetTransferSessionMode();
}

namespace
{
	bool IsSameContext(const FPEPInventoryUIContext& A, const FPEPInventoryUIContext& B)
	{
		return A.Type == B.Type && A.InventoryBindingId == B.InventoryBindingId
		&& A.TransferSessionId == B.TransferSessionId && A.SessionMode == B.SessionMode;
	}
}

FPEPItemResult UPEPInventoryUISubsystem::PreviewPlacement(
	const FPEPInventoryUIContext& SourceContext, const FPEPInventoryUIContext& DestinationContext,
	FPEPItemId ItemId, FPEPItemLocation Destination) const
{
	FPlacementPreviewCache& Cache = PlacementPreviewCache;
	if (Cache.bValid && Cache.ItemId == ItemId && Cache.Location == Destination
		&& IsSameContext(Cache.Source, SourceContext) && IsSameContext(Cache.Destination, DestinationContext))
	{
		return Cache.Result;
	}
	Cache.Result = ComputePlacementPreview(SourceContext, DestinationContext, ItemId, Destination);
	Cache.Source = SourceContext;
	Cache.Destination = DestinationContext;
	Cache.ItemId = ItemId;
	Cache.Location = Destination;
	Cache.bValid = true;
	return Cache.Result;
}

FPEPItemResult UPEPInventoryUISubsystem::ComputePlacementPreview(
	const FPEPInventoryUIContext& SourceContext, const FPEPInventoryUIContext& DestinationContext,
	FPEPItemId ItemId, const FPEPItemLocation& Destination) const
{
	if (!IsContextCurrent(SourceContext) || !IsContextCurrent(DestinationContext))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_StateMismatch, ItemId);
	}
	if (!ItemId.IsValid() || !Destination.IsStructurallyValid())
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
	}

	UPEPInventoryComponent* Inventory = GetInventoryComponent();
	UPEPInventorySessionComponent* Session = GetSessionComponent();
	const bool bInvolvesSession = SourceContext.Type == EPEPInventoryUIContextType::TransferSession
		|| DestinationContext.Type == EPEPInventoryUIContextType::TransferSession;
	if (bInvolvesSession && (!Session || Session->IsTransferSessionReadOnly()))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_UnsupportedMode, ItemId);
	}
	if (SourceContext.Type == EPEPInventoryUIContextType::PlayerInventory)
	{
		return DestinationContext.Type == EPEPInventoryUIContextType::PlayerInventory
			? Inventory->PreviewMoveItem(ItemId, Destination)
			: Session->PreviewPutIntoSession(ItemId, Destination);
	}
	if (DestinationContext.Type == EPEPInventoryUIContextType::PlayerInventory)
	{
		return Session->PreviewTakeFromSession(ItemId, Destination);
	}
	return Session->PreviewMoveInSession(ItemId, Destination);
}

EPEPRequestResult UPEPInventoryUISubsystem::RequestPlacement(
	const FPEPInventoryUIContext& SourceContext, const FPEPInventoryUIContext& DestinationContext,
	FPEPItemId ItemId, FPEPItemLocation Destination, int32& OutRequestId)
{
	OutRequestId = 0;
	// Never from cache: this is the last check before a request, so it reads the stores as they are.
	if (!ComputePlacementPreview(SourceContext, DestinationContext, ItemId, Destination).bSuccess)
	{
		return EPEPRequestResult::Failed;
	}

	UPEPInventoryComponent* Inventory = GetInventoryComponent();
	UPEPInventorySessionComponent* Session = GetSessionComponent();
	if (SourceContext.Type == EPEPInventoryUIContextType::PlayerInventory)
	{
		return DestinationContext.Type == EPEPInventoryUIContextType::PlayerInventory
			? Inventory->RequestMoveItem(ItemId, Destination, OutRequestId)
			: Session->RequestPutIntoSession(
				DestinationContext.TransferSessionId, ItemId, Destination, OutRequestId);
	}
	return DestinationContext.Type == EPEPInventoryUIContextType::PlayerInventory
		? Session->RequestTakeFromSession(
			SourceContext.TransferSessionId, ItemId, Destination, OutRequestId)
		: Session->RequestMoveInSession(
			SourceContext.TransferSessionId, ItemId, Destination, OutRequestId);
}

const UPEPInventoryUISettings* UPEPInventoryUISubsystem::GetUISettings() const
{
	return GetDefault<UPEPInventoryUISettings>();
}

FLinearColor UPEPInventoryUISubsystem::GetDropPreviewColor(bool bIsValidPlacement) const
{
	const UPEPInventoryUISettings* Settings = GetUISettings();
	FLinearColor Color = bIsValidPlacement ? Settings->ValidDropColor : Settings->InvalidDropColor;
	Color.A *= FMath::Clamp(Settings->PreviewOpacity, 0.f, 1.f);
	return Color;
}

bool UPEPInventoryUISubsystem::BeginDrag(UDragDropOperation* Operation)
{
	if (!IsValid(Operation)
		|| !Operation->GetClass()->ImplementsInterface(UPEPInventoryUIDragOperation::StaticClass()))
	{
		return false;
	}

	if (UDragDropOperation* Active = GetActiveDragOperation())
	{
		// Do not let a second window replace an interaction that has not ended.
		return Active == Operation;
	}

	ActiveDragOperation = Operation;
	Operation->OnDrop.AddUniqueDynamic(this, &ThisClass::HandleDragEnded);
	Operation->OnDragCancelled.AddUniqueDynamic(this, &ThisClass::HandleDragEnded);
	return true;
}

bool UPEPInventoryUISubsystem::RotateActiveDrag()
{
	UDragDropOperation* Operation = GetActiveDragOperation();
	return Operation && IPEPInventoryUIDragOperation::Execute_RotateDraggedItem(Operation);
}

UDragDropOperation* UPEPInventoryUISubsystem::GetActiveDragOperation() const
{
	return ActiveDragOperation.Get();
}

void UPEPInventoryUISubsystem::EndDrag(UDragDropOperation* Operation)
{
	if (Operation && ActiveDragOperation.Get() == Operation)
	{
		ClearActiveDrag();
	}
}

void UPEPInventoryUISubsystem::ClearActiveDrag()
{
	// A cached answer belongs to one drag; the next one starts asking afresh.
	InvalidatePlacementPreview();
	if (UDragDropOperation* Operation = ActiveDragOperation.Get())
	{
		Operation->OnDrop.RemoveDynamic(this, &ThisClass::HandleDragEnded);
		Operation->OnDragCancelled.RemoveDynamic(this, &ThisClass::HandleDragEnded);
	}
	ActiveDragOperation.Reset();
}

void UPEPInventoryUISubsystem::HandleDragEnded(UDragDropOperation* Operation)
{
	EndDrag(Operation);
}

void UPEPInventoryUISubsystem::HandleInventoryChanged(const FPEPItemStoreChange& Change)
{
	InvalidatePlacementPreview();
	OnInventoryChanged.Broadcast(Change);
}

void UPEPInventoryUISubsystem::HandleSessionViewChanged()
{
	InvalidatePlacementPreview();
	UpdateSessionWindow();
	OnSessionViewChanged.Broadcast();
}

void UPEPInventoryUISubsystem::HandleRequestResolved(const FPEPInventoryRequestResult& RequestResult)
{
	OnRequestResolved.Broadcast(RequestResult);
}

namespace PEPInventoryUISubsystemPrivate
{
	/** The world item a transfer session was opened on: the mirror's occupant of the world-item slot. */
	const FPEPInventoryEntry* FindWorldItemRootEntry(const UPEPInventorySessionComponent& Session)
	{
		for (const FPEPInventoryEntry& Entry : Session.GetSessionEntries())
		{
			if (Entry.Location.Type == EPEPItemLocationType::NamedSlot
				&& Entry.Location.LocationTag == PEPInventoryTags::Inventory_Location_WorldItem)
			{
				return &Entry;
			}
		}
		return nullptr;
	}
}

void UPEPInventoryUISubsystem::GetEntriesInContainerSlot(const FPEPInventoryUIContext& Context,
	FPEPItemId ParentItemId, FName SlotName, TArray<FPEPInventoryEntry>& OutEntries) const
{
	OutEntries.Reset();
	if (!IsContextCurrent(Context))
	{
		return;
	}
	if (Context.Type == EPEPInventoryUIContextType::PlayerInventory)
	{
		GetInventoryComponent()->GetEntriesInContainerSlot(ParentItemId, SlotName, OutEntries);
		return;
	}
	const UPEPInventorySessionComponent* Session = GetSessionComponent();
	if (!Session)
	{
		return;
	}
	// One pass over the mirror; only the occupants are copied, once each.
	for (const FPEPInventoryEntry& Entry : Session->GetSessionEntries())
	{
		const FPEPItemLocation& Location = Entry.Location;
		if (Location.Type == EPEPItemLocationType::ItemContainer
			&& Location.ParentItemId == ParentItemId && Location.ContainerSlotName == SlotName)
		{
			OutEntries.Add(Entry);
		}
	}
}

bool UPEPInventoryUISubsystem::FindEntryInNamedSlot(const FPEPInventoryUIContext& Context,
	FGameplayTag SlotTag, FPEPInventoryEntry& OutEntry) const
{
	OutEntry = FPEPInventoryEntry();
	if (!SlotTag.IsValid() || !IsContextCurrent(Context))
	{
		return false;
	}
	const FPEPInventoryEntry* Found = nullptr;
	if (Context.Type == EPEPInventoryUIContextType::TransferSession)
	{
		const UPEPInventorySessionComponent* Session = GetSessionComponent();
		if (!Session)
		{
			return false;
		}
		for (const FPEPInventoryEntry& Entry : Session->GetSessionEntries())
		{
			if (Entry.Location.Type == EPEPItemLocationType::NamedSlot && Entry.Location.LocationTag == SlotTag)
			{
				Found = &Entry;
				break;
			}
		}
	}
	else
	{
		// The store answers a named slot from its own slot index.
		const UPEPInventoryComponent* Inventory = GetInventoryComponent();
		FPEPItemId ItemId;
		if (Inventory && Inventory->FindItemAtLocation(FPEPItemLocation::MakeNamedSlot(SlotTag), ItemId))
		{
			Found = Inventory->FindEntry(ItemId);
		}
	}
	if (!Found)
	{
		return false;
	}
	OutEntry = *Found;
	return true;
}

bool UPEPInventoryUISubsystem::GetTransferSessionRootItem(FPEPItemInstance& OutRootItem) const
{
	OutRootItem = FPEPItemInstance();
	const UPEPInventorySessionComponent* Session = GetSessionComponent();
	if (!Session || !IsContextCurrent(GetTransferSessionContext()))
	{
		return false;
	}
	// An inspection names its root; a transfer session's root is whatever holds the world-item slot.
	const FPEPInventoryEntry* RootEntry = Session->IsTransferSessionReadOnly()
		? Session->FindSessionEntryPtr(Session->GetTransferSessionRootItemId())
		: PEPInventoryUISubsystemPrivate::FindWorldItemRootEntry(*Session);
	if (!RootEntry)
	{
		return false;
	}
	OutRootItem = RootEntry->Item;
	return true;
}

void UPEPInventoryUISubsystem::UpdateSessionMenu()
{
    const FPEPInventoryUIContext Context = GetTransferSessionContext();
    const FGuid SessionId = IsContextCurrent(Context) ? Context.TransferSessionId : FGuid();
    if (SessionId == PresentedSessionId) return;
    const bool bEndedPresentedSession = PresentedSessionId.IsValid() && !SessionId.IsValid();
    PresentedSessionId = SessionId;
    // Announced before the menu opens, so the screen it reveals is already showing the loot side.
    OnTransferSessionPresented.Broadcast(SessionId.IsValid() ? Context : FPEPInventoryUIContext());
    IPEPPlayerMenuHost* MenuHost = Cast<IPEPPlayerMenuHost>(ObservedController.Get());
    if (!SessionId.IsValid())
    {
        // The container went away (picked up, destroyed, out of range): leave the menu as a toggle
        // would. A no-op when closing the menu is what ended the session.
        if (bEndedPresentedSession && MenuHost) MenuHost->CloseMenu();
        return;
    }
    if (!MenuHost || !MenuHost->OpenMenu())
    {
        // A session nobody can see would only hold the container open until the player walks off.
        PresentedSessionId.Invalidate();
        OnTransferSessionPresented.Broadcast(FPEPInventoryUIContext());
		if (UPEPInventorySessionComponent* Session = GetSessionComponent())
		{
			Session->RequestCloseTransferSession(Context.TransferSessionId);
		}
    }
}

namespace
{
	/** Removes every inventory window the screen hosts; collected first, since removing edits the tree. */
	void CloseInventoryWindows(UUserWidget* Screen)
	{
		if (!Screen || !Screen->WidgetTree) return;
		TArray<UWidget*> Widgets;
		Screen->WidgetTree->GetAllWidgets(Widgets);
		for (UWidget* Widget : Widgets)
		{
			if (UPEPInventoryWindow* Window = Cast<UPEPInventoryWindow>(Widget)) Window->RemoveFromParent();
		}
	}
}

void UPEPInventoryUISubsystem::HandlePlayerInputStateChanged(const EPEPPlayerInputState InputState)
{
    if (InputState != EPEPPlayerInputState::Gameplay) return;
    // Windows opened from the menu belong to it, so leaving the menu closes them too.
    CloseInventoryWindows(InventoryScreen);
    // Closing the menu is how the player leaves the container; the server ends the session.
    if (!PresentedSessionId.IsValid()) return;
	if (UPEPInventorySessionComponent* Session = GetSessionComponent())
	{
		Session->RequestCloseTransferSession(PresentedSessionId);
	}
}

void UPEPInventoryUISubsystem::UpdateSessionWindow()
{
	// Only a player menu host (APEPPlayerController) has a player menu; anything else keeps the floating window.
	if (GetUISettings()->bPresentSessionInPlayerMenu && Cast<IPEPPlayerMenuHost>(ObservedController.Get()))
	{
		CancelSessionWindowLoad();
		UpdateSessionMenu();
		return;
	}
	const FPEPInventoryUIContext Context = GetTransferSessionContext();
	if (SessionWindow && SessionWindow->InventoryContext.TransferSessionId == Context.TransferSessionId
		&& IsContextCurrent(Context))
	{
		return;
	}
	RemoveSessionWindow();
	const UPEPInventorySessionComponent* Session = GetSessionComponent();
	const UPEPInventoryUISettings* Settings = GetUISettings();
	const FPEPInventoryEntry* RootEntry = Session && IsContextCurrent(Context) && !Settings->SessionWindowClass.IsNull()
		? PEPInventoryUISubsystemPrivate::FindWorldItemRootEntry(*Session)
		: nullptr;
	if (!RootEntry)
	{
		// Nothing to show, so nothing to wait for: a session that closed mid-load ends here.
		CancelSessionWindowLoad();
		return;
	}
	if (UClass* WindowClass = Settings->SessionWindowClass.Get())
	{
		CancelSessionWindowLoad();
		// Copied: widget construction runs Blueprint code, which may change the mirror under a pointer.
		const FPEPItemInstance RootItem = RootEntry->Item;
		CreateSessionWindow(WindowClass, Context, RootItem);
		return;
	}
	// A load in flight for this session is waited for; one that failed is not retried until the next.
	if (SessionWindowLoadSessionId == Context.TransferSessionId)
	{
		return;
	}
	CancelSessionWindowLoad();
	if (!UAssetManager::IsInitialized())
	{
		return;
	}
	SessionWindowLoadSessionId = Context.TransferSessionId;
	SessionWindowClassHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		Settings->SessionWindowClass.ToSoftObjectPath(),
		FStreamableDelegate::CreateWeakLambda(this, [this]()
		{
			HandleSessionWindowClassLoaded();
		}));
}

void UPEPInventoryUISubsystem::HandleSessionWindowClassLoaded()
{
	SessionWindowClassHandle.Reset();
	if (!GetUISettings()->SessionWindowClass.Get())
	{
		// Failed. SessionWindowLoadSessionId stays, so this session does not ask again on every change.
		return;
	}
	SessionWindowLoadSessionId.Invalidate();
	// Whatever happened while loading (the session closed, changed, or the menu took over) is read now.
	UpdateSessionWindow();
}

void UPEPInventoryUISubsystem::CancelSessionWindowLoad()
{
	if (SessionWindowClassHandle.IsValid())
	{
		SessionWindowClassHandle->CancelHandle();
		SessionWindowClassHandle.Reset();
	}
	SessionWindowLoadSessionId.Invalidate();
}

void UPEPInventoryUISubsystem::CreateSessionWindow(UClass* WindowClass, const FPEPInventoryUIContext& Context,
	const FPEPItemInstance& RootItem)
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* PC = LocalPlayer ? LocalPlayer->GetPlayerController(GetWorld()) : nullptr;
	if (!WindowClass || !PC)
	{
		return;
	}
	SessionWindow = CreateWidget<UPEPInventoryWindow>(PC, WindowClass);
	if (!SessionWindow)
	{
		return;
	}
	SessionWindow->bManageTransferSession = true;
	SessionWindow->OwnerItem = RootItem;
	SessionWindow->InventoryContext = Context;
	SessionLayer = CreateWidget<UPEPInventorySessionLayer>(PC);
	if (!SessionLayer)
	{
		SessionWindow = nullptr;
		return;
	}
	SessionLayer->SetWindow(SessionWindow);
	SessionLayer->AddToViewport(20);
}

void UPEPInventoryUISubsystem::RemoveSessionWindow()
{
	if (!SessionWindow)
	{
		return;
	}
	UPEPInventoryWindow* Previous = SessionWindow;
	SessionWindow = nullptr;
	Previous->RemoveFromParent();
	if (SessionLayer)
	{
		SessionLayer->RemoveFromParent();
		SessionLayer = nullptr;
	}
}

void UPEPInventoryUISubsystem::HandlePawnChanged(APawn* OldPawn, APawn* NewPawn)
{
    APlayerController* PC = ObservedController.Get();
    // A session component added since the controller was announced is picked up here too.
    if (PC && !ObservedSession.IsValid()) AttachSession(PC);
    UPEPInventoryComponent* Inventory = NewPawn ? NewPawn->FindComponentByClass<UPEPInventoryComponent>() : nullptr;
    if (!Inventory && PC) Inventory = PC->FindComponentByClass<UPEPInventoryComponent>();
    if (Inventory) RegisterInventoryComponent(Inventory);
    else if (OldPawn) UnregisterInventoryComponent();
}
