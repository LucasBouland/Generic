#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemResultTypes.h"
#include "UObject/Interface.h"

#include "PEPEquipmentEligibilityProvider.generated.h"

class UPEPEquipmentComponent;
class UPEPItemDefinition;

UINTERFACE(MinimalAPI, BlueprintType, Blueprintable)
class UPEPEquipmentEligibilityProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Extra rule deciding whether an item may occupy an equipment slot, on top of the slot's own
 * Required/Blocked item tags.
 *
 * UPEPEquipmentComponent asks every object implementing this on its owner — the owner actor
 * itself, then its components — and the first failure wins. It does so in PreviewEquipItem, in
 * the authoritative equip path and in the location validator it registers on the item store, so a
 * move into a slot coming from an inventory UI is held to the same rule as an equip request.
 *
 * Implementations must be pure queries: they are called on clients for previews, may be called
 * several times for one request, and never from a GMC prediction path.
 */
class PEPEQUIPMENT_API IPEPEquipmentEligibilityProvider
{
	GENERATED_BODY()

public:
	/**
	 * @param Equipment   The component asking. Read-only.
	 * @param Item        The item that would occupy the slot.
	 * @param Definition  The item's definition if already resident, else null: eligibility never
	 *                    triggers a load. A provider that needs it should refuse when it is null
	 *                    only if refusing is the safe answer for its rule.
	 * @param SlotTag     The equipment slot being entered.
	 * @return Success to allow; a failure with a Reason tag to refuse.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="PEP|Equipment|Eligibility")
	FPEPItemResult CanEquipItem(
		const UPEPEquipmentComponent* Equipment,
		const FPEPItemInstance& Item,
		const UPEPItemDefinition* Definition,
		FGameplayTag SlotTag) const;

	/** Allows by default, so an implementer only overrides the rule it actually has. */
	virtual FPEPItemResult CanEquipItem_Implementation(
		const UPEPEquipmentComponent* Equipment,
		const FPEPItemInstance& Item,
		const UPEPItemDefinition* Definition,
		FGameplayTag SlotTag) const;
};
