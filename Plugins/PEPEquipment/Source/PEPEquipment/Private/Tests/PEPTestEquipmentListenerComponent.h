#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Equipment/PEPEquipmentEligibilityProvider.h"
#include "Equipment/PEPEquipmentEventTypes.h"
#include "Equipment/PEPEquipmentTypes.h"

#include "PEPTestEquipmentListenerComponent.generated.h"

class UPEPEquipmentComponent;

/**
 * Test double: records typed equipment events and request resolutions in arrival order, and
 * refuses one slot as an eligibility provider when told to.
 */
UCLASS()
class UPEPTestEquipmentListenerComponent final
	: public UActorComponent
	, public IPEPEquipmentEligibilityProvider
{
	GENERATED_BODY()

public:
	void Listen(UPEPEquipmentComponent* Equipment);

	UFUNCTION()
	void HandleItemEquipped(const FPEPEquipmentChange& Change);

	UFUNCTION()
	void HandleItemUnequipped(const FPEPEquipmentChange& Change);

	UFUNCTION()
	void HandleRequestResolved(const FPEPEquipmentRequestResult& RequestResult);

	virtual FPEPItemResult CanEquipItem_Implementation(
		const UPEPEquipmentComponent* Equipment,
		const FPEPItemInstance& Item,
		const UPEPItemDefinition* Definition,
		FGameplayTag SlotTag) const override;

	TArray<FPEPEquipmentChange> Changes;
	TArray<FPEPEquipmentRequestResult> Resolutions;

	/** Refused when valid; every other slot is allowed. */
	FGameplayTag RefusedSlotTag;
	FGameplayTag RefusalReasonTag;
	mutable int32 EligibilityQueries = 0;
};
