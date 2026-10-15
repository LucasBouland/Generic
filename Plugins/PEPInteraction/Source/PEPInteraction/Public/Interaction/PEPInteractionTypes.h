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

/**
 * The timed interaction the authority is running for one interactor.
 *
 * Replicated to the owning client only, so its UI can show progress. StartServerTime is read from
 * AGameStateBase::GetServerWorldTimeSeconds on the authority; a client compares it against its own
 * estimate of the same clock, which is what makes progress meaningful on both sides.
 */
USTRUCT(BlueprintType)
struct PEPINTERACTION_API FPEPActiveInteraction
{
	GENERATED_BODY()

	/** Allocated by the authority, increasing for each timed interaction started. 0 means none. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Interaction")
	int32 InteractionId = 0;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Interaction")
	TObjectPtr<AActor> Target = nullptr;

	UPROPERTY(
		BlueprintReadOnly,
		Category="PEP|Interaction",
		meta=(Categories="Interaction"))
	FGameplayTag InteractionTag;

	/** Server world time, in seconds, at which the interaction started. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Interaction")
	double StartServerTime = 0.0;

	/** Hold time, in seconds, the target asked for when the interaction started. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Interaction")
	float Duration = 0.0f;

	bool IsActive() const
	{
		return InteractionId != 0 && InteractionTag.IsValid();
	}
};
