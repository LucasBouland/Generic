#pragma once

#include "CoreMinimal.h"
#include "Equipment/PEPEquipmentTypes.h"
#include "GameplayTagContainer.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemResultTypes.h"
#include "Item/PEPItemStore.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PEPItemUILibrary.h"
#include "PEPUIDropTargetStyle.h"

#include "PEPEquipmentUILibrary.generated.h"

class UPEPEquipmentComponent;

/** Update of the root item image, distinct from changes in its nested contents. */
UENUM(BlueprintType)
enum class EPEPEquipmentSlotItemUpdate : uint8
{
	None,
	Clear,
	Refresh
};

/** What an equipment UI shows about a slot itself, whatever occupies it. */
USTRUCT(BlueprintType)
struct PEPEQUIPMENTUI_API FPEPEquipmentSlotDisplayData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="PEP|Equipment|UI", meta=(Categories="Equipment.Slot"))
	FGameplayTag SlotTag;

	/** Whether the component's effective profile declares the slot. Otherwise only Label is set. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Equipment|UI")
	bool bDeclared = false;

	/** The profile entry's DisplayName, or the last part of SlotTag when it has none. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Equipment|UI")
	FText Label;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Equipment|UI")
	EPEPEquipmentHand Hand = EPEPEquipmentHand::None;

	/** False for a slot hidden from other players. Its owner still sees it and it still grants. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Equipment|UI")
	bool bShowPublicly = true;
};

/**
 * Presentation helpers for equipment UI. Pure reads: nothing here moves an item or decides whether
 * a move is allowed — that is UPEPEquipmentComponent's (PreviewEquipItem, RequestEquipItem).
 */
UCLASS()
class PEPEQUIPMENTUI_API UPEPEquipmentUILibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * True if this change affects SlotTag, including nested contents. Exact tag comparison.
	 * ItemUpdate applies only to the root item: None forwards to contents without replacing it.
	 * Clear removes the root image; Refresh displays Change.ItemInstance.
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment|UI")
	static bool FilterEquipmentSlotChange(
		const FPEPItemStoreChange& Change,
		FGameplayTag SlotTag,
		EPEPEquipmentSlotItemUpdate& ItemUpdate);

	/**
	 * SlotTag as the component's effective profile describes it: label, hand and public
	 * visibility. Without a component, or for a slot it does not declare, bDeclared is false and
	 * only the label (derived from the tag) is set.
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment|UI")
	static FPEPEquipmentSlotDisplayData GetEquipmentSlotDisplayData(
		const UPEPEquipmentComponent* Equipment,
		FGameplayTag SlotTag);

	/** The last part of SlotTag: "Equipment.Slot.Hand.Right" gives "Right". Empty when invalid. */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment|UI")
	static FText MakeDefaultSlotLabel(FGameplayTag SlotTag);

	/**
	 * The item to present for an equipped node: the full instance when this machine's store holds
	 * it (the authority, the owning client), else a snapshot with only ItemId and DefinitionId —
	 * all a public node says about another player's item. False for an invalid node.
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment|UI")
	static bool MakeEquippedItemSnapshot(
		const UPEPEquipmentComponent* Equipment,
		const FPEPEquippedItemInfo& Node,
		FPEPItemInstance& OutItem);

	/**
	 * PEPItemUI view data for an equipped node, from MakeEquippedItemSnapshot. False while the
	 * item's definition is not resident: it is never loaded here, the caller loads it and asks again.
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment|UI")
	static bool BuildEquippedItemViewData(
		const UPEPEquipmentComponent* Equipment,
		const FPEPEquippedItemInfo& Node,
		FPEPItemViewData& OutViewData);

	/**
	 * The PEPCoreUI drop target state for a drag over a slot: Hovered while there is no candidate
	 * item yet, then Valid or Invalid from the equipment's verdict (PreviewEquipItem).
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment|UI")
	static EPEPUIDropTargetVisualState GetDropTargetVisualState(
		bool bHasCandidate,
		const FPEPItemResult& Verdict);
};
