#include "Equipment/PEPEquipmentTags.h"

namespace PEPEquipmentTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Hand_Left, "Equipment.Slot.Hand.Left", "Active left-hand equipment slot.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Hand_Right, "Equipment.Slot.Hand.Right", "Active right-hand equipment slot.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Head, "Equipment.Slot.Head", "Equipment worn on the head, such as a helmet.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Face, "Equipment.Slot.Face", "Equipment worn over the face.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Eyes, "Equipment.Slot.Eyes", "Eye-worn equipment.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Ears, "Equipment.Slot.Ears", "Ear or hearing equipment.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Neck, "Equipment.Slot.Neck", "Equipment worn around the neck.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Chest, "Equipment.Slot.Chest", "Equipment worn on the chest.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Back, "Equipment.Slot.Back", "Back-mounted equipment such as a backpack.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Belt, "Equipment.Slot.Belt", "Equipment attached to the belt.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Primary, "Equipment.Slot.Primary", "Stowed primary equipment slot.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Secondary, "Equipment.Slot.Secondary", "Stowed secondary equipment slot.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Accessory_Left, "Equipment.Slot.Accessory.Left", "Left-side accessory attachment slot.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Accessory_Right, "Equipment.Slot.Accessory.Right", "Right-side accessory attachment slot.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Body_Head, "Equipment.Slot.Body.Head", "Modular body head part.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Body_Torso, "Equipment.Slot.Body.Torso", "Modular body torso part.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Body_Arms, "Equipment.Slot.Body.Arms", "Modular body arms part.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Body_Legs, "Equipment.Slot.Body.Legs", "Modular body legs part.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Body_Arm_Left, "Equipment.Slot.Body.Arm.Left", "Modular left arm part.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Body_Arm_Right, "Equipment.Slot.Body.Arm.Right", "Modular right arm part.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Body_Hand_Left, "Equipment.Slot.Body.Hand.Left", "Modular left hand part.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Body_Hand_Right, "Equipment.Slot.Body.Hand.Right", "Modular right hand part.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Body_Leg_Left, "Equipment.Slot.Body.Leg.Left", "Modular left leg part.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Body_Leg_Right, "Equipment.Slot.Body.Leg.Right", "Modular right leg part.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Body_Foot_Left, "Equipment.Slot.Body.Foot.Left", "Modular left foot part.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Slot_Body_Foot_Right, "Equipment.Slot.Body.Foot.Right", "Modular right foot part.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_PresentationTarget_Body_Main, "Equipment.PresentationTarget.Body.Main", "Primary pawn skeletal mesh used as the default attachment target.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_PresentationTarget_Body_Head, "Equipment.PresentationTarget.Body.Head", "Modular head skeletal mesh target.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_PresentationTarget_Body_Torso, "Equipment.PresentationTarget.Body.Torso", "Modular torso skeletal mesh target.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_PresentationTarget_Body_Arms, "Equipment.PresentationTarget.Body.Arms", "Modular arms skeletal mesh target.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_PresentationTarget_Body_Hands, "Equipment.PresentationTarget.Body.Hands", "Modular hands skeletal mesh target.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_PresentationTarget_Body_Legs, "Equipment.PresentationTarget.Body.Legs", "Modular legs skeletal mesh target.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Action_Equip, "Equipment.Action.Equip", "Request that an item move into an equipment slot.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Action_Fire, "Equipment.Action.Fire", "Request an equipped-item fire presentation.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Action_Light_SetActive, "Equipment.Action.Light.SetActive", "Set an equipped light's active state.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Action_Laser_SetActive, "Equipment.Action.Laser.SetActive", "Set an equipped laser's active state.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Action_Laser_SetTarget, "Equipment.Action.Laser.SetTarget", "Update an equipped laser target.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Event_Changed, "Equipment.Event.Changed", "The public equipment projection changed.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Event_Fire, "Equipment.Event.Fire", "A cosmetic fire presentation was accepted.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_State_Hand_Left_Occupied, "Equipment.State.Hand.Left.Occupied", "The active left hand is occupied.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_State_Hand_Right_Occupied, "Equipment.State.Hand.Right.Occupied", "The active right hand is occupied.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_State_Light_Active, "Equipment.State.Light.Active", "An equipped light is active.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_State_Laser_Active, "Equipment.State.Laser.Active", "An equipped laser is active.");
}
