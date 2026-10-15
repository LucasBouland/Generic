#pragma once
#include "CoreMinimal.h"
#include "Item/PEPItemFragment.h"
#include "PEPItemFragment_Stackable.generated.h"

USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemState_Stack : public FPEPItemStateFragment
{
	GENERATED_BODY()

public:
	/**
	 * Units in the stack. Zero is legal and means depleted (the owner destroys the item), so the
	 * editor range starts at zero like the runtime validation. EditAnywhere so presets can author
	 * an initial count; read-only to Blueprint, which changes state through the owning store.
	 */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadOnly, Category="PEP|Item", meta=(ClampMin="0"))
	int32 Count = 1;
	virtual EPEPStateStackability GetStackability() const override;
	virtual bool IsDepleted() const override;
};

USTRUCT(BlueprintType, meta=(DisplayName="Stackable"))
struct PEPITEMCORE_API FPEPItemFragment_Stackable : public FPEPItemFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item")
	bool bUseProjectDefaultMaxStackSize = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item",
		meta=(ClampMin="1", EditCondition="!bUseProjectDefaultMaxStackSize"))
	int32 MaxStackSize = 1;
	int32 GetEffectiveMaxStackSize() const;
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
