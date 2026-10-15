#include "Equipment/PEPEquipmentProfile.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FPEPPresentationTargetDefinition* UPEPEquipmentProfile::FindPresentationTarget(
	const FGameplayTag TargetTag) const
{
	return PresentationTargets.FindByPredicate(
		[TargetTag](const FPEPPresentationTargetDefinition& Target)
		{
			return Target.TargetTag == TargetTag;
		});
}

#if WITH_EDITOR
EDataValidationResult UPEPEquipmentProfile::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	TSet<FGameplayTag> SeenSlots;
	TSet<FGameplayTag> SeenTargets;
	TSet<FGameplayTag> SeenModularTargets;
	TSet<EPEPEquipmentHand> SeenHands;

	for (int32 Index = 0; Index < PresentationTargets.Num(); ++Index)
	{
		const FPEPPresentationTargetDefinition& Target = PresentationTargets[Index];
		if (!Target.TargetTag.IsValid())
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("PresentationTargets[%d] has no TargetTag."), Index)));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		if (SeenTargets.Contains(Target.TargetTag))
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Presentation target '%s' is declared more than once."),
				*Target.TargetTag.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		SeenTargets.Add(Target.TargetTag);
	}

	if (Slots.IsEmpty())
	{
		Context.AddError(FText::FromString(TEXT("Equipment profile contains no slots.")));
		Result = EDataValidationResult::Invalid;
	}

	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FPEPEquipmentSlotDefinition& Slot = Slots[Index];
		if (!Slot.SlotTag.IsValid())
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Slots[%d] has no SlotTag."), Index)));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		if (SeenSlots.Contains(Slot.SlotTag))
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Slot '%s' is declared more than once."),
				*Slot.SlotTag.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		SeenSlots.Add(Slot.SlotTag);
		if (Slot.PresentationTargetTag.IsValid()
			&& !SeenTargets.Contains(Slot.PresentationTargetTag))
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Slot '%s' references undeclared presentation target '%s'."),
				*Slot.SlotTag.ToString(),
				*Slot.PresentationTargetTag.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		/*
		 * A slot with bShowPublicly off is not reported: it is a legitimate choice (a secure pouch
		 * nobody else should see) and no longer costs anything but its public presence. Its
		 * occupant still enters the owner view, raises OnOwnerItemEquipped and grants through
		 * PEPEquipmentGMAS; only the visual and the public events are withheld.
		 */
		if (Slot.Hand != EPEPEquipmentHand::None)
		{
			if (SeenHands.Contains(Slot.Hand))
			{
				Context.AddError(FText::FromString(FString::Printf(
					TEXT("Hand '%s' is assigned to more than one slot."),
					*UEnum::GetValueAsString(Slot.Hand))));
				Result = EDataValidationResult::Invalid;
			}
			SeenHands.Add(Slot.Hand);
		}

		if (Slot.PresentationPolicy == EPEPEquipmentPresentationPolicy::ModularSkeletal)
		{
			if (!Slot.PresentationTargetTag.IsValid())
			{
				Context.AddError(FText::FromString(FString::Printf(
					TEXT("Modular slot '%s' has no PresentationTargetTag."),
					*Slot.SlotTag.ToString())));
				Result = EDataValidationResult::Invalid;
			}
			else if (SeenModularTargets.Contains(Slot.PresentationTargetTag))
			{
				Context.AddError(FText::FromString(FString::Printf(
					TEXT("Modular target '%s' is driven by more than one slot."),
					*Slot.PresentationTargetTag.ToString())));
				Result = EDataValidationResult::Invalid;
			}
			SeenModularTargets.Add(Slot.PresentationTargetTag);

			if (const FPEPPresentationTargetDefinition* Target =
				FindPresentationTarget(Slot.PresentationTargetTag);
				Target && Target->ExpectedSkeleton.IsNull())
			{
				Context.AddWarning(FText::FromString(FString::Printf(
					TEXT("Modular target '%s' has no ExpectedSkeleton; skeleton compatibility cannot be validated."),
					*Slot.PresentationTargetTag.ToString())));
			}
		}
	}

	return Result;
}
#endif
