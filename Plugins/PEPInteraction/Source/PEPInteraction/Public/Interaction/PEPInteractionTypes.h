#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

#include "PEPInteractionTypes.generated.h"

USTRUCT(BlueprintType)
struct PEPINTERACTION_API FPEPInteractionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="PEP|Interaction")
	bool bSuccess = false;

	UPROPERTY(
		BlueprintReadOnly,
		Category="PEP|Interaction",
		meta=(Categories="Reason"))
	FGameplayTag ReasonTag;

	UPROPERTY(
		BlueprintReadOnly,
		Category="PEP|Interaction",
		meta=(Categories="Interaction"))
	FGameplayTag InteractionTag;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Interaction")
	TObjectPtr<AActor> Target = nullptr;

	static FPEPInteractionResult Success(
		AActor* InTarget,
		const FGameplayTag InInteractionTag)
	{
		FPEPInteractionResult Result;
		Result.bSuccess = true;
		Result.Target = InTarget;
		Result.InteractionTag = InInteractionTag;
		return Result;
	}

	static FPEPInteractionResult Failure(
		const FGameplayTag InReasonTag,
		AActor* InTarget = nullptr,
		const FGameplayTag InInteractionTag = FGameplayTag())
	{
		FPEPInteractionResult Result;
		Result.ReasonTag = InReasonTag;
		Result.Target = InTarget;
		Result.InteractionTag = InInteractionTag;
		return Result;
	}
};