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
 *
 * The raw value is private: gameplay code receives ids and compares them, it does not make them.
 * GetValue() reads it (logs, deterministic ordering); FromRawValue() is for id allocators, save
 * restoration and tests. Blueprint still reads Value through the struct's break node.
 */
USTRUCT(BlueprintType)
struct PEPCORE_API FPEPItemId
{
	GENERATED_BODY()

	/** Wraps a raw value. Only id allocators, save restoration and tests should need this. */
	static FPEPItemId FromRawValue(const int32 InValue)
	{
		FPEPItemId Id;
		Id.Value = InValue;
		return Id;
	}

	int32 GetValue() const
	{
		return Value;
	}

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

private:
	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item", meta=(AllowPrivateAccess="true"))
	int32 Value = INDEX_NONE;
};

FORCEINLINE uint32 GetTypeHash(const FPEPItemId& Id)
{
	return GetTypeHash(Id.GetValue());
}

/**
 * Generic PEP operation result for infrastructure-level operations that need a reason tag.
 * Domain results carry the same outcome and reason plus their own context (FPEPItemResult adds the
 * item id) and convert down to this type, e.g. FPEPItemResult::ToOperationResult(); PEPCore never
 * depends on them.
 */
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
