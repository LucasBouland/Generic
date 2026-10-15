#include "Item/Fragments/PEPItemFragment_EquipmentAbilities.h"

#include "Ability/GMCAbility.h"
#include "Effects/GMCAbilityEffect.h"

bool FPEPItemFragment_EquipmentAbilities::AppliesTo(
	const FGameplayTag RootSlotTag,
	const bool bIsAttachment) const
{
	if (bIsAttachment && !bApplyWhenAttachment)
	{
		return false;
	}
	return ActiveInSlots.IsEmpty() || (RootSlotTag.IsValid() && RootSlotTag.MatchesAny(ActiveInSlots));
}

#if WITH_EDITOR
EDataValidationResult FPEPItemFragment_EquipmentAbilities::ValidateFragmentData(
	FDataValidationContext& Context,
	const int32 FragmentIndex) const
{
	EDataValidationResult Result = EDataValidationResult::Valid;
	TSet<FGameplayTag> SeenInputTags;
	for (int32 Index = 0; Index < AbilityMaps.Num(); ++Index)
	{
		const FAbilityMapData& Entry = AbilityMaps[Index];
		if (!Entry.InputTag.IsValid())
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("DefinitionFragments[%d].AbilityMaps[%d] has no InputTag."),
				FragmentIndex,
				Index)));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		if (SeenInputTags.Contains(Entry.InputTag))
		{
			/* The ability map is keyed by InputTag: a second entry would silently replace the first. */
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("DefinitionFragments[%d].AbilityMaps maps InputTag '%s' twice."),
				FragmentIndex,
				*Entry.InputTag.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		SeenInputTags.Add(Entry.InputTag);
		if (Entry.Abilities.IsEmpty()
			|| Entry.Abilities.ContainsByPredicate([](const TSubclassOf<UGMCAbility>& Ability)
			{
				return Ability.Get() == nullptr;
			}))
		{
			Context.AddWarning(FText::FromString(FString::Printf(
				TEXT("DefinitionFragments[%d].AbilityMaps[%d] ('%s') has no ability or an empty ability class."),
				FragmentIndex,
				Index,
				*Entry.InputTag.ToString())));
		}
	}
	if (EquippedEffects.ContainsByPredicate([](const TSubclassOf<UGMCAbilityEffect>& Effect)
	{
		return Effect.Get() == nullptr;
	}))
	{
		Context.AddWarning(FText::FromString(FString::Printf(
			TEXT("DefinitionFragments[%d].EquippedEffects contains an empty effect class."),
			FragmentIndex)));
	}
	return Result;
}
#endif
