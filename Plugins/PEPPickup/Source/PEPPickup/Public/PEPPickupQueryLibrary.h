#pragma once

#include "CoreMinimal.h"
#include "Item/PEPItemDefinition.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PEPPickupQueryLibrary.generated.h"

UCLASS()
class PEPPICKUP_API UPEPPickupQueryLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="PEP|Pickup")
	static bool CanPickupByDefault(const UPEPItemDefinition* Definition);
};
