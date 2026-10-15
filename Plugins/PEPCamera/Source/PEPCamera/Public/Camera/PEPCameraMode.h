#pragma once

#include "CoreMinimal.h"
#include "Camera/PEPCameraTypes.h"
#include "UObject/Object.h"

#include "PEPCameraMode.generated.h"

class UPEPCameraComponent;
class UPEPCameraModeStack;

/**
 * One way of looking at the world: first person, third person, strategy, or a project mode.
 *
 * A camera component owns one instance per mode class and evaluates them through a blending stack,
 * so every mode keeps its own state (zoom, focus point, lag) while it is in use. Modes in the stack
 * are updated every frame, including while they blend out.
 *
 * The view is computed by the native virtual ComputeView. A Blueprint subclass may implement
 * "Compute View" instead; it is only called when implemented, so native modes never pay for a
 * Blueprint dispatch per frame. A Blueprint implementation can start from the native result with
 * "Get Native View".
 *
 * Input is mode-relative: the component forwards pan, zoom and orbit to its current mode, and each
 * mode interprets them (a strategy camera pans its focus, a third-person camera zooms its boom).
 * Look input is not camera input: it stays on the pawn (AddControllerYawInput/AddControllerPitchInput)
 * and reaches the modes through FPEPCameraModeContext::ControlRotation.
 *
 * Instances persist for the life of their component: values written at runtime (an aiming FOV,
 * a zoom) stay until changed back. The class defaults remain available on the CDO.
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class PEPCAMERA_API UPEPCameraMode : public UObject
{
	GENERATED_BODY()

	friend class UPEPCameraModeStack;

public:
	virtual UWorld* GetWorld() const override;

	/**
	 * Computes this frame's view, with FieldOfView clamped to a valid projection. Called by the stack
	 * on every mode it holds; public so custom stacks and tests can evaluate a mode directly.
	 */
	FPEPCameraView UpdateView(const FPEPCameraModeContext& Context);

	/** Blend alpha for a raw blend weight in [0, 1], shaped by BlendFunction. */
	float EvaluateBlendAlpha(float Weight) const;

	/** Inverse of EvaluateBlendAlpha: the weight whose alpha is Alpha. */
	float InverseBlendAlpha(float Alpha) const;

	float GetBlendTime() const
	{
		return BlendTime;
	}

	UFUNCTION(BlueprintPure, Category="PEP|Camera")
	UPEPCameraComponent* GetCameraComponent() const;

	/**
	 * Forgets lag, smoothing and obstruction state so the next view starts where it should be.
	 * The component calls it after a teleport or when it was not evaluated for a while.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="PEP|Camera")
	void ResetInterpolation();
	virtual void ResetInterpolation_Implementation();

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

	/** Valid field of view range. Values outside it are clamped when the view is produced. */
	static constexpr float MinFieldOfView = 5.0f;
	static constexpr float MaxFieldOfView = 170.0f;

	/** Horizontal field of view of this mode, in degrees. Writable at runtime (aiming, sprint FOV, ...). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="PEP|Camera",
		meta=(UIMin="5.0", UIMax="170.0", ClampMin="5.0", ClampMax="170.0", Units="deg"))
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
	virtual FPEPCameraView ComputeView(const FPEPCameraModeContext& Context);

	/** Blueprint replacement for ComputeView, called only when a Blueprint subclass implements it. */
	UFUNCTION(BlueprintImplementableEvent, Category="PEP|Camera", meta=(DisplayName="Compute View"))
	FPEPCameraView K2_ComputeView(const FPEPCameraModeContext& Context);

	/** The native view of this mode, for a Blueprint "Compute View" that adjusts it. */
	UFUNCTION(BlueprintCallable, Category="PEP|Camera", meta=(DisplayName="Get Native View"))
	FPEPCameraView K2_GetNativeView(const FPEPCameraModeContext& Context);

	virtual void OnActivated(const FPEPCameraModeContext& Context)
	{
	}

	virtual void OnDeactivated()
	{
	}

	/** Blueprint hook, for example to hide the owner's head mesh in first person. */
	UFUNCTION(BlueprintImplementableEvent, Category="PEP|Camera", meta=(DisplayName="On Activated"))
	void ReceiveActivated(const FPEPCameraModeContext& Context);

	/** Called when the mode leaves the stack, including when the component ends play. */
	UFUNCTION(BlueprintImplementableEvent, Category="PEP|Camera", meta=(DisplayName="On Deactivated"))
	void ReceiveDeactivated();

private:
	/** The mode entered the stack. Resets transient state such as lag. */
	void NotifyActivated(const FPEPCameraModeContext& Context);

	/** The mode left the stack after being fully blended out, or the stack was reset. */
	void NotifyDeactivated();

	/** Whether a Blueprint subclass implements Compute View; resolved once per instance. */
	bool UsesBlueprintComputeView();

	TOptional<bool> bBlueprintComputeView;
};
