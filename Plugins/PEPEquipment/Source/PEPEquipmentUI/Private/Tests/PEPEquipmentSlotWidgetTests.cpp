#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/PEPEquipmentComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Equipment/PEPEquipmentPresentationFragment.h"
#include "Equipment/PEPEquipmentProfile.h"
#include "Equipment/PEPEquipmentTags.h"
#include "GameFramework/Actor.h"
#include "Item/PEPItemAccess.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemInstance.h"
#include "PEPEquipmentUILibrary.h"
#include "StructUtils/InstancedStruct.h"
#include "Tests/PEPTestEquipmentSlotWidget.h"
/* The runtime module's test store (exported for its sibling test modules). */
#include "../../../PEPEquipment/Private/Tests/PEPTestItemStoreComponent.h"

namespace PEPEquipmentSlotWidgetTest
{
	/**
	 * The authority's view of a pawn: a labelled main hand, a belt hidden from the projection and
	 * a back slot for a backpack. Widgets are plain objects here, never constructed into Slate.
	 */
	struct FSlotWidgetFixture
	{
		UWorld* World = nullptr;
		AActor* Owner = nullptr;
		UPEPTestItemStoreComponent* Store = nullptr;
		UPEPEquipmentComponent* Equipment = nullptr;
		TArray<TObjectPtr<UPEPItemDefinition>> Definitions;

		FSlotWidgetFixture()
		{
			UPEPEquipmentProfile* Profile = NewObject<UPEPEquipmentProfile>(GetTransientPackage());
			FPEPEquipmentSlotDefinition& Hand = Profile->Slots.AddDefaulted_GetRef();
			Hand.SlotTag = PEPEquipmentTags::Equipment_Slot_Hand_Right;
			Hand.Hand = EPEPEquipmentHand::Right;
			Hand.DisplayName = FText::FromString(TEXT("Main hand"));
			FPEPEquipmentSlotDefinition& Belt = Profile->Slots.AddDefaulted_GetRef();
			Belt.SlotTag = PEPEquipmentTags::Equipment_Slot_Belt;
			Belt.bShowPublicly = false;
			Profile->Slots.AddDefaulted_GetRef().SlotTag = PEPEquipmentTags::Equipment_Slot_Back;

			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());

			Owner = World->SpawnActor<AActor>();
			Store = NewObject<UPEPTestItemStoreComponent>(Owner);
			Store->RegisterComponent();
			Equipment = NewObject<UPEPEquipmentComponent>(Owner);
			Equipment->SetLocalPresentationEnabled(false);
			Equipment->SetEquipmentProfile(Profile);
			Equipment->RegisterComponent();
			World->BeginPlay();
			Owner->DispatchBeginPlay();
		}

		~FSlotWidgetFixture()
		{
			for (const TObjectPtr<UPEPItemDefinition>& Definition : Definitions)
			{
				if (Definition)
				{
					PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(
						Definition->GetPrimaryAssetId());
				}
			}
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		UPEPItemDefinition* MakeDefinition(const FName Name, const TCHAR* DisplayName)
		{
			UPEPItemDefinition* Definition = NewObject<UPEPItemDefinition>(GetTransientPackage(), Name);
			Definition->DisplayName = FText::FromString(DisplayName);
			Definition->DefinitionFragments.Add(
				FInstancedStruct::Make(FPEPEquipmentPresentationFragment()));
			PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(Definition);
			Definitions.Add(Definition);
			return Definition;
		}

		bool Add(const FPEPItemInstance& Item, const FPEPItemLocation& Location)
		{
			const bool bAdded =
				Store->AddItem(Item, Location, FPEPItemAccessContext::System()).bSuccess;
			Equipment->FlushDeferredUpdates();
			return bAdded;
		}
	};
}

using PEPEquipmentSlotWidgetTest::FSlotWidgetFixture;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentSlotWidgetBindingTest,
	"PEP.Equipment.UI.SlotWidget.ShowsTheOwnersSlotAndRoutesDrops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentSlotWidgetBindingTest::RunTest(const FString& Parameters)
{
	FSlotWidgetFixture Fixture;
	const FGameplayTag Hand = PEPEquipmentTags::Equipment_Slot_Hand_Right;
	const FGameplayTag Belt = PEPEquipmentTags::Equipment_Slot_Belt;
	const FGameplayTag Back = PEPEquipmentTags::Equipment_Slot_Back;

	FPEPItemInstance Rifle = PEPItemAutomationTestSupport::MakeItemForTests(
		Fixture.MakeDefinition(TEXT("EquipmentUISlotRifle"), TEXT("Rifle")));
	FPEPItemInstance Pistol = PEPItemAutomationTestSupport::MakeItemForTests(
		Fixture.MakeDefinition(TEXT("EquipmentUISlotPistol"), TEXT("Pistol")));
	FPEPItemInstance Backpack = PEPItemAutomationTestSupport::MakeItemForTests(
		Fixture.MakeDefinition(TEXT("EquipmentUISlotBackpack"), TEXT("Backpack")));
	FPEPItemInstance Stranger = PEPItemAutomationTestSupport::MakeItemForTests(
		Fixture.MakeDefinition(TEXT("EquipmentUISlotStranger"), TEXT("Stranger")));
	const FPEPItemId RifleId = Rifle.ItemId;
	const FPEPItemId PistolId = Pistol.ItemId;
	const FPEPItemId BackpackId = Backpack.ItemId;
	TestTrue(TEXT("the rifle is in hand"), Fixture.Add(Rifle, FPEPItemLocation::MakeNamedSlot(Hand)));
	TestTrue(TEXT("the pistol is on the hidden belt"),
		Fixture.Add(Pistol, FPEPItemLocation::MakeNamedSlot(Belt)));
	TestTrue(TEXT("the backpack is on the back"),
		Fixture.Add(Backpack, FPEPItemLocation::MakeNamedSlot(Back)));

	UPEPTestEquipmentSlotWidget* Widget = NewObject<UPEPTestEquipmentSlotWidget>(GetTransientPackage());
	Widget->SetSlotTag(Belt);
	FPEPEquippedItemInfo Shown;
	TestFalse(TEXT("unbound, the slot is empty"), Widget->GetEquippedItemInfo(Shown));

	Widget->BindToEquipment(Fixture.Equipment);
	TestTrue(TEXT("binding reads the slot at once, with no Slate"),
		Widget->GetEquippedItemInfo(Shown) && Shown.ItemId == PistolId);
	TestTrue(TEXT("its owner sees the hidden belt's pistol"),
		Widget->GetItemViewData().bValid
		&& Widget->GetItemViewData().ItemId == PistolId
		&& Widget->GetItemViewData().DisplayData.DisplayName.ToString() == TEXT("Pistol"));
	const FPEPEquipmentSlotDisplayData SlotData = Widget->GetSlotDisplayData();
	TestTrue(TEXT("the slot data comes from the profile"),
		SlotData.bDeclared && !SlotData.bShowPublicly
		&& SlotData.Label.ToString() == TEXT("Belt"));

	/* Drag over: the verdict is the component's preview, mirrored as a drop target state. */
	TestTrue(TEXT("the rifle may be dropped on the belt (a swap)"),
		Widget->CanAcceptItem(RifleId).bSuccess);
	TestTrue(TEXT("a valid candidate shows Valid"),
		Widget->UpdateDropPreview(RifleId).bSuccess
		&& Widget->GetDropVisualState() == EPEPUIDropTargetVisualState::Valid);
	TestFalse(TEXT("an item outside the store is refused"),
		Widget->UpdateDropPreview(Stranger.ItemId).bSuccess);
	TestEqual(TEXT("and shows Invalid"),
		Widget->GetDropVisualState(), EPEPUIDropTargetVisualState::Invalid);
	Widget->UpdateDropPreview(FPEPItemId());
	TestEqual(TEXT("no candidate yet shows Hovered"),
		Widget->GetDropVisualState(), EPEPUIDropTargetVisualState::Hovered);
	Widget->ClearDropPreview();
	TestEqual(TEXT("leaving clears it"),
		Widget->GetDropVisualState(), EPEPUIDropTargetVisualState::Neutral);

	/* Drop: an authority request executes at once, and the widget follows the change. */
	Widget->FollowEquipmentEvents();
	const int32 NotificationsBeforeDrop = Widget->Notifications;
	int32 RequestId = 0;
	TestEqual(TEXT("dropping the rifle equips it on the belt"),
		static_cast<int32>(Widget->AcceptItem(RifleId, RequestId)),
		static_cast<int32>(EPEPRequestResult::Executed));
	TestTrue(TEXT("the widget now shows the rifle"),
		Widget->GetEquippedItemInfo(Shown) && Shown.ItemId == RifleId
		&& Widget->GetItemViewData().DisplayData.DisplayName.ToString() == TEXT("Rifle"));
	TestTrue(TEXT("and told its Blueprint"), Widget->Notifications > NotificationsBeforeDrop);

	/* Drag out into the backpack. */
	const FPEPItemLocation IntoBackpack =
		FPEPItemLocation::MakeItemContainer(BackpackId, FName(TEXT("main")));
	TestTrue(TEXT("the occupant may be moved into the backpack"),
		Widget->CanUnequipTo(IntoBackpack).bSuccess);
	TestEqual(TEXT("dragging it out unequips it"),
		static_cast<int32>(Widget->RequestUnequipTo(IntoBackpack, RequestId)),
		static_cast<int32>(EPEPRequestResult::Executed));
	TestFalse(TEXT("the slot is empty again"), Widget->GetEquippedItemInfo(Shown));
	TestFalse(TEXT("with no view data"), Widget->GetItemViewData().bValid);
	TestEqual(TEXT("an empty slot has nothing to drag out"),
		static_cast<int32>(Widget->RequestUnequipTo(IntoBackpack, RequestId)),
		static_cast<int32>(EPEPRequestResult::Failed));

	/* A refresh that changes nothing is silent. */
	const int32 NotificationsBeforeRefresh = Widget->Notifications;
	Widget->RefreshSlot();
	TestEqual(TEXT("an unchanged refresh does not notify"),
		Widget->Notifications, NotificationsBeforeRefresh);

	Widget->SetSlotTag(Hand);
	TestTrue(TEXT("pointing the widget at another slot reads it at once"),
		Widget->GetEquippedItemInfo(Shown) && Shown.ItemId == PistolId);
	TestEqual(TEXT("whose label is the profile's"),
		Widget->GetSlotDisplayData().Label.ToString(), FString(TEXT("Main hand")));

	Widget->Unbind();
	TestNull(TEXT("unbinding forgets the component"), Widget->GetBoundEquipment());
	TestFalse(TEXT("and empties the slot"), Widget->GetEquippedItemInfo(Shown));
	TestFalse(TEXT("an unbound widget accepts nothing"), Widget->CanAcceptItem(RifleId).bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentUILibraryTest,
	"PEP.Equipment.UI.Library.SlotDisplayAndItemViewData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentUILibraryTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("a default label is the tag's last part"),
		UPEPEquipmentUILibrary::MakeDefaultSlotLabel(
			PEPEquipmentTags::Equipment_Slot_Hand_Right).ToString(),
		FString(TEXT("Right")));
	TestTrue(TEXT("an invalid tag has no label"),
		UPEPEquipmentUILibrary::MakeDefaultSlotLabel(FGameplayTag()).IsEmpty());

	FPEPItemResult Allowed;
	Allowed.bSuccess = true;
	const FPEPItemResult Refused = FPEPItemResult::Failure();
	TestEqual(TEXT("no candidate: hovered"),
		UPEPEquipmentUILibrary::GetDropTargetVisualState(false, Allowed),
		EPEPUIDropTargetVisualState::Hovered);
	TestEqual(TEXT("an accepted candidate: valid"),
		UPEPEquipmentUILibrary::GetDropTargetVisualState(true, Allowed),
		EPEPUIDropTargetVisualState::Valid);
	TestEqual(TEXT("a refused candidate: invalid"),
		UPEPEquipmentUILibrary::GetDropTargetVisualState(true, Refused),
		EPEPUIDropTargetVisualState::Invalid);

	FSlotWidgetFixture Fixture;
	const FPEPEquipmentSlotDisplayData Hand = UPEPEquipmentUILibrary::GetEquipmentSlotDisplayData(
		Fixture.Equipment,
		PEPEquipmentTags::Equipment_Slot_Hand_Right);
	TestTrue(TEXT("a declared slot reads its label and hand from the profile"),
		Hand.bDeclared && Hand.bShowPublicly && Hand.Hand == EPEPEquipmentHand::Right
		&& Hand.Label.ToString() == TEXT("Main hand"));
	const FPEPEquipmentSlotDisplayData Head = UPEPEquipmentUILibrary::GetEquipmentSlotDisplayData(
		Fixture.Equipment,
		PEPEquipmentTags::Equipment_Slot_Head);
	TestTrue(TEXT("an undeclared slot only gets a derived label"),
		!Head.bDeclared && Head.Label.ToString() == TEXT("Head"));

	UPEPItemDefinition* Definition =
		Fixture.MakeDefinition(TEXT("EquipmentUILibraryHelmet"), TEXT("Helmet"));
	FPEPItemInstance Helmet = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	TestTrue(TEXT("the helmet is in hand"), Fixture.Add(
		Helmet,
		FPEPItemLocation::MakeNamedSlot(PEPEquipmentTags::Equipment_Slot_Hand_Right)));
	FPEPEquippedItemInfo Node;
	TestTrue(TEXT("the slot lookup finds it"), Fixture.Equipment->FindEquippedRootInSlot(
		PEPEquipmentTags::Equipment_Slot_Hand_Right,
		Node));

	FPEPItemInstance Snapshot;
	TestTrue(TEXT("a held item is presented from the store itself"),
		UPEPEquipmentUILibrary::MakeEquippedItemSnapshot(Fixture.Equipment, Node, Snapshot)
		&& Snapshot.ItemId == Helmet.ItemId
		&& Snapshot.DefinitionId == Helmet.DefinitionId);

	/* Another player's item: only what a public node carries. */
	FPEPEquippedItemInfo PublicOnly;
	PublicOnly.ItemId = PEPItemAutomationTestSupport::MakeItemIdForTests();
	PublicOnly.DefinitionId = Definition->GetPrimaryAssetId();
	PublicOnly.SlotTag = PEPEquipmentTags::Equipment_Slot_Head;
	TestTrue(TEXT("an item this machine does not hold is a bare snapshot"),
		UPEPEquipmentUILibrary::MakeEquippedItemSnapshot(Fixture.Equipment, PublicOnly, Snapshot)
		&& Snapshot.ItemId == PublicOnly.ItemId
		&& Snapshot.DefinitionId == PublicOnly.DefinitionId);
	FPEPItemViewData ViewData;
	TestTrue(TEXT("which still builds view data from its definition"),
		UPEPEquipmentUILibrary::BuildEquippedItemViewData(Fixture.Equipment, PublicOnly, ViewData)
		&& ViewData.bValid && ViewData.DisplayData.DisplayName.ToString() == TEXT("Helmet"));

	PublicOnly.DefinitionId = FPrimaryAssetId(
		FPrimaryAssetType(FName(TEXT("PEPItem"))),
		FName(TEXT("EquipmentUILibraryNotLoaded")));
	TestFalse(TEXT("a definition that is not resident builds nothing, and loads nothing"),
		UPEPEquipmentUILibrary::BuildEquippedItemViewData(Fixture.Equipment, PublicOnly, ViewData));
	TestFalse(TEXT("an invalid node has no snapshot"),
		UPEPEquipmentUILibrary::MakeEquippedItemSnapshot(
			Fixture.Equipment,
			FPEPEquippedItemInfo(),
			Snapshot));
	return true;
}

#endif
