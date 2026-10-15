#pragma once

#include "CoreMinimal.h"

#include "PEPInteractionSelectionTypes.generated.h"

class AActor;

UENUM(BlueprintType)
enum class EPEPInteractionSelectionShape : uint8
{
	Line,
	SphereSweep,
	Cylinder
};

UENUM(BlueprintType)
enum class EPEPInteractionCylinderAxisSource : uint8
{
	CameraForward,
	InteractorForward
};

/**
 * One interactable considered by a local selection pass. Lives only for that pass: the buffer
 * holding it is reset before and after, so the raw actor pointer never outlives the frame.
 */
struct FPEPInteractionSelectionCandidate
{
	AActor* Actor = nullptr;
	float Score = -1.0f;
	float AimAlignment = 0.0f;
	float Distance = 0.0f;

	/* Line of sight is traced lazily, only for the candidates the selection needs to know about. */
	bool bVisibilityKnown = false;
	bool bVisible = false;
};
