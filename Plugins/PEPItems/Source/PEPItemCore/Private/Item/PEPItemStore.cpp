#include "Item/PEPItemStore.h"

#include "Core/PEPCoreTags.h"

FPEPItemResult IPEPItemStore::RemoveItemTree(
	const FPEPItemId& ItemId,
	const FPEPItemAccessContext& Access)
{
	if (!FindItem(ItemId))
	{
		return FPEPItemResult::Failure(
			PEPCoreTags::Reason_InvalidTarget,
			ItemId);
	}

	/*
	 * The whole subtree is collected before anything is removed, because GetDirectChildren reads
	 * the store the removals are about to change under it.
	 *
	 * Visited is not defensive bookkeeping: a store is free to hold a shape it never validated,
	 * and a traversal trusting parent links would not terminate on a cycle. It also keeps an item
	 * reachable twice from being removed twice.
	 */
	TArray<FPEPItemId> Ordered;
	TSet<FPEPItemId> Visited;
	TArray<FPEPItemId> Pending;

	Visited.Add(ItemId);
	Pending.Add(ItemId);

	TArray<FPEPItemId> Children;

	while (!Pending.IsEmpty())
	{
		const FPEPItemId Current = Pending.Pop();
		Ordered.Add(Current);

		GetDirectChildren(Current, Children);

		for (const FPEPItemId& Child : Children)
		{
			bool bAlreadyVisited = false;
			Visited.Add(Child, &bAlreadyVisited);

			if (!bAlreadyVisited)
			{
				Pending.Add(Child);
			}
		}
	}

	/*
	 * Only one property of Ordered matters and the stack above guarantees it: a parent is appended
	 * before any of its children. Walking it backwards therefore reaches every child before the
	 * item holding it, which is the order that never leaves a location naming an absent parent.
	 */
	for (int32 Index = Ordered.Num() - 1; Index >= 0; --Index)
	{
		const FPEPItemResult Result = RemoveItem(Ordered[Index], Access);

		if (!Result.bSuccess)
		{
			return Result;
		}
	}

	return FPEPItemResult::Success(
		PEPCoreTags::Reason_None,
		ItemId);
}
