#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Item/PEPItemMutation.h"
#include "Item/Internal/PEPItemMutableAccess.h"
#include "Item/PEPItemQuery.h"
#include "PEPInventoryTestFixture.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

/*
 * The save format is the forest AddItemTrees already accepts. What a save must not lose is the
 * one thing this checks: ids, locations and per-item state survive an export and a re-import.
 */

using PEPInventoryTest::FFixture;

namespace
{
	const FName GridName(TEXT("main"));
	const FName MountName(TEXT("magazine"));

	/* A rig with a grid for contents and one attachment mount, so a tree carries both kinds. */
	UPEPItemDefinition* MakeRigDefinition(FFixture& Fixture)
	{
		FPEPContainerSlotDefinition Grid;
		Grid.SlotName = GridName;
		Grid.Mode = EPEPContainerSlotMode::Grid;
		Grid.GridSize = FIntPoint(4, 4);
		Grid.bOwnerCanPut = true;
		Grid.bOwnerCanTake = true;
		Grid.bOwnerCanMoveInside = true;

		FPEPContainerSlotDefinition Mount;
		Mount.SlotName = MountName;
		Mount.Mode = EPEPContainerSlotMode::Single;
		Mount.Purpose = EPEPContainerSlotPurpose::Attachment;
		Mount.RequiredItemTags.AddTag(PEPInventoryTest::Test_Inventory_Item_Magazine);

		FPEPItemFragment_Container Container;
		Container.Slots.Add(Grid);
		Container.Slots.Add(Mount);

		FPEPItemFragment_InventoryShape Shape;
		Shape.Size = FIntPoint(1, 1);

		return Fixture.MakeDefinition(TEXT("SaveRigDef"),
			{ FInstancedStruct::Make(Container), FInstancedStruct::Make(Shape) });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventorySaveExportRoundTripsTest,
	"PEP.Inventory.Save.ExportRoundTrips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventorySaveExportRoundTripsTest::RunTest(const FString&)
{
	FFixture Source;
	UPEPItemDefinition* RigDef = MakeRigDefinition(Source);
	UPEPItemDefinition* AmmoDef = Source.MakeStackableDefinition(TEXT("SaveAmmoDef"), 60);
	UPEPItemDefinition* ToolDef = Source.MakeShapedDefinition(TEXT("SaveToolDef"), FIntPoint(2, 1));
	UPEPItemDefinition* MagazineDef = Source.MakeShapedDefinition(TEXT("SaveMagazineDef"), FIntPoint(1, 1));
	MagazineDef->OwnedTags.AddTag(PEPInventoryTest::Test_Inventory_Item_Magazine);

	const FPEPItemInstance Rig = FFixture::MakeItem(RigDef);
	const FPEPItemId RigId = Rig.ItemId;
	if (!TestTrue(TEXT("the rig is equipped"), Source.AddToRootSlot(Rig).bSuccess))
	{
		return false;
	}

	FPEPItemInstance Ammo = FFixture::MakeItem(AmmoDef);
	TestTrue(TEXT("the ammo stack holds seven"), PEPItemMutation::SetStackCount(Ammo, 7).bSuccess);
	const FPEPItemInstance Tool = FFixture::MakeItem(ToolDef);
	const FPEPItemInstance Magazine = FFixture::MakeItem(MagazineDef);

	const FPEPItemLocation AmmoAt = PEPInventoryGrid::MakeGridLocation(RigId, GridName, FIntPoint(0, 0));
	const FPEPItemLocation ToolAt = PEPInventoryGrid::MakeGridLocation(RigId, GridName, FIntPoint(1, 1), true);
	const FPEPItemLocation MagazineAt = FPEPItemLocation::MakeItemContainer(RigId, MountName);

	if (!TestTrue(TEXT("the ammo is stored"),
			Source.Inventory->AddItem(Ammo, AmmoAt, FPEPItemAccessContext::System()).bSuccess)
		|| !TestTrue(TEXT("the tool is stored rotated"),
			Source.Inventory->AddItem(Tool, ToolAt, FPEPItemAccessContext::System()).bSuccess)
		|| !TestTrue(TEXT("the magazine is mounted"),
			Source.Inventory->AddItem(Magazine, MagazineAt, FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	TArray<FPEPInventoryTreePlacement> Placements;
	Source.Inventory->ExportItemTrees(Placements);

	if (!TestEqual(TEXT("one tree per root entry"), Placements.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("the root keeps its slot"),
		Placements[0].RootLocation == FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag()));
	TestTrue(TEXT("the root is the rig"), Placements[0].Tree.RootItem.ItemId == RigId);
	TestEqual(TEXT("every descendant is in the tree"), Placements[0].Tree.Children.Num(), 3);

	/* A second store, as a fresh session would have: empty, with the same slot claimed. */
	FFixture Restored;
	if (!TestTrue(TEXT("the export restores into an empty store"),
		Restored.Inventory->AddItemTrees(Placements, FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	TestEqual(TEXT("the restored store holds the same number of entries"),
		Restored.Inventory->GetInventoryView().Entries.Num(),
		Source.Inventory->GetInventoryView().Entries.Num());

	for (const FPEPInventoryEntry& Entry : Source.Inventory->GetInventoryView().Entries)
	{
		const FPEPItemId Id = Entry.Item.ItemId;
		const FPEPInventoryEntry* Twin = Restored.Inventory->FindEntry(Id);
		if (!TestNotNull(*FString::Printf(TEXT("item %d kept its id"), Id.Value), Twin))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("item %d kept its location"), Id.Value),
			Twin->Location == Entry.Location);
		TestTrue(*FString::Printf(TEXT("item %d kept its definition"), Id.Value),
			Twin->Item.DefinitionId == Entry.Item.DefinitionId);
	}

	FPEPItemStackData StackData;
	const FPEPItemInstance* RestoredAmmo = Restored.Inventory->FindItem(Ammo.ItemId);
	if (TestNotNull(TEXT("the ammo came back"), RestoredAmmo))
	{
		PEPItemQuery::GetStackData(*RestoredAmmo, StackData);
		TestEqual(TEXT("the stack count survived"), StackData.Count, 7);
	}

	/* The same forest again is a different save, not a merge: every id is already taken. */
	TestFalse(TEXT("restoring twice is refused"),
		Restored.Inventory->CanAddItemTrees(Placements, FPEPItemAccessContext::System()).bSuccess);

	return true;
}

namespace
{
	/* Writes and reads the forest the way a USaveGame-style archive does: SaveGame properties only. */
	TArray<FPEPInventoryTreePlacement> RoundTripSaveGame(TArray<FPEPInventoryTreePlacement>& Placements)
	{
		UScriptStruct* PlacementStruct = FPEPInventoryTreePlacement::StaticStruct();

		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, true);
		FObjectAndNameAsStringProxyArchive WriterProxy(Writer, true);
		WriterProxy.ArIsSaveGame = true;
		int32 Count = Placements.Num();
		WriterProxy << Count;
		for (FPEPInventoryTreePlacement& Placement : Placements)
		{
			PlacementStruct->SerializeItem(WriterProxy, &Placement, nullptr);
		}

		TArray<FPEPInventoryTreePlacement> Loaded;
		FMemoryReader Reader(Bytes, true);
		FObjectAndNameAsStringProxyArchive ReaderProxy(Reader, true);
		ReaderProxy.ArIsSaveGame = true;
		int32 LoadedCount = 0;
		ReaderProxy << LoadedCount;
		Loaded.SetNum(LoadedCount);
		for (FPEPInventoryTreePlacement& Placement : Loaded)
		{
			PlacementStruct->SerializeItem(ReaderProxy, &Placement, nullptr);
		}
		return Loaded;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventorySaveGameArchiveRoundTripsTest,
	"PEP.Inventory.Save.SaveGameArchiveRoundTrips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventorySaveGameArchiveRoundTripsTest::RunTest(const FString&)
{
	FFixture Source;
	UPEPItemDefinition* RigDef = MakeRigDefinition(Source);
	UPEPItemDefinition* AmmoDef = Source.MakeStackableDefinition(TEXT("ArchiveAmmoDef"), 60);
	UPEPItemDefinition* ToolDef = Source.MakeShapedDefinition(TEXT("ArchiveToolDef"), FIntPoint(2, 1));

	const FPEPItemInstance Rig = FFixture::MakeItem(RigDef);
	const FPEPItemId RigId = Rig.ItemId;
	if (!TestTrue(TEXT("the rig is equipped"), Source.AddToRootSlot(Rig).bSuccess))
	{
		return false;
	}

	FPEPItemInstance Ammo = FFixture::MakeItem(AmmoDef);
	TestTrue(TEXT("the ammo stack holds seven"), PEPItemMutation::SetStackCount(Ammo, 7).bSuccess);
	const FPEPItemInstance Tool = FFixture::MakeItem(ToolDef);
	const FPEPItemLocation AmmoAt = PEPInventoryGrid::MakeGridLocation(RigId, GridName, FIntPoint(0, 0));
	const FPEPItemLocation ToolAt = PEPInventoryGrid::MakeGridLocation(RigId, GridName, FIntPoint(1, 1), true);
	if (!TestTrue(TEXT("the ammo is stored"),
			Source.Inventory->AddItem(Ammo, AmmoAt, FPEPItemAccessContext::System()).bSuccess)
		|| !TestTrue(TEXT("the tool is stored rotated"),
			Source.Inventory->AddItem(Tool, ToolAt, FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	TArray<FPEPInventoryTreePlacement> Placements;
	Source.Inventory->ExportItemTrees(Placements);
	if (!TestEqual(TEXT("one tree per root entry"), Placements.Num(), 1))
	{
		return false;
	}

	/* Container runtime state lives on the instance; it must cross the archive like any other state. */
	FPEPItemMutableAccess::FindOrAddState<FPEPItemState_ContainerRuntime>(Placements[0].Tree.RootItem).bLocked = true;

	TArray<FPEPInventoryTreePlacement> Loaded = RoundTripSaveGame(Placements);
	if (!TestEqual(TEXT("the archive keeps every tree"), Loaded.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("the root location survives"), Loaded[0].RootLocation == Placements[0].RootLocation);
	TestTrue(TEXT("the root item survives"), Loaded[0].Tree.RootItem.ItemId == RigId);
	TestTrue(TEXT("the root definition survives"),
		Loaded[0].Tree.RootItem.DefinitionId == Placements[0].Tree.RootItem.DefinitionId);
	TestEqual(TEXT("every descendant survives"), Loaded[0].Tree.Children.Num(), Placements[0].Tree.Children.Num());
	const FPEPItemState_ContainerRuntime* LoadedLock =
		Loaded[0].Tree.RootItem.GetState<FPEPItemState_ContainerRuntime>();
	TestTrue(TEXT("the container lock survives"), LoadedLock && LoadedLock->bLocked);
	for (int32 Index = 0; Index < Loaded[0].Tree.Children.Num() && Index < Placements[0].Tree.Children.Num(); ++Index)
	{
		TestTrue(TEXT("a child keeps its id"),
			Loaded[0].Tree.Children[Index].Item.ItemId == Placements[0].Tree.Children[Index].Item.ItemId);
		TestTrue(TEXT("a child keeps its location, rotation included"),
			Loaded[0].Tree.Children[Index].Location == Placements[0].Tree.Children[Index].Location);
	}

	/*
	 * Location equality would also hold if both sides had lost the placement, so the rotated tool
	 * is read back field by field: the grid placement is an instanced struct inside the location,
	 * and only its SaveGame fields cross an ArIsSaveGame archive.
	 */
	const FPEPItemInstanceTreeEntry* LoadedTool = Loaded[0].Tree.Children.FindByPredicate(
		[&Tool](const FPEPItemInstanceTreeEntry& Child)
		{
			return Child.Item.ItemId == Tool.ItemId;
		});
	if (TestNotNull(TEXT("the rotated tool is in the archive"), LoadedTool))
	{
		const FPEPGridPlacement* LoadedCell = PEPInventoryGrid::FindGridPlacement(LoadedTool->Location);
		if (TestNotNull(TEXT("the tool keeps a grid placement"), LoadedCell))
		{
			TestEqual(TEXT("the tool keeps its cell"), LoadedCell->Position, FIntPoint(1, 1));
			TestTrue(TEXT("the tool stays rotated"), LoadedCell->bRotated);
		}
		TestTrue(TEXT("the tool keeps its whole location"), LoadedTool->Location == ToolAt);
	}
	const FPEPItemInstanceTreeEntry* LoadedAmmo = Loaded[0].Tree.Children.FindByPredicate(
		[&Ammo](const FPEPItemInstanceTreeEntry& Child)
		{
			return Child.Item.ItemId == Ammo.ItemId;
		});
	if (TestNotNull(TEXT("the ammo is in the archive"), LoadedAmmo))
	{
		TestFalse(TEXT("the origin stays spelled as no placement"), LoadedAmmo->Location.Placement.IsValid());
		TestTrue(TEXT("the ammo keeps its whole location"), LoadedAmmo->Location == AmmoAt);
	}

	/* Unlock again so the restore exercises the ordinary path rather than the lock policy. */
	FPEPItemMutableAccess::FindOrAddState<FPEPItemState_ContainerRuntime>(Loaded[0].Tree.RootItem).bLocked = false;

	FFixture Restored;
	if (!TestTrue(TEXT("the archived forest restores into an empty store"),
		Restored.Inventory->AddItemTrees(Loaded, FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	FPEPItemStackData StackData;
	const FPEPItemInstance* RestoredAmmo = Restored.Inventory->FindItem(Ammo.ItemId);
	if (TestNotNull(TEXT("the ammo came back"), RestoredAmmo))
	{
		PEPItemQuery::GetStackData(*RestoredAmmo, StackData);
		TestEqual(TEXT("the stack count survived the archive"), StackData.Count, 7);
	}

	FPEPItemLocation RestoredToolAt;
	TestTrue(TEXT("the restored tool sits where it was saved, still rotated"),
		Restored.Inventory->FindItemLocation(Tool.ItemId, RestoredToolAt) && RestoredToolAt == ToolAt);

	return true;
}

#endif
