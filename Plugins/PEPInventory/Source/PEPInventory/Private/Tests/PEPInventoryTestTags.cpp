#if WITH_DEV_AUTOMATION_TESTS

#include "PEPInventoryTestFixture.h"

namespace PEPInventoryTest
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Test_Inventory_Slot_Secondary, "Test.Inventory.Slot.Secondary", "Automation tests only: a second root named slot.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Test_Inventory_Item_Magazine, "Test.Inventory.Item.Magazine", "Automation tests only: a magazine-like item kind.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Test_Inventory_Item_Key, "Test.Inventory.Item.Key", "Automation tests only: an item kind no test item owns.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Test_Inventory_GameplayAccess_Reload, "Test.Inventory.GameplayAccess.Reload", "Automation tests only: a project-defined gameplay access.");
}

#endif
