#pragma once

#include "CoreMinimal.h"
#include "Camera/PEPCameraMode.h"
#include "Engine/EngineTypes.h"

#include "PEPCameraMode_ThirdPerson.generated.h"

/**
 * Third-person camera on a boom behind a pivot, looking along the control rotation.
 *
 * The pivot is the view target (or a socket on its mesh) plus PivotOffset, optionally lagged.
 * The boom has a zoomable length and an over-the-shoulder offset that can swap sides. A sphere probe
 * pulls the camera in front of blocking geometry immediately and lets it recover smoothly.
 */
UCLASS(Blueprintable, meta=(DisplayName="PEP Third Person Camera"))
class PEPCAMERA_API UPEPCameraMode_ThirdPerson : public UPEPCameraMode
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="PEP|Camera|Third Person")
	void SwapShoulder();

	UFUNCTION(BlueprintCallable, Category="PEP|Camera|Third Person")
	void SetRightShoulder(bool bRight);

	UFUNCTION(BlueprintPure, Category="PEP|Camera|Third Person")
	bool IsRightShoulder() const
	{
		return bRightShoulder;
	}

	/** Sets the boom length the camera zooms towards, clamped to [MinArmLength, MaxArmLength]. */
	UFUNCTION(BlueprintCallable, Category="PEP|Camera|Third Person")
	void SetTargetArmLength(float NewArmLength, bool bSnap = false);

	UFUNCTION(BlueprintPure, Category="PEP|Camera|Third Person")
	float GetTargetArmLength() const;

	virtual void AddZoomInput_Implementation(float Value) override;

	/** Optional socket on the view target's skeletal mesh used as pivot instead of the actor location. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Third Person|Pivot")
	FName PivotSocketName = NAME_None;

	/** World-space offset added to the pivot (typically chest or head height). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Third Person|Pivot")
	FVector PivotOffset = FVector(0.0f, 0.0f, 60.0f);

	/** Pivot follow speed. 0 follows exactly. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Third Person|Pivot", meta=(ClampMin="0.0"))
	float PivotLagSpeed = 0.0f;

	/** Initial boom length. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Third Person|Boom", meta=(ClampMin="0.0", Units="cm"))
	float ArmLength = 300.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Third Person|Boom", meta=(ClampMin="0.0", Units="cm"))
	float MinArmLength = 120.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Third Person|Boom", meta=(ClampMin="0.0", Units="cm"))
	float MaxArmLength = 600.0f;

	/** Boom length change per zoom step. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Third Person|Boom", meta=(ClampMin="0.0", Units="cm"))
	float ZoomStep = 50.0f;

	/** 0 applies zoom instantly. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Third Person|Boom", meta=(ClampMin="0.0"))
	float ZoomInterpSpeed = 10.0f;

	/** Offset at the end of the boom, in camera space: X forward, Y right (mirrored on the left shoulder), Z up. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Third Person|Shoulder")
	FVector ShoulderOffset = FVector(0.0f, 50.0f, 0.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Third Person|Shoulder")
	bool bRightShoulder = true;

	/** 0 swaps instantly. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Third Person|Shoulder", meta=(ClampMin="0.0"))
	float ShoulderInterpSpeed = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Third Person|Collision")
	bool bDoCollisionTest = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Third Person|Collision", meta=(ClampMin="0.0", Units="cm"))
	float ProbeSize = 12.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Third Person|Collision")
	TEnumAsByte<ECollisionChannel> ProbeChannel = ECC_Camera;

	/** Speed at which the camera returns after an obstruction clears. 0 returns instantly. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Third Person|Collision", meta=(ClampMin="0.0"))
	float CollisionRecoverySpeed = 8.0f;

protected:
	virtual FPEPCameraView ComputeView_Implementation(const FPEPCameraModeContext& Context) override;
	virtual void OnActivated(const FPEPCameraModeContext& Context) override;

	/** World location of the pivot before PivotOffset. */
	virtual FVector ResolvePivotLocation(const FPEPCameraModeContext& Context) const;

	/** Distance from Pivot towards DesiredLocation that is free of blocking geometry. */
	virtual float ProbeBoomLength(const FPEPCameraModeContext& Context, const FVector& Pivot, const FVector& DesiredLocation) const;

private:
	void EnsureArmInitialized();

	float TargetArmLength = -1.0f;
	float CurrentArmLength = -1.0f;
	float CurrentShoulderSign = 1.0f;
	bool bShoulderInitialized = false;
	FVector SmoothedPivot = FVector::ZeroVector;
	bool bHasSmoothedPivot = false;
	float CurrentCollisionLength = -1.0f;
};
