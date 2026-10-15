#pragma once

#include "CoreMinimal.h"
#include "Core/PEPTypes.h"
#include "Item/PEPItemResultTypes.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemRules.h"

/**
 * Every entry point here is one shot: it re-runs its rule against live state, then writes.
 * Nothing accepts a pre-computed decision, so a caller cannot hand over a stale one.
 *
 * Deliberately C++ only. These functions mutate item state with no authority check of their own,
 * so they are not exposed to Blueprint: the owning component receives the intent by server RPC,
 * checks authority, and calls them. Expose Blueprint nodes there, not here.
 */
namespace PEPItemMutation
{
	/**
	 * @param RequestedTransferCount  0 means "as much as fits".
	 * @param OutTransferCount        How many units actually moved.
	 */
	PEPITEMCORE_API FPEPItemResult MergeStacks(
		FPEPItemInstance& SourceItem,
		FPEPItemInstance& TargetItem,
		int32 RequestedTransferCount,
		int32& OutTransferCount);

	/** Divides the unit count; all other state is copied unchanged to the new instance. */
	PEPITEMCORE_API FPEPItemResult SplitStack(
		FPEPItemInstance& SourceItem,
		int32 SplitCount,
		FPEPItemInstance& OutSplitItem);

	PEPITEMCORE_API bool DuplicateItemWithNewId(
		const FPEPItemInstance& SourceItem,
		FPEPItemInstance& OutDuplicateItem);

	PEPITEMCORE_API FPEPItemResult SetStackCount(
		FPEPItemInstance& Item,
		int32 NewCount);

	/**
	 * Takes Count units off the stack. Reaching zero succeeds and leaves the item depleted:
	 * check FPEPItemInstance::IsDepleted and destroy it. Taking more than is there fails.
	 *
	 * @param OutRemaining  Units left on the stack after the operation.
	 */
	PEPITEMCORE_API FPEPItemResult ConsumeFromStack(
		FPEPItemInstance& Item,
		int32 Count,
		int32& OutRemaining);

	/** Same contract as ConsumeFromStack, on the resource pool instead of the stack. */
	PEPITEMCORE_API FPEPItemResult ConsumeResource(
		FPEPItemInstance& Item,
		int32 Amount,
		int32& OutRemaining);

	PEPITEMCORE_API FPEPItemResult SetDurability(
		FPEPItemInstance& Item,
		float NewDurability);

	PEPITEMCORE_API FPEPItemResult DamageDurability(
		FPEPItemInstance& Item,
		float DamageAmount);

	PEPITEMCORE_API FPEPItemResult RepairDurability(
		FPEPItemInstance& Item,
		float RepairAmount);
}
