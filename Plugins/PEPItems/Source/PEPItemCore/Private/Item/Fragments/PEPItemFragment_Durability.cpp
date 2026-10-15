#include "Item/Fragments/PEPItemFragment_Durability.h"

#include "Item/Internal/PEPItemMutableAccess.h"
#include "Item/PEPItemScalarTypes.h"
#include "Item/PEPItemSettings.h"
#include "Item/PEPItemValueTags.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace PEPItemDurabilityScalarPrivate
{
	bool ReadDurability(
		const FPEPItemInstance& Item,
		float& OutValue)
	{
		const FPEPItemState_Durability* State =
			Item.GetState<FPEPItemState_Durability>();

		if (!State)
		{
			return false;
		}

		OutValue = State->Durability;
		return true;
	}

	bool WriteDurability(
		FPEPItemInstance& Item,
		const float NewValue)
	{
		if (!FMath::IsFinite(NewValue)
			|| NewValue < 0.0f
			|| NewValue > 1.0f)
		{
			return false;
		}

		FPEPItemState_Durability* State =
			FPEPItemMutableAccess::GetMutableState<FPEPItemState_Durability>(Item);

		if (!State)
		{
			return false;
		}

		State->Durability = NewValue;
		return true;
	}
}

bool FPEPItemState_Durability::IsBroken() const
{
	return Durability <= 0.0f;
}

void FPEPItemFragment_Durability::BuildDefaultStates(TArray<FInstancedStruct>& OutStates) const
{
	FPEPItemState_Durability* DurabilityState = FindOrAddDefaultState<FPEPItemState_Durability>(OutStates);
	if (DurabilityState)
	{
		DurabilityState->Durability = PEPItemSettings().GetEffectiveStartingDurability(
			StartingDurability, bUseProjectDefaultStartingDurability);
	}
}

void FPEPItemFragment_Durability::GetOwnedStateStructs(TArray<const UScriptStruct*>& OutStateStructs) const
{
	OutStateStructs.Add(FPEPItemState_Durability::StaticStruct());
}

bool FPEPItemFragment_Durability::TryGetFloatValueAccessor(
	const FGameplayTag ValueTag,
	FPEPItemFloatValueAccessor& OutAccessor) const
{
	if (ValueTag != PEPItemValueTags::Durability)
	{
		return false;
	}

	OutAccessor = FPEPItemFloatValueAccessor();
	OutAccessor.bHasDefinitionValue = true;
	OutAccessor.DefinitionValue = PEPItemSettings().GetEffectiveStartingDurability(
		StartingDurability,
		bUseProjectDefaultStartingDurability);
	OutAccessor.bHasMinValue = true;
	OutAccessor.MinValue = 0.0f;
	OutAccessor.bHasMaxValue = true;
	OutAccessor.MaxValue = 1.0f;
	OutAccessor.ReadRuntimeValue = &PEPItemDurabilityScalarPrivate::ReadDurability;
	OutAccessor.WriteRuntimeValue = &PEPItemDurabilityScalarPrivate::WriteDurability;
	return true;
}

#if WITH_EDITOR
EDataValidationResult FPEPItemFragment_Durability::ValidateFragmentData(
	FDataValidationContext& Context,
	const int32 FragmentIndex) const
{
	// The effective value is already clamped and NaN-safe, so check the authored one: that is what
	// the designer has to fix.
	const float AuthoredStartingDurability = bUseProjectDefaultStartingDurability
		? PEPItemSettings().DefaultStartingDurability
		: StartingDurability;
	if (!FMath::IsFinite(AuthoredStartingDurability)
		|| AuthoredStartingDurability < 0.0f
		|| AuthoredStartingDurability > 1.0f)
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("DefinitionFragments[%d].Durability effective StartingDurability must be a finite value between 0.0 and 1.0."),
			FragmentIndex)));
		return EDataValidationResult::Invalid;
	}
	return EDataValidationResult::Valid;
}
#endif
