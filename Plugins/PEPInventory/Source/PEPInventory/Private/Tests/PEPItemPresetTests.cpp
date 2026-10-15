#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Item/Fragments/PEPItemFragment_Durability.h"
#include "Item/Fragments/PEPItemFragment_InventoryShape.h"
#include "Item/Fragments/PEPItemFragment_Stackable.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemPreset.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemPresetBuildTreeTest,
	"PEP.Items.Preset.BuildsFreshConfiguredTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemPresetBuildTreeTest::RunTest(const FString& Parameters)
{
	UPEPItemDefinition* RootDefinition = NewObject<UPEPItemDefinition>(GetTransientPackage());
	FPEPContainerSlotDefinition AttachmentSlot;
	AttachmentSlot.SlotName = TEXT("Optic");
	AttachmentSlot.Mode = EPEPContainerSlotMode::Single;
	AttachmentSlot.Purpose = EPEPContainerSlotPurpose::Attachment;
	FPEPItemFragment_Container Container;
	Container.Slots.Add(AttachmentSlot);
	RootDefinition->DefinitionFragments.Add(FInstancedStruct::Make(Container));

	UPEPItemDefinition* ChildDefinition = NewObject<UPEPItemDefinition>(GetTransientPackage());
	FPEPItemFragment_Durability DurabilityFragment;
	FPEPItemFragment_InventoryShape ShapeFragment;
	ChildDefinition->DefinitionFragments.Add(FInstancedStruct::Make(DurabilityFragment));
	ChildDefinition->DefinitionFragments.Add(FInstancedStruct::Make(ShapeFragment));

	PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(RootDefinition);
	PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(ChildDefinition);

	UPEPItemPreset* ChildPreset = NewObject<UPEPItemPreset>(GetTransientPackage());
	ChildPreset->RootDefinition = ChildDefinition;
	FPEPItemState_Durability DurabilityOverride;
	DurabilityOverride.Durability = 0.42f;
	ChildPreset->StateOverrides.Add(FInstancedStruct::Make(DurabilityOverride));

	UPEPItemPreset* RootPreset = NewObject<UPEPItemPreset>(GetTransientPackage());
	RootPreset->RootDefinition = RootDefinition;
	FPEPItemPresetChild Child;
	Child.Preset = ChildPreset;
	Child.ContainerSlotName = AttachmentSlot.SlotName;
	RootPreset->Children.Add(Child);

	FPEPItemInstanceTree FirstTree;
	FText Error;
	TestTrue(TEXT("valid preset builds a runtime tree"), RootPreset->CreateItemTreeForEditor(FirstTree, Error));
	TestTrue(TEXT("generated tree is structurally valid"), FirstTree.IsStructurallyValid());
	TestEqual(TEXT("tree contains one attachment"), FirstTree.Children.Num(), 1);
	if (FirstTree.Children.Num() == 1)
	{
		const FPEPItemInstanceTreeEntry& ChildEntry = FirstTree.Children[0];
		TestTrue(
			TEXT("child location references the generated root"),
			ChildEntry.Location.ParentItemId == FirstTree.RootItem.ItemId);
		TestEqual(
			TEXT("child uses the configured slot"),
			ChildEntry.Location.ContainerSlotName,
			AttachmentSlot.SlotName);
		const FPEPItemState_Durability* Durability =
			ChildEntry.Item.GetState<FPEPItemState_Durability>();
		TestTrue(
			TEXT("child state override is applied"),
			Durability && FMath::IsNearlyEqual(Durability->Durability, 0.42f));
	}

	FPEPItemInstanceTree SecondTree;
	TestTrue(TEXT("preset can build a second tree"), RootPreset->CreateItemTreeForEditor(SecondTree, Error));
	TestTrue(
		TEXT("each creation generates a fresh root identity"),
		FirstTree.RootItem.ItemId != SecondTree.RootItem.ItemId);
	if (FirstTree.Children.Num() == 1 && SecondTree.Children.Num() == 1)
	{
		TestTrue(
			TEXT("each creation generates fresh child identities"),
			FirstTree.Children[0].Item.ItemId != SecondTree.Children[0].Item.ItemId);
	}

	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(
		RootDefinition->GetPrimaryAssetId());
	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(
		ChildDefinition->GetPrimaryAssetId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemAttachmentSlotPurposeTest,
	"PEP.Items.Container.AttachmentSlotsAreExplicitAndSingle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemAttachmentSlotPurposeTest::RunTest(const FString& Parameters)
{
	FPEPContainerSlotDefinition DefaultSlot;
	DefaultSlot.SlotName = TEXT("content");
	TestTrue(TEXT("existing and unspecified slots remain private content by default"),
		DefaultSlot.Purpose == EPEPContainerSlotPurpose::Content);

	FPEPContainerSlotDefinition AttachmentSlot;
	AttachmentSlot.SlotName = TEXT("optic");
	AttachmentSlot.Mode = EPEPContainerSlotMode::Single;
	AttachmentSlot.Purpose = EPEPContainerSlotPurpose::Attachment;
	TestTrue(TEXT("a single attachment slot is structurally valid"), AttachmentSlot.IsValid());

	AttachmentSlot.Mode = EPEPContainerSlotMode::Grid;
	TestFalse(TEXT("an attachment slot cannot be a grid"), AttachmentSlot.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemPresetRejectsCycleTest,
	"PEP.Items.Preset.RejectsCycles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemPresetRejectsCycleTest::RunTest(const FString& Parameters)
{
	UPEPItemDefinition* Definition = NewObject<UPEPItemDefinition>(GetTransientPackage());
	FPEPContainerSlotDefinition Slot;
	Slot.SlotName = TEXT("Nested");
	Slot.Mode = EPEPContainerSlotMode::Single;
	FPEPItemFragment_Container Container;
	Container.Slots.Add(Slot);
	Definition->DefinitionFragments.Add(FInstancedStruct::Make(Container));
	PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(Definition);

	UPEPItemPreset* Preset = NewObject<UPEPItemPreset>(GetTransientPackage());
	Preset->RootDefinition = Definition;
	FPEPItemPresetChild Child;
	Child.Preset = Preset;
	Child.ContainerSlotName = Slot.SlotName;
	Preset->Children.Add(Child);

	FPEPItemInstanceTree Tree;
	FText Error;
	TestFalse(TEXT("cyclic preset is rejected"), Preset->CreateItemTreeForEditor(Tree, Error));
	TestFalse(TEXT("failed build returns no partial root"), Tree.RootItem.IsValid());
	TestFalse(TEXT("cycle reports a designer-facing error"), Error.IsEmpty());

	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(
		Definition->GetPrimaryAssetId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemPresetRejectsInvalidDirectChildTest,
	"PEP.Inventory.Preset.RejectsInvalidDirectChildInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemPresetRejectsInvalidDirectChildTest::RunTest(const FString& Parameters)
{
	UPEPItemDefinition* ContainerDefinition = NewObject<UPEPItemDefinition>(GetTransientPackage());
	FPEPContainerSlotDefinition Slot;
	Slot.SlotName = TEXT("Content");
	Slot.Mode = EPEPContainerSlotMode::Single;
	FPEPItemFragment_Container Container;
	Container.Slots.Add(Slot);
	ContainerDefinition->DefinitionFragments.Add(FInstancedStruct::Make(Container));

	UPEPItemDefinition* InvalidChildDefinition = NewObject<UPEPItemDefinition>(GetTransientPackage());
	FPEPItemFragment_Stackable Stackable;
	Stackable.bUseProjectDefaultMaxStackSize = false;
	Stackable.MaxStackSize = 5;
	InvalidChildDefinition->DefinitionFragments.Add(FInstancedStruct::Make(Stackable));
	InvalidChildDefinition->DefinitionFragments.Add(FInstancedStruct::Make(FPEPItemFragment_Durability()));

	PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(ContainerDefinition);
	PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(InvalidChildDefinition);

	UPEPItemPreset* Preset = NewObject<UPEPItemPreset>(GetTransientPackage());
	Preset->RootDefinition = ContainerDefinition;
	FPEPItemPresetChild Child;
	Child.ItemDefinition = InvalidChildDefinition;
	Child.ContainerSlotName = Slot.SlotName;
	Preset->Children.Add(Child);

	FPEPItemInstanceTree Tree;
	FText Error;
	TestFalse(TEXT("preset fails when its direct child creates an invalid stack instance"),
		Preset->CreateItemTreeForEditor(Tree, Error));
	TestFalse(TEXT("failed preset does not return a partial tree"), Tree.RootItem.IsValid());
	TestFalse(TEXT("direct-child validation reports a useful error"), Error.IsEmpty());

	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(
		ContainerDefinition->GetPrimaryAssetId());
	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(
		InvalidChildDefinition->GetPrimaryAssetId());
	return true;
}

#endif
