#include "Camera/Modes/PEPCameraMode_Strategy.h"

#include "CollisionQueryParams.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"

UPEPCameraMode_Strategy::UPEPCameraMode_Strategy()
{
	FieldOfView = 60.0f;
	BlendTime = 0.5f;
}

void UPEPCameraMode_Strategy::AddPanInput_Implementation(const FVector2D Value)
{
	PendingPanInput += Value;
}

void UPEPCameraMode_Strategy::AddZoomInput_Implementation(const float Value)
{
	const float Current = GetTargetDistance();
	SetTargetDistance(Current * FMath::Pow(1.0f - ZoomStepFraction, Value));
}

void UPEPCameraMode_Strategy::AddOrbitInput_Implementation(const FVector2D Value)
{
	if (bAllowOrbit)
	{
		SetYaw(GetYaw() + Value.X * OrbitSpeed);
	}
}

void UPEPCameraMode_Strategy::ResetInterpolation_Implementation()
{
	if (TargetDistance >= 0.0f)
	{
		CurrentDistance = TargetDistance;
	}
	PendingPanInput = FVector2D::ZeroVector;
}

void UPEPCameraMode_Strategy::SetFocusLocation(const FVector NewFocusLocation)
{
	FocusLocation = ConstrainFocusLocation(NewFocusLocation);
	bFocusInitialized = true;
}

FVector UPEPCameraMode_Strategy::ConstrainFocusLocation(FVector Location) const
{
	if (bConstrainFocus && FocusBounds.bIsValid)
	{
		Location.X = FMath::Clamp(Location.X, FocusBounds.Min.X, FocusBounds.Max.X);
		Location.Y = FMath::Clamp(Location.Y, FocusBounds.Min.Y, FocusBounds.Max.Y);
	}
	return Location;
}

void UPEPCameraMode_Strategy::SetYaw(const float NewYaw)
{
	Yaw = FRotator::NormalizeAxis(NewYaw);
	bYawInitialized = true;
}

float UPEPCameraMode_Strategy::GetYaw() const
{
	return bYawInitialized ? Yaw : DefaultYaw;
}

void UPEPCameraMode_Strategy::SetTargetDistance(const float NewDistance, const bool bSnap)
{
	TargetDistance = ClampDistance(NewDistance);
	if (bSnap || CurrentDistance < 0.0f)
	{
		CurrentDistance = TargetDistance;
	}
}

float UPEPCameraMode_Strategy::GetTargetDistance() const
{
	return TargetDistance >= 0.0f ? TargetDistance : ClampDistance(DefaultDistance);
}

float UPEPCameraMode_Strategy::GetCurrentDistance() const
{
	return CurrentDistance >= 0.0f ? CurrentDistance : GetTargetDistance();
}

float UPEPCameraMode_Strategy::ClampDistance(const float Distance) const
{
	return FMath::Clamp(Distance, MinDistance, FMath::Max(MinDistance, MaxDistance));
}

void UPEPCameraMode_Strategy::EnsureInitialized(const FPEPCameraModeContext& Context)
{
	if (!bFocusInitialized)
	{
		SetFocusLocation(Context.PivotLocation);
	}
	if (!bYawInitialized)
	{
		SetYaw(bInheritControlYaw ? Context.ControlRotation.Yaw : DefaultYaw);
	}
	if (TargetDistance < 0.0f)
	{
		SetTargetDistance(DefaultDistance, true);
	}
}

bool UPEPCameraMode_Strategy::CanEdgeScroll(const FPEPCameraModeContext& Context) const
{
	const APlayerController* PlayerController = Context.PlayerController;
	if (!bEdgeScrolling || !PlayerController || !PlayerController->IsLocalController()
		|| !PlayerController->ShouldShowMouseCursor())
	{
		return false;
	}

	/* A window in the background keeps reporting the last cursor position, often on an edge. */
	const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	const UGameViewportClient* ViewportClient = LocalPlayer ? LocalPlayer->ViewportClient.Get() : nullptr;
	return ViewportClient && ViewportClient->Viewport && ViewportClient->Viewport->HasFocus();
}

FVector2D UPEPCameraMode_Strategy::ComputeEdgeScrollInput(const FPEPCameraModeContext& Context) const
{
	if (!CanEdgeScroll(Context))
	{
		return FVector2D::ZeroVector;
	}
	const APlayerController* PlayerController = Context.PlayerController;

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	int32 SizeX = 0;
	int32 SizeY = 0;
	PlayerController->GetViewportSize(SizeX, SizeY);
	if (SizeX <= 0 || SizeY <= 0 || !PlayerController->GetMousePosition(MouseX, MouseY))
	{
		return FVector2D::ZeroVector;
	}

	const float MarginX = SizeX * EdgeScrollMargin;
	const float MarginY = SizeY * EdgeScrollMargin;
	FVector2D Input = FVector2D::ZeroVector;
	if (MouseX <= MarginX)
	{
		Input.X = -1.0f;
	}
	else if (MouseX >= SizeX - MarginX)
	{
		Input.X = 1.0f;
	}
	/* Screen Y grows downwards; the top edge pans forward. */
	if (MouseY <= MarginY)
	{
		Input.Y = 1.0f;
	}
	else if (MouseY >= SizeY - MarginY)
	{
		Input.Y = -1.0f;
	}
	return Input;
}

void UPEPCameraMode_Strategy::FollowTerrain(const FPEPCameraModeContext& Context)
{
	const UWorld* World = GetWorld();
	if (!bFollowTerrain || !World)
	{
		return;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(PEPStrategyCameraTerrain), false);
	if (Context.ViewTarget)
	{
		Params.AddIgnoredActor(Context.ViewTarget.Get());
	}

	const FVector Up(0.0f, 0.0f, TerrainProbeHalfHeight);
	FHitResult Hit;
	if (World->LineTraceSingleByObjectType(
		Hit,
		FocusLocation + Up,
		FocusLocation - Up,
		FCollisionObjectQueryParams(TerrainObjectType),
		Params))
	{
		FocusLocation.Z = TerrainInterpSpeed > 0.0f
			? FMath::FInterpTo(FocusLocation.Z, Hit.ImpactPoint.Z, Context.DeltaTime, TerrainInterpSpeed)
			: Hit.ImpactPoint.Z;
	}
}

FPEPCameraView UPEPCameraMode_Strategy::ComputeView(const FPEPCameraModeContext& Context)
{
	EnsureInitialized(Context);
	const float DeltaTime = Context.DeltaTime;

	CurrentDistance = ZoomInterpSpeed > 0.0f
		? FMath::FInterpTo(GetCurrentDistance(), TargetDistance, DeltaTime, ZoomInterpSpeed)
		: TargetDistance;

	if (FocusMode == EPEPStrategyFocusMode::FollowViewTarget)
	{
		FocusLocation = ConstrainFocusLocation(
			(Context.ViewTarget ? Context.ViewTarget->GetActorLocation() : Context.PivotLocation) + FollowOffset);
	}
	else
	{
		FVector2D PanInput = PendingPanInput + ComputeEdgeScrollInput(Context);
		if (PanInput.SizeSquared() > 1.0f)
		{
			PanInput.Normalize();
		}

		const float DistanceScale = bScalePanWithDistance && DefaultDistance > KINDA_SMALL_NUMBER
			? CurrentDistance / DefaultDistance
			: 1.0f;
		const FVector Forward = FRotator(0.0f, Yaw, 0.0f).Vector();
		const FVector Right = FRotator(0.0f, Yaw + 90.0f, 0.0f).Vector();
		FocusLocation += (Forward * PanInput.Y + Right * PanInput.X) * PanSpeed * DistanceScale * DeltaTime;

		FocusLocation = ConstrainFocusLocation(FocusLocation);
		FollowTerrain(Context);
	}
	/* Input is per frame: what was not consumed now must not move the camera later. */
	PendingPanInput = FVector2D::ZeroVector;

	const float Range = MaxDistance - MinDistance;
	const float ZoomAlpha = Range > KINDA_SMALL_NUMBER
		? FMath::Clamp((CurrentDistance - MinDistance) / Range, 0.0f, 1.0f)
		: 0.0f;
	const FRotator Rotation(FMath::Lerp(PitchAtMinDistance, PitchAtMaxDistance, ZoomAlpha), Yaw, 0.0f);

	FPEPCameraView View;
	View.Rotation = Rotation;
	View.Location = FocusLocation - Rotation.Vector() * CurrentDistance;
	View.FieldOfView = FieldOfView;
	return View;
}
