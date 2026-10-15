#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/PEPCoreTags.h"
#include "Item/PEPItemIdLibrary.h"
#include "PEPInventoryTestFixture.h"

/*
 * The grid lives in the inventory, not in ItemCore's location: a location carries an opaque
 * placement and the store owning the slot reads it. These pin down the two halves of that
 * contract — how a grid placement is spelled on a location, and how each kind of slot reads it.
 */

using PEPInventoryTest::FFixture;

/* Named rather than anonymous, so a unity build cannot fold these into another test's helpers. */
namespace PEPInventoryGridPlacementTestSupport
{
	const FName GridName(TEXT("grid"));
	const FName SocketName(TEXT("socket"));

	/** A carrier with a 4x4 grid and one single slot, both open to any item. */
	UPEPItemDefinition* MakeCarrierDefinition(FFixture& Fixture)
	{
		FPEPContainerSlotDefinition Grid;
		Grid.SlotName = GridName;
		Grid.Mode = EPEPContainerSlotMode::Grid;
		Grid.GridSize = FIntPoint(4, 4);

		FPEPContainerSlotDefinition Socket;
		Socket.SlotName = SocketName;
		Socket.Mode = EPEPContainerSlotMode::Single;

		FPEPItemFragment_Container Container;
		Container.Slots.Add(Grid);
		Container.Slots.Add(Socket);

		FPEPItemFragment_InventoryShape Shape;
		Shape.Size = FIntPoint(1, 1);

		return Fixture.MakeDefinition(TEXT("GridPlacementCarrierDef"),
			{ FInstancedStruct::Make(Container), FInstancedStruct::Make(Shape) });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryGridPlacementSpellingTest,
	"PEP.Inventory.Grid.PlacementSpelling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryGridPlacementSpellingTest::RunTest(const FString&)
{
	using namespace PEPInventoryGridPlacementTestSupport;

	const FPEPItemId ParentId = PEPItemAutomationTestSupport::MakeItemIdForTests();

	const FPEPItemLocation Origin = PEPInventoryGrid::MakeGridLocation(ParentId, GridName, FIntPoint::ZeroValue);
	TestFalse(TEXT("the unrotated origin carries no placement"), Origin.Placement.IsValid());
	TestTrue(TEXT("and is the plain container location"),
		Origin == FPEPItemLocation::MakeItemContainer(ParentId, GridName));
	TestNull(TEXT("no grid placement is found on it"), PEPInventoryGrid::FindGridPlacement(Origin));
	TestEqual(TEXT("it reads as the origin"), PEPInventoryGrid::GetGridPlacement(Origin).Position, FIntPoint::ZeroValue);
	TestFalse(TEXT("unrotated"), PEPInventoryGrid::GetGridPlacement(Origin).bRotated);

	const FPEPItemLocation Turned = PEPInventoryGrid::MakeGridLocation(ParentId, GridName, FIntPoint(2, 1), true);
	const FPEPGridPlacement* TurnedCell = PEPInventoryGrid::FindGridPlacement(Turned);
	if (TestNotNull(TEXT("another cell carries a grid placement"), TurnedCell))
	{
		TestEqual(TEXT("with its cell"), TurnedCell->Position, FIntPoint(2, 1));
		TestTrue(TEXT("and its orientation"), TurnedCell->bRotated);
	}
	TestTrue(TEXT("a placed location is structurally valid"), Turned.IsStructurallyValid());
	TestTrue(TEXT("the same cell built twice is the same location"),
		Turned == PEPInventoryGrid::MakeGridLocation(ParentId, GridName, FIntPoint(2, 1), true));
	TestTrue(TEXT("and hashes the same"),
		GetTypeHash(Turned)
			== GetTypeHash(PEPInventoryGrid::MakeGridLocation(ParentId, GridName, FIntPoint(2, 1), true)));
	TestTrue(TEXT("orientation is part of the location"),
		Turned != PEPInventoryGrid::MakeGridLocation(ParentId, GridName, FIntPoint(2, 1), false));
	TestTrue(TEXT("so is the cell"),
		Turned != PEPInventoryGrid::MakeGridLocation(ParentId, GridName, FIntPoint(1, 2), true));
	TestTrue(TEXT("and a placed cell is not the origin"), Turned != Origin);

	FPEPItemLocation Moving = Turned;
	TestTrue(TEXT("a container location takes a new cell"),
		PEPInventoryGrid::SetGridPlacement(Moving, FIntPoint(3, 0), false));
	TestEqual(TEXT("which is read back"), PEPInventoryGrid::GetGridPlacement(Moving).Position, FIntPoint(3, 0));
	TestTrue(TEXT("back to the origin"), PEPInventoryGrid::SetGridPlacement(Moving, FIntPoint::ZeroValue, false));
	TestFalse(TEXT("the origin is spelled without a placement again"), Moving.Placement.IsValid());

	FPEPItemLocation Named = FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag());
	TestFalse(TEXT("a named slot takes no grid placement"),
		PEPInventoryGrid::SetGridPlacement(Named, FIntPoint(1, 1), false));
	TestFalse(TEXT("and is left untouched"), Named.Placement.IsValid());

	Named.Placement = FInstancedStruct::Make(FPEPGridPlacement(FIntPoint(1, 1), false));
	TestFalse(TEXT("a named slot carrying a placement is malformed"), Named.IsStructurallyValid());

	const FPEPItemLocation NotAPlacement = FPEPItemLocation::MakeItemContainer(
		ParentId, GridName, FInstancedStruct::Make(FPEPItemFragment_InventoryShape()));
	TestFalse(TEXT("a struct that is no placement is malformed"), NotAPlacement.IsStructurallyValid());

	const FPEPItemLocation ForeignPlacement = FPEPItemLocation::MakeItemContainer(
		ParentId, GridName, FInstancedStruct::Make(FPEPItemPlacement()));
	TestTrue(TEXT("another domain's placement is ItemCore-valid"), ForeignPlacement.IsStructurallyValid());
	TestFalse(TEXT("but the inventory cannot read it"), PEPInventoryGrid::IsPlacementWellFormed(ForeignPlacement));

	TestFalse(TEXT("a cell before the grid's first is not well formed"),
		PEPInventoryGrid::IsPlacementWellFormed(
			PEPInventoryGrid::MakeGridLocation(ParentId, GridName, FIntPoint(-1, 0))));
	TestTrue(TEXT("no placement is well formed"), PEPInventoryGrid::IsPlacementWellFormed(Origin));
	TestTrue(TEXT("a grid cell is well formed"), PEPInventoryGrid::IsPlacementWellFormed(Turned));

	FIntPoint OutPosition = FIntPoint::ZeroValue;
	bool bOutRotated = false;
	TestTrue(TEXT("Blueprint reads a placed cell"),
		UPEPInventoryGridLibrary::GetGridPlacement(Turned, OutPosition, bOutRotated));
	TestTrue(TEXT("with its values"), OutPosition == FIntPoint(2, 1) && bOutRotated);
	TestTrue(TEXT("Blueprint reads the origin as a grid cell too"),
		UPEPInventoryGridLibrary::GetGridPlacement(Origin, OutPosition, bOutRotated));
	TestFalse(TEXT("but not a named slot"),
		UPEPInventoryGridLibrary::GetGridPlacement(
			FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag()), OutPosition, bOutRotated));
	TestFalse(TEXT("nor another domain's placement"),
		UPEPInventoryGridLibrary::GetGridPlacement(ForeignPlacement, OutPosition, bOutRotated));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryGridPlacementSlotRulesTest,
	"PEP.Inventory.Grid.SlotsReadPlacements",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryGridPlacementSlotRulesTest::RunTest(const FString&)
{
	using namespace PEPInventoryGridPlacementTestSupport;

	FFixture Fixture;
	const FPEPItemAccessContext System = FPEPItemAccessContext::System();
	const FPEPItemInstance Carrier = FFixture::MakeItem(MakeCarrierDefinition(Fixture));
	const FPEPItemId CarrierId = Carrier.ItemId;
	if (!TestTrue(TEXT("the carrier is equipped"), Fixture.AddToRootSlot(Carrier).bSuccess))
	{
		return false;
	}

	UPEPItemDefinition* SmallDef = Fixture.MakeShapedDefinition(TEXT("GridPlacementSmallDef"), FIntPoint(1, 1));
	UPEPItemDefinition* LongDef = Fixture.MakeShapedDefinition(TEXT("GridPlacementLongDef"), FIntPoint(2, 1));
	const FPEPItemInstance Probe = FFixture::MakeItem(SmallDef);

	/* A grid slot. */
	TestTrue(TEXT("a grid reads a missing placement as its origin"),
		Fixture.Inventory->CanAddItem(Probe, FPEPItemLocation::MakeItemContainer(CarrierId, GridName), System).bSuccess);
	TestTrue(TEXT("and accepts the origin spelled out"),
		Fixture.Inventory->CanAddItem(Probe, FPEPItemLocation::MakeItemContainer(
			CarrierId, GridName, FInstancedStruct::Make(FPEPGridPlacement())), System).bSuccess);
	const FPEPItemResult Negative = Fixture.Inventory->CanAddItem(
		Probe, PEPInventoryGrid::MakeGridLocation(CarrierId, GridName, FIntPoint(0, -1)), System);
	TestFalse(TEXT("a cell before the first is refused"), Negative.bSuccess);
	TestTrue(TEXT("as malformed rather than full"), Negative.ReasonTag == PEPCoreTags::Reason_InvalidTarget);
	const FPEPItemResult Foreign = Fixture.Inventory->CanAddItem(
		Probe, FPEPItemLocation::MakeItemContainer(CarrierId, GridName, FInstancedStruct::Make(FPEPItemPlacement())), System);
	TestFalse(TEXT("a placement that is not a grid one is refused"), Foreign.bSuccess);
	TestTrue(TEXT("as malformed"), Foreign.ReasonTag == PEPCoreTags::Reason_InvalidTarget);

	/* A single slot. */
	TestTrue(TEXT("a single slot takes a location without placement"),
		Fixture.Inventory->CanAddItem(Probe, FPEPItemLocation::MakeItemContainer(CarrierId, SocketName), System).bSuccess);
	TestTrue(TEXT("which is what the grid helper builds at the origin"),
		Fixture.Inventory->CanAddItem(Probe, PEPInventoryGrid::MakeGridLocation(CarrierId, SocketName, FIntPoint::ZeroValue), System).bSuccess);
	const FPEPItemResult SocketCell = Fixture.Inventory->CanAddItem(
		Probe, PEPInventoryGrid::MakeGridLocation(CarrierId, SocketName, FIntPoint(1, 0)), System);
	TestFalse(TEXT("a single slot refuses a grid cell"), SocketCell.bSuccess);
	TestTrue(TEXT("as an invalid target"), SocketCell.ReasonTag == PEPCoreTags::Reason_InvalidTarget);
	TestFalse(TEXT("and a rotation"),
		Fixture.Inventory->CanAddItem(Probe, PEPInventoryGrid::MakeGridLocation(CarrierId, SocketName, FIntPoint::ZeroValue, true), System).bSuccess);
	TestFalse(TEXT("even the origin spelled out"),
		Fixture.Inventory->CanAddItem(Probe, FPEPItemLocation::MakeItemContainer(
			CarrierId, SocketName, FInstancedStruct::Make(FPEPGridPlacement())), System).bSuccess);

	/* The placement decides the footprint: a 2x1 turned at (1,0) covers (1,0) and (1,1). */
	const FPEPItemInstance Long = FFixture::MakeItem(LongDef);
	const FPEPItemLocation LongAt = PEPInventoryGrid::MakeGridLocation(CarrierId, GridName, FIntPoint(1, 0), true);
	if (!TestTrue(TEXT("a turned item is stored"), Fixture.Inventory->AddItem(Long, LongAt, System).bSuccess))
	{
		return false;
	}
	TestFalse(TEXT("its turned footprint covers the cell below"),
		Fixture.Inventory->CanAddItem(Probe, PEPInventoryGrid::MakeGridLocation(CarrierId, GridName, FIntPoint(1, 1)), System).bSuccess);
	TestTrue(TEXT("but not the cell beside"),
		Fixture.Inventory->CanAddItem(Probe, PEPInventoryGrid::MakeGridLocation(CarrierId, GridName, FIntPoint(2, 0)), System).bSuccess);
	FPEPItemId Found;
	TestTrue(TEXT("it is found at its placed location"),
		Fixture.Inventory->FindItemAtLocation(LongAt, Found) && Found == Long.ItemId);

	/* Auto-placement hands back the canonical spelling: the origin as no placement. */
	const FPEPItemInstance FirstPickup = FFixture::MakeItem(SmallDef);
	if (!TestTrue(TEXT("a pickup finds a cell"),
		Fixture.Inventory->AddPickupItem(FirstPickup, System).bSuccess))
	{
		return false;
	}
	FPEPItemLocation FirstAt;
	Fixture.Inventory->FindItemLocation(FirstPickup.ItemId, FirstAt);
	TestTrue(TEXT("the first free cell is the origin, spelled without a placement"),
		FirstAt == FPEPItemLocation::MakeItemContainer(CarrierId, GridName));

	const FPEPItemInstance SecondPickup = FFixture::MakeItem(SmallDef);
	if (!TestTrue(TEXT("a second pickup finds a cell"),
		Fixture.Inventory->AddPickupItem(SecondPickup, System).bSuccess))
	{
		return false;
	}
	FPEPItemLocation SecondAt;
	Fixture.Inventory->FindItemLocation(SecondPickup.ItemId, SecondAt);
	TestTrue(TEXT("the next free cell in row order is past the turned item"),
		SecondAt == PEPInventoryGrid::MakeGridLocation(CarrierId, GridName, FIntPoint(2, 0)));

	return true;
}

#endif
