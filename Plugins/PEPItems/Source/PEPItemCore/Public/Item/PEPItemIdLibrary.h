#pragma once

#include "CoreMinimal.h"
#include "Core/PEPTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PEPItemIdLibrary.generated.h"

UCLASS()
class PEPITEMCORE_API UPEPItemIdLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Identity equality, including two unset IDs. */
	UFUNCTION(BlueprintPure, Category="PEP|Item|Id",
		meta=(DisplayName="Equal (PEPItemId)", CompactNodeTitle="==", Keywords="equal equality =="))
	static bool EqualEqual_ItemIdItemId(FPEPItemId A, FPEPItemId B);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Id",
		meta=(DisplayName="Not Equal (PEPItemId)", CompactNodeTitle="!=", Keywords="not equal inequality !="))
	static bool NotEqual_ItemIdItemId(FPEPItemId A, FPEPItemId B);
};
