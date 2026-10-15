#include "Item/PEPItemFragment.h"

void FPEPItemFragment::BuildDefaultStates(TArray<FInstancedStruct>& OutStates) const
{
}

void FPEPItemFragment::GetOwnedStateStructs(TArray<const UScriptStruct*>& OutStateStructs) const
{
}

bool FPEPItemFragment::TryGetIntValueAccessor(
	const FGameplayTag ValueTag,
	FPEPItemIntValueAccessor& OutAccessor) const
{
	return false;
}

bool FPEPItemFragment::TryGetFloatValueAccessor(
	const FGameplayTag ValueTag,
	FPEPItemFloatValueAccessor& OutAccessor) const
{
	return false;
}

#if WITH_EDITOR
EDataValidationResult FPEPItemFragment::ValidateFragmentData(
	FDataValidationContext& Context,
	const int32 FragmentIndex) const
{
	return EDataValidationResult::Valid;
}
#endif
