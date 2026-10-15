#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Item/Internal/PEPItemMutableAccess.h"
#include "Item/Fragments/PEPItemFragment_Durability.h"
#include "Item/Fragments/PEPItemFragment_Resource.h"
#include "Item/Fragments/PEPItemFragment_Stackable.h"
#include "Item/PEPIdAllocatorSubsystem.h"
#include "Item/PEPItemAccess.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemMutation.h"
#include "Item/PEPItemQuery.h"
#include "Item/PEPItemRules.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemInstanceResourceRepairTest,
	"PEP.Items.InstanceValidation.ResourceBoundsAndRepair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemInstanceResourceRepairTest::RunTest(const FString& Parameters)
{
	UPEPItemDefinition* Definition = NewObject<UPEPItemDefinition>(GetTransientPackage());
	FPEPItemFragment_Resource Resource;
	Resource.MaxResource = 5;
	Definition->DefinitionFragments.Add(FInstancedStruct::Make(Resource));
	PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(Definition);

	FPEPItemInstance Item = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	FPEPItemState_Resource* State = FPEPItemMutableAccess::GetMutableState<FPEPItemState_Resource>(Item);
	TestNotNull(TEXT("resource state was created"), State);
	if (State)
	{
		State->Current = 6;
		FPEPItemInstanceValidationResult Validation;
		TestFalse(TEXT("resource above its definition maximum is invalid"), Item.ValidateInstance(Validation));
		TestTrue(TEXT("validation reports the out-of-range resource"), Validation.Errors.Num() > 0);
		TestTrue(TEXT("repair clamps resource state to its definition"), Item.RepairLoadedInstance());
		TestEqual(TEXT("resource is clamped to MaxResource"), State->Current, 5);
		TestTrue(TEXT("repaired resource validates"), Item.ValidateInstance(Validation));
	}

	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(Definition->GetPrimaryAssetId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemInstanceDurabilityRepairTest,
	"PEP.Items.InstanceValidation.DurabilityRejectsNaNAndRepairs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemInstanceDurabilityRepairTest::RunTest(const FString& Parameters)
{
	UPEPItemDefinition* Definition = NewObject<UPEPItemDefinition>(GetTransientPackage());
	FPEPItemFragment_Durability Durability;
	Durability.bUseProjectDefaultStartingDurability = false;
	Durability.StartingDurability = 0.75f;
	Definition->DefinitionFragments.Add(FInstancedStruct::Make(Durability));
	PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(Definition);

	FPEPItemInstance Item = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	FPEPItemState_Durability* State = FPEPItemMutableAccess::GetMutableState<FPEPItemState_Durability>(Item);
	TestNotNull(TEXT("durability state was created"), State);
	if (State)
	{
		State->Durability = std::numeric_limits<float>::quiet_NaN();
		FPEPItemInstanceValidationResult Validation;
		TestFalse(TEXT("NaN durability is invalid"), Item.ValidateInstance(Validation));
		TestTrue(TEXT("repair resets NaN to the definition starting value"), Item.RepairLoadedInstance());
		TestTrue(TEXT("repaired durability is finite and restored from the fragment"),
			FMath::IsFinite(State->Durability) && FMath::IsNearlyEqual(State->Durability, 0.75f));
		TestTrue(TEXT("repaired durability validates"), Item.ValidateInstance(Validation));
	}

	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(Definition->GetPrimaryAssetId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemInvalidStackSplitAndMergeTest,
	"PEP.Items.Stack.InvalidPerUnitStateCannotSplitOrMerge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemInvalidStackSplitAndMergeTest::RunTest(const FString& Parameters)
{
	UPEPItemDefinition* Definition = NewObject<UPEPItemDefinition>(GetTransientPackage());
	FPEPItemFragment_Stackable Stackable;
	Stackable.bUseProjectDefaultMaxStackSize = false;
	Stackable.MaxStackSize = 10;
	Definition->DefinitionFragments.Add(FInstancedStruct::Make(Stackable));
	Definition->DefinitionFragments.Add(FInstancedStruct::Make(FPEPItemFragment_Durability()));
	PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(Definition);

	FPEPItemInstance Source = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	FPEPItemInstance Target = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	if (FPEPItemState_Stack* Stack = FPEPItemMutableAccess::GetMutableState<FPEPItemState_Stack>(Source))
	{
		Stack->Count = 4;
	}
	int32 TransferCount = 0;
	TestFalse(TEXT("invalid stack state is rejected for splitting"),
		PEPItemRules::CanSplitStack(Source, 2).bSuccess);
	TestFalse(TEXT("invalid stack state is rejected for merging"),
		PEPItemRules::CanStackItems(Source, Target, 1, TransferCount).bSuccess);

	FPEPItemInstance Split;
	TestFalse(TEXT("split mutation rechecks and rejects the invalid stack"),
		PEPItemMutation::SplitStack(Source, 2, Split).bSuccess);
	TestFalse(TEXT("merge mutation rechecks and rejects the invalid stack"),
		PEPItemMutation::MergeStacks(Source, Target, 1, TransferCount).bSuccess);
	TestEqual(TEXT("rejected merge does not transfer units"), TransferCount, 0);
	TestEqual(TEXT("source count is unchanged"),
		FPEPItemMutableAccess::GetMutableState<FPEPItemState_Stack>(Source)->Count, 4);

	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(Definition->GetPrimaryAssetId());
	return true;
}

namespace PEPItemInstanceValidationTests
{
	UPEPItemDefinition* MakeStackDefinition(const int32 MaxStackSize)
	{
		UPEPItemDefinition* Definition = NewObject<UPEPItemDefinition>(GetTransientPackage());
		FPEPItemFragment_Stackable Stackable;
		Stackable.bUseProjectDefaultMaxStackSize = false;
		Stackable.MaxStackSize = MaxStackSize;
		Definition->DefinitionFragments.Add(FInstancedStruct::Make(Stackable));
		PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(Definition);
		return Definition;
	}

	int32 PeekNextAllocatedId()
	{
		// GetSaveState reads only the process-wide counter, so the class default object will do.
		return GetDefault<UPEPIdAllocatorSubsystem>()->GetSaveState().NextId;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemInstanceUnversionedValidationTest,
	"PEP.Items.InstanceValidation.UnversionedRecordNeedsMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemInstanceUnversionedValidationTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("a default-constructed record is unversioned"),
		FPEPItemInstance().SchemaVersion, FPEPItemInstance::UnversionedSchemaVersion);

	UPEPItemDefinition* Definition = PEPItemInstanceValidationTests::MakeStackDefinition(5);
	FPEPItemInstance Item = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	FPEPItemInstanceValidationResult Validation;
	TestTrue(TEXT("a created record validates"), Item.ValidateInstance(Validation));

	Item.SchemaVersion = FPEPItemInstance::UnversionedSchemaVersion;
	TestFalse(TEXT("an unversioned record is refused until migrated"), Item.ValidateInstance(Validation));
	TestFalse(TEXT("a negative version cannot be migrated"), Item.MigrateInstanceFromVersion(-1));
	TestFalse(TEXT("a newer version cannot be migrated"),
		Item.MigrateInstanceFromVersion(FPEPItemInstance::CurrentSchemaVersion + 1));
	TestEqual(TEXT("a refused migration leaves the record untouched"),
		Item.SchemaVersion, FPEPItemInstance::UnversionedSchemaVersion);
	TestTrue(TEXT("version 0 migrates as legacy version 1 data"),
		Item.MigrateInstanceFromVersion(FPEPItemInstance::UnversionedSchemaVersion));
	TestEqual(TEXT("migration stamps the current version"),
		Item.SchemaVersion, FPEPItemInstance::CurrentSchemaVersion);
	TestTrue(TEXT("the migrated record validates"), Item.ValidateInstance(Validation));

	Item.SchemaVersion = FPEPItemInstance::UnversionedSchemaVersion;
	TestTrue(TEXT("repair also migrates an unversioned record"), Item.RepairLoadedInstance());

	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(Definition->GetPrimaryAssetId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemDurabilityNonFiniteAmountTest,
	"PEP.Items.Durability.NonFiniteAmountsAreRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemDurabilityNonFiniteAmountTest::RunTest(const FString& Parameters)
{
	UPEPItemDefinition* Definition = NewObject<UPEPItemDefinition>(GetTransientPackage());
	FPEPItemFragment_Durability Durability;
	Durability.bUseProjectDefaultStartingDurability = false;
	Durability.StartingDurability = 0.5f;
	Definition->DefinitionFragments.Add(FInstancedStruct::Make(Durability));
	PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(Definition);

	FPEPItemInstance Item = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const float Infinity = std::numeric_limits<float>::infinity();

	TestFalse(TEXT("a NaN repair is refused by the rule"), PEPItemRules::CanRepairDurability(Item, NaN).bSuccess);
	TestFalse(TEXT("a NaN repair is refused by the mutation"), PEPItemMutation::RepairDurability(Item, NaN).bSuccess);
	TestFalse(TEXT("an infinite repair is refused"), PEPItemMutation::RepairDurability(Item, Infinity).bSuccess);
	TestFalse(TEXT("an infinite damage is refused"), PEPItemMutation::DamageDurability(Item, Infinity).bSuccess);
	TestFalse(TEXT("a NaN damage is refused"), PEPItemMutation::DamageDurability(Item, NaN).bSuccess);
	TestFalse(TEXT("a NaN durability cannot be set"), PEPItemMutation::SetDurability(Item, NaN).bSuccess);

	const FPEPItemState_Durability* State = Item.GetState<FPEPItemState_Durability>();
	TestTrue(TEXT("refused requests leave durability unchanged"),
		State && FMath::IsNearlyEqual(State->Durability, 0.5f));

	TestTrue(TEXT("a finite repair still works"), PEPItemMutation::RepairDurability(Item, 0.25f).bSuccess);
	TestTrue(TEXT("and lands where expected"), State && FMath::IsNearlyEqual(State->Durability, 0.75f));

	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(Definition->GetPrimaryAssetId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemPreviewSplitTest,
	"PEP.Items.Stack.PreviewSplitMatchesSplitWithoutAllocating",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemPreviewSplitTest::RunTest(const FString& Parameters)
{
	UPEPItemDefinition* Definition = PEPItemInstanceValidationTests::MakeStackDefinition(10);
	FPEPItemInstance Source = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	if (FPEPItemState_Stack* Stack = FPEPItemMutableAccess::GetMutableState<FPEPItemState_Stack>(Source))
	{
		Stack->Count = 6;
	}

	const int32 NextIdBefore = PEPItemInstanceValidationTests::PeekNextAllocatedId();
	FPEPItemInstance SourceAfter;
	FPEPItemInstance PreviewSplit;
	const FPEPItemResult Preview = PEPItemMutation::PreviewSplitStack(Source, 2, SourceAfter, PreviewSplit);
	TestTrue(TEXT("the preview succeeds"), Preview.bSuccess);
	TestTrue(TEXT("the preview result names the source, not the preview id"), Preview.ItemId == Source.ItemId);
	TestEqual(TEXT("the preview allocates no id"),
		PEPItemInstanceValidationTests::PeekNextAllocatedId(), NextIdBefore);
	TestTrue(TEXT("the preview split carries the preview id"),
		PreviewSplit.ItemId == PEPItemMutation::GetPreviewItemId());
	TestTrue(TEXT("the preview id is MAX_int32"), PEPItemMutation::GetPreviewItemId().GetValue() == MAX_int32);

	const FPEPItemState_Stack* AfterState = SourceAfter.GetState<FPEPItemState_Stack>();
	const FPEPItemState_Stack* PreviewState = PreviewSplit.GetState<FPEPItemState_Stack>();
	TestTrue(TEXT("the previewed source keeps the remainder"), AfterState && AfterState->Count == 4);
	TestTrue(TEXT("the previewed split holds the split count"), PreviewState && PreviewState->Count == 2);
	TestEqual(TEXT("the real source is untouched"), Source.GetState<FPEPItemState_Stack>()->Count, 6);
	FPEPItemInstanceValidationResult Validation;
	TestTrue(TEXT("the preview split validates like a real one"), PreviewSplit.ValidateInstance(Validation));

	FPEPItemInstance RealSplit;
	TestTrue(TEXT("the real split succeeds"), PEPItemMutation::SplitStack(Source, 2, RealSplit).bSuccess);
	TestTrue(TEXT("the real split gets a real id"),
		RealSplit.ItemId.IsValid() && RealSplit.ItemId != PEPItemMutation::GetPreviewItemId());
	TestEqual(TEXT("the real source matches the preview"),
		Source.GetState<FPEPItemState_Stack>()->Count, AfterState ? AfterState->Count : -1);
	TestEqual(TEXT("the real split matches the preview"),
		RealSplit.GetState<FPEPItemState_Stack>()->Count, PreviewState ? PreviewState->Count : -1);

	FPEPItemInstance RejectedAfter;
	FPEPItemInstance RejectedSplit;
	TestFalse(TEXT("an impossible split is refused"),
		PEPItemMutation::PreviewSplitStack(Source, 4, RejectedAfter, RejectedSplit).bSuccess);
	TestFalse(TEXT("a refused preview leaves no source output"), RejectedAfter.IsValid());
	TestFalse(TEXT("a refused preview leaves no split output"), RejectedSplit.IsValid());

	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(Definition->GetPrimaryAssetId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemQueryFailsClosedTest,
	"PEP.Items.Query.MissingStateOrDefinitionFailsClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemQueryFailsClosedTest::RunTest(const FString& Parameters)
{
	UPEPItemDefinition* Definition = PEPItemInstanceValidationTests::MakeStackDefinition(10);
	FPEPItemInstance Item = PEPItemAutomationTestSupport::MakeItemForTests(Definition);

	FPEPItemStackData StackData;
	FPEPItemWeightData WeightData;
	TestTrue(TEXT("a stack with state reports its count"),
		PEPItemQuery::GetStackData(Item, StackData) && StackData.Count == 1);
	TestTrue(TEXT("a loaded item has a weight"), PEPItemQuery::GetWeightData(Item, WeightData));

	FPEPItemInstance Stateless = Item;
	FPEPItemMutableAccess::GetMutableStateFragments(Stateless).Reset();
	TestFalse(TEXT("a stack without state does not invent a count"),
		PEPItemQuery::GetStackData(Stateless, StackData));
	TestFalse(TEXT("its stack data is flagged invalid"), StackData.bValid);
	TestFalse(TEXT("its weight cannot be computed"), PEPItemQuery::GetWeightData(Stateless, WeightData));

	FPEPItemInstance Unloaded = Item;
	Unloaded.DefinitionId = FPrimaryAssetId(UPEPItemDefinition::ItemAssetType, FName(TEXT("PEPTest_NeverLoaded")));
	TestFalse(TEXT("an unloaded definition has no weight"), PEPItemQuery::GetWeightData(Unloaded, WeightData));
	TestFalse(TEXT("its weight data is flagged invalid"), WeightData.bValid);

	// One tag filter for every domain: an invalid record never matches, even with empty filters.
	FPEPItemInstance Anonymous = Item;
	Anonymous.ItemId.Reset();
	const FGameplayTagContainer NoTags;
	TestTrue(TEXT("a valid item matches empty filters"),
		PEPItemQuery::DoesItemMatchRequiredBlockedTags(Item, NoTags, NoTags));
	TestFalse(TEXT("an invalid item fails the query filter"),
		PEPItemQuery::DoesItemMatchRequiredBlockedTags(Anonymous, NoTags, NoTags));
	TestFalse(TEXT("and the rules filter agrees"),
		PEPItemRules::DoesItemMatchRequiredBlockedTags(Anonymous, NoTags, NoTags));

	PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(Definition->GetPrimaryAssetId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemAccessContextTest,
	"PEP.Items.Access.OnlySystemIsSystem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPItemAccessContextTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("System() is the server"), FPEPItemAccessContext::System().IsSystem());
	TestFalse(TEXT("a default context is not the server"), FPEPItemAccessContext().IsSystem());
	TestFalse(TEXT("From(nullptr) is not the server"), FPEPItemAccessContext::From(nullptr).IsSystem());
	return true;
}

#endif
