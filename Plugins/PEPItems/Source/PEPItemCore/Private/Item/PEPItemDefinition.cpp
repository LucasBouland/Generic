#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemFragment.h"
#include "Item/PEPItemSettings.h"
#include "Item/Fragments/PEPItemFragment_Resource.h"
#include "Item/Fragments/PEPItemFragment_Stackable.h"
#include "Item/Fragments/PEPItemFragment_WorldPresentation.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
const FPrimaryAssetType UPEPItemDefinition::ItemAssetType(TEXT("ItemDef"));

FPrimaryAssetId UPEPItemDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(ItemAssetType, GetFName());
}

bool UPEPItemDefinition::HasOwnedTag(const FGameplayTag Tag) const
{
	return Tag.IsValid() && OwnedTags.HasTag(Tag);
}

bool UPEPItemDefinition::HasOwnedTagExact(const FGameplayTag Tag) const
{
	return Tag.IsValid() && OwnedTags.HasTagExact(Tag);
}

bool UPEPItemDefinition::HasAnyOwnedTags(const FGameplayTagContainer& Tags) const
{
	return !Tags.IsEmpty() && OwnedTags.HasAny(Tags);
}

bool UPEPItemDefinition::HasAllOwnedTags(const FGameplayTagContainer& Tags) const
{
	return Tags.IsEmpty() || OwnedTags.HasAll(Tags);
}

void UPEPItemDefinition::BuildDefaultStateFragments(TArray<FInstancedStruct>& OutStates) const
{
	OutStates.Reset();
	for (const FInstancedStruct& DefEntry : DefinitionFragments)
	{
		if (const FPEPItemFragment* DefFragment = DefEntry.GetPtr<FPEPItemFragment>())
		{
			DefFragment->BuildDefaultStates(OutStates);
		}
	}
}
#if WITH_EDITOR
namespace PEPItemDefinitionValidation
{
	bool IsStructExactOrFamilyDuplicate(const UScriptStruct* Existing, const UScriptStruct* Candidate)
	{
		return Existing && Candidate && (Existing == Candidate || Existing->IsChildOf(Candidate) || Candidate->
			IsChildOf(Existing));
	}

	int32 FindConflictingFragmentIndex(const TArray<const UScriptStruct*>& SeenFragmentTypes,
	                                   const UScriptStruct* Candidate)
	{
		if (!Candidate)
		{
			return INDEX_NONE;
		}
		for (int32 Index = 0; Index < SeenFragmentTypes.Num(); ++Index)
		{
			if (IsStructExactOrFamilyDuplicate(SeenFragmentTypes[Index], Candidate))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	void MarkInvalid(EDataValidationResult& Result)
	{
		Result = EDataValidationResult::Invalid;
	}
}

EDataValidationResult UPEPItemDefinition::IsDataValid(FDataValidationContext& Context) const
{
	return ValidateForStartup(Context, true, true);
}

EDataValidationResult UPEPItemDefinition::ValidateForStartup(
	FDataValidationContext& Context,
	const bool bValidateDefinition,
	const bool bValidateFragments) const
{
	using PEPItemDefinitionValidation::MarkInvalid;

	EDataValidationResult Result = Super::IsDataValid(Context);
	TArray<const UScriptStruct*> SeenFragmentTypes;
	for (int32 Index = 0; Index < DefinitionFragments.Num(); ++Index)
	{
		const FInstancedStruct& Entry = DefinitionFragments[Index];
		const UScriptStruct* StructType = Entry.GetScriptStruct();
		if (!StructType || !StructType->IsChildOf(FPEPItemFragment::StaticStruct()))
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("DefinitionFragments[%d] must derive from FPEPItemFragment."),
				Index)));
			MarkInvalid(Result);
			continue;
		}
		if (bValidateDefinition)
		{
			const int32 ConflictingIndex = PEPItemDefinitionValidation::FindConflictingFragmentIndex(
				SeenFragmentTypes, StructType);
			if (ConflictingIndex != INDEX_NONE)
			{
				Context.AddError(FText::FromString(FString::Printf(
					TEXT(
						"DefinitionFragments[%d] conflicts with an existing fragment type in the same inheritance family: '%s'."),
					Index,
					*StructType->GetName())));
				MarkInvalid(Result);
				continue;
			}
			SeenFragmentTypes.Add(StructType);
		}
		const FPEPItemFragment* Fragment = Entry.GetPtr<FPEPItemFragment>();
		if (!Fragment)
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("DefinitionFragments[%d] could not be read as FPEPItemFragment."),
				Index)));
			MarkInvalid(Result);
			continue;
		}
		if (bValidateFragments &&
			Fragment->ValidateFragmentData(Context, Index) == EDataValidationResult::Invalid)
		{
			MarkInvalid(Result);
		}
	}
	if (bValidateDefinition)
	{
		TArray<const UScriptStruct*> SeenOwnedStateTypes;
		for (int32 Index = 0; Index < DefinitionFragments.Num(); ++Index)
		{
			const FPEPItemFragment* Fragment = DefinitionFragments[Index].GetPtr<FPEPItemFragment>();
			if (!Fragment)
			{
				continue;
			}

			TArray<const UScriptStruct*> OwnedByThis;
			Fragment->GetOwnedStateStructs(OwnedByThis);
			for (const UScriptStruct* Owned : OwnedByThis)
			{
				if (!Owned)
				{
					continue;
				}
				const int32 ConflictIdx = PEPItemDefinitionValidation::FindConflictingFragmentIndex(
					SeenOwnedStateTypes, Owned);
				if (ConflictIdx != INDEX_NONE)
				{
					Context.AddError(FText::FromString(FString::Printf(
						TEXT("DefinitionFragments[%d] claims state '%s' which is already owned by an earlier fragment."),
						Index, *Owned->GetName())));
					MarkInvalid(Result);
				}
				else
				{
					SeenOwnedStateTypes.Add(Owned);
				}
			}
		}

		// Per-unit state cannot be shared by a stack: a merge would hand every transferred unit the
		// target's value. Stackable items may only carry state that qualifies the whole stack.
		if (HasDefinitionFragment<FPEPItemFragment_Stackable>())
		{
			TArray<FInstancedStruct> DefaultStates;
			BuildDefaultStateFragments(DefaultStates);
			for (const FInstancedStruct& StateEntry : DefaultStates)
			{
				const FPEPItemStateFragment* State = StateEntry.GetPtr<FPEPItemStateFragment>();
				if (!State || State->GetStackability() != EPEPStateStackability::Divisible)
				{
					continue;
				}
				Context.AddError(FText::FromString(FString::Printf(
					TEXT("Stackable item definition carries per-unit state '%s'. Either remove the Stackable "
						"fragment, or make that state report EPEPStateStackability::StackIdentity."),
					*StateEntry.GetScriptStruct()->GetName())));
				MarkInvalid(Result);
			}
		}

		const UPEPItemSettings& ItemSettings = PEPItemSettings();
		const auto AddConfiguredWarning = [&](const FString& Message)
		{
			if (ItemSettings.bTreatItemValidationWarningsAsErrors)
			{
				Context.AddError(FText::FromString(Message));
				MarkInvalid(Result);
			}
			else
			{
				Context.AddWarning(FText::FromString(Message));
			}
		};

		if (ItemSettings.bWarnOnMissingItemIcon && ItemIcon.IsNull())
		{
			AddConfiguredWarning(TEXT("Item definition has no ItemIcon."));
		}
		if (ItemSettings.bWarnOnMissingWorldPresentation &&
			!HasDefinitionFragment<FPEPItemFragment_WorldPresentation>() &&
			!ItemSettings.bUseDefaultWorldPresentationWhenMissing)
		{
			AddConfiguredWarning(TEXT(
				"Item definition has no WorldPresentation fragment and no project default world presentation fallback."));
		}
	}

	return Result;
}
#endif
