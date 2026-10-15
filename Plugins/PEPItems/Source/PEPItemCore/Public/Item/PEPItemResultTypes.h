#pragma once

#include "CoreMinimal.h"
#include "Core/PEPTypes.h"
#include "GameplayTagContainer.h"
#include "PEPItemResultTypes.generated.h"

/**
 * Item-domain operation result for item rules and mutations.
 * Inventory, equipment, weapon, and action layers should convert this at their
 * own module boundaries instead of exposing item internals directly.
 */
USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemResult
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Result")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Result", meta=(Categories="Reason"))
	FGameplayTag ReasonTag;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Result")
	FPEPItemId ItemId;

	static FPEPItemResult Success(
		const FPEPItemId InItemId = FPEPItemId(),
		const FGameplayTag InReasonTag = FGameplayTag())
	{
		FPEPItemResult Result;
		Result.bSuccess = true;
		Result.ItemId = InItemId;
		Result.ReasonTag = InReasonTag;
		return Result;
	}

	static FPEPItemResult Failure(
		const FGameplayTag InReasonTag = FGameplayTag(),
		const FPEPItemId InItemId = FPEPItemId())
	{
		FPEPItemResult Result;
		Result.bSuccess = false;
		Result.ItemId = InItemId;
		Result.ReasonTag = InReasonTag;
		return Result;
	}
};
