#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Interaction/PEPInteractionTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "PEPInteractionLibrary.generated.h"

/**
 * Blueprint helpers for interactables.
 *
 * FPEPInteractionResult is read-only in Blueprint, so a Blueprint interactable builds the values
 * its CanInteract and ExecuteInteraction return with these. Identity left empty (no target, no
 * option) is completed by the interaction component from the request; the request id is always
 * the component's own.
 */
UCLASS()
class PEPINTERACTION_API UPEPInteractionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The interaction is allowed (CanInteract) or was carried out (ExecuteInteraction). */
	UFUNCTION(BlueprintPure, Category="PEP|Interaction")
	static FPEPInteractionResult MakeInteractionSuccess(
		AActor* Target,
		UPARAM(meta=(Categories="Interaction")) FGameplayTag InteractionTag);

	/** The interaction is refused or failed; ReasonTag says why (Reason.BlockedByState, ...). */
	UFUNCTION(BlueprintPure, Category="PEP|Interaction")
	static FPEPInteractionResult MakeInteractionFailure(
		UPARAM(meta=(Categories="Reason")) FGameplayTag ReasonTag,
		AActor* Target,
		UPARAM(meta=(Categories="Interaction")) FGameplayTag InteractionTag);
};
