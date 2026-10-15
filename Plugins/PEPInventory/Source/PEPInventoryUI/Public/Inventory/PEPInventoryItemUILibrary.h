#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemLocation.h"
#include "Item/PEPItemQueryTypes.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "PEPInventoryItemUILibrary.generated.h"

/** Editable display snapshot only; changes never mutate inventory. Pixel values are UMG layout units, before viewport DPI scaling. */
USTRUCT(BlueprintType)
struct PEPINVENTORYUI_API FPEPContainerItemViewData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="PEP|Inventory|UI")
	FPEPItemId ItemId;
	UPROPERTY(BlueprintReadWrite, Category="PEP|Inventory|UI")
	FPrimaryAssetId DefinitionId;
	UPROPERTY(BlueprintReadWrite, Category="PEP|Inventory|UI")
	bool bDefinitionReady = false;
	UPROPERTY(BlueprintReadWrite, Category="PEP|Inventory|UI")
	FPEPItemDisplayData DisplayData;
	UPROPERTY(BlueprintReadWrite, Category="PEP|Inventory|UI")
	FIntPoint GridPosition = FIntPoint::ZeroValue;
	/** Occupied size, already swapped when rotated. */
	UPROPERTY(BlueprintReadWrite, Category="PEP|Inventory|UI")
	FIntPoint GridSize = FIntPoint::ZeroValue;
	UPROPERTY(BlueprintReadWrite, Category="PEP|Inventory|UI")
	FVector2D PixelPosition = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadWrite, Category="PEP|Inventory|UI")
	FVector2D PixelSize = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadWrite, Category="PEP|Inventory|UI")
	bool bRotated = false;
};

UCLASS()
class PEPINVENTORYUI_API UPEPInventoryItemUILibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Builds a destination only; placement must still be validated by the inventory. Same result
	 * as PEPInventoryGridLibrary's MakeGridLocation: the unrotated origin carries no placement, so
	 * leaving GridPosition at zero and bRotated off also addresses a single slot.
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI|Drag")
	static FPEPItemLocation MakeItemContainerLocation(FPEPItemId ParentItemId,
		FName ContainerSlotName, FIntPoint GridPosition, bool bRotated = false);

	/** Builds a named-slot destination; placement must still be validated by the inventory. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI|Drag",
		meta=(DisplayName="Make Item Named Slot Location"))
	static FPEPItemLocation MakeItemNamedSlotLocation(FGameplayTag SlotTag);

	/** False for a missing definition or slot; output is reset on failure. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI")
	static bool GetContainerSlotDefinition(const UPEPItemDefinition* Definition, FName SlotName,
		FPEPContainerSlotDefinition& OutSlot);

	/** Definition order is preserved. Attachment slots are excluded. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI")
	static TArray<FPEPContainerSlotDefinition> GetContainerContentSlots(const UPEPItemDefinition* Definition);

	/** Single slots occupy one cell. Invalid sizes return false and zero. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI")
	static bool CalculateContainerPixelSize(const FPEPContainerSlotDefinition& Slot, float CellSize,
		FVector2D& OutPixelSize);

	/**
	 * Connect Item and Location from Break PEPInventoryEntry. No synchronous asset loads.
	 * False means the snapshot cannot be laid out yet; do not apply its pixel values.
	 * This does not validate placement permissions, collisions or container bounds.
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI")
	static bool BuildContainerItemViewData(const FPEPItemInstance& Item, const FPEPItemLocation& Location,
		float CellSize, FPEPContainerItemViewData& OutData);
};
