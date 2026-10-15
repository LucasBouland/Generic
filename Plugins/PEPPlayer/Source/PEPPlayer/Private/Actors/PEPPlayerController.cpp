#include "Actors/PEPPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/Pawn.h"
#include "GenericPlatform/GenericApplication.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"
#include "Templates/UnrealTemplate.h"
#include "UI/PEPPlayerHUDContract.h"
#include "Widgets/SViewport.h"

namespace
{
	/** Modifier groups a menu shortcut chord can require, read from the Slate modifier state of the key event. */
	constexpr int32 ShortcutShift = 1 << 0;
	constexpr int32 ShortcutControl = 1 << 1;
	constexpr int32 ShortcutAlt = 1 << 2;
	constexpr int32 ShortcutCommand = 1 << 3;
	/** Held modifiers that reject a shortcut unless one of its chords requires them. Shift alone is tolerated. */
	constexpr int32 ShortcutExclusiveModifiers = ShortcutControl | ShortcutAlt | ShortcutCommand;

	int32 GetShortcutModifier(const FKey& Key)
	{
		if (Key == EKeys::LeftShift || Key == EKeys::RightShift)
		{
			return ShortcutShift;
		}
		if (Key == EKeys::LeftControl || Key == EKeys::RightControl)
		{
			return ShortcutControl;
		}
		if (Key == EKeys::LeftAlt || Key == EKeys::RightAlt)
		{
			return ShortcutAlt;
		}
		if (Key == EKeys::LeftCommand || Key == EKeys::RightCommand)
		{
			return ShortcutCommand;
		}
		return 0;
	}

	int32 GetHeldShortcutModifiers(const FModifierKeysState& ModifierKeys)
	{
		int32 Modifiers = 0;
		Modifiers |= ModifierKeys.IsShiftDown() ? ShortcutShift : 0;
		Modifiers |= ModifierKeys.IsControlDown() ? ShortcutControl : 0;
		Modifiers |= ModifierKeys.IsAltDown() ? ShortcutAlt : 0;
		Modifiers |= ModifierKeys.IsCommandDown() ? ShortcutCommand : 0;
		return Modifiers;
	}

	/** Modifier groups of the keys mapped to a chord action. Non-modifier keys cannot be evaluated from a key event and are ignored. */
	int32 GetChordModifiers(const UInputAction* ChordAction, const TConstArrayView<FEnhancedActionKeyMapping> Mappings)
	{
		int32 Modifiers = 0;
		if (ChordAction)
		{
			for (const FEnhancedActionKeyMapping& Mapping : Mappings)
			{
				if (Mapping.Action.Get() == ChordAction)
				{
					Modifiers |= GetShortcutModifier(Mapping.Key);
				}
			}
		}
		return Modifiers;
	}

	/**
	 * Evaluates one trigger list (action-level or mapping-level) as a single key press. Returns false for triggers the
	 * bridge cannot honor (Hold, Tap, Released, Pulse, combos, custom triggers): those mappings stay on the normal
	 * Enhanced Input path. Chords on modifier keys must be held; injected chord blockers reject while their chord is held.
	 */
	bool EvaluateShortcutTriggers(const TArray<TObjectPtr<UInputTrigger>>& Triggers,
		const TConstArrayView<FEnhancedActionKeyMapping> Mappings, const int32 HeldModifiers,
		int32& InOutRequiredModifiers, int32& InOutChordCount)
	{
		for (const TObjectPtr<UInputTrigger>& TriggerPtr : Triggers)
		{
			const UInputTrigger* Trigger = TriggerPtr.Get();
			if (!Trigger)
			{
				continue;
			}
			if (const UInputTriggerChordAction* Chord = Cast<UInputTriggerChordAction>(Trigger))
			{
				const int32 ChordModifiers = GetChordModifiers(Chord->ChordAction.Get(), Mappings);
				if (Trigger->IsA<UInputTriggerChordBlocker>())
				{
					if ((ChordModifiers & HeldModifiers) != 0)
					{
						return false;
					}
				}
				else
				{
					if ((ChordModifiers & HeldModifiers) == 0)
					{
						return false;
					}
					InOutRequiredModifiers |= ChordModifiers;
					++InOutChordCount;
				}
			}
			else if (!Trigger->IsA<UInputTriggerDown>() && !Trigger->IsA<UInputTriggerPressed>())
			{
				return false;
			}
		}
		return true;
	}
}

APEPPlayerController::APEPPlayerController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	OverridePlayerInputClass = UEnhancedPlayerInput::StaticClass();
}

UEnhancedInputLocalPlayerSubsystem* APEPPlayerController::GetInputSubsystem() const
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	return LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
}

void APEPPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	UEnhancedInputComponent* EnhancedComponent = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedComponent)
	{
		return;
	}
	TSet<UInputAction*> BoundActions;
	const TArray<UInputAction*> StateActions = { ToggleMenuAction, OpenMenuAction, CloseMenuAction };
	for (UInputAction* Action : StateActions)
	{
		if (Action && !BoundActions.Contains(Action))
		{
			EnhancedComponent->BindAction(Action, ETriggerEvent::Started, this, &ThisClass::HandleMenuAction, Action);
			BoundActions.Add(Action);
		}
	}
	for (UInputAction* Action : MenuCommandActions)
	{
		if (Action && !BoundActions.Contains(Action))
		{
			EnhancedComponent->BindAction(Action, ETriggerEvent::Started, this, &ThisClass::HandleMenuCommand, Action);
			BoundActions.Add(Action);
		}
	}
}

void APEPPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		InitializePlayerHUD();
		ApplyGameplayInput();
		SynchronizePawn();
	}
}

void APEPPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreContexts(false);
	ReleaseMenuInputBlocks();
	if (IPEPPlayerHUDContract* HUD = GetPlayerHUDContract())
	{
		HUD->NotifyControlledPawnChanged(ObservedPawn.Get(), nullptr);
	}
	if (PlayerHUD)
	{
		PlayerHUD->RemoveFromParent();
		PlayerHUD = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void APEPPlayerController::SetPawn(APawn* InPawn)
{
	if (InPawn != GetPawn())
	{
		CloseMenu();
	}
	Super::SetPawn(InPawn);
	SynchronizePawn();
}

void APEPPlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();
	SynchronizePawn();
}

void APEPPlayerController::SynchronizePawn()
{
	if (!IsLocalController())
	{
		return;
	}
	if (IsMenuOpen() && (!IsValid(GetPawn()) || GetPawn() != MenuPawn.Get()))
	{
		CloseMenu();
	}
	if (ObservedPawn.Get() == GetPawn() && !ObservedPawn.IsStale())
	{
		return;
	}
	APawn* PreviousPawn = ObservedPawn.Get();
	ObservedPawn = GetPawn();
	if (IPEPPlayerHUDContract* HUD = GetPlayerHUDContract())
	{
		HUD->NotifyControlledPawnChanged(PreviousPawn, GetPawn());
	}
}

IPEPPlayerHUDContract* APEPPlayerController::GetPlayerHUDContract() const
{
	return PlayerHUD ? Cast<IPEPPlayerHUDContract>(PlayerHUD.Get()) : nullptr;
}

bool APEPPlayerController::InitializePlayerHUD()
{
	if (!IsLocalController())
	{
		return false;
	}
	if (PlayerHUD)
	{
		return true;
	}
	if (!PlayerHUDClass)
	{
		return false;
	}
	UUserWidget* CreatedHUD = CreateWidget<UUserWidget>(this, PlayerHUDClass);
	if (!CreatedHUD || !Cast<IPEPPlayerHUDContract>(CreatedHUD))
	{
		return false;
	}
	PlayerHUD = CreatedHUD;
	PlayerHUD->SetIsFocusable(true);
	if (!PlayerHUD->AddToPlayerScreen(PlayerHUDZOrder))
	{
		PlayerHUD = nullptr;
		return false;
	}
	if (IPEPPlayerHUDContract* HUD = GetPlayerHUDContract())
	{
		HUD->ShowMenu(false);
	}
	ObservedPawn = GetPawn();
	if (IPEPPlayerHUDContract* HUD = GetPlayerHUDContract())
	{
		HUD->NotifyControlledPawnChanged(nullptr, GetPawn());
	}
	return true;
}

bool APEPPlayerController::CanOpenMenu() const
{
	const IPEPPlayerHUDContract* HUD = GetPlayerHUDContract();
	return IsLocalController() && IsValid(GetPawn()) && HUD && HUD->CanShowMenu()
		&& MenuMappingContext
		&& !GameplayMappingContexts.Contains(MenuMappingContext) && GetInputSubsystem()
		&& Cast<UEnhancedInputComponent>(InputComponent) && !bTransitionInProgress;
}

UUserWidget* APEPPlayerController::GetPlayerMenu() const
{
	const IPEPPlayerHUDContract* HUD = GetPlayerHUDContract();
	return HUD ? HUD->GetMenuWidget() : nullptr;
}

void APEPPlayerController::ToggleMenu()
{
	if (IsMenuOpen())
	{
		CloseMenu();
	}
	else
	{
		OpenMenu();
	}
}

void APEPPlayerController::HandleMenuAction(UInputAction* Action)
{
	if (Action == ToggleMenuAction)
	{
		ToggleMenu();
	}
	else if (Action == CloseMenuAction)
	{
		CloseMenu();
	}
	else if (Action == OpenMenuAction)
	{
		OpenMenu();
	}
}

void APEPPlayerController::HandleMenuCommand(UInputAction* Action)
{
	if (IsMenuOpen() && !bTransitionInProgress)
	{
		OnMenuCommand.Broadcast(Action);
	}
}

UInputAction* APEPPlayerController::FindMenuShortcutAction(const UInputAction* Action, int32& OutRank) const
{
	if (!Action)
	{
		return nullptr;
	}
	constexpr int32 StateActionCount = 3;
	UInputAction* const StateActions[StateActionCount] = { CloseMenuAction.Get(), ToggleMenuAction.Get(), OpenMenuAction.Get() };
	for (int32 Index = 0; Index < StateActionCount; ++Index)
	{
		if (StateActions[Index] == Action)
		{
			OutRank = Index;
			return StateActions[Index];
		}
	}
	for (int32 Index = 0; Index < MenuCommandActions.Num(); ++Index)
	{
		if (MenuCommandActions[Index].Get() == Action)
		{
			OutRank = StateActionCount + Index;
			return MenuCommandActions[Index].Get();
		}
	}
	return nullptr;
}

EPEPMenuShortcutMatch APEPPlayerController::ResolveMenuShortcut(const FKey& Key, const FModifierKeysState& ModifierKeys,
	UInputAction*& OutAction) const
{
	OutAction = nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetInputSubsystem();
	UEnhancedPlayerInput* EnhancedInput = Subsystem ? Subsystem->GetPlayerInput() : nullptr;
	if (!IsMenuOpen() || !EnhancedInput || !Key.IsValid())
	{
		return EPEPMenuShortcutMatch::None;
	}
	// One allocation-free pass over the live mappings: the deferred menu context rebuild and player remaps are always
	// reflected, so no cached key table can go stale.
	const TConstArrayView<FEnhancedActionKeyMapping> Mappings = EnhancedInput->GetEnhancedActionMappingsView();
	// A modifier key pressed on its own is the shortcut, not a modifier of it.
	const int32 HeldModifiers = GetHeldShortcutModifiers(ModifierKeys) & ~GetShortcutModifier(Key);
	bool bSuppressed = false;
	int32 BestChordCount = INDEX_NONE;
	int32 BestRank = MAX_int32;
	for (const FEnhancedActionKeyMapping& Mapping : Mappings)
	{
		if (Mapping.Key != Key)
		{
			continue;
		}
		int32 Rank = INDEX_NONE;
		UInputAction* Action = FindMenuShortcutAction(Mapping.Action.Get(), Rank);
		if (!Action)
		{
			continue;
		}
		int32 RequiredModifiers = 0;
		int32 ChordCount = 0;
		if (!EvaluateShortcutTriggers(Action->Triggers, Mappings, HeldModifiers, RequiredModifiers, ChordCount)
			|| !EvaluateShortcutTriggers(Mapping.Triggers, Mappings, HeldModifiers, RequiredModifiers, ChordCount))
		{
			continue;
		}
		if ((HeldModifiers & ~RequiredModifiers & ShortcutExclusiveModifiers) != 0)
		{
			bSuppressed = true;
			continue;
		}
		// The most specific chord wins (Shift+Tab over Tab), then the configured precedence.
		if (ChordCount > BestChordCount || (ChordCount == BestChordCount && Rank < BestRank))
		{
			OutAction = Action;
			BestChordCount = ChordCount;
			BestRank = Rank;
		}
	}
	if (OutAction)
	{
		return EPEPMenuShortcutMatch::Matched;
	}
	return bSuppressed ? EPEPMenuShortcutMatch::Suppressed : EPEPMenuShortcutMatch::None;
}

bool APEPPlayerController::RouteMenuShortcut(const FKey& Key, const bool bRepeat, const FModifierKeysState& ModifierKeys)
{
	UInputAction* Action = nullptr;
	if (ResolveMenuShortcut(Key, ModifierKeys, Action) != EPEPMenuShortcutMatch::Matched)
	{
		return false;
	}
	if (!bRepeat)
	{
		if (Action == CloseMenuAction || Action == ToggleMenuAction || Action == OpenMenuAction)
		{
			HandleMenuAction(Action);
		}
		else
		{
			HandleMenuCommand(Action);
		}
	}
	return true;
}

bool APEPPlayerController::OpenMenu()
{
	if (IsMenuOpen())
	{
		return true;
	}
	if (bTransitionInProgress || !InitializePlayerHUD() || !CanOpenMenu())
	{
		return false;
	}
	{
		TGuardValue<bool> Guard(bTransitionInProgress, true);
		UEnhancedInputLocalPlayerSubsystem* Subsystem = GetInputSubsystem();
		InputState = EPEPPlayerInputState::Menu;
		MenuPawn = GetPawn();
		FModifyContextOptions Options;
		// Batch changes at the normal engine rebuild point, outside input delegate iteration.
		for (UInputMappingContext* Context : GameplayMappingContexts)
		{
			int32 Priority = 0;
			if (Context && !SuspendedContexts.Contains(Context) && Subsystem->HasMappingContext(Context, Priority))
			{
				SuspendedContexts.Add(Context, Priority);
				Subsystem->RemoveMappingContext(Context, Options);
			}
		}
		bMenuContextAdded = !Subsystem->HasMappingContext(MenuMappingContext);
		if (bMenuContextAdded)
		{
			Subsystem->AddMappingContext(MenuMappingContext, MenuMappingPriority, Options);
		}
		SetIgnoreMoveInput(true);
		SetIgnoreLookInput(true);
		bMenuInputBlocksApplied = true;
		IPEPPlayerHUDContract* HUD = GetPlayerHUDContract();
		UWidget* FocusWidget = HUD ? HUD->ShowMenu(true) : nullptr;
		bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(FocusWidget ? FocusWidget->TakeWidget() : PlayerHUD->TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
	}
	if (!IsValid(GetPawn()) || GetPawn() != MenuPawn.Get())
	{
		CloseMenu();
		return false;
	}
	OnPlayerInputStateChanged.Broadcast(InputState);
	return true;
}

void APEPPlayerController::RestoreContexts(const bool bRestoreGameplay)
{
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = GetInputSubsystem())
	{
		FModifyContextOptions Options;
		Options.bIgnoreAllPressedKeysUntilRelease = true;
		if (bMenuContextAdded)
		{
			Subsystem->RemoveMappingContext(MenuMappingContext, Options);
		}
		if (bRestoreGameplay)
		{
			for (const auto& Context : SuspendedContexts)
			{
				if (!Subsystem->HasMappingContext(Context.Key))
				{
					Subsystem->AddMappingContext(Context.Key, Context.Value, Options);
				}
			}
		}
	}
	SuspendedContexts.Reset();
	bMenuContextAdded = false;
}

void APEPPlayerController::CloseMenu()
{
	if (!IsLocalController() || !IsMenuOpen() || bTransitionInProgress)
	{
		return;
	}
	TGuardValue<bool> Guard(bTransitionInProgress, true);
	CancelPlayerDragDrop();
	InputState = EPEPPlayerInputState::Gameplay;
	if (IPEPPlayerHUDContract* HUD = GetPlayerHUDContract())
	{
		HUD->ShowMenu(false);
	}
	RestoreContexts(IsValid(GetPawn()) && GetPawn() == MenuPawn.Get());
	MenuPawn.Reset();
	ReleaseMenuInputBlocks();
	ApplyGameplayInput();
	OnPlayerInputStateChanged.Broadcast(InputState);
}

void APEPPlayerController::ApplyGameplayInput()
{
	// SetInputMode routes focus through this local player's Slate operations, so other split-screen players keep
	// their focus. Mouse lock, capture and cursor-hiding settings belong to the shared game viewport client.
	bShowMouseCursor = bShowMouseCursorInGameplay;
	if (GameplayInputMode == EPEPGameplayInputMode::GameAndUI)
	{
		FInputModeGameAndUI Mode;
		// Without an explicit target Game And UI keeps the current focus, which may still be the closed menu.
		if (UGameViewportClient* GameViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			Mode.SetWidgetToFocus(GameViewportClient->GetGameViewportWidget());
		}
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(!bShowMouseCursorInGameplay);
		SetInputMode(Mode);
	}
	else
	{
		FInputModeGameOnly Mode;
		Mode.SetConsumeCaptureMouseDown(true);
		SetInputMode(Mode);
	}
}

void APEPPlayerController::CancelPlayerDragDrop()
{
	// FSlateApplication::CancelDragDrop (UWidgetBlueprintLibrary::CancelDragDrop) cancels every Slate user's drag;
	// only this player's drag belongs to its menu.
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (!LocalPlayer || !FSlateApplication::IsInitialized())
	{
		return;
	}
	if (const TSharedPtr<FSlateUser> SlateUser = LocalPlayer->GetSlateUser())
	{
		SlateUser->CancelDragDrop();
	}
}

void APEPPlayerController::ReleaseMenuInputBlocks()
{
	if (bMenuInputBlocksApplied)
	{
		SetIgnoreMoveInput(false);
		SetIgnoreLookInput(false);
		bMenuInputBlocksApplied = false;
	}
}
