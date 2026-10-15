#pragma once

#include "CoreMinimal.h"
#include "PEPInventoryQueryTypes.generated.h"

USTRUCT(BlueprintType)
struct PEPINVENTORY_API FPEPItemInventoryShapeData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="PEP|Inventory")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Inventory")
	FIntPoint Size = FIntPoint(1, 1);

	UPROPERTY(BlueprintReadOnly, Category="PEP|Inventory")
	bool bCanRotate = true;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Inventory")
	bool bUsesDefaultShape = true;
};
