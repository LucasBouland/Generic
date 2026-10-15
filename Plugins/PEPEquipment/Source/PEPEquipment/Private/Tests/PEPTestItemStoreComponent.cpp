#include "Tests/PEPTestItemStoreComponent.h"

#include "Core/PEPCoreTags.h"

const FPEPItemInstance* UPEPTestItemStoreComponent::FindItem(const FPEPItemId& ItemId) const
{
	const FStoredItem* Stored = FindStoredItem(ItemId);
	return Stored ? &Stored->Item : nullptr;
}

bool UPEPTestItemStoreComponent::FindItemLocation(
	const FPEPItemId& ItemId,
	FPEPItemLocation& OutLocation) const
{
	const FStoredItem* Stored = FindStoredItem(ItemId);
	if (!Stored)
	{
		OutLocation = FPEPItemLocation();
		return false;
	}
	OutLocation = Stored->Location;
	return true;
}

bool UPEPTestItemStoreComponent::FindItemAtLocation(
	const FPEPItemLocation& Location,
	FPEPItemId& OutItemId) const
{
	const FStoredItem* Stored = Items.FindByPredicate(
		[&Location](const FStoredItem& Candidate)
		{
			return Candidate.Location == Location;
		});
	if (!Stored)
	{
		OutItemId.Reset();
		return false;
	}
	OutItemId = Stored->Item.ItemId;
	return true;
}

void UPEPTestItemStoreComponent::GetDirectChildren(
	const FPEPItemId& ParentItemId,
	TArray<FPEPItemId>& OutChildItemIds) const
{
	OutChildItemIds.Reset();

	for (const FStoredItem& Stored : Items)
	{
		if (Stored.Location.Type == EPEPItemLocationType::ItemContainer
			&& Stored.Location.ParentItemId == ParentItemId)
		{
			OutChildItemIds.Add(Stored.Item.ItemId);
		}
	}
}

void UPEPTestItemStoreComponent::GetPublicChildren(
	const FPEPItemId& ParentItemId,
	TArray<FPEPItemId>& OutChildItemIds) const
{
	OutChildItemIds.Reset();

	for (const FStoredItem& Stored : Items)
	{
		if (Stored.Location.Type == EPEPItemLocationType::ItemContainer
			&& Stored.Location.ParentItemId == ParentItemId
			&& PublicSlotNames.Contains(Stored.Location.ContainerSlotName))
		{
			OutChildItemIds.Add(Stored.Item.ItemId);
		}
	}
}

FPEPItemResult UPEPTestItemStoreComponent::AddItem(
	const FPEPItemInstance& Item,
	const FPEPItemLocation& Destination,
	const FPEPItemAccessContext& Access)
{
	if (!Item.ItemId.IsValid() || FindStoredItem(Item.ItemId))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, Item.ItemId);
	}
	const FPEPItemResult Validation = ValidateDestination(Item, Destination, FPEPItemId());
	if (!Validation.bSuccess)
	{
		return Validation;
	}

	FStoredItem& Stored = Items.AddDefaulted_GetRef();
	Stored.Item = Item;
	Stored.Location = Destination;

	FPEPItemStoreChange Change;
	Change.ItemId = Item.ItemId;
	Change.ItemInstance = Item;
	Change.ChangeType = EPEPItemStoreChangeType::Added;
	Change.CurrentLocation = Destination;
	OnChanged.Broadcast(Change);
	return FPEPItemResult::Success(Item.ItemId);
}

FPEPItemResult UPEPTestItemStoreComponent::CanMoveItem(
	const FPEPItemId& ItemId,
	const FPEPItemLocation& Destination,
	const FPEPItemAccessContext& Access) const
{
	const FStoredItem* Stored = FindStoredItem(ItemId);
	return Stored
		? ValidateDestination(Stored->Item, Destination, ItemId)
		: FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
}

FPEPItemResult UPEPTestItemStoreComponent::MoveItem(
	const FPEPItemId& ItemId,
	const FPEPItemLocation& Destination,
	const FPEPItemAccessContext& Access)
{
	FStoredItem* Stored = FindStoredItem(ItemId);
	if (!Stored)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
	}
	const FPEPItemResult Validation = ValidateDestination(Stored->Item, Destination, ItemId);
	if (!Validation.bSuccess)
	{
		return Validation;
	}

	const FPEPItemLocation PreviousLocation = Stored->Location;
	Stored->Location = Destination;
	FPEPItemStoreChange Change;
	Change.ItemId = ItemId;
	Change.ItemInstance = Stored->Item;
	Change.ChangeType = EPEPItemStoreChangeType::Moved;
	Change.PreviousLocation = PreviousLocation;
	Change.CurrentLocation = Destination;
	OnChanged.Broadcast(Change);
	return FPEPItemResult::Success(ItemId);
}

FPEPItemResult UPEPTestItemStoreComponent::CanSwapItems(
	const FPEPItemId& FirstItemId,
	const FPEPItemId& SecondItemId,
	const FPEPItemAccessContext& Access) const
{
	const FStoredItem* First = FindStoredItem(FirstItemId);
	const FStoredItem* Second = FindStoredItem(SecondItemId);
	if (!First || !Second || FirstItemId == SecondItemId)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, FirstItemId);
	}
	const FPEPItemResult FirstValidation =
		ValidateDestination(First->Item, Second->Location, SecondItemId);
	return FirstValidation.bSuccess
		? ValidateDestination(Second->Item, First->Location, FirstItemId)
		: FirstValidation;
}

FPEPItemResult UPEPTestItemStoreComponent::SwapItems(
	const FPEPItemId& FirstItemId,
	const FPEPItemId& SecondItemId,
	const FPEPItemAccessContext& Access)
{
	const FPEPItemResult Validation = CanSwapItems(FirstItemId, SecondItemId, Access);
	if (!Validation.bSuccess)
	{
		return Validation;
	}
	FStoredItem* First = FindStoredItem(FirstItemId);
	FStoredItem* Second = FindStoredItem(SecondItemId);
	const FPEPItemLocation FirstLocation = First->Location;
	const FPEPItemLocation SecondLocation = Second->Location;
	First->Location = SecondLocation;
	Second->Location = FirstLocation;

	FPEPItemStoreChange FirstChange;
	FirstChange.ItemId = FirstItemId;
	FirstChange.ItemInstance = First->Item;
	FirstChange.ChangeType = EPEPItemStoreChangeType::Moved;
	FirstChange.PreviousLocation = FirstLocation;
	FirstChange.CurrentLocation = SecondLocation;
	OnChanged.Broadcast(FirstChange);
	FPEPItemStoreChange SecondChange;
	SecondChange.ItemId = SecondItemId;
	SecondChange.ItemInstance = Second->Item;
	SecondChange.ChangeType = EPEPItemStoreChangeType::Moved;
	SecondChange.PreviousLocation = SecondLocation;
	SecondChange.CurrentLocation = FirstLocation;
	OnChanged.Broadcast(SecondChange);
	return FPEPItemResult::Success(FirstItemId);
}

FPEPItemResult UPEPTestItemStoreComponent::RemoveItem(
	const FPEPItemId& ItemId,
	const FPEPItemAccessContext& Access)
{
	const int32 Index = Items.IndexOfByPredicate(
		[ItemId](const FStoredItem& Candidate)
		{
			return Candidate.Item.ItemId == ItemId;
		});
	if (Index == INDEX_NONE)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
	}

	const FPEPItemInstance RemovedItem = Items[Index].Item;
	const FPEPItemLocation PreviousLocation = Items[Index].Location;
	Items.RemoveAtSwap(Index);
	FPEPItemStoreChange Change;
	Change.ItemId = ItemId;
	Change.ItemInstance = RemovedItem;
	Change.ChangeType = EPEPItemStoreChangeType::Removed;
	Change.PreviousLocation = PreviousLocation;
	OnChanged.Broadcast(Change);
	return FPEPItemResult::Success(ItemId);
}

FPEPItemResult UPEPTestItemStoreComponent::MutateItem(
	const FPEPItemId& ItemId,
	const FPEPItemStoreMutation& Mutation,
	const FPEPItemAccessContext& Access)
{
	FStoredItem* Stored = FindStoredItem(ItemId);
	if (!Stored || !Mutation)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
	}

	FPEPItemInstance WorkingCopy = Stored->Item;
	if (!Mutation(WorkingCopy) || WorkingCopy.ItemId != ItemId)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_StateMismatch, ItemId);
	}
	Stored->Item = MoveTemp(WorkingCopy);

	FPEPItemStoreChange Change;
	Change.ItemId = ItemId;
	Change.ItemInstance = Stored->Item;
	Change.ChangeType = EPEPItemStoreChangeType::StateChanged;
	Change.CurrentLocation = Stored->Location;
	OnChanged.Broadcast(Change);
	return FPEPItemResult::Success(ItemId);
}

bool UPEPTestItemStoreComponent::RegisterLocationValidator(
	const FGameplayTag RootLocationTag,
	FPEPItemLocationValidator Validator,
	const bool bAllowAutoPlacement)
{
	if (!RootLocationTag.IsValid() || !Validator.IsBound())
	{
		return false;
	}

	FValidatorBinding* Existing = Validators.FindByPredicate(
		[RootLocationTag](const FValidatorBinding& Binding)
		{
			return Binding.RootLocationTag == RootLocationTag;
		});
	if (!Existing)
	{
		Existing = &Validators.AddDefaulted_GetRef();
		Existing->RootLocationTag = RootLocationTag;
	}
	Existing->Validator = MoveTemp(Validator);
	Existing->bAllowAutoPlacement = bAllowAutoPlacement;
	return true;
}

void UPEPTestItemStoreComponent::UnregisterLocationValidator(
	const FGameplayTag RootLocationTag,
	const UObject* BoundObject)
{
	Validators.RemoveAll(
		[RootLocationTag, BoundObject](const FValidatorBinding& Binding)
		{
			return Binding.RootLocationTag == RootLocationTag
				&& (!BoundObject || Binding.Validator.IsBoundToObject(BoundObject));
		});
}

bool UPEPTestItemStoreComponent::DoesLocationAllowAutoPlacement(
	const FGameplayTag RootLocationTag) const
{
	const FValidatorBinding* Binding = Validators.FindByPredicate(
		[RootLocationTag](const FValidatorBinding& Candidate)
		{
			return Candidate.RootLocationTag == RootLocationTag;
		});
	return Binding && Binding->bAllowAutoPlacement;
}

FPEPItemResult UPEPTestItemStoreComponent::ValidateDestination(
	const FPEPItemInstance& Item,
	const FPEPItemLocation& Destination,
	const FPEPItemId& IgnoredItemId) const
{
	if (!Destination.IsStructurallyValid())
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, Item.ItemId);
	}

	for (const FStoredItem& Existing : Items)
	{
		if (Existing.Item.ItemId != IgnoredItemId && Existing.Location == Destination)
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_NoFreeSlot, Item.ItemId);
		}
	}

	if (Destination.Type == EPEPItemLocationType::ItemContainer)
	{
		return FindStoredItem(Destination.ParentItemId)
			? FPEPItemResult::Success(Item.ItemId)
			: FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, Item.ItemId);
	}

	const FValidatorBinding* BestBinding = nullptr;
	for (const FValidatorBinding& Binding : Validators)
	{
		if (Destination.LocationTag.MatchesTag(Binding.RootLocationTag)
			&& (!BestBinding
				|| Binding.RootLocationTag.ToString().Len()
					> BestBinding->RootLocationTag.ToString().Len()))
		{
			BestBinding = &Binding;
		}
	}
	return BestBinding
		? BestBinding->Validator.Execute(Item, Destination)
		: FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, Item.ItemId);
}

UPEPTestItemStoreComponent::FStoredItem* UPEPTestItemStoreComponent::FindStoredItem(
	const FPEPItemId& ItemId)
{
	return Items.FindByPredicate(
		[ItemId](const FStoredItem& Candidate)
		{
			return Candidate.Item.ItemId == ItemId;
		});
}

const UPEPTestItemStoreComponent::FStoredItem* UPEPTestItemStoreComponent::FindStoredItem(
	const FPEPItemId& ItemId) const
{
	return Items.FindByPredicate(
		[ItemId](const FStoredItem& Candidate)
		{
			return Candidate.Item.ItemId == ItemId;
		});
}
