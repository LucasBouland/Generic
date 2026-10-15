#include "Actors/PEPPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Widget.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Templates/UnrealTemplate.h"
#include "UI/PEPPlayerHUDContract.h"

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

bool APEPPlayerController::RouteMenuShortcut(const FKey Key, const bool bRepeat)
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetInputSubsystem();
	if (!IsMenuOpen() || !Subsystem)
	{
		return false;
	}
	const TArray<UInputAction*> StateActions = { CloseMenuAction, ToggleMenuAction, OpenMenuAction };
	for (UInputAction* Action : StateActions)
	{
		if (Action && Subsystem->QueryKeysMappedToAction(Action).Contains(Key))
		{
			if (!bRepeat)
			{
				HandleMenuAction(Action);
			}
			return true;
		}
	}
	for (UInputAction* Action : MenuCommandActions)
	{
		if (Action && Subsystem->QueryKeysMappedToAction(Action).Contains(Key))
		{
			if (!bRepeat)
			{
				HandleMenuCommand(Action);
			}
			return true;
		}
	}
	return false;
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
	UWidgetBlueprintLibrary::CancelDragDrop();
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
	bShowMouseCursor = false;
	FInputModeGameOnly Mode;
	Mode.SetConsumeCaptureMouseDown(true);
	SetInputMode(Mode);
	UWidgetBlueprintLibrary::SetFocusToGameViewport();
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
