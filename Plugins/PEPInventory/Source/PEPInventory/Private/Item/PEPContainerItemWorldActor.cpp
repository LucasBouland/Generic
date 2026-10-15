#include "Item/PEPContainerItemWorldActor.h"
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
FPEPItemInstance APEPContainerItemWorldActor::GetAuthorityItemInstance() const
{
	const FPEPItemInstance* Item = HasAuthority() ? Inventory->FindItem(RootItemId) : nullptr;
	return Item ? *Item : FPEPItemInstance();
}
FPEPItemInstanceTree APEPContainerItemWorldActor::GetAuthorityItemTree() const
{
	FPEPItemInstanceTree Tree;
	Tree.RootItem = GetAuthorityItemInstance();
	if (Tree.RootItem.IsValid())
	{
		for (const FPEPInventoryEntry& Entry : Inventory->GetInventoryView().Entries)
		{
			if (Entry.Item.ItemId != RootItemId)
			{
				FPEPItemInstanceTreeEntry& Child = Tree.Children.AddDefaulted_GetRef();
				Child.Item = Entry.Item;
				Child.Location = Entry.Location;
			}
		}
	}
	return Tree;
}
void APEPContainerItemWorldActor::GetAuthorityChildren(FPEPItemId ParentId,
	TArray<FPEPItemInstanceTreeEntry>& OutChildren) const
{
	if (!HasAuthority()) return;
	TArray<const FPEPInventoryEntry*> ChildEntries;
	Inventory->GetDirectChildEntries(ParentId, ChildEntries);
	for (const FPEPInventoryEntry* Entry : ChildEntries)
	{
		FPEPItemInstanceTreeEntry& Child = OutChildren.AddDefaulted_GetRef();
		Child.Item = Entry->Item;
		Child.Location = Entry->Location;
	}
}
void APEPContainerItemWorldActor::ResetAuthorityStorage()
{
	TGuardValue<bool> Guard(bReplacingStorage, true);
	if (GetAuthorityItemInstance().IsValid())
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
	if (bReplacingStorage || !HasAuthority()) return;
	if (!GetAuthorityItemInstance().IsValid())
	{
		Destroy();
		return;
	}
	RefreshPublicPresentation();
}

bool APEPContainerItemWorldActor::SupportsDefinition(const UPEPItemDefinition* Definition) const
{
	return Definition && Definition->GetDefinitionFragment<FPEPItemFragment_Container>();
}
