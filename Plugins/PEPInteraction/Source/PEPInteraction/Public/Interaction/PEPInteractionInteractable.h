#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "PEPInteractionTypes.h"
#include "UObject/Interface.h"
#include "PEPInteractionInteractable.generated.h"

UINTERFACE()
class UPEPInteractionInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class PEPINTERACTION_API IPEPInteractionInteractable
{
	GENERATED_BODY()

public:
	/*
 * Options potentiellement disponibles.
 *
 * Cela ne remplace pas CanInteract : une option peut être exposée,
 * mais temporairement bloquée.
 */
	UFUNCTION(
		BlueprintCallable,
		BlueprintNativeEvent,
		Category="PEP|Interaction")
	TArray<FGameplayTag> GetInteractOptionTags(
		AActor* Interactor) const;

	/*
 * Validation métier de la cible.
 *
 * Exemple :
 * - objet déjà récupéré ;
 * - porte verrouillée ;
 * - inventaire absent ;
 * - interaction interdite par l'état courant.
 */
	UFUNCTION(
		BlueprintCallable,
		BlueprintNativeEvent,
		Category="PEP|Interaction")
	FPEPInteractionResult CanInteract(
		AActor* Interactor,
		FGameplayTag InteractionTag) const;

	UFUNCTION(
		BlueprintCallable,
		BlueprintNativeEvent,
		Category="PEP|Interaction")
	FPEPInteractionResult ExecuteInteraction(
		AActor* Interactor,
		FGameplayTag InteractionTag);
};
