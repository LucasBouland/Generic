#include "Equipment/PEPEquipmentEventTypes.h"

#include "Algo/Sort.h"

namespace
{
	using FPEPNodeIndex = TMap<FPEPItemId, const FPEPEquippedItemInfo*>;

	void IndexNodes(const TArray<FPEPEquippedItemInfo>& Nodes, FPEPNodeIndex& OutIndex)
	{
		OutIndex.Reset();
		OutIndex.Reserve(Nodes.Num());
		for (const FPEPEquippedItemInfo& Node : Nodes)
		{
			if (Node.ItemId.IsValid())
			{
				OutIndex.Add(Node.ItemId, &Node);
			}
		}
	}

	/** Grows Set with every node of Index whose ancestor is already in it. */
	void IncludeDescendants(const FPEPNodeIndex& Index, TSet<FPEPItemId>& Set)
	{
		bool bGrew = true;
		while (bGrew)
		{
			bGrew = false;
			for (const TPair<FPEPItemId, const FPEPEquippedItemInfo*>& Pair : Index)
			{
				const FPEPItemId ParentItemId = Pair.Value->ParentItemId;
				if (ParentItemId.IsValid() && !Set.Contains(Pair.Key) && Set.Contains(ParentItemId))
				{
					Set.Add(Pair.Key);
					bGrew = true;
				}
			}
		}
	}

	FPEPEquipmentChange DescribeChange(
		const FPEPNodeIndex& Index,
		const FPEPEquippedItemInfo& Node,
		const EPEPEquipmentChangeType ChangeType)
	{
		FPEPEquipmentChange Change;
		Change.ChangeType = ChangeType;
		Change.Node = Node;

		const FPEPEquippedItemInfo* Current = &Node;
		TSet<FPEPItemId> Visited;
		int32 Depth = 0;
		while (Current->ParentItemId.IsValid() && !Visited.Contains(Current->ItemId))
		{
			Visited.Add(Current->ItemId);
			const FPEPEquippedItemInfo* const* Parent = Index.Find(Current->ParentItemId);
			if (!Parent)
			{
				break;
			}
			Current = *Parent;
			++Depth;
		}
		Change.RootSlotTag = Current->IsAttachment() ? FGameplayTag() : Current->SlotTag;
		Change.Depth = Depth;
		return Change;
	}
}

bool PEPEquipmentEvents::IsSameEquippedNode(
	const FPEPEquippedItemInfo& A,
	const FPEPEquippedItemInfo& B)
{
	return A.ItemId == B.ItemId
		&& A.DefinitionId == B.DefinitionId
		&& A.SlotTag == B.SlotTag
		&& A.ParentItemId == B.ParentItemId
		&& A.ParentSlotName == B.ParentSlotName;
}

void PEPEquipmentEvents::DiffPublicProjection(
	const TArray<FPEPEquippedItemInfo>& Previous,
	const TArray<FPEPEquippedItemInfo>& Current,
	TArray<FPEPEquipmentChange>& OutUnequipped,
	TArray<FPEPEquipmentChange>& OutEquipped)
{
	OutUnequipped.Reset();
	OutEquipped.Reset();

	FPEPNodeIndex PreviousIndex;
	FPEPNodeIndex CurrentIndex;
	IndexNodes(Previous, PreviousIndex);
	IndexNodes(Current, CurrentIndex);

	TSet<FPEPItemId> Left;
	TSet<FPEPItemId> Entered;
	for (const TPair<FPEPItemId, const FPEPEquippedItemInfo*>& Pair : PreviousIndex)
	{
		const FPEPEquippedItemInfo* const* Now = CurrentIndex.Find(Pair.Key);
		if (!Now || !IsSameEquippedNode(*Pair.Value, **Now))
		{
			Left.Add(Pair.Key);
		}
	}
	for (const TPair<FPEPItemId, const FPEPEquippedItemInfo*>& Pair : CurrentIndex)
	{
		const FPEPEquippedItemInfo* const* Before = PreviousIndex.Find(Pair.Key);
		if (!Before || !IsSameEquippedNode(**Before, *Pair.Value))
		{
			Entered.Add(Pair.Key);
		}
	}
	IncludeDescendants(PreviousIndex, Left);
	IncludeDescendants(CurrentIndex, Entered);

	for (const FPEPItemId ItemId : Left)
	{
		OutUnequipped.Add(DescribeChange(
			PreviousIndex,
			*PreviousIndex.FindChecked(ItemId),
			EPEPEquipmentChangeType::Unequipped));
	}
	for (const FPEPItemId ItemId : Entered)
	{
		OutEquipped.Add(DescribeChange(
			CurrentIndex,
			*CurrentIndex.FindChecked(ItemId),
			EPEPEquipmentChangeType::Equipped));
	}

	Algo::Sort(OutUnequipped, [](const FPEPEquipmentChange& A, const FPEPEquipmentChange& B)
	{
		return A.Depth != B.Depth
			? A.Depth > B.Depth
			: A.Node.ItemId.GetValue() < B.Node.ItemId.GetValue();
	});
	Algo::Sort(OutEquipped, [](const FPEPEquipmentChange& A, const FPEPEquipmentChange& B)
	{
		return A.Depth != B.Depth
			? A.Depth < B.Depth
			: A.Node.ItemId.GetValue() < B.Node.ItemId.GetValue();
	});
}
