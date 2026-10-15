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
	if (TimeSinceSync >= SyncInterval
		&& (!bHasSyncedFocus || FVector::DistSquared(Focus, LastSyncedFocus) >= FMath::Square(SyncDistance)))
	{
		Server_SetFocusLocation(Focus);
		LastSyncedFocus = Focus;
		bHasSyncedFocus = true;
		TimeSinceSync = 0.0f;
	}
}

void APEPStrategyCameraPawn::Server_SetFocusLocation_Implementation(const FVector_NetQuantize FocusLocation)
{
	/* A relevancy hint only: nothing gameplay-relevant may read this pawn's location as authority. */
	SetActorLocation(FocusLocation);
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
