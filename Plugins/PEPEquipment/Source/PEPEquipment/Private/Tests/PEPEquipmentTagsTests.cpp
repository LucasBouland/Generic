#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Equipment/PEPEquipmentTags.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentNativeTagsTest,
	"PEP.Equipment.Tags.NativeVocabularyIsRegistered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentNativeTagsTest::RunTest(const FString& Parameters)
{
	const FGameplayTag EquipmentSlot = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot"));
	TestTrue(TEXT("left and right hands remain independent slot tags"),
		PEPEquipmentTags::Equipment_Slot_Hand_Left.GetTag()
			!= PEPEquipmentTags::Equipment_Slot_Hand_Right.GetTag());
	TestTrue(TEXT("modular body tags belong to the equipment slot hierarchy"),
		PEPEquipmentTags::Equipment_Slot_Body_Torso.GetTag().MatchesTag(EquipmentSlot));
	TestTrue(TEXT("combined arms and legs match the human pawn components"),
		PEPEquipmentTags::Equipment_Slot_Body_Arms.GetTag().IsValid()
			&& PEPEquipmentTags::Equipment_Slot_Body_Legs.GetTag().IsValid());
	TestTrue(TEXT("modular presentation targets are registered"),
		PEPEquipmentTags::Equipment_PresentationTarget_Body_Head.GetTag().IsValid()
			&& PEPEquipmentTags::Equipment_PresentationTarget_Body_Torso.GetTag().IsValid()
			&& PEPEquipmentTags::Equipment_PresentationTarget_Body_Arms.GetTag().IsValid()
			&& PEPEquipmentTags::Equipment_PresentationTarget_Body_Hands.GetTag().IsValid()
			&& PEPEquipmentTags::Equipment_PresentationTarget_Body_Legs.GetTag().IsValid());
	TestTrue(TEXT("equipment visual state tags are registered"),
		PEPEquipmentTags::Equipment_State_Laser_Active.GetTag().IsValid());

	return true;
}

#endif
