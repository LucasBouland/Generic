#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "PEPUtilityTypes.generated.h"

/** Generic tag/value pair for helper APIs; domain ownership stays with the caller. */
USTRUCT(BlueprintType)
struct PEPUTILITY_API FPEPTaggedMagnitude
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PEP|Data", meta=(Categories="Attribute,SetByCaller"))
	FGameplayTag Tag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PEP|Data")
	float Value = 0.0f;

	bool IsValid() const
	{
		return Tag.IsValid();
	}
};
