#pragma once

#include "CoreMinimal.h"

#include "PEPInteractionSelectionTypes.generated.h"

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
