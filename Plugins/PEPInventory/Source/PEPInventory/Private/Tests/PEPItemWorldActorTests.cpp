#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Item/Actor/PEPSimpleItemWorldActor.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Item/Fragments/PEPItemFragment_WorldPresentation.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemPreset.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemWorldActorReplicationSourceTest,
	"PEP.Items.WorldActor.ReplicatesOnlyPublicPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemWorldActorReplicationSourceTest::RunTest(const FString& Parameters)
{
	const FProperty* DefinitionProperty =
		APEPItemWorldActor::StaticClass()->FindPropertyByName(TEXT("ItemDefinition"));
	const FProperty* PresetProperty =
		APEPItemWorldActor::StaticClass()->FindPropertyByName(TEXT("SpawnPreset"));
	const FProperty* LegacyInstanceProperty =
		APEPItemWorldActor::StaticClass()->FindPropertyByName(TEXT("ItemInstance"));
	const FProperty* LegacyContentsProperty =
		APEPItemWorldActor::StaticClass()->FindPropertyByName(TEXT("ContainedItems"));
	const FProperty* AuthorityTreeProperty =
		APEPItemWorldActor::StaticClass()->FindPropertyByName(TEXT("AuthorityItemTree"));
	const FProperty* PublicPresentationProperty =
		APEPItemWorldActor::StaticClass()->FindPropertyByName(TEXT("PublicPresentation"));

	TestNull(TEXT("world actor has no direct definition property"), DefinitionProperty);
	TestNotNull(TEXT("authoring preset property exists"), PresetProperty);
	TestNull(TEXT("legacy replicated item instance is removed"), LegacyInstanceProperty);
	TestNull(TEXT("legacy private contents array is removed"), LegacyContentsProperty);
	TestNull(TEXT("base actor has no persistent authority tree"), AuthorityTreeProperty);
	TestNotNull(TEXT("public presentation exists"), PublicPresentationProperty);
	TestFalse(
		TEXT("authoring preset is not replicated"),
		PresetProperty && PresetProperty->HasAnyPropertyFlags(CPF_Net));
	TestTrue(
		TEXT("public presentation is replicated"),
		PublicPresentationProperty && PublicPresentationProperty->HasAnyPropertyFlags(CPF_Net));
	TestTrue(
		TEXT("public presentation owns the replication notification"),
		PublicPresentationProperty
			&& PublicPresentationProperty->HasAnyPropertyFlags(CPF_RepNotify));

	const FProperty* DefinitionIdProperty =
		FPEPWorldPresentationNode::StaticStruct()->FindPropertyByName(TEXT("DefinitionId"));
	const FProperty* StaticMeshProperty =
		FPEPWorldPresentationNode::StaticStruct()->FindPropertyByName(TEXT("StaticMesh"));
	TestNull(TEXT("public nodes do not reveal item definitions"), DefinitionIdProperty);
	TestNotNull(TEXT("public nodes carry their cosmetic mesh"), StaticMeshProperty);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemWorldActorDefinitionPriorityTest,
	"PEP.Items.WorldActor.PresetCreatesRuntimeInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemWorldActorDefinitionPriorityTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	UPEPItemDefinition* RuntimeDefinition = NewObject<UPEPItemDefinition>(
		GetTransientPackage());
	FPEPItemFragment_WorldPresentation WorldPresentation;
	WorldPresentation.DefaultCosmetics.VariantName = TEXT("Desert");
	FPEPWorldScalarParameter DirtParameter;
	DirtParameter.ParameterName = TEXT("DirtAmount");
	DirtParameter.Value = 0.65f;
	WorldPresentation.DefaultCosmetics.ScalarParameters.Add(DirtParameter);
	RuntimeDefinition->DefinitionFragments.Add(FInstancedStruct::Make(WorldPresentation));
	PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(RuntimeDefinition);
	UPEPItemPreset* Preset = NewObject<UPEPItemPreset>(GetTransientPackage());
	Preset->RootDefinition = RuntimeDefinition;
	FPEPItemState_WorldPresentation PresetCosmetics;
	PresetCosmetics.Cosmetics = WorldPresentation.DefaultCosmetics;
	PresetCosmetics.Cosmetics.VariantName = TEXT("Urban");
	Preset->StateOverrides.Add(FInstancedStruct::Make(PresetCosmetics));

	APEPItemWorldActor* Actor = World->SpawnActor<APEPSimpleItemWorldActor>();
	if (TestNotNull(TEXT("world item actor spawns"), Actor))
	{
		Actor->SetSpawnPreset(Preset);
		TestTrue(
			TEXT("preset creates a valid authority runtime instance"),
			Actor->GetAuthorityItemInstance().IsValid());
		TestTrue(
			TEXT("runtime definition is resolved from the created instance"),
			Actor->GetItemDefinition() == RuntimeDefinition);
		const TArray<FPEPWorldPresentationNode> Nodes = Actor->GetPublicPresentationNodes();
		TestEqual(TEXT("runtime item projects one visual root"), Nodes.Num(), 1);
		if (Nodes.Num() == 1)
		{
			TestEqual(TEXT("preset cosmetic override is projected"),
				Nodes[0].Cosmetics.VariantName, FName(TEXT("Urban")));
			TestEqual(TEXT("material parameters are projected"),
				Nodes[0].Cosmetics.ScalarParameters.Num(), 1);
		}
		Actor->SetItemInstance(FPEPItemInstance());
		TestNull(
			TEXT("preset is not a runtime definition fallback"),
			Actor->GetItemDefinition());
	}

	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(
		RuntimeDefinition->GetPrimaryAssetId());
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	return true;
}


#endif
