#include "Item/Fragments/PEPItemFragment_Container.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
bool FPEPContainerSlotDefinition::IsValid() const
{
	if (SlotName.IsNone()
		|| RequiredItemTags.HasAny(BlockedItemTags)
		|| (Purpose == EPEPContainerSlotPurpose::Attachment
			&& Mode != EPEPContainerSlotMode::Single))
	{
		return false;
	}

	return Mode == EPEPContainerSlotMode::Single
		|| (GridSize.X > 0 && GridSize.Y > 0);
}

void FPEPContainerSlotPreset::ApplyTo(FPEPContainerSlotDefinition& Slot) const
{
	Slot.bAllowRotation = bAllowRotation;
	Slot.RequiredItemTags = RequiredItemTags;
	Slot.BlockedItemTags = BlockedItemTags;
	Slot.OwnerVisibility = OwnerVisibility;
	Slot.OtherVisibility = OtherVisibility;
	Slot.bOwnerCanTake = bOwnerCanTake;
	Slot.bOwnerCanPut = bOwnerCanPut;
	Slot.bOwnerCanMoveInside = bOwnerCanMoveInside;
	Slot.bOtherCanTake = bOtherCanTake;
	Slot.bOtherCanPut = bOtherCanPut;
	Slot.bOtherCanMoveInside = bOtherCanMoveInside;
	Slot.bIncludeChildrenInExternalView = bIncludeChildrenInExternalView;
}

FPEPSlotAccessPolicy FPEPContainerSlotDefinition::GetPolicyFor(
	const EPEPAccessRole Role) const
{
	const bool bOwner = Role == EPEPAccessRole::Owner;

	FPEPSlotAccessPolicy Policy;
	Policy.bCanTake = bOwner ? bOwnerCanTake : bOtherCanTake;
	Policy.bCanPut = bOwner ? bOwnerCanPut : bOtherCanPut;
	Policy.bCanMoveInside = bOwner ? bOwnerCanMoveInside : bOtherCanMoveInside;
	Policy.Visibility = bOwner ? OwnerVisibility : OtherVisibility;

	return Policy;
}

void FPEPItemFragment_Container::BuildDefaultStates(TArray<FInstancedStruct>& OutStates) const
{
	FPEPItemState_ContainerRuntime* RuntimeState = FindOrAddDefaultState<
		FPEPItemState_ContainerRuntime>(OutStates);
	if (!RuntimeState)
	{
		return;
	}
	RuntimeState->bLocked = bStartsLocked;
}

void FPEPItemFragment_Container::GetOwnedStateStructs(TArray<const UScriptStruct*>& OutStateStructs) const
{
	OutStateStructs.Add(FPEPItemState_ContainerRuntime::StaticStruct());
}

const FPEPContainerSlotDefinition* FPEPItemFragment_Container::FindSlot(
	const FName SlotName) const
{
	return Slots.FindByPredicate(
		[SlotName](const FPEPContainerSlotDefinition& Slot)
		{
			return Slot.SlotName == SlotName;
		});
}
#if WITH_EDITOR
EDataValidationResult FPEPItemFragment_Container::ValidateFragmentData(
	FDataValidationContext& Context,
	const int32 FragmentIndex) const
{
	EDataValidationResult Result = EDataValidationResult::Valid;
	if (Slots.IsEmpty())
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("DefinitionFragments[%d].Container has no slots."),
			FragmentIndex)));
		Result = EDataValidationResult::Invalid;
	}
	TSet<FName> SeenSlotNames;
	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
	{
		const FPEPContainerSlotDefinition& Slot = Slots[SlotIndex];
		if (Slot.SlotName.IsNone())
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("DefinitionFragments[%d].Container.Slots[%d] has an invalid slot name."),
				FragmentIndex,
				SlotIndex)));
			Result = EDataValidationResult::Invalid;
		}
		else if (SeenSlotNames.Contains(Slot.SlotName))
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("DefinitionFragments[%d].Container.Slots[%d] duplicates slot name '%s'."),
				FragmentIndex,
				SlotIndex,
				*Slot.SlotName.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		SeenSlotNames.Add(Slot.SlotName);
		if (Slot.Mode == EPEPContainerSlotMode::Grid
			&& (Slot.GridSize.X <= 0 || Slot.GridSize.Y <= 0))
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("DefinitionFragments[%d].Container.Slots[%d] has invalid grid size %d x %d."),
				FragmentIndex,
				SlotIndex,
				Slot.GridSize.X,
				Slot.GridSize.Y)));
			Result = EDataValidationResult::Invalid;
		}
		if (Slot.Purpose == EPEPContainerSlotPurpose::Attachment
			&& Slot.Mode != EPEPContainerSlotMode::Single)
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("DefinitionFragments[%d].Container.Slots[%d] is an Attachment but does not use Single mode."),
				FragmentIndex,
				SlotIndex)));
			Result = EDataValidationResult::Invalid;
		}
		if (Slot.RequiredItemTags.HasAny(Slot.BlockedItemTags))
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("DefinitionFragments[%d].Container.Slots[%d] has overlapping required and blocked item tags."),
				FragmentIndex,
				SlotIndex)));
			Result = EDataValidationResult::Invalid;
		}
	}
	if (RequiredContainedItemTags.HasAny(BlockedContainedItemTags))
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("DefinitionFragments[%d].Container has overlapping required and blocked contained item tags."),
			FragmentIndex)));
		Result = EDataValidationResult::Invalid;
	}
	if (bStartsLocked && RequiredUnlockKeyTags.IsEmpty())
	{
		Context.AddWarning(FText::FromString(FString::Printf(
			TEXT("DefinitionFragments[%d].Container starts locked but has no required unlock key tags."),
			FragmentIndex)));
	}
	return Result;
}
#endif
