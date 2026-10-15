#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraComponent.h"
#include "Camera/PEPCameraTypes.h"

#include "PEPCameraComponent.generated.h"

class UEnhancedInputComponent;
class UPEPCameraInputConfig;
class UPEPCameraMode;
class UPEPCameraModeStack;
struct FInputActionValue;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnPEPCameraModeChanged,
	UPEPCameraMode*, PreviousMode,
	UPEPCameraMode*, NewMode);

/**
 * Camera component driven by camera modes (first person, third person, strategy, or project modes).
 *
 * Put it on the actor used as view target, usually the pawn. Each frame it evaluates the current mode
 * through a blending stack, so switching modes blends instead of cutting, and it moves itself to the
 * resulting view. Modes are instanced once per class and keep their state between uses.
 *
 * Switch modes with SetCameraMode, or cycle through CycleCameraModes. Pan, zoom and orbit input go to
 * the current mode; BindCameraInput wires them from an Enhanced Input component.
 *
 * Local presentation only: nothing here replicates or touches movement, so it never runs inside
 * GMC prediction or replay.
 */
UCLASS(Blueprintable, ClassGroup=(PEP), meta=(BlueprintSpawnableComponent, DisplayName="PEP Camera Component"))
class PEPCAMERA_API UPEPCameraComponent : public UCameraComponent
{
	GENERATED_BODY()

public:
	UPEPCameraComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView) override;

	/** Requests a mode. The change blends in from the next camera update. Null returns to DefaultCameraMode. */
	UFUNCTION(BlueprintCallable, Category="PEP|Camera")
	void SetCameraMode(TSubclassOf<UPEPCameraMode> ModeClass);

	/** The mode the component is moving towards (requested, or DefaultCameraMode). */
	UFUNCTION(BlueprintPure, Category="PEP|Camera")
	TSubclassOf<UPEPCameraMode> GetCameraModeClass() const;

	/** Instance of the mode the component is moving towards, created on demand. */
	UFUNCTION(BlueprintCallable, Category="PEP|Camera")
	UPEPCameraMode* GetCurrentCameraMode();

	/** This component's instance of ModeClass, created on demand. Use it to tune a mode at runtime. */
	UFUNCTION(BlueprintCallable, Category="PEP|Camera", meta=(DeterminesOutputType="ModeClass"))
	UPEPCameraMode* GetCameraModeInstance(TSubclassOf<UPEPCameraMode> ModeClass);

	template <typename TMode>
	TMode* GetCameraModeInstance()
	{
		return Cast<TMode>(GetCameraModeInstance(TMode::StaticClass()));
	}

	/** Moves to the next (Direction > 0) or previous mode of CycleCameraModes. */
	UFUNCTION(BlueprintCallable, Category="PEP|Camera")
	void CycleCameraMode(int32 Direction = 1);

	/** True while a previous mode is still blending out. */
	UFUNCTION(BlueprintPure, Category="PEP|Camera")
	bool IsBlendingCameraModes() const;

	/** The last view this component produced. */
	UFUNCTION(BlueprintPure, Category="PEP|Camera")
	FPEPCameraView GetLastCameraView() const
	{
		return LastCameraView;
	}

	UFUNCTION(BlueprintCallable, Category="PEP|Camera|Input")
	void AddPanInput(FVector2D Value);

	UFUNCTION(BlueprintCallable, Category="PEP|Camera|Input")
	void AddZoomInput(float Value);

	UFUNCTION(BlueprintCallable, Category="PEP|Camera|Input")
	void AddOrbitInput(FVector2D Value);

	/** Swaps the shoulder of the third-person mode, if the current mode is one. */
	UFUNCTION(BlueprintCallable, Category="PEP|Camera|Input")
	void SwapShoulder();

	/**
	 * Binds the actions of InputConfig on InputComponent. Call it from the pawn's
	 * SetupPlayerInputComponent. Calling it again replaces the previous bindings.
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Camera|Input")
	void BindCameraInput(UEnhancedInputComponent* InputComponent, const UPEPCameraInputConfig* InputConfig);

	/** Fired when a different mode starts blending in. */
	UPROPERTY(BlueprintAssignable, Category="PEP|Camera")
	FOnPEPCameraModeChanged OnCameraModeChanged;

	/** Mode used when none is requested. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Camera")
	TSubclassOf<UPEPCameraMode> DefaultCameraMode;

	/** Modes CycleCameraMode steps through, for example first person then third person. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Camera")
	TArray<TSubclassOf<UPEPCameraMode>> CycleCameraModes;

protected:
	/** Mode to use this frame. Override to choose from gameplay state (aiming, vehicle, ...). */
	virtual TSubclassOf<UPEPCameraMode> DetermineCameraMode() const;

	virtual FPEPCameraModeContext BuildModeContext(float DeltaTime) const;

private:
	UPEPCameraModeStack* GetOrCreateModeStack();
	void UpdateCameraModes(const FPEPCameraModeContext& Context);

	void HandlePanAction(const FInputActionValue& Value);
	void HandleZoomAction(const FInputActionValue& Value);
	void HandleOrbitAction(const FInputActionValue& Value);
	void HandleCycleModeAction(const FInputActionValue& Value);
	void HandleSwapShoulderAction(const FInputActionValue& Value);

	UPROPERTY(Transient)
	TObjectPtr<UPEPCameraModeStack> ModeStack;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPEPCameraMode>> ModeInstances;

	UPROPERTY(Transient)
	TSubclassOf<UPEPCameraMode> RequestedCameraMode;

	UPROPERTY(Transient)
	TObjectPtr<UPEPCameraMode> LastPushedMode;

	FPEPCameraView LastCameraView;

	TWeakObjectPtr<UEnhancedInputComponent> BoundInputComponent;
	TArray<uint32> BoundInputHandles;
};
