#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Item/Internal/PEPItemMutableAccess.h"
#include "Item/Fragments/PEPItemFragment_Durability.h"
#include "Item/Fragments/PEPItemFragment_Resource.h"
#include "Item/Fragments/PEPItemFragment_Stackable.h"
#include "Item/Fragments/PEPItemFragment_WorldPresentation.h"
#include "Engine/GameInstance.h"
#include "Item/PEPIdAllocatorSubsystem.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemInstance.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

namespace PEPItemSaveGameArchiveTests
{
	/**
	 * Same archive setup as a USaveGame-style save: names as strings, only SaveGame properties.
	 * With Defaults, tagged serialization skips every property equal to it, the way a struct
	 * nested in a UObject is delta-saved against its archetype.
	 */
	TArray<uint8> Write(FPEPItemInstance& Item, const FPEPItemInstance* Defaults = nullptr)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, true);
		FObjectAndNameAsStringProxyArchive WriterProxy(Writer, true);
		WriterProxy.ArIsSaveGame = true;
		FPEPItemInstance::StaticStruct()->SerializeItem(WriterProxy, &Item, Defaults);
		return Bytes;
	}

	/** Loads over OutLoaded, so a property absent from the archive keeps OutLoaded's value. */
	void ReadInto(const TArray<uint8>& Bytes, FPEPItemInstance& OutLoaded)
	{
		FMemoryReader Reader(Bytes, true);
		FObjectAndNameAsStringProxyArchive ReaderProxy(Reader, true);
		ReaderProxy.ArIsSaveGame = true;
		FPEPItemInstance::StaticStruct()->SerializeItem(ReaderProxy, &OutLoaded, nullptr);
	}

	FPEPItemInstance RoundTrip(FPEPItemInstance& Item)
	{
		FPEPItemInstance Loaded;
		ReadInto(Write(Item), Loaded);
		return Loaded;
	}

	UPEPItemDefinition* MakeStackDefinition()
	{
		UPEPItemDefinition* Definition = NewObject<UPEPItemDefinition>(GetTransientPackage());
		FPEPItemFragment_Stackable Stackable;
		Stackable.bUseProjectDefaultMaxStackSize = false;
		Stackable.MaxStackSize = 10;
		Definition->DefinitionFragments.Add(FInstancedStruct::Make(Stackable));
		PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(Definition);
		return Definition;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemSaveGameArchiveUnversionedTest,
	"PEP.Items.Save.UnversionedRecordLoadsAsCurrent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemSaveGameArchiveUnversionedTest::RunTest(const FString& Parameters)
{
	UPEPItemDefinition* Definition = PEPItemSaveGameArchiveTests::MakeStackDefinition();
	FPEPItemInstance Item = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	if (FPEPItemState_Stack* State = FPEPItemMutableAccess::GetMutableState<FPEPItemState_Stack>(Item))
	{
		State->Count = 3;
	}
	TestEqual(TEXT("a created record is stamped current"),
		Item.SchemaVersion, FPEPItemInstance::CurrentSchemaVersion);

	// A record written as unversioned (0) is legacy version 1 data and is upgraded on load.
	Item.SchemaVersion = FPEPItemInstance::UnversionedSchemaVersion;
	const FPEPItemInstance LoadedFromZero = PEPItemSaveGameArchiveTests::RoundTrip(Item);
	TestEqual(TEXT("a record saved with version 0 loads as current"),
		LoadedFromZero.SchemaVersion, FPEPItemInstance::CurrentSchemaVersion);
	FPEPItemInstanceValidationResult Validation;
	TestTrue(TEXT("the migrated record validates"), LoadedFromZero.ValidateInstance(Validation));
	const FPEPItemState_Stack* MigratedCount = LoadedFromZero.GetState<FPEPItemState_Stack>();
	TestTrue(TEXT("migration keeps the state"), MigratedCount && MigratedCount->Count == 3);

	// Delta-saved against a default record, the version field matches the default and is skipped:
	// this is what saves written before the field was persisted look like.
	const FPEPItemInstance DefaultRecord;
	const TArray<uint8> WithoutVersion = PEPItemSaveGameArchiveTests::Write(Item, &DefaultRecord);

	FPEPItemInstance Probe;
	Probe.SchemaVersion = FPEPItemInstance::CurrentSchemaVersion + 1;
	PEPItemSaveGameArchiveTests::ReadInto(WithoutVersion, Probe);
	TestEqual(TEXT("the archive really omits SchemaVersion (the probe keeps its own value)"),
		Probe.SchemaVersion, FPEPItemInstance::CurrentSchemaVersion + 1);

	FPEPItemInstance LoadedWithoutVersion;
	PEPItemSaveGameArchiveTests::ReadInto(WithoutVersion, LoadedWithoutVersion);
	TestEqual(TEXT("a record without SchemaVersion loads as current"),
		LoadedWithoutVersion.SchemaVersion, FPEPItemInstance::CurrentSchemaVersion);
	TestTrue(TEXT("its identity survives"),
		LoadedWithoutVersion.ItemId == Item.ItemId && LoadedWithoutVersion.DefinitionId == Item.DefinitionId);
	TestTrue(TEXT("and it validates"), LoadedWithoutVersion.ValidateInstance(Validation));

	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(Definition->GetPrimaryAssetId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemSaveGameArchiveObservedIdTest,
	"PEP.Items.Save.LoadedIdsAreNeverReallocated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemSaveGameArchiveObservedIdTest::RunTest(const FString& Parameters)
{
	UPEPItemDefinition* Definition = PEPItemSaveGameArchiveTests::MakeStackDefinition();
	FPEPItemInstance Item = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	TestTrue(TEXT("the allocator produced an id"), Item.ItemId.IsValid() && Item.ItemId.Value >= 0);

	// A save from a later session holds ids this process has not reached yet.
	Item.ItemId.Value += 100;
	const FPEPItemInstance Loaded = PEPItemSaveGameArchiveTests::RoundTrip(Item);
	TestTrue(TEXT("the loaded id survives"), Loaded.ItemId == Item.ItemId);

	const FPEPItemInstance Next = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	TestTrue(TEXT("the next allocation lands past every loaded id"), Next.ItemId.Value > Item.ItemId.Value);

	// Restoring an older allocator snapshot never pulls the counter back below loaded ids. The
	// subsystem needs a GameInstance outer; this one has no world, so it counts as the authority.
	UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	UPEPIdAllocatorSubsystem* Allocator = NewObject<UPEPIdAllocatorSubsystem>(GameInstance);
	FPEPItemIdAllocatorSaveState OldState;
	OldState.NextId = 0;
	Allocator->RestoreFromSaveState(OldState);
	TestTrue(TEXT("restore is monotonic"), Allocator->GetSaveState().NextId > Next.ItemId.Value);

	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(Definition->GetPrimaryAssetId());
	return true;
}

#endif
