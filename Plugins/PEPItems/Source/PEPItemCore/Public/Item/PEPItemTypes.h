#pragma once

#include "CoreMinimal.h"
#include "Core/PEPTypes.h"
#include "PEPItemTypes.generated.h"

USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemCount
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PEP|Item")
	FPEPItemId ItemId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PEP|Item", meta=(ClampMin="0"))
	int32 Count = 0;

	bool IsValid() const
	{
		return ItemId.IsValid() && Count > 0;
	}
};
