#include "Camera/Modes/PEPCameraMode_ThirdPerson.h"

#include "CollisionQueryParams.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	/* Interpolates towards Target, or jumps there when Speed is 0. */
	float InterpOrSnap(const float Current, const float Target, const float DeltaTime, const float Speed)
	{
		return Speed > 0.0f ? FMath::FInterpTo(Current, Target, DeltaTime, Speed) : Target;
	}
}

void UPEPCameraMode_ThirdPerson::SwapShoulder()
{
	bRightShoulder = !bRightShoulder;
}

void UPEPCameraMode_ThirdPerson::SetRightShoulder(const bool bRight)
{
	bRightShoulder = bRight;
}

void UPEPCameraMode_ThirdPerson::SetTargetArmLength(const float NewArmLength, const bool bSnap)
{
	EnsureArmInitialized();
	TargetArmLength = FMath::Clamp(NewArmLength, MinArmLength, FMath::Max(MinArmLength, MaxArmLength));
	if (bSnap)
	{
		CurrentArmLength = TargetArmLength;
	}
}

float UPEPCameraMode_ThirdPerson::GetTargetArmLength() const
{
	return TargetArmLength >= 0.0f ? TargetArmLength : ArmLength;
}

void UPEPCameraMode_ThirdPerson::AddZoomInput_Implementation(const float Value)
{
	EnsureArmInitialized();
	SetTargetArmLength(TargetArmLength - Value * ZoomStep);
}

void UPEPCameraMode_ThirdPerson::OnActivated(const FPEPCameraModeContext& Context)
{
	/* Lag and obstruction state from a previous use would drag the camera from a stale place. */
	bHasSmoothedPivot = false;
	CurrentCollisionLength = -1.0f;
}

void UPEPCameraMode_ThirdPerson::EnsureArmInitialized()
{
	if (TargetArmLength < 0.0f)
	{
		TargetArmLength = FMath::Clamp(ArmLength, MinArmLength, FMath::Max(MinArmLength, MaxArmLength));
		CurrentArmLength = TargetArmLength;
	}
}

FVector UPEPCameraMode_ThirdPerson::ResolvePivotLocation(const FPEPCameraModeContext& Context) const
{
	if (!PivotSocketName.IsNone() && Context.ViewTarget)
	{
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Context.ViewTarget.Get());
		for (const USkeletalMeshComponent* Mesh : Meshes)
		{
			if (Mesh && Mesh->DoesSocketExist(PivotSocketName))
			{
				return Mesh->GetSocketLocation(PivotSocketName);
			}
		}
	}
	return Context.PivotLocation;
}

float UPEPCameraMode_ThirdPerson::ProbeBoomLength(
	const FPEPCameraModeContext& Context,
	const FVector& Pivot,
	const FVector& DesiredLocation) const
{
	const float FullLength = FVector::Dist(Pivot, DesiredLocation);
	const UWorld* World = GetWorld();
	if (!bDoCollisionTest || !World || FullLength <= KINDA_SMALL_NUMBER)
	{
		return FullLength;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(PEPThirdPersonCameraProbe), false);
	if (Context.ViewTarget)
	{
		Params.AddIgnoredActor(Context.ViewTarget.Get());
	}

	FHitResult Hit;
	if (World->SweepSingleByChannel(
		Hit,
		Pivot,
		DesiredLocation,
		FQuat::Identity,
		ProbeChannel,
		FCollisionShape::MakeSphere(ProbeSize),
		Params))
	{
		return FVector::Dist(Pivot, Hit.Location);
	}
	return FullLength;
}

FPEPCameraView UPEPCameraMode_ThirdPerson::ComputeView_Implementation(const FPEPCameraModeContext& Context)
{
	EnsureArmInitialized();
	const float DeltaTime = Context.DeltaTime;

	const FVector Pivot = ResolvePivotLocation(Context) + PivotOffset;
	if (PivotLagSpeed > 0.0f && bHasSmoothedPivot)
	{
		SmoothedPivot = FMath::VInterpTo(SmoothedPivot, Pivot, DeltaTime, PivotLagSpeed);
	}
	else
	{
		SmoothedPivot = Pivot;
	}
	bHasSmoothedPivot = true;

	CurrentArmLength = InterpOrSnap(CurrentArmLength, TargetArmLength, DeltaTime, ZoomInterpSpeed);

	const float TargetShoulderSign = bRightShoulder ? 1.0f : -1.0f;
	CurrentShoulderSign = bShoulderInitialized
		? InterpOrSnap(CurrentShoulderSign, TargetShoulderSign, DeltaTime, ShoulderInterpSpeed)
		: TargetShoulderSign;
	bShoulderInitialized = true;

	const FRotator Rotation = Context.ControlRotation;
	const FVector Shoulder(ShoulderOffset.X, ShoulderOffset.Y * CurrentShoulderSign, ShoulderOffset.Z);
	const FVector DesiredLocation =
		SmoothedPivot - Rotation.Vector() * CurrentArmLength + Rotation.RotateVector(Shoulder);

	/* Obstructions pull the camera in at once; clearing them lets it glide back out. */
	const float FreeLength = ProbeBoomLength(Context, SmoothedPivot, DesiredLocation);
	if (CurrentCollisionLength < 0.0f || FreeLength < CurrentCollisionLength)
	{
		CurrentCollisionLength = FreeLength;
	}
	else
	{
		CurrentCollisionLength = InterpOrSnap(CurrentCollisionLength, FreeLength, DeltaTime, CollisionRecoverySpeed);
	}

	FPEPCameraView View;
	View.Location = SmoothedPivot + (DesiredLocation - SmoothedPivot).GetSafeNormal() * CurrentCollisionLength;
	View.Rotation = Rotation;
	View.FieldOfView = FieldOfView;
	return View;
}
