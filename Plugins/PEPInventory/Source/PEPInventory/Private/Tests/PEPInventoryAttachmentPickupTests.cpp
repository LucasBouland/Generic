#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PEPInventoryTestFixture.h"
#include "Item/PEPItemPresetTypes.h"
#include "Item/PEPItemQuery.h"

using PEPInventoryTest::FFixture;

namespace
{
	FPEPContainerSlotDefinition MakeAttachmentSlot(const FName Name)
	{
		FPEPContainerSlotDefinition Slot;
		Slot.SlotName = Name;
		Slot.Mode = EPEPContainerSlotMode::Single;
		Slot.Purpose = EPEPContainerSlotPurpose::Attachment;
		Slot.RequiredItemTags.AddTag(PEPItemTags::Item_Type_Magazine);
		return Slot;
	}

	UPEPItemDefinition* MakeSlotDefinition(
		FFixture& Fixture, const FName Name, const TArray<FPEPContainerSlotDefinition>& Slots)
	{
		FPEPItemFragment_Container Container;
		Container.Slots = Slots;
		FPEPItemFragment_InventoryShape Shape;
		Shape.Size = FIntPoint(1, 1);
		return Fixture.MakeDefinition(Name,
			{ FInstancedStruct::Make(Container), FInstancedStruct::Make(Shape) });
	}

	void RegisterSecondEquipmentSlot(FFixture& Fixture)
	{
		Fixture.Inventory->RegisterLocationValidator(PEPItemTags::Item_Slot_Content_QuickAccess,
			FPEPItemLocationValidator::CreateLambda(
				[](const FPEPItemInstance& Item, const FPEPItemLocation&)
				{
					return FPEPItemResult::Success(Item.ItemId);
				}));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPEPInventoryAttachmentPickupPriorityTest,
	"PEP.Inventory.Pickup.AttachmentsBeforeStorage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryAttachmentPickupPriorityTest::RunTest(const FString&)
{
	FFixture Fixture;
	RegisterSecondEquipmentSlot(Fixture);

	FPEPContainerSlotDefinition Pocket;
	Pocket.SlotName = TEXT("pocket");
	Pocket.GridSize = FIntPoint(3, 1);
	FPEPContainerSlotDefinition Incompatible = MakeAttachmentSlot(TEXT("incompatible"));
	Incompatible.RequiredItemTags.Reset();
	Incompatible.RequiredItemTags.AddTag(PEPItemTags::Item_Type_Key);
	FPEPContainerSlotDefinition ContentSingle = MakeAttachmentSlot(TEXT("content_single"));
	ContentSingle.Purpose = EPEPContainerSlotPurpose::Content;
	FPEPContainerSlotDefinition Forbidden = MakeAttachmentSlot(TEXT("forbidden"));
	Forbidden.bOwnerCanPut = false;

	// The first equipped root has storage and rejected slots. The second owns the valid mount.
	const FPEPItemInstance Rig = FFixture::MakeItem(MakeSlotDefinition(Fixture,
		TEXT("AttachmentPickupRig"), { Pocket, Incompatible, ContentSingle, Forbidden }));
	const FName MountName(TEXT("magazine"));
	const FPEPItemInstance Weapon = FFixture::MakeItem(MakeSlotDefinition(Fixture,
		TEXT("AttachmentPickupWeapon"), { MakeAttachmentSlot(MountName) }));
	if (!TestTrue(TEXT("first equipment root added"), Fixture.AddToRootSlot(Rig).bSuccess)
		|| !TestTrue(TEXT("second equipment root added"), Fixture.Inventory->AddItem(Weapon,
			FPEPItemLocation::MakeNamedSlot(PEPItemTags::Item_Slot_Content_QuickAccess),
			FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}

	UPEPItemDefinition* MagazineDef = Fixture.MakeShapedDefinition(
		TEXT("AttachmentPickupMagazine"), FIntPoint(1, 1));
	MagazineDef->OwnedTags.AddTag(PEPItemTags::Item_Type_Magazine);
	const FPEPItemInstance Magazine = FFixture::MakeItem(MagazineDef);
	TestTrue(TEXT("preview accepts a compatible attachment"), Fixture.Inventory->CanAddPickupItem(
		Magazine, FPEPItemAccessContext::System()).bSuccess);
	TestNull(TEXT("preview does not mutate inventory"), Fixture.Inventory->FindItem(Magazine.ItemId));
	if (!TestTrue(TEXT("pickup attaches"), Fixture.Inventory->AddPickupItem(
		Magazine, FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}
	FPEPItemLocation Location;
	TestTrue(TEXT("pickup location found"), Fixture.Inventory->FindItemLocation(Magazine.ItemId, Location));
	TestTrue(TEXT("all equipped roots searched before first root's grid"), Location.ParentItemId == Weapon.ItemId);
	TestEqual(TEXT("correct attachment slot"), Location.ContainerSlotName, MountName);
	TestTrue(TEXT("single slot uses zero cell and no rotation"),
		Location.GridPosition == FIntPoint::ZeroValue && !Location.bRotated);

	const FPEPItemInstance Second = FFixture::MakeItem(MagazineDef);
	TestTrue(TEXT("occupied mount falls back to storage in preview"),
		Fixture.Inventory->CanAddPickupItem(Second, FPEPItemAccessContext::System()).bSuccess);
	TestTrue(TEXT("occupied mount falls back to storage in execution"),
		Fixture.Inventory->AddPickupItem(Second, FPEPItemAccessContext::System()).bSuccess);
	Fixture.Inventory->FindItemLocation(Second.ItemId, Location);
	TestTrue(TEXT("incompatible, forbidden, content-only and occupied single slots skipped"),
		Location.ParentItemId == Rig.ItemId && Location.ContainerSlotName == Pocket.SlotName);

	// A carried weapon also must not be mounted automatically: no attachment tag.
	const FPEPItemInstance Other = FFixture::MakeItem(Fixture.MakeShapedDefinition(
		TEXT("AttachmentPickupOther"), FIntPoint(1, 1)));
	TestTrue(TEXT("ordinary pickup still finds storage"), Fixture.Inventory->AddPickupItem(
		Other, FPEPItemAccessContext::System()).bSuccess);
	Fixture.Inventory->FindItemLocation(Other.ItemId, Location);
	TestTrue(TEXT("ordinary pickup uses the pocket"),
		Location.ParentItemId == Rig.ItemId && Location.ContainerSlotName == Pocket.SlotName);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPEPInventoryAttachmentPickupTreeTest,
	"PEP.Inventory.Pickup.AttachmentTreePreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryAttachmentPickupTreeTest::RunTest(const FString&)
{
	FFixture Fixture;
	const FName MountName(TEXT("magazine"));
	const FPEPItemInstance Weapon = FFixture::MakeItem(MakeSlotDefinition(Fixture,
		TEXT("AttachmentTreeWeapon"), { MakeAttachmentSlot(MountName) }));
	if (!TestTrue(TEXT("weapon equipped"), Fixture.AddToRootSlot(Weapon).bSuccess))
	{
		return false;
	}
	const FName ContentsName(TEXT("contents"));
	UPEPItemDefinition* MagazineDef = Fixture.MakeContainerDefinition(
		TEXT("AttachmentTreeMagazine"), ContentsName, FIntPoint(1, 1));
	MagazineDef->OwnedTags.AddTag(PEPItemTags::Item_Type_Magazine);
	FPEPItemInstanceTree Tree;
	Tree.RootItem = FFixture::MakeItem(MagazineDef);
	FPEPItemInstanceTreeEntry& Child = Tree.Children.AddDefaulted_GetRef();
	Child.Item = FFixture::MakeItem(Fixture.MakeShapedDefinition(TEXT("AttachmentTreeChild"), FIntPoint(1, 1)));
	Child.Location = FPEPItemLocation::MakeItemContainer(Tree.RootItem.ItemId, ContentsName);
	const FPEPItemId ChildId = Child.Item.ItemId;
	TestTrue(TEXT("tree preview can attach even with no storage"),
		Fixture.Inventory->CanAddPickupItemTree(Tree, FPEPItemAccessContext::System()).bSuccess);
	if (!TestTrue(TEXT("tree pickup attaches"),
		Fixture.Inventory->AddPickupItemTree(Tree, FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}
	FPEPItemLocation Location;
	Fixture.Inventory->FindItemLocation(Tree.RootItem.ItemId, Location);
	TestTrue(TEXT("tree root attached to weapon"),
		Location.ParentItemId == Weapon.ItemId && Location.ContainerSlotName == MountName);
	TestTrue(TEXT("child preserved"), Fixture.Inventory->FindItemLocation(ChildId, Location));
	TestTrue(TEXT("child stays inside picked-up attachment"),
		Location.ParentItemId == Tree.RootItem.ItemId && Location.ContainerSlotName == ContentsName);

	const FPEPItemInstance Overflow = FFixture::MakeItem(MagazineDef);
	TestFalse(TEXT("occupied attachment with no storage refused in preview"),
		Fixture.Inventory->CanAddPickupItem(Overflow, FPEPItemAccessContext::System()).bSuccess);
	TestFalse(TEXT("occupied attachment with no storage refused in execution"),
		Fixture.Inventory->AddPickupItem(Overflow, FPEPItemAccessContext::System()).bSuccess);
	TestNull(TEXT("failed pickup does not add an item"), Fixture.Inventory->FindItem(Overflow.ItemId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPEPInventoryAttachmentPickupBeforeMergeTest,
	"PEP.Inventory.Pickup.AttachmentsBeforeStackMerging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryAttachmentPickupBeforeMergeTest::RunTest(const FString&)
{
	FFixture Fixture;
	FPEPContainerSlotDefinition Pocket;
	Pocket.SlotName = TEXT("pocket");
	Pocket.GridSize = FIntPoint(1, 1);
	const FName MountName(TEXT("magazine"));
	const FPEPItemInstance Weapon = FFixture::MakeItem(MakeSlotDefinition(Fixture,
		TEXT("StackAttachmentWeapon"), { Pocket, MakeAttachmentSlot(MountName) }));
	if (!TestTrue(TEXT("weapon equipped"), Fixture.AddToRootSlot(Weapon).bSuccess))
	{
		return false;
	}
	UPEPItemDefinition* MagazineDef = Fixture.MakeStackableDefinition(TEXT("StackAttachmentMagazine"), 10);
	MagazineDef->OwnedTags.AddTag(PEPItemTags::Item_Type_Magazine);
	const FPEPItemInstance Stored = FFixture::MakeItem(MagazineDef);
	if (!TestTrue(TEXT("compatible stack stored in pocket"), Fixture.Inventory->AddItem(Stored,
		FPEPItemLocation::MakeItemContainer(Weapon.ItemId, Pocket.SlotName),
		FPEPItemAccessContext::System()).bSuccess))
	{
		return false;
	}
	const FPEPItemInstance Pickup = FFixture::MakeItem(MagazineDef);
	TestTrue(TEXT("stackable attachment preview accepted"),
		Fixture.Inventory->CanAddPickupItem(Pickup, FPEPItemAccessContext::System()).bSuccess);
	TestTrue(TEXT("stackable attachment mounted"),
		Fixture.Inventory->AddPickupItem(Pickup, FPEPItemAccessContext::System()).bSuccess);
	FPEPItemLocation Location;
	TestTrue(TEXT("pickup remains a distinct item rather than merging"),
		Fixture.Inventory->FindItemLocation(Pickup.ItemId, Location));
	TestTrue(TEXT("pickup mounted on weapon"),
		Location.ParentItemId == Weapon.ItemId && Location.ContainerSlotName == MountName);
	FPEPItemStackData Data;
	const FPEPItemInstance* Carried = Fixture.Inventory->FindItem(Stored.ItemId);
	if (TestNotNull(TEXT("carried stack still exists"), Carried))
	{
		TestTrue(TEXT("carried stack data readable"), PEPItemQuery::GetStackData(*Carried, Data));
		TestEqual(TEXT("carried stack was not topped up"), Data.Count, 1);
	}
	return true;
}

#endif
