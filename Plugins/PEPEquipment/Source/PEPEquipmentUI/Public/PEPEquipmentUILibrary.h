#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Item/PEPItemStore.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "PEPEquipmentUILibrary.generated.h"

/** Update of the root item image, distinct from changes in its nested contents. */
UENUM(BlueprintType)
enum class EPEPEquipmentSlotItemUpdate : uint8
{
	None,
	Clear,
	Refresh
};

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
};
