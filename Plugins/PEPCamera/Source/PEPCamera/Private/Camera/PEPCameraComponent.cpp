#include "Camera/PEPCameraComponent.h"

#include "Camera/Modes/PEPCameraMode_ThirdPerson.h"
#include "Camera/PEPCameraInputConfig.h"
#include "Camera/PEPCameraMode.h"
#include "Camera/PEPCameraModeStack.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "PEPCameraLog.h"

UPEPCameraComponent::UPEPCameraComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	/* The modes decide where the camera is; the control rotation reaches them through the context. */
	bUsePawnControlRotation = false;
}

void UPEPCameraComponent::GetCameraView(const float DeltaTime, FMinimalViewInfo& DesiredView)
{
	const FPEPCameraModeContext Context = BuildModeContext(DeltaTime);
	UpdateCameraModes(Context);

	LastCameraView = GetOrCreateModeStack()->EvaluateStack(Context);

	/* Keep the component where the view is, so attached effects and audio follow the camera. */
	SetWorldLocationAndRotation(LastCameraView.Location, LastCameraView.Rotation);
	SetFieldOfView(LastCameraView.FieldOfView);

	DesiredView.Location = LastCameraView.Location;
	DesiredView.Rotation = LastCameraView.Rotation;
	DesiredView.FOV = LastCameraView.FieldOfView;
	DesiredView.OrthoWidth = OrthoWidth;
	DesiredView.OrthoNearClipPlane = OrthoNearClipPlane;
	DesiredView.OrthoFarClipPlane = OrthoFarClipPlane;
	DesiredView.AspectRatio = AspectRatio;
	DesiredView.bConstrainAspectRatio = bConstrainAspectRatio;
	DesiredView.bUseFieldOfViewForLOD = bUseFieldOfViewForLOD;
	DesiredView.ProjectionMode = ProjectionMode;

	DesiredView.PostProcessBlendWeight = PostProcessBlendWeight;
	if (PostProcessBlendWeight > 0.0f)
	{
		DesiredView.PostProcessSettings = PostProcessSettings;
	}
}

void UPEPCameraComponent::SetCameraMode(const TSubclassOf<UPEPCameraMode> ModeClass)
{
	RequestedCameraMode = ModeClass;
}

TSubclassOf<UPEPCameraMode> UPEPCameraComponent::GetCameraModeClass() const
{
	return DetermineCameraMode();
}

UPEPCameraMode* UPEPCameraComponent::GetCurrentCameraMode()
{
	return GetCameraModeInstance(DetermineCameraMode());
}

UPEPCameraMode* UPEPCameraComponent::GetCameraModeInstance(const TSubclassOf<UPEPCameraMode> ModeClass)
{
	if (!ModeClass || ModeClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return nullptr;
	}

	for (UPEPCameraMode* Instance : ModeInstances)
	{
		if (Instance && Instance->GetClass() == ModeClass)
		{
			return Instance;
		}
	}

	UPEPCameraMode* Instance = NewObject<UPEPCameraMode>(this, ModeClass);
	ModeInstances.Add(Instance);
	return Instance;
}

void UPEPCameraComponent::CycleCameraMode(const int32 Direction)
{
	TArray<TSubclassOf<UPEPCameraMode>> Modes;
	for (const TSubclassOf<UPEPCameraMode>& ModeClass : CycleCameraModes)
	{
		if (ModeClass)
		{
			Modes.Add(ModeClass);
		}
	}
	if (Modes.IsEmpty() || Direction == 0)
	{
		return;
	}

	/* From a mode outside the cycle, the first step lands on the first (or last) entry. */
	const int32 CurrentIndex = Modes.IndexOfByKey(DetermineCameraMode());
	int32 NextIndex = 0;
	if (CurrentIndex == INDEX_NONE)
	{
		NextIndex = Direction > 0 ? 0 : Modes.Num() - 1;
	}
	else
	{
		const int32 Step = Direction > 0 ? 1 : -1;
		NextIndex = (CurrentIndex + Step + Modes.Num()) % Modes.Num();
	}
	SetCameraMode(Modes[NextIndex]);
}

bool UPEPCameraComponent::IsBlendingCameraModes() const
{
	return ModeStack && ModeStack->IsBlending();
}

void UPEPCameraComponent::AddPanInput(const FVector2D Value)
{
	if (UPEPCameraMode* Mode = GetCurrentCameraMode())
	{
		Mode->AddPanInput(Value);
	}
}

void UPEPCameraComponent::AddZoomInput(const float Value)
{
	if (UPEPCameraMode* Mode = GetCurrentCameraMode())
	{
		Mode->AddZoomInput(Value);
	}
}

void UPEPCameraComponent::AddOrbitInput(const FVector2D Value)
{
	if (UPEPCameraMode* Mode = GetCurrentCameraMode())
	{
		Mode->AddOrbitInput(Value);
	}
}

void UPEPCameraComponent::SwapShoulder()
{
	if (UPEPCameraMode_ThirdPerson* ThirdPerson = Cast<UPEPCameraMode_ThirdPerson>(GetCurrentCameraMode()))
	{
		ThirdPerson->SwapShoulder();
	}
}

void UPEPCameraComponent::BindCameraInput(
	UEnhancedInputComponent* InputComponent,
	const UPEPCameraInputConfig* InputConfig)
{
	if (UEnhancedInputComponent* Previous = BoundInputComponent.Get())
	{
		for (const uint32 Handle : BoundInputHandles)
		{
			Previous->RemoveBindingByHandle(Handle);
		}
	}
	BoundInputHandles.Reset();
	BoundInputComponent.Reset();

	if (!InputComponent || !InputConfig)
	{
		return;
	}
	BoundInputComponent = InputComponent;

	if (InputConfig->PanAction)
	{
		BoundInputHandles.Add(InputComponent->BindAction(
			InputConfig->PanAction, ETriggerEvent::Triggered, this, &ThisClass::HandlePanAction).GetHandle());
	}
	if (InputConfig->ZoomAction)
	{
		BoundInputHandles.Add(InputComponent->BindAction(
			InputConfig->ZoomAction, ETriggerEvent::Triggered, this, &ThisClass::HandleZoomAction).GetHandle());
	}
	if (InputConfig->OrbitAction)
	{
		BoundInputHandles.Add(InputComponent->BindAction(
			InputConfig->OrbitAction, ETriggerEvent::Triggered, this, &ThisClass::HandleOrbitAction).GetHandle());
	}
	if (InputConfig->CycleModeAction)
	{
		BoundInputHandles.Add(InputComponent->BindAction(
			InputConfig->CycleModeAction, ETriggerEvent::Started, this, &ThisClass::HandleCycleModeAction).GetHandle());
	}
	if (InputConfig->SwapShoulderAction)
	{
		BoundInputHandles.Add(InputComponent->BindAction(
			InputConfig->SwapShoulderAction, ETriggerEvent::Started, this, &ThisClass::HandleSwapShoulderAction).GetHandle());
	}
}

TSubclassOf<UPEPCameraMode> UPEPCameraComponent::DetermineCameraMode() const
{
	return RequestedCameraMode ? RequestedCameraMode : DefaultCameraMode;
}

FPEPCameraModeContext UPEPCameraComponent::BuildModeContext(const float DeltaTime) const
{
	FPEPCameraModeContext Context;
	Context.DeltaTime = DeltaTime;

	AActor* Owner = GetOwner();
	Context.ViewTarget = Owner;
	if (!Owner)
	{
		return Context;
	}

	Context.PivotLocation = Owner->GetActorLocation();
	Context.PivotRotation = Owner->GetActorRotation();
	Context.ControlRotation = Context.PivotRotation;

	if (const APawn* Pawn = Cast<APawn>(Owner))
	{
		Context.PlayerController = Cast<APlayerController>(Pawn->GetController());
		Context.ControlRotation = Pawn->GetViewRotation();
	}
	return Context;
}

UPEPCameraModeStack* UPEPCameraComponent::GetOrCreateModeStack()
{
	if (!ModeStack)
	{
		ModeStack = NewObject<UPEPCameraModeStack>(this);
	}
	return ModeStack;
}

void UPEPCameraComponent::UpdateCameraModes(const FPEPCameraModeContext& Context)
{
	UPEPCameraMode* Mode = GetCurrentCameraMode();
	if (!Mode)
	{
		UE_CLOG(!DetermineCameraMode(), LogPEPCamera, Verbose,
			TEXT("%s has no camera mode; set DefaultCameraMode."), *GetPathName());
		return;
	}
	if (Mode == LastPushedMode)
	{
		return;
	}

	UPEPCameraMode* PreviousMode = LastPushedMode;
	LastPushedMode = Mode;
	GetOrCreateModeStack()->PushCameraMode(Mode, Context);
	OnCameraModeChanged.Broadcast(PreviousMode, Mode);
}

void UPEPCameraComponent::HandlePanAction(const FInputActionValue& Value)
{
	AddPanInput(Value.Get<FVector2D>());
}

void UPEPCameraComponent::HandleZoomAction(const FInputActionValue& Value)
{
	AddZoomInput(Value.Get<float>());
}

void UPEPCameraComponent::HandleOrbitAction(const FInputActionValue& Value)
{
	AddOrbitInput(Value.Get<FVector2D>());
}

void UPEPCameraComponent::HandleCycleModeAction(const FInputActionValue& Value)
{
	CycleCameraMode(1);
}

void UPEPCameraComponent::HandleSwapShoulderAction(const FInputActionValue& Value)
{
	SwapShoulder();
}
