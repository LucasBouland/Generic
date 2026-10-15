#pragma once
#include "CoreMinimal.h"
#include "Item/PEPItemFragment.h"
#include "PEPItemFragment_Durability.generated.h"

USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemState_Durability : public FPEPItemStateFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Durability = 1.0f;
	bool IsBroken() const;
};

USTRUCT(BlueprintType, meta=(DisplayName="Durability"))
struct PEPITEMCORE_API FPEPItemFragment_Durability : public FPEPItemFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item")
	bool bUseProjectDefaultStartingDurability = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item",
		meta=(ClampMin="0.0", ClampMax="1.0", EditCondition="!bUseProjectDefaultStartingDurability"))
	float StartingDurability = 1.0f;
	virtual void BuildDefaultStates(TArray<FInstancedStruct>& OutStates) const override;
	virtual void GetOwnedStateStructs(TArray<const UScriptStruct*>& OutStateStructs) const override;
	virtual bool TryGetFloatValueAccessor(
		FGameplayTag ValueTag,
		FPEPItemFloatValueAccessor& OutAccessor) const override;
#if WITH_EDITOR
	virtual EDataValidationResult ValidateFragmentData(
		FDataValidationContext& Context,
		const int32 FragmentIndex) const override;
#endif
};
