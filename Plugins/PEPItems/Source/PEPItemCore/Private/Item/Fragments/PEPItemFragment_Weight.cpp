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
	if (GetEffectiveUnitWeight() < 0.0f)
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("DefinitionFragments[%d].Weight effective UnitWeight must not be negative."),
			FragmentIndex)));
		return EDataValidationResult::Invalid;
	}
	return EDataValidationResult::Valid;
}
#endif
