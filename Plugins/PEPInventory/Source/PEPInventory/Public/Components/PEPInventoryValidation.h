#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Item/PEPItemStore.h"

/**
 * Ties a root location tag to the domain that gave it meaning by claiming it.
 *
 * Public only because the store holds an array of them and the session copies that array into its
 * preview store; nothing outside PEPInventory builds or reads one.
 */
struct FPEPLocationValidatorBinding
{
	FGameplayTag RootLocationTag;
	FPEPItemLocationValidator Validator;
	bool bAllowAutoPlacement = true;

	/**
	 * Segments in RootLocationTag, counted once when the tag is claimed. Every named-slot check
	 * picks the deepest claim covering its tag, and deriving this from the tag's string there cost
	 * an allocation per binding per check.
	 */
	int32 TagDepth = 0;
};

/**
 * The geometry a grid placement rests on, with no store and no rules in it.
 *
 * Separated because it is the only part of validation that is decidable on its own: everything
 * else needs to know what is in the store, who is asking and what the slot allows, while these
 * three are arithmetic. Pulling them out is what makes them testable without an inventory, and it
 * keeps the arithmetic from being re-derived inline at each call site — which is how a comparison
 * ends up written `<=` in one place and `<` in another.
 */
namespace PEPInventoryValidation
{
	/** A footprint's extent once rotation is applied. Rotation swaps the axes and nothing else. */
	inline FIntPoint OrientedSize(const FIntPoint Size, const bool bRotated)
	{
		return bRotated ? FIntPoint(Size.Y, Size.X) : Size;
	}

	/** Whether a footprint of Size placed at Position lies wholly inside GridSize. */
	inline bool IsFootprintInsideGrid(
		const FIntPoint Position,
		const FIntPoint Size,
		const FIntPoint GridSize)
	{
		return Size.X > 0
			&& Size.Y > 0
			&& Position.X >= 0
			&& Position.Y >= 0
			&& Size.X <= GridSize.X
			&& Size.Y <= GridSize.Y
			&& Position.X <= GridSize.X - Size.X
			&& Position.Y <= GridSize.Y - Size.Y;
	}

	/**
	 * Whether two placed footprints share a cell.
	 *
	 * Half-open on both axes: a footprint occupies [Position, Position + Size), so two that merely
	 * touch along an edge do not overlap. Getting that boundary wrong in one direction loses a
	 * column of every grid; in the other it lets two items sit on the same cell.
	 */
	inline bool DoFootprintsOverlap(
		const FIntPoint FirstPosition,
		const FIntPoint FirstSize,
		const FIntPoint SecondPosition,
		const FIntPoint SecondSize)
	{
		return FirstPosition.X < SecondPosition.X + SecondSize.X
			&& FirstPosition.X + FirstSize.X > SecondPosition.X
			&& FirstPosition.Y < SecondPosition.Y + SecondSize.Y
			&& FirstPosition.Y + FirstSize.Y > SecondPosition.Y;
	}
}
