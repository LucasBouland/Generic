#pragma once
#include "CoreMinimal.h"
#include "Item/PEPItemFragment.h"
#include "PEPItemFragment_Resource.generated.h"

/**
 * A discrete pool consumed by use: medical charges, key uses, tool charges.
 * Distinct from durability, which is a 0..1 condition ratio rather than a countable reserve.
 */
USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemState_Resource : public FPEPItemStateFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item", meta=(ClampMin="0"))
	int32 Current = 0;

	virtual bool IsDepleted() const override;
};

USTRUCT(BlueprintType, meta=(DisplayName="Resource"))
struct PEPITEMCORE_API FPEPItemFragment_Resource : public FPEPItemFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item", meta=(ClampMin="1"))
	int32 MaxResource = 1;

	virtual void BuildDefaultStates(TArray<FInstancedStruct>& OutStates) const override;
	virtual void GetOwnedStateStructs(TArray<const UScriptStruct*>& OutStateStructs) const override;
	virtual bool TryGetIntValueAccessor(
		FGameplayTag ValueTag,
		FPEPItemIntValueAccessor& OutAccessor) const override;
#if WITH_EDITOR
	virtual EDataValidationResult ValidateFragmentData(
		FDataValidationContext& Context,
		int32 FragmentIndex) const override;
#endif
};
