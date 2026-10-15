#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Components/PEPInventoryComponent.h"
#include "Components/PEPInventorySessionComponent.h"
#include "Interaction/PEPPickupItemActor.h"
#include "Interaction/PEPPickupContainerActor.h"
#include "Interaction/PEPInteractionTags.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Item/PEPItemContainerMutation.h"
#include "Item/PEPItemDefinition.h"
#include "NativeGameplayTags.h"
#include "TimerManager.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_TestWorldItemDestination, "Inventory.Test.WorldItemDestination");

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPEPWorldItemStorageTest,
    "PEP.Pickup.WorldStorage.OpenTransferAndDrop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPEPWorldItemStorageTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    UPEPItemDefinition* Definition = NewObject<UPEPItemDefinition>();
    UPEPItemDefinition* AttachmentDefinition = NewObject<UPEPItemDefinition>();
    FPEPItemFragment_Container Container;
    FPEPContainerSlotDefinition Slot;
    Slot.SlotName = TEXT("Attachment");
    Slot.Mode = EPEPContainerSlotMode::Single;
    Slot.Purpose = EPEPContainerSlotPurpose::Attachment;
    Slot.bOtherCanPut = true;
    Container.Slots.Add(Slot);
    Container.bCanRelock = true;
    Definition->DefinitionFragments.Add(FInstancedStruct::Make(Container));
    PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(Definition);
    PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(AttachmentDefinition);
    ON_SCOPE_EXIT
    {
        PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(Definition->GetPrimaryAssetId());
        PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(AttachmentDefinition->GetPrimaryAssetId());
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    };
    APlayerController* Player = World->SpawnActor<APlayerController>();
    UPEPInventoryComponent* Inventory = NewObject<UPEPInventoryComponent>(Player);
    Player->AddInstanceComponent(Inventory);
    Inventory->RegisterComponent();
    UPEPInventorySessionComponent* PlayerSession = NewObject<UPEPInventorySessionComponent>(Player);
    Player->AddInstanceComponent(PlayerSession);
    PlayerSession->RegisterComponent();
    APlayerController* Observer = World->SpawnActor<APlayerController>();
    UPEPInventoryComponent* ObserverInventory = NewObject<UPEPInventoryComponent>(Observer);
    Observer->AddInstanceComponent(ObserverInventory);
    ObserverInventory->RegisterComponent();
    UPEPInventorySessionComponent* ObserverSession = NewObject<UPEPInventorySessionComponent>(Observer);
    Observer->AddInstanceComponent(ObserverSession);
    ObserverSession->RegisterComponent();

    APEPPickupItemActor* Simple = World->SpawnActor<APEPPickupItemActor>();
    Simple->SetSpawnItemDefinition(AttachmentDefinition);
    TestNull(TEXT("simple actor allocates no inventory"), Simple->FindComponentByClass<UPEPInventoryComponent>());
    TestTrue(TEXT("simple instance initializes"), Simple->GetAuthorityItemInstance().IsValid());
    TestFalse(TEXT("simple cannot Open"), Simple->CanInteract_Implementation(Player, PEPInteractionTags::Open).bSuccess);
    const FPEPItemId SimpleId = Simple->GetAuthorityItemInstance().ItemId;

    FPEPItemInstanceTree Tree;
    Tree.RootItem = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
    FPEPItemInstanceTreeEntry& Child = Tree.Children.AddDefaulted_GetRef();
    Child.Item = PEPItemAutomationTestSupport::MakeItemForTests(AttachmentDefinition);
    Child.Location = FPEPItemLocation::MakeItemContainer(Tree.RootItem.ItemId, Slot.SlotName);
    const FPEPItemId ChildId = Child.Item.ItemId;
    TestFalse(TEXT("simple refuses container tree"), Simple->SetItemTree(Tree));
    TestTrue(TEXT("refusal preserves simple instance"), Simple->GetAuthorityItemInstance().ItemId == SimpleId);

    APEPPickupContainerActor* Actor = World->SpawnActor<APEPPickupContainerActor>();
    Actor->SetSpawnItemDefinition(Definition);
    TestTrue(TEXT("empty container has a runtime root"), Actor->GetAuthorityItemInstance().IsValid());
    TestEqual(TEXT("empty container still uses an inventory"), Actor->Inventory->GetInventoryView().Entries.Num(), 1);
    if (!TestTrue(TEXT("container initializes"), Actor->SetItemTree(Tree))) return false;
    FPEPItemInstanceTree LockedTree = Tree;
    TestTrue(TEXT("container can be locked for restoration"),
        PEPItemContainerMutation::LockContainer(LockedTree.RootItem).bSuccess);
    TestTrue(TEXT("restoring a locked populated container preserves its layout"), Actor->SetItemTree(LockedTree));
    TestTrue(TEXT("locked restore keeps descendants"), Actor->Inventory->ContainsItem(ChildId));
    TestTrue(TEXT("restore unlocked tree for interaction"), Actor->SetItemTree(Tree));

    TestFalse(TEXT("world contents component does not replicate"), Actor->Inventory->GetIsReplicated());
    TestEqual(TEXT("root and attachment have one store"), Actor->Inventory->GetInventoryView().Entries.Num(), 2);
    TestEqual(TEXT("attachment is projected"), Actor->GetPublicPresentationNodes().Num(), 2);
    TestFalse(TEXT("attachment-only object cannot be opened"),
        Actor->ExecuteInteraction_Implementation(Player, PEPInteractionTags::Open).bSuccess);
    TestTrue(TEXT("Inspect through interaction"),
        Actor->ExecuteInteraction_Implementation(Player, PEPInteractionTags::Inspect).bSuccess);
    TestTrue(TEXT("second player can inspect same object"),
        Actor->ExecuteInteraction_Implementation(Observer, PEPInteractionTags::Inspect).bSuccess);
    FPEPInventoryEntry Entry;
    TestTrue(TEXT("session exposes attachment"), PlayerSession->FindSessionEntry(ChildId, Entry));
    TestFalse(TEXT("pickup without room fails"), Actor->ExecuteInteraction_Implementation(Player, PEPInteractionTags::Pickup).bSuccess);
    TestTrue(TEXT("failed pickup preserves source"), Actor->Inventory->ContainsItem(ChildId));
    Inventory->RegisterLocationValidator(TAG_TestWorldItemDestination, FPEPItemLocationValidator::CreateLambda(
        [](const FPEPItemInstance& Item, const FPEPItemLocation&) { return FPEPItemResult::Success(Item.ItemId); }));
    const FPEPItemLocation Destination = FPEPItemLocation::MakeNamedSlot(TAG_TestWorldItemDestination);
    TestTrue(TEXT("take attachment"), Actor->Inventory->TransferItemToInventory(Inventory, ChildId,
        Destination, FPEPItemAccessContext::From(Player)).bSuccess);
    TestEqual(TEXT("removed attachment disappears from projection"), Actor->GetPublicPresentationNodes().Num(), 1);
    TestFalse(TEXT("second session updates"), ObserverSession->FindSessionEntry(ChildId, Entry));
    TestTrue(TEXT("put attachment back"), Inventory->TransferItemToInventory(Actor->Inventory, ChildId,
        Child.Location, FPEPItemAccessContext::From(Player)).bSuccess);
    TestEqual(TEXT("restored attachment reappears"), Actor->GetPublicPresentationNodes().Num(), 2);
    TestTrue(TEXT("pickup transfers entire tree"), Actor->ExecuteInteraction_Implementation(Player, PEPInteractionTags::Pickup).bSuccess);
    TestTrue(TEXT("root reaches player"), Inventory->ContainsItem(Tree.RootItem.ItemId));
    TestTrue(TEXT("attachment reaches player"), Inventory->ContainsItem(ChildId));
    TestEqual(TEXT("source store is empty"), Actor->Inventory->GetInventoryView().Entries.Num(), 0);
    World->GetTimerManager().Tick(0.3f);
    TestFalse(TEXT("observer session closes after root pickup"), ObserverSession->HasTransferSession());
    APEPItemWorldActor* Dropped = nullptr;
    TestFalse(TEXT("wrong drop class is refused"), Inventory->DropItemTree(Tree.RootItem.ItemId,
        FTransform::Identity, FPEPItemAccessContext::From(Player), Dropped, APEPPickupItemActor::StaticClass()).bSuccess);
    TestNull(TEXT("failed drop returns no actor"), Dropped);
    TestTrue(TEXT("failed drop preserves root"), Inventory->ContainsItem(Tree.RootItem.ItemId));
    TestTrue(TEXT("failed drop preserves attachment"), Inventory->ContainsItem(ChildId));
    TestTrue(TEXT("container drop succeeds"), Inventory->DropItemTree(Tree.RootItem.ItemId,
        FTransform::Identity, FPEPItemAccessContext::From(Player), Dropped, APEPPickupContainerActor::StaticClass()).bSuccess);
    TestTrue(TEXT("drop preserves root identity"), Dropped && Dropped->GetAuthorityItemInstance().ItemId == Tree.RootItem.ItemId);
    const FPEPItemInstanceTree DroppedTree = Dropped ? Dropped->GetAuthorityItemTree() : FPEPItemInstanceTree();
    TestTrue(TEXT("drop preserves descendant identity"), DroppedTree.Children.Num() == 1 && DroppedTree.Children[0].Item.ItemId == ChildId);
    TestTrue(TEXT("simple pickup still succeeds"), Simple->ExecuteInteraction_Implementation(Player, PEPInteractionTags::Pickup).bSuccess);
    TestTrue(TEXT("simple pickup preserves identity"), Inventory->ContainsItem(SimpleId));
    APEPItemWorldActor* SimpleDrop = nullptr;
    TestTrue(TEXT("simple drop succeeds"), Inventory->DropItemTree(SimpleId, FTransform::Identity,
        FPEPItemAccessContext::From(Player), SimpleDrop, APEPPickupItemActor::StaticClass()).bSuccess);
    TestTrue(TEXT("simple drop preserves identity"), SimpleDrop && SimpleDrop->GetAuthorityItemInstance().ItemId == SimpleId);
    TestTrue(TEXT("simple drop has no inventory"), SimpleDrop && !SimpleDrop->FindComponentByClass<UPEPInventoryComponent>());
    return true;
}
#endif
