#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Inventory/PEPInventoryTags.h"
#include "Item/PEPItemTags.h"

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
	TestTrue(TEXT("the generic main root slot is registered"),
		PEPInventoryTags::Inventory_Slot_Main.GetTag().IsValid());
	TestTrue(TEXT("gameplay accesses share their hierarchy"),
		PEPInventoryTags::Inventory_GameplayAccess_Equip.GetTag().MatchesTag(
			FGameplayTag::RequestGameplayTag(TEXT("Inventory.GameplayAccess")))
		&& PEPInventoryTags::Inventory_GameplayAccess_Use.GetTag().MatchesTag(
			FGameplayTag::RequestGameplayTag(TEXT("Inventory.GameplayAccess"))));
	TestTrue(TEXT("item kinds hang from ItemCore's convention root"),
		PEPItemTags::Item_Type.GetTag() == FGameplayTag::RequestGameplayTag(TEXT("Item.Type")));

	return true;
}

#endif
