#include "Item/PEPItemSettings.h"
#include "Item/Fragments/PEPItemFragment_WorldPresentation.h"
#include "Item/PEPItemQueryTypes.h"

int32 UPEPItemSettings::GetEffectiveMaxStackSize(const int32 FragmentMaxStackSize,
                                                    const bool bUseProjectDefault) const
{
	return FMath::Max(1, bUseProjectDefault ? DefaultMaxStackSize : FragmentMaxStackSize);
}

float UPEPItemSettings::GetEffectiveStartingDurability(const float FragmentStartingDurability,
                                                          const bool bUseProjectDefault) const
{
	const float Durability = bUseProjectDefault ? DefaultStartingDurability : FragmentStartingDurability;
	// Explicit rather than left to FMath::Clamp, whose answer for NaN depends on its comparison order.
	return FMath::IsFinite(Durability) ? FMath::Clamp(Durability, 0.0f, 1.0f) : 1.0f;
}

float UPEPItemSettings::GetEffectiveUnitWeight(const float FragmentUnitWeight,
                                               const bool bUseProjectDefault) const
{
	const float UnitWeight = bUseProjectDefault ? DefaultUnitWeight : FragmentUnitWeight;
	// FMath::Max(0, NaN) returns NaN, which would then poison every stack and carry weight sum.
	return FMath::IsFinite(UnitWeight) ? FMath::Max(0.0f, UnitWeight) : 0.0f;
}

bool UPEPItemSettings::GetEffectiveWorldPresentation(
	const FPEPItemFragment_WorldPresentation* Fragment,
	FPEPItemWorldPresentationData& OutData) const
{
	OutData = FPEPItemWorldPresentationData();
	if (Fragment)
	{
		OutData.bValid = true;
		OutData.StaticMesh = Fragment->StaticMesh;
		OutData.MeshTransform = Fragment->MeshTransform;
		if (!OutData.StaticMesh.ToSoftObjectPath().IsValid() && bUseDefaultWorldPresentationWhenMissing)
		{
			OutData.StaticMesh = DefaultWorldStaticMesh;
		}
		return true;
	}
	if (!bUseDefaultWorldPresentationWhenMissing)
	{
		return false;
	}
	OutData.bValid = DefaultWorldStaticMesh.IsValid() || DefaultWorldStaticMesh.ToSoftObjectPath().IsValid();
	OutData.StaticMesh = DefaultWorldStaticMesh;
	OutData.MeshTransform = DefaultWorldMeshTransform;
	return OutData.bValid;
}

