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

	/*
	 * How long, in seconds, the interactor must keep interacting before ExecuteInteraction runs.
	 *
	 * 0 (the default) executes immediately. Read on the authority when the interaction starts;
	 * the value is then fixed for that interaction. Keep it a pure query.
	 */
	UFUNCTION(
		BlueprintCallable,
		BlueprintNativeEvent,
		Category="PEP|Interaction")
	float GetInteractionDuration(
		AActor* Interactor,
		FGameplayTag InteractionTag) const;
	virtual float GetInteractionDuration_Implementation(
		AActor* Interactor,
		FGameplayTag InteractionTag) const;

	/*
	 * Authority only: a timed interaction on this target has started. Does nothing by default.
	 * Exactly one of ExecuteInteraction or OnInteractionCancelled follows.
	 */
	UFUNCTION(
		BlueprintNativeEvent,
		Category="PEP|Interaction")
	void OnInteractionStarted(
		AActor* Interactor,
		FGameplayTag InteractionTag);
	virtual void OnInteractionStarted_Implementation(
		AActor* Interactor,
		FGameplayTag InteractionTag);

	/*
	 * Authority only: a timed interaction on this target stopped before completing.
	 * ReasonTag says why (Reason.Interaction.Cancelled, Reason.OutOfRange, ...). Does nothing by default.
	 */
	UFUNCTION(
		BlueprintNativeEvent,
		Category="PEP|Interaction")
	void OnInteractionCancelled(
		AActor* Interactor,
		FGameplayTag InteractionTag,
		FGameplayTag ReasonTag);
	virtual void OnInteractionCancelled_Implementation(
		AActor* Interactor,
		FGameplayTag InteractionTag,
		FGameplayTag ReasonTag);
};
