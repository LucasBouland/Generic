#include "PEPInventoryWindow.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "InputCoreTypes.h"
#include "Components/PEPInventoryComponent.h"
#include "Components/PEPInventorySessionComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/** Finds ItemId in the store Context names, provided that context is still current. */
	bool FindInContext(const UPEPInventoryUISubsystem& UI, const FPEPInventoryUIContext& Context,
		const FPEPItemId ItemId, FPEPInventoryEntry& OutEntry)
	{
		if (!UI.IsContextCurrent(Context)) return false;
		if (Context.Type == EPEPInventoryUIContextType::TransferSession)
		{
			const UPEPInventorySessionComponent* Session = UI.GetSessionComponent();
			return Session && Session->FindSessionEntry(ItemId, OutEntry);
		}
		return UI.GetInventoryComponent()->FindInventoryEntry(ItemId, OutEntry);
	}

	/**
	 * Where the player can see ItemId now: Current if the item is still there, else the other store.
	 * A transfer never shows the item in neither on the way: the server finalises both stores before
	 * notifying, and on a client the player's view replicates before the session view (both on the
	 * same component, InventoryView declared first).
	 */
	bool LocateItem(const UPEPInventoryUISubsystem& UI, const FPEPInventoryUIContext& Current,
		const FPEPItemId ItemId, FPEPInventoryUIContext& OutContext, FPEPInventoryEntry& OutEntry)
	{
		const FPEPInventoryUIContext Other = Current.Type == EPEPInventoryUIContextType::TransferSession
			? UI.GetPlayerInventoryContext()
			: UI.GetTransferSessionContext();
		for (const FPEPInventoryUIContext& Candidate : { Current, Other })
		{
			if (FindInContext(UI, Candidate, ItemId, OutEntry))
			{
				OutContext = Candidate;
				return true;
			}
		}
		return false;
	}
}

void UPEPInventoryWindow::NativeConstruct()
{
	Super::NativeConstruct();
	ULocalPlayer* Player = GetOwningLocalPlayer();
	if (UPEPInventoryUISubsystem* UI = Player ? Player->GetSubsystem<UPEPInventoryUISubsystem>() : nullptr)
	{
		UI->OnSessionViewChanged.AddUniqueDynamic(this, &ThisClass::HandleSessionChanged);
		UI->OnInventoryChanged.AddUniqueDynamic(this, &ThisClass::HandleItemStoreChanged);
		BoundUI = UI;
	}
	if (bManageTransferSession)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			bAcquiredInput = !PC->bShowMouseCursor;
			if (bAcquiredInput)
			{
				PC->bShowMouseCursor = true;
				PC->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false));
			}
		}
		SetIsFocusable(true);
		SetKeyboardFocus();
	}
	/*
	 * Never removed here: removing a widget while Slate is still building its slot leaves the Slate
	 * widget on screen with no UMG parent. An item already gone, taken by another player while the
	 * item menu was open, is hidden instead; the next change or closing the menu removes it.
	 */
	if (!FollowItem(false))
	{
		SetVisibility(ESlateVisibility::Collapsed);
	}
}
void UPEPInventoryWindow::NativeDestruct()
{
	// Through the weak pointer, not the local player: when play ends this widget can be destructed
	// after the subsystem, and unbinding from a destroyed delegate trips its access detector.
	if (UPEPInventoryUISubsystem* UI = BoundUI.Get())
	{
		UI->OnSessionViewChanged.RemoveDynamic(this, &ThisClass::HandleSessionChanged);
		UI->OnInventoryChanged.RemoveDynamic(this, &ThisClass::HandleItemStoreChanged);
		if (bManageTransferSession && InventoryContext.Type == EPEPInventoryUIContextType::TransferSession
			&& UI->IsContextCurrent(InventoryContext))
		{
			if (UPEPInventorySessionComponent* Session = UI->GetSessionComponent())
			{
				Session->RequestCloseTransferSession(InventoryContext.TransferSessionId);
			}
		}
	}
	BoundUI.Reset();
	if (bAcquiredInput)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			PC->bShowMouseCursor = false;
			PC->SetInputMode(FInputModeGameOnly());
		}
		bAcquiredInput = false;
	}
	Super::NativeDestruct();
}
void UPEPInventoryWindow::HandleSessionChanged()
{
	if (!FollowItem(true)) RemoveFromParent();
}
void UPEPInventoryWindow::HandleItemStoreChanged(const FPEPItemStoreChange& Change)
{
	if (!FollowItem(false)) RemoveFromParent();
}
bool UPEPInventoryWindow::FollowItem(const bool bSessionChanged)
{
	UPEPInventoryUISubsystem* UI = BoundUI.Get();
	FPEPInventoryUIContext Context;
	FPEPInventoryEntry Entry;
	if (!UI || !LocateItem(*UI, InventoryContext, OwnerItem.ItemId, Context, Entry)) return false;
	const bool bMoved = Context.Type != InventoryContext.Type
		|| Context.InventoryBindingId != InventoryContext.InventoryBindingId
		|| Context.TransferSessionId != InventoryContext.TransferSessionId;
	InventoryContext = Context;
	OwnerItem = Entry.Item;
	/*
	 * A window that changed store re-reads it once. Otherwise session windows re-read on every
	 * session change, which carries no per-item detail, and player windows follow the per-item
	 * changes the inventory screen already forwards to them.
	 */
	if (bMoved || (bSessionChanged && Context.Type == EPEPInventoryUIContextType::TransferSession))
	{
		RefreshSessionContents();
	}
	return true;
}

FReply UPEPInventoryWindow::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (bManageTransferSession && KeyEvent.GetKey() == EKeys::Escape)
	{
		RemoveFromParent();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(Geometry, KeyEvent);
}
void UPEPInventorySessionLayer::SetWindow(UPEPInventoryWindow* Window)
{
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("SessionCanvas"));
	Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Canvas;
	UCanvasPanelSlot* WindowSlot = Canvas->AddChildToCanvas(Window);
	WindowSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	WindowSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	WindowSlot->SetPosition(FVector2D::ZeroVector);
	WindowSlot->SetAutoSize(true);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}
