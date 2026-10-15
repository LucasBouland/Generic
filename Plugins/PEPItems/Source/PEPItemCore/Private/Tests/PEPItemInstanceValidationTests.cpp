#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Item/Internal/PEPItemMutableAccess.h"
#include "Item/Fragments/PEPItemFragment_Durability.h"
#include "Item/Fragments/PEPItemFragment_Resource.h"
#include "Item/Fragments/PEPItemFragment_Stackable.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemMutation.h"
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

#endif
