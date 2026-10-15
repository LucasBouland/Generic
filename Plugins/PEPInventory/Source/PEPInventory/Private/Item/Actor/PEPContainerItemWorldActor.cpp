#include "Item/Actor/PEPContainerItemWorldActor.h"
#include "Components/PEPInventoryComponent.h"
#include "Core/PEPCoreTags.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Inventory/PEPInventorySettings.h"
#include "Inventory/PEPInventoryTags.h"

APEPContainerItemWorldActor::APEPContainerItemWorldActor()
{
	Inventory = CreateDefaultSubobject<UPEPInventoryComponent>(TEXT("Inventory"));
}
void APEPContainerItemWorldActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	ConfigureInventory();
}
void APEPContainerItemWorldActor::ConfigureInventory()
{
	// Private contents reach clients only through their player-owned session view.
	Inventory->SetIsReplicated(false);
	Inventory->ExternalAccess = GetDefault<UPEPInventorySettings>()->WorldContainerExternalAccess;
	// The root slot holds this actor's own item and nothing else: never a target for auto-placement.
	Inventory->RegisterLocationValidator(PEPInventoryTags::Inventory_Location_WorldItem,
		FPEPItemLocationValidator::CreateUObject(this, &ThisClass::ValidateRoot), false);
	Inventory->OnItemStoreChanged().RemoveAll(this);
	Inventory->OnItemStoreChanged().AddUObject(this, &ThisClass::HandleStoreChanged);
}
FPEPItemResult APEPContainerItemWorldActor::ValidateRoot(
	const FPEPItemInstance& Item, const FPEPItemLocation& Location) const
{
	return Location.LocationTag == PEPInventoryTags::Inventory_Location_WorldItem && Item.ItemId == RootItemId
		? FPEPItemResult::Success(Item.ItemId)
		: FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, Item.ItemId);
}
const FPEPItemInstance* APEPContainerItemWorldActor::FindAuthorityItemInstance() const
{
	return HasAuthority() && Inventory && RootItemId.IsValid() ? Inventory->FindItem(RootItemId) : nullptr;
}
FPEPItemInstanceTree APEPContainerItemWorldActor::GetAuthorityItemTree() const
{
	FPEPItemInstanceTree Tree;
	const FPEPItemInstance* Root = FindAuthorityItemInstance();
	if (!Root)
	{
		return Tree;
	}
	Tree.RootItem = *Root;
	const TArray<FPEPInventoryEntry>& Entries = Inventory->GetInventoryView().Entries;
	Tree.Children.Reserve(FMath::Max(0, Entries.Num() - 1));
	for (const FPEPInventoryEntry& Entry : Entries)
	{
		if (Entry.Item.ItemId != RootItemId)
		{
			FPEPItemInstanceTreeEntry& Child = Tree.Children.AddDefaulted_GetRef();
			Child.Item = Entry.Item;
			Child.Location = Entry.Location;
		}
	}
	return Tree;
}
FPEPItemResult APEPContainerItemWorldActor::TransferTreeForPickup(
	UPEPInventoryComponent* TargetInventory, const FPEPItemAccessContext& Access)
{
	if (!FindAuthorityItemInstance())
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget);
	}
	const FPEPItemId PickedUpRootId = RootItemId;
	FPEPItemResult Result;
	{
		/* The root leaving here is the pickup itself, not a reason to destroy the actor under its caller. */
		TGuardValue<bool> Guard(bReplacingStorage, true);
		Result = Inventory->TransferPickupToInventory(TargetInventory, PickedUpRootId, Access);
	}
	if (Result.bSuccess)
	{
		RootItemId = FPEPItemId();
		RefreshPublicPresentation();
	}
	return Result;
}
void APEPContainerItemWorldActor::VisitAuthorityChildren(FPEPItemId ParentId,
	TFunctionRef<void(const FPEPItemInstance&, const FPEPItemLocation&)> Visitor) const
{
	if (!HasAuthority() || !Inventory) return;
	/* Pointers into the store, which the presentation walk only reads: nothing is copied here. */
	TArray<const FPEPInventoryEntry*> ChildEntries;
	Inventory->GetDirectChildEntries(ParentId, ChildEntries);
	for (const FPEPInventoryEntry* Entry : ChildEntries)
	{
		if (Entry)
		{
			Visitor(Entry->Item, Entry->Location);
		}
	}
}
void APEPContainerItemWorldActor::ResetAuthorityStorage()
{
	TGuardValue<bool> Guard(bReplacingStorage, true);
	if (FindAuthorityItemInstance())
	{
		Inventory->DestroyItemTree(RootItemId, FPEPItemAccessContext::System());
	}
	RootItemId = FPEPItemId();
}
bool APEPContainerItemWorldActor::StoreAuthorityTree(const FPEPItemInstanceTree& Tree)
{
	if (!Tree.RootItem.GetDefinitionFragment<FPEPItemFragment_Container>())
	{
		return false;
	}
	ConfigureInventory();
	TGuardValue<bool> Guard(bReplacingStorage, true);
	const FPEPItemId PreviousRoot = RootItemId;
	RootItemId = Tree.RootItem.ItemId;
	const bool bRestored = Inventory->RestoreWorldItemTree(Tree, PEPInventoryTags::Inventory_Location_WorldItem);
	if (!bRestored) RootItemId = PreviousRoot;
	return bRestored;
}
void APEPContainerItemWorldActor::HandleStoreChanged(const FPEPItemStoreChange& Change)
{
	if (bReplacingStorage || !HasAuthority() || IsActorBeingDestroyed()) return;
	if (!FindAuthorityItemInstance())
	{
		Destroy();
		return;
	}
	/*
	 * Most changes happen out of sight — an item moved between two pockets, a stack topped up in a
	 * backpack — and none of those can change what the world sees. Rebuilding the presentation for
	 * each of them walked the whole visible tree only to find it unchanged.
	 */
	if (DoesChangeAffectPresentation(Change))
	{
		RefreshPublicPresentation();
	}
}
bool APEPContainerItemWorldActor::DoesChangeAffectPresentation(const FPEPItemStoreChange& Change) const
{
	if (Change.ItemId == RootItemId)
	{
		return true;
	}
	switch (Change.ChangeType)
	{
	case EPEPItemStoreChangeType::Added:
	case EPEPItemStoreChangeType::StateChanged:
		return IsPresentedLocation(Change.CurrentLocation);
	case EPEPItemStoreChangeType::Removed:
		return IsPresentedLocation(Change.PreviousLocation);
	case EPEPItemStoreChangeType::Moved:
	default:
		return IsPresentedLocation(Change.PreviousLocation) || IsPresentedLocation(Change.CurrentLocation);
	}
}
bool APEPContainerItemWorldActor::IsPresentedLocation(const FPEPItemLocation& Location) const
{
	/*
	 * Walked upwards: drawn means sitting in an Attachment slot of something itself drawn, up to
	 * the root. Bounded by the store size, so a corrupt parent cycle ends rather than spins.
	 */
	FPEPItemLocation Current = Location;
	for (int32 Remaining = Inventory->GetInventoryView().Entries.Num(); Remaining >= 0; --Remaining)
	{
		if (Current.Type != EPEPItemLocationType::ItemContainer)
		{
			return false;
		}
		const FPEPInventoryEntry* Parent = Inventory->FindEntry(Current.ParentItemId);
		const FPEPItemFragment_Container* Container = Parent
			? Parent->Item.GetDefinitionFragment<FPEPItemFragment_Container>()
			: nullptr;
		const FPEPContainerSlotDefinition* Slot = Container
			? Container->FindSlot(Current.ContainerSlotName)
			: nullptr;
		if (!Slot || Slot->Purpose != EPEPContainerSlotPurpose::Attachment)
		{
			return false;
		}
		if (Current.ParentItemId == RootItemId)
		{
			return true;
		}
		Current = Parent->Location;
	}
	return false;
}

bool APEPContainerItemWorldActor::SupportsDefinition(const UPEPItemDefinition* Definition) const
{
	return Definition && Definition->GetDefinitionFragment<FPEPItemFragment_Container>();
}
