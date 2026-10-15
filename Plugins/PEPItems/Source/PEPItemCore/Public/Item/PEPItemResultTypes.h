#pragma once

#include "CoreMinimal.h"
#include "Core/PEPTypes.h"
#include "GameplayTagContainer.h"
#include "PEPItemResultTypes.generated.h"

/**
 * Item-domain operation result for item rules and mutations.
 * Inventory, equipment, weapon, and action layers should convert this at their
 * own module boundaries instead of exposing item internals directly.
 *
 * Relationship to PEPCore's FPEPOperationResult: this is that result plus the item it concerns.
 * bSuccess and ReasonTag carry the same meaning and the same canonical PEPCoreTags reasons, so
 * ToOperationResult() is lossless apart from ItemId. The two types stay separate (rather than one
 * deriving from the other) because both are reflected and widely used by value.
 *
 * Note the factory argument order differs: Success(ItemId, Reason) but Failure(Reason, ItemId).
 * It is kept as is: changing it would silently swap arguments at many call sites.
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

	/** Drops the item identity for infrastructure-level consumers that only need outcome and reason. */
	FPEPOperationResult ToOperationResult() const
	{
		return bSuccess
			? FPEPOperationResult::Success(ReasonTag)
			: FPEPOperationResult::Failure(ReasonTag);
	}
};
