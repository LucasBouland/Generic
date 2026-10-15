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

	/**
	 * Generic root named slot for a store's main storage: the item whose containers hold what is
	 * carried, for a project that has no equipment domain to name its root slots. Like any named
	 * slot it means nothing until a domain claims it with RegisterLocationValidator.
	 */
	PEPINVENTORY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Inventory_Slot_Main);

	/**
	 * Gameplay access a container grants to what it holds (FPEPItemFragment_Container's
	 * GrantedGameplayAccess), asked through HasGrantedGameplayAccess: equipping an item, using it.
	 * Projects add their own children of Inventory.GameplayAccess for anything else.
	 */
	PEPINVENTORY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Inventory_GameplayAccess_Equip);
	PEPINVENTORY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Inventory_GameplayAccess_Use);

	/** Semantic state tag for systems mirroring HasTransferSession into their own tag container. */
	PEPINVENTORY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Inventory_State_TransferSession_Active);
}
