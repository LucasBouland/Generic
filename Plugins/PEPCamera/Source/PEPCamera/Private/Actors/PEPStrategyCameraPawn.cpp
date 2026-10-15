#include "Actors/PEPStrategyCameraPawn.h"

#include "Camera/Modes/PEPCameraMode_Strategy.h"
#include "Camera/PEPCameraComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"

APEPStrategyCameraPawn::APEPStrategyCameraPawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	/* Only the locally controlling machine has a focus to follow; see UpdateTickEnabled. */
	PrimaryActorTick.bStartWithTickEnabled = false;

	/* The owning client moves this pawn; replicated movement would drag it back to the server's hint. */
	SetReplicatingMovement(false);
	SetCanBeDamaged(false);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	CameraComponent = CreateDefaultSubobject<UPEPCameraComponent>(TEXT("Camera"));
	CameraComponent->SetupAttachment(Root);
	CameraComponent->DefaultCameraMode = UPEPCameraMode_Strategy::StaticClass();
}

void APEPStrategyCameraPawn::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsLocallyControlled() || !CameraComponent)
	{
		return;
	}

	const UPEPCameraMode_Strategy* Strategy =
		Cast<UPEPCameraMode_Strategy>(CameraComponent->GetCurrentCameraMode());
	if (!Strategy || !Strategy->HasFocus())
	{
		return;
	}

	const FVector Focus = Strategy->GetFocusLocation();
	SetActorLocation(Focus);

	if (HasAuthority() || SyncInterval <= 0.0f)
	{
		return;
	}
	TimeSinceSync += DeltaSeconds;
	if (TimeSinceSync < SyncInterval)
	{
		return;
	}

	const bool bMovedEnough = !bHasSyncedFocus
		|| FVector::DistSquared(Focus, LastSyncedFocus) >= FMath::Square(SyncDistance);
	if (bMovedEnough)
	{
		Server_SetFocusLocation(Focus);
		LastSyncedFocus = Focus;
		bHasSyncedFocus = true;
		bSettledFocusSent = false;
		TimeSinceSync = 0.0f;
		return;
	}

	/*
	 * Moving updates are unreliable and the last one may be lost; small moves below SyncDistance are
	 * never sent on their own. Once the camera has stayed within SyncDistance for the keep-alive
	 * interval, its current focus is sent once, reliably, so the server always ends up exact.
	 */
	if (!bSettledFocusSent && SyncKeepAliveInterval > 0.0f && TimeSinceSync >= SyncKeepAliveInterval)
	{
		Server_SetSettledFocusLocation(Focus);
		LastSyncedFocus = Focus;
		bSettledFocusSent = true;
		TimeSinceSync = 0.0f;
	}
}

void APEPStrategyCameraPawn::Server_SetSettledFocusLocation_Implementation(const FVector_NetQuantize FocusLocation)
{
	Server_SetFocusLocation_Implementation(FocusLocation);
}

void APEPStrategyCameraPawn::Server_SetFocusLocation_Implementation(const FVector_NetQuantize FocusLocation)
{
	/*
	 * A hint only: nothing gameplay-relevant may read this pawn's location as authority. Still kept
	 * inside the strategy mode's focus bounds, so a modified client cannot park it anywhere.
	 */
	FVector Location = FocusLocation;
	if (const UPEPCameraMode_Strategy* Strategy = CameraComponent
		? CameraComponent->GetCameraModeInstance<UPEPCameraMode_Strategy>()
		: nullptr)
	{
		Location = Strategy->ConstrainFocusLocation(Location);
	}
	SetActorLocation(Location);
}

void APEPStrategyCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (CameraComponent)
	{
		CameraComponent->BindCameraInput(Cast<UEnhancedInputComponent>(PlayerInputComponent), InputConfig);
	}
}

void APEPStrategyCameraPawn::NotifyControllerChanged()
{
	RemoveMappingContext(PreviousController);
	Super::NotifyControllerChanged();
	AddMappingContext(Controller);
	UpdateTickEnabled();
}

void APEPStrategyCameraPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	/* Destroyed while still possessed (no unpossess first): the local player keeps no stale context. */
	RemoveMappingContext(Controller);
	Super::EndPlay(EndPlayReason);
}

void APEPStrategyCameraPawn::UpdateTickEnabled()
{
	const bool bLocal = IsLocallyControlled();
	SetActorTickEnabled(bLocal);
	if (!bLocal)
	{
		bHasSyncedFocus = false;
		bSettledFocusSent = false;
		TimeSinceSync = 0.0f;
	}
}

void APEPStrategyCameraPawn::AddMappingContext(AController* ForController)
{
	const APlayerController* PlayerController = Cast<APlayerController>(ForController);
	if (!InputMappingContext || !PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(InputMappingContext, InputMappingPriority);
	}
}

void APEPStrategyCameraPawn::RemoveMappingContext(AController* ForController)
{
	const APlayerController* PlayerController = Cast<APlayerController>(ForController);
	if (!InputMappingContext || !PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
	{
		Subsystem->RemoveMappingContext(InputMappingContext);
	}
}
