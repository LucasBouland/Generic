#pragma once

#include "CoreMinimal.h"
#include "Camera/PEPCameraTypes.h"
#include "UObject/Object.h"

#include "PEPCameraMode.generated.h"

class UPEPCameraComponent;

/**
 * One way of looking at the world: first person, third person, strategy, or a project mode.
 *
 * A camera component owns one instance per mode class and evaluates them through a blending stack,
 * so every mode keeps its own state (zoom, focus point, lag) while it is in use. Modes in the stack
 * are updated every frame, including while they blend out.
 *
 * Input is mode-relative: the component forwards pan, zoom and orbit to its current mode, and each
 * mode interprets them (a strategy camera pans its focus, a third-person camera zooms its boom).
 * Look input is not camera input: it stays on the pawn (AddControllerYawInput/AddControllerPitchInput)
 * and reaches the modes through FPEPCameraModeContext::ControlRotation.
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class PEPCAMERA_API UPEPCameraMode : public UObject
{
	GENERATED_BODY()

public:
	virtual UWorld* GetWorld() const override;

	/** Computes this frame's view. Called by the stack on every mode it holds. */
	FPEPCameraView UpdateView(const FPEPCameraModeContext& Context);

	/** The mode entered the stack. Resets transient state such as lag. */
	void NotifyActivated(const FPEPCameraModeContext& Context);

	/** The mode left the stack after being fully blended out. */
	void NotifyDeactivated();

	/** Blend alpha for a raw blend weight in [0, 1], shaped by BlendFunction. */
	float EvaluateBlendAlpha(float Weight) const;

	float GetBlendTime() const
	{
		return BlendTime;
	}

	UFUNCTION(BlueprintPure, Category="PEP|Camera")
	UPEPCameraComponent* GetCameraComponent() const;

	/** Strafe/advance input, X right and Y forward. Rate input: scaled by delta time where it moves things. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="PEP|Camera|Input")
	void AddPanInput(FVector2D Value);
	virtual void AddPanInput_Implementation(FVector2D Value);

	/** Positive zooms in. Step input: one wheel notch is 1. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="PEP|Camera|Input")
	void AddZoomInput(float Value);
	virtual void AddZoomInput_Implementation(float Value);

	/** X turns around the focus, Y tilts. Step input, in degrees once scaled by the mode. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="PEP|Camera|Input")
	void AddOrbitInput(FVector2D Value);
	virtual void AddOrbitInput_Implementation(FVector2D Value);

	/** Horizontal field of view of this mode, in degrees. Writable at runtime (aiming, sprint FOV, ...). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera",
		meta=(UIMin="5.0", UIMax="170.0", ClampMin="1.0", ClampMax="170.0", Units="deg"))
	float FieldOfView = 90.0f;

	/** Seconds to blend in when this mode becomes current. 0 cuts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Camera|Blend", meta=(ClampMin="0.0", Units="s"))
	float BlendTime = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Camera|Blend")
	EPEPCameraBlendFunction BlendFunction = EPEPCameraBlendFunction::EaseOut;

	/** Exponent of the ease functions. Ignored by Linear. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Camera|Blend", meta=(ClampMin="0.1"))
	float BlendExponent = 2.0f;

protected:
	/** The view of this mode alone. Defaults to the pivot looking along the control rotation. */
	UFUNCTION(BlueprintNativeEvent, Category="PEP|Camera")
	FPEPCameraView ComputeView(const FPEPCameraModeContext& Context);
	virtual FPEPCameraView ComputeView_Implementation(const FPEPCameraModeContext& Context);

	virtual void OnActivated(const FPEPCameraModeContext& Context)
	{
	}

	virtual void OnDeactivated()
	{
	}

	/** Blueprint hook, for example to hide the owner's head mesh in first person. */
	UFUNCTION(BlueprintImplementableEvent, Category="PEP|Camera", meta=(DisplayName="On Activated"))
	void ReceiveActivated(const FPEPCameraModeContext& Context);

	UFUNCTION(BlueprintImplementableEvent, Category="PEP|Camera", meta=(DisplayName="On Deactivated"))
	void ReceiveDeactivated();
};
