#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "PEPTypes.generated.h"

UENUM(BlueprintType)
enum class EPEPRequestResult : uint8
{
	Failed UMETA(DisplayName = "Failed"),
	Submitted UMETA(DisplayName = "Submitted"),
	Executed UMETA(DisplayName = "Executed")
};


/**
 * Stable identity contract shared by item, inventory, equipment and pickup domains.
 * Creation and persistence are owned by PEPItemCore; PEPCore only defines the shared value type.
 */
USTRUCT(BlueprintType)
struct PEPCORE_API FPEPItemId
{
	GENERATED_BODY()
	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item")
	int32 Value = INDEX_NONE;

	bool IsValid() const
	{
		return Value != INDEX_NONE;
	}

	void Reset()
	{
		Value = INDEX_NONE;
	}

	FString ToString() const
	{
		return FString::FromInt(Value);
	}

	friend bool operator==(const FPEPItemId& A, const FPEPItemId& B)
	{
		return A.Value == B.Value;
	}

	friend bool operator!=(const FPEPItemId& A, const FPEPItemId& B)
	{
		return !(A == B);
	}
};

FORCEINLINE uint32 GetTypeHash(const FPEPItemId& Id)
{
	return GetTypeHash(Id.Value);
}

/** Generic PEP operation result for infrastructure-level operations that need a reason tag. */
USTRUCT(BlueprintType)
struct PEPCORE_API FPEPOperationResult
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category="PEP|Result")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Result", meta=(Categories="Reason"))
	FGameplayTag ReasonTag;

	static FPEPOperationResult Success(const FGameplayTag InReasonTag = FGameplayTag())
	{
		FPEPOperationResult Result;
		Result.bSuccess = true;
		Result.ReasonTag = InReasonTag;
		return Result;
	}

	static FPEPOperationResult Failure(
		const FGameplayTag InReasonTag)
	{
		FPEPOperationResult Result;
		Result.bSuccess = false;
		Result.ReasonTag = InReasonTag;
		return Result;
	}
};
