#pragma once

#include "CoreMinimal.h"
#include "Camera/PEPCameraMode.h"
#include "Engine/EngineTypes.h"

#include "PEPCameraMode_Strategy.generated.h"

/** Where a strategy camera's focus point comes from. */
UENUM(BlueprintType)
enum class EPEPStrategyFocusMode : uint8
{
	/** The camera owns its focus and moves it with pan input and edge scrolling (RTS). */
	Free,
	/** The focus follows the view target; pan input is ignored (top-down action games). */
	FollowViewTarget
};

/**
 * Strategy / top-down camera orbiting a focus point on the ground.
 *
 * The camera sits Distance away from the focus, at a pitch that goes from PitchAtMinDistance when
 * zoomed in to PitchAtMaxDistance when zoomed out, around a yaw changed by orbit input. In Free focus
 * mode, pan input (and optional screen-edge scrolling) moves the focus along the camera yaw, the
 * focus can follow the terrain height and be kept inside bounds.
 *
 * The focus is local camera state. A pawn that should stand at the focus (relevancy, audio, streaming)
 * reads GetFocusLocation, as APEPStrategyCameraPawn does.
 */
UCLASS(Blueprintable, meta=(DisplayName="PEP Strategy Camera"))
class PEPCAMERA_API UPEPCameraMode_Strategy : public UPEPCameraMode
{
	GENERATED_BODY()

public:
	UPEPCameraMode_Strategy();

	virtual void AddPanInput_Implementation(FVector2D Value) override;
	virtual void AddZoomInput_Implementation(float Value) override;
	virtual void AddOrbitInput_Implementation(FVector2D Value) override;
	virtual void ResetInterpolation_Implementation() override;

	/** Moves the focus immediately, for example to jump to a selected unit or an alert. */
	UFUNCTION(BlueprintCallable, Category="PEP|Camera|Strategy")
	void SetFocusLocation(FVector NewFocusLocation);

	UFUNCTION(BlueprintPure, Category="PEP|Camera|Strategy")
	FVector GetFocusLocation() const
	{
		return FocusLocation;
	}

	/** True once the focus has a meaningful location (first update, or SetFocusLocation). */
	UFUNCTION(BlueprintPure, Category="PEP|Camera|Strategy")
	bool HasFocus() const
	{
		return bFocusInitialized;
	}

	UFUNCTION(BlueprintCallable, Category="PEP|Camera|Strategy")
	void SetYaw(float NewYaw);

	UFUNCTION(BlueprintPure, Category="PEP|Camera|Strategy")
	float GetYaw() const;

	/** Sets the distance the camera zooms towards, clamped to [MinDistance, MaxDistance]. */
	UFUNCTION(BlueprintCallable, Category="PEP|Camera|Strategy")
	void SetTargetDistance(float NewDistance, bool bSnap = false);

	UFUNCTION(BlueprintPure, Category="PEP|Camera|Strategy")
	float GetTargetDistance() const;

	UFUNCTION(BlueprintPure, Category="PEP|Camera|Strategy")
	float GetCurrentDistance() const;

	/** Location clamped to FocusBounds when bConstrainFocus is set; unchanged otherwise. */
	UFUNCTION(BlueprintPure, Category="PEP|Camera|Strategy")
	FVector ConstrainFocusLocation(FVector Location) const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Focus")
	EPEPStrategyFocusMode FocusMode = EPEPStrategyFocusMode::Free;

	/** Added to the view target location in FollowViewTarget mode. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Focus")
	FVector FollowOffset = FVector::ZeroVector;

	/** Pan speed at DefaultDistance, in cm per second for a full input. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Pan", meta=(ClampMin="0.0"))
	float PanSpeed = 2500.0f;

	/** Pans faster when zoomed out, proportionally to the current distance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Pan")
	bool bScalePanWithDistance = true;

	/**
	 * Pans when the mouse cursor is near the viewport edge. Only while the player controller shows the
	 * cursor and the viewport has focus (see CanEdgeScroll).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Pan")
	bool bEdgeScrolling = false;

	/** Edge band, as a fraction of the viewport size. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Pan",
		meta=(ClampMin="0.0", ClampMax="0.5", EditCondition="bEdgeScrolling"))
	float EdgeScrollMargin = 0.02f;

	/** Keeps the focus on the ground under it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Terrain")
	bool bFollowTerrain = true;

	/**
	 * Object type the terrain probe hits. Queried by object type, not by trace channel, so units and
	 * other dynamic objects under the focus do not lift it.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Terrain", meta=(EditCondition="bFollowTerrain"))
	TEnumAsByte<ECollisionChannel> TerrainObjectType = ECC_WorldStatic;

	/** Half height of the vertical terrain probe around the focus. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Terrain",
		meta=(ClampMin="0.0", Units="cm", EditCondition="bFollowTerrain"))
	float TerrainProbeHalfHeight = 50000.0f;

	/** 0 follows the terrain height instantly. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Terrain",
		meta=(ClampMin="0.0", EditCondition="bFollowTerrain"))
	float TerrainInterpSpeed = 10.0f;

	/** Keeps the focus inside FocusBounds, in every focus mode and for SetFocusLocation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Focus")
	bool bConstrainFocus = false;

	/** World X/Y bounds of the focus. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Focus", meta=(EditCondition="bConstrainFocus"))
	FBox2D FocusBounds = FBox2D(FVector2D(-100000.0f, -100000.0f), FVector2D(100000.0f, 100000.0f));

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Zoom", meta=(ClampMin="0.0", Units="cm"))
	float DefaultDistance = 2500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Zoom", meta=(ClampMin="0.0", Units="cm"))
	float MinDistance = 800.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Zoom", meta=(ClampMin="0.0", Units="cm"))
	float MaxDistance = 6000.0f;

	/** Distance change per zoom step, as a fraction of the current distance (zoom feels even at any height). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Zoom", meta=(ClampMin="0.0", ClampMax="0.9"))
	float ZoomStepFraction = 0.15f;

	/** 0 applies zoom instantly. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Zoom", meta=(ClampMin="0.0"))
	float ZoomInterpSpeed = 8.0f;

	/** Pitch when fully zoomed in. Negative looks down. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Angle", meta=(ClampMin="-89.0", ClampMax="0.0", Units="deg"))
	float PitchAtMinDistance = -40.0f;

	/** Pitch when fully zoomed out. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Angle", meta=(ClampMin="-89.0", ClampMax="0.0", Units="deg"))
	float PitchAtMaxDistance = -65.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Angle")
	bool bAllowOrbit = true;

	/** Degrees of yaw per unit of orbit input. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Angle", meta=(EditCondition="bAllowOrbit"))
	float OrbitSpeed = 1.0f;

	/** Initial yaw, unless bInheritControlYaw takes it from the controller when the mode first runs. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Angle", meta=(Units="deg"))
	float DefaultYaw = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera|Strategy|Angle")
	bool bInheritControlYaw = false;

protected:
	virtual FPEPCameraView ComputeView(const FPEPCameraModeContext& Context) override;

	/**
	 * Whether edge scrolling may run this frame. Default: a local player controller that shows the
	 * mouse cursor and whose viewport has focus. Override to also stop it while a menu is open.
	 */
	virtual bool CanEdgeScroll(const FPEPCameraModeContext& Context) const;

	/** Screen-edge pan direction from the mouse position, X right and Y forward. */
	virtual FVector2D ComputeEdgeScrollInput(const FPEPCameraModeContext& Context) const;

	/** Moves FocusLocation's height towards the ground below it. */
	virtual void FollowTerrain(const FPEPCameraModeContext& Context);

private:
	void EnsureInitialized(const FPEPCameraModeContext& Context);
	float ClampDistance(float Distance) const;

	FVector FocusLocation = FVector::ZeroVector;
	FVector2D PendingPanInput = FVector2D::ZeroVector;
	float Yaw = 0.0f;
	float TargetDistance = -1.0f;
	float CurrentDistance = -1.0f;
	bool bFocusInitialized = false;
	bool bYawInitialized = false;
};
