#include "Item/Fragments/PEPItemFragment_WorldPresentation.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

void FPEPItemFragment_WorldPresentation::BuildDefaultStates(
	TArray<FInstancedStruct>& OutStates) const
{
	FPEPItemState_WorldPresentation State;
	State.MeshOverride = DefaultMeshOverride;
	State.Cosmetics = DefaultCosmetics;
	OutStates.Add(FInstancedStruct::Make(State));
}

void FPEPItemFragment_WorldPresentation::GetOwnedStateStructs(
	TArray<const UScriptStruct*>& OutStateStructs) const
{
	OutStateStructs.Add(FPEPItemState_WorldPresentation::StaticStruct());
}

#if WITH_EDITOR
namespace
{
	template <typename TParameter>
	void ValidateMaterialParameters(
		const TArray<TParameter>& Parameters,
		const TCHAR* ParameterKind,
		FDataValidationContext& Context,
		EDataValidationResult& Result)
	{
		TSet<FString> SeenParameters;
		for (int32 Index = 0; Index < Parameters.Num(); ++Index)
		{
			const TParameter& Parameter = Parameters[Index];
			if (Parameter.ParameterName.IsNone() || Parameter.MaterialIndex < INDEX_NONE)
			{
				Context.AddError(FText::FromString(FString::Printf(
					TEXT("%s parameter %d needs a name and a material index >= -1."),
					ParameterKind,
					Index)));
				Result = EDataValidationResult::Invalid;
				continue;
			}

			const FString Key = FString::Printf(
				TEXT("%d:%s"),
				Parameter.MaterialIndex,
				*Parameter.ParameterName.ToString());
			if (SeenParameters.Contains(Key))
			{
				Context.AddError(FText::FromString(FString::Printf(
					TEXT("%s parameter '%s' is duplicated for material index %d."),
					ParameterKind,
					*Parameter.ParameterName.ToString(),
					Parameter.MaterialIndex)));
				Result = EDataValidationResult::Invalid;
			}
			SeenParameters.Add(Key);
		}
	}
}

EDataValidationResult FPEPItemFragment_WorldPresentation::ValidateFragmentData(
	FDataValidationContext& Context,
	const int32 FragmentIndex) const
{
	EDataValidationResult Result = FPEPItemFragment::ValidateFragmentData(Context, FragmentIndex);
	TSet<int32> SeenMaterialIndices;
	for (int32 Index = 0; Index < DefaultCosmetics.MaterialOverrides.Num(); ++Index)
	{
		const FPEPWorldMaterialOverride& Override = DefaultCosmetics.MaterialOverrides[Index];
		if (Override.MaterialIndex < 0 || Override.Material.IsNull())
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Material override %d needs a non-negative index and a material."),
				Index)));
			Result = EDataValidationResult::Invalid;
		}
		if (SeenMaterialIndices.Contains(Override.MaterialIndex))
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Material index %d is overridden more than once."),
				Override.MaterialIndex)));
			Result = EDataValidationResult::Invalid;
		}
		SeenMaterialIndices.Add(Override.MaterialIndex);
	}

	ValidateMaterialParameters(
		DefaultCosmetics.ScalarParameters,
		TEXT("Scalar"),
		Context,
		Result);
	ValidateMaterialParameters(
		DefaultCosmetics.VectorParameters,
		TEXT("Vector"),
		Context,
		Result);
	ValidateMaterialParameters(
		DefaultCosmetics.TextureParameters,
		TEXT("Texture"),
		Context,
		Result);
	return Result;
}
#endif
