#pragma once

#include "NativeGameplayTags.h"

/** Native vocabulary for inventory input routing, events, and externally mirrored state. */
namespace PEPInventoryTags
{
	PEPINVENTORY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Inventory_Action_Move);
	PEPINVENTORY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Inventory_Action_Pickup);
	PEPINVENTORY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Inventory_Action_Transfer);
	PEPINVENTORY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Inventory_Action_Destroy);

	PEPINVENTORY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Inventory_Event_Item_Added);
	PEPINVENTORY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Inventory_Event_Item_Moved);
	PEPINVENTORY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Inventory_Event_Item_StateChanged);
	PEPINVENTORY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Inventory_Event_Item_Removed);
	PEPINVENTORY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Inventory_Event_TransferSession_Opened);
	PEPINVENTORY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Inventory_Event_TransferSession_Closed);

	/** Root named slot holding the item a world container actor carries. */
	PEPINVENTORY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Inventory_Location_WorldItem);

	/** Semantic state tag for systems mirroring HasTransferSession into their own tag container. */
	PEPINVENTORY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Inventory_State_TransferSession_Active);
}
