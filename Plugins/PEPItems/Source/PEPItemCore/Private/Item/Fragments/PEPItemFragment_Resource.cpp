#include "Item/Fragments/PEPItemFragment_Resource.h"

#include "Item/Internal/PEPItemMutableAccess.h"
#include "Item/PEPItemScalarTypes.h"
#include "Item/PEPItemValueTags.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace PEPItemResourceScalarPrivate
{
	bool ReadResource(
		const FPEPItemInstance& Item,
		int32& OutValue)
	{
		const FPEPItemState_Resource* State =
			Item.GetState<FPEPItemState_Resource>();

		if (!State)
		{
			return false;
		}

		OutValue = State->Current;
		return true;
	}

	bool WriteResource(
		FPEPItemInstance& Item,
		const int32 NewValue)
	{
		if (NewValue < 0)
		{
			return false;
		}

		const FPEPItemFragment_Resource* Fragment =
			Item.GetDefinitionFragment<FPEPItemFragment_Resource>();

		if (!Fragment || NewValue > Fragment->MaxResource)
		{
			return false;
		}

		FPEPItemState_Resource* State =
			FPEPItemMutableAccess::GetMutableState<FPEPItemState_Resource>(Item);

		if (!State)
		{
			return false;
		}

		State->Current = NewValue;
		return true;
	}
}

bool FPEPItemState_Resource::IsDepleted() const
{
	return Current <= 0;
}

void FPEPItemFragment_Resource::BuildDefaultStates(TArray<FInstancedStruct>& OutStates) const
{
	FPEPItemState_Resource* ResourceState = FindOrAddDefaultState<FPEPItemState_Resource>(OutStates);
	if (ResourceState)
	{
		ResourceState->Current = FMath::Max(0, MaxResource);
	}
}

void FPEPItemFragment_Resource::GetOwnedStateStructs(TArray<const UScriptStruct*>& OutStateStructs) const
{
	OutStateStructs.Add(FPEPItemState_Resource::StaticStruct());
}

bool FPEPItemFragment_Resource::TryGetIntValueAccessor(
	const FGameplayTag ValueTag,
	FPEPItemIntValueAccessor& OutAccessor) const
{
	if (ValueTag != PEPItemValueTags::Resource)
	{
		return false;
	}

	OutAccessor = FPEPItemIntValueAccessor();
	OutAccessor.bHasDefinitionValue = true;
	OutAccessor.DefinitionValue = MaxResource;
	OutAccessor.bHasMinValue = true;
	OutAccessor.MinValue = 0;
	OutAccessor.bHasMaxValue = true;
	OutAccessor.MaxValue = MaxResource;
	OutAccessor.ReadRuntimeValue = &PEPItemResourceScalarPrivate::ReadResource;
	OutAccessor.WriteRuntimeValue = &PEPItemResourceScalarPrivate::WriteResource;
	return true;
}

#if WITH_EDITOR
EDataValidationResult FPEPItemFragment_Resource::ValidateFragmentData(
	FDataValidationContext& Context,
	const int32 FragmentIndex) const
{
	if (MaxResource <= 0)
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("DefinitionFragments[%d].Resource MaxResource must be greater than zero."),
			FragmentIndex)));
		return EDataValidationResult::Invalid;
	}
	return EDataValidationResult::Valid;
}
#endif
