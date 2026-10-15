#pragma once

#include "CoreMinimal.h"
#include "Camera/PEPCameraTypes.h"
#include "UObject/Object.h"

#include "PEPCameraModeStack.generated.h"

class UPEPCameraMode;

USTRUCT()
struct FPEPCameraModeStackEntry
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UPEPCameraMode> Mode = nullptr;

	/** Raw blend weight in [0, 1]; the mode's blend function shapes it into an alpha. */
	float Weight = 1.0f;
};

/**
 * Blends camera modes. Index 0 is the bottom; the last entry is the current mode.
 *
 * Each frame every mode computes its view, then the result is the bottom view blended towards each
 * entry above it by that entry's alpha. Once an entry is fully blended in, everything below it is
 * removed and deactivated. Pushing a mode already in the stack moves it to the top and starts it
 * from the weight that keeps the result continuous, so switching back mid-blend does not pop.
 */
UCLASS()
class PEPCAMERA_API UPEPCameraModeStack : public UObject
{
	GENERATED_BODY()

public:
	/** Makes Mode the current mode. Does nothing if it already is. */
	void PushCameraMode(UPEPCameraMode* Mode, const FPEPCameraModeContext& Context);

	/** Advances blend weights by Context.DeltaTime and returns the blended view. */
	FPEPCameraView EvaluateStack(const FPEPCameraModeContext& Context);

	/** Deactivates and removes every mode. */
	void Reset();

	UPEPCameraMode* GetTopMode() const;

	bool ContainsMode(const UPEPCameraMode* Mode) const;

	int32 Num() const
	{
		return Entries.Num();
	}

	/** True while the current mode has not fully replaced the ones below it. */
	bool IsBlending() const
	{
		return Entries.Num() > 1;
	}

private:
	UPROPERTY()
	TArray<FPEPCameraModeStackEntry> Entries;
};
