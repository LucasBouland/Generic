#include "Inventory/PEPInventoryTags.h"

namespace PEPInventoryTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Inventory_Action_Move, "Inventory.Action.Move", "Move an item within an item store.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Inventory_Action_Pickup, "Inventory.Action.Pickup", "Pick an item up into an inventory.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Inventory_Action_Transfer, "Inventory.Action.Transfer", "Transfer an item between inventories.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Inventory_Action_Destroy, "Inventory.Action.Destroy", "Destroy an item owned by an inventory.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Inventory_Event_Item_Added, "Inventory.Event.Item.Added", "An item was added to the store.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Inventory_Event_Item_Moved, "Inventory.Event.Item.Moved", "An item changed location in the store.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Inventory_Event_Item_StateChanged, "Inventory.Event.Item.StateChanged", "An item's private state changed in place.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Inventory_Event_Item_Removed, "Inventory.Event.Item.Removed", "An item was removed from the store.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Inventory_Event_TransferSession_Opened, "Inventory.Event.TransferSession.Opened", "A remote inventory transfer session opened.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Inventory_Event_TransferSession_Closed, "Inventory.Event.TransferSession.Closed", "A remote inventory transfer session closed.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Inventory_Location_WorldItem, "Inventory.Location.WorldItem", "Root slot of the item a world container actor carries.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Inventory_Slot_Main, "Inventory.Slot.Main", "Generic root slot for a store's main storage.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Inventory_GameplayAccess_Equip, "Inventory.GameplayAccess.Equip", "A container lets what it holds be equipped.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Inventory_GameplayAccess_Use, "Inventory.GameplayAccess.Use", "A container lets what it holds be used.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Inventory_State_TransferSession_Active, "Inventory.State.TransferSession.Active", "A transfer session is currently active.");
}
