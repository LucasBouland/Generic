#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

#include "PEPPresentationTargetDefinition.generated.h"

class USkeleton;

/** Designer-authored description of a pawn presentation target used by an equipment profile. */
USTRUCT(BlueprintType)
struct PEPEQUIPMENT_API FPEPPresentationTargetDefinition
{
	GENERATED_BODY()

	/** Logical target registered by the pawn at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment",
		meta=(Categories="Equipment.PresentationTarget"))
	FGameplayTag TargetTag;

	/** Optional strict skeleton contract for modular skeletal visuals using this target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment")
	TSoftObjectPtr<USkeleton> ExpectedSkeleton;

	bool IsValid() const
	{
		return TargetTag.IsValid();
	}
};
