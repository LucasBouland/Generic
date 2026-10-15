#pragma once

#include "CoreMinimal.h"
#include "Item/PEPItemLocation.h"
#include "Item/PEPItemPlacement.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "PEPInventoryGrid.generated.h"

/**
 * Where an item sits inside a grid slot: the cell under its top-left corner, and whether its
 * footprint is turned a quarter, which swaps its width and height.
 *
 * Carried by FPEPItemLocation::Placement for container slots whose mode is Grid; ItemCore holds
 * it without knowing what a grid is. The unrotated origin is spelled as no placement at all —
 * MakeGridLocation and SetGridPlacement write it that way — so a cell has one spelling and a
 * location built without a placement keeps meaning what it always did: validation reads a grid
 * slot location without a placement as the origin, unrotated. A single slot takes no placement.
 */
USTRUCT(BlueprintType)
struct PEPINVENTORY_API FPEPGridPlacement : public FPEPItemPlacement
{
	GENERATED_BODY()

	FPEPGridPlacement() = default;

	FPEPGridPlacement(const FIntPoint InPosition, const bool bInRotated)
		: Position(InPosition)
		, bRotated(bInRotated)
	{
	}

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Inventory|Grid")
	FIntPoint Position = FIntPoint::ZeroValue;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Inventory|Grid")
	bool bRotated = false;

	/** The placement a location without one stands for. */
	bool IsOrigin() const
	{
		return Position == FIntPoint::ZeroValue && !bRotated;
	}

	bool operator==(const FPEPGridPlacement& Other) const
	{
		return Position == Other.Position && bRotated == Other.bRotated;
	}

	bool operator!=(const FPEPGridPlacement& Other) const
	{
		return !(*this == Other);
	}
};

/* Location equality compares placements through FInstancedStruct, which asks the struct itself. */
template <>
struct TStructOpsTypeTraits<FPEPGridPlacement> : public TStructOpsTypeTraitsBase2<FPEPGridPlacement>
{
	enum
	{
		WithIdenticalViaEquality = true
	};
};

/**
 * Reading and writing grid placements on a location. Nothing here validates a placement against a
 * slot: the inventory does that when the location is used.
 */
namespace PEPInventoryGrid
{
	/** A container-slot location at Position; the unrotated origin carries no placement. */
	PEPINVENTORY_API FPEPItemLocation MakeGridLocation(
		FPEPItemId ParentItemId,
		FName SlotName,
		FIntPoint Position,
		bool bRotated = false);

	/**
	 * The grid placement Location carries, or null: none (the unrotated origin, read it with
	 * GetGridPlacement), another domain's placement, or a location that holds no placement.
	 */
	PEPINVENTORY_API const FPEPGridPlacement* FindGridPlacement(const FPEPItemLocation& Location);

	/** Cell and orientation as a grid reads them: without a grid placement, the unrotated origin. */
	PEPINVENTORY_API FPEPGridPlacement GetGridPlacement(const FPEPItemLocation& Location);

	/**
	 * Replaces Location's placement with this cell and orientation, or clears it for the unrotated
	 * origin. False, leaving Location untouched, unless it is a container-slot location.
	 */
	PEPINVENTORY_API bool SetGridPlacement(FPEPItemLocation& Location, FIntPoint Position, bool bRotated);

	/**
	 * Whether the inventory can read Location's placement at all: none, or a grid placement whose
	 * cell is not before a grid's first. Says nothing about the slot being a grid or the cell
	 * fitting it; ValidateLocationForItem decides that.
	 */
	PEPINVENTORY_API bool IsPlacementWellFormed(const FPEPItemLocation& Location);
}

UCLASS()
class PEPINVENTORY_API UPEPInventoryGridLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Builds a grid destination only; placement must still be validated by the inventory. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Grid")
	static FPEPItemLocation MakeGridLocation(
		FPEPItemId ParentItemId,
		FName SlotName,
		FIntPoint Position,
		bool bRotated = false);

	/**
	 * Cell and orientation of Location in its grid; a container-slot location without a placement
	 * is the unrotated origin. False, with the origin in the outputs, for a named slot or for a
	 * placement that is not a grid one.
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Grid")
	static bool GetGridPlacement(
		const FPEPItemLocation& Location,
		FIntPoint& OutPosition,
		bool& bOutRotated);

	/** Moves Location to another cell or orientation of the same slot. False unless it is a container-slot location. */
	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|Grid")
	static bool SetGridPlacement(
		UPARAM(ref) FPEPItemLocation& Location,
		FIntPoint Position,
		bool bRotated);
};
