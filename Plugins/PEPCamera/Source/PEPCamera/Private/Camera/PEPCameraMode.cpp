#include "Camera/PEPCameraMode.h"

#include "Camera/PEPCameraComponent.h"

UWorld* UPEPCameraMode::GetWorld() const
{
	/* The CDO has no world; asking the outer of a CDO would break Blueprint function availability. */
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return nullptr;
	}
	return GetOuter() ? GetOuter()->GetWorld() : nullptr;
}

FPEPCameraView UPEPCameraMode::UpdateView(const FPEPCameraModeContext& Context)
{
	return ComputeView(Context);
}

void UPEPCameraMode::NotifyActivated(const FPEPCameraModeContext& Context)
{
	OnActivated(Context);
	ReceiveActivated(Context);
}

void UPEPCameraMode::NotifyDeactivated()
{
	OnDeactivated();
	ReceiveDeactivated();
}

float UPEPCameraMode::EvaluateBlendAlpha(const float Weight) const
{
	const float ClampedWeight = FMath::Clamp(Weight, 0.0f, 1.0f);
	switch (BlendFunction)
	{
	case EPEPCameraBlendFunction::EaseIn:
		return FMath::InterpEaseIn(0.0f, 1.0f, ClampedWeight, BlendExponent);
	case EPEPCameraBlendFunction::EaseOut:
		return FMath::InterpEaseOut(0.0f, 1.0f, ClampedWeight, BlendExponent);
	case EPEPCameraBlendFunction::EaseInOut:
		return FMath::InterpEaseInOut(0.0f, 1.0f, ClampedWeight, BlendExponent);
	case EPEPCameraBlendFunction::Linear:
	default:
		return ClampedWeight;
	}
}

UPEPCameraComponent* UPEPCameraMode::GetCameraComponent() const
{
	return Cast<UPEPCameraComponent>(GetOuter());
}

void UPEPCameraMode::AddPanInput_Implementation(const FVector2D Value)
{
}

void UPEPCameraMode::AddZoomInput_Implementation(const float Value)
{
}

void UPEPCameraMode::AddOrbitInput_Implementation(const FVector2D Value)
{
}

FPEPCameraView UPEPCameraMode::ComputeView_Implementation(const FPEPCameraModeContext& Context)
{
	FPEPCameraView View;
	View.Location = Context.PivotLocation;
	View.Rotation = Context.ControlRotation;
	View.FieldOfView = FieldOfView;
	return View;
}
