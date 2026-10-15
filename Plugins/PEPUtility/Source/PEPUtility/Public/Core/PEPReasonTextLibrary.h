#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Core/PEPTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PEPReasonTextLibrary.generated.h"

USTRUCT(BlueprintType)
struct PEPUTILITY_API FPEPFailureMessage
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category="PEP|Failure")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Failure", meta=(Categories="Reason"))
	FGameplayTag ReasonTag;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Failure")
	FPEPItemId BadItemId;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Failure")
	FPEPItemId SecondaryItemId;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Failure")
	FName BadField = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Failure")
	FText Message;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Failure")
	FText SuggestedFix;
};

UCLASS()
class PEPUTILITY_API UPEPReasonTextLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="PEP|Reason")
	static FText GetReasonText(FGameplayTag ReasonTag);

	UFUNCTION(BlueprintPure, Category="PEP|Reason")
	static FText GetSuggestedFixText(FGameplayTag ReasonTag);

	UFUNCTION(BlueprintPure, Category="PEP|Reason")
	static FPEPFailureMessage MakeFailureMessageFromOperationResult(
		const FPEPOperationResult& Result,
		FName BadField = NAME_None);
};
