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
		}),
		/*bAllowAutoPlacement*/ true);
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
		ViewProperty->ContainerPtrToValuePtr<FPEPInventoryView>(Inventory)->GetOwnerComponent() == Inventory);
	// Model a Blueprint property/template copy before registration; OnRegister must repair it too.
	*ViewProperty->ContainerPtrToValuePtr<FPEPInventoryView>(Inventory) =
		*ViewProperty->ContainerPtrToValuePtr<FPEPInventoryView>(Template);
	Inventory->RegisterComponent();
	UI->RegisterInventoryComponent(Inventory);
	FPEPInventoryView* View = ViewProperty->ContainerPtrToValuePtr<FPEPInventoryView>(Inventory);
	TestTrue(TEXT("Local callbacks target the registered instance, not its template/CDO"),
		View->GetOwnerComponent() == Inventory);
	TestFalse(TEXT("Local view is not a session"), View->IsSessionView());
	// The same template copy, on the session component that now owns the mirror.
	UPEPInventorySessionComponent* SessionTemplate = NewObject<UPEPInventorySessionComponent>(Controller);
	UPEPInventorySessionComponent* SessionComponent = NewObject<UPEPInventorySessionComponent>(Controller,
		UPEPInventorySessionComponent::StaticClass(), NAME_None, RF_NoFlags, SessionTemplate);
	*SessionProperty->ContainerPtrToValuePtr<FPEPInventoryView>(SessionComponent) =
		*SessionProperty->ContainerPtrToValuePtr<FPEPInventoryView>(SessionTemplate);
	SessionComponent->RegisterComponent();
	FPEPInventoryView* Session = SessionProperty->ContainerPtrToValuePtr<FPEPInventoryView>(SessionComponent);
	TestTrue(TEXT("Session callbacks target the registered instance"), Session->GetSessionOwner() == SessionComponent);
	TestTrue(TEXT("Remote view is a session"), Session->IsSessionView());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPEPInventoryUISessionMirrorGateTest,
	"PEP.Inventory.UI.SessionMirrorGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryUISessionMirrorGateTest::RunTest(const FString& Parameters)
{
	using namespace PEPInventoryTest;
	FFixture Fixture;
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	APlayerController* Controller = Fixture.World->SpawnActor<APlayerController>();
	Controller->Player = LocalPlayer;
	UPEPInventoryComponent* Inventory = NewObject<UPEPInventoryComponent>(Controller);
	Inventory->RegisterComponent();
	Inventory->RegisterLocationValidator(FFixture::RootSlotTag(),
		FPEPItemLocationValidator::CreateLambda([](const FPEPItemInstance& Item, const FPEPItemLocation&)
		{
			return FPEPItemResult::Success(Item.ItemId);
		}),
		/*bAllowAutoPlacement*/ true);

	// The controller is announced before it carries a session component, as when one is added late.
	UPEPInventoryUISubsystem* UI = NewObject<UPEPInventoryUISubsystem>(LocalPlayer);
	UI->PlayerControllerChanged(Controller);
	TestTrue(TEXT("The controller's own inventory is registered"), UI->GetInventoryComponent() == Inventory);
	UPEPInventorySessionComponent* Session = NewObject<UPEPInventorySessionComponent>(Controller);
	Session->RegisterComponent();
	TestFalse(TEXT("A component added late is not bound yet"), Session->OnSessionViewChanged.IsBound());
	TestTrue(TEXT("The late component is found"), UI->GetSessionComponent() == Session);
	TestTrue(TEXT("Finding it binds its session events"), Session->OnSessionViewChanged.IsBound());
	TestTrue(TEXT("and its request results"), Session->OnRequestResolved.IsBound());

	// A rig with a pocket an outsider sees into and one they do not.
	const FName OpenSlot(TEXT("Open"));
	const FName SealedSlot(TEXT("Sealed"));
	const FName InnerSlot(TEXT("Inner"));
	FPEPContainerSlotDefinition OpenPocket;
	OpenPocket.SlotName = OpenSlot;
	OpenPocket.Mode = EPEPContainerSlotMode::Grid;
	OpenPocket.GridSize = FIntPoint(2, 2);
	OpenPocket.OtherVisibility = EPEPContainerSlotVisibility::Lootable;
	OpenPocket.bOtherCanTake = true;
	OpenPocket.bOtherCanPut = true;
	OpenPocket.bOtherCanMoveInside = true;
	FPEPContainerSlotDefinition SealedPocket = OpenPocket;
	SealedPocket.SlotName = SealedSlot;
	SealedPocket.GridSize = FIntPoint(2, 1);
	SealedPocket.OtherVisibility = EPEPContainerSlotVisibility::Hidden;
	FPEPItemFragment_Container RigContainer;
	RigContainer.Slots.Add(OpenPocket);
	RigContainer.Slots.Add(SealedPocket);
	FPEPContainerSlotDefinition InnerPocket = OpenPocket;
	InnerPocket.SlotName = InnerSlot;
	InnerPocket.GridSize = FIntPoint(1, 1);
	FPEPItemFragment_Container BagContainer;
	BagContainer.Slots.Add(InnerPocket);
	FPEPItemFragment_InventoryShape OneCell;
	OneCell.Size = FIntPoint(1, 1);

	const FPEPItemInstance Rig = FFixture::MakeItem(Fixture.MakeDefinition(TEXT("GateRig"),
		{ FInstancedStruct::Make(RigContainer), FInstancedStruct::Make(OneCell) }));
	UPEPItemDefinition* BagDefinition = Fixture.MakeDefinition(TEXT("GateBag"),
		{ FInstancedStruct::Make(BagContainer), FInstancedStruct::Make(OneCell) });
	UPEPItemDefinition* TrinketDefinition = Fixture.MakeShapedDefinition(TEXT("GateTrinket"), FIntPoint(1, 1));
	const FPEPItemInstance Visible = FFixture::MakeItem(TrinketDefinition);
	const FPEPItemInstance VisibleBag = FFixture::MakeItem(BagDefinition);
	const FPEPItemInstance Hidden = FFixture::MakeItem(TrinketDefinition);
	const FPEPItemInstance HiddenBag = FFixture::MakeItem(BagDefinition);
	const FPEPItemAccessContext System = FPEPItemAccessContext::System();
	TestTrue(TEXT("Seed rig"), Fixture.AddToRootSlot(Rig).bSuccess);
	TestTrue(TEXT("Seed visible item"), Fixture.Inventory->AddItem(Visible,
		FPEPItemLocation::MakeItemContainer(Rig.ItemId, OpenSlot), System).bSuccess);
	TestTrue(TEXT("Seed visible bag"), Fixture.Inventory->AddItem(VisibleBag,
		FPEPItemLocation::MakeItemContainer(Rig.ItemId, OpenSlot, FIntPoint(0, 1)), System).bSuccess);
	TestTrue(TEXT("Seed hidden item"), Fixture.Inventory->AddItem(Hidden,
		FPEPItemLocation::MakeItemContainer(Rig.ItemId, SealedSlot), System).bSuccess);
	TestTrue(TEXT("Seed hidden bag"), Fixture.Inventory->AddItem(HiddenBag,
		FPEPItemLocation::MakeItemContainer(Rig.ItemId, SealedSlot, FIntPoint(1, 0)), System).bSuccess);

	const FPEPItemInstance LocalBag = FFixture::MakeItem(
		Fixture.MakeContainerDefinition(TEXT("GateLocalBag"), TEXT("Storage"), FIntPoint(4, 4)));
	const FPEPItemInstance Pebble = FFixture::MakeItem(TrinketDefinition);
	TestTrue(TEXT("Seed local bag"), Inventory->AddItem(LocalBag,
		FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag()), System).bSuccess);
	TestTrue(TEXT("Seed local pebble"), Inventory->AddItem(Pebble,
		FPEPItemLocation::MakeItemContainer(LocalBag.ItemId, TEXT("Storage")), System).bSuccess);

	// Slot rules: this player is an outsider to the remote store.
	Fixture.Inventory->ExternalAccess = EPEPExternalAccessMode::SlotRules;
	if (!TestTrue(TEXT("Open remote session"), Session->OpenTransferSession(Fixture.Inventory)))
	{
		return false;
	}
	const FGuid SessionId = Session->GetTransferSessionId();
	if (!TestNotNull(TEXT("Mirror holds the rig"), Session->FindSessionEntryPtr(Rig.ItemId))
		|| !TestNotNull(TEXT("Mirror holds the visible item"), Session->FindSessionEntryPtr(Visible.ItemId))
		|| !TestNotNull(TEXT("Mirror holds the visible bag"), Session->FindSessionEntryPtr(VisibleBag.ItemId)))
	{
		return false;
	}
	TestNull(TEXT("Mirror withholds the hidden item"), Session->FindSessionEntryPtr(Hidden.ItemId));
	TestNull(TEXT("Mirror withholds the hidden bag"), Session->FindSessionEntryPtr(HiddenBag.ItemId));
	TestEqual(TEXT("Mirror size"), Session->GetSessionEntries().Num(), 3);

	const FPEPInventoryUIContext Remote = UI->GetTransferSessionContext();
	TArray<FPEPInventoryEntry> SlotEntries;
	UI->GetEntriesInContainerSlot(Remote, Rig.ItemId, OpenSlot, SlotEntries);
	TestEqual(TEXT("Open pocket shows its two occupants"), SlotEntries.Num(), 2);
	UI->GetEntriesInContainerSlot(Remote, Rig.ItemId, SealedSlot, SlotEntries);
	TestEqual(TEXT("Sealed pocket shows nothing"), SlotEntries.Num(), 0);
	FPEPInventoryEntry SlotEntry;
	TestTrue(TEXT("Session named slot answers from the mirror"),
		UI->FindEntryInNamedSlot(Remote, FFixture::RootSlotTag(), SlotEntry) && SlotEntry.Item.ItemId == Rig.ItemId);
	TestTrue(TEXT("Local named slot answers from the store"),
		UI->FindEntryInNamedSlot(UI->GetPlayerInventoryContext(), FFixture::RootSlotTag(), SlotEntry)
			&& SlotEntry.Item.ItemId == LocalBag.ItemId);

	// A moved container reconciles the whole mirror; untouched entries keep their replication ids.
	const int32 RigReplicationId = Session->FindSessionEntryPtr(Rig.ItemId)->ReplicationID;
	const int32 VisibleReplicationId = Session->FindSessionEntryPtr(Visible.ItemId)->ReplicationID;
	TestTrue(TEXT("Move the visible bag"), Fixture.Inventory->MoveItem(VisibleBag.ItemId,
		FPEPItemLocation::MakeItemContainer(Rig.ItemId, OpenSlot, FIntPoint(1, 1)), System).bSuccess);
	const FPEPInventoryEntry* MovedBag = Session->FindSessionEntryPtr(VisibleBag.ItemId);
	TestTrue(TEXT("Mirror follows the moved bag"), MovedBag && MovedBag->Location.GridPosition == FIntPoint(1, 1));
	const FPEPInventoryEntry* RigAfterMove = Session->FindSessionEntryPtr(Rig.ItemId);
	const FPEPInventoryEntry* VisibleAfterMove = Session->FindSessionEntryPtr(Visible.ItemId);
	TestTrue(TEXT("Reconciliation keeps the rig's replication id"),
		RigAfterMove && RigAfterMove->ReplicationID == RigReplicationId);
	TestTrue(TEXT("Reconciliation keeps the item's replication id"),
		VisibleAfterMove && VisibleAfterMove->ReplicationID == VisibleReplicationId);
	TestEqual(TEXT("Reconciliation neither adds nor drops entries"), Session->GetSessionEntries().Num(), 3);

	// Ids the player was never sent are not actionable, whatever the store would have allowed.
	int32 RequestId = 0;
	const FPEPItemLocation LocalCell = FPEPItemLocation::MakeItemContainer(LocalBag.ItemId, TEXT("Storage"), FIntPoint(1, 1));
	TestEqual(TEXT("Taking an unmirrored item is refused"),
		Session->RequestTakeFromSession(SessionId, Hidden.ItemId, LocalCell, RequestId), EPEPRequestResult::Failed);
	TestTrue(TEXT("The hidden item stays in the remote store"), Fixture.Inventory->ContainsItem(Hidden.ItemId));
	TestFalse(TEXT("and never reaches the player"), Inventory->ContainsItem(Hidden.ItemId));
	TestEqual(TEXT("Moving an unmirrored item is refused"),
		Session->RequestMoveInSession(SessionId, Hidden.ItemId,
			FPEPItemLocation::MakeItemContainer(Rig.ItemId, OpenSlot, FIntPoint(1, 0)), RequestId),
		EPEPRequestResult::Failed);
	FPEPItemLocation HiddenLocation;
	Fixture.Inventory->FindItemLocation(Hidden.ItemId, HiddenLocation);
	TestTrue(TEXT("The hidden item stays where it was"), HiddenLocation.ContainerSlotName == SealedSlot);
	TestEqual(TEXT("Putting into an unmirrored container is refused"),
		Session->RequestPutIntoSession(SessionId, Pebble.ItemId,
			FPEPItemLocation::MakeItemContainer(HiddenBag.ItemId, InnerSlot), RequestId),
		EPEPRequestResult::Failed);
	TestTrue(TEXT("The pebble stays with the player"), Inventory->ContainsItem(Pebble.ItemId));
	TestFalse(TEXT("Previewing a put into an unmirrored container fails too"),
		Session->PreviewPutIntoSession(Pebble.ItemId,
			FPEPItemLocation::MakeItemContainer(HiddenBag.ItemId, InnerSlot)).bSuccess);

	// What the mirror shows is still actionable, and the index follows the removal.
	TestEqual(TEXT("Taking a mirrored item executes"),
		Session->RequestTakeFromSession(SessionId, Visible.ItemId, LocalCell, RequestId), EPEPRequestResult::Executed);
	TestTrue(TEXT("The taken item reaches the player"), Inventory->ContainsItem(Visible.ItemId));
	TestNull(TEXT("and leaves the mirror"), Session->FindSessionEntryPtr(Visible.ItemId));
	TestEqual(TEXT("Mirror shrinks by one"), Session->GetSessionEntries().Num(), 2);
	const FPEPInventoryEntry* RigEntry = Session->FindSessionEntryPtr(Rig.ItemId);
	const FPEPInventoryEntry* BagEntry = Session->FindSessionEntryPtr(VisibleBag.ItemId);
	TestTrue(TEXT("Index still resolves the rig"), RigEntry && RigEntry->Item.ItemId == Rig.ItemId);
	TestTrue(TEXT("Index still resolves the bag"), BagEntry && BagEntry->Item.ItemId == VisibleBag.ItemId);

	Session->CloseTransferSession();
	TestEqual(TEXT("Closing empties the mirror"), Session->GetSessionEntries().Num(), 0);
	TestNull(TEXT("and its index"), Session->FindSessionEntryPtr(Rig.ItemId));
	UI->PlayerControllerChanged(nullptr);
	TestFalse(TEXT("Controller change unbinds the late component"), Session->OnSessionViewChanged.IsBound());
	return true;
}
#endif
