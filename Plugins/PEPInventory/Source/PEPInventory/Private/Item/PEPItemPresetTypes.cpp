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

	TMap<FPEPItemId, const FPEPItemInstanceTreeEntry*> EntriesById;
	EntriesById.Reserve(Children.Num());
	for (const FPEPItemInstanceTreeEntry& Entry : Children)
	{
		if (!Entry.Item.IsValid()
			|| Entry.Item.ItemId == RootItem.ItemId
			|| EntriesById.Contains(Entry.Item.ItemId)
			|| !Entry.Location.IsStructurallyValid()
			|| Entry.Location.Type != EPEPItemLocationType::ItemContainer)
		{
			return false;
		}

		EntriesById.Add(Entry.Item.ItemId, &Entry);
	}

	for (const FPEPItemInstanceTreeEntry& Entry : Children)
	{
		TSet<FPEPItemId> Visited;
		FPEPItemId ParentId = Entry.Location.ParentItemId;
		while (ParentId.IsValid() && ParentId != RootItem.ItemId)
		{
			if (Visited.Contains(ParentId))
			{
				return false;
			}

			Visited.Add(ParentId);
			const FPEPItemInstanceTreeEntry* const* Parent = EntriesById.Find(ParentId);
			if (!Parent || !*Parent)
			{
				return false;
			}

			ParentId = (*Parent)->Location.ParentItemId;
		}

		if (ParentId != RootItem.ItemId)
		{
			return false;
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
	const auto ValidateInstance = [&OutResult, &AddItemError](const FPEPItemInstance& Item)
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
	};

	ValidateInstance(RootItem);
	for (const FPEPItemInstanceTreeEntry& Entry : Children)
	{
		ValidateInstance(Entry.Item);
	}

	// Resource depletion removes an entire inventory subtree. Until a domain policy can preserve,
	// spill, or reject descendants explicitly, a resource and a container cannot share one item.
	for (const TPair<FPEPItemId, const FPEPItemInstance*>& Pair : ItemsById)
	{
		const FPEPItemInstance* Item = Pair.Value;
		const UPEPItemDefinition* Definition = Item ? Item->GetDefinitionIfLoaded() : nullptr;
		if (Definition
			&& Definition->GetDefinitionFragment<FPEPItemFragment_Container>()
			&& Definition->GetDefinitionFragment<FPEPItemFragment_Resource>())
		{
			AddItemError(*Item, FText::FromString(
				TEXT("An inventory item cannot combine Container and Resource because depletion removes its descendants.")));
		}
	}

	// Validate slot declarations once, including unused slots, before FindSlot() could silently
	// select the first duplicate name.
	for (const TPair<FPEPItemId, const FPEPItemInstance*>& Pair : ItemsById)
	{
		const FPEPItemInstance* Item = Pair.Value;
		const UPEPItemDefinition* Definition = Item ? Item->GetDefinitionIfLoaded() : nullptr;
		const FPEPItemFragment_Container* Container = Definition
			? Definition->GetDefinitionFragment<FPEPItemFragment_Container>()
			: nullptr;
		if (!Item || !Container)
		{
			continue;
		}
		if (Container->Slots.IsEmpty())
		{
			AddItemError(*Item, FText::FromString(TEXT("The container definition declares no slots.")));
		}

		TSet<FName> SeenSlotNames;
		for (const FPEPContainerSlotDefinition& Slot : Container->Slots)
		{
			if (!Slot.IsValid())
			{
				AddItemError(*Item, FText::Format(
					FText::FromString(TEXT("Container slot '{0}' has an invalid definition.")),
					FText::FromName(Slot.SlotName)));
			}
			if (SeenSlotNames.Contains(Slot.SlotName))
			{
				AddItemError(*Item, FText::Format(
					FText::FromString(TEXT("Container slot name '{0}' is duplicated.")),
					FText::FromName(Slot.SlotName)));
			}
			SeenSlotNames.Add(Slot.SlotName);
		}
	}

	struct FPlacedTreeItem
	{
		FName SlotName;
		FIntPoint Position = FIntPoint::ZeroValue;
		FIntPoint Size = FIntPoint::ZeroValue;
		bool bSingle = false;
	};
	TMap<FPEPItemId, TArray<FPlacedTreeItem>> PlacedByParent;

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

		const FPEPItemFragment_InventoryShape* ShapeFragment =
			ChildDefinition->GetDefinitionFragment<FPEPItemFragment_InventoryShape>();
		if (ShapeFragment && (ShapeFragment->Size.X <= 0 || ShapeFragment->Size.Y <= 0))
		{
			AddItemError(Entry.Item, FText::FromString(
				TEXT("The item's inventory shape must have positive dimensions.")));
			continue;
		}

		FIntPoint ItemSize;
		bool bItemCanRotate = false;
		if (!PEPInventorySettings().GetEffectiveInventoryShape(ShapeFragment, ItemSize, bItemCanRotate))
		{
			AddItemError(Entry.Item, FText::FromString(TEXT("The item's effective inventory shape is invalid.")));
			continue;
		}

		TArray<FPlacedTreeItem>& ExistingPlacements = PlacedByParent.FindOrAdd(Entry.Location.ParentItemId);
		if (Slot->Mode == EPEPContainerSlotMode::Single)
		{
			if (Entry.Location.GridPosition != FIntPoint::ZeroValue || Entry.Location.bRotated)
			{
				AddItemError(Entry.Item, FText::FromString(
					TEXT("Single slots require position (0, 0) and no rotation.")));
			}
			if (ExistingPlacements.ContainsByPredicate(
				[&Entry](const FPlacedTreeItem& Existing)
				{
					return Existing.SlotName == Entry.Location.ContainerSlotName;
				}))
			{
				AddItemError(Entry.Item, FText::Format(
					FText::FromString(TEXT("Single slot '{0}' contains more than one item.")),
					FText::FromName(Entry.Location.ContainerSlotName)));
			}
			ExistingPlacements.Add({Entry.Location.ContainerSlotName, FIntPoint::ZeroValue,
				FIntPoint::ZeroValue, true});
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

		for (const FPlacedTreeItem& Existing : ExistingPlacements)
		{
			if (Existing.SlotName != Entry.Location.ContainerSlotName || Existing.bSingle)
			{
				continue;
			}
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
		ExistingPlacements.Add({Entry.Location.ContainerSlotName, Position, OrientedSize, false});
	}

	return OutResult.bValid;
}
