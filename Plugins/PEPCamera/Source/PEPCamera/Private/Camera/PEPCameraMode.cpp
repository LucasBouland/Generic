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
	FPEPCameraView View = UsesBlueprintComputeView() ? K2_ComputeView(Context) : ComputeView(Context);
	View.FieldOfView = FMath::Clamp(View.FieldOfView, MinFieldOfView, MaxFieldOfView);
	return View;
}

bool UPEPCameraMode::UsesBlueprintComputeView()
{
	if (!bBlueprintComputeView.IsSet())
	{
		bBlueprintComputeView = GetClass()->IsFunctionImplementedInScript(
			GET_FUNCTION_NAME_CHECKED(UPEPCameraMode, K2_ComputeView));
	}
	return bBlueprintComputeView.GetValue();
}

FPEPCameraView UPEPCameraMode::K2_GetNativeView(const FPEPCameraModeContext& Context)
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
	const float W = FMath::Clamp(Weight, 0.0f, 1.0f);
	switch (BlendFunction)
	{
	case EPEPCameraBlendFunction::EaseIn:
		return FMath::InterpEaseIn(0.0f, 1.0f, W, BlendExponent);
	case EPEPCameraBlendFunction::EaseOut:
		return FMath::InterpEaseOut(0.0f, 1.0f, W, BlendExponent);
	case EPEPCameraBlendFunction::EaseInOut:
		return FMath::InterpEaseInOut(0.0f, 1.0f, W, BlendExponent);
	case EPEPCameraBlendFunction::Linear:
	default:
		return W;
	}
}

float UPEPCameraMode::InverseBlendAlpha(const float Alpha) const
{
	/*
	 * Closed-form inverses of the engine ease curves (FMath::InterpEase*, exponent E):
	 *   EaseIn    a = w^E                          ->  w = a^(1/E)
	 *   EaseOut   a = 1 - (1 - w)^E                ->  w = 1 - (1 - a)^(1/E)
	 *   EaseInOut a = EaseIn(2w)/2 on the first half, 1/2 + EaseOut(2w - 1)/2 on the second.
	 */
	const float A = FMath::Clamp(Alpha, 0.0f, 1.0f);
	const float InvExponent = 1.0f / FMath::Max(BlendExponent, KINDA_SMALL_NUMBER);
	switch (BlendFunction)
	{
	case EPEPCameraBlendFunction::EaseIn:
		return FMath::Pow(A, InvExponent);
	case EPEPCameraBlendFunction::EaseOut:
		return 1.0f - FMath::Pow(1.0f - A, InvExponent);
	case EPEPCameraBlendFunction::EaseInOut:
		return A < 0.5f
			? 0.5f * FMath::Pow(2.0f * A, InvExponent)
			: 1.0f - 0.5f * FMath::Pow(2.0f - 2.0f * A, InvExponent);
	case EPEPCameraBlendFunction::Linear:
	default:
		return A;
	}
}

UPEPCameraComponent* UPEPCameraMode::GetCameraComponent() const
{
	return Cast<UPEPCameraComponent>(GetOuter());
}

void UPEPCameraMode::ResetInterpolation_Implementation()
{
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

FPEPCameraView UPEPCameraMode::ComputeView(const FPEPCameraModeContext& Context)
{
	FPEPCameraView View;
	View.Location = Context.PivotLocation;
	View.Rotation = Context.ControlRotation;
	View.FieldOfView = FieldOfView;
	return View;
}
