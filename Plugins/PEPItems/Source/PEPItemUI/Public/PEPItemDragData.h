#pragma once

#include "CoreMinimal.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemLocation.h"
#include "PEPItemDragData.generated.h"

/** Local UI drag payload. Does not move items or grant access to an inventory. */
USTRUCT(BlueprintType)
struct PEPITEMUI_API FPEPItemDragData
{
	GENERATED_BODY()

	/** Snapshot captured at drag start. The item identity is Item.ItemId. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PEP|Item|UI|Drag")
	FPEPItemInstance Item;

	/** Original location. Keep unchanged while moving or rotating the preview. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PEP|Item|UI|Drag")
	FPEPItemLocation SourceLocation;

	/** Pointer offset from the preview's top-left, in cells (fractional values allowed).
	 * Recalculate when changing the preview orientation if the grab point is to stay fixed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PEP|Item|UI|Drag")
	FVector2D GrabOffsetInCells = FVector2D::ZeroVector;

	/** Initialize from SourceLocation.bRotated at drag start; then change only this preview flag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PEP|Item|UI|Drag")
	bool bPreviewRotated = false;
};
