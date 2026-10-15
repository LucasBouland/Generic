#pragma once
#include "Item/PEPContainerItemWorldActor.h"
#include "Interaction/PEPInteractionInteractable.h"
#include "PEPPickupContainerActor.generated.h"

UCLASS(BlueprintType, Blueprintable, ClassGroup=(PEP))
class PEPPICKUP_API APEPPickupContainerActor : public APEPContainerItemWorldActor, public IPEPInteractionInteractable
{
	GENERATED_BODY()
public:
	virtual TArray<FGameplayTag> GetInteractOptionTags_Implementation(AActor* Interactor) const override;
	virtual FPEPInteractionResult CanInteract_Implementation(AActor* Interactor, FGameplayTag InteractionTag) const override;
	virtual FPEPInteractionResult ExecuteInteraction_Implementation(AActor* Interactor, FGameplayTag InteractionTag) override;
protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Interaction", meta=(ClampMin="1.0"))
	float OpenDistance = 300.0f;
};
