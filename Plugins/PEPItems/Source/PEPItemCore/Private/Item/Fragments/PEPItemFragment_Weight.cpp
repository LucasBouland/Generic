#include "Item/Fragments/PEPItemFragment_Weight.h"

#include "Item/PEPItemSettings.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

float FPEPItemFragment_Weight::GetEffectiveUnitWeight() const
{
	return PEPItemSettings().GetEffectiveUnitWeight(UnitWeight, bUseProjectDefaultUnitWeight);
}

#if WITH_EDITOR
EDataValidationResult FPEPItemFragment_Weight::ValidateFragmentData(
	FDataValidationContext& Context,
	const int32 FragmentIndex) const
{
	// The effective value is already clamped and NaN-safe, so check the authored one.
	const float AuthoredUnitWeight = bUseProjectDefaultUnitWeight
		? PEPItemSettings().DefaultUnitWeight
		: UnitWeight;
	if (!FMath::IsFinite(AuthoredUnitWeight) || AuthoredUnitWeight < 0.0f)
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("DefinitionFragments[%d].Weight effective UnitWeight must be finite and not negative."),
			FragmentIndex)));
		return EDataValidationResult::Invalid;
	}
	return EDataValidationResult::Valid;
}
#endif
