#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Inventory/PEPInventoryTags.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryNativeTagsTest,
	"PEP.Inventory.Tags.NativeVocabularyIsRegistered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryNativeTagsTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("inventory move action is registered"),
		PEPInventoryTags::Inventory_Action_Move.GetTag().IsValid());
	TestTrue(TEXT("inventory item events share their hierarchy"),
		PEPInventoryTags::Inventory_Event_Item_Added.GetTag().MatchesTag(
			FGameplayTag::RequestGameplayTag(TEXT("Inventory.Event.Item"))));
	TestTrue(TEXT("transfer session state is registered"),
		PEPInventoryTags::Inventory_State_TransferSession_Active.GetTag().IsValid());

	return true;
}

#endif
