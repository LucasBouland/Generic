#if WITH_DEV_AUTOMATION_TESTS

#include "PEPInventoryUISubsystem.h"
#include "PEPInventoryUIAutomationListener.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Core/PEPCoreTags.h"
#include "Item/PEPItemIdLibrary.h"
#include "../../../PEPInventory/Private/Tests/PEPInventoryTestFixture.h"
#include "NativeGameplayTags.h"

/* Test-only loot panel vocabulary: the plugin defines none of its own. */
namespace PEPInventoryUITestTags
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(LootPanel_A, "Inventory.LootPanel.Test.A");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(LootPanel_B, "Inventory.LootPanel.Test.B");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPEPInventoryUIDragLifecycleTest,
	"PEP.Inventory.UI.DragLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryUIDragLifecycleTest::RunTest(const FString& Parameters)
{
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	UPEPInventoryUISubsystem* UI = NewObject<UPEPInventoryUISubsystem>(LocalPlayer);
	UPEPInventoryUITestDragOperation* First = NewObject<UPEPInventoryUITestDragOperation>();
	UPEPInventoryUITestDragOperation* Second = NewObject<UPEPInventoryUITestDragOperation>();
	UDragDropOperation* Unsupported = NewObject<UDragDropOperation>();

	TestFalse(TEXT("No drag means no handled rotation"), UI->RotateActiveDrag());
	TestFalse(TEXT("Null operation rejected"), UI->BeginDrag(nullptr));
	TestFalse(TEXT("Operation must implement the command interface"), UI->BeginDrag(Unsupported));
	TestTrue(TEXT("Begin first drag"), UI->BeginDrag(First));
	TestTrue(TEXT("Begin is idempotent"), UI->BeginDrag(First));
	TestFalse(TEXT("Another window cannot replace an active drag"), UI->BeginDrag(Second));
	UI->EndDrag(Second);
	UI->EndDrag(nullptr);
	TestTrue(TEXT("Unrelated cleanup preserves active drag"), UI->GetActiveDragOperation() == First);
	TestTrue(TEXT("Rotation routed to operation"), UI->RotateActiveDrag());
	TestEqual(TEXT("One command produces one rotation"), First->RotationCount, 1);
	First->bHandleRotation = false;
	TestFalse(TEXT("Operation can decline rotation"), UI->RotateActiveDrag());

	First->Drop(FPointerEvent());
	TestNull(TEXT("UMG Drop clears the operation automatically"), UI->GetActiveDragOperation());
	TestFalse(TEXT("Drop detaches cancel delegate"), First->OnDragCancelled.IsBound());

	TestTrue(TEXT("Second window can begin after drop"), UI->BeginDrag(Second));
	UI->EndDrag(First);
	TestTrue(TEXT("Late cleanup from old operation preserves new drag"),
		UI->GetActiveDragOperation() == Second);
	Second->DragCancelled(FPointerEvent());
	TestNull(TEXT("UMG cancel clears the operation automatically"), UI->GetActiveDragOperation());
	TestFalse(TEXT("Cancel detaches drop delegate"), Second->OnDrop.IsBound());

	UI->BeginDrag(First);
	UI->UnregisterInventoryComponent();
	TestNull(TEXT("Inventory unregistration clears interaction"), UI->GetActiveDragOperation());
	TestFalse(TEXT("Unregistration detaches operation callbacks"), First->OnDrop.IsBound());
	UI->BeginDrag(Second);
	UI->Deinitialize();
	TestNull(TEXT("Deinitialization clears interaction"), UI->GetActiveDragOperation());
	TestFalse(TEXT("Deinitialization detaches callbacks"), Second->OnDragCancelled.IsBound());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPEPInventoryUIRoutingTest,
	"PEP.Inventory.UI.RoutingAndContexts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryUIRoutingTest::RunTest(const FString& Parameters)
{
	using namespace PEPInventoryTest;
	FFixture Fixture;
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	APlayerController* Controller = Fixture.World->SpawnActor<APlayerController>();
	Controller->Player = LocalPlayer;
	UPEPInventoryComponent* Inventory = NewObject<UPEPInventoryComponent>(Controller);
	Inventory->RegisterComponent();
	// On the controller, as in a game; it finds the controller's own inventory as its local store.
	UPEPInventorySessionComponent* Session = NewObject<UPEPInventorySessionComponent>(Controller);
	Session->RegisterComponent();
	Inventory->RegisterLocationValidator(FFixture::RootSlotTag(),
		FPEPItemLocationValidator::CreateLambda([](const FPEPItemInstance& Item, const FPEPItemLocation&)
		{
			return FPEPItemResult::Success(Item.ItemId);
		}));
	UPEPInventoryUISubsystem* UI = NewObject<UPEPInventoryUISubsystem>(LocalPlayer);
	TestFalse(TEXT("Unregistered context is invalid"), UI->IsContextCurrent(UI->GetPlayerInventoryContext()));
	TestFalse(TEXT("Null registration rejected"), UI->RegisterInventoryComponent(nullptr));
	TestFalse(TEXT("Remote inventory cannot be registered as the local inventory"),
		UI->RegisterInventoryComponent(Fixture.Inventory));
	if (!TestTrue(TEXT("Local inventory registration"), UI->RegisterInventoryComponent(Inventory)))
	{
		return false;
	}
	const FPEPInventoryUIContext Local = UI->GetPlayerInventoryContext();
	TestTrue(TEXT("Local context current"), UI->IsContextCurrent(Local));
	UPEPInventoryUIAutomationListener* Listener = NewObject<UPEPInventoryUIAutomationListener>();
	UI->OnInventoryChanged.AddDynamic(Listener, &UPEPInventoryUIAutomationListener::RecordInventoryChange);
	UI->PlayerControllerChanged(Controller);
	TestTrue(TEXT("Initial controller notification preserves the registered inventory"),
		UI->GetInventoryComponent() == Inventory);
	TestTrue(TEXT("Initial controller notification preserves context identity"), UI->IsContextCurrent(Local));
	TestTrue(TEXT("Initial controller notification preserves the component delegate"),
		Inventory->OnInventoryChanged.IsBound());
	UI->RegisterInventoryComponent(Inventory);
	TestTrue(TEXT("Registering the same component preserves contexts"), UI->IsContextCurrent(Local));

	const FName Slot(TEXT("Storage"));
	const FPEPItemInstance Bag = FFixture::MakeItem(
		Fixture.MakeContainerDefinition(TEXT("UIRoutingBag"), Slot, FIntPoint(4, 4)));
	const FPEPItemInstance Item = FFixture::MakeItem(
		Fixture.MakeShapedDefinition(TEXT("UIRoutingItem"), FIntPoint(2, 1)));
	TestTrue(TEXT("Seed bag"), Inventory->AddItem(Bag,
		FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag()), FPEPItemAccessContext::System()).bSuccess);
	TestEqual(TEXT("Inventory change reaches UI listener after controller notification"),
		Listener->InventoryChangeCount, 1);
	TestTrue(TEXT("Subsystem relays the changed item identity"), Listener->LastItemId == Bag.ItemId);
	TestTrue(TEXT("Seed item"), Inventory->AddItem(Item,
		FPEPItemLocation::MakeItemContainer(Bag.ItemId, Slot), FPEPItemAccessContext::System()).bSuccess);
	const FPEPItemLocation Moved = FPEPItemLocation::MakeItemContainer(Bag.ItemId, Slot, FIntPoint(1, 2));
	TestTrue(TEXT("Move preview succeeds"), UI->PreviewPlacement(Local, Local, Item.ItemId, Moved).bSuccess);
	FPEPInventoryEntry Entry;
	Inventory->FindInventoryEntry(Item.ItemId, Entry);
	TestEqual(TEXT("Preview does not mutate location"), Entry.Location.GridPosition, FIntPoint::ZeroValue);
	int32 RequestId = 0;
	TestEqual(TEXT("Local request executes"), UI->RequestPlacement(Local, Local, Item.ItemId, Moved, RequestId),
		EPEPRequestResult::Executed);
	Inventory->FindInventoryEntry(Item.ItemId, Entry);
	TestEqual(TEXT("Requested position applied"), Entry.Location.GridPosition, Moved.GridPosition);
	TestTrue(TEXT("Request gets an id"), RequestId > 0);
	const FPEPItemLocation Outside = FPEPItemLocation::MakeItemContainer(Bag.ItemId, Slot, FIntPoint(4, 4));
	TestEqual(TEXT("Invalid placement rejected"), UI->RequestPlacement(Local, Local, Item.ItemId, Outside, RequestId),
		EPEPRequestResult::Failed);
	TestEqual(TEXT("Rejected preview allocates no request id"), RequestId, 0);

	UPEPItemDefinition* RemoteDefinition = Fixture.MakeContainerDefinition(
		TEXT("UIRoutingRemoteItem"), Slot, FIntPoint(2, 2));
	const FPEPItemInstance RemoteItem = FFixture::MakeItem(RemoteDefinition);
	TestTrue(TEXT("Seed remote item"), Fixture.AddToRootSlot(RemoteItem).bSuccess);
	const FPEPItemInstance RemoteChild = FFixture::MakeItem(
		Fixture.MakeShapedDefinition(TEXT("UIRoutingRemoteChild"), FIntPoint(1, 1)));
	TestTrue(TEXT("Seed remote child"), Fixture.Inventory->AddItem(RemoteChild,
		FPEPItemLocation::MakeItemContainer(RemoteItem.ItemId, Slot), FPEPItemAccessContext::System()).bSuccess);
	// A shared store, as every world container is: rearranging it needs no per-slot bOther* flags.
	Fixture.Inventory->ExternalAccess = EPEPExternalAccessMode::SameAsOwner;
	TestFalse(TEXT("No session has no panel tag"), UI->GetTransferSessionPanelTag().IsValid());
	TestFalse(TEXT("Loot presentation is unset by default"), Fixture.Inventory->LootPanelTag.IsValid());
	Fixture.Inventory->LootPanelTag = PEPInventoryUITestTags::LootPanel_A;
	TestTrue(TEXT("Open remote session"), Session->OpenTransferSession(Fixture.Inventory));
	TestEqual(TEXT("UI reads the remote presentation tag"), UI->GetTransferSessionPanelTag(), PEPInventoryUITestTags::LootPanel_A.GetTag());
	Fixture.Inventory->LootPanelTag = PEPInventoryUITestTags::LootPanel_B;
	TestEqual(TEXT("Presentation is stable until next session"), UI->GetTransferSessionPanelTag(), PEPInventoryUITestTags::LootPanel_A.GetTag());

	const FPEPInventoryUIContext Remote = UI->GetTransferSessionContext();
	TestTrue(TEXT("Remote context current"), UI->IsContextCurrent(Remote));
	TArray<FPEPInventoryEntry> SlotEntries;
	UI->GetEntriesInContainerSlot(Remote, RemoteItem.ItemId, Slot, SlotEntries);
	TestEqual(TEXT("Remote window reads its session slot"), SlotEntries.Num(), 1);
	if (SlotEntries.Num() == 1) TestTrue(TEXT("Session slot contains remote child"), SlotEntries[0].Item.ItemId == RemoteChild.ItemId);
	UI->GetEntriesInContainerSlot(Local, RemoteItem.ItemId, Slot, SlotEntries);
	TestEqual(TEXT("Local context cannot accidentally read remote slot"), SlotEntries.Num(), 0);
	UI->GetEntriesInContainerSlot(Local, Bag.ItemId, Slot, SlotEntries);
	TestEqual(TEXT("Local window still reads local slots"), SlotEntries.Num(), 1);

	// Equipment widgets read named slots: a session has to answer them from its own view.
	FPEPInventoryEntry SlotEntry;
	TestTrue(TEXT("Session named slot holds the remote item"),
		UI->FindEntryInNamedSlot(Remote, FFixture::RootSlotTag(), SlotEntry) && SlotEntry.Item.ItemId == RemoteItem.ItemId);
	TestTrue(TEXT("Local named slot holds the local item, not the remote one"),
		UI->FindEntryInNamedSlot(Local, FFixture::RootSlotTag(), SlotEntry) && SlotEntry.Item.ItemId == Bag.ItemId);
	TestFalse(TEXT("No slot tag finds nothing"), UI->FindEntryInNamedSlot(Remote, FGameplayTag(), SlotEntry));

	const FPEPItemLocation Taken = FPEPItemLocation::MakeItemContainer(Bag.ItemId, Slot);
	TestTrue(TEXT("Take preview succeeds"), UI->PreviewPlacement(Remote, Local, RemoteItem.ItemId, Taken).bSuccess);
	const FPEPItemLocation RemoteMoved = FPEPItemLocation::MakeItemContainer(
		RemoteItem.ItemId, Slot, FIntPoint(1, 1));
	Fixture.Inventory->ExternalAccess = EPEPExternalAccessMode::SlotRules;
	TestEqual(TEXT("Remote move respects external permissions"),
		Session->RequestMoveInSession(Session->GetTransferSessionId(), RemoteChild.ItemId, RemoteMoved, RequestId),
		EPEPRequestResult::Failed);
	Fixture.Inventory->ExternalAccess = EPEPExternalAccessMode::SameAsOwner;
	TestTrue(TEXT("Remote rearrangement preview succeeds"),
		UI->PreviewPlacement(Remote, Remote, RemoteChild.ItemId, RemoteMoved).bSuccess);
	Fixture.Inventory->FindInventoryEntry(RemoteChild.ItemId, Entry);
	TestEqual(TEXT("Remote preview does not mutate store"), Entry.Location.GridPosition, FIntPoint::ZeroValue);
	TestEqual(TEXT("Remote rearrangement request executes"),
		UI->RequestPlacement(Remote, Remote, RemoteChild.ItemId, RemoteMoved, RequestId), EPEPRequestResult::Executed);
	TestTrue(TEXT("Remote rearrangement allocates request id"), RequestId > 0);
	Fixture.Inventory->FindInventoryEntry(RemoteChild.ItemId, Entry);
	TestEqual(TEXT("Remote store position updated"), Entry.Location.GridPosition, RemoteMoved.GridPosition);
	Session->FindSessionEntry(RemoteChild.ItemId, Entry);
	TestEqual(TEXT("Remote session view refreshed"), Entry.Location.GridPosition, RemoteMoved.GridPosition);
	TestFalse(TEXT("Rearrangement never transfers to player"), Inventory->ContainsItem(RemoteChild.ItemId));
	const FPEPItemLocation RemoteOutside = FPEPItemLocation::MakeItemContainer(
		RemoteItem.ItemId, Slot, FIntPoint(2, 2));
	TestEqual(TEXT("Remote out of bounds preview rejected"),
		UI->RequestPlacement(Remote, Remote, RemoteChild.ItemId, RemoteOutside, RequestId), EPEPRequestResult::Failed);
	TestEqual(TEXT("Server rejects invalid remote move without UI preview"),
		Session->RequestMoveInSession(Session->GetTransferSessionId(), RemoteChild.ItemId, RemoteOutside, RequestId),
		EPEPRequestResult::Failed);
	Fixture.Inventory->FindInventoryEntry(RemoteChild.ItemId, Entry);
	TestEqual(TEXT("Rejected remote move preserves position"), Entry.Location.GridPosition, RemoteMoved.GridPosition);
	TestEqual(TEXT("Take request routed"), UI->RequestPlacement(Remote, Local, RemoteItem.ItemId, Taken, RequestId),
		EPEPRequestResult::Executed);
	TestTrue(TEXT("Taken item in player inventory"), Inventory->FindInventoryEntry(RemoteItem.ItemId, Entry));
	TestFalse(TEXT("Taken item removed from remote"), Fixture.Inventory->FindInventoryEntry(RemoteItem.ItemId, Entry));
	TestFalse(TEXT("Emptied session named slot finds nothing"),
		UI->FindEntryInNamedSlot(Remote, FFixture::RootSlotTag(), SlotEntry));
	const FPEPItemLocation Put = FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag());
	TestTrue(TEXT("Put preview succeeds"), UI->PreviewPlacement(Local, Remote, RemoteItem.ItemId, Put).bSuccess);
	TestTrue(TEXT("Repeated put preview answers the same"),
		UI->PreviewPlacement(Local, Remote, RemoteItem.ItemId, Put).bSuccess);
	{
		// The remote slot fills mid-drag: both preview caches must drop the answer they hold.
		const FPEPItemInstance Blocker = FFixture::MakeItem(
			Fixture.MakeShapedDefinition(TEXT("UIRoutingRemoteBlocker"), FIntPoint(1, 1)));
		TestTrue(TEXT("Remote slot filled"), Fixture.AddToRootSlot(Blocker).bSuccess);
		TestFalse(TEXT("Put preview follows the remote change"),
			UI->PreviewPlacement(Local, Remote, RemoteItem.ItemId, Put).bSuccess);
		TestTrue(TEXT("Remote slot emptied"),
			Fixture.Inventory->RemoveItem(Blocker.ItemId, FPEPItemAccessContext::System()).bSuccess);
		TestTrue(TEXT("Put preview follows the slot freeing again"),
			UI->PreviewPlacement(Local, Remote, RemoteItem.ItemId, Put).bSuccess);
	}
	TestEqual(TEXT("Put request routed"), UI->RequestPlacement(Local, Remote, RemoteItem.ItemId, Put, RequestId),
		EPEPRequestResult::Executed);
	TestTrue(TEXT("Put item in remote inventory"), Fixture.Inventory->FindInventoryEntry(RemoteItem.ItemId, Entry));
	TestFalse(TEXT("Put item removed from player"), Inventory->FindInventoryEntry(RemoteItem.ItemId, Entry));

	TestTrue(TEXT("Reopen same inventory"), Session->OpenTransferSession(Fixture.Inventory));
	TestEqual(TEXT("New session samples the new presentation tag"), UI->GetTransferSessionPanelTag(), PEPInventoryUITestTags::LootPanel_B.GetTag());
	UI->GetEntriesInContainerSlot(Remote, RemoteItem.ItemId, Slot, SlotEntries);
	TestEqual(TEXT("Stale session reads return no entries"), SlotEntries.Num(), 0);
	TestFalse(TEXT("Stale session named slot reads find nothing"),
		UI->FindEntryInNamedSlot(Remote, FFixture::RootSlotTag(), SlotEntry));
	TestTrue(TEXT("The new session finds the put item in its named slot"),
		UI->FindEntryInNamedSlot(UI->GetTransferSessionContext(), FFixture::RootSlotTag(), SlotEntry)
			&& SlotEntry.Item.ItemId == RemoteItem.ItemId);
	TestFalse(TEXT("Old drag invalid after reopening"), UI->IsContextCurrent(Remote));
	TestEqual(TEXT("Stale remote request rejected"),
		UI->RequestPlacement(Remote, Local, RemoteItem.ItemId, Taken, RequestId), EPEPRequestResult::Failed);
	TestEqual(TEXT("Stale request allocates no id"), RequestId, 0);
	const FPEPInventoryUIContext Reopened = UI->GetTransferSessionContext();
	Session->CloseTransferSession();
	TestFalse(TEXT("Closed context invalid"), UI->IsContextCurrent(Reopened));
	TestFalse(TEXT("Closing clears the panel tag"), UI->GetTransferSessionPanelTag().IsValid());
	TestTrue(TEXT("Local context survives session changes"), UI->IsContextCurrent(Local));
	UI->UnregisterInventoryComponent();
	TestFalse(TEXT("Old local drag invalid after unregister"), UI->IsContextCurrent(Local));
	TestFalse(TEXT("Inventory subscription detached"), Inventory->OnInventoryChanged.IsBound());
	TestFalse(TEXT("Request subscription detached"), Inventory->OnRequestResolved.IsBound());
	UI->RegisterInventoryComponent(Inventory);
	TestFalse(TEXT("Old context invalid after re-register"), UI->IsContextCurrent(Local));
	UI->PlayerControllerChanged(nullptr);
	TestNull(TEXT("Controller change detaches inventory"), UI->GetInventoryComponent());
	TestFalse(TEXT("Controller change detaches the session subscription"), Session->OnSessionViewChanged.IsBound());
	TestNull(TEXT("No controller means no session component"), UI->GetSessionComponent());
	UI->RegisterInventoryComponent(Inventory);
	APlayerController* OtherController = Fixture.World->SpawnActor<APlayerController>();
	OtherController->Player = LocalPlayer;
	UI->PlayerControllerChanged(OtherController);
	TestNull(TEXT("Different controller detaches the old inventory"), UI->GetInventoryComponent());
	TestFalse(TEXT("Different controller removes the old component subscription"),
		Inventory->OnInventoryChanged.IsBound());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPEPInventoryUIReplicatedCallbackTest,
	"PEP.Inventory.UI.ReplicatedCallbackTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryUIReplicatedCallbackTest::RunTest(const FString& Parameters)
{
	using namespace PEPInventoryTest;
	FFixture Fixture;
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	APlayerController* Controller = Fixture.World->SpawnActor<APlayerController>();
	Controller->Player = LocalPlayer;
	UPEPInventoryUISubsystem* UI = NewObject<UPEPInventoryUISubsystem>(LocalPlayer);
	UPEPInventoryUIAutomationListener* Listener = NewObject<UPEPInventoryUIAutomationListener>();
	UI->OnInventoryChanged.AddDynamic(Listener, &UPEPInventoryUIAutomationListener::RecordInventoryChange);
	FStructProperty* ViewProperty = FindFProperty<FStructProperty>(
		UPEPInventoryComponent::StaticClass(), TEXT("InventoryView"));
	FStructProperty* SessionProperty = FindFProperty<FStructProperty>(
		UPEPInventorySessionComponent::StaticClass(), TEXT("SessionView"));
	if (!TestNotNull(TEXT("Inventory view property"), ViewProperty)
		|| !TestNotNull(TEXT("Session view property"), SessionProperty))
	{
		return false;
	}
	UPEPInventoryComponent* Template = NewObject<UPEPInventoryComponent>(Controller);
	UPEPInventoryComponent* Inventory = NewObject<UPEPInventoryComponent>(Controller,
		UPEPInventoryComponent::StaticClass(), NAME_None, RF_NoFlags, Template);
	TestTrue(TEXT("PostInitProperties repairs the callback target after template copying"),
		ViewProperty->ContainerPtrToValuePtr<FPEPInventoryView>(Inventory)->OwnerComponent == Inventory);
	// Model a Blueprint property/template copy before registration; OnRegister must repair it too.
	*ViewProperty->ContainerPtrToValuePtr<FPEPInventoryView>(Inventory) =
		*ViewProperty->ContainerPtrToValuePtr<FPEPInventoryView>(Template);
	Inventory->RegisterComponent();
	UI->RegisterInventoryComponent(Inventory);
	FPEPInventoryView* View = ViewProperty->ContainerPtrToValuePtr<FPEPInventoryView>(Inventory);
	TestTrue(TEXT("Local callbacks target the registered instance, not its template/CDO"),
		View->OwnerComponent == Inventory);
	TestFalse(TEXT("Local view is not a session"), View->bIsSessionView);
	// The same template copy, on the session component that now owns the mirror.
	UPEPInventorySessionComponent* SessionTemplate = NewObject<UPEPInventorySessionComponent>(Controller);
	UPEPInventorySessionComponent* SessionComponent = NewObject<UPEPInventorySessionComponent>(Controller,
		UPEPInventorySessionComponent::StaticClass(), NAME_None, RF_NoFlags, SessionTemplate);
	*SessionProperty->ContainerPtrToValuePtr<FPEPInventoryView>(SessionComponent) =
		*SessionProperty->ContainerPtrToValuePtr<FPEPInventoryView>(SessionTemplate);
	SessionComponent->RegisterComponent();
	FPEPInventoryView* Session = SessionProperty->ContainerPtrToValuePtr<FPEPInventoryView>(SessionComponent);
	TestTrue(TEXT("Session callbacks target the registered instance"), Session->SessionOwner == SessionComponent);
	TestTrue(TEXT("Remote view is a session"), Session->bIsSessionView);
	FPEPInventoryEntry Received;
	Received.Item = FFixture::MakeItem(Fixture.MakeShapedDefinition(TEXT("UIReceivedItem"), FIntPoint(1, 1)));
	Received.Location = FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag());
	Received.PostReplicatedAdd(*View);
	TestEqual(TEXT("Replicated add reaches the subsystem listener"), Listener->InventoryChangeCount, 1);
	TestTrue(TEXT("Replicated add relays the received item"), Listener->LastItemId == Received.Item.ItemId);
	Received.Location = FPEPItemLocation::MakeItemContainer(PEPItemAutomationTestSupport::MakeItemIdForTests(), TEXT("Storage"), FIntPoint(1, 0));
	Received.PostReplicatedChange(*View);
	TestEqual(TEXT("Replicated change reaches the subsystem listener"), Listener->InventoryChangeCount, 2);
	UI->UnregisterInventoryComponent();
	return true;
}
#endif
