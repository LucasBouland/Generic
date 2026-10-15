#include "Interaction/PEPInteractionLibrary.h"

FPEPInteractionResult UPEPInteractionLibrary::MakeInteractionSuccess(
	AActor* Target,
	const FGameplayTag InteractionTag)
{
	return FPEPInteractionResult::Success(
		Target,
		InteractionTag);
}

FPEPInteractionResult UPEPInteractionLibrary::MakeInteractionFailure(
	const FGameplayTag ReasonTag,
	AActor* Target,
	const FGameplayTag InteractionTag)
{
	return FPEPInteractionResult::Failure(
		ReasonTag,
		Target,
		InteractionTag);
}
