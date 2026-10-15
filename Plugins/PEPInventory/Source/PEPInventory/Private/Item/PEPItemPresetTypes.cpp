#include "Item/PEPItemPresetTypes.h"

#include "Item/PEPItemContainerRules.h"
#include "Inventory/PEPInventorySettings.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Item/Fragments/PEPItemFragment_InventoryShape.h"
#include "Item/Fragments/PEPItemFragment_Resource.h"

void FPEPItemInstanceTree::Reset()
{
	RootItem.Reset();
	Children.Reset();
}

bool FPEPItemInstanceTree::IsStructurallyValid() const
{
	if (!RootItem.IsValid())
	{
		return false;
	}

	TMap<FPEPItemId, int32> IndexById;
	IndexById.Reserve(Children.Num());
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		const FPEPItemInstanceTreeEntry& Entry = Children[Index];
		if (!Entry.Item.IsValid()
			|| Entry.Item.ItemId == RootItem.ItemId
			|| IndexById.Contains(Entry.Item.ItemId)
			|| !Entry.Location.IsStructurallyValid()
			|| Entry.Location.Type != EPEPItemLocationType::ItemContainer)
		{
			return false;
		}

		IndexById.Add(Entry.Item.ItemId, Index);
	}

	/*
	 * Every child must reach the root through parents in this tree, without a cycle. Each walk
	 * stops at the first entry already known to reach it and settles everything it passed, so an
	 * entry is walked through once in total: linear, where a fresh walk and a fresh visited set per
	 * child made a deep chain quadratic.
	 */
	enum class EWalkState : uint8
	{
		Unvisited,
		OnCurrentWalk,
		ReachesRoot
	};
	TArray<EWalkState> States;
	States.Init(EWalkState::Unvisited, Children.Num());
	TArray<int32> Walk;
	for (int32 Start = 0; Start < Children.Num(); ++Start)
	{
		Walk.Reset();
		int32 Current = Start;
		while (States[Current] != EWalkState::ReachesRoot)
		{
			/* Met again before reaching the root or a settled entry: a cycle. */
			if (States[Current] == EWalkState::OnCurrentWalk)
			{
				return false;
			}

			States[Current] = EWalkState::OnCurrentWalk;
			Walk.Add(Current);

			const FPEPItemId ParentId = Children[Current].Location.ParentItemId;
			if (ParentId == RootItem.ItemId)
			{
				break;
			}

			const int32* ParentIndex = IndexById.Find(ParentId);
			if (!ParentIndex)
			{
				return false;
			}
			Current = *ParentIndex;
		}

		for (const int32 Settled : Walk)
		{
			States[Settled] = EWalkState::ReachesRoot;
		}
	}

	return true;
}

bool FPEPItemInstanceTree::ValidateDetailed(FPEPItemInstanceTreeValidationResult& OutResult) const
{
	OutResult = FPEPItemInstanceTreeValidationResult();
	if (!IsStructurallyValid())
	{
		OutResult.AddError(FText::FromString(TEXT("The item tree has invalid IDs, parent links, or locations.")));
		return false;
	}

	TMap<FPEPItemId, const FPEPItemInstance*> ItemsById;
	ItemsById.Reserve(Children.Num() + 1);
	ItemsById.Add(RootItem.ItemId, &RootItem);
	for (const FPEPItemInstanceTreeEntry& Entry : Children)
	{
		ItemsById.Add(Entry.Item.ItemId, &Entry.Item);
	}

	const auto AddItemError = [&OutResult](const FPEPItemInstance& Item, const FText& Error)
	{
		OutResult.AddError(FText::Format(
			FText::FromString(TEXT("Item {0}: {1}")),
			FText::FromString(Item.ItemId.ToString()),
			Error));
	};

	/*
	 * Each item's own instance, then what its definition declares. A definition is checked once
	 * however many items in the tree share it: a crate of thirty identical pouches declares the
	 * same slots thirty times, and the answer cannot differ between them.
	 */
	TSet<const UPEPItemDefinition*> CheckedDefinitions;
	const auto ValidateItem = [&OutResult, &AddItemError, &CheckedDefinitions](const FPEPItemInstance& Item)
	{
		FPEPItemInstanceValidationResult InstanceResult;
		Item.ValidateInstance(InstanceResult);
		for (const FText& Error : InstanceResult.Errors)
		{
			AddItemError(Item, Error);
		}
		for (const FText& Warning : InstanceResult.Warnings)
		{
			OutResult.AddWarning(FText::Format(
				FText::FromString(TEXT("Item {0}: {1}")),
				FText::FromString(Item.ItemId.ToString()),
				Warning));
		}

		const UPEPItemDefinition* Definition = Item.GetDefinitionIfLoaded();
		bool bAlreadyChecked = false;
		if (!Definition)
		{
			return;
		}
		CheckedDefinitions.Add(Definition, &bAlreadyChecked);
		const FPEPItemFragment_Container* Container = bAlreadyChecked
			? nullptr
			: Definition->GetDefinitionFragment<FPEPItemFragment_Container>();
		if (!Container)
		{
			return;
		}

		// Resource depletion removes an entire inventory subtree. Until a domain policy can preserve,
		// spill, or reject descendants explicitly, a resource and a container cannot share one item.
		if (Definition->GetDefinitionFragment<FPEPItemFragment_Resource>())
		{
			AddItemError(Item, FText::FromString(
				TEXT("An inventory item cannot combine Container and Resource because depletion removes its descendants.")));
		}

		// Validate slot declarations once, including unused slots, before FindSlot() could silently
		// select the first duplicate name.
		if (Container->Slots.IsEmpty())
		{
			AddItemError(Item, FText::FromString(TEXT("The container definition declares no slots.")));
		}

		TSet<FName> SeenSlotNames;
		SeenSlotNames.Reserve(Container->Slots.Num());
		for (const FPEPContainerSlotDefinition& Slot : Container->Slots)
		{
			if (!Slot.IsValid())
			{
				AddItemError(Item, FText::Format(
					FText::FromString(TEXT("Container slot '{0}' has an invalid definition.")),
					FText::FromName(Slot.SlotName)));
			}
			bool bDuplicate = false;
			SeenSlotNames.Add(Slot.SlotName, &bDuplicate);
			if (bDuplicate)
			{
				AddItemError(Item, FText::Format(
					FText::FromString(TEXT("Container slot name '{0}' is duplicated.")),
					FText::FromName(Slot.SlotName)));
			}
		}
	};

	ValidateItem(RootItem);
	for (const FPEPItemInstanceTreeEntry& Entry : Children)
	{
		ValidateItem(Entry.Item);
	}

	/*
	 * Placement is checked per (parent, slot): only items sharing a slot can collide, so each one
	 * is compared with its slot-mates alone rather than with everything its parent holds.
	 */
	struct FPlacedFootprint
	{
		FIntPoint Position = FIntPoint::ZeroValue;
		FIntPoint Size = FIntPoint::ZeroValue;
	};
	using FSlotKey = TPair<FPEPItemId, FName>;
	TMap<FSlotKey, TArray<FPlacedFootprint>> PlacedBySlot;

	for (const FPEPItemInstanceTreeEntry& Entry : Children)
	{
		const FPEPItemInstance* const* ParentPtr = ItemsById.Find(Entry.Location.ParentItemId);
		const FPEPItemInstance* ParentItem = ParentPtr ? *ParentPtr : nullptr;
		const UPEPItemDefinition* ParentDefinition = ParentItem
			? ParentItem->GetDefinitionIfLoaded()
			: nullptr;
		const UPEPItemDefinition* ChildDefinition = Entry.Item.GetDefinitionIfLoaded();
		const FPEPItemFragment_Container* Container = ParentDefinition
			? ParentDefinition->GetDefinitionFragment<FPEPItemFragment_Container>()
			: nullptr;
		const FPEPContainerSlotDefinition* Slot = Container
			? Container->FindSlot(Entry.Location.ContainerSlotName)
			: nullptr;

		if (!ParentItem || !ParentDefinition || !Container || !Slot || !ChildDefinition)
		{
			AddItemError(Entry.Item, FText::FromString(
				TEXT("The parent container, child definition, or declared slot is unavailable.")));
			continue;
		}
		if (!Slot->IsValid())
		{
			continue; // Already reported with the owning container above.
		}

		FText PlacementError;
		if (!UPEPItemContainerRulesLibrary::CanPlaceItemInSlot(
			ParentDefinition, Entry.Location.ContainerSlotName, ChildDefinition, PlacementError))
		{
			AddItemError(Entry.Item, PlacementError);
			continue;
		}

		FIntPoint ItemSize;
		bool bItemCanRotate = false;
		if (!PEPInventorySettings().GetEffectiveInventoryShape(
			ChildDefinition->GetDefinitionFragment<FPEPItemFragment_InventoryShape>(),
			ItemSize,
			bItemCanRotate))
		{
			AddItemError(Entry.Item, FText::FromString(
				TEXT("The item's inventory shape must have positive dimensions.")));
			continue;
		}

		TArray<FPlacedFootprint>& SlotPlacements = PlacedBySlot.FindOrAdd(
			FSlotKey(Entry.Location.ParentItemId, Entry.Location.ContainerSlotName));
		if (Slot->Mode == EPEPContainerSlotMode::Single)
		{
			if (Entry.Location.GridPosition != FIntPoint::ZeroValue || Entry.Location.bRotated)
			{
				AddItemError(Entry.Item, FText::FromString(
					TEXT("Single slots require position (0, 0) and no rotation.")));
			}
			if (!SlotPlacements.IsEmpty())
			{
				AddItemError(Entry.Item, FText::Format(
					FText::FromString(TEXT("Single slot '{0}' contains more than one item.")),
					FText::FromName(Entry.Location.ContainerSlotName)));
			}
			SlotPlacements.AddDefaulted();
			continue;
		}

		if (Entry.Location.bRotated && (!Slot->bAllowRotation || !bItemCanRotate))
		{
			AddItemError(Entry.Item, FText::FromString(
				TEXT("The item's definition or container slot does not allow rotation.")));
		}
		const FIntPoint OrientedSize = Entry.Location.bRotated
			? FIntPoint(ItemSize.Y, ItemSize.X)
			: ItemSize;
		const FIntPoint Position = Entry.Location.GridPosition;

		/* Subtracted rather than added: an authored position near INT32_MAX must not wrap inside. */
		if (Position.X < 0 || Position.Y < 0
			|| OrientedSize.X > Slot->GridSize.X || OrientedSize.Y > Slot->GridSize.Y
			|| Position.X > Slot->GridSize.X - OrientedSize.X
			|| Position.Y > Slot->GridSize.Y - OrientedSize.Y)
		{
			AddItemError(Entry.Item, FText::Format(
				FText::FromString(TEXT("The item footprint is outside grid slot '{0}'.")),
				FText::FromName(Entry.Location.ContainerSlotName)));
			continue;
		}

		/* In bounds, so every sum below is bounded by the grid size. */
		for (const FPlacedFootprint& Existing : SlotPlacements)
		{
			const bool bOverlaps = Position.X < Existing.Position.X + Existing.Size.X
				&& Position.X + OrientedSize.X > Existing.Position.X
				&& Position.Y < Existing.Position.Y + Existing.Size.Y
				&& Position.Y + OrientedSize.Y > Existing.Position.Y;
			if (bOverlaps)
			{
				AddItemError(Entry.Item, FText::Format(
					FText::FromString(TEXT("The item overlaps another item in grid slot '{0}'.")),
					FText::FromName(Entry.Location.ContainerSlotName)));
				break;
			}
		}
		SlotPlacements.Add({Position, OrientedSize});
	}

	return OutResult.bValid;
}
