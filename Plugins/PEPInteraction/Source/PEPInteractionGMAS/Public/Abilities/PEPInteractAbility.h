#pragma once

#include "CoreMinimal.h"
#include "Ability/GMCAbility.h"
#include "GameplayTagContainer.h"
#include "Interaction/PEPInteractionTypes.h"
#include "StructUtils/InstancedStruct.h"

#include "PEPInteractAbility.generated.h"

class UPEPInteractionComponent;

/** What the owning client asks the authority to interact with, sent as GMAS task target data. */
USTRUCT(BlueprintType)
struct PEPINTERACTIONGMAS_API FPEPInteractAbilityTargetData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="PEP|Interaction")
	TObjectPtr<AActor> Target = nullptr;

	UPROPERTY(
		BlueprintReadWrite,
		Category="PEP|Interaction",
		meta=(Categories="Interaction"))
	FGameplayTag InteractionTag;
};

/**
 * GMAS ability driving UPEPInteractionComponent: interact with the locally selected target.
 *
 * Ancillary (never activated in the movement tick, so never replayed). The flow, on every machine
 * running the ability so that GMAS task ids stay paired:
 * - the owning client (or listen-server host) sends its selected target and option through
 *   UGMCAbilityTask_SetTargetDataInstancedStruct;
 * - when that data arrives, the authority calls BeginInteractWithInteractionTag, which validates
 *   and either executes at once (instant option) or starts a timed interaction. Nothing else in
 *   this ability touches game state, and only the authority does that, outside any replay;
 * - with bCancelOnInputRelease, a UGMCAbilityTask_WaitForInputKeyRelease cancels a timed
 *   interaction when the ability's input is released;
 * - the ability ends on each machine when the component reports the interaction completed or
 *   cancelled there (authority directly, owning client through the component's client RPCs), when
 *   the input is released, or when the request was empty.
 */
UCLASS(Blueprintable, BlueprintType)
class PEPINTERACTIONGMAS_API UPEPInteractAbility : public UGMCAbility
{
	GENERATED_BODY()

public:
	UPEPInteractAbility();

	virtual void BeginAbility() override;

	virtual void EndAbility() override;

	virtual void CancelAbility() override;

	UFUNCTION(BlueprintPure, Category="PEP|Interaction")
	UPEPInteractionComponent* GetInteractionComponent() const;

protected:
	/** The interaction component used by this ability. Defaults to the one on the ability owner. */
	virtual UPEPInteractionComponent* ResolveInteractionComponent() const;

	/**
	 * The request the owning client sends. Defaults to the selected target and selected option.
	 * Also evaluated on the authority, whose copy is never sent.
	 */
	virtual FPEPInteractAbilityTargetData MakeLocalTargetData() const;

	/**
	 * Releasing the ability's input cancels a timed interaction and ends the ability.
	 * Ignored when the ability was not activated from an input action.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Interaction")
	bool bCancelOnInputRelease = true;

	/** Ending the ability for any other reason (cancelled by tag, owner gone) cancels its timed interaction. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Interaction")
	bool bCancelInteractionOnAbilityEnd = true;

private:
	UFUNCTION()
	void HandleTargetDataReceived(FInstancedStruct TargetData);

	UFUNCTION()
	void HandleInputReleased(float HeldDuration);

	UFUNCTION()
	void HandleInteractionCompleted(const FPEPInteractionResult& Result);

	UFUNCTION()
	void HandleInteractionCancelled(
		AActor* CancelledTarget,
		FGameplayTag InteractionTag,
		FGameplayTag ReasonTag);

	/* Authority, outside any GMC replay: the only place game state may change. */
	bool CanRunAuthorityLogic() const;

	/*
	 * Whether an outcome is about the request this ability sent. A null target on either side
	 * matches: a target destroyed by its own execution can arrive as null on the client.
	 */
	bool IsRequestedInteraction(
		const AActor* InTarget,
		FGameplayTag InInteractionTag) const;

	void BindInteractionEvents();

	void UnbindInteractionEvents();

	/* Authority: cancels the component's timed interaction if it is the one this ability requested. */
	void CancelRequestedInteraction(FGameplayTag ReasonTag);

	UPROPERTY(Transient)
	TObjectPtr<UPEPInteractionComponent> InteractionComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AActor> RequestedTarget = nullptr;

	UPROPERTY(Transient)
	FGameplayTag RequestedInteractionTag;

	bool bInteractionRequested = false;
};
