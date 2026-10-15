#pragma once

#include "CoreMinimal.h"

#include "PEPItemPlacement.generated.h"

/**
 * Domain-defined position inside a container slot, interpreted by the store that owns the slot.
 *
 * ItemCore addresses a container slot by its parent item and slot name and stops there: whether
 * that slot is a grid, a list or a single socket belongs to the plugin defining it. That plugin
 * derives its own placement from this base — PEPInventory's grid cell and orientation is
 * FPEPGridPlacement — and FPEPItemLocation::Placement carries it without ItemCore reading it.
 *
 * A derived type lives in an FInstancedStruct, which saves and replicates its inner struct: mark
 * every persistent field SaveGame (an ArIsSaveGame archive skips the others, even in there), and
 * give the type an operator== with WithIdenticalViaEquality, since location equality compares
 * placements through the struct's own comparison.
 */
USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemPlacement
{
	GENERATED_BODY()
};
