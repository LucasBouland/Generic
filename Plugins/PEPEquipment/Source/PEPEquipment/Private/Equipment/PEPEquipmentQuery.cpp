#include "Equipment/PEPEquipmentQuery.h"

#include "Item/Fragments/PEPItemFragment_Equippable.h"

bool UPEPEquipmentQueryLibrary::IsItemEquippable(const FPEPItemInstance& Item)
{
	return Item.GetDefinitionFragment<FPEPItemFragment_Equippable>() != nullptr;
}
