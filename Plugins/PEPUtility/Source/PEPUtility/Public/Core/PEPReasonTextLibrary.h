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
	FName BadField = NAME_None;

	/** Player-facing text. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Failure")
	FText Message;

	/** Developer diagnostics (logs, tools, validation reports); do not show to players. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Failure")
	FText DeveloperMessage;

	/** Developer-facing remediation hint; empty on success. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Failure")
	FText SuggestedFix;
};

/** Reason tag texts backed by FPEPReasonTextRegistry; domain plugins register texts for their own reason tags. */
UCLASS()
class PEPUTILITY_API UPEPReasonTextLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Player-facing text of a reason; unregistered or missing reasons get a generic message. */
	UFUNCTION(BlueprintPure, Category="PEP|Reason")
	static FText GetReasonText(FGameplayTag ReasonTag);

	/** Developer-facing text of a reason; names the tag when it has no registered text. */
	UFUNCTION(BlueprintPure, Category="PEP|Reason")
	static FText GetDeveloperReasonText(FGameplayTag ReasonTag);

	/** Developer-facing remediation hint; empty for Reason.None. */
	UFUNCTION(BlueprintPure, Category="PEP|Reason")
	static FText GetSuggestedFixText(FGameplayTag ReasonTag);

	/** A success without a reason tag is described as Reason.None and carries no suggested fix. */
	UFUNCTION(BlueprintPure, Category="PEP|Reason")
	static FPEPFailureMessage MakeFailureMessageFromOperationResult(
		const FPEPOperationResult& Result,
		FName BadField = NAME_None);
};
