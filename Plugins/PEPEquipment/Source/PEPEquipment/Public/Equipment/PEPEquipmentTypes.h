#pragma once

#include "CoreMinimal.h"
#include "Core/PEPTypes.h"
#include "GameplayTagContainer.h"
#include "Item/PEPItemResultTypes.h"

#include "PEPEquipmentTypes.generated.h"

UENUM(BlueprintType)
enum class EPEPEquipmentHand : uint8
{
	None,
	Left,
	Right
};

/** Determines how a root equipment slot materializes its local cosmetic presentation. */
UENUM(BlueprintType)
enum class EPEPEquipmentPresentationPolicy : uint8
{
	/** A carrier actor snapped to a socket: a rifle on the back, a lamp on the shoulder. */
	Attached,

	/**
	 * Replaces a registered body part's mesh. This is the modular-character policy: a bare torso
	 * becomes a clothed torso, and there is only ever one mesh on that component.
	 */
	ModularSkeletal,

	/**
	 * Adds a skeletal mesh over the body, sharing its skeleton and following its pose.
	 *
	 * Distinct from ModularSkeletal because a plate carrier is not the torso — it sits on top of
	 * whatever the character is already wearing, and replacing the body part would strip that
	 * away. Each item gets its own component, removed with the item.
	 */
	LayeredSkeletal
};

USTRUCT(BlueprintType)
struct PEPEQUIPMENT_API FPEPEquipmentRequestResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="PEP|Equipment|Request")
	int32 RequestId = 0;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Equipment|Request")
	FPEPItemResult Result;
};

USTRUCT(BlueprintType)
struct PEPEQUIPMENT_API FPEPEquipmentSlotDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment", meta=(Categories="Equipment.Slot"))
	FGameplayTag SlotTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment")
	FName AttachSocket;

	/** Attached roots use PresentationMesh when unset. Modular slots require a registered target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment",
		meta=(Categories="Equipment.PresentationTarget"))
	FGameplayTag PresentationTargetTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment")
	EPEPEquipmentPresentationPolicy PresentationPolicy =
		EPEPEquipmentPresentationPolicy::Attached;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment")
	EPEPEquipmentHand Hand = EPEPEquipmentHand::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment", meta=(Categories="Item"))
	FGameplayTagContainer RequiredItemTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment", meta=(Categories="Item"))
	FGameplayTagContainer BlockedItemTags;

	/**
	 * Whether a pickup with no explicit destination may land here.
	 *
	 * On by default: an equipment slot is a place things go, and a store with nothing equipped has
	 * no other place at all — a container has to be equipped before its grid exists, so a slot that
	 * refused auto-placement would leave the first pickup nowhere to go.
	 *
	 * What keeps this from putting a bandage on someone's head is the slot's Required/Blocked item
	 * tags, not this flag. Turn it off for a slot that must only ever be filled deliberately.
	 * Explicit equip requests are unaffected either way.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment")
	bool bAllowAutoEquipOnPickup = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment")
	bool bShowPublicly = true;

	bool IsValid() const
	{
		return SlotTag.IsValid();
	}
};
