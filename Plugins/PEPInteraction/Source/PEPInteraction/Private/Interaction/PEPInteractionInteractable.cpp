#include "Interaction/PEPInteractionInteractable.h"

float IPEPInteractionInteractable::GetInteractionDuration_Implementation(
	AActor* Interactor,
	const FGameplayTag InteractionTag) const
{
	return 0.0f;
}

void IPEPInteractionInteractable::OnInteractionStarted_Implementation(
	AActor* Interactor,
	const FGameplayTag InteractionTag)
{
}

void IPEPInteractionInteractable::OnInteractionCancelled_Implementation(
	AActor* Interactor,
	const FGameplayTag InteractionTag,
	const FGameplayTag ReasonTag)
{
}
