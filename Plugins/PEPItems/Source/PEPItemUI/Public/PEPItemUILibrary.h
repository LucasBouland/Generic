#pragma once

#include "CoreMinimal.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemQueryTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PEPItemUILibrary.generated.h"

USTRUCT(BlueprintType)
struct PEPITEMUI_API FPEPItemViewData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|UI")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|UI")
	FPEPItemId ItemId;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|UI")
	FPEPItemDisplayData DisplayData;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|UI")
	bool bHasStack = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|UI")
	FPEPItemStackData StackData;
};

UCLASS()
class PEPITEMUI_API UPEPItemUILibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Builds a presentation snapshot only. It never mutates or moves the item. */
	UFUNCTION(BlueprintPure, Category="PEP|Item|UI")
	static bool BuildItemViewData(const FPEPItemInstance& Item, FPEPItemViewData& OutData);
};
