#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "PEPCameraInputConfig.generated.h"

class UInputAction;

/**
 * Enhanced Input actions the camera component can bind itself to. Every action is optional.
 *
 * Mapping contexts stay with whoever owns the pawn's input (the pawn or the player controller);
 * this asset only names the actions. Look input is not here: it drives the control rotation on the pawn.
 */
UCLASS(BlueprintType, Const)
class PEPCAMERA_API UPEPCameraInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Axis2D, X right and Y forward. Strategy cameras pan their focus. Bound on Triggered. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Camera|Input")
	TObjectPtr<const UInputAction> PanAction;

	/** Axis1D, positive zooms in (mouse wheel up). Bound on Triggered. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Camera|Input")
	TObjectPtr<const UInputAction> ZoomAction;

	/** Axis1D or Axis2D, X turns the strategy camera around its focus. Bound on Triggered. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Camera|Input")
	TObjectPtr<const UInputAction> OrbitAction;

	/** Digital: next mode in the component's CycleCameraModes (first/third person toggle). Bound on Started. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Camera|Input")
	TObjectPtr<const UInputAction> CycleModeAction;

	/** Digital: swaps the third-person shoulder. Bound on Started. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Camera|Input")
	TObjectPtr<const UInputAction> SwapShoulderAction;
};
