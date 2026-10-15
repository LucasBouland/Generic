#include "Input/PEPInputTagResolvers.h"

bool UPEPInputTagResolvers::IsNearlyValue(const float A, const float B, const float Tolerance)
{
	return FMath::Abs(A - B) <= Tolerance;
}

FGameplayTag UPEPInputTagResolvers::ResolveInputTagFromValue(const float RawValue,
                                                             const TArray<FPEPInputValueTagMapping>& Mappings,
                                                             const float Tolerance)
{
	const int32 RoundedValue = FMath::RoundToInt(RawValue);
	if (!IsNearlyValue(RawValue, static_cast<float>(RoundedValue), Tolerance))
	{
		return FGameplayTag();
	}
	for (const FPEPInputValueTagMapping& Mapping : Mappings)
	{
		if (Mapping.Value == RoundedValue)
		{
			return Mapping.InputTag;
		}
	}
	return FGameplayTag();
}
