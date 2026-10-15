#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Item/Internal/PEPItemMutableAccess.h"
#include "Item/Fragments/PEPItemFragment_Durability.h"
#include "Item/Fragments/PEPItemFragment_Resource.h"
#include "Item/Fragments/PEPItemFragment_Stackable.h"
#include "Item/Fragments/PEPItemFragment_WorldPresentation.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemInstance.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

namespace PEPItemSaveGameArchiveTests
{
	/** Same archive setup as a USaveGame-style save: names as strings, only SaveGame properties. */
	FPEPItemInstance RoundTrip(FPEPItemInstance& Item)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, true);
		FObjectAndNameAsStringProxyArchive WriterProxy(Writer, true);
		WriterProxy.ArIsSaveGame = true;
		FPEPItemInstance::StaticStruct()->SerializeItem(WriterProxy, &Item, nullptr);

		FPEPItemInstance Loaded;
		FMemoryReader Reader(Bytes, true);
		FObjectAndNameAsStringProxyArchive ReaderProxy(Reader, true);
		ReaderProxy.ArIsSaveGame = true;
		FPEPItemInstance::StaticStruct()->SerializeItem(ReaderProxy, &Loaded, nullptr);
		return Loaded;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemSaveGameArchiveStateTest,
	"PEP.Items.Save.SaveGameArchiveKeepsIdentityAndState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemSaveGameArchiveStateTest::RunTest(const FString& Parameters)
{
	UPEPItemDefinition* StackDefinition = NewObject<UPEPItemDefinition>(GetTransientPackage());
	FPEPItemFragment_Stackable Stackable;
	Stackable.bUseProjectDefaultMaxStackSize = false;
	Stackable.MaxStackSize = 10;
	StackDefinition->DefinitionFragments.Add(FInstancedStruct::Make(Stackable));
	PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(StackDefinition);

	UPEPItemDefinition* ToolDefinition = NewObject<UPEPItemDefinition>(GetTransientPackage());
	FPEPItemFragment_Resource Resource;
	Resource.MaxResource = 5;
	ToolDefinition->DefinitionFragments.Add(FInstancedStruct::Make(Resource));
	ToolDefinition->DefinitionFragments.Add(FInstancedStruct::Make(FPEPItemFragment_Durability()));
	PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(ToolDefinition);

	FPEPItemInstance Stack = PEPItemAutomationTestSupport::MakeItemForTests(StackDefinition);
	if (FPEPItemState_Stack* State = FPEPItemMutableAccess::GetMutableState<FPEPItemState_Stack>(Stack))
	{
		State->Count = 7;
	}

	FPEPItemInstance Tool = PEPItemAutomationTestSupport::MakeItemForTests(ToolDefinition);
	if (FPEPItemState_Resource* State = FPEPItemMutableAccess::GetMutableState<FPEPItemState_Resource>(Tool))
	{
		State->Current = 2;
	}
	if (FPEPItemState_Durability* State = FPEPItemMutableAccess::GetMutableState<FPEPItemState_Durability>(Tool))
	{
		State->Durability = 0.4f;
	}
	FPEPItemState_WorldPresentation& Presentation =
		FPEPItemMutableAccess::FindOrAddState<FPEPItemState_WorldPresentation>(Tool);
	Presentation.MeshOverride = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/PEPTests/SM_Skin.SM_Skin")));
	Presentation.Cosmetics.VariantName = TEXT("Worn");
	FPEPWorldScalarParameter& Scalar = Presentation.Cosmetics.ScalarParameters.AddDefaulted_GetRef();
	Scalar.ParameterName = TEXT("Dirt");
	Scalar.Value = 0.75f;
	Scalar.MaterialIndex = 1;

	const FPEPItemInstance LoadedStack = PEPItemSaveGameArchiveTests::RoundTrip(Stack);
	TestTrue(TEXT("item id survives a SaveGame archive"), LoadedStack.ItemId == Stack.ItemId);
	TestTrue(TEXT("definition id survives a SaveGame archive"),
		LoadedStack.DefinitionId.IsValid() && LoadedStack.DefinitionId == Stack.DefinitionId);
	TestEqual(TEXT("schema version survives a SaveGame archive"),
		LoadedStack.SchemaVersion, FPEPItemInstance::CurrentSchemaVersion);
	const FPEPItemState_Stack* LoadedCount = LoadedStack.GetState<FPEPItemState_Stack>();
	TestTrue(TEXT("stack count survives a SaveGame archive"), LoadedCount && LoadedCount->Count == 7);
	FPEPItemInstanceValidationResult Validation;
	TestTrue(TEXT("a loaded item validates"), LoadedStack.ValidateInstance(Validation));

	const FPEPItemInstance LoadedTool = PEPItemSaveGameArchiveTests::RoundTrip(Tool);
	const FPEPItemState_Resource* LoadedResource = LoadedTool.GetState<FPEPItemState_Resource>();
	TestTrue(TEXT("resource survives a SaveGame archive"), LoadedResource && LoadedResource->Current == 2);
	const FPEPItemState_Durability* LoadedDurability = LoadedTool.GetState<FPEPItemState_Durability>();
	TestTrue(TEXT("durability survives a SaveGame archive"),
		LoadedDurability && FMath::IsNearlyEqual(LoadedDurability->Durability, 0.4f));
	const FPEPItemState_WorldPresentation* LoadedPresentation =
		LoadedTool.GetState<FPEPItemState_WorldPresentation>();
	TestTrue(TEXT("world presentation state survives a SaveGame archive"), LoadedPresentation != nullptr);
	if (LoadedPresentation)
	{
		TestTrue(TEXT("mesh override survives"),
			LoadedPresentation->MeshOverride.ToSoftObjectPath() == Presentation.MeshOverride.ToSoftObjectPath());
		TestTrue(TEXT("cosmetics survive"), LoadedPresentation->Cosmetics == Presentation.Cosmetics);
	}

	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(StackDefinition->GetPrimaryAssetId());
	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(ToolDefinition->GetPrimaryAssetId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemSaveGameArchiveFutureVersionTest,
	"PEP.Items.Save.NewerSchemaIsNotRelabelled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemSaveGameArchiveFutureVersionTest::RunTest(const FString& Parameters)
{
	UPEPItemDefinition* Definition = NewObject<UPEPItemDefinition>(GetTransientPackage());
	PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(Definition);

	FPEPItemInstance Item = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	Item.SchemaVersion = FPEPItemInstance::CurrentSchemaVersion + 1;

	const FPEPItemInstance Loaded = PEPItemSaveGameArchiveTests::RoundTrip(Item);
	TestEqual(TEXT("a newer schema is kept as written"),
		Loaded.SchemaVersion, FPEPItemInstance::CurrentSchemaVersion + 1);
	FPEPItemInstanceValidationResult Validation;
	TestFalse(TEXT("a record from a newer build is rejected rather than downgraded"),
		Loaded.ValidateInstance(Validation));

	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(Definition->GetPrimaryAssetId());
	return true;
}

#endif
