#pragma once

#include "CoreMinimal.h"
#include "Camera/PEPCameraMode.h"

#include "PEPCameraMode_FirstPerson.generated.h"

class USkeletalMeshComponent;

/**
 * First-person camera: the eye looks along the control rotation.
 *
 * The eye is a socket on one of the view target's skeletal meshes when ViewSocketName is set and
 * found (a head or camera bone that follows animation), otherwise the pawn view location
 * (actor location plus BaseEyeHeight). Hiding the owner's head is a project choice: do it from the
 * mode's On Activated / On Deactivated events or with owner-no-see on the mesh.
 */
UCLASS(Blueprintable, meta=(DisplayName="PEP First Person Camera"))
class PEPCAMERA_API UPEPCameraMode_FirstPerson : public UPEPCameraMode
{
	GENERATED_BODY()

public:
	/** Optional socket or bone on the view target's skeletal mesh. None uses the pawn view location. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|First Person")
	FName ViewSocketName = NAME_None;

	/** Offset from the eye, in control-yaw space: X forward, Y right, Z up. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|First Person")
	FVector ViewOffset = FVector::ZeroVector;

protected:
	virtual FPEPCameraView ComputeView_Implementation(const FPEPCameraModeContext& Context) override;
	virtual void OnActivated(const FPEPCameraModeContext& Context) override;

	/** World location of the eye before ViewOffset. */
	virtual FVector ResolveEyeLocation(const FPEPCameraModeContext& Context);

private:
	USkeletalMeshComponent* FindSocketMesh(AActor* ViewTarget);

	TWeakObjectPtr<USkeletalMeshComponent> CachedSocketMesh;
};
