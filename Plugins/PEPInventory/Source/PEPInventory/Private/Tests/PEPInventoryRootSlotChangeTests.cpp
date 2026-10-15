#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PEPInventoryTestFixture.h"

using PEPInventoryTest::FFixture;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPEPInventoryRootSlotChangeTest,
    "PEP.Inventory.Events.RootSlotTags",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryRootSlotChangeTest::RunTest(const FString&)
{
    FFixture Fixture;
    const FGameplayTag FirstTag = FFixture::RootSlotTag();
    const FGameplayTag SecondTag = PEPItemTags::Item_Slot_Content_QuickAccess;
    Fixture.Inventory->RegisterLocationValidator(SecondTag,
        FPEPItemLocationValidator::CreateLambda([](const FPEPItemInstance& Item, const FPEPItemLocation&)
        {
            return FPEPItemResult::Success(Item.ItemId);
        }));
    const FPEPItemAccessContext Access = FPEPItemAccessContext::System();
    const FName Slot = TEXT("contents");
    UPEPItemDefinition* Container = Fixture.MakeContainerDefinition(TEXT("RootTagContainer"), Slot, FIntPoint(4, 4));
    const FPEPItemInstance Root = FFixture::MakeItem(Container);
    const FPEPItemInstance Child = FFixture::MakeItem(Container);
    const FPEPItemInstance Grandchild = FFixture::MakeItem(Container);
    const FPEPItemInstance Leaf = FFixture::MakeItem(Fixture.MakeShapedDefinition(TEXT("RootTagLeaf"), FIntPoint(1, 1)));
    TArray<FPEPItemStoreChange> Events;
    Fixture.Inventory->OnItemStoreChanged().AddLambda([&Events](const FPEPItemStoreChange& Change) { Events.Add(Change); });
    TestTrue(TEXT("root added"), Fixture.AddToRootSlot(Root).bSuccess);
    TestTrue(TEXT("child added"), Fixture.Inventory->AddItem(Child,
        FPEPItemLocation::MakeItemContainer(Root.ItemId, Slot, FIntPoint(0, 0), false), Access).bSuccess);
    TestTrue(TEXT("grandchild added"), Fixture.Inventory->AddItem(Grandchild,
        FPEPItemLocation::MakeItemContainer(Child.ItemId, Slot, FIntPoint(0, 0), false), Access).bSuccess);
    const FPEPItemLocation LeafOrigin = FPEPItemLocation::MakeItemContainer(Grandchild.ItemId, Slot, FIntPoint(0, 0), false);
    TestTrue(TEXT("leaf added"), Fixture.Inventory->AddItem(Leaf, LeafOrigin, Access).bSuccess);
    for (const FPEPItemStoreChange& Event : Events)
    {
        TestTrue(TEXT("every addition resolves the same root through nested parents"), Event.CurrentRootSlotTag == FirstTag);
        TestFalse(TEXT("addition has no previous root"), Event.PreviousRootSlotTag.IsValid());
    }
    Events.Reset();
    TestTrue(TEXT("nested leaf moves directly to the other root slot"), Fixture.Inventory->MoveItem(
        Leaf.ItemId, FPEPItemLocation::MakeNamedSlot(SecondTag), Access).bSuccess);
    if (!TestEqual(TEXT("one move"), Events.Num(), 1)) { return false; }
    TestTrue(TEXT("move retains nested source root"), Events[0].PreviousRootSlotTag == FirstTag);
    TestTrue(TEXT("move resolves destination root"), Events[0].CurrentRootSlotTag == SecondTag);
    TestTrue(TEXT("leaf returns"), Fixture.Inventory->MoveItem(Leaf.ItemId, LeafOrigin, Access).bSuccess);
    Events.Reset();
    TestTrue(TEXT("whole tree removed"), Fixture.Inventory->DestroyItemTree(Root.ItemId, Access).bSuccess);
    TestEqual(TEXT("one removal per item"), Events.Num(), 4);
    for (const FPEPItemStoreChange& Event : Events)
    {
        TestTrue(TEXT("removed descendant keeps root after all parents disappear"), Event.PreviousRootSlotTag == FirstTag);
        TestFalse(TEXT("removed entry has no current root"), Event.CurrentRootSlotTag.IsValid());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPEPInventoryReplicatedRootSlotChangeTest,
    "PEP.Inventory.Events.ReplicatedRootSlotTags",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInventoryReplicatedRootSlotChangeTest::RunTest(const FString&)
{
    FFixture Fixture;
    const FPEPItemAccessContext Access = FPEPItemAccessContext::System();
    const FName Slot = TEXT("contents");
    const FGameplayTag FirstTag = FFixture::RootSlotTag();
    const FGameplayTag SecondTag = PEPItemTags::Item_Slot_Content_QuickAccess;
    Fixture.Inventory->RegisterLocationValidator(SecondTag,
        FPEPItemLocationValidator::CreateLambda([](const FPEPItemInstance& Item, const FPEPItemLocation&)
        {
            return FPEPItemResult::Success(Item.ItemId);
        }));
    const FPEPItemInstance Root = FFixture::MakeItem(Fixture.MakeContainerDefinition(TEXT("ReplicatedRootTagContainer"), Slot, FIntPoint(4, 4)));
    const FPEPItemInstance Leaf = FFixture::MakeItem(Fixture.MakeShapedDefinition(TEXT("ReplicatedRootTagLeaf"), FIntPoint(1, 1)));
    const FPEPItemLocation Origin = FPEPItemLocation::MakeItemContainer(Root.ItemId, Slot, FIntPoint(0, 0), false);
    TestTrue(TEXT("root seeded"), Fixture.AddToRootSlot(Root).bSuccess);
    TestTrue(TEXT("leaf seeded"), Fixture.Inventory->AddItem(Leaf, Origin, Access).bSuccess);
    FPEPInventoryView ReceivedView;
    ReceivedView.OwnerComponent = Fixture.Inventory;
    ReceivedView.PostReplicatedReceive(FFastArraySerializer::FPostReplicatedReceiveParameters{});
    // Simulate a delta whose parent has already moved when the child's callback runs.
    TestTrue(TEXT("root relocated"), Fixture.Inventory->MoveItem(Root.ItemId, FPEPItemLocation::MakeNamedSlot(SecondTag), Access).bSuccess);
    FPEPInventoryEntry Received;
    Received.Item = Leaf;
    Received.Location = FPEPItemLocation::MakeItemContainer(Root.ItemId, Slot, FIntPoint(1, 0), false);
    TArray<FPEPItemStoreChange> Events;
    Fixture.Inventory->OnItemStoreChanged().AddLambda([&Events](const FPEPItemStoreChange& Change) { Events.Add(Change); });
    Received.LastObservedLocation = Origin;
    Received.PostReplicatedChange(ReceivedView);
    if (!TestEqual(TEXT("replicated move emitted"), Events.Num(), 1)) { return false; }
    TestTrue(TEXT("previous root uses pre-delta parent graph"), Events[0].PreviousRootSlotTag == FirstTag);
    TestTrue(TEXT("current root uses final parent graph"), Events[0].CurrentRootSlotTag == SecondTag);
    ReceivedView.PostReplicatedReceive(FFastArraySerializer::FPostReplicatedReceiveParameters{});
    for (const FPEPInventoryEntry& Entry : Fixture.Inventory->GetInventoryView().Entries)
    {
        FPEPInventoryEntry Removed = Entry;
        Removed.PreReplicatedRemove(ReceivedView);
    }
    TestTrue(TEXT("tree gone before replicated removal notification"), Fixture.Inventory->DestroyItemTree(Root.ItemId, Access).bSuccess);
    Events.Reset();
    ReceivedView.PostReplicatedReceive(FFastArraySerializer::FPostReplicatedReceiveParameters{});
    TestEqual(TEXT("queued removals emitted"), Events.Num(), 2);
    for (const FPEPItemStoreChange& Event : Events)
    {
        TestTrue(TEXT("replicated removal keeps departed ancestor"), Event.PreviousRootSlotTag == SecondTag);
    }
    return true;
}

#endif
