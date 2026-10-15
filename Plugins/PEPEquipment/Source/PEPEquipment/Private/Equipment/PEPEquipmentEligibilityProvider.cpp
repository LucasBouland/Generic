#include "Equipment/PEPEquipmentEligibilityProvider.h"

FPEPItemResult IPEPEquipmentEligibilityProvider::CanEquipItem_Implementation(
	const UPEPEquipmentComponent* Equipment,
	const FPEPItemInstance& Item,
	const UPEPItemDefinition* Definition,
	const FGameplayTag SlotTag) const
{
	(void)Equipment;
	(void)Definition;
	(void)SlotTag;
	return FPEPItemResult::Success(Item.ItemId);
}
