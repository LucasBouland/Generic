#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/SceneComponent.h"
#include "Core/PEPCoreTags.h"
#include "GameFramework/PlayerController.h"
#include "Item/Actor/PEPItemWorldActor.h"
#include "Item/PEPContainerItemWorldActor.h"
#include "Item/Fragments/PEPItemFragment_Resource.h"
#include "Item/PEPItemMutation.h"
#include "Item/PEPItemPreset.h"
#include "Inventory/PEPInventorySettings.h"
#include "Loadout/PEPLoadoutMutation.h"
#include "Loadout/PEPLoadoutPreset.h"
#include "Misc/ScopeExit.h"
#include "Item/PEPItemQuery.h"
#include "Item/PEPItemRules.h"
#include "Item/PEPItemIdLibrary.h"
#include "PEPInventoryTestFixture.h"

/*
 * These cover the invariants whose failure costs items: a container swallowing itself, two items
 * sharing a cell, a subtree losing members on the way out, and a depleted item lingering. They
 * are why the component validates as much as it does, so they are what a regression should trip.
 */

using PEPInventoryTest::FFixture;

namespace
{
	FPEPItemLocation SlotAt(
		const FPEPItemId ParentItemId,
		const FName SlotName,
		const int32 X,
		const int32 Y,
		const bool bRotated = false)
	{
		return FPEPItemLocation::MakeItemContainer(
			ParentItemId, SlotName, FIntPoint(X, Y), bRotated);
	}

	const FName SlotName(TEXT("main"));

	/* Whole-store snapshots, for tests that count or walk every entry; the API hands out ids. */
	TArray<FPEPInventoryEntry> InventoryEntries(const UPEPInventoryComponent& Inventory)
	{
		TArray<FPEPItemId> Ids;
		Inventory.GetInventoryItemIds(Ids);
		TArray<FPEPInventoryEntry> Entries;
		for (const FPEPItemId Id : Ids)
		{
			FPEPInventoryEntry& Entry = Entries.AddDefaulted_GetRef();
			Inventory.FindInventoryEntry(Id, Entry);
		}
		return Entries;
	}

	TArray<FPEPInventoryEntry> SessionEntries(const UPEPInventorySessionComponent* Session)
	{
		TArray<FPEPInventoryEntry> Entries;
		if (!Session)
		{
			return Entries;
		}
		TArray<FPEPItemId> Ids;
		Session->GetSessionItemIds(Ids);
		for (const FPEPItemId Id : Ids)
		{
			FPEPInventoryEntry& Entry = Entries.AddDefaulted_GetRef();
			Session->FindSessionEntry(Id, Entry);
		}
		return Entries;
	}

	UPEPInventoryComponent* MakeAdditionalInventory(UWorld* World)
	{
		AActor* Owner = World ? World->SpawnActor<AActor>() : nullptr;
		UPEPInventoryComponent* Inventory = Owner
			? NewObject<UPEPInventoryComponent>(Owner)
			: nullptr;
		if (!Inventory)
		{
			return nullptr;
		}

		Inventory->RegisterComponent();
		Inventory->RegisterLocationValidator(
			FFixture::RootSlotTag(),
			FPEPItemLocationValidator::CreateLambda(
				[](const FPEPItemInstance& Item, const FPEPItemLocation&)
				{
					return FPEPItemResult::Success(Item.ItemId);
				}));
		return Inventory;
	}

	/**
	 * An inventory whose owner is a controller, so a store it loots resolves it to External.
	 *
	 * An owner with no controller resolves to System, which the item layer treats as the server
	 * acting for no player and grants owner permissions — correct, and useless for testing what an
	 * outsider is shown.
	 */
	UPEPInventoryComponent* MakeLooterInventory(UWorld* World)
	{
		AActor* Owner = World ? World->SpawnActor<APlayerController>() : nullptr;
		UPEPInventoryComponent* Inventory = Owner
			? NewObject<UPEPInventoryComponent>(Owner)
			: nullptr;
		if (!Inventory)
		{
			return nullptr;
		}

		Inventory->RegisterComponent();
		return Inventory;
	}

	/** A rig with one pocket an outsider may see into and one they may not. */
	UPEPItemDefinition* MakeTwoPocketRigDefinition(
		PEPInventoryTest::FFixture& Fixture,
		const FName Name,
		const FName OpenSlotName,
		const FName SealedSlotName)
	{
		FPEPContainerSlotDefinition OpenPocket;
		OpenPocket.SlotName = OpenSlotName;
		OpenPocket.Mode = EPEPContainerSlotMode::Grid;
		OpenPocket.GridSize = FIntPoint(1, 1);
		OpenPocket.OtherVisibility = EPEPContainerSlotVisibility::Lootable;

		FPEPContainerSlotDefinition SealedPocket;
		SealedPocket.SlotName = SealedSlotName;
		SealedPocket.Mode = EPEPContainerSlotMode::Grid;
		SealedPocket.GridSize = FIntPoint(1, 1);
		SealedPocket.OtherVisibility = EPEPContainerSlotVisibility::Hidden;

		FPEPItemFragment_Container Container;
		Container.Slots.Add(OpenPocket);
		Container.Slots.Add(SealedPocket);

		FPEPItemFragment_InventoryShape Shape;
		Shape.Size = FIntPoint(1, 1);

		return Fixture.MakeDefinition(
			Name,
			{ FInstancedStruct::Make(Container), FInstancedStruct::Make(Shape) });
	}

	bool SessionMirrors(const UPEPInventoryComponent& Looter, const FPEPItemId ItemId)
	{
		FPEPInventoryEntry Unused;
		const UPEPInventorySessionComponent* Session =
			UPEPInventorySessionComponent::FindSessionComponent(Looter.GetOwner());
		return Session && Session->FindSessionEntry(ItemId, Unused);
	}
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryItemStoreDirectChildrenTest,
	"PEP.Inventory.Store.DirectChildrenAreGeneric",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryItemStoreDirectChildrenTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("DirectChildrenBagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* LeafDef =
		Fixture.MakeShapedDefinition(TEXT("DirectChildrenLeafDef"), FIntPoint(1, 1));

	FPEPItemInstance Root = FFixture::MakeItem(BagDef);
	FPEPItemInstance DirectChild = FFixture::MakeItem(BagDef);
	FPEPItemInstance Grandchild = FFixture::MakeItem(LeafDef);
	const FPEPItemId RootId = Root.ItemId;
	const FPEPItemId DirectChildId = DirectChild.ItemId;
	const FPEPItemId GrandchildId = Grandchild.ItemId;

	if (!TestTrue(TEXT("the root container is stored"), Fixture.AddToRootSlot(Root).bSuccess)
		|| !TestTrue(
			TEXT("the direct child is stored"),
			Fixture.Inventory->AddItem(
				DirectChild,
				SlotAt(RootId, SlotName, 0, 0),
				FPEPItemAccessContext::System()).bSuccess)
		|| !TestTrue(
			TEXT("the grandchild is stored"),
			Fixture.Inventory->AddItem(
				Grandchild,
				SlotAt(DirectChildId, SlotName, 0, 0),
				FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	const IPEPItemStore* Store = Fixture.Inventory;
	TArray<FPEPItemId> ChildIds;
	ChildIds.Add(GrandchildId);
	Store->GetDirectChildren(RootId, ChildIds);

	TestEqual(TEXT("only one direct child is returned"), ChildIds.Num(), 1);
	TestTrue(TEXT("the direct child is returned"), ChildIds.Contains(DirectChildId));
	TestFalse(TEXT("the grandchild is not returned"), ChildIds.Contains(GrandchildId));

	ChildIds.Add(RootId);
	Store->GetDirectChildren(PEPItemAutomationTestSupport::MakeItemIdForTests(), ChildIds);
	TestTrue(TEXT("the output is reset when the parent is unknown"), ChildIds.IsEmpty());

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryCycleTest,
	"PEP.Inventory.Tree.RejectsCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryCycleTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("BagDef"), SlotName, FIntPoint(4, 4));

	FPEPItemInstance Outer = FFixture::MakeItem(BagDef);
	FPEPItemInstance Inner = FFixture::MakeItem(BagDef);
	const FPEPItemId OuterId = Outer.ItemId;
	const FPEPItemId InnerId = Inner.ItemId;

	if (!TestTrue(TEXT("the outer bag goes into the equipment slot"),
		Fixture.AddToRootSlot(Outer).bSuccess))
	{
		return false;
	}

	if (!TestTrue(TEXT("the inner bag goes inside the outer one"),
		Fixture.Inventory->AddItem(Inner, SlotAt(OuterId, SlotName, 0, 0), FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	// The whole point: the outer bag must not be movable into its own descendant.
	TestFalse(TEXT("a container cannot be moved inside its own child"),
		Fixture.Inventory->CanMoveItem(OuterId, SlotAt(InnerId, SlotName, 0, 0), FPEPItemAccessContext::System()).bSuccess);

	// Nor directly into itself.
	TestFalse(TEXT("a container cannot be moved inside itself"),
		Fixture.Inventory->CanMoveItem(OuterId, SlotAt(OuterId, SlotName, 1, 1), FPEPItemAccessContext::System()).bSuccess);

	// The legitimate direction still works.
	TestTrue(TEXT("moving the child elsewhere in the parent is allowed"),
		Fixture.Inventory->CanMoveItem(InnerId, SlotAt(OuterId, SlotName, 2, 2), FPEPItemAccessContext::System()).bSuccess);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryOverlapTest,
	"PEP.Inventory.Grid.RejectsOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryOverlapTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("BagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* WideDef =
		Fixture.MakeShapedDefinition(TEXT("WideDef"), FIntPoint(2, 1));

	FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemId BagId = Bag.ItemId;

	if (!TestTrue(TEXT("the bag goes into the equipment slot"),
		Fixture.AddToRootSlot(Bag).bSuccess))
	{
		return false;
	}

	FPEPItemInstance First = FFixture::MakeItem(WideDef);
	if (!TestTrue(TEXT("a 2x1 item fits at the origin"),
		Fixture.Inventory->AddItem(First, SlotAt(BagId, SlotName, 0, 0), FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	FPEPItemInstance Second = FFixture::MakeItem(WideDef);

	TestFalse(TEXT("the cell under the first item is taken"),
		Fixture.Inventory->CanAddItem(Second, SlotAt(BagId, SlotName, 0, 0), FPEPItemAccessContext::System()).bSuccess);

	TestFalse(TEXT("a 2x1 at x=1 still overlaps the one at x=0"),
		Fixture.Inventory->CanAddItem(Second, SlotAt(BagId, SlotName, 1, 0), FPEPItemAccessContext::System()).bSuccess);

	TestTrue(TEXT("x=2 clears it"),
		Fixture.Inventory->CanAddItem(Second, SlotAt(BagId, SlotName, 2, 0), FPEPItemAccessContext::System()).bSuccess);

	TestTrue(TEXT("the row below is free"),
		Fixture.Inventory->CanAddItem(Second, SlotAt(BagId, SlotName, 0, 1), FPEPItemAccessContext::System()).bSuccess);

	// Rotated, the item is 1x2: at x=1 it still sits under the one occupying (0,0)-(1,0).
	TestFalse(TEXT("rotated at x=1 still overlaps"),
		Fixture.Inventory->CanAddItem(Second, SlotAt(BagId, SlotName, 1, 0, true), FPEPItemAccessContext::System()).bSuccess);

	TestTrue(TEXT("rotated at x=2 clears it"),
		Fixture.Inventory->CanAddItem(Second, SlotAt(BagId, SlotName, 2, 0, true), FPEPItemAccessContext::System()).bSuccess);

	TestFalse(TEXT("a 2x1 hanging off the right edge is rejected"),
		Fixture.Inventory->CanAddItem(Second, SlotAt(BagId, SlotName, 3, 0), FPEPItemAccessContext::System()).bSuccess);

	// Rotated it is 2 tall, so the bottom row cannot hold it.
	TestFalse(TEXT("a rotated item hanging off the bottom edge is rejected"),
		Fixture.Inventory->CanAddItem(Second, SlotAt(BagId, SlotName, 2, 3, true), FPEPItemAccessContext::System()).bSuccess);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventorySubtreeTest,
	"PEP.Inventory.Tree.SubtreeExtractionKeepsMembers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventorySubtreeTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("BagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("SmallDef"), FIntPoint(1, 1));

	FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);

	FPEPItemInstance Rig = FFixture::MakeItem(BagDef);
	const FPEPItemId RigId = Rig.ItemId;
	Fixture.Inventory->AddItem(Rig, SlotAt(BagId, SlotName, 0, 0), FPEPItemAccessContext::System());

	FPEPItemInstance Trinket = FFixture::MakeItem(SmallDef);
	const FPEPItemId TrinketId = Trinket.ItemId;
	Fixture.Inventory->AddItem(Trinket, SlotAt(RigId, SlotName, 0, 0), FPEPItemAccessContext::System());

	TestEqual(TEXT("three items are stored"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 3);

	// A loaded container cannot be pulled out as a single item; that would orphan its contents.
	TestFalse(TEXT("extracting a container that still holds something is refused"),
		Fixture.Inventory->CanExtractItem(BagId, FPEPItemAccessContext::System()).bSuccess);

	TArray<FPEPInventoryEntry> Extracted;
	const FPEPItemResult TreeResult =
		Fixture.Inventory->ExtractItemTree(BagId, Extracted, FPEPItemAccessContext::System());

	if (!TestTrue(TEXT("the whole subtree extracts"), TreeResult.bSuccess))
	{
		return false;
	}

	TestEqual(TEXT("all three came out"), Extracted.Num(), 3);
	TestEqual(TEXT("the store is empty"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 0);

	TestFalse(TEXT("the leaf is gone from the store"),
		Fixture.Inventory->ContainsItem(TrinketId));

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryTransferTreeTest,
	"PEP.Inventory.Tree.TransferKeepsCompleteSubtree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryTransferTreeTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPInventoryComponent* Target = MakeAdditionalInventory(Fixture.World);
	if (!TestNotNull(TEXT("the target inventory exists"), Target))
	{
		return false;
	}

	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("TransferTreeBagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("TransferTreeSmallDef"), FIntPoint(1, 1));

	FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);

	FPEPItemInstance InnerBag = FFixture::MakeItem(BagDef);
	const FPEPItemId InnerBagId = InnerBag.ItemId;
	Fixture.Inventory->AddItem(
		InnerBag,
		SlotAt(BagId, SlotName, 0, 0),
		FPEPItemAccessContext::System());

	FPEPItemInstance Content = FFixture::MakeItem(SmallDef);
	const FPEPItemId ContentId = Content.ItemId;
	Fixture.Inventory->AddItem(
		Content,
		SlotAt(InnerBagId, SlotName, 0, 0),
		FPEPItemAccessContext::System());

	const FPEPItemLocation Destination =
		FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag());
	const FPEPItemResult Result = Fixture.Inventory->TransferItemToInventory(
		Target,
		BagId,
		Destination,
		FPEPItemAccessContext::System());

	TestTrue(TEXT("the loaded container transfers"), Result.bSuccess);
	TestEqual(TEXT("the source no longer owns any tree member"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 0);
	TestEqual(TEXT("the target receives the complete tree"),
		Target->GetInventoryView().Entries.Num(), 3);

	FPEPItemLocation BagLocation;
	FPEPItemLocation InnerBagLocation;
	FPEPItemLocation ContentLocation;
	TestTrue(TEXT("the root resolves in the target"), Target->FindItemLocation(BagId, BagLocation));
	TestTrue(TEXT("the child resolves in the target"),
		Target->FindItemLocation(InnerBagId, InnerBagLocation));
	TestTrue(TEXT("the grandchild resolves in the target"),
		Target->FindItemLocation(ContentId, ContentLocation));
	TestTrue(TEXT("only the root receives the requested destination"),
		BagLocation == Destination);
	TestTrue(TEXT("the child keeps its parent"),
		InnerBagLocation.ParentItemId == BagId);
	TestTrue(TEXT("the grandchild keeps its parent"),
		ContentLocation.ParentItemId == InnerBagId);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryTransferTreeRollbackTest,
	"PEP.Inventory.Tree.FailedTransferKeepsBothStoresUntouched",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryTransferTreeRollbackTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPInventoryComponent* Target = MakeAdditionalInventory(Fixture.World);
	if (!TestNotNull(TEXT("the target inventory exists"), Target))
	{
		return false;
	}

	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("TransferRollbackBagDef"), SlotName, FIntPoint(2, 2));
	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("TransferRollbackSmallDef"), FIntPoint(1, 1));

	FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);

	FPEPItemInstance Content = FFixture::MakeItem(SmallDef);
	const FPEPItemId ContentId = Content.ItemId;
	Fixture.Inventory->AddItem(
		Content,
		SlotAt(BagId, SlotName, 0, 0),
		FPEPItemAccessContext::System());

	FPEPItemInstance Occupant = FFixture::MakeItem(SmallDef);
	const FPEPItemId OccupantId = Occupant.ItemId;
	Target->AddItem(
		Occupant,
		FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag()),
		FPEPItemAccessContext::System());

	const FPEPItemResult Result = Fixture.Inventory->TransferItemToInventory(
		Target,
		BagId,
		FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag()),
		FPEPItemAccessContext::System());

	TestFalse(TEXT("the occupied destination rejects the complete tree"), Result.bSuccess);
	TestEqual(TEXT("the failed transfer keeps both source entries"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 2);
	TestEqual(TEXT("the failed transfer keeps the target unchanged"),
		Target->GetInventoryView().Entries.Num(), 1);
	TestTrue(TEXT("the source still owns the root"), Fixture.Inventory->ContainsItem(BagId));
	TestTrue(TEXT("the source still owns the child"), Fixture.Inventory->ContainsItem(ContentId));
	TestTrue(TEXT("the original target occupant remains"), Target->ContainsItem(OccupantId));

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryStackPickupTest,
	"PEP.Inventory.Stack.PickupMergesIntoExisting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryStackPickupTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("BagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* AmmoDef =
		Fixture.MakeStackableDefinition(TEXT("AmmoDef"), 60);

	FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);

	FPEPItemInstance Carried = FFixture::MakeItem(AmmoDef);
	const FPEPItemId CarriedId = Carried.ItemId;
	if (!TestTrue(TEXT("a first ammo stack is carried"),
		Fixture.Inventory->AddItem(Carried, SlotAt(BagId, SlotName, 0, 0), FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	const int32 BeforeCount = Fixture.Inventory->GetInventoryView().Entries.Num();

	// Picking up more of the same must top up the carried stack, not take a new cell.
	FPEPItemInstance PickedUp = FFixture::MakeItem(AmmoDef);
	TestTrue(TEXT("the pickup succeeds"),
		Fixture.Inventory->AddPickupItem(PickedUp, FPEPItemAccessContext::System()).bSuccess);

	TestEqual(TEXT("no new entry was created"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), BeforeCount);

	FPEPItemStackData StackData;
	const FPEPItemInstance* Stored = Fixture.Inventory->FindItem(CarriedId);

	if (!TestNotNull(TEXT("the carried stack is still there"), Stored))
	{
		return false;
	}

	PEPItemQuery::GetStackData(*Stored, StackData);
	TestEqual(TEXT("the carried stack grew to two"), StackData.Count, 2);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryAtomicStackPickupTest,
	"PEP.Inventory.Stack.FailedPickupDoesNotPartiallyMerge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryAtomicStackPickupTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("AtomicBagDef"), SlotName, FIntPoint(1, 1));
	UPEPItemDefinition* AmmoDef =
		Fixture.MakeStackableDefinition(TEXT("AtomicAmmoDef"), 2);

	FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);

	FPEPItemInstance Carried = FFixture::MakeItem(AmmoDef);
	const FPEPItemId CarriedId = Carried.ItemId;
	Fixture.Inventory->AddItem(
		Carried,
		SlotAt(BagId, SlotName, 0, 0),
		FPEPItemAccessContext::System());

	FPEPItemInstance Pickup = FFixture::MakeItem(AmmoDef);
	TestTrue(TEXT("the incoming pickup contains two units"),
		PEPItemMutation::SetStackCount(Pickup, 2).bSuccess);

	const FPEPItemResult Result = Fixture.Inventory->AddPickupItem(
		Pickup,
		FPEPItemAccessContext::System());
	TestFalse(TEXT("the pickup fails because its remainder has no cell"), Result.bSuccess);

	FPEPItemStackData StackData;
	const FPEPItemInstance* Stored = Fixture.Inventory->FindItem(CarriedId);
	TestTrue(TEXT("the original stack still resolves"),
		Stored && PEPItemQuery::GetStackData(*Stored, StackData));
	TestEqual(TEXT("the failed pickup did not partially fill it"), StackData.Count, 1);
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryPickupTreeDryRunTest,
	"PEP.Inventory.Tree.PickupRejectsInvalidChildPlacementAtomically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryPickupTreeDryRunTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("TreeBagDef"), SlotName, FIntPoint(1, 1));
	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("TreeSmallDef"), FIntPoint(1, 1));

	FPEPInventoryEntry Root;
	Root.Item = FFixture::MakeItem(BagDef);
	Root.Location = FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag());

	FPEPInventoryEntry FirstChild;
	FirstChild.Item = FFixture::MakeItem(SmallDef);
	FirstChild.Location = SlotAt(Root.Item.ItemId, SlotName, 0, 0);

	FPEPInventoryEntry OverlappingChild;
	OverlappingChild.Item = FFixture::MakeItem(SmallDef);
	OverlappingChild.Location = SlotAt(Root.Item.ItemId, SlotName, 0, 0);

	const FPEPItemResult Result = Fixture.Inventory->AddPickupItemTree(
		{ Root, FirstChild, OverlappingChild },
		FPEPItemAccessContext::System());

	TestFalse(TEXT("overlapping siblings reject the complete pickup tree"), Result.bSuccess);
	TestEqual(TEXT("the failed tree leaves the inventory untouched"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 0);
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryPresetTreePickupTest,
	"PEP.Inventory.Tree.AcceptsPresetInstanceTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryPresetTreePickupTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("PresetTreeBagDef"), SlotName, FIntPoint(1, 1));
	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("PresetTreeSmallDef"), FIntPoint(1, 1));

	FPEPItemInstanceTree Tree;
	Tree.RootItem = FFixture::MakeItem(BagDef);
	FPEPItemInstanceTreeEntry& Child = Tree.Children.AddDefaulted_GetRef();
	Child.Item = FFixture::MakeItem(SmallDef);
	Child.Location = SlotAt(Tree.RootItem.ItemId, SlotName, 0, 0);

	const FPEPItemId RootId = Tree.RootItem.ItemId;
	const FPEPItemId ChildId = Child.Item.ItemId;
	TestTrue(TEXT("preset tree is accepted as one pickup"),
		Fixture.Inventory->AddPickupItemTree(
			Tree,
			FPEPItemAccessContext::System()).bSuccess);
	TestTrue(TEXT("preset root is stored"), Fixture.Inventory->ContainsItem(RootId));
	TestTrue(TEXT("private preset child is stored"), Fixture.Inventory->ContainsItem(ChildId));

	FPEPItemLocation StoredChildLocation;
	TestTrue(TEXT("stored child keeps its generated parent"),
		Fixture.Inventory->FindItemLocation(ChildId, StoredChildLocation)
			&& StoredChildLocation.ParentItemId == RootId);
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryLoneTreePickupMergesTest,
	"PEP.Inventory.Stack.LoneTreePickupTopsUpCarriedStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A carrier on the ground hands over a tree, which is the path every real pickup takes. A tree of
 * one is still an ordinary pickup: it used to demand a free cell of its own, so cartridges taken
 * off the ground never filled the ones already on the rig.
 */
bool FPEPInventoryLoneTreePickupMergesTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("LoneTreeBagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* AmmoDef =
		Fixture.MakeStackableDefinition(TEXT("LoneTreeAmmoDef"), 60);

	FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);

	FPEPItemInstance Carried = FFixture::MakeItem(AmmoDef);
	const FPEPItemId CarriedId = Carried.ItemId;

	if (!TestTrue(TEXT("a first ammo stack is carried"),
		Fixture.Inventory->AddItem(
			Carried,
			SlotAt(BagId, SlotName, 0, 0),
			FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	/* The bag has room to spare, so a second cell would be taken silently rather than refused. */
	const int32 BeforeCount = Fixture.Inventory->GetInventoryView().Entries.Num();

	FPEPItemInstanceTree Tree;
	Tree.RootItem = FFixture::MakeItem(AmmoDef);

	TestTrue(TEXT("the tree pickup succeeds"),
		Fixture.Inventory->AddPickupItemTree(Tree, FPEPItemAccessContext::System()).bSuccess);

	TestEqual(TEXT("no new entry was created"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), BeforeCount);

	FPEPItemStackData StackData;
	const FPEPItemInstance* Stored = Fixture.Inventory->FindItem(CarriedId);

	if (!TestNotNull(TEXT("the carried stack is still there"), Stored))
	{
		return false;
	}

	PEPItemQuery::GetStackData(*Stored, StackData);
	TestEqual(TEXT("the carried stack grew to two"), StackData.Count, 2);
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryTransferSessionTest,
	"PEP.Inventory.Replication.TransferRequiresAuthorizedSession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryTransferSessionTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("SessionBagDef"), SlotName, FIntPoint(1, 1));

	FPEPItemInstance OwnedBag = FFixture::MakeItem(BagDef);
	const FPEPItemId OwnedBagId = OwnedBag.ItemId;
	Fixture.AddToRootSlot(OwnedBag);

	AActor* RemoteOwner = Fixture.World->SpawnActor<AActor>();
	UPEPInventoryComponent* Remote = NewObject<UPEPInventoryComponent>(RemoteOwner);
	Remote->RegisterComponent();
	Remote->RegisterLocationValidator(
		FFixture::RootSlotTag(),
		FPEPItemLocationValidator::CreateLambda(
			[](const FPEPItemInstance& Item, const FPEPItemLocation&)
			{
				return FPEPItemResult::Success(Item.ItemId);
			}));
	Remote->AddItem(
		FFixture::MakeItem(BagDef),
		FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag()),
		FPEPItemAccessContext::System());

	int32 RequestId = 0;
	const EPEPRequestResult WithoutSession = Fixture.Session->RequestPutIntoSession(
		FGuid(),
		OwnedBagId,
		FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag()),
		RequestId);
	TestEqual(TEXT("putting into a remote store without a session is rejected"),
		static_cast<int32>(WithoutSession),
		static_cast<int32>(EPEPRequestResult::Failed));

	TestTrue(TEXT("the authority opens a nearby remote session"),
		Fixture.Session->OpenTransferSession(Remote, 300.0f));
	TestEqual(TEXT("the owner-only session snapshot contains the remote entry"),
		SessionEntries(Fixture.Session).Num(), 1);

	Fixture.Session->CloseTransferSession();
	TestFalse(TEXT("closing removes the active session"), Fixture.Session->HasTransferSession());
	TestEqual(TEXT("closing clears its private snapshot"),
		SessionEntries(Fixture.Session).Num(), 0);
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryAutoPlacementTest,
	"PEP.Inventory.Pickup.AutoPlacementFillsGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryAutoPlacementTest::RunTest(const FString&)
{
	FFixture Fixture;

	// A 2x1 grid holds exactly two 1x1 items, so the third pickup has nowhere to go.
	UPEPItemDefinition* RigDef =
		Fixture.MakeContainerDefinition(TEXT("RigDef"), SlotName, FIntPoint(2, 1));
	UPEPItemDefinition* TrinketDef =
		Fixture.MakeShapedDefinition(TEXT("TrinketDef"), FIntPoint(1, 1));

	FPEPItemInstance Rig = FFixture::MakeItem(RigDef);
	const FPEPItemId RigId = Rig.ItemId;

	if (!TestTrue(TEXT("the rig is equipped"), Fixture.AddToRootSlot(Rig).bSuccess))
	{
		return false;
	}

	FPEPItemInstance FirstPickup = FFixture::MakeItem(TrinketDef);
	const FPEPItemId FirstId = FirstPickup.ItemId;

	if (!TestTrue(TEXT("a non-stackable pickup finds a cell on its own"),
		Fixture.Inventory->AddPickupItem(FirstPickup, FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	FPEPItemLocation Placed;
	Fixture.Inventory->FindItemLocation(FirstId, Placed);

	TestEqual(TEXT("it landed inside the rig"),
		static_cast<int32>(Placed.Type),
		static_cast<int32>(EPEPItemLocationType::ItemContainer));
	TestTrue(TEXT("in the rig's own slot"), Placed.ParentItemId == RigId);

	// The second must take the other cell rather than fail or overlap.
	FPEPItemInstance SecondPickup = FFixture::MakeItem(TrinketDef);
	const FPEPItemId SecondId = SecondPickup.ItemId;

	TestTrue(TEXT("the second pickup finds the remaining cell"),
		Fixture.Inventory->AddPickupItem(SecondPickup, FPEPItemAccessContext::System()).bSuccess);

	FPEPItemLocation SecondPlaced;
	Fixture.Inventory->FindItemLocation(SecondId, SecondPlaced);

	TestTrue(TEXT("the two pickups occupy different cells"),
		SecondPlaced.GridPosition != Placed.GridPosition);

	TestEqual(TEXT("rig plus two trinkets are stored"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 3);

	// Full grid: auto-placement must report no free slot rather than stack them.
	FPEPItemInstance ThirdPickup = FFixture::MakeItem(TrinketDef);
	const FPEPItemResult Overflow = Fixture.Inventory->AddPickupItem(ThirdPickup, FPEPItemAccessContext::System());

	TestFalse(TEXT("a full rig refuses the third pickup"), Overflow.bSuccess);
	TestEqual(TEXT("nothing was stored on failure"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryAutoPlacementOptOutTest,
	"PEP.Inventory.Pickup.RespectsNamedSlotAutoPlacementOptOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryAutoPlacementOptOutTest::RunTest(const FString&)
{
	FFixture Fixture;
	Fixture.Inventory->RegisterLocationValidator(
		FFixture::RootSlotTag(),
		FPEPItemLocationValidator::CreateLambda(
			[](const FPEPItemInstance& Item, const FPEPItemLocation&)
			{
				return FPEPItemResult::Success(Item.ItemId);
			}),
		false);

	UPEPItemDefinition* ItemDefinition = Fixture.MakeShapedDefinition(
		TEXT("AutoPlacementOptOutItem"),
		FIntPoint(1, 1));
	FPEPItemInstance Item = FFixture::MakeItem(ItemDefinition);

	TestFalse(TEXT("pickup does not enter a named slot that opted out"),
		Fixture.Inventory->AddPickupItem(
			Item,
			FPEPItemAccessContext::System()).bSuccess);
	TestNull(TEXT("failed automatic placement does not add the item"),
		Fixture.Inventory->FindItem(Item.ItemId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryNestedAutoPlacementTest,
	"PEP.Inventory.Pickup.AutoPlacementDescendsIntoNestedContainers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A pouch inside a rig is the ordinary arrangement, and its grid used to be unreachable: automatic
 * placement only ever looked at containers sitting directly in a named slot, so a full rig meant a
 * full inventory however much room the pouches inside it had.
 */
bool FPEPInventoryNestedAutoPlacementTest::RunTest(const FString&)
{
	FFixture Fixture;
	const FName PouchSlotName(TEXT("pouch"));

	// One cell in the rig, and the pouch takes it. Anything else has to go a level deeper.
	UPEPItemDefinition* RigDef =
		Fixture.MakeContainerDefinition(TEXT("NestedRigDef"), SlotName, FIntPoint(1, 1));
	UPEPItemDefinition* PouchDef =
		Fixture.MakeContainerDefinition(TEXT("NestedPouchDef"), PouchSlotName, FIntPoint(1, 1));
	UPEPItemDefinition* TrinketDef =
		Fixture.MakeShapedDefinition(TEXT("NestedTrinketDef"), FIntPoint(1, 1));

	FPEPItemInstance Rig = FFixture::MakeItem(RigDef);
	const FPEPItemId RigId = Rig.ItemId;

	if (!TestTrue(TEXT("the rig is equipped"), Fixture.AddToRootSlot(Rig).bSuccess))
	{
		return false;
	}

	FPEPItemInstance Pouch = FFixture::MakeItem(PouchDef);
	const FPEPItemId PouchId = Pouch.ItemId;

	if (!TestTrue(TEXT("the pouch fills the rig's only cell"),
		Fixture.Inventory->AddItem(
			Pouch,
			SlotAt(RigId, SlotName, 0, 0),
			FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	FPEPItemInstance Trinket = FFixture::MakeItem(TrinketDef);
	const FPEPItemId TrinketId = Trinket.ItemId;

	if (!TestTrue(TEXT("the pickup finds room a level down"),
		Fixture.Inventory->AddPickupItem(Trinket, FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	FPEPItemLocation Placed;
	Fixture.Inventory->FindItemLocation(TrinketId, Placed);

	TestTrue(TEXT("it landed in the pouch, not the rig"), Placed.ParentItemId == PouchId);
	TestEqual(TEXT("in the pouch's own slot"), Placed.ContainerSlotName, PouchSlotName);
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryAutoMoveInPlaceTest,
	"PEP.Inventory.Move.AutoMoveRefusesRatherThanMoveInPlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Placement ignores the item itself when it looks for a conflict, so an equipped item used to
 * validate the slot it was already in, be moved there, and report success having moved nothing —
 * a refusal the UI had no way of telling apart from a completed move.
 */
bool FPEPInventoryAutoMoveInPlaceTest::RunTest(const FString&)
{
	FFixture Fixture;

	UPEPItemDefinition* RigDef =
		Fixture.MakeContainerDefinition(TEXT("AutoMoveRigDef"), SlotName, FIntPoint(1, 1));

	FPEPItemInstance Rig = FFixture::MakeItem(RigDef);
	const FPEPItemId RigId = Rig.ItemId;

	if (!TestTrue(TEXT("the rig is equipped"), Fixture.AddToRootSlot(Rig).bSuccess))
	{
		return false;
	}

	/* The only named slot is the one it occupies, and a rig cannot be stowed inside itself. */
	const FPEPItemResult Result =
		Fixture.Inventory->MoveItemAuto(RigId, FPEPItemAccessContext::System());

	TestFalse(TEXT("an equipped item with nowhere to go refuses the automatic move"),
		Result.bSuccess);

	FPEPItemLocation Placed;
	Fixture.Inventory->FindItemLocation(RigId, Placed);

	TestEqual(TEXT("the refused move left it equipped"),
		static_cast<int32>(Placed.Type),
		static_cast<int32>(EPEPItemLocationType::NamedSlot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryAutoMoveLeavesItsSlotTest,
	"PEP.Inventory.Move.AutoMoveLeavesTheSlotItWasIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryAutoMoveLeavesItsSlotTest::RunTest(const FString&)
{
	FFixture Fixture;
	const FName PouchSlotName(TEXT("pouch"));

	UPEPItemDefinition* RigDef =
		Fixture.MakeContainerDefinition(TEXT("AutoMoveHostDef"), SlotName, FIntPoint(2, 1));
	UPEPItemDefinition* PouchDef =
		Fixture.MakeContainerDefinition(TEXT("AutoMovePouchDef"), PouchSlotName, FIntPoint(1, 1));
	UPEPItemDefinition* TrinketDef =
		Fixture.MakeShapedDefinition(TEXT("AutoMoveTrinketDef"), FIntPoint(1, 1));

	FPEPItemInstance Rig = FFixture::MakeItem(RigDef);
	const FPEPItemId RigId = Rig.ItemId;
	Fixture.AddToRootSlot(Rig);

	FPEPItemInstance Pouch = FFixture::MakeItem(PouchDef);
	const FPEPItemId PouchId = Pouch.ItemId;
	Fixture.Inventory->AddItem(
		Pouch,
		SlotAt(RigId, SlotName, 0, 0),
		FPEPItemAccessContext::System());

	FPEPItemInstance Trinket = FFixture::MakeItem(TrinketDef);
	const FPEPItemId TrinketId = Trinket.ItemId;

	if (!TestTrue(TEXT("the trinket starts beside the pouch"),
		Fixture.Inventory->AddItem(
			Trinket,
			SlotAt(RigId, SlotName, 1, 0),
			FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	/* The rig's grid is where it already is, so the only answer left is inside the pouch. */
	TestTrue(TEXT("the automatic move succeeds"),
		Fixture.Inventory->MoveItemAuto(TrinketId, FPEPItemAccessContext::System()).bSuccess);

	FPEPItemLocation Placed;
	Fixture.Inventory->FindItemLocation(TrinketId, Placed);

	TestTrue(TEXT("it left the rig's grid for the pouch"), Placed.ParentItemId == PouchId);
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryDepletionTest,
	"PEP.Inventory.Stack.DepletedItemIsRemoved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryDepletionTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("BagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* AmmoDef =
		Fixture.MakeStackableDefinition(TEXT("AmmoDef"), 60);

	FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);

	FPEPItemInstance Ammo = FFixture::MakeItem(AmmoDef);
	const FPEPItemId AmmoId = Ammo.ItemId;
	Fixture.Inventory->AddItem(Ammo, SlotAt(BagId, SlotName, 0, 0), FPEPItemAccessContext::System());

	TestTrue(TEXT("the stack is stored"), Fixture.Inventory->ContainsItem(AmmoId));

	// Consuming the last unit must destroy the item rather than leave an empty stack behind.
	int32 Remaining = -1;
	const FPEPItemResult Result = Fixture.Inventory->MutateItem(
		AmmoId,
		[&Remaining](FPEPItemInstance& Item)
		{
			return PEPItemMutation::ConsumeFromStack(Item, 1, Remaining).bSuccess;
		},
		FPEPItemAccessContext::System());

	TestTrue(TEXT("the consumption succeeds"), Result.bSuccess);
	TestEqual(TEXT("nothing is left on the stack"), Remaining, 0);
	TestFalse(TEXT("the emptied item is gone from the store"),
		Fixture.Inventory->ContainsItem(AmmoId));

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryExternalAccessTest,
	"PEP.Inventory.Access.ExternalRequesterUsesOtherPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The store must decide the role itself. The fixture's owner is a bare actor with no controller,
 * so any named requester is External to it — the shape of a corpse being looted. What the same
 * request is allowed to do therefore has to change with who is asking, and only with that.
 */
bool FPEPInventoryExternalAccessTest::RunTest(const FString&)
{
	FFixture Fixture;

	/* Built here rather than through the fixture helper so the other-side flags are explicit. */
	FPEPContainerSlotDefinition Slot;
	Slot.SlotName = SlotName;
	Slot.Mode = EPEPContainerSlotMode::Grid;
	Slot.GridSize = FIntPoint(4, 4);
	Slot.bOwnerCanTake = true;
	Slot.bOwnerCanPut = true;
	Slot.bOtherCanTake = false;
	Slot.bOtherCanPut = false;
	Slot.bOtherCanMoveInside = false;

	FPEPItemFragment_Container Container;
	Container.Slots.Add(Slot);

	FPEPItemFragment_InventoryShape Shape;
	Shape.Size = FIntPoint(1, 1);

	UPEPItemDefinition* BagDef = Fixture.MakeDefinition(
		TEXT("SealedBagDef"),
		{ FInstancedStruct::Make(Container), FInstancedStruct::Make(Shape) });

	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("SmallDef"), FIntPoint(1, 1));

	FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);

	FPEPItemInstance Stored = FFixture::MakeItem(SmallDef);
	const FPEPItemId StoredId = Stored.ItemId;
	Fixture.Inventory->AddItem(Stored, SlotAt(BagId, SlotName, 0, 0), FPEPItemAccessContext::System());

	/*
	 * Constructed rather than spawned: the role resolves on pointer identity alone, and a real
	 * player controller in a world with no game mode logs errors that would fail the test for
	 * reasons having nothing to do with access.
	 */
	APlayerController* Outsider = NewObject<APlayerController>(Fixture.World);
	if (!TestNotNull(TEXT("an outsider controller exists"), Outsider))
	{
		return false;
	}

	const FPEPItemAccessContext External = FPEPItemAccessContext::From(Outsider);

	TestTrue(TEXT("a named requester that owns nothing here resolves to External"),
		Fixture.Inventory->ResolveAccessRole(External) == EPEPAccessRole::External);

	TestTrue(TEXT("a server mutation resolves to Owner"),
		Fixture.Inventory->ResolveAccessRole(FPEPItemAccessContext::System())
			== EPEPAccessRole::Owner);

	FPEPItemInstance Incoming = FFixture::MakeItem(SmallDef);

	TestTrue(TEXT("the server may still place into the slot"),
		Fixture.Inventory->CanAddItem(
			Incoming,
			SlotAt(BagId, SlotName, 1, 0),
			FPEPItemAccessContext::System()).bSuccess);

	TestFalse(TEXT("the outsider may not, because bOtherCanPut is false"),
		Fixture.Inventory->CanAddItem(
			Incoming,
			SlotAt(BagId, SlotName, 1, 0),
			External).bSuccess);

	TestTrue(TEXT("the server may take from the slot"),
		Fixture.Inventory->CanExtractItem(
			StoredId, FPEPItemAccessContext::System()).bSuccess);

	TestFalse(TEXT("the outsider may not, because bOtherCanTake is false"),
		Fixture.Inventory->CanExtractItem(StoredId, External).bSuccess);

	/* A shared store grants the owner's permissions once, over every slot's bOther* flags. */
	Fixture.Inventory->ExternalAccess = EPEPExternalAccessMode::SameAsOwner;

	TestTrue(TEXT("a shared store resolves the outsider to Owner"),
		Fixture.Inventory->ResolveAccessRole(External) == EPEPAccessRole::Owner);

	TestTrue(TEXT("the outsider may place into a shared store despite bOtherCanPut"),
		Fixture.Inventory->CanAddItem(
			Incoming,
			SlotAt(BagId, SlotName, 1, 0),
			External).bSuccess);

	TestTrue(TEXT("the outsider may move inside a shared store despite bOtherCanMoveInside"),
		Fixture.Inventory->CanMoveItem(
			StoredId,
			SlotAt(BagId, SlotName, 1, 0),
			External).bSuccess);

	/*
	 * A requester destroyed between building the context and reading it must not collapse onto the
	 * ownerless store's own null controller and be answered as its owner — nor be granted what a
	 * shared store grants the living.
	 */
	Outsider->MarkAsGarbage();

	TestTrue(TEXT("a stale requester stays External rather than matching a null owner"),
		Fixture.Inventory->ResolveAccessRole(External) == EPEPAccessRole::External);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryExternalStackMergeTest,
	"PEP.Inventory.Access.ExternalPickupCannotTopUpSealedStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Pickup tops up a matching stack where it already sits, which reaches no free cell and so never
 * passes through placement validation. That shortcut has to answer to the same permission as
 * placing would, or a slot closed to outsiders still accepts their ammunition.
 */
bool FPEPInventoryExternalStackMergeTest::RunTest(const FString&)
{
	FFixture Fixture;

	/* The fixture's container permits the owner to put and, by default, forbids everyone else. */
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("BagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* AmmoDef =
		Fixture.MakeStackableDefinition(TEXT("AmmoDef"), 60);

	FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);

	FPEPItemInstance Carried = FFixture::MakeItem(AmmoDef);
	const FPEPItemId CarriedId = Carried.ItemId;
	if (!TestTrue(TEXT("a stack is carried"),
		Fixture.Inventory->AddItem(
			Carried,
			SlotAt(BagId, SlotName, 0, 0),
			FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	APlayerController* Outsider = NewObject<APlayerController>(Fixture.World);
	const FPEPItemAccessContext External = FPEPItemAccessContext::From(Outsider);

	const auto StackCount = [&Fixture, CarriedId]() -> int32
	{
		const FPEPItemInstance* Stored = Fixture.Inventory->FindItem(CarriedId);
		FPEPItemStackData StackData;

		return Stored && PEPItemQuery::GetStackData(*Stored, StackData)
			? StackData.Count
			: -1;
	};

	TestEqual(TEXT("the carried stack starts at one"), StackCount(), 1);

	FPEPItemInstance OutsiderPickup = FFixture::MakeItem(AmmoDef);
	TestFalse(TEXT("an outsider cannot top up a stack in a slot closed to them"),
		Fixture.Inventory->AddPickupItem(OutsiderPickup, External).bSuccess);

	TestEqual(TEXT("the carried stack did not grow"), StackCount(), 1);

	/* The same pickup by the server still merges, so the check narrows nothing it should not. */
	FPEPItemInstance ServerPickup = FFixture::MakeItem(AmmoDef);
	TestTrue(TEXT("the server still tops it up"),
		Fixture.Inventory->AddPickupItem(
			ServerPickup, FPEPItemAccessContext::System()).bSuccess);

	TestEqual(TEXT("the carried stack grew to two"), StackCount(), 2);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryChangeEventTest,
	"PEP.Inventory.Events.ServerAndReplicationAgree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * An authority and a machine receiving deltas must describe the same change the same way. They
 * reach the event from opposite directions — one knows what it just did, the other only sees the
 * result — so the two vocabularies drifting apart is the failure this guards.
 */
bool FPEPInventoryChangeEventTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("BagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("SmallDef"), FIntPoint(1, 1));

	FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);

	TArray<FPEPItemStoreChange> Events;
	Fixture.Inventory->OnItemStoreChanged().AddLambda(
		[&Events](const FPEPItemStoreChange& Change)
		{
			Events.Add(Change);
		});

	const FPEPItemLocation Origin = SlotAt(BagId, SlotName, 0, 0);
	const FPEPItemLocation Elsewhere = SlotAt(BagId, SlotName, 1, 0);

	// ---- authority -------------------------------------------------------------------------

	FPEPItemInstance Stored = FFixture::MakeItem(SmallDef);
	const FPEPItemId StoredId = Stored.ItemId;
	Fixture.Inventory->AddItem(Stored, Origin, FPEPItemAccessContext::System());

	Events.Reset();
	Fixture.Inventory->MoveItem(StoredId, Elsewhere, FPEPItemAccessContext::System());

	if (!TestEqual(TEXT("moving raises exactly one event"), Events.Num(), 1))
	{
		return false;
	}

	TestTrue(TEXT("the authority calls it a move"),
		Events[0].ChangeType == EPEPItemStoreChangeType::Moved);
	TestTrue(TEXT("move carries the instance"), Events[0].ItemInstance.ItemId == StoredId);
	TestTrue(TEXT("it says where the item came from"),
		Events[0].PreviousLocation == Origin);
	TestTrue(TEXT("and where it went"),
		Events[0].CurrentLocation == Elsewhere);

	Events.Reset();
	Fixture.Inventory->DestroyItem(StoredId, FPEPItemAccessContext::System());

	if (!TestEqual(TEXT("destroying raises exactly one event"), Events.Num(), 1))
	{
		return false;
	}

	TestTrue(TEXT("removal reports where the item was, in PreviousLocation"),
		Events[0].PreviousLocation == Elsewhere);
	TestTrue(TEXT("a removed item has no current location"),
		Events[0].CurrentLocation.Type == EPEPItemLocationType::None);
	TestTrue(TEXT("removed instance survives extraction"), Events[0].ItemInstance.ItemId == StoredId);
	TestTrue(TEXT("removed definition survives extraction"), Events[0].ItemInstance.DefinitionId == Stored.DefinitionId);

	// ---- replication -----------------------------------------------------------------------

	/*
	 * Driven by hand on a detached view: the callbacks are what a receiving machine runs once a
	 * delta has been applied, and that sequence is the thing under test rather than the transport.
	 */
	FPEPInventoryView ReceivedView;
	ReceivedView.OwnerComponent = Fixture.Inventory;

	FPEPInventoryEntry Received;
	Received.Item = FFixture::MakeItem(SmallDef);
	Received.Location = Origin;

	Events.Reset();
	Received.PostReplicatedAdd(ReceivedView);

	if (!TestEqual(TEXT("an arriving entry raises one event"), Events.Num(), 1))
	{
		return false;
	}

	TestTrue(TEXT("a receiver calls it an add"),
		Events[0].ChangeType == EPEPItemStoreChangeType::Added);
	TestTrue(TEXT("replicated add carries the received snapshot"), Events[0].ItemInstance.ItemId == Received.Item.ItemId);

	// A delta that carried a new location: the receiver has to recognise a move it never performed.
	Received.Location = Elsewhere;
	Events.Reset();
	Received.PostReplicatedChange(ReceivedView);

	if (!TestEqual(TEXT("a changed entry raises one event"), Events.Num(), 1))
	{
		return false;
	}

	TestTrue(TEXT("a receiver calls it a move too"),
		Events[0].ChangeType == EPEPItemStoreChangeType::Moved);
	TestTrue(TEXT("and recovers the location the delta overwrote"),
		Events[0].PreviousLocation == Origin);
	TestTrue(TEXT("with the location it now holds"),
		Events[0].CurrentLocation == Elsewhere);

	/*
	 * A delta that left the location alone — a round spent, durability lost, a stack topped up.
	 * The same callback fires, and reporting this as a move is what it used to do.
	 */
	Events.Reset();
	Received.PostReplicatedChange(ReceivedView);

	if (!TestEqual(TEXT("a state-only delta raises one event"), Events.Num(), 1))
	{
		return false;
	}

	TestTrue(TEXT("a receiver calls it a state change, not a move"),
		Events[0].ChangeType == EPEPItemStoreChangeType::StateChanged);
	TestTrue(TEXT("replicated state carries the received snapshot"), Events[0].ItemInstance.ItemId == Received.Item.ItemId);
	TestTrue(TEXT("the item is reported where it still stands"),
		Events[0].CurrentLocation == Elsewhere);

	Events.Reset();
	Received.PreReplicatedRemove(ReceivedView);
	TestEqual(TEXT("remove is held until the delta has finished applying"), Events.Num(), 0);

	/*
	 * Released through the serializer rather than by calling the component, which is both what the
	 * transport does and the only way in now: a receiver has no business announcing a delta it did
	 * not receive, so the entry points belong to the fast array alone.
	 */
	ReceivedView.PostReplicatedReceive(
		FFastArraySerializer::FPostReplicatedReceiveParameters());

	if (!TestEqual(TEXT("a removed entry raises one event"), Events.Num(), 1))
	{
		return false;
	}

	TestTrue(TEXT("a receiver reports removal the way the authority does"),
		Events[0].ChangeType == EPEPItemStoreChangeType::Removed
			&& Events[0].PreviousLocation == Elsewhere
			&& Events[0].CurrentLocation.Type == EPEPItemLocationType::None);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryManualStackTransactionsTest,
	"PEP.Inventory.Stack.ManualSplitMergeAreAtomic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryManualStackTransactionsTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDefinition =
		Fixture.MakeContainerDefinition(TEXT("StackTransactionBag"), SlotName, FIntPoint(2, 1));
	UPEPItemDefinition* AmmoDefinition =
		Fixture.MakeStackableDefinition(TEXT("StackTransactionAmmo"), 60);

	FPEPItemInstance Bag = FFixture::MakeItem(BagDefinition);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);
	FPEPItemInstance Source = FFixture::MakeItem(AmmoDefinition);
	PEPItemMutation::SetStackCount(Source, 10);
	const FPEPItemId SourceId = Source.ItemId;
	const FPEPItemLocation SourceLocation = SlotAt(BagId, SlotName, 0, 0);
	const FPEPItemLocation SplitLocation = SlotAt(BagId, SlotName, 1, 0);
	Fixture.Inventory->AddItem(Source, SourceLocation, FPEPItemAccessContext::System());

	const auto GetStackCount = [&Fixture](const FPEPItemId ItemId)
	{
		FPEPItemStackData Data;
		const FPEPItemInstance* Item = Fixture.Inventory->FindItem(ItemId);
		return Item && PEPItemQuery::GetStackData(*Item, Data) ? Data.Count : -1;
	};

	TestTrue(TEXT("split preview succeeds"),
		Fixture.Inventory->PreviewSplitStack(SourceId, 4, SplitLocation).bSuccess);
	TestEqual(TEXT("preview does not change source count"), GetStackCount(SourceId), 10);
	TestEqual(TEXT("preview does not add an entry"),
		InventoryEntries(*Fixture.Inventory).Num(), 2);

	int32 SplitRequestId = 0;
	TestEqual(TEXT("authority executes split request"),
		Fixture.Inventory->RequestSplitStack(SourceId, 4, SplitLocation, SplitRequestId),
		EPEPRequestResult::Executed);
	TestTrue(TEXT("split request allocates an id"), SplitRequestId > 0);
	TestEqual(TEXT("source count is committed"), GetStackCount(SourceId), 6);

	FPEPItemId SplitItemId;
	for (const FPEPInventoryEntry& Entry : InventoryEntries(*Fixture.Inventory))
	{
		if (Entry.Item.ItemId != BagId && Entry.Item.ItemId != SourceId)
		{
			SplitItemId = Entry.Item.ItemId;
		}
	}
	TestTrue(TEXT("split creates a new identity"), SplitItemId.IsValid());
	TestEqual(TEXT("new stack receives requested units"), GetStackCount(SplitItemId), 4);

	int32 FailedRequestId = 0;
	TestEqual(TEXT("a second split into the occupied cell is rejected"),
		Fixture.Inventory->RequestSplitStack(SourceId, 2, SplitLocation, FailedRequestId),
		EPEPRequestResult::Failed);
	TestEqual(TEXT("failed split leaves source untouched"), GetStackCount(SourceId), 6);
	TestEqual(TEXT("failed split leaves target untouched"), GetStackCount(SplitItemId), 4);

	int32 MergeRequestId = 0;
	TestEqual(TEXT("authority executes merge request"),
		Fixture.Inventory->RequestMergeStacks(SplitItemId, SourceId, 0, MergeRequestId),
		EPEPRequestResult::Executed);
	TestTrue(TEXT("merge request allocates an id"), MergeRequestId > 0);
	TestFalse(TEXT("fully merged source is removed"), Fixture.Inventory->ContainsItem(SplitItemId));
	TestEqual(TEXT("target receives all units"), GetStackCount(SourceId), 10);
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventorySwapTransactionTest,
	"PEP.Inventory.Move.SwapIsAtomic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventorySwapTransactionTest::RunTest(const FString&)
{
	{
		FFixture Fixture;
		UPEPItemDefinition* BagDefinition =
			Fixture.MakeContainerDefinition(TEXT("SwapRollbackBag"), SlotName, FIntPoint(3, 1));
		UPEPItemDefinition* LargeDefinition =
			Fixture.MakeShapedDefinition(TEXT("SwapRollbackLarge"), FIntPoint(2, 1));
		UPEPItemDefinition* SmallDefinition =
			Fixture.MakeShapedDefinition(TEXT("SwapRollbackSmall"), FIntPoint(1, 1));
		FPEPItemInstance Bag = FFixture::MakeItem(BagDefinition);
		const FPEPItemId BagId = Bag.ItemId;
		Fixture.AddToRootSlot(Bag);
		FPEPItemInstance Large = FFixture::MakeItem(LargeDefinition);
		FPEPItemInstance Small = FFixture::MakeItem(SmallDefinition);
		const FPEPItemId LargeId = Large.ItemId;
		const FPEPItemId SmallId = Small.ItemId;
		const FPEPItemLocation LargeLocation = SlotAt(BagId, SlotName, 0, 0);
		const FPEPItemLocation SmallLocation = SlotAt(BagId, SlotName, 2, 0);
		Fixture.Inventory->AddItem(Large, LargeLocation, FPEPItemAccessContext::System());
		Fixture.Inventory->AddItem(Small, SmallLocation, FPEPItemAccessContext::System());

		TestFalse(TEXT("swap rejects an item that would no longer fit"),
			Fixture.Inventory->SwapItems(
				LargeId, SmallId, FPEPItemAccessContext::System()).bSuccess);
		FPEPItemLocation ActualLargeLocation;
		FPEPItemLocation ActualSmallLocation;
		Fixture.Inventory->FindItemLocation(LargeId, ActualLargeLocation);
		Fixture.Inventory->FindItemLocation(SmallId, ActualSmallLocation);
		TestTrue(TEXT("failed swap keeps first location"), ActualLargeLocation == LargeLocation);
		TestTrue(TEXT("failed swap keeps second location"), ActualSmallLocation == SmallLocation);
	}

	{
		FFixture Fixture;
		UPEPItemDefinition* BagDefinition =
			Fixture.MakeContainerDefinition(TEXT("SwapSuccessBag"), SlotName, FIntPoint(2, 1));
		UPEPItemDefinition* ItemDefinition =
			Fixture.MakeShapedDefinition(TEXT("SwapSuccessItem"), FIntPoint(1, 1));
		FPEPItemInstance Bag = FFixture::MakeItem(BagDefinition);
		const FPEPItemId BagId = Bag.ItemId;
		Fixture.AddToRootSlot(Bag);
		FPEPItemInstance First = FFixture::MakeItem(ItemDefinition);
		FPEPItemInstance Second = FFixture::MakeItem(ItemDefinition);
		const FPEPItemId FirstId = First.ItemId;
		const FPEPItemId SecondId = Second.ItemId;
		const FPEPItemLocation FirstLocation = SlotAt(BagId, SlotName, 0, 0);
		const FPEPItemLocation SecondLocation = SlotAt(BagId, SlotName, 1, 0);
		Fixture.Inventory->AddItem(First, FirstLocation, FPEPItemAccessContext::System());
		Fixture.Inventory->AddItem(Second, SecondLocation, FPEPItemAccessContext::System());

		bool bObserversSawOnlyFinalState = true;
		int32 EventCount = 0;
		Fixture.Inventory->OnItemStoreChanged().AddLambda(
			[&Fixture, FirstId, SecondId, FirstLocation, SecondLocation,
				&bObserversSawOnlyFinalState, &EventCount](const FPEPItemStoreChange&)
			{
				++EventCount;
				FPEPItemLocation CurrentFirst;
				FPEPItemLocation CurrentSecond;
				Fixture.Inventory->FindItemLocation(FirstId, CurrentFirst);
				Fixture.Inventory->FindItemLocation(SecondId, CurrentSecond);
				bObserversSawOnlyFinalState &= CurrentFirst == SecondLocation
					&& CurrentSecond == FirstLocation;
			});

		TestTrue(TEXT("valid swap succeeds"), Fixture.Inventory->SwapItems(
			FirstId, SecondId, FPEPItemAccessContext::System()).bSuccess);
		TestEqual(TEXT("swap emits two move events"), EventCount, 2);
		TestTrue(TEXT("every observer sees only the final state"), bObserversSawOnlyFinalState);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------

namespace
{
	/** A one-item tree, ready to be placed somewhere by AddItemTreeAt or AddItemTrees. */
	FPEPItemInstanceTree MakeLeafTree(const UPEPItemDefinition* Definition, FPEPItemId& OutItemId)
	{
		FPEPItemInstanceTree Tree;
		Tree.RootItem = FFixture::MakeItem(Definition);
		OutItemId = Tree.RootItem.ItemId;
		return Tree;
	}

	FPEPInventoryTreePlacement PlaceAt(
		FPEPItemInstanceTree&& Tree,
		const FPEPItemLocation& RootLocation)
	{
		FPEPInventoryTreePlacement Placement;
		Placement.RootLocation = RootLocation;
		Placement.Tree = MoveTemp(Tree);
		return Placement;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryTreeAtExactLocationTest,
	"PEP.Inventory.Tree.AddTreeAtHonoursTheGivenLocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Pickup places an item wherever it fits. A restore cannot: a rig saved on the back that comes
 * back on the chest is a different loadout, and the difference is silent.
 */
bool FPEPInventoryTreeAtExactLocationTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("TreeAtBagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("TreeAtSmallDef"), FIntPoint(1, 1));

	FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);

	/* A rig holding one item, exactly as an export would hand it back. */
	FPEPItemInstanceTree Tree;
	Tree.RootItem = FFixture::MakeItem(BagDef);
	const FPEPItemId RigId = Tree.RootItem.ItemId;

	FPEPItemInstanceTreeEntry& Child = Tree.Children.AddDefaulted_GetRef();
	Child.Item = FFixture::MakeItem(SmallDef);
	Child.Location = SlotAt(RigId, SlotName, 2, 1);
	const FPEPItemId ChildId = Child.Item.ItemId;

	/* Cell (3,3) is free but is not where auto-placement would have put it. */
	const FPEPItemLocation RigLocation = SlotAt(BagId, SlotName, 3, 3);

	if (!TestTrue(TEXT("the tree is accepted at the requested location"),
		Fixture.Inventory->AddItemTreeAt(
			MoveTemp(Tree), RigLocation, FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	FPEPItemLocation StoredRigLocation;
	FPEPItemLocation StoredChildLocation;
	Fixture.Inventory->FindItemLocation(RigId, StoredRigLocation);
	Fixture.Inventory->FindItemLocation(ChildId, StoredChildLocation);

	TestTrue(TEXT("the root sits exactly where it was asked to"),
		StoredRigLocation == RigLocation);
	TestTrue(TEXT("and the child kept the layout the tree carried"),
		StoredChildLocation == SlotAt(RigId, SlotName, 2, 1));

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryMultiTreeConflictTest,
	"PEP.Inventory.Tree.ConflictingTreesAreRefusedTogether",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The reason a forest is one operation rather than a loop. Two trees claiming the same cell are
 * each perfectly legal on their own, so validating them one at a time accepts both — and the
 * second lands on top of the first. Restoring an inventory is exactly this shape.
 */
bool FPEPInventoryMultiTreeConflictTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("MultiTreeBagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("MultiTreeSmallDef"), FIntPoint(1, 1));

	FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);

	const FPEPItemLocation Contested = SlotAt(BagId, SlotName, 0, 0);

	FPEPItemId FirstId;
	FPEPItemId SecondId;
	FPEPItemInstanceTree First = MakeLeafTree(SmallDef, FirstId);
	FPEPItemInstanceTree Second = MakeLeafTree(SmallDef, SecondId);

	/* Each one alone is legal — that is the whole point. */
	TestTrue(TEXT("the first tree is legal at the contested cell"),
		Fixture.Inventory->CanAddItemTreeAt(
			First, Contested, FPEPItemAccessContext::System()).bSuccess);
	TestTrue(TEXT("and so is the second, judged on its own"),
		Fixture.Inventory->CanAddItemTreeAt(
			Second, Contested, FPEPItemAccessContext::System()).bSuccess);

	TArray<FPEPInventoryTreePlacement> Placements;
	Placements.Add(PlaceAt(MoveTemp(First), Contested));
	Placements.Add(PlaceAt(MoveTemp(Second), Contested));

	TestFalse(TEXT("together they are refused"),
		Fixture.Inventory->CanAddItemTrees(Placements, FPEPItemAccessContext::System()).bSuccess);

	TArray<FPEPItemStoreChange> Events;
	Fixture.Inventory->OnItemStoreChanged().AddLambda(
		[&Events](const FPEPItemStoreChange& Change)
		{
			Events.Add(Change);
		});

	TestFalse(TEXT("and the attempt writes nothing"),
		Fixture.Inventory->AddItemTrees(Placements, FPEPItemAccessContext::System()).bSuccess);

	TestEqual(TEXT("the store still holds only the bag"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 1);
	TestFalse(TEXT("the first tree was not committed before the second failed"),
		Fixture.Inventory->ContainsItem(FirstId));
	TestFalse(TEXT("nor the second"), Fixture.Inventory->ContainsItem(SecondId));
	TestEqual(TEXT("and nothing was announced"), Events.Num(), 0);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryMultiTreeRestoreTest,
	"PEP.Inventory.Tree.SeveralTreesAreAddedAtomically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The shape of a real restore: several equipment slots, each holding a loaded container, all
 * arriving at once. Item ids have to survive, or nothing that referenced them still means anything.
 */
bool FPEPInventoryMultiTreeRestoreTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("RestoreBagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("RestoreSmallDef"), FIntPoint(1, 1));

	/* One root slot is claimed by the fixture; a second stands in for another equipment slot. */
	const FGameplayTag SecondSlotTag = PEPItemTags::Item_Slot_Content_QuickAccess;
	Fixture.Inventory->RegisterLocationValidator(
		SecondSlotTag,
		FPEPItemLocationValidator::CreateLambda(
			[](const FPEPItemInstance& Item, const FPEPItemLocation&)
			{
				return FPEPItemResult::Success(Item.ItemId);
			}));

	FPEPItemInstanceTree FirstTree;
	FirstTree.RootItem = FFixture::MakeItem(BagDef);
	const FPEPItemId FirstRootId = FirstTree.RootItem.ItemId;
	FPEPItemInstanceTreeEntry& FirstChild = FirstTree.Children.AddDefaulted_GetRef();
	FirstChild.Item = FFixture::MakeItem(SmallDef);
	FirstChild.Location = SlotAt(FirstRootId, SlotName, 1, 0);
	const FPEPItemId FirstChildId = FirstChild.Item.ItemId;

	FPEPItemId SecondRootId;
	FPEPItemInstanceTree SecondTree = MakeLeafTree(BagDef, SecondRootId);

	TArray<FPEPInventoryTreePlacement> Placements;
	Placements.Add(PlaceAt(
		MoveTemp(FirstTree), FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag())));
	Placements.Add(PlaceAt(
		MoveTemp(SecondTree), FPEPItemLocation::MakeNamedSlot(SecondSlotTag)));

	TArray<FPEPItemStoreChange> Events;
	Fixture.Inventory->OnItemStoreChanged().AddLambda(
		[&Events](const FPEPItemStoreChange& Change)
		{
			Events.Add(Change);
		});

	if (!TestTrue(TEXT("the forest restores"),
		Fixture.Inventory->AddItemTrees(Placements, FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	TestEqual(TEXT("every member is in the store"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 3);
	TestTrue(TEXT("the first root kept its id"), Fixture.Inventory->ContainsItem(FirstRootId));
	TestTrue(TEXT("its content kept its id too"), Fixture.Inventory->ContainsItem(FirstChildId));
	TestTrue(TEXT("and so did the second root"), Fixture.Inventory->ContainsItem(SecondRootId));

	FPEPItemLocation ChildLocation;
	Fixture.Inventory->FindItemLocation(FirstChildId, ChildLocation);
	TestTrue(TEXT("the saved layout came back unchanged"),
		ChildLocation == SlotAt(FirstRootId, SlotName, 1, 0));

	TestEqual(TEXT("one add event per member"), Events.Num(), 3);

	/* Replaying the same forest must find every id already taken, and change nothing. */
	TestFalse(TEXT("restoring the same forest twice is refused"),
		Fixture.Inventory->CanAddItemTrees(Placements, FPEPItemAccessContext::System()).bSuccess);
	TestEqual(TEXT("and left the store as it was"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 3);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryLoadoutTest,
	"PEP.Inventory.Loadout.OutfitsAnEmptyStoreInOneTransaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * What a starting kit has to do: several worn items, each with its own contents, arriving on an
 * empty store at once. Applying it twice must be refused rather than half-applied, and applying it
 * to two stores must give each its own items — a loadout is a recipe, not a pool.
 */
bool FPEPInventoryLoadoutTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("LoadoutBagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("LoadoutSmallDef"), FIntPoint(1, 1));

	const FGameplayTag SecondSlotTag = PEPItemTags::Item_Slot_Content_QuickAccess;
	Fixture.Inventory->RegisterLocationValidator(
		SecondSlotTag,
		FPEPItemLocationValidator::CreateLambda(
			[](const FPEPItemInstance& Item, const FPEPItemLocation&)
			{
				return FPEPItemResult::Success(Item.ItemId);
			}));

	/* A preset-built rig holding one item, and a direct loose item for the second slot. */
	UPEPItemPreset* ContentPreset = NewObject<UPEPItemPreset>(GetTransientPackage());
	ContentPreset->RootDefinition = SmallDef;

	UPEPItemPreset* RigPreset = NewObject<UPEPItemPreset>(GetTransientPackage());
	RigPreset->RootDefinition = BagDef;
	FPEPItemPresetChild& Child = RigPreset->Children.AddDefaulted_GetRef();
	Child.Preset = ContentPreset;
	Child.ContainerSlotName = SlotName;
	Child.GridPosition = FIntPoint(1, 0);

	UPEPLoadoutPreset* Loadout = NewObject<UPEPLoadoutPreset>(GetTransientPackage());
	FPEPLoadoutEntry& WornRig = Loadout->Entries.AddDefaulted_GetRef();
	WornRig.SlotTag = FFixture::RootSlotTag();
	WornRig.Preset = RigPreset;
	FPEPLoadoutEntry& Carried = Loadout->Entries.AddDefaulted_GetRef();
	Carried.SlotTag = SecondSlotTag;
	Carried.ItemDefinition = SmallDef;

	const FPEPItemResult Applied =
		Fixture.Inventory->ApplyLoadout(Loadout, FPEPItemAccessContext::System());

	if (!TestTrue(TEXT("the loadout outfits an empty store"), Applied.bSuccess))
	{
		return false;
	}

	/* Rig, its content, and the loose item. */
	TestEqual(TEXT("every item of the kit is present"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 3);

	/* The same asset applied to a second store must produce different items, not the same ones. */
	UPEPInventoryComponent* Other = MakeAdditionalInventory(Fixture.World);
	if (!TestNotNull(TEXT("a second store exists"), Other))
	{
		return false;
	}

	/* It has to offer the same slots, or it would refuse half the kit and so refuse all of it. */
	Other->RegisterLocationValidator(
		SecondSlotTag,
		FPEPItemLocationValidator::CreateLambda(
			[](const FPEPItemInstance& Item, const FPEPItemLocation&)
			{
				return FPEPItemResult::Success(Item.ItemId);
			}));

	TestTrue(TEXT("the same loadout outfits it too"),
		Other->ApplyLoadout(Loadout, FPEPItemAccessContext::System()).bSuccess);
	TestEqual(TEXT("with its own copy of the kit"),
		Other->GetInventoryView().Entries.Num(), 3);

	TArray<FPEPItemId> FirstIds;
	Fixture.Inventory->GetInventoryItemIds(FirstIds);
	for (const FPEPItemId& Id : FirstIds)
	{
		TestFalse(TEXT("no item is shared between the two stores"), Other->ContainsItem(Id));
	}

	/* Applying it again finds the slots taken, and must change nothing. */
	TestFalse(TEXT("a second application on a full store is refused"),
		Fixture.Inventory->ApplyLoadout(Loadout, FPEPItemAccessContext::System()).bSuccess);
	TestEqual(TEXT("and leaves the kit as it was"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 3);

	/* A recipe that contradicts itself is caught before any store is touched. */
	UPEPLoadoutPreset* Contradictory = NewObject<UPEPLoadoutPreset>(GetTransientPackage());
	FPEPLoadoutEntry& First = Contradictory->Entries.AddDefaulted_GetRef();
	First.SlotTag = SecondSlotTag;
	First.Preset = ContentPreset;
	FPEPLoadoutEntry& Duplicate = Contradictory->Entries.AddDefaulted_GetRef();
	Duplicate.SlotTag = SecondSlotTag;
	Duplicate.Preset = ContentPreset;

	TArray<FPEPInventoryTreePlacement> Unused;
	FText Error;
	TestFalse(TEXT("a loadout filling one slot twice is rejected"),
		Contradictory->CreatePlacementsForEditor(Unused, Error));
	TestFalse(TEXT("and says why"), Error.IsEmpty());

	/* An entry must not be ambiguous about whether its source is a preset or a direct item. */
	UPEPLoadoutPreset* Ambiguous = NewObject<UPEPLoadoutPreset>(GetTransientPackage());
	FPEPLoadoutEntry& AmbiguousEntry = Ambiguous->Entries.AddDefaulted_GetRef();
	AmbiguousEntry.SlotTag = SecondSlotTag;
	AmbiguousEntry.Preset = ContentPreset;
	AmbiguousEntry.ItemDefinition = SmallDef;
	TestFalse(TEXT("an entry with both a preset and a direct item is rejected"),
		Ambiguous->CreatePlacementsForEditor(Unused, Error));
	TestFalse(TEXT("and reports the ambiguous source"), Error.IsEmpty());

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryDropTest,
	"PEP.Inventory.Drop.PutsTheWholeSubtreeOntoACarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The other half of a pickup. What matters is not that an actor appears but that the item is in
 * exactly one place afterwards: still in the store and also on the ground is a duplication bug, and
 * in neither is a loss. The contents have to travel with the container, or dropping a full rig
 * would quietly empty it.
 */
bool FPEPInventoryDropTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("DropBagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("DropSmallDef"), FIntPoint(1, 1));

	FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);

	FPEPItemInstance Content = FFixture::MakeItem(SmallDef);
	const FPEPItemId ContentId = Content.ItemId;
	const FPEPItemLocation ContentLocation = SlotAt(BagId, SlotName, 1, 2);
	Fixture.Inventory->AddItem(Content, ContentLocation, FPEPItemAccessContext::System());

	/*
	 * Cleared for the duration rather than assumed: a project that has configured a carrier class
	 * would otherwise make this branch untestable, and the test would silently stop checking it.
	 */
	UPEPInventorySettings* Settings = GetMutableDefault<UPEPInventorySettings>();
	const TSoftClassPtr<APEPItemWorldActor> ConfiguredClass = Settings->DefaultWorldItemActorClass;
	const auto ConfiguredContainerClass = Settings->DefaultWorldContainerActorClass;
	Settings->DefaultWorldContainerActorClass = nullptr;
	Settings->DefaultWorldItemActorClass = nullptr;

	ON_SCOPE_EXIT
	{
		Settings->DefaultWorldItemActorClass = ConfiguredClass;
		Settings->DefaultWorldContainerActorClass = ConfiguredContainerClass;
	};

	/* No carrier class configured: the store must refuse rather than take the item apart. */
	APEPItemWorldActor* Carrier = nullptr;
	const FPEPItemResult WithoutClass = Fixture.Inventory->DropItemTree(
		BagId, FTransform::Identity, FPEPItemAccessContext::System(), Carrier);

	TestFalse(TEXT("a drop with no carrier class is refused"), WithoutClass.bSuccess);
	TestNull(TEXT("and spawns nothing"), Carrier);
	TestEqual(TEXT("and leaves the store untouched"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 2);

	const FTransform Where(FVector(120.0f, -40.0f, 5.0f));
	const FPEPItemResult Dropped = Fixture.Inventory->DropItemTree(
		BagId,
		Where,
		FPEPItemAccessContext::System(),
		Carrier,
		APEPContainerItemWorldActor::StaticClass());

	if (!TestTrue(TEXT("the drop succeeds once a carrier class is named"), Dropped.bSuccess))
	{
		return false;
	}

	if (!TestNotNull(TEXT("a carrier was spawned"), Carrier))
	{
		return false;
	}

	TestTrue(
		*FString::Printf(TEXT("the carrier stands where it was asked to (got %s, wanted %s)"),
			*Carrier->GetActorLocation().ToString(), *Where.GetLocation().ToString()),
		Carrier->GetActorLocation().Equals(Where.GetLocation()));

	TestEqual(TEXT("the store gave up the container and its contents"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 0);
	TestFalse(TEXT("the container is gone from the store"),
		Fixture.Inventory->ContainsItem(BagId));
	TestFalse(TEXT("and so is what it held"), Fixture.Inventory->ContainsItem(ContentId));

	const FPEPItemInstanceTree Carried = Carrier->GetAuthorityItemTree();

	TestTrue(TEXT("the carrier holds the container, ids intact"),
		Carried.RootItem.ItemId == BagId);
	if (TestEqual(TEXT("along with its one occupant"), Carried.Children.Num(), 1))
	{
		TestTrue(TEXT("which kept its id"), Carried.Children[0].Item.ItemId == ContentId);
		TestTrue(TEXT("and the layout it had inside the container"),
			Carried.Children[0].Location == ContentLocation);
	}

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventorySessionVisibilityTest,
	"PEP.Inventory.Session.HiddenSlotsAreNotMirroredToALooter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The session snapshot is replicated to the looting client, so an entry copied into it is an entry
 * that client has. It used to be copied wholesale: every slot a designer marked Hidden, OwnerOnly
 * or RequiresSearch went over the wire to the one player it was meant to be kept from.
 */
bool FPEPInventorySessionVisibilityTest::RunTest(const FString&)
{
	FFixture Fixture;
	const FName OpenSlotName(TEXT("open"));
	const FName SealedSlotName(TEXT("sealed"));

	UPEPItemDefinition* RigDef = MakeTwoPocketRigDefinition(
		Fixture, TEXT("VisibilityRigDef"), OpenSlotName, SealedSlotName);
	UPEPItemDefinition* TrinketDef =
		Fixture.MakeShapedDefinition(TEXT("VisibilityTrinketDef"), FIntPoint(1, 1));

	/* The corpse: an owner with no controller, which is what makes every requester External. */
	UPEPInventoryComponent* Corpse = MakeAdditionalInventory(Fixture.World);
	if (!TestNotNull(TEXT("the looted store exists"), Corpse))
	{
		return false;
	}

	FPEPItemInstance Rig = FFixture::MakeItem(RigDef);
	const FPEPItemId RigId = Rig.ItemId;
	Corpse->AddItem(
		Rig,
		FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag()),
		FPEPItemAccessContext::System());

	FPEPItemInstance Shown = FFixture::MakeItem(TrinketDef);
	const FPEPItemId ShownId = Shown.ItemId;
	Corpse->AddItem(
		Shown,
		SlotAt(RigId, OpenSlotName, 0, 0),
		FPEPItemAccessContext::System());

	FPEPItemInstance Concealed = FFixture::MakeItem(TrinketDef);
	const FPEPItemId ConcealedId = Concealed.ItemId;

	if (!TestTrue(TEXT("the sealed pocket holds something"),
		Corpse->AddItem(
			Concealed,
			SlotAt(RigId, SealedSlotName, 0, 0),
			FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	UPEPInventoryComponent* Looter = MakeLooterInventory(Fixture.World);
	if (!TestNotNull(TEXT("the looter exists"), Looter))
	{
		return false;
	}

	if (!TestTrue(TEXT("the session opens"), PEPInventoryTest::SessionFor(Looter)->OpenTransferSession(Corpse, 500.0f)))
	{
		return false;
	}

	TestTrue(TEXT("the rig itself is mirrored"), SessionMirrors(*Looter, RigId));
	TestTrue(TEXT("so is what the open pocket holds"), SessionMirrors(*Looter, ShownId));
	TestFalse(TEXT("what the sealed pocket holds is not"),
		SessionMirrors(*Looter, ConcealedId));
	TestEqual(TEXT("and nothing else slipped in"),
		SessionEntries(PEPInventoryTest::SessionFor(Looter)).Num(), 2);

	/* Moving it out of the sealed pocket reveals it, without reopening the session. */
	Corpse->MoveItem(
		ConcealedId,
		SlotAt(RigId, OpenSlotName, 0, 0),
		FPEPItemAccessContext::System());

	TestFalse(TEXT("the open pocket was full, so the move was refused"),
		SessionMirrors(*Looter, ConcealedId));

	Corpse->DestroyItem(ShownId, FPEPItemAccessContext::System());
	Corpse->MoveItem(
		ConcealedId,
		SlotAt(RigId, OpenSlotName, 0, 0),
		FPEPItemAccessContext::System());

	TestTrue(TEXT("once moved into the open pocket it appears"),
		SessionMirrors(*Looter, ConcealedId));

	PEPInventoryTest::SessionFor(Looter)->CloseTransferSession();
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventorySessionContainerOnlyTest,
	"PEP.Inventory.Session.ShowContainerOnlyKeepsItsContentsBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventorySessionContainerOnlyTest::RunTest(const FString&)
{
	FFixture Fixture;

	FPEPContainerSlotDefinition Pocket;
	Pocket.SlotName = SlotName;
	Pocket.Mode = EPEPContainerSlotMode::Grid;
	Pocket.GridSize = FIntPoint(1, 1);

	/* The slot would show it; the container as a whole says otherwise, and that comes first. */
	FPEPItemFragment_Container Container;
	Container.Slots.Add(Pocket);
	Container.ExternalLootVisibility = EPEPContainerLootVisibility::ShowContainerOnly;

	FPEPItemFragment_InventoryShape RigShape;
	RigShape.Size = FIntPoint(1, 1);

	UPEPItemDefinition* RigDef = Fixture.MakeDefinition(
		TEXT("ContainerOnlyRigDef"),
		{ FInstancedStruct::Make(Container), FInstancedStruct::Make(RigShape) });
	UPEPItemDefinition* TrinketDef =
		Fixture.MakeShapedDefinition(TEXT("ContainerOnlyTrinketDef"), FIntPoint(1, 1));

	UPEPInventoryComponent* Corpse = MakeAdditionalInventory(Fixture.World);
	UPEPInventoryComponent* Looter = MakeLooterInventory(Fixture.World);
	if (!TestTrue(TEXT("both stores exist"), Corpse != nullptr && Looter != nullptr))
	{
		return false;
	}

	FPEPItemInstance Rig = FFixture::MakeItem(RigDef);
	const FPEPItemId RigId = Rig.ItemId;
	Corpse->AddItem(
		Rig,
		FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag()),
		FPEPItemAccessContext::System());

	FPEPItemInstance Content = FFixture::MakeItem(TrinketDef);
	const FPEPItemId ContentId = Content.ItemId;
	Corpse->AddItem(
		Content,
		SlotAt(RigId, SlotName, 0, 0),
		FPEPItemAccessContext::System());

	if (!TestTrue(TEXT("the session opens"), PEPInventoryTest::SessionFor(Looter)->OpenTransferSession(Corpse, 500.0f)))
	{
		return false;
	}

	TestTrue(TEXT("the container is mirrored"), SessionMirrors(*Looter, RigId));
	TestFalse(TEXT("its contents are not"), SessionMirrors(*Looter, ContentId));

	PEPInventoryTest::SessionFor(Looter)->CloseTransferSession();
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventorySessionOwnerViewTest,
	"PEP.Inventory.Session.OwnerSeesEverythingItHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Visibility is a projection for outsiders, never a rule about the store itself. A stash opened by
 * the player who owns it hands over everything, sealed pockets included.
 */
bool FPEPInventorySessionOwnerViewTest::RunTest(const FString&)
{
	FFixture Fixture;
	const FName OpenSlotName(TEXT("open"));
	const FName SealedSlotName(TEXT("sealed"));

	UPEPItemDefinition* RigDef = MakeTwoPocketRigDefinition(
		Fixture, TEXT("OwnerViewRigDef"), OpenSlotName, SealedSlotName);
	UPEPItemDefinition* TrinketDef =
		Fixture.MakeShapedDefinition(TEXT("OwnerViewTrinketDef"), FIntPoint(1, 1));

	UPEPInventoryComponent* Stash = MakeAdditionalInventory(Fixture.World);
	if (!TestNotNull(TEXT("the stash exists"), Stash))
	{
		return false;
	}

	FPEPItemInstance Rig = FFixture::MakeItem(RigDef);
	const FPEPItemId RigId = Rig.ItemId;
	Stash->AddItem(
		Rig,
		FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag()),
		FPEPItemAccessContext::System());

	FPEPItemInstance Concealed = FFixture::MakeItem(TrinketDef);
	const FPEPItemId ConcealedId = Concealed.ItemId;
	Stash->AddItem(
		Concealed,
		SlotAt(RigId, SealedSlotName, 0, 0),
		FPEPItemAccessContext::System());

	/* This fixture's owner carries no controller, so the stash resolves it as itself. */
	if (!TestTrue(TEXT("the session opens"),
		Fixture.Session->OpenTransferSession(Stash, 500.0f)))
	{
		return false;
	}

	TestTrue(TEXT("the sealed pocket is mirrored to the owner"),
		SessionMirrors(*Fixture.Inventory, ConcealedId));
	TestEqual(TEXT("along with everything else"),
		SessionEntries(Fixture.Session).Num(), 2);

	Fixture.Session->CloseTransferSession();
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventorySessionDeltaTest,
	"PEP.Inventory.Session.MirrorsOneChangeWithoutResendingTheRest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The session snapshot is a fast array, and rebuilding it reassigns every replication id — so a
 * receiver is sent the whole container each time one item leaves it. What this checks is not the
 * contents, which a rebuild also gets right, but that the entries nobody touched were left alone:
 * their replication keys are what decides whether they go back on the wire.
 */
bool FPEPInventorySessionDeltaTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("DeltaBagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("DeltaSmallDef"), FIntPoint(1, 1));

	AActor* RemoteOwner = Fixture.World->SpawnActor<AActor>();
	UPEPInventoryComponent* Remote = NewObject<UPEPInventoryComponent>(RemoteOwner);
	Remote->RegisterComponent();
	Remote->RegisterLocationValidator(
		FFixture::RootSlotTag(),
		FPEPItemLocationValidator::CreateLambda(
			[](const FPEPItemInstance& Item, const FPEPItemLocation&)
			{
				return FPEPItemResult::Success(Item.ItemId);
			}));

	/* A container holding four items: the corpse being looted. */
	FPEPItemInstance RemoteBag = FFixture::MakeItem(BagDef);
	const FPEPItemId RemoteBagId = RemoteBag.ItemId;
	Remote->AddItem(
		RemoteBag,
		FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag()),
		FPEPItemAccessContext::System());

	TArray<FPEPItemId> ContentIds;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FPEPItemInstance Content = FFixture::MakeItem(SmallDef);
		ContentIds.Add(Content.ItemId);
		Remote->AddItem(
			Content,
			SlotAt(RemoteBagId, SlotName, Index, 0),
			FPEPItemAccessContext::System());
	}

	if (!TestTrue(TEXT("the session opens"), Fixture.Session->OpenTransferSession(Remote, 300.0f)))
	{
		return false;
	}

	TArray<FPEPInventoryEntry> Before = SessionEntries(Fixture.Session);
	if (!TestEqual(TEXT("the snapshot mirrors the remote store"), Before.Num(), 5))
	{
		return false;
	}

	/*
	 * What this test cannot check, and why: the saving is carried by the fast array's replication
	 * ids, and FFastArraySerializerItem resets those in its copy constructor and assignment
	 * operator — a copied entry is a new entry as far as replication is concerned. Every accessor
	 * hands out copies, so from outside the component the ids read INDEX_NONE whatever happened.
	 * Confirming the bandwidth itself needs the network profiler on a live session.
	 *
	 * What is checked here is the contract a delta has to keep to be allowed to replace a rebuild:
	 * the snapshot tracks the remote exactly, entry for entry, with nothing added or duplicated.
	 */
	const FPEPItemId TakenId = ContentIds[1];
	TestTrue(TEXT("the remote store gives the item up"),
		Remote->DestroyItem(TakenId, FPEPItemAccessContext::System()).bSuccess);

	TArray<FPEPInventoryEntry> After = SessionEntries(Fixture.Session);

	TestEqual(TEXT("the snapshot lost exactly one entry"), After.Num(), 4);
	TestFalse(TEXT("and it is the one that left the remote store"),
		After.ContainsByPredicate(
			[TakenId](const FPEPInventoryEntry& Entry)
			{
				return Entry.Item.ItemId == TakenId;
			}));

	/* Every survivor keeps the location the remote still holds for it. */
	for (const FPEPInventoryEntry& Entry : After)
	{
		FPEPInventoryEntry RemoteEntry;
		if (TestTrue(TEXT("a surviving entry still exists remotely"),
			Remote->FindInventoryEntry(Entry.Item.ItemId, RemoteEntry)))
		{
			TestTrue(TEXT("and mirrors its location"),
				Entry.Location == RemoteEntry.Location);
		}
	}

	const FPEPItemId MovedId = ContentIds[0];
	TestTrue(TEXT("the remote store accepts the move"),
		Remote->MoveItem(
			MovedId,
			SlotAt(RemoteBagId, SlotName, 3, 1),
			FPEPItemAccessContext::System()).bSuccess);

	TArray<FPEPInventoryEntry> AfterMove = SessionEntries(Fixture.Session);

	TestEqual(TEXT("a move adds nothing and removes nothing"), AfterMove.Num(), 4);

	TSet<FPEPItemId> Seen;
	for (const FPEPInventoryEntry& Entry : AfterMove)
	{
		bool bDuplicate = false;
		Seen.Add(Entry.Item.ItemId, &bDuplicate);
		TestFalse(TEXT("the mirror never holds the same item twice"), bDuplicate);
	}

	FPEPInventoryEntry Mirrored;
	TestTrue(TEXT("the mirror can be read back by id"),
		Fixture.Session->FindSessionEntry(MovedId, Mirrored));
	TestTrue(TEXT("and it carries the new location"),
		Mirrored.Location == SlotAt(RemoteBagId, SlotName, 3, 1));

	/* The entries the move did not concern are still where the remote left them. */
	for (const FPEPInventoryEntry& Entry : AfterMove)
	{
		if (Entry.Item.ItemId == MovedId)
		{
			continue;
		}

		FPEPInventoryEntry RemoteEntry;
		if (TestTrue(TEXT("an untouched entry still exists remotely"),
			Remote->FindInventoryEntry(Entry.Item.ItemId, RemoteEntry)))
		{
			TestTrue(TEXT("and was left alone"), Entry.Location == RemoteEntry.Location);
		}
	}

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryMoveInvalidatesOccupancyTest,
	"PEP.Inventory.Move.LeavingASlotFreesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Occupancy is answered from an index rebuilt only when the store is flagged dirty. Adding and
 * removing flag it; moving changes an entry's location without touching its position in the array,
 * so it is easy to conclude nothing needs rebuilding. The slot maps are keyed by location, though,
 * which is precisely what a move changes — leave them alone and a vacated slot stays occupied and
 * an entered one stays free.
 */
bool FPEPInventoryMoveInvalidatesOccupancyTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("OccupancyBagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("OccupancySmallDef"), FIntPoint(1, 1));

	const FGameplayTag SecondSlotTag = PEPItemTags::Item_Slot_Content_QuickAccess;
	Fixture.Inventory->RegisterLocationValidator(
		SecondSlotTag,
		FPEPItemLocationValidator::CreateLambda(
			[](const FPEPItemInstance& Item, const FPEPItemLocation&)
			{
				return FPEPItemResult::Success(Item.ItemId);
			}));

	FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);

	FPEPItemInstance Held = FFixture::MakeItem(SmallDef);
	const FPEPItemId HeldId = Held.ItemId;
	Fixture.Inventory->AddItem(
		Held,
		FPEPItemLocation::MakeNamedSlot(SecondSlotTag),
		FPEPItemAccessContext::System());

	/* Out of the named slot and into the bag: the named slot is now empty. */
	if (!TestTrue(TEXT("the item leaves the named slot"),
		Fixture.Inventory->MoveItem(
			HeldId,
			SlotAt(BagId, SlotName, 0, 0),
			FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	FPEPItemInstance Replacement = FFixture::MakeItem(SmallDef);
	TestTrue(TEXT("the vacated named slot accepts another item"),
		Fixture.Inventory->CanAddItem(
			Replacement,
			FPEPItemLocation::MakeNamedSlot(SecondSlotTag),
			FPEPItemAccessContext::System()).bSuccess);

	/* And the slot it entered is now taken: nothing else may overlap it. */
	FPEPItemInstance Intruder = FFixture::MakeItem(SmallDef);
	TestFalse(TEXT("the cell it moved onto is no longer free"),
		Fixture.Inventory->CanAddItem(
			Intruder,
			SlotAt(BagId, SlotName, 0, 0),
			FPEPItemAccessContext::System()).bSuccess);

	/* The slot query has to agree with both answers. */
	TArray<FPEPInventoryEntry> SlotEntries;
	Fixture.Inventory->GetEntriesInContainerSlot(BagId, SlotName, SlotEntries);
	TestEqual(TEXT("the destination slot reports its new occupant"), SlotEntries.Num(), 1);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryCrossSlotSwapTest,
	"PEP.Inventory.Move.SwapAcrossSlotsKeepsOccupancyStraight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Swapping is two moves, so it invalidates the same cache a move does — but the existing swap
 * coverage exchanges two items inside one container slot, where no cache key changes at all.
 * Two different slots is the case that exercises it, and it is also the ordinary one: putting a
 * weapon in hand by trading it against the one already there.
 */
bool FPEPInventoryCrossSlotSwapTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("CrossSwapBagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("CrossSwapSmallDef"), FIntPoint(1, 1));

	const FGameplayTag SecondSlotTag = PEPItemTags::Item_Slot_Content_QuickAccess;
	Fixture.Inventory->RegisterLocationValidator(
		SecondSlotTag,
		FPEPItemLocationValidator::CreateLambda(
			[](const FPEPItemInstance& Item, const FPEPItemLocation&)
			{
				return FPEPItemResult::Success(Item.ItemId);
			}));

	FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);

	/* One item in a named slot, one in a container cell. */
	FPEPItemInstance Held = FFixture::MakeItem(SmallDef);
	const FPEPItemId HeldId = Held.ItemId;
	Fixture.Inventory->AddItem(
		Held,
		FPEPItemLocation::MakeNamedSlot(SecondSlotTag),
		FPEPItemAccessContext::System());

	FPEPItemInstance Stowed = FFixture::MakeItem(SmallDef);
	const FPEPItemId StowedId = Stowed.ItemId;
	const FPEPItemLocation StowedLocation = SlotAt(BagId, SlotName, 1, 1);
	Fixture.Inventory->AddItem(Stowed, StowedLocation, FPEPItemAccessContext::System());

	if (!TestTrue(TEXT("the two items exchange places"),
		Fixture.Inventory->SwapItems(HeldId, StowedId, FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	FPEPItemLocation HeldNow;
	FPEPItemLocation StowedNow;
	Fixture.Inventory->FindItemLocation(HeldId, HeldNow);
	Fixture.Inventory->FindItemLocation(StowedId, StowedNow);

	TestTrue(TEXT("the held item is now in the container cell"), HeldNow == StowedLocation);
	TestTrue(TEXT("and the stowed one is in the named slot"),
		StowedNow == FPEPItemLocation::MakeNamedSlot(SecondSlotTag));

	/* Both caches must now describe the exchange, not the state before it. */
	TArray<FPEPInventoryEntry> SlotEntries;
	Fixture.Inventory->GetEntriesInContainerSlot(BagId, SlotName, SlotEntries);
	if (TestEqual(TEXT("the container cell reports one occupant"), SlotEntries.Num(), 1))
	{
		TestTrue(TEXT("and it is the item that moved in"),
			SlotEntries[0].Item.ItemId == HeldId);
	}

	FPEPItemInstance Intruder = FFixture::MakeItem(SmallDef);
	TestFalse(TEXT("the named slot is still taken, by the other item"),
		Fixture.Inventory->CanAddItem(
			Intruder,
			FPEPItemLocation::MakeNamedSlot(SecondSlotTag),
			FPEPItemAccessContext::System()).bSuccess);
	TestFalse(TEXT("and so is the cell they traded"),
		Fixture.Inventory->CanAddItem(
			Intruder,
			StowedLocation,
			FPEPItemAccessContext::System()).bSuccess);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryCheapAccessorsTest,
	"PEP.Inventory.Store.CheapAccessorsAgreeWithTheStore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/* The accessors that replace copying the whole store have to answer exactly what it holds. */
bool FPEPInventoryCheapAccessorsTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("AccessorBagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("AccessorSmallDef"), FIntPoint(1, 1));

	FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);

	TArray<FPEPItemId> ContentIds;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FPEPItemInstance Content = FFixture::MakeItem(SmallDef);
		ContentIds.Add(Content.ItemId);
		Fixture.Inventory->AddItem(
			Content,
			SlotAt(BagId, SlotName, Index, 0),
			FPEPItemAccessContext::System());
	}

	TArray<FPEPItemId> Ids;
	Fixture.Inventory->GetInventoryItemIds(Ids);

	TestEqual(TEXT("every stored item is enumerated"), Ids.Num(), 4);
	TestTrue(TEXT("including the container"), Ids.Contains(BagId));
	for (const FPEPItemId& ContentId : ContentIds)
	{
		TestTrue(TEXT("and each of its contents"), Ids.Contains(ContentId));
	}

	FPEPInventoryEntry Resolved;
	TestTrue(TEXT("an enumerated id resolves to its entry"),
		Fixture.Inventory->FindInventoryEntry(ContentIds[2], Resolved));
	TestTrue(TEXT("with the location the store holds"),
		Resolved.Location == SlotAt(BagId, SlotName, 2, 0));

	TestFalse(TEXT("an unknown id resolves to nothing"),
		Fixture.Inventory->FindInventoryEntry(PEPItemAutomationTestSupport::MakeItemIdForTests(), Resolved));

	TArray<FPEPInventoryEntry> SlotEntries;
	Fixture.Inventory->GetEntriesInContainerSlot(BagId, SlotName, SlotEntries);

	TestEqual(TEXT("the slot query returns its occupants and nothing else"), SlotEntries.Num(), 3);
	for (const FPEPInventoryEntry& Entry : SlotEntries)
	{
		TestTrue(TEXT("each occupant belongs to the queried slot"),
			Entry.Location.ParentItemId == BagId && Entry.Location.ContainerSlotName == SlotName);
	}

	TArray<FPEPInventoryEntry> EmptySlot;
	Fixture.Inventory->GetEntriesInContainerSlot(BagId, TEXT("nosuchslot"), EmptySlot);
	TestEqual(TEXT("an unoccupied slot returns nothing"), EmptySlot.Num(), 0);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryPruneMissingDefinitionTest,
	"PEP.Inventory.Tree.PruneDropsSubtreesWithMissingDefinitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A saved inventory outlives the assets it names. The store cannot read a shape or a container
 * slot off a definition it no longer has, so an item like this fails the restore as a placement
 * error and never says why — the prune exists so the failure is named and the rest still loads.
 */
bool FPEPInventoryPruneMissingDefinitionTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("PruneBagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("PruneSmallDef"), FIntPoint(1, 1));

	/*
	 * Registered like the others so a valid instance can be built from it, then unregistered: that
	 * is what a definition deleted between two builds looks like to a save that still names it.
	 */
	UPEPItemDefinition* DoomedDef =
		Fixture.MakeContainerDefinition(TEXT("PruneDoomedDef"), SlotName, FIntPoint(2, 2));

	/* A rig holding a doomed sub-container, which itself holds an item, plus a healthy sibling. */
	FPEPItemInstanceTree Tree;
	Tree.RootItem = FFixture::MakeItem(BagDef);
	const FPEPItemId RigId = Tree.RootItem.ItemId;

	FPEPItemInstanceTreeEntry& Doomed = Tree.Children.AddDefaulted_GetRef();
	Doomed.Item = FFixture::MakeItem(DoomedDef);
	Doomed.Location = SlotAt(RigId, SlotName, 0, 0);
	const FPEPItemId DoomedId = Doomed.Item.ItemId;

	FPEPItemInstanceTreeEntry& Orphan = Tree.Children.AddDefaulted_GetRef();
	Orphan.Item = FFixture::MakeItem(SmallDef);
	Orphan.Location = SlotAt(DoomedId, SlotName, 0, 0);
	const FPEPItemId OrphanId = Orphan.Item.ItemId;

	FPEPItemInstanceTreeEntry& Survivor = Tree.Children.AddDefaulted_GetRef();
	Survivor.Item = FFixture::MakeItem(SmallDef);
	Survivor.Location = SlotAt(RigId, SlotName, 2, 0);
	const FPEPItemId SurvivorId = Survivor.Item.ItemId;

	TArray<FPEPInventoryTreePlacement> Placements;
	Placements.Add(PlaceAt(
		MoveTemp(Tree), FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag())));

	/* Nothing is missing yet, so a prune must leave the forest exactly as it found it. */
	FPEPInventoryPruneReport Untouched;
	PEPLoadoutMutation::PruneMissingDefinitions(Placements, Untouched);

	TestTrue(TEXT("a forest whose definitions all exist is left alone"), Untouched.IsEmpty());
	TestEqual(TEXT("and keeps every child"), Placements[0].Tree.Children.Num(), 3);

	/* The asset goes away. */
	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(
		DoomedDef->GetPrimaryAssetId());

	FPEPInventoryPruneReport Report;
	PEPLoadoutMutation::PruneMissingDefinitions(Placements, Report);

	if (!TestEqual(TEXT("the tree survives, minus what it lost"), Placements.Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("the doomed item and its contents are gone"),
		Placements[0].Tree.Children.Num(), 1);
	TestTrue(TEXT("and the healthy sibling is what remains"),
		Placements[0].Tree.Children[0].Item.ItemId == SurvivorId);

	TestEqual(TEXT("both losses are reported"), Report.RemovedItemIds.Num(), 2);
	TestTrue(TEXT("the item whose definition vanished"),
		Report.RemovedItemIds.Contains(DoomedId));
	TestTrue(TEXT("and the one it was holding, which had nowhere left to be"),
		Report.RemovedItemIds.Contains(OrphanId));
	TestEqual(TEXT("the missing definition is named once"), Report.MissingDefinitions.Num(), 1);

	/* What is left must now restore cleanly — the point of running the prune at all. */
	TestTrue(TEXT("the pruned forest is accepted"),
		Fixture.Inventory->AddItemTrees(Placements, FPEPItemAccessContext::System()).bSuccess);
	TestEqual(TEXT("leaving the rig and its surviving content"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 2);
	TestFalse(TEXT("the pruned item was never stored"),
		Fixture.Inventory->ContainsItem(DoomedId));

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryPruneDeadRootTest,
	"PEP.Inventory.Tree.PruneDropsATreeWhoseRootIsMissing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/* A dead root takes the whole tree: nothing under it has anywhere left to be. */
bool FPEPInventoryPruneDeadRootTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("PruneRootBagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("PruneRootSmallDef"), FIntPoint(1, 1));

	FPEPItemInstanceTree Doomed;
	Doomed.RootItem = FFixture::MakeItem(BagDef);
	const FPEPItemId DoomedRootId = Doomed.RootItem.ItemId;
	FPEPItemInstanceTreeEntry& Content = Doomed.Children.AddDefaulted_GetRef();
	Content.Item = FFixture::MakeItem(SmallDef);
	Content.Location = SlotAt(DoomedRootId, SlotName, 0, 0);

	FPEPItemId KeeperId;
	FPEPItemInstanceTree Keeper = MakeLeafTree(SmallDef, KeeperId);

	const FGameplayTag SecondSlotTag = PEPItemTags::Item_Slot_Content_QuickAccess;
	Fixture.Inventory->RegisterLocationValidator(
		SecondSlotTag,
		FPEPItemLocationValidator::CreateLambda(
			[](const FPEPItemInstance& Item, const FPEPItemLocation&)
			{
				return FPEPItemResult::Success(Item.ItemId);
			}));

	TArray<FPEPInventoryTreePlacement> Placements;
	Placements.Add(PlaceAt(
		MoveTemp(Doomed), FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag())));
	Placements.Add(PlaceAt(
		MoveTemp(Keeper), FPEPItemLocation::MakeNamedSlot(SecondSlotTag)));

	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(
		BagDef->GetPrimaryAssetId());

	FPEPInventoryPruneReport Report;
	PEPLoadoutMutation::PruneMissingDefinitions(Placements, Report);

	if (!TestEqual(TEXT("only the intact tree is left"), Placements.Num(), 1))
	{
		return false;
	}

	TestTrue(TEXT("and it is the one whose definition still exists"),
		Placements[0].Tree.RootItem.ItemId == KeeperId);
	TestEqual(TEXT("the dead root and its content are both reported"),
		Report.RemovedItemIds.Num(), 2);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryMultiTreeDuplicateIdTest,
	"PEP.Inventory.Tree.DuplicateIdAcrossTreesIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A corrupted or hand-edited save can name the same item twice in two different trees. Caught
 * per-tree this is invisible: each tree is internally consistent.
 */
bool FPEPInventoryMultiTreeDuplicateIdTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("DupBagDef"), SlotName, FIntPoint(4, 4));

	FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);

	FPEPItemId SharedId;
	FPEPItemInstanceTree First = MakeLeafTree(BagDef, SharedId);

	/* The same instance, verbatim, in a second tree aimed at a different cell. */
	FPEPItemInstanceTree Second;
	Second.RootItem = First.RootItem;

	TArray<FPEPInventoryTreePlacement> Placements;
	Placements.Add(PlaceAt(MoveTemp(First), SlotAt(BagId, SlotName, 0, 0)));
	Placements.Add(PlaceAt(MoveTemp(Second), SlotAt(BagId, SlotName, 2, 0)));

	TestFalse(TEXT("the same item id in two trees is refused"),
		Fixture.Inventory->CanAddItemTrees(Placements, FPEPItemAccessContext::System()).bSuccess);
	TestFalse(TEXT("and nothing is written"),
		Fixture.Inventory->AddItemTrees(Placements, FPEPItemAccessContext::System()).bSuccess);
	TestEqual(TEXT("the store is untouched"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 1);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryDestroyTreeTest,
	"PEP.Inventory.Tree.DestroyTakesTheWholeSubtree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A despawning corpse and an ending raid both have to clear a loaded container, and neither has
 * any use for what was inside. Driven through IPEPItemStore rather than the component, because
 * the contract that used to be missing is the store's: every caller reaching an inventory as a
 * store — equipment, a world actor, whatever comes next — has to be able to do this.
 */
bool FPEPInventoryDestroyTreeTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("DestroyBagDef"), SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("DestroySmallDef"), FIntPoint(1, 1));

	FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);

	FPEPItemInstance Rig = FFixture::MakeItem(BagDef);
	const FPEPItemId RigId = Rig.ItemId;
	const FPEPItemLocation RigLocation = SlotAt(BagId, SlotName, 0, 0);
	Fixture.Inventory->AddItem(Rig, RigLocation, FPEPItemAccessContext::System());

	FPEPItemInstance Trinket = FFixture::MakeItem(SmallDef);
	const FPEPItemId TrinketId = Trinket.ItemId;
	const FPEPItemLocation TrinketLocation = SlotAt(RigId, SlotName, 0, 0);
	Fixture.Inventory->AddItem(Trinket, TrinketLocation, FPEPItemAccessContext::System());

	IPEPItemStore* Store = Fixture.Inventory;

	// The single-item path stays narrow: it is still the one that must not orphan anything.
	TestFalse(TEXT("removing a loaded container as one item is still refused"),
		Store->RemoveItem(BagId, FPEPItemAccessContext::System()).bSuccess);
	TestEqual(TEXT("the refusal removed nothing"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 3);

	TArray<FPEPItemStoreChange> Events;
	Fixture.Inventory->OnItemStoreChanged().AddLambda(
		[&Events](const FPEPItemStoreChange& Change)
		{
			Events.Add(Change);
		});

	if (!TestTrue(TEXT("destroying the tree succeeds"),
		Store->RemoveItemTree(BagId, FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	TestEqual(TEXT("the store is empty"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 0);
	TestFalse(TEXT("the root is gone"), Fixture.Inventory->ContainsItem(BagId));
	TestFalse(TEXT("the intermediate container is gone"), Fixture.Inventory->ContainsItem(RigId));
	TestFalse(TEXT("the leaf is gone rather than orphaned"),
		Fixture.Inventory->ContainsItem(TrinketId));

	if (!TestEqual(TEXT("one removal event per member"), Events.Num(), 3))
	{
		return false;
	}

	bool bAllRemovals = true;
	for (const FPEPItemStoreChange& Change : Events)
	{
		bAllRemovals &= Change.ChangeType == EPEPItemStoreChangeType::Removed
			&& Change.CurrentLocation.Type == EPEPItemLocationType::None;
	}
	TestTrue(TEXT("every event is a removal with no current location"), bAllRemovals);

	/* Removed reports where the item was, and a subtree member was inside its parent. */
	const FPEPItemStoreChange* TrinketEvent = Events.FindByPredicate(
		[TrinketId](const FPEPItemStoreChange& Change)
		{
			return Change.ItemId == TrinketId;
		});

	if (!TestNotNull(TEXT("the leaf reports its own removal"), TrinketEvent))
	{
		return false;
	}

	TestTrue(TEXT("and reports the slot it was taken from"),
		TrinketEvent->PreviousLocation == TrinketLocation);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryDestroyTreeRefusalTest,
	"PEP.Inventory.Tree.DestroyRefusalLeavesTheSubtreeIntact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Cascading destruction reads permission once, on the root. A refusal therefore has to be total:
 * a partially destroyed rig is worse than one that was never touched, because the items it ate
 * are gone for good.
 */
bool FPEPInventoryDestroyTreeRefusalTest::RunTest(const FString&)
{
	FFixture Fixture;

	FPEPContainerSlotDefinition Slot;
	Slot.SlotName = SlotName;
	Slot.Mode = EPEPContainerSlotMode::Grid;
	Slot.GridSize = FIntPoint(4, 4);
	Slot.bOwnerCanTake = true;
	Slot.bOwnerCanPut = true;
	Slot.bOtherCanTake = false;
	Slot.bOtherCanPut = false;

	FPEPItemFragment_Container Container;
	Container.Slots.Add(Slot);

	FPEPItemFragment_InventoryShape Shape;
	Shape.Size = FIntPoint(1, 1);

	UPEPItemDefinition* SealedBagDef = Fixture.MakeDefinition(
		TEXT("DestroySealedBagDef"),
		{ FInstancedStruct::Make(Container), FInstancedStruct::Make(Shape) });

	FPEPItemInstance Bag = FFixture::MakeItem(SealedBagDef);
	const FPEPItemId BagId = Bag.ItemId;
	Fixture.AddToRootSlot(Bag);

	FPEPItemInstance Rig = FFixture::MakeItem(SealedBagDef);
	const FPEPItemId RigId = Rig.ItemId;
	Fixture.Inventory->AddItem(Rig, SlotAt(BagId, SlotName, 0, 0), FPEPItemAccessContext::System());

	FPEPItemInstance Nested = FFixture::MakeItem(SealedBagDef);
	Fixture.Inventory->AddItem(
		Nested, SlotAt(RigId, SlotName, 0, 0), FPEPItemAccessContext::System());

	APlayerController* Outsider = NewObject<APlayerController>(Fixture.World);
	if (!TestNotNull(TEXT("an outsider controller exists"), Outsider))
	{
		return false;
	}

	const FPEPItemAccessContext External = FPEPItemAccessContext::From(Outsider);

	// The root sits in a named slot the outsider may take from; the rig sits in a sealed one.
	TestFalse(TEXT("an outsider may not destroy a subtree it may not take from"),
		Fixture.Inventory->CanDestroyItemTree(RigId, External).bSuccess);

	TArray<FPEPItemStoreChange> Events;
	Fixture.Inventory->OnItemStoreChanged().AddLambda(
		[&Events](const FPEPItemStoreChange& Change)
		{
			Events.Add(Change);
		});

	TestFalse(TEXT("and the attempt itself fails"),
		Fixture.Inventory->DestroyItemTree(RigId, External).bSuccess);

	TestEqual(TEXT("nothing was removed"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 3);
	TestEqual(TEXT("and nothing was announced"), Events.Num(), 0);

	TestTrue(TEXT("the server may still clear the same subtree"),
		Fixture.Inventory->DestroyItemTree(RigId, FPEPItemAccessContext::System()).bSuccess);
	TestEqual(TEXT("leaving only the root behind"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 1);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryDepletedContainerTest,
	"PEP.Inventory.Tree.DepletedContainerTakesItsContents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Nothing stops a definition from being both a container and depletable. When a mutation empties
 * one, the store destroys it — and destroying it alone would leave its contents pointing at a
 * parent that no longer exists: entries nothing can reach, replicated for the rest of the match.
 */
bool FPEPInventoryDepletedContainerTest::RunTest(const FString&)
{
	FFixture Fixture;

	FPEPContainerSlotDefinition Slot;
	Slot.SlotName = SlotName;
	Slot.Mode = EPEPContainerSlotMode::Grid;
	Slot.GridSize = FIntPoint(2, 2);
	Slot.bOwnerCanTake = true;
	Slot.bOwnerCanPut = true;

	FPEPItemFragment_Container Container;
	Container.Slots.Add(Slot);

	FPEPItemFragment_InventoryShape Shape;
	Shape.Size = FIntPoint(1, 1);

	FPEPItemFragment_Resource Resource;
	Resource.MaxResource = 2;

	UPEPItemDefinition* CaseDef = Fixture.MakeDefinition(
		TEXT("DepletableCaseDef"),
		{
			FInstancedStruct::Make(Container),
			FInstancedStruct::Make(Shape),
			FInstancedStruct::Make(Resource)
		});

	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("DepletedContentsDef"), FIntPoint(1, 1));

	FPEPItemInstance Case = FFixture::MakeItem(CaseDef);
	const FPEPItemId CaseId = Case.ItemId;
	Fixture.AddToRootSlot(Case);

	FPEPItemInstance Contents = FFixture::MakeItem(SmallDef);
	const FPEPItemId ContentsId = Contents.ItemId;
	Fixture.Inventory->AddItem(
		Contents, SlotAt(CaseId, SlotName, 0, 0), FPEPItemAccessContext::System());

	if (!TestEqual(TEXT("both items are stored"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 2))
	{
		return false;
	}

	TArray<FPEPItemStoreChange> Events;
	Fixture.Inventory->OnItemStoreChanged().AddLambda(
		[&Events](const FPEPItemStoreChange& Change)
		{
			Events.Add(Change);
		});

	const FPEPItemResult Consumed = Fixture.Inventory->MutateItem(
		CaseId,
		[](FPEPItemInstance& Item)
		{
			int32 Remaining = 0;
			return PEPItemMutation::ConsumeResource(Item, 2, Remaining).bSuccess;
		},
		FPEPItemAccessContext::System());

	if (!TestTrue(TEXT("emptying the case succeeds"), Consumed.bSuccess))
	{
		return false;
	}

	TestFalse(TEXT("the depleted container is destroyed"),
		Fixture.Inventory->ContainsItem(CaseId));
	TestFalse(TEXT("and takes its contents with it rather than orphaning them"),
		Fixture.Inventory->ContainsItem(ContentsId));
	TestEqual(TEXT("the store is empty"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 0);
	TestEqual(TEXT("both removals are announced"), Events.Num(), 2);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryGrantedGameplayAccessTest,
	"PEP.Inventory.Container.GrantedGameplayAccessRequiresEveryAncestor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryGrantedGameplayAccessTest::RunTest(const FString&)
{
	FFixture Fixture;
	const FName AccessSlot(TEXT("access"));

	const auto MakeContainer = [&Fixture, AccessSlot](
		const FName Name,
		const FGameplayTagContainer& GrantedAccess)
	{
		FPEPContainerSlotDefinition Slot;
		Slot.SlotName = AccessSlot;
		Slot.Mode = EPEPContainerSlotMode::Grid;
		Slot.GridSize = FIntPoint(2, 2);

		FPEPItemFragment_Container Container;
		Container.Slots.Add(Slot);
		Container.GrantedGameplayAccess = GrantedAccess;

		FPEPItemFragment_InventoryShape Shape;
		Shape.Size = FIntPoint(1, 1);
		return Fixture.MakeDefinition(
			Name,
			{FInstancedStruct::Make(Container), FInstancedStruct::Make(Shape)});
	};

	FGameplayTagContainer EquipAndReload;
	EquipAndReload.AddTag(PEPItemTags::GameplayAccess_Equip);
	EquipAndReload.AddTag(PEPItemTags::GameplayAccess_Reload);
	const FGameplayTagContainer NoAccess;
	UPEPItemDefinition* BackpackDefinition =
		MakeContainer(TEXT("GameplayAccessBackpack"), NoAccess);
	UPEPItemDefinition* RigDefinition = MakeContainer(TEXT("GameplayAccessRig"), EquipAndReload);
	UPEPItemDefinition* ItemDefinition =
		Fixture.MakeShapedDefinition(TEXT("GameplayAccessItem"), FIntPoint(1, 1));

	FPEPItemInstance Backpack = FFixture::MakeItem(BackpackDefinition);
	FPEPItemInstance Rig = FFixture::MakeItem(RigDefinition);
	FPEPItemInstance Item = FFixture::MakeItem(ItemDefinition);
	const FPEPItemId BackpackId = Backpack.ItemId;
	const FPEPItemId RigId = Rig.ItemId;
	const FPEPItemId ItemId = Item.ItemId;
	if (!Fixture.AddToRootSlot(Backpack).bSuccess
		|| !Fixture.Inventory->AddItem(
			Rig,
			SlotAt(BackpackId, AccessSlot, 0, 0),
			FPEPItemAccessContext::System()).bSuccess
		|| !Fixture.Inventory->AddItem(
			Item,
			SlotAt(RigId, AccessSlot, 0, 0),
			FPEPItemAccessContext::System()).bSuccess)
	{
		AddError(TEXT("Failed to build the nested access fixture."));
		return false;
	}

	TestFalse(
		TEXT("a granting rig cannot bypass a non-granting backpack ancestor"),
		Fixture.Inventory->HasGrantedGameplayAccess(ItemId, PEPItemTags::GameplayAccess_Equip));
	TestTrue(
		TEXT("a root item requires no container grant"),
		Fixture.Inventory->HasGrantedGameplayAccess(
			BackpackId,
			PEPItemTags::GameplayAccess_Equip));

	Fixture.Inventory->RegisterLocationValidator(
		PEPItemTags::Item_Slot_Content_QuickAccess,
		FPEPItemLocationValidator::CreateLambda(
			[](const FPEPItemInstance& Candidate, const FPEPItemLocation&)
			{
				return FPEPItemResult::Success(Candidate.ItemId);
			}));
	const FPEPItemResult MoveRig = Fixture.Inventory->MoveItem(
		RigId,
		FPEPItemLocation::MakeNamedSlot(PEPItemTags::Item_Slot_Content_QuickAccess),
		FPEPItemAccessContext::System());
	TestTrue(TEXT("the rig can be moved out of the backpack"), MoveRig.bSuccess);
	TestTrue(
		TEXT("the rig grant applies once every ancestor grants or the chain reaches a root"),
		Fixture.Inventory->HasGrantedGameplayAccess(ItemId, PEPItemTags::GameplayAccess_Equip));
	TestFalse(
		TEXT("an ungranted action remains unavailable"),
		Fixture.Inventory->HasGrantedGameplayAccess(ItemId, PEPItemTags::GameplayAccess_Use));

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventorySessionEndsWithRemoteTest,
	"PEP.Inventory.Session.EndsWhenTheRemoteLeavesPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A container that is destroyed, picked up or streamed out must end the sessions open on it at
 * once, by event, not at the next distance check: until then the looter would be shown, and could
 * request against, a store that no longer exists.
 */
bool FPEPInventorySessionEndsWithRemoteTest::RunTest(const FString&)
{
	FFixture Fixture;
	AActor* RemoteOwner = Fixture.World->SpawnActor<AActor>();
	UPEPInventoryComponent* Remote = NewObject<UPEPInventoryComponent>(RemoteOwner);
	Remote->RegisterComponent();
	// EndPlay is only routed to actors that began play; the fixture world never starts it.
	RemoteOwner->DispatchBeginPlay();

	if (!TestTrue(TEXT("the session opens"), Fixture.Session->OpenTransferSession(Remote, 300.0f)))
	{
		return false;
	}

	RemoteOwner->Destroy();

	TestFalse(TEXT("the session ends as the remote actor leaves play, with no timer tick"),
		Fixture.Session->HasTransferSession());
	TestFalse(TEXT("its id is gone with it"), Fixture.Session->GetTransferSessionId().IsValid());

	UPEPInventoryComponent* Next = NewObject<UPEPInventoryComponent>(Fixture.World->SpawnActor<AActor>());
	Next->RegisterComponent();
	TestTrue(TEXT("a later session opens normally"), Fixture.Session->OpenTransferSession(Next, 300.0f));
	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventorySessionCanOpenTest,
	"PEP.Inventory.Session.CanOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Interactables answer "can I be opened" through CanOpenTransferSession and then open through
 * OpenTransferSession. If the two ever disagree, a prompt is offered that then fails, or an open
 * succeeds that the prompt refused; so every refusal is checked against the open as well.
 */
bool FPEPInventorySessionCanOpenTest::RunTest(const FString&)
{
	FFixture Fixture;

	// A bare actor has no root component and so no location; distance needs one.
	auto SpawnRemoteAt = [&Fixture](const FVector& Location)
	{
		AActor* Owner = Fixture.World->SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(Owner);
		Owner->SetRootComponent(Root);
		Root->RegisterComponent();
		Owner->SetActorLocation(Location);
		UPEPInventoryComponent* Inventory = NewObject<UPEPInventoryComponent>(Owner);
		Inventory->RegisterComponent();
		return Inventory;
	};

	auto ExpectRefused = [this, &Fixture](
		const TCHAR* What,
		const UPEPInventoryComponent* Remote,
		const float MaxDistance,
		const FGameplayTag ExpectedReason)
	{
		const FPEPItemResult Result = Fixture.Session->CanOpenTransferSession(Remote, MaxDistance);
		TestFalse(FString::Printf(TEXT("%s: refused"), What), Result.bSuccess);
		TestTrue(FString::Printf(TEXT("%s: reason is %s, got %s"), What,
			*ExpectedReason.ToString(), *Result.ReasonTag.ToString()), Result.ReasonTag == ExpectedReason);
		TestFalse(FString::Printf(TEXT("%s: the open agrees"), What),
			Fixture.Session->OpenTransferSession(const_cast<UPEPInventoryComponent*>(Remote), MaxDistance));
	};

	UPEPInventoryComponent* Near = SpawnRemoteAt(FVector(100.0f, 0.0f, 0.0f));
	UPEPInventoryComponent* Far = SpawnRemoteAt(FVector(1000.0f, 0.0f, 0.0f));
	UPEPInventoryComponent* Remote = SpawnRemoteAt(FVector(100.0f, 0.0f, 0.0f));
	Remote->GetOwner()->SetRole(ROLE_SimulatedProxy);

	ExpectRefused(TEXT("no remote"), nullptr, 300.0f, PEPCoreTags::Reason_InvalidTarget);
	ExpectRefused(TEXT("itself"), Fixture.Inventory, 300.0f, PEPCoreTags::Reason_InvalidTarget);
	ExpectRefused(TEXT("no distance allowed"), Near, 0.0f, PEPCoreTags::Reason_InvalidTarget);
	ExpectRefused(TEXT("remote not authoritative"), Remote, 300.0f, PEPCoreTags::Reason_NotAuthority);
	ExpectRefused(TEXT("too far"), Far, 300.0f, PEPCoreTags::Reason_OutOfRange);

	TestTrue(TEXT("a nearby remote may be opened"),
		Fixture.Session->CanOpenTransferSession(Near, 300.0f).bSuccess);
	if (!TestTrue(TEXT("and the open agrees"), Fixture.Session->OpenTransferSession(Near, 300.0f)))
	{
		return false;
	}

	ExpectRefused(TEXT("too far, with a session open"), Far, 300.0f, PEPCoreTags::Reason_OutOfRange);
	TestTrue(TEXT("a refused open leaves the current session intact"),
		Fixture.Session->HasTransferSession());

	Fixture.Owner->SetRole(ROLE_SimulatedProxy);
	ExpectRefused(TEXT("looter not authoritative"), Near, 300.0f, PEPCoreTags::Reason_NotAuthority);
	Fixture.Owner->SetRole(ROLE_Authority);
	return true;
}

#endif
