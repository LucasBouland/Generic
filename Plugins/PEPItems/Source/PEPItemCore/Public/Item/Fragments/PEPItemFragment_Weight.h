#pragma once
#include "CoreMinimal.h"
#include "Item/PEPItemFragment.h"
#include "PEPItemFragment_Weight.generated.h"

/**
 * Per-unit mass. Static data only: weight never varies at runtime, so this fragment owns no state.
 * A definition without it falls back to the project default, so every item has a weight.
 */
USTRUCT(BlueprintType, meta=(DisplayName="Weight"))
struct PEPITEMCORE_API FPEPItemFragment_Weight : public FPEPItemFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item")
	bool bUseProjectDefaultUnitWeight = false;

	/** Mass of a single unit, in the project's chosen unit (kilograms by convention). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item",
		meta=(ClampMin="0.0", EditCondition="!bUseProjectDefaultUnitWeight"))
	float UnitWeight = 0.0f;

	float GetEffectiveUnitWeight() const;
#if WITH_EDITOR
	virtual EDataValidationResult ValidateFragmentData(
		FDataValidationContext& Context,
		int32 FragmentIndex) const override;
#endif
};
