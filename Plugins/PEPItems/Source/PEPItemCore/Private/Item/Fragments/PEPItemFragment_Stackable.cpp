#include "Item/Fragments/PEPItemFragment_Stackable.h"

#include "Item/Internal/PEPItemMutableAccess.h"
#include "Item/PEPItemScalarTypes.h"
#include "Item/PEPItemSettings.h"
#include "Item/PEPItemValueTags.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace PEPItemStackScalarPrivate
{
	bool ReadStackCount(
		const FPEPItemInstance& Item,
		int32& OutValue)
	{
		const FPEPItemState_Stack* State =
			Item.GetState<FPEPItemState_Stack>();

		if (!State)
		{
			return false;
		}

		OutValue = State->Count;
		return true;
	}

	bool WriteStackCount(
		FPEPItemInstance& Item,
		const int32 NewValue)
	{
		if (NewValue < 0)
		{
			return false;
		}

		const FPEPItemFragment_Stackable* Fragment =
			Item.GetDefinitionFragment<FPEPItemFragment_Stackable>();

		if (!Fragment
			|| NewValue > Fragment->GetEffectiveMaxStackSize())
		{
			return false;
		}

		FPEPItemState_Stack* State =
			FPEPItemMutableAccess::GetMutableState<FPEPItemState_Stack>(Item);

		if (!State)
		{
			return false;
		}

		State->Count = NewValue;
		return true;
	}
}

EPEPStateStackability FPEPItemState_Stack::GetStackability() const
{
	return EPEPStateStackability::StackCount;
}

bool FPEPItemState_Stack::IsDepleted() const
{
	return Count <= 0;
}

int32 FPEPItemFragment_Stackable::GetEffectiveMaxStackSize() const
{
	return PEPItemSettings().GetEffectiveMaxStackSize(MaxStackSize, bUseProjectDefaultMaxStackSize);
}

void FPEPItemFragment_Stackable::BuildDefaultStates(TArray<FInstancedStruct>& OutStates) const
{
	FPEPItemState_Stack* StackState = FindOrAddDefaultState<FPEPItemState_Stack>(OutStates);
	if (StackState)
	{
		StackState->Count = 1;
	}
}

void FPEPItemFragment_Stackable::GetOwnedStateStructs(TArray<const UScriptStruct*>& OutStateStructs) const
{
	OutStateStructs.Add(FPEPItemState_Stack::StaticStruct());
}

bool FPEPItemFragment_Stackable::TryGetIntValueAccessor(
	const FGameplayTag ValueTag,
	FPEPItemIntValueAccessor& OutAccessor) const
{
	if (ValueTag != PEPItemValueTags::Stack_Count)
	{
		return false;
	}

	OutAccessor = FPEPItemIntValueAccessor();
	OutAccessor.bHasDefinitionValue = true;
	OutAccessor.DefinitionValue = 1;
	OutAccessor.bHasMinValue = true;
	OutAccessor.MinValue = 0;
	OutAccessor.bHasMaxValue = true;
	OutAccessor.MaxValue = GetEffectiveMaxStackSize();
	OutAccessor.ReadRuntimeValue = &PEPItemStackScalarPrivate::ReadStackCount;
	OutAccessor.WriteRuntimeValue = &PEPItemStackScalarPrivate::WriteStackCount;
	return true;
}

#if WITH_EDITOR
EDataValidationResult FPEPItemFragment_Stackable::ValidateFragmentData(
	FDataValidationContext& Context,
	const int32 FragmentIndex) const
{
	const int32 EffectiveMaxStackSize = GetEffectiveMaxStackSize();
	if (EffectiveMaxStackSize <= 0)
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("DefinitionFragments[%d].Stackable effective MaxStackSize must be greater than zero."),
			FragmentIndex)));
		return EDataValidationResult::Invalid;
	}
	return EDataValidationResult::Valid;
}
#endif
