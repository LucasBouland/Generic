#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "PEPEquipmentUILibrary.h"

/* Test-only slot vocabulary: the filter compares tags and gives them no meaning. */
namespace PEPEquipmentUISlotChangeTestTags
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(Slot_A, "Test.Equipment.UI.SlotChange.A");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(Slot_B, "Test.Equipment.UI.SlotChange.B");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPEPEquipmentUISlotChangeTest,
	"PEP.Equipment.UI.SlotChangeFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentUISlotChangeTest::RunTest(const FString&)
{
	const FGameplayTag Slot = PEPEquipmentUISlotChangeTestTags::Slot_A;
	const FGameplayTag OtherSlot = PEPEquipmentUISlotChangeTestTags::Slot_B;
	const EPEPItemStoreChangeType Types[] = { EPEPItemStoreChangeType::Added,
		EPEPItemStoreChangeType::Removed, EPEPItemStoreChangeType::Moved,
		EPEPItemStoreChangeType::StateChanged };
	for (const EPEPItemStoreChangeType Type : Types)
	{
		for (int32 Mask = 0; Mask < 4; ++Mask)
		{
			for (const bool bNested : { false, true })
			{
				FPEPItemStoreChange Change;
				Change.ChangeType = Type;
				Change.PreviousRootSlotTag = (Mask & 1) ? Slot : OtherSlot;
				Change.CurrentRootSlotTag = (Mask & 2) ? Slot : OtherSlot;
				if (!bNested)
				{
					Change.PreviousLocation = FPEPItemLocation::MakeNamedSlot(Change.PreviousRootSlotTag);
					Change.CurrentLocation = FPEPItemLocation::MakeNamedSlot(Change.CurrentRootSlotTag);
				}
				else
				{
					Change.PreviousLocation.Type = EPEPItemLocationType::ItemContainer;
					Change.CurrentLocation.Type = EPEPItemLocationType::ItemContainer;
					// A stale tag in an ItemContainer location must never replace the root image.
					Change.PreviousLocation.LocationTag = Slot;
					Change.CurrentLocation.LocationTag = Slot;
				}
				const bool bPrevious = (Mask & 1)
					&& (Type == EPEPItemStoreChangeType::Removed || Type == EPEPItemStoreChangeType::Moved);
				const bool bCurrent = (Mask & 2) && Type != EPEPItemStoreChangeType::Removed;
				EPEPEquipmentSlotItemUpdate Update = EPEPEquipmentSlotItemUpdate::Refresh;
				const bool bAffected = UPEPEquipmentUILibrary::FilterEquipmentSlotChange(Change, Slot, Update);
				TestEqual(TEXT("routing follows the meaningful previous/current roots"),
					bAffected, bPrevious || bCurrent);
				const EPEPEquipmentSlotItemUpdate Expected = bNested
					? EPEPEquipmentSlotItemUpdate::None
					: bCurrent ? EPEPEquipmentSlotItemUpdate::Refresh
					: bPrevious ? EPEPEquipmentSlotItemUpdate::Clear
					: EPEPEquipmentSlotItemUpdate::None;
				TestTrue(TEXT("only direct named-slot changes update the root image"), Update == Expected);
			}
		}
	}

	FPEPItemStoreChange Change;
	EPEPEquipmentSlotItemUpdate Update = EPEPEquipmentSlotItemUpdate::Refresh;
	TestFalse(TEXT("invalid slot does not match empty tags"),
		UPEPEquipmentUILibrary::FilterEquipmentSlotChange(Change, FGameplayTag(), Update));
	TestTrue(TEXT("ignored change resets output"), Update == EPEPEquipmentSlotItemUpdate::None);
	Change.ChangeType = EPEPItemStoreChangeType::Added;
	Change.CurrentLocation = FPEPItemLocation::MakeNamedSlot(Slot);
	TestTrue(TEXT("direct location works without root metadata"),
		UPEPEquipmentUILibrary::FilterEquipmentSlotChange(Change, Slot, Update));
	TestTrue(TEXT("direct add refreshes"), Update == EPEPEquipmentSlotItemUpdate::Refresh);
	return true;
}

#endif
