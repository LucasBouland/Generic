#include "Item/Fragments/PEPItemFragment_InventoryShape.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"

EDataValidationResult FPEPItemFragment_InventoryShape::ValidateFragmentData(
	FDataValidationContext& Context,
	const int32 FragmentIndex) const
{
	if (Size.X <= 0 || Size.Y <= 0)
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("DefinitionFragments[%d].InventoryShape size must be positive. Current size: %d x %d."),
			FragmentIndex,
			Size.X,
			Size.Y)));
		return EDataValidationResult::Invalid;
	}
	return EDataValidationResult::Valid;
}
#endif
