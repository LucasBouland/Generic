#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PEPInventoryTestFixture.h"

#include "Inventory/PEPInventorySettings.h"
#include "Item/PEPItemPresetTypes.h"
#include "Loadout/PEPLoadoutMutation.h"

using PEPInventoryTest::FFixture;

/* Named rather than anonymous: in a unity build every test file shares one translation unit. */
namespace PEPPruneSafetyTestSupport
{
	const FName SlotName(TEXT("main"));

	/** Adds a child of Definition under ParentId and returns its id; no reference outlives the add. */
	FPEPItemId AddChild(
		FPEPItemInstanceTree& Tree,
		const UPEPItemDefinition* Definition,
		const FPEPItemId ParentId,
		const FIntPoint Cell,
		const FName InSlotName = SlotName)
	{
		FPEPItemInstanceTreeEntry& Entry = Tree.Children.AddDefaulted_GetRef();
		Entry.Item = FFixture::MakeItem(Definition);
		Entry.Location = FPEPItemLocation::MakeItemContainer(ParentId, InSlotName, Cell);
		return Entry.Item.ItemId;
	}

	FPEPInventoryTreePlacement MakePlacement(FPEPItemInstanceTree&& Tree)
	{
		FPEPInventoryTreePlacement Placement;
		Placement.RootLocation = FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag());
		Placement.Tree = MoveTemp(Tree);
		return Placement;
	}

	/** A primary asset type no asset manager was ever told about. */
	FPrimaryAssetId MakeUnregisteredTypeId()
	{
		return FPrimaryAssetId(FPrimaryAssetType(TEXT("PEPTestUnregisteredType")), TEXT("Ghost"));
	}
}

using PEPPruneSafetyTestSupport::AddChild;
using PEPPruneSafetyTestSupport::MakePlacement;

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPPruneKeepsUnknownDefinitionTypesTest,
	"PEP.Inventory.Tree.PruneKeepsItemsWhoseTypeCannotBeResolved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A definition of a primary asset type the asset manager does not know is unanswerable, not
 * absent: the scan may simply not be configured on this machine. Pruning on that would rewrite a
 * save on the strength of a question nobody could answer.
 */
bool FPEPPruneKeepsUnknownDefinitionTypesTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef = Fixture.MakeContainerDefinition(
		TEXT("PruneSafetyUnknownBagDef"), PEPPruneSafetyTestSupport::SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* SmallDef = Fixture.MakeShapedDefinition(
		TEXT("PruneSafetyUnknownSmallDef"), FIntPoint(1, 1));

	FPEPItemInstanceTree Tree;
	Tree.RootItem = FFixture::MakeItem(BagDef);
	const FPEPItemId RootId = Tree.RootItem.ItemId;
	const FPEPItemId UnknownId = AddChild(Tree, SmallDef, RootId, FIntPoint(0, 0));
	Tree.Children.Last().Item.DefinitionId = PEPPruneSafetyTestSupport::MakeUnregisteredTypeId();

	TArray<FPEPInventoryTreePlacement> Placements;
	Placements.Add(MakePlacement(MoveTemp(Tree)));

	FPEPInventoryPruneReport Report;
	PEPLoadoutMutation::PruneMissingDefinitions(Placements, Report);

	TestTrue(TEXT("nothing is pruned on an unanswerable question"), Report.IsEmpty());
	TestTrue(TEXT("nothing is reported missing"), Report.MissingDefinitions.IsEmpty());
	if (!TestEqual(TEXT("the tree survives"), Placements.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("with its child"), Placements[0].Tree.Children.Num(), 1);
	TestTrue(TEXT("the very child whose type is unknown"),
		Placements[0].Tree.Children.Num() == 1 && Placements[0].Tree.Children[0].Item.ItemId == UnknownId);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPPruneReportsInvalidDefinitionIdsTest,
	"PEP.Inventory.Tree.PruneReportsInvalidDefinitionIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/* An id that names nothing can never be restored: it is pruned, and the loss is still reported. */
bool FPEPPruneReportsInvalidDefinitionIdsTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef = Fixture.MakeContainerDefinition(
		TEXT("PruneSafetyInvalidBagDef"), PEPPruneSafetyTestSupport::SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* SmallDef = Fixture.MakeShapedDefinition(
		TEXT("PruneSafetyInvalidSmallDef"), FIntPoint(1, 1));

	FPEPItemInstanceTree Tree;
	Tree.RootItem = FFixture::MakeItem(BagDef);
	const FPEPItemId RootId = Tree.RootItem.ItemId;
	const FPEPItemId BrokenId = AddChild(Tree, BagDef, RootId, FIntPoint(0, 0));
	Tree.Children.Last().Item.DefinitionId = FPrimaryAssetId();
	const FPEPItemId HeldId = AddChild(Tree, SmallDef, BrokenId, FIntPoint(0, 0));
	const FPEPItemId SurvivorId = AddChild(Tree, SmallDef, RootId, FIntPoint(1, 0));

	TArray<FPEPInventoryTreePlacement> Placements;
	Placements.Add(MakePlacement(MoveTemp(Tree)));

	FPEPInventoryPruneReport Report;
	PEPLoadoutMutation::PruneMissingDefinitions(Placements, Report);

	if (!TestEqual(TEXT("the tree survives"), Placements.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("minus the broken item and what it held"), Placements[0].Tree.Children.Num(), 1);
	TestTrue(TEXT("leaving the healthy sibling"),
		Placements[0].Tree.Children.Num() == 1 && Placements[0].Tree.Children[0].Item.ItemId == SurvivorId);
	TestEqual(TEXT("both losses are reported"), Report.RemovedItemIds.Num(), 2);
	TestTrue(TEXT("the broken item"), Report.RemovedItemIds.Contains(BrokenId));
	TestTrue(TEXT("and its content"), Report.RemovedItemIds.Contains(HeldId));
	TestEqual(TEXT("one missing definition is named"), Report.MissingDefinitions.Num(), 1);
	TestTrue(TEXT("and it is the invalid id"), Report.MissingDefinitions.Contains(FPrimaryAssetId()));

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPPruneOrderIndependenceTest,
	"PEP.Inventory.Tree.PruneRemovesDescendantsListedBeforeTheirParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Children come in no particular order: a grandchild may be listed before the parent that
 * condemns it. Whole dead trees go too, and whatever survives keeps its order.
 */
bool FPEPPruneOrderIndependenceTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef = Fixture.MakeContainerDefinition(
		TEXT("PruneSafetyOrderBagDef"), PEPPruneSafetyTestSupport::SlotName, FIntPoint(4, 4));
	UPEPItemDefinition* SmallDef = Fixture.MakeShapedDefinition(
		TEXT("PruneSafetyOrderSmallDef"), FIntPoint(1, 1));
	UPEPItemDefinition* DoomedDef = Fixture.MakeContainerDefinition(
		TEXT("PruneSafetyOrderDoomedDef"), PEPPruneSafetyTestSupport::SlotName, FIntPoint(2, 2));

	/* A tree whose own root is about to vanish, listed first. */
	FPEPItemInstanceTree DeadTree;
	DeadTree.RootItem = FFixture::MakeItem(DoomedDef);
	const FPEPItemId DeadRootId = DeadTree.RootItem.ItemId;
	const FPEPItemId DeadContentId = AddChild(DeadTree, SmallDef, DeadRootId, FIntPoint(0, 0));

	/* A healthy rig holding a doomed pouch, with the pouch's content listed before the pouch. */
	FPEPItemInstanceTree Rig;
	Rig.RootItem = FFixture::MakeItem(BagDef);
	const FPEPItemId RigId = Rig.RootItem.ItemId;
	const FPEPItemId PouchId = PEPItemAutomationTestSupport::MakeItemIdForTests();
	FPEPItemInstanceTreeEntry& EarlyGrandchild = Rig.Children.AddDefaulted_GetRef();
	EarlyGrandchild.Item = FFixture::MakeItem(SmallDef);
	EarlyGrandchild.Location = FPEPItemLocation::MakeItemContainer(
		PouchId, PEPPruneSafetyTestSupport::SlotName, FIntPoint(0, 0));
	const FPEPItemId GrandchildId = EarlyGrandchild.Item.ItemId;
	const FPEPItemId FirstSurvivorId = AddChild(Rig, SmallDef, RigId, FIntPoint(0, 0));
	FPEPItemInstanceTreeEntry& Pouch = Rig.Children.AddDefaulted_GetRef();
	Pouch.Item = FFixture::MakeItem(DoomedDef);
	/* The id the grandchild already names, as a save that wrote the pouch later would hold it. */
	Pouch.Item.ItemId = PouchId;
	Pouch.Location = FPEPItemLocation::MakeItemContainer(
		RigId, PEPPruneSafetyTestSupport::SlotName, FIntPoint(1, 0));
	const FPEPItemId SecondSurvivorId = AddChild(Rig, SmallDef, RigId, FIntPoint(2, 0));

	FPEPItemInstanceTree Leaf;
	Leaf.RootItem = FFixture::MakeItem(SmallDef);
	const FPEPItemId LeafId = Leaf.RootItem.ItemId;

	TArray<FPEPInventoryTreePlacement> Placements;
	Placements.Add(MakePlacement(MoveTemp(DeadTree)));
	Placements.Add(MakePlacement(MoveTemp(Rig)));
	Placements.Add(MakePlacement(MoveTemp(Leaf)));

	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(
		DoomedDef->GetPrimaryAssetId());

	FPEPInventoryPruneReport Report;
	PEPLoadoutMutation::PruneMissingDefinitions(Placements, Report);

	if (!TestEqual(TEXT("the dead tree is gone and the other two remain"), Placements.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("in their original order: the rig first"), Placements[0].Tree.RootItem.ItemId == RigId);
	TestTrue(TEXT("then the leaf"), Placements[1].Tree.RootItem.ItemId == LeafId);

	const TArray<FPEPItemInstanceTreeEntry>& RigChildren = Placements[0].Tree.Children;
	if (!TestEqual(TEXT("the rig loses the pouch and what it held"), RigChildren.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("its survivors keep their order"),
		RigChildren[0].Item.ItemId == FirstSurvivorId && RigChildren[1].Item.ItemId == SecondSurvivorId);

	TestEqual(TEXT("every loss is reported"), Report.RemovedItemIds.Num(), 4);
	TestTrue(TEXT("the dead root"), Report.RemovedItemIds.Contains(DeadRootId));
	TestTrue(TEXT("its content"), Report.RemovedItemIds.Contains(DeadContentId));
	TestTrue(TEXT("the pouch"), Report.RemovedItemIds.Contains(PouchId));
	TestTrue(TEXT("and the grandchild listed before it"), Report.RemovedItemIds.Contains(GrandchildId));
	TestEqual(TEXT("the missing definition is named once"), Report.MissingDefinitions.Num(), 1);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemTreeStructuralValidationTest,
	"PEP.Inventory.Tree.StructuralValidationIsOrderIndependentAndCatchesCycles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemTreeStructuralValidationTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef = Fixture.MakeContainerDefinition(
		TEXT("TreeShapeBagDef"), PEPPruneSafetyTestSupport::SlotName, FIntPoint(4, 4));

	/* A chain three deep, listed deepest first: every parent appears after its child. */
	FPEPItemInstanceTree Tree;
	Tree.RootItem = FFixture::MakeItem(BagDef);
	const FPEPItemId RootId = Tree.RootItem.ItemId;
	const FPEPItemId FirstId = PEPItemAutomationTestSupport::MakeItemIdForTests();
	const FPEPItemId SecondId = PEPItemAutomationTestSupport::MakeItemIdForTests();

	const FPEPItemId ThirdId = AddChild(Tree, BagDef, SecondId, FIntPoint(0, 0));
	FPEPItemInstanceTreeEntry& Second = Tree.Children.AddDefaulted_GetRef();
	Second.Item = FFixture::MakeItem(BagDef);
	Second.Item.ItemId = SecondId;
	Second.Location = FPEPItemLocation::MakeItemContainer(FirstId, PEPPruneSafetyTestSupport::SlotName);
	FPEPItemInstanceTreeEntry& First = Tree.Children.AddDefaulted_GetRef();
	First.Item = FFixture::MakeItem(BagDef);
	First.Item.ItemId = FirstId;
	First.Location = FPEPItemLocation::MakeItemContainer(RootId, PEPPruneSafetyTestSupport::SlotName);

	TestTrue(TEXT("a chain listed child-first is valid"), Tree.IsStructurallyValid());

	FPEPItemInstanceTree Cycle = Tree;
	Cycle.Children.Last().Location.ParentItemId = ThirdId;
	TestFalse(TEXT("a parent cycle that never reaches the root is refused"), Cycle.IsStructurallyValid());

	FPEPItemInstanceTree Orphaned = Tree;
	Orphaned.Children[0].Location.ParentItemId = PEPItemAutomationTestSupport::MakeItemIdForTests();
	TestFalse(TEXT("a parent outside the tree is refused"), Orphaned.IsStructurallyValid());

	FPEPItemInstanceTree Duplicated = Tree;
	const FPEPItemInstanceTreeEntry Repeated = Duplicated.Children[0];
	Duplicated.Children.Add(Repeated);
	TestFalse(TEXT("an id listed twice is refused"), Duplicated.IsStructurallyValid());

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemTreeSlotOverlapTest,
	"PEP.Inventory.Tree.DetailedValidationComparesFootprintsPerSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/* Only items sharing a slot can collide; the same cell in two slots of one parent is no overlap. */
bool FPEPItemTreeSlotOverlapTest::RunTest(const FString&)
{
	FFixture Fixture;

	FPEPContainerSlotDefinition Left;
	Left.SlotName = TEXT("left");
	Left.Mode = EPEPContainerSlotMode::Grid;
	Left.GridSize = FIntPoint(2, 2);
	FPEPContainerSlotDefinition Right = Left;
	Right.SlotName = TEXT("right");
	FPEPItemFragment_Container Container;
	Container.Slots = { Left, Right };
	FPEPItemFragment_InventoryShape Shape;
	Shape.Size = FIntPoint(1, 1);
	UPEPItemDefinition* VestDef = Fixture.MakeDefinition(
		TEXT("TreeOverlapVestDef"),
		{ FInstancedStruct::Make(Container), FInstancedStruct::Make(Shape) });
	UPEPItemDefinition* SmallDef = Fixture.MakeShapedDefinition(TEXT("TreeOverlapSmallDef"), FIntPoint(1, 1));

	FPEPItemInstanceTree Tree;
	Tree.RootItem = FFixture::MakeItem(VestDef);
	const FPEPItemId RootId = Tree.RootItem.ItemId;
	AddChild(Tree, SmallDef, RootId, FIntPoint(0, 0), Left.SlotName);
	AddChild(Tree, SmallDef, RootId, FIntPoint(0, 0), Right.SlotName);
	AddChild(Tree, SmallDef, RootId, FIntPoint(1, 1), Left.SlotName);

	FPEPItemInstanceTreeValidationResult Result;
	TestTrue(TEXT("the same cell in two different slots is not an overlap"), Tree.ValidateDetailed(Result));

	FPEPItemInstanceTree Overlapping = Tree;
	AddChild(Overlapping, SmallDef, RootId, FIntPoint(0, 0), Left.SlotName);
	TestFalse(TEXT("the same cell twice in one slot is"), Overlapping.ValidateDetailed(Result));

	/* Position + size would wrap here and land inside the grid; it must be refused instead. */
	FPEPItemInstanceTree FarAway = Tree;
	AddChild(FarAway, SmallDef, RootId, FIntPoint(MAX_int32, 0), Right.SlotName);
	TestFalse(TEXT("a position at the integer limit is outside the grid"), FarAway.ValidateDetailed(Result));

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryShapeReportsInvalidTest,
	"PEP.Inventory.Settings.EffectiveShapeReportsAnInvalidSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/* A non-positive size used to become 1x1 and report success, which made every failure branch dead. */
bool FPEPInventoryShapeReportsInvalidTest::RunTest(const FString&)
{
	FPEPItemFragment_InventoryShape Shape;
	FIntPoint Size;
	bool bCanRotate = true;

	Shape.Size = FIntPoint(0, 2);
	TestFalse(TEXT("a zero dimension is no shape"),
		PEPInventorySettings().GetEffectiveInventoryShape(&Shape, Size, bCanRotate));
	TestTrue(TEXT("and reports no size"), Size == FIntPoint::ZeroValue);

	Shape.Size = FIntPoint(2, -1);
	TestFalse(TEXT("nor is a negative one"),
		PEPInventorySettings().GetEffectiveInventoryShape(&Shape, Size, bCanRotate));

	Shape.Size = FIntPoint(2, 1);
	TestTrue(TEXT("a positive size is"),
		PEPInventorySettings().GetEffectiveInventoryShape(&Shape, Size, bCanRotate));
	TestTrue(TEXT("and is reported as authored"), Size == FIntPoint(2, 1));

	return true;
}

#endif
