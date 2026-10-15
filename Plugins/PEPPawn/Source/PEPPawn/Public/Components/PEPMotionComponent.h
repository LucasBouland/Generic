#pragma once

#include "CoreMinimal.h"
#include "Components/GMCMotion.h"
#include "PEPMotionComponent.generated.h"

/** PEP extension point for the GMCMotion movement component. */
UCLASS(ClassGroup=(PEP), meta=(BlueprintSpawnableComponent, DisplayName="PEP Motion Component"))
class PEPPAWN_API UPEPMotionComponent : public UGMCMotion
{
	GENERATED_BODY()
};
