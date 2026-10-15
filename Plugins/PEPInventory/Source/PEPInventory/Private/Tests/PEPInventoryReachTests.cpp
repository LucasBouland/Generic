#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/PEPCoreTags.h"
#include "GameFramework/PlayerController.h"
#include "Item/PEPItemContainerMutation.h"
#include "Item/PEPItemQuery.h"
#include "Item/PEPItemQueryTypes.h"
#include "PEPInventoryTestFixture.h"

/*
 * What an outsider may reach, what a swap may overlap, what a pickup may duplicate and who may
 * claim a slot: the refusals that keep a store from being looted through a wall, from holding two
 * items on one cell, or from storing the same id twice. Plus the indices a store keeps write by
 * write, which have to describe it exactly after every kind of removal.
 */

using PEPInventoryTest::FFixture;

/* Named rather than anonymous, so a unity build cannot fold these into another test's helpers. */
namespace PEPInventoryReachTests
{
	const FName OpenSlotName(TEXT("open"));
	const FName HiddenSlotName(TEXT("hidden"));
	const FName MainSlotName(TEXT("main"));

	/** A grid both roles may take from, put into and rearrange: visibility and locks alone refuse. */
	FPEPContainerSlotDefinition MakeOpenGridSlot(const FName InSlotName, const FIntPoint InGridSize)
	{
		FPEPContainerSlotDefinition Slot;
		Slot.SlotName = InSlotName;
		Slot.Mode = EPEPContainerSlotMode::Grid;
		Slot.GridSize = InGridSize;
		Slot.bOwnerCanTake = true;
		Slot.bOwnerCanPut = true;
		Slot.bOwnerCanMoveInside = true;
		Slot.bOtherCanTake = true;
		Slot.bOtherCanPut = true;
		Slot.bOtherCanMoveInside = true;
		Slot.OwnerVisibility = EPEPContainerSlotVisibility::Lootable;
		Slot.OtherVisibility = EPEPContainerSlotVisibility::Lootable;
		Slot.bIncludeChildrenInExternalView = true;
		return Slot;
	}

	UPEPItemDefinition* MakeReachContainerDefinition(
		FFixture& Fixture,
		const FName DefinitionName,
		const TArray<FPEPContainerSlotDefinition>& Slots,
		const bool bCanRelock)
	{
		FPEPItemFragment_Container Container;
		Container.Slots = Slots;
		Container.ExternalLootVisibility = EPEPContainerLootVisibility::ShowContents;
		Container.bCanRelock = bCanRelock;

		FPEPItemFragment_InventoryShape Shape;
		Shape.Size = FIntPoint(1, 1);

		return Fixture.MakeDefinition(
			DefinitionName,
			{ FInstancedStruct::Make(Container), FInstancedStruct::Make(Shape) });
	}

	FPEPItemLocation At(const FPEPItemId InParentItemId, const FName InSlotName, const int32 X, const int32 Y)
	{
		return PEPInventoryGrid::MakeGridLocation(InParentItemId, InSlotName, FIntPoint(X, Y));
	}

	/*
	 * Constructed rather than spawned, as the access tests do: the role resolves on pointer
	 * identity alone. The caller keeps the controller alive for the length of the test.
	 */
	APlayerController* MakeOutsider(FFixture& Fixture)
	{
		return NewObject<APlayerController>(Fixture.World);
	}

	/** A validator owned by Domain that accepts, or refuses, everything it is asked about. */
	FPEPItemLocationValidator MakeDomainValidator(AActor* Domain, const bool bAccept)
	{
		return FPEPItemLocationValidator::CreateWeakLambda(
			Domain,
			[bAccept](const FPEPItemInstance& Item, const FPEPItemLocation&)
			{
				return bAccept
					? FPEPItemResult::Success(Item.ItemId)
					: FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, Item.ItemId);
			});
	}
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryExternalHiddenReachTest,
	"PEP.Inventory.Access.ExternalCannotTakeWhatItCannotSee",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The slot rules only ever read the slot an item sits in. A pocket hidden from outsiders but whose
 * bOtherCanTake was left at its default handed its contents to a looter who was never shown them —
 * the session hid the entry, and the store gave it up anyway.
 */
bool FPEPInventoryExternalHiddenReachTest::RunTest(const FString&)
{
	FFixture Fixture;

	FPEPContainerSlotDefinition Open = PEPInventoryReachTests::MakeOpenGridSlot(PEPInventoryReachTests::OpenSlotName, FIntPoint(2, 1));
	FPEPContainerSlotDefinition Hidden = PEPInventoryReachTests::MakeOpenGridSlot(PEPInventoryReachTests::HiddenSlotName, FIntPoint(2, 1));
	Hidden.OtherVisibility = EPEPContainerSlotVisibility::Hidden;

	UPEPItemDefinition* RigDef = PEPInventoryReachTests::MakeReachContainerDefinition(
		Fixture, TEXT("ReachHiddenRigDef"), { Open, Hidden }, false);
	UPEPItemDefinition* TrinketDef =
		Fixture.MakeShapedDefinition(TEXT("ReachHiddenTrinketDef"), FIntPoint(1, 1));

	const FPEPItemInstance Rig = FFixture::MakeItem(RigDef);
	const FPEPItemInstance Shown = FFixture::MakeItem(TrinketDef);
	const FPEPItemInstance Concealed = FFixture::MakeItem(TrinketDef);
	const FPEPItemAccessContext System = FPEPItemAccessContext::System();

	if (!TestTrue(TEXT("the rig is worn"), Fixture.AddToRootSlot(Rig).bSuccess)
		|| !TestTrue(TEXT("the open pocket holds something"),
			Fixture.Inventory->AddItem(Shown, PEPInventoryReachTests::At(Rig.ItemId, PEPInventoryReachTests::OpenSlotName, 0, 0), System).bSuccess)
		|| !TestTrue(TEXT("so does the hidden one"),
			Fixture.Inventory->AddItem(Concealed, PEPInventoryReachTests::At(Rig.ItemId, PEPInventoryReachTests::HiddenSlotName, 0, 0), System).bSuccess))
	{
		return false;
	}

	APlayerController* OutsiderController = PEPInventoryReachTests::MakeOutsider(Fixture);
	if (!TestNotNull(TEXT("an outsider controller exists"), OutsiderController))
	{
		return false;
	}
	const FPEPItemAccessContext Outsider = FPEPItemAccessContext::From(OutsiderController);

	if (!TestTrue(TEXT("the requester is an outsider"),
		Fixture.Inventory->ResolveAccessRole(Outsider) == EPEPAccessRole::External))
	{
		return false;
	}

	TestTrue(TEXT("an outsider may take what the open pocket shows"),
		Fixture.Inventory->CanExtractItemTree(Shown.ItemId, Outsider).bSuccess);

	const FPEPItemResult HiddenTake = Fixture.Inventory->CanExtractItemTree(Concealed.ItemId, Outsider);
	TestFalse(TEXT("but not what the hidden pocket keeps back, though its slot would allow it"),
		HiddenTake.bSuccess);
	TestTrue(TEXT("and the refusal does not confirm there is anything there"),
		HiddenTake.ReasonTag == PEPCoreTags::Reason_InvalidTarget);
	TestFalse(TEXT("nor one item at a time"),
		Fixture.Inventory->CanExtractItem(Concealed.ItemId, Outsider).bSuccess);
	TestFalse(TEXT("nor by rearranging it inside its pocket"),
		Fixture.Inventory->CanMoveItem(
			Concealed.ItemId, PEPInventoryReachTests::At(Rig.ItemId, PEPInventoryReachTests::HiddenSlotName, 1, 0), Outsider).bSuccess);
	TestFalse(TEXT("nor by moving it out into the open one"),
		Fixture.Inventory->CanMoveItem(
			Concealed.ItemId, PEPInventoryReachTests::At(Rig.ItemId, PEPInventoryReachTests::OpenSlotName, 1, 0), Outsider).bSuccess);

	/* Owners are not projected: the same pocket is theirs in full. */
	TestTrue(TEXT("the owner still takes from the hidden pocket"),
		Fixture.Inventory->CanExtractItemTree(Concealed.ItemId, System).bSuccess);
	TestTrue(TEXT("and still rearranges it"),
		Fixture.Inventory->CanMoveItem(
			Concealed.ItemId, PEPInventoryReachTests::At(Rig.ItemId, PEPInventoryReachTests::OpenSlotName, 1, 0), System).bSuccess);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryExternalAncestorLockTest,
	"PEP.Inventory.Access.LockedAncestorKeepsOutsidersOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A lock was read on the container holding an item and nowhere above it. An open pouch inside a
 * locked backpack therefore gave up its contents, and took new ones, from anyone the pouch's own
 * slot allowed — the lock on the backpack locked only the pouch itself.
 */
bool FPEPInventoryExternalAncestorLockTest::RunTest(const FString&)
{
	FFixture Fixture;

	const FPEPContainerSlotDefinition Main = PEPInventoryReachTests::MakeOpenGridSlot(PEPInventoryReachTests::MainSlotName, FIntPoint(2, 2));
	UPEPItemDefinition* BackpackDef = PEPInventoryReachTests::MakeReachContainerDefinition(
		Fixture, TEXT("ReachLockBackpackDef"), { Main }, /*bCanRelock*/ true);
	UPEPItemDefinition* PouchDef = PEPInventoryReachTests::MakeReachContainerDefinition(
		Fixture, TEXT("ReachLockPouchDef"), { Main }, /*bCanRelock*/ false);
	UPEPItemDefinition* TrinketDef =
		Fixture.MakeShapedDefinition(TEXT("ReachLockTrinketDef"), FIntPoint(1, 1));

	const FPEPItemInstance Backpack = FFixture::MakeItem(BackpackDef);
	const FPEPItemInstance Pouch = FFixture::MakeItem(PouchDef);
	const FPEPItemInstance Trinket = FFixture::MakeItem(TrinketDef);
	const FPEPItemInstance Spare = FFixture::MakeItem(TrinketDef);
	const FPEPItemAccessContext System = FPEPItemAccessContext::System();

	if (!TestTrue(TEXT("the backpack is worn"), Fixture.AddToRootSlot(Backpack).bSuccess)
		|| !TestTrue(TEXT("the pouch is inside it"),
			Fixture.Inventory->AddItem(Pouch, PEPInventoryReachTests::At(Backpack.ItemId, PEPInventoryReachTests::MainSlotName, 0, 0), System).bSuccess)
		|| !TestTrue(TEXT("and something is inside the pouch"),
			Fixture.Inventory->AddItem(Trinket, PEPInventoryReachTests::At(Pouch.ItemId, PEPInventoryReachTests::MainSlotName, 0, 0), System).bSuccess))
	{
		return false;
	}

	APlayerController* OutsiderController = PEPInventoryReachTests::MakeOutsider(Fixture);
	if (!TestNotNull(TEXT("an outsider controller exists"), OutsiderController))
	{
		return false;
	}
	const FPEPItemAccessContext Outsider = FPEPItemAccessContext::From(OutsiderController);

	const FPEPItemLocation SpareCell = PEPInventoryReachTests::At(Pouch.ItemId, PEPInventoryReachTests::MainSlotName, 1, 0);

	TestTrue(TEXT("unlocked, an outsider may take from the pouch"),
		Fixture.Inventory->CanExtractItemTree(Trinket.ItemId, Outsider).bSuccess);
	TestTrue(TEXT("and put into it"),
		Fixture.Inventory->CanAddItem(Spare, SpareCell, Outsider).bSuccess);

	const FPEPItemResult Locked = Fixture.Inventory->MutateItem(
		Backpack.ItemId,
		[](FPEPItemInstance& Item)
		{
			return PEPItemContainerMutation::LockContainer(Item).bSuccess;
		},
		System);

	if (!TestTrue(TEXT("the backpack locks"), Locked.bSuccess))
	{
		return false;
	}

	const FPEPItemResult LockedTake = Fixture.Inventory->CanExtractItemTree(Trinket.ItemId, Outsider);
	TestFalse(TEXT("a locked grandparent keeps an outsider out of the open pouch"), LockedTake.bSuccess);
	TestTrue(TEXT("as a refusal, not a disappearance"),
		LockedTake.ReasonTag == PEPCoreTags::Reason_BlockedByState);
	TestFalse(TEXT("item by item as well"),
		Fixture.Inventory->CanExtractItem(Trinket.ItemId, Outsider).bSuccess);
	TestFalse(TEXT("and out of rearranging what is in it"),
		Fixture.Inventory->CanMoveItem(
			Trinket.ItemId, PEPInventoryReachTests::At(Pouch.ItemId, PEPInventoryReachTests::MainSlotName, 1, 1), Outsider).bSuccess);
	TestFalse(TEXT("and out of putting anything into it"),
		Fixture.Inventory->CanAddItem(Spare, SpareCell, Outsider).bSuccess);

	TestTrue(TEXT("the owner still takes from the pouch"),
		Fixture.Inventory->CanExtractItemTree(Trinket.ItemId, System).bSuccess);
	TestTrue(TEXT("and still puts into it"),
		Fixture.Inventory->CanAddItem(Spare, SpareCell, System).bSuccess);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventorySwapOverlapTest,
	"PEP.Inventory.Move.SwapRefusesFootprintsThatWouldOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Each half of a swap is checked with the other item ignored, which is right for the cells it
 * leaves and blind to where it lands. In a row of three, a 1x1 at x=0 and a 2x1 at x=1 trade
 * origins onto a shared middle cell: both halves passed, and the swap stacked two items there.
 */
bool FPEPInventorySwapOverlapTest::RunTest(const FString&)
{
	{
		FFixture Fixture;
		UPEPItemDefinition* BagDef =
			Fixture.MakeContainerDefinition(TEXT("SwapOverlapBagDef"), PEPInventoryReachTests::MainSlotName, FIntPoint(3, 1));
		UPEPItemDefinition* SmallDef =
			Fixture.MakeShapedDefinition(TEXT("SwapOverlapSmallDef"), FIntPoint(1, 1));
		UPEPItemDefinition* WideDef =
			Fixture.MakeShapedDefinition(TEXT("SwapOverlapWideDef"), FIntPoint(2, 1));

		const FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
		const FPEPItemInstance Small = FFixture::MakeItem(SmallDef);
		const FPEPItemInstance Wide = FFixture::MakeItem(WideDef);
		const FPEPItemLocation SmallLocation = PEPInventoryReachTests::At(Bag.ItemId, PEPInventoryReachTests::MainSlotName, 0, 0);
		const FPEPItemLocation WideLocation = PEPInventoryReachTests::At(Bag.ItemId, PEPInventoryReachTests::MainSlotName, 1, 0);
		const FPEPItemAccessContext System = FPEPItemAccessContext::System();

		if (!TestTrue(TEXT("the bag is worn"), Fixture.AddToRootSlot(Bag).bSuccess)
			|| !TestTrue(TEXT("a 1x1 sits at x=0"),
				Fixture.Inventory->AddItem(Small, SmallLocation, System).bSuccess)
			|| !TestTrue(TEXT("a 2x1 sits at x=1"),
				Fixture.Inventory->AddItem(Wide, WideLocation, System).bSuccess))
		{
			return false;
		}

		TestFalse(TEXT("the swap is refused in preview"),
			Fixture.Inventory->CanSwapItems(Small.ItemId, Wide.ItemId, System).bSuccess);
		TestFalse(TEXT("and in execution"),
			Fixture.Inventory->SwapItems(Small.ItemId, Wide.ItemId, System).bSuccess);

		FPEPItemLocation SmallNow;
		FPEPItemLocation WideNow;
		Fixture.Inventory->FindItemLocation(Small.ItemId, SmallNow);
		Fixture.Inventory->FindItemLocation(Wide.ItemId, WideNow);
		TestTrue(TEXT("the 1x1 stayed put"), SmallNow == SmallLocation);
		TestTrue(TEXT("and so did the 2x1"), WideNow == WideLocation);
	}

	/* Different sizes are not the fault in themselves: with room between them, they trade. */
	{
		FFixture Fixture;
		UPEPItemDefinition* BagDef =
			Fixture.MakeContainerDefinition(TEXT("SwapApartBagDef"), PEPInventoryReachTests::MainSlotName, FIntPoint(4, 1));
		UPEPItemDefinition* SmallDef =
			Fixture.MakeShapedDefinition(TEXT("SwapApartSmallDef"), FIntPoint(1, 1));
		UPEPItemDefinition* WideDef =
			Fixture.MakeShapedDefinition(TEXT("SwapApartWideDef"), FIntPoint(2, 1));

		const FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
		const FPEPItemInstance Small = FFixture::MakeItem(SmallDef);
		const FPEPItemInstance Wide = FFixture::MakeItem(WideDef);
		const FPEPItemLocation SmallLocation = PEPInventoryReachTests::At(Bag.ItemId, PEPInventoryReachTests::MainSlotName, 0, 0);
		const FPEPItemLocation WideLocation = PEPInventoryReachTests::At(Bag.ItemId, PEPInventoryReachTests::MainSlotName, 2, 0);
		const FPEPItemAccessContext System = FPEPItemAccessContext::System();

		if (!TestTrue(TEXT("the bag is worn"), Fixture.AddToRootSlot(Bag).bSuccess)
			|| !TestTrue(TEXT("a 1x1 sits at x=0"),
				Fixture.Inventory->AddItem(Small, SmallLocation, System).bSuccess)
			|| !TestTrue(TEXT("a 2x1 sits at x=2"),
				Fixture.Inventory->AddItem(Wide, WideLocation, System).bSuccess))
		{
			return false;
		}

		TestTrue(TEXT("footprints that stay apart swap"),
			Fixture.Inventory->SwapItems(Small.ItemId, Wide.ItemId, System).bSuccess);

		FPEPItemLocation SmallNow;
		FPEPItemLocation WideNow;
		Fixture.Inventory->FindItemLocation(Small.ItemId, SmallNow);
		Fixture.Inventory->FindItemLocation(Wide.ItemId, WideNow);
		TestTrue(TEXT("the 1x1 took the 2x1's origin"), SmallNow == WideLocation);
		TestTrue(TEXT("and the 2x1 the 1x1's"), WideNow == SmallLocation);
	}

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryPickupDuplicateIdTest,
	"PEP.Inventory.Pickup.RefusesAnIdTheStoreAlreadyHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A pickup that merged away entirely never reached a validation of the item itself, and one that
 * found a cell reached the commit with nothing but a check() between a duplicate id and the store.
 * Either way, the same item picked up twice is refused before anything is written.
 */
bool FPEPInventoryPickupDuplicateIdTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("DuplicatePickupBagDef"), PEPInventoryReachTests::MainSlotName, FIntPoint(4, 4));
	UPEPItemDefinition* ToolDef =
		Fixture.MakeShapedDefinition(TEXT("DuplicatePickupToolDef"), FIntPoint(1, 1));
	UPEPItemDefinition* AmmoDef =
		Fixture.MakeStackableDefinition(TEXT("DuplicatePickupAmmoDef"), 60);

	const FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	const FPEPItemInstance Tool = FFixture::MakeItem(ToolDef);
	const FPEPItemInstance Ammo = FFixture::MakeItem(AmmoDef);
	const FPEPItemAccessContext System = FPEPItemAccessContext::System();

	if (!TestTrue(TEXT("the bag is worn"), Fixture.AddToRootSlot(Bag).bSuccess)
		|| !TestTrue(TEXT("a tool is stored"),
			Fixture.Inventory->AddItem(Tool, PEPInventoryReachTests::At(Bag.ItemId, PEPInventoryReachTests::MainSlotName, 0, 0), System).bSuccess)
		|| !TestTrue(TEXT("a stack is stored"),
			Fixture.Inventory->AddItem(Ammo, PEPInventoryReachTests::At(Bag.ItemId, PEPInventoryReachTests::MainSlotName, 1, 0), System).bSuccess))
	{
		return false;
	}

	const int32 EntriesBefore = Fixture.Inventory->GetInventoryView().Entries.Num();

	TestFalse(TEXT("a second copy of a stored item is refused in preview"),
		Fixture.Inventory->CanAddPickupItem(Tool, System).bSuccess);
	TestFalse(TEXT("and in execution, rather than stored twice"),
		Fixture.Inventory->AddPickupItem(Tool, System).bSuccess);

	/* The stack would merge into itself, needing no cell: the path that skipped the item checks. */
	TestFalse(TEXT("a stored stack picked up again is refused too"),
		Fixture.Inventory->AddPickupItem(Ammo, System).bSuccess);

	TestEqual(TEXT("nothing was added"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), EntriesBefore);

	FPEPItemStackData StackData;
	const FPEPItemInstance* StoredAmmo = Fixture.Inventory->FindItem(Ammo.ItemId);
	TestTrue(TEXT("the stored stack still reads"),
		StoredAmmo && PEPItemQuery::GetStackData(*StoredAmmo, StackData));
	TestEqual(TEXT("and was not topped up with itself"), StackData.Count, 1);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryValidatorClaimTest,
	"PEP.Inventory.Validators.AClaimBelongsToItsRegistrant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A second domain registering a claimed tag used to replace the first silently: the first went on
 * believing it decided the slot, and nothing said otherwise.
 */
bool FPEPInventoryValidatorClaimTest::RunTest(const FString&)
{
	FFixture Fixture;
	const FGameplayTag SlotTag = FFixture::SecondarySlotTag();
	const FPEPItemLocation Slot = FPEPItemLocation::MakeNamedSlot(SlotTag);
	const FPEPItemAccessContext System = FPEPItemAccessContext::System();

	AActor* FirstDomain = Fixture.World->SpawnActor<AActor>();
	AActor* SecondDomain = Fixture.World->SpawnActor<AActor>();
	if (!TestTrue(TEXT("two registrants exist"), FirstDomain != nullptr && SecondDomain != nullptr))
	{
		return false;
	}

	const FPEPItemInstance Item =
		FFixture::MakeItem(Fixture.MakeShapedDefinition(TEXT("ValidatorClaimItemDef"), FIntPoint(1, 1)));

	/* The refused registration is a setup fault, and says so once. */
	AddExpectedError(TEXT("already claims location tag"), EAutomationExpectedErrorFlags::Contains, 1);

	TestTrue(TEXT("the first registrant claims the tag"),
		Fixture.Inventory->RegisterLocationValidator(
			SlotTag,
			PEPInventoryReachTests::MakeDomainValidator(FirstDomain, true),
			/*bAllowAutoPlacement*/ true));

	TestFalse(TEXT("a second registrant is refused"),
		Fixture.Inventory->RegisterLocationValidator(
			SlotTag,
			PEPInventoryReachTests::MakeDomainValidator(SecondDomain, false),
			/*bAllowAutoPlacement*/ true));

	TestTrue(TEXT("and the first one still decides the slot"),
		Fixture.Inventory->CanAddItem(Item, Slot, System).bSuccess);

	TestTrue(TEXT("the holder may register again"),
		Fixture.Inventory->RegisterLocationValidator(
			SlotTag,
			PEPInventoryReachTests::MakeDomainValidator(FirstDomain, false),
			/*bAllowAutoPlacement*/ true));

	TestFalse(TEXT("which replaces its own binding"),
		Fixture.Inventory->CanAddItem(Item, Slot, System).bSuccess);

	Fixture.Inventory->UnregisterLocationValidator(SlotTag, FirstDomain);

	TestTrue(TEXT("once released, the tag can be claimed by someone else"),
		Fixture.Inventory->RegisterLocationValidator(
			SlotTag,
			PEPInventoryReachTests::MakeDomainValidator(SecondDomain, true),
			/*bAllowAutoPlacement*/ true));

	TestTrue(TEXT("and the new holder decides"),
		Fixture.Inventory->CanAddItem(Item, Slot, System).bSuccess);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInventoryIncrementalIndexTest,
	"PEP.Inventory.Store.IndicesFollowEveryWrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The indices are maintained write by write rather than rebuilt, and a removal swaps the last
 * entry into the hole it leaves. Every accessor has to keep answering exactly what the store holds
 * through removals from the middle, moves into freed cells and a whole subtree leaving at once.
 */
bool FPEPInventoryIncrementalIndexTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* BagDef =
		Fixture.MakeContainerDefinition(TEXT("IndexBagDef"), PEPInventoryReachTests::MainSlotName, FIntPoint(4, 4));
	UPEPItemDefinition* SmallDef =
		Fixture.MakeShapedDefinition(TEXT("IndexSmallDef"), FIntPoint(1, 1));
	const FPEPItemAccessContext System = FPEPItemAccessContext::System();

	const FPEPItemInstance Bag = FFixture::MakeItem(BagDef);
	if (!TestTrue(TEXT("the bag is worn"), Fixture.AddToRootSlot(Bag).bSuccess))
	{
		return false;
	}

	constexpr int32 ContentCount = 8;
	TArray<FPEPItemId> ContentIds;
	TArray<FPEPItemLocation> ContentLocations;
	for (int32 Index = 0; Index < ContentCount; ++Index)
	{
		const FPEPItemInstance Content = FFixture::MakeItem(SmallDef);
		const FPEPItemLocation Location = PEPInventoryReachTests::At(Bag.ItemId, PEPInventoryReachTests::MainSlotName, Index % 4, Index / 4);
		if (!TestTrue(TEXT("a content item is stored"),
			Fixture.Inventory->AddItem(Content, Location, System).bSuccess))
		{
			return false;
		}
		ContentIds.Add(Content.ItemId);
		ContentLocations.Add(Location);
	}

	/* From the middle and the front, so each removal swaps a different survivor into its place. */
	const TArray<int32> Destroyed = { 1, 4, 0 };
	for (const int32 Index : Destroyed)
	{
		TestTrue(TEXT("a content item is destroyed"),
			Fixture.Inventory->DestroyItem(ContentIds[Index], System).bSuccess);
	}

	TArray<FPEPItemId> Children;
	Fixture.Inventory->GetDirectChildren(Bag.ItemId, Children);
	TestEqual(TEXT("the bag lists exactly its survivors"), Children.Num(), ContentCount - Destroyed.Num());

	for (int32 Index = 0; Index < ContentCount; ++Index)
	{
		const bool bDestroyed = Destroyed.Contains(Index);
		FPEPItemLocation Found;
		FPEPItemId Occupant;
		const bool bOccupied = Fixture.Inventory->FindItemAtLocation(ContentLocations[Index], Occupant);

		if (bDestroyed)
		{
			TestFalse(TEXT("a destroyed item is gone"), Fixture.Inventory->ContainsItem(ContentIds[Index]));
			TestFalse(TEXT("and its cell reads empty"), bOccupied);
			TestTrue(TEXT("and takes a new item"),
				Fixture.Inventory->CanAddItem(
					FFixture::MakeItem(SmallDef), ContentLocations[Index], System).bSuccess);
			continue;
		}

		TestTrue(TEXT("a survivor still resolves"),
			Fixture.Inventory->FindItemLocation(ContentIds[Index], Found));
		TestTrue(TEXT("at the location it was given"), Found == ContentLocations[Index]);
		TestTrue(TEXT("its cell names it"), bOccupied && Occupant == ContentIds[Index]);
		TestTrue(TEXT("the bag lists it"), Children.Contains(ContentIds[Index]));
		TestFalse(TEXT("and nothing else fits on it"),
			Fixture.Inventory->CanAddItem(
				FFixture::MakeItem(SmallDef), ContentLocations[Index], System).bSuccess);
	}

	/* A survivor moved into a freed cell leaves its own cell free and takes the other. */
	const FPEPItemId Mover = ContentIds[7];
	if (TestTrue(TEXT("a survivor moves into a freed cell"),
		Fixture.Inventory->MoveItem(Mover, ContentLocations[1], System).bSuccess))
	{
		FPEPItemId Occupant;
		TestTrue(TEXT("the freed cell now names it"),
			Fixture.Inventory->FindItemAtLocation(ContentLocations[1], Occupant) && Occupant == Mover);
		TestFalse(TEXT("and the cell it left reads empty"),
			Fixture.Inventory->FindItemAtLocation(ContentLocations[7], Occupant));
	}

	/* The whole tree at once: K removals, each repairing the indices it touches. */
	TestTrue(TEXT("the bag and its contents are destroyed together"),
		Fixture.Inventory->DestroyItemTree(Bag.ItemId, System).bSuccess);

	TArray<FPEPItemId> Remaining;
	Fixture.Inventory->GetItemIds(Remaining);
	TestEqual(TEXT("the store is empty"), Remaining.Num(), 0);
	Fixture.Inventory->GetDirectChildren(Bag.ItemId, Children);
	TestEqual(TEXT("and the bag lists nothing"), Children.Num(), 0);

	FPEPItemId Occupant;
	TestFalse(TEXT("its named slot reads empty"),
		Fixture.Inventory->FindItemAtLocation(
			FPEPItemLocation::MakeNamedSlot(FFixture::RootSlotTag()), Occupant));
	TestTrue(TEXT("and takes a new item"),
		Fixture.AddToRootSlot(FFixture::MakeItem(BagDef)).bSuccess);

	return true;
}

#endif
