#pragma once

#include "CoreMinimal.h"
#include "Ability/GMCAbilityMapData.h"
#include "GameplayTagContainer.h"
#include "Item/PEPItemFragment.h"

#include "PEPItemFragment_EquipmentAbilities.generated.h"

class UGMCAbilityEffect;

/**
 * What equipping this item gives its wearer through GMAS. Read by
 * UPEPEquipmentAbilityBridgeComponent, which must sit on the same pawn as the equipment and the
 * GMC ability system components.
 *
 * Granting follows the equipment's OWNER view: the item counts while it occupies one of the
 * profile's slots, or hangs from such an item as a visible attachment, and only then.
 *  - Whether the slot is shown publicly (bShowPublicly) makes no difference: a hidden slot grants
 *    like any other, it only keeps its visual and public events from other players.
 *  - A container's private content never grants anything.
 * Use ActiveInSlots to narrow where an item grants.
 */
USTRUCT(BlueprintType, meta=(DisplayName="Equipment Abilities (GMAS)"))
struct PEPEQUIPMENTGMAS_API FPEPItemFragment_EquipmentAbilities : public FPEPItemFragment
{
	GENERATED_BODY()

	/**
	 * Ability map entries added while equipped: InputTag -> ability classes. bGrantedByDefault
	 * means the authority grants InputTag for as long as the item is equipped; leave it off when
	 * the game grants the tag by its own rules.
	 *
	 * The entry is written to the local AbilityMap of both the server and the owning client, never
	 * granted locally. When two equipped items map the same InputTag, AbilityMapPriority picks the
	 * one that answers it; the other takes over when the first is unequipped.
	 */
	UPROPERTY(EditAnywhere, Category="PEP|Equipment|Abilities", meta=(TitleProperty="{InputTag}"))
	TArray<FAbilityMapData> AbilityMaps;

	/** Higher wins an InputTag shared with another equipped item. Ties go to the lower ItemId. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment|Abilities")
	int32 AbilityMapPriority = 0;

	/** Applied by the authority as ServerAuth effects while equipped, removed on unequip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment|Abilities")
	TArray<TSubclassOf<UGMCAbilityEffect>> EquippedEffects;

	/**
	 * Equipment slots in which the item grants anything, matched against the slot of the root it
	 * hangs under. Empty means any slot, shown publicly or not: set it to Hand slots for a weapon
	 * whose abilities should not follow it onto the back.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment|Abilities",
		meta=(Categories="Equipment.Slot"))
	FGameplayTagContainer ActiveInSlots;

	/** The wearer's GMAS active tags must contain all of these for the item to be equipped. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment|Eligibility")
	FGameplayTagContainer RequiredActiveTags;

	/** The item cannot be equipped while the wearer has any of these GMAS active tags. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment|Eligibility")
	FGameplayTagContainer BlockedActiveTags;

	/**
	 * Whether this item grants while it is an attachment (an optic on a rifle) rather than a slot
	 * occupant. Off by default: what a root carries is usually described by the root.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment|Abilities")
	bool bApplyWhenAttachment = false;

	bool HasEligibilityRequirements() const
	{
		return !RequiredActiveTags.IsEmpty() || !BlockedActiveTags.IsEmpty();
	}

	/** Whether a node with this root slot, at this depth, should grant at all. */
	bool AppliesTo(FGameplayTag RootSlotTag, bool bIsAttachment) const;

#if WITH_EDITOR
	virtual EDataValidationResult ValidateFragmentData(
		FDataValidationContext& Context,
		const int32 FragmentIndex) const override;
#endif
};
