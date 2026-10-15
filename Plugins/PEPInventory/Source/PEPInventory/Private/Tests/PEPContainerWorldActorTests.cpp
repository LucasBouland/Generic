#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Item/PEPContainerItemWorldActor.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Item/Fragments/PEPItemFragment_WorldPresentation.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemPreset.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemWorldActorAttachmentProjectionTest,
	"PEP.Items.WorldActor.ProjectsAttachmentsButNotContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemWorldActorAttachmentProjectionTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	UPEPItemDefinition* RootDefinition = NewObject<UPEPItemDefinition>(GetTransientPackage());
	UPEPItemDefinition* AttachmentDefinition =
		NewObject<UPEPItemDefinition>(GetTransientPackage());
	UPEPItemDefinition* ContentDefinition = NewObject<UPEPItemDefinition>(GetTransientPackage());

	FPEPContainerSlotDefinition AttachmentSlot;
	AttachmentSlot.SlotName = TEXT("Optic");
	AttachmentSlot.Mode = EPEPContainerSlotMode::Single;
	AttachmentSlot.Purpose = EPEPContainerSlotPurpose::Attachment;
	FPEPContainerSlotDefinition ContentSlot;
	ContentSlot.SlotName = TEXT("Cargo");
	ContentSlot.Mode = EPEPContainerSlotMode::Grid;
	ContentSlot.Purpose = EPEPContainerSlotPurpose::Content;
	FPEPItemFragment_Container Container;
	Container.Slots = {AttachmentSlot, ContentSlot};
	RootDefinition->DefinitionFragments.Add(FInstancedStruct::Make(Container));

	PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(RootDefinition);
	PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(AttachmentDefinition);
	PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(ContentDefinition);

	FPEPItemInstanceTree Tree;
	Tree.RootItem = PEPItemAutomationTestSupport::MakeItemForTests(RootDefinition);
	FPEPItemInstanceTreeEntry& AttachmentEntry = Tree.Children.AddDefaulted_GetRef();
	AttachmentEntry.Item = PEPItemAutomationTestSupport::MakeItemForTests(AttachmentDefinition);
	AttachmentEntry.Location = FPEPItemLocation::MakeItemContainer(
		Tree.RootItem.ItemId,
		AttachmentSlot.SlotName);
	FPEPItemInstanceTreeEntry& ContentEntry = Tree.Children.AddDefaulted_GetRef();
	ContentEntry.Item = PEPItemAutomationTestSupport::MakeItemForTests(ContentDefinition);
	ContentEntry.Location = FPEPItemLocation::MakeItemContainer(
		Tree.RootItem.ItemId,
		ContentSlot.SlotName);

	APEPItemWorldActor* Actor = World->SpawnActor<APEPContainerItemWorldActor>();
	if (TestNotNull(TEXT("world item actor spawns"), Actor))
	{
		TestTrue(TEXT("authority accepts the complete private tree"), Actor->SetItemTree(Tree));
		const TArray<FPEPWorldPresentationNode> Nodes = Actor->GetPublicPresentationNodes();
		TestEqual(TEXT("projection contains root and visible attachment only"), Nodes.Num(), 2);
		if (Nodes.Num() == 2)
		{
			TestEqual(TEXT("root node has no parent"), Nodes[0].ParentNodeId, INDEX_NONE);
			TestEqual(TEXT("attachment references root node"), Nodes[1].ParentNodeId, 0);
			TestEqual(TEXT("attachment socket defaults to slot name"),
				Nodes[1].AttachSocket, AttachmentSlot.SlotName);
		}
		TestEqual(
			TEXT("authority retains attachment and private content"),
			Actor->GetAuthorityItemTree().Children.Num(),
			2);
	}

	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(
		RootDefinition->GetPrimaryAssetId());
	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(
		AttachmentDefinition->GetPrimaryAssetId());
	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(
		ContentDefinition->GetPrimaryAssetId());
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
