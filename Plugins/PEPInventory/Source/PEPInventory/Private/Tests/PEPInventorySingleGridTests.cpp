#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Item/PEPItemLocation.h"
#include "Item/PEPItemInstance.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Item/PEPItemIdLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPEPInventorySingleGridSlotTest,
    "PEP.Inventory.Container.SingleGrid",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FPEPInventorySingleGridSlotTest::RunTest(
    const FString& Parameters)
{
    (void)Parameters;

    FPEPContainerSlotDefinition SingleSlot;
    SingleSlot.SlotName = FName(TEXT("optic_socket"));
    SingleSlot.Mode = EPEPContainerSlotMode::Single;
    SingleSlot.GridSize = FIntPoint::ZeroValue;

    TestTrue(
        TEXT("Single slot ignores GridSize"),
        SingleSlot.IsValid());

    FPEPContainerSlotDefinition GridSlot;
    GridSlot.SlotName = FName(TEXT("Storage"));
    GridSlot.Mode = EPEPContainerSlotMode::Grid;
    GridSlot.GridSize = FIntPoint::ZeroValue;

    TestFalse(
        TEXT("Grid slot requires a positive GridSize"),
        GridSlot.IsValid());

    const FPEPItemLocation Location =
        FPEPItemLocation::MakeItemContainer(
		PEPItemAutomationTestSupport::MakeItemIdForTests(),
            FName(TEXT("optic_socket")));

    TestTrue(
        TEXT("FName item-container location is structurally valid"),
        Location.IsStructurallyValid());
    TestEqual(
        TEXT("Container slot name is preserved"),
        Location.ContainerSlotName,
        FName(TEXT("optic_socket")));
    TestFalse(
        TEXT("Container location does not reuse LocationTag"),
        Location.LocationTag.IsValid());

    return true;
}

#endif
