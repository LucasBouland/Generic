#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PEPInputTagResolvers.generated.h"

USTRUCT(BlueprintType)
struct PEPUTILITY_API FPEPInputValueTagMapping
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Input")
	int32 Value = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Input", meta=(Categories="Input"))
	FGameplayTag InputTag;
};

UCLASS()
class PEPUTILITY_API UPEPInputTagResolvers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="PEP|Input")
	static FGameplayTag ResolveInputTagFromValue(float RawValue, const TArray<FPEPInputValueTagMapping>& Mappings,
	                                             float Tolerance = 0.25f);

private:
	static bool IsNearlyValue(float A, float B, float Tolerance = 0.25f);
};
