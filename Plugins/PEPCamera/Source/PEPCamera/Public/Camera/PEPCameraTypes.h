#pragma once

#include "CoreMinimal.h"

#include "PEPCameraTypes.generated.h"

class AActor;
class APlayerController;

/** Curve applied to a camera mode's blend weight while it blends in. */
UENUM(BlueprintType)
enum class EPEPCameraBlendFunction : uint8
{
	Linear,
	EaseIn,
	EaseOut,
	EaseInOut
};

/** What a camera mode produces each frame. The component turns the blended result into the final view. */
USTRUCT(BlueprintType)
struct PEPCAMERA_API FPEPCameraView
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PEP|Camera")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PEP|Camera")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PEP|Camera",
		meta=(UIMin="5.0", UIMax="170.0", ClampMin="5.0", ClampMax="170.0", Units="deg"))
	float FieldOfView = 90.0f;

	/** Moves this view towards Other by Alpha (0 keeps this view, 1 becomes Other), along the shortest rotation. */
	void Blend(const FPEPCameraView& Other, float Alpha);
};

/**
 * Everything a camera mode reads to compute its view. Built once per frame by the camera component.
 *
 * Modes are local presentation only: they never write gameplay or movement state, so they are safe
 * to run on any machine with a local viewer and never take part in GMC prediction or replay.
 */
USTRUCT(BlueprintType)
struct PEPCAMERA_API FPEPCameraModeContext
{
	GENERATED_BODY()

	/** Actor the camera looks at or from: the component's owner. Null for a detached component. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Camera")
	TObjectPtr<AActor> ViewTarget = nullptr;

	/** Local player controlling ViewTarget, when there is one. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Camera")
	TObjectPtr<APlayerController> PlayerController = nullptr;

	/** ViewTarget location, or the origin when there is no view target. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Camera")
	FVector PivotLocation = FVector::ZeroVector;

	/** ViewTarget rotation. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Camera")
	FRotator PivotRotation = FRotator::ZeroRotator;

	/** Pawn view (control) rotation, driven by look input. Equals PivotRotation for a non-pawn target. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Camera")
	FRotator ControlRotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Camera")
	float DeltaTime = 0.0f;
};
