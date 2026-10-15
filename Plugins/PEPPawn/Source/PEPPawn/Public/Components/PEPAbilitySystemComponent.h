#pragma once

#include "CoreMinimal.h"
#include "Components/GMCAbilityComponent.h"
#include "PEPAbilitySystemComponent.generated.h"

/** PEP extension point for the GMC Ability System Component. */
UCLASS(ClassGroup=(PEP), meta=(BlueprintSpawnableComponent, DisplayName="PEP Ability System Component"))
class PEPPAWN_API UPEPAbilitySystemComponent : public UGMC_AbilitySystemComponent
{
	GENERATED_BODY()
};
