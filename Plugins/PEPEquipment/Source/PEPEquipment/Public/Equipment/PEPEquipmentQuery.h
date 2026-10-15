#pragma once

#include "CoreMinimal.h"
#include "Item/PEPItemInstance.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PEPEquipmentQuery.generated.h"

UCLASS()
class PEPEQUIPMENT_API UPEPEquipmentQueryLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** True when the item definition carries the Equipment-owned Equippable fragment. */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment|Query")
	static bool IsItemEquippable(const FPEPItemInstance& Item);
};
