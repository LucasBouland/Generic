#include "Tests/PEPInteractionTestTypes.h"

#include "Components/SceneComponent.h"
#include "Core/PEPCoreTags.h"
#include "Interaction/PEPInteractionLibrary.h"

APEPInteractionTestTarget::APEPInteractionTestTarget()
{
	/* A root so the tests can place the target relative to the interactor. */
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

TArray<FGameplayTag> APEPInteractionTestTarget::GetInteractOptionTags_Implementation(
	AActor* Interactor) const
{
	return Options;
}

FPEPInteractionResult APEPInteractionTestTarget::CanInteract_Implementation(
	AActor* Interactor,
	const FGameplayTag InteractionTag) const
{
	++CanInteractCount;
	if (!bAllowInteraction)
	{
		return FPEPInteractionResult::Failure(
			PEPCoreTags::Reason_BlockedByState,
			const_cast<ThisClass*>(this),
			InteractionTag);
	}
	return FPEPInteractionResult::Success(
		const_cast<ThisClass*>(this),
		InteractionTag);
}

FPEPInteractionResult APEPInteractionTestTarget::ExecuteInteraction_Implementation(
	AActor* Interactor,
	const FGameplayTag InteractionTag)
{
	++ExecuteCount;
	if (bReturnBareFailure)
	{
		return UPEPInteractionLibrary::MakeInteractionFailure(
			PEPCoreTags::Reason_BlockedByState,
			nullptr,
			FGameplayTag());
	}
	return UPEPInteractionLibrary::MakeInteractionSuccess(this, InteractionTag);
}

float APEPInteractionTestTimedTarget::GetInteractionDuration_Implementation(
	AActor* Interactor,
	const FGameplayTag InteractionTag) const
{
	return Duration;
}

void APEPInteractionTestTimedTarget::OnInteractionStarted_Implementation(
	AActor* Interactor,
	const FGameplayTag InteractionTag)
{
	++StartedCount;
}

void APEPInteractionTestTimedTarget::OnInteractionCancelled_Implementation(
	AActor* Interactor,
	const FGameplayTag InteractionTag,
	const FGameplayTag ReasonTag)
{
	++CancelledCount;
	LastCancelReason = ReasonTag;
}

void UPEPInteractionTestListener::Bind(UPEPInteractionComponent* Component)
{
	Component->OnInteractionStarted.AddDynamic(this, &ThisClass::HandleStarted);
	Component->OnInteractionCompleted.AddDynamic(this, &ThisClass::HandleCompleted);
	Component->OnInteractionCancelled.AddDynamic(this, &ThisClass::HandleCancelled);
	Component->OnInteractionSelectionChanged.AddDynamic(this, &ThisClass::HandleSelectionChanged);
}

void UPEPInteractionTestListener::HandleStarted(
	AActor* Target,
	const FGameplayTag InteractionTag,
	const float Duration)
{
	++StartedCount;
}

void UPEPInteractionTestListener::HandleCompleted(const FPEPInteractionResult& Result)
{
	++CompletedCount;
	LastResult = Result;
}

void UPEPInteractionTestListener::HandleCancelled(
	const int32 InteractionId,
	AActor* Target,
	const FGameplayTag InteractionTag,
	const FGameplayTag ReasonTag)
{
	++CancelledCount;
	LastCancelReason = ReasonTag;
	LastCancelledInteractionId = InteractionId;
}

void UPEPInteractionTestListener::HandleSelectionChanged(
	AActor* Target,
	const TArray<FGameplayTag>& Options)
{
	++SelectionChangedCount;
}
