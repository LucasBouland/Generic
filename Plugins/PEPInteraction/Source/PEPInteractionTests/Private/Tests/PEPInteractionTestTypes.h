#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/PEPInteractionComponent.h"
#include "Interaction/PEPInteractionInteractable.h"

#include "PEPInteractionTestTypes.generated.h"

/**
 * Interactable relying on every interface default: instant, no start or cancel handling.
 * Offers Options, allows them while bAllowInteraction, and counts CanInteract calls and executions.
 * With bReturnBareFailure, ExecuteInteraction fails through UPEPInteractionLibrary without naming
 * itself or the option, as a Blueprint interactable might.
 */
UCLASS(NotBlueprintable)
class APEPInteractionTestTarget : public AActor, public IPEPInteractionInteractable
{
	GENERATED_BODY()

public:
	APEPInteractionTestTarget();

	virtual TArray<FGameplayTag> GetInteractOptionTags_Implementation(
		AActor* Interactor) const override;
	virtual FPEPInteractionResult CanInteract_Implementation(
		AActor* Interactor,
		FGameplayTag InteractionTag) const override;
	virtual FPEPInteractionResult ExecuteInteraction_Implementation(
		AActor* Interactor,
		FGameplayTag InteractionTag) override;

	TArray<FGameplayTag> Options;
	bool bAllowInteraction = true;
	bool bReturnBareFailure = false;
	int32 ExecuteCount = 0;
	mutable int32 CanInteractCount = 0;
};

/** Timed interactable recording the authority notifications it receives. */
UCLASS(NotBlueprintable)
class APEPInteractionTestTimedTarget : public APEPInteractionTestTarget
{
	GENERATED_BODY()

public:
	virtual float GetInteractionDuration_Implementation(
		AActor* Interactor,
		FGameplayTag InteractionTag) const override;
	virtual void OnInteractionStarted_Implementation(
		AActor* Interactor,
		FGameplayTag InteractionTag) override;
	virtual void OnInteractionCancelled_Implementation(
		AActor* Interactor,
		FGameplayTag InteractionTag,
		FGameplayTag ReasonTag) override;

	float Duration = 2.0f;
	int32 StartedCount = 0;
	int32 CancelledCount = 0;
	FGameplayTag LastCancelReason;
};

/**
 * Interaction component opening the protected hooks a test needs: an explicit clock for the timed
 * interaction, and no line of sight requirement (collision in a bare test world is not under test).
 */
UCLASS(NotBlueprintable)
class UPEPInteractionTestComponent : public UPEPInteractionComponent
{
	GENERATED_BODY()

public:
	void SetRangeForTest(const float InRange, const float InServerRangeTolerance)
	{
		Range = InRange;
		ServerRangeTolerance = InServerRangeTolerance;
	}

	void SetValidationTimingForTest(const float InInterval, const float InGracePeriod)
	{
		ActiveInteractionValidationInterval = InInterval;
		ActiveInteractionValidationGracePeriod = InGracePeriod;
	}

	bool ShouldPerformLocalSelectionForTest() const
	{
		return ShouldPerformLocalSelection();
	}

	/* What the owning client's request RPC runs on the authority. */
	void ReceiveClientRequestForTest(
		AActor* RequestedTarget,
		const FGameplayTag InteractionTag,
		const int32 InteractionId)
	{
		Server_RequestInteractWithInteractionTag_Implementation(
			RequestedTarget,
			InteractionTag,
			InteractionId);
	}

	void TickActiveInteractionForTest(const double ServerTimeSeconds)
	{
		TickActiveInteraction(ServerTimeSeconds);
	}

	float GetInteractionProgressAtTimeForTest(const double ServerTimeSeconds) const
	{
		return GetInteractionProgressAtTime(ServerTimeSeconds);
	}

	double GetActiveInteractionStartTimeForTest() const
	{
		return ActiveInteraction.StartServerTime;
	}

protected:
	virtual bool ValidateLineOfSight(AActor* RequestedTarget) const override
	{
		return true;
	}
};

/** Records the component's interaction events. */
UCLASS()
class UPEPInteractionTestListener : public UObject
{
	GENERATED_BODY()

public:
	void Bind(UPEPInteractionComponent* Component);

	UFUNCTION()
	void HandleStarted(AActor* Target, FGameplayTag InteractionTag, float Duration);

	UFUNCTION()
	void HandleCompleted(const FPEPInteractionResult& Result);

	UFUNCTION()
	void HandleCancelled(
		int32 InteractionId,
		AActor* Target,
		FGameplayTag InteractionTag,
		FGameplayTag ReasonTag);

	UFUNCTION()
	void HandleSelectionChanged(AActor* Target, const TArray<FGameplayTag>& Options);

	int32 StartedCount = 0;
	int32 CompletedCount = 0;
	int32 CancelledCount = 0;
	int32 SelectionChangedCount = 0;
	FPEPInteractionResult LastResult;
	FGameplayTag LastCancelReason;
	int32 LastCancelledInteractionId = 0;
};
