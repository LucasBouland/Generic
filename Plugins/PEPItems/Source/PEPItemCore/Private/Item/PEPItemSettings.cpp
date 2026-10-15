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
	return FMath::Clamp(bUseProjectDefault ? DefaultStartingDurability : FragmentStartingDurability, 0.0f, 1.0f);
}

float UPEPItemSettings::GetEffectiveUnitWeight(const float FragmentUnitWeight,
                                               const bool bUseProjectDefault) const
{
	return FMath::Max(0.0f, bUseProjectDefault ? DefaultUnitWeight : FragmentUnitWeight);
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

