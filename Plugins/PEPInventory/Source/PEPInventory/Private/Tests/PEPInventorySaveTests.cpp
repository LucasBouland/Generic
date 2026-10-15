#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Item/PEPItemMutation.h"
#include "Item/PEPItemQuery.h"
#include "PEPInventoryTestFixture.h"

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
		Mount.RequiredItemTags.AddTag(PEPItemTags::Item_Type_Magazine);

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
	MagazineDef->OwnedTags.AddTag(PEPItemTags::Item_Type_Magazine);

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

	const FPEPItemLocation AmmoAt = FPEPItemLocation::MakeItemContainer(RigId, GridName, FIntPoint(0, 0));
	const FPEPItemLocation ToolAt = FPEPItemLocation::MakeItemContainer(RigId, GridName, FIntPoint(1, 1), true);
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

#endif
