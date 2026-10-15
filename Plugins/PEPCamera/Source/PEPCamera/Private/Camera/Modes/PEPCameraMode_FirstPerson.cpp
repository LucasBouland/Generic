#include "Camera/Modes/PEPCameraMode_FirstPerson.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"

FPEPCameraView UPEPCameraMode_FirstPerson::ComputeView_Implementation(const FPEPCameraModeContext& Context)
{
	const FRotator YawRotation(0.0f, Context.ControlRotation.Yaw, 0.0f);

	FPEPCameraView View;
	View.Location = ResolveEyeLocation(Context) + YawRotation.RotateVector(ViewOffset);
	View.Rotation = Context.ControlRotation;
	View.FieldOfView = FieldOfView;
	return View;
}

void UPEPCameraMode_FirstPerson::OnActivated(const FPEPCameraModeContext& Context)
{
	/* The view target may have changed meshes, or be another actor altogether. */
	CachedSocketMesh.Reset();
}

FVector UPEPCameraMode_FirstPerson::ResolveEyeLocation(const FPEPCameraModeContext& Context)
{
	if (!ViewSocketName.IsNone())
	{
		if (const USkeletalMeshComponent* Mesh = FindSocketMesh(Context.ViewTarget))
		{
			return Mesh->GetSocketLocation(ViewSocketName);
		}
	}

	if (const APawn* Pawn = Cast<APawn>(Context.ViewTarget))
	{
		return Pawn->GetPawnViewLocation();
	}
	return Context.PivotLocation;
}

USkeletalMeshComponent* UPEPCameraMode_FirstPerson::FindSocketMesh(AActor* ViewTarget)
{
	if (!ViewTarget)
	{
		return nullptr;
	}

	USkeletalMeshComponent* Cached = CachedSocketMesh.Get();
	if (Cached && Cached->GetOwner() == ViewTarget && Cached->DoesSocketExist(ViewSocketName))
	{
		return Cached;
	}

	TInlineComponentArray<USkeletalMeshComponent*> Meshes(ViewTarget);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (Mesh && Mesh->DoesSocketExist(ViewSocketName))
		{
			CachedSocketMesh = Mesh;
			return Mesh;
		}
	}

	CachedSocketMesh.Reset();
	return nullptr;
}
