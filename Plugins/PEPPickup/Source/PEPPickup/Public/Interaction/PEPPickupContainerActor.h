#pragma once
#include "Item/Actor/PEPContainerItemWorldActor.h"
#include "Interaction/PEPInteractionInteractable.h"
#include "PEPPickupContainerActor.generated.h"

/**
 * A container on the ground: Open and Inspect through an inventory session, Pickup of the whole
 * tree. Options and checks follow UPEPPickupSettings like APEPPickupItemActor: Pickup is offered
 * and allowed only when CanPickupDefinition accepts the root's definition.
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup=(PEP))
class PEPPICKUP_API APEPPickupContainerActor : public APEPContainerItemWorldActor, public IPEPInteractionInteractable
{
	GENERATED_BODY()
public:
	virtual TArray<FGameplayTag> GetInteractOptionTags_Implementation(AActor* Interactor) const override;
	virtual FPEPInteractionResult CanInteract_Implementation(AActor* Interactor, FGameplayTag InteractionTag) const override;
	virtual FPEPInteractionResult ExecuteInteraction_Implementation(AActor* Interactor, FGameplayTag InteractionTag) override;
protected:
	/**
	 * Called on the authority once the whole tree is in the store. The transfer already emptied this
	 * actor's storage, like APEPPickupItemActor. The default destroys the empty actor; an emptied
	 * container would otherwise destroy itself on its next store change.
	 */
	UFUNCTION(BlueprintNativeEvent, Category="PEP|Interaction")
	void HandlePickedUp(AActor* Interactor);
	virtual void HandlePickedUp_Implementation(AActor* Interactor);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Interaction", meta=(ClampMin="1.0"))
	float OpenDistance = 300.0f;
};
