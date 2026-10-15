#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PEPInventoryTestFixture.h"

#include "Core/PEPCoreTags.h"
#include "GameFramework/PlayerController.h"
#include "Item/Actor/PEPContainerItemWorldActor.h"
#include "Item/Actor/PEPSimpleItemWorldActor.h"
#include "Item/PEPItemPresetTypes.h"
#include "Pickup/PEPItemPickup.h"

using PEPInventoryTest::FFixture;

/* Named rather than anonymous: in a unity build every test file shares one translation unit. */
namespace PEPItemPickupTestSupport
{
	/** A simple world item holding one fresh instance of Definition, on the fixture's authority. */
	APEPSimpleItemWorldActor* SpawnSimpleItem(
		FFixture& Fixture,
		const UPEPItemDefinition* Definition,
		FPEPItemId& OutItemId)
	{
		APEPSimpleItemWorldActor* Actor = Fixture.World->SpawnActor<APEPSimpleItemWorldActor>();
		if (!Actor)
		{
			return nullptr;
		}

		const FPEPItemInstance Item = FFixture::MakeItem(Definition);
		OutItemId = Item.ItemId;
		Actor->SetItemInstance(Item);
		return Actor;
	}

	/*
	 * Any live requester acts as the store's owner, so what these tests exercise is the pickup
	 * itself rather than the outsider slot rules, which have tests of their own.
	 */
	APlayerController* SpawnRequester(FFixture& Fixture)
	{
		Fixture.Inventory->ExternalAccess = EPEPExternalAccessMode::SameAsOwner;
		return Fixture.World->SpawnActor<APlayerController>();
	}
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemPickupRefusesNullControllerTest,
	"PEP.Inventory.Pickup.RefusesNullController",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A pickup with nobody asking used to reach the store as FPEPItemAccessContext::From(nullptr) —
 * the server's own bypass under another name. An unpossessed pawn is not the server.
 */
bool FPEPItemPickupRefusesNullControllerTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* Definition =
		Fixture.MakeShapedDefinition(TEXT("PickupNullControllerDef"), FIntPoint(1, 1));
	APlayerController* Requester = PEPItemPickupTestSupport::SpawnRequester(Fixture);

	FPEPItemId ItemId;
	APEPSimpleItemWorldActor* Actor =
		PEPItemPickupTestSupport::SpawnSimpleItem(Fixture, Definition, ItemId);
	if (!TestNotNull(TEXT("the world item spawns"), Actor)
		|| !TestNotNull(TEXT("the requester spawns"), Requester)
		|| !TestNotNull(TEXT("and the item holds its instance"), Actor->FindAuthorityItemInstance()))
	{
		return false;
	}

	const FPEPItemResult Check =
		PEPItemPickup::CanPickupWorldItem(Actor, Fixture.Inventory, nullptr);
	TestFalse(TEXT("a pickup nobody asked for is refused"), Check.bSuccess);
	TestTrue(TEXT("as an invalid target"), Check.ReasonTag == PEPCoreTags::Reason_InvalidTarget);

	const FPEPItemResult Executed =
		PEPItemPickup::ExecutePickupWorldItem(Actor, Fixture.Inventory, nullptr);
	TestFalse(TEXT("and executing it is refused the same way"), Executed.bSuccess);
	TestTrue(TEXT("for the same reason"), Executed.ReasonTag == PEPCoreTags::Reason_InvalidTarget);
	TestTrue(TEXT("the actor keeps its item"),
		Actor->GetAuthorityItemInstance().ItemId == ItemId);
	TestFalse(TEXT("and the store never received it"), Fixture.Inventory->ContainsItem(ItemId));

	/* The refusal was the missing identity and nothing else: with someone asking, it fits. */
	TestTrue(TEXT("the same pickup with a requester is accepted"),
		PEPItemPickup::CanPickupWorldItem(Actor, Fixture.Inventory, Requester).bSuccess);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemPickupEmptiesSimpleActorTest,
	"PEP.Inventory.Pickup.SimpleActorIsEmptiedAtomically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The item ends in exactly one place. The actor is emptied before the store announces the
 * arrival — a listener reacting to it must not find the item still on the ground — and a second
 * pickup of the same actor finds nothing. Destroying the actor stays its caller's decision.
 */
bool FPEPItemPickupEmptiesSimpleActorTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* Definition =
		Fixture.MakeShapedDefinition(TEXT("PickupAtomicDef"), FIntPoint(1, 1));
	APlayerController* Requester = PEPItemPickupTestSupport::SpawnRequester(Fixture);

	FPEPItemId ItemId;
	APEPSimpleItemWorldActor* Actor =
		PEPItemPickupTestSupport::SpawnSimpleItem(Fixture, Definition, ItemId);
	if (!TestNotNull(TEXT("the world item spawns"), Actor)
		|| !TestNotNull(TEXT("the requester spawns"), Requester))
	{
		return false;
	}

	bool bObservedArrival = false;
	bool bActorHeldItemDuringArrival = false;
	const FDelegateHandle Listener = Fixture.Inventory->OnItemStoreChanged().AddLambda(
		[Actor, ItemId, &bObservedArrival, &bActorHeldItemDuringArrival](const FPEPItemStoreChange& Change)
		{
			if (Change.ItemId == ItemId && Change.ChangeType == EPEPItemStoreChangeType::Added)
			{
				bObservedArrival = true;
				bActorHeldItemDuringArrival = Actor->FindAuthorityItemInstance() != nullptr;
			}
		});

	const FPEPItemResult Result =
		PEPItemPickup::ExecutePickupWorldItem(Actor, Fixture.Inventory, Requester);
	Fixture.Inventory->OnItemStoreChanged().Remove(Listener);

	if (!TestTrue(TEXT("the pickup succeeds"), Result.bSuccess))
	{
		return false;
	}

	TestTrue(TEXT("the item is in the store"), Fixture.Inventory->ContainsItem(ItemId));
	TestTrue(TEXT("the store announced its arrival"), bObservedArrival);
	TestFalse(TEXT("and the actor was already empty while it did"), bActorHeldItemDuringArrival);
	TestNull(TEXT("the actor holds nothing afterwards"), Actor->FindAuthorityItemInstance());
	TestFalse(TEXT("its authority tree is empty"), Actor->GetAuthorityItemTree().RootItem.IsValid());
	TestTrue(TEXT("and it presents nothing"), Actor->GetPublicPresentationNodes().IsEmpty());
	TestFalse(TEXT("the pickup does not destroy the actor itself"), Actor->IsActorBeingDestroyed());

	const FPEPItemResult Second =
		PEPItemPickup::ExecutePickupWorldItem(Actor, Fixture.Inventory, Requester);
	TestFalse(TEXT("picking the emptied actor up again fails"), Second.bSuccess);
	TestTrue(TEXT("as an invalid target"), Second.ReasonTag == PEPCoreTags::Reason_InvalidTarget);
	TestEqual(TEXT("the store holds the item exactly once"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 1);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemPickupLeavesContainerActorToCallerTest,
	"PEP.Inventory.Pickup.ContainerActorIsLeftToItsCaller",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A container's own store is the only copy, so moving the tree out is the whole transaction. The
 * actor is left empty and standing, exactly like a simple one: what becomes of it is the caller's
 * decision, not a side effect of its store emptying mid-pickup.
 */
bool FPEPItemPickupLeavesContainerActorToCallerTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* CrateDef = Fixture.MakeContainerDefinition(
		TEXT("PickupCrateDef"), TEXT("main"), FIntPoint(2, 2));
	APlayerController* Requester = PEPItemPickupTestSupport::SpawnRequester(Fixture);

	APEPContainerItemWorldActor* Actor = Fixture.World->SpawnActor<APEPContainerItemWorldActor>();
	if (!TestNotNull(TEXT("the container actor spawns"), Actor)
		|| !TestNotNull(TEXT("the requester spawns"), Requester))
	{
		return false;
	}

	FPEPItemInstanceTree Tree;
	Tree.RootItem = FFixture::MakeItem(CrateDef);
	const FPEPItemId CrateId = Tree.RootItem.ItemId;
	if (!TestTrue(TEXT("the container holds its tree"), Actor->SetItemTree(Tree)))
	{
		return false;
	}

	TestFalse(TEXT("nobody asking is refused here too"),
		PEPItemPickup::ExecutePickupWorldItem(Actor, Fixture.Inventory, nullptr).bSuccess);
	TestNotNull(TEXT("and the container keeps its tree"), Actor->FindAuthorityItemInstance());

	const FPEPItemResult Result =
		PEPItemPickup::ExecutePickupWorldItem(Actor, Fixture.Inventory, Requester);
	if (!TestTrue(TEXT("the pickup succeeds"), Result.bSuccess))
	{
		return false;
	}

	TestTrue(TEXT("the container is in the store"), Fixture.Inventory->ContainsItem(CrateId));
	TestNull(TEXT("the actor holds nothing afterwards"), Actor->FindAuthorityItemInstance());
	TestTrue(TEXT("and presents nothing"), Actor->GetPublicPresentationNodes().IsEmpty());
	TestFalse(TEXT("but is still standing for its caller to deal with"), Actor->IsActorBeingDestroyed());
	TestFalse(TEXT("a second pickup finds nothing"),
		PEPItemPickup::ExecutePickupWorldItem(Actor, Fixture.Inventory, Requester).bSuccess);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPItemPickupRefusalKeepsActorTest,
	"PEP.Inventory.Pickup.RefusedPickupLeavesTheActorWhole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/* A store with no room refuses the pickup, and the actor still holds the very same item. */
bool FPEPItemPickupRefusalKeepsActorTest::RunTest(const FString&)
{
	FFixture Fixture;
	UPEPItemDefinition* Definition =
		Fixture.MakeShapedDefinition(TEXT("PickupRefusedDef"), FIntPoint(1, 1));
	UPEPItemDefinition* BlockerDefinition =
		Fixture.MakeShapedDefinition(TEXT("PickupBlockerDef"), FIntPoint(1, 1));
	APlayerController* Requester = PEPItemPickupTestSupport::SpawnRequester(Fixture);

	/* The fixture's one named slot, taken by something that holds nothing: nowhere left to go. */
	if (!TestTrue(TEXT("the only slot is filled"),
		Fixture.AddToRootSlot(FFixture::MakeItem(BlockerDefinition)).bSuccess))
	{
		return false;
	}

	FPEPItemId ItemId;
	APEPSimpleItemWorldActor* Actor =
		PEPItemPickupTestSupport::SpawnSimpleItem(Fixture, Definition, ItemId);
	if (!TestNotNull(TEXT("the world item spawns"), Actor)
		|| !TestNotNull(TEXT("the requester spawns"), Requester))
	{
		return false;
	}

	const FPEPItemResult Result =
		PEPItemPickup::ExecutePickupWorldItem(Actor, Fixture.Inventory, Requester);
	TestFalse(TEXT("a full store refuses the pickup"), Result.bSuccess);
	TestTrue(TEXT("the actor still holds the same item"),
		Actor->GetAuthorityItemInstance().ItemId == ItemId);
	TestFalse(TEXT("the store never received it"), Fixture.Inventory->ContainsItem(ItemId));
	TestEqual(TEXT("and still holds only what it had"),
		Fixture.Inventory->GetInventoryView().Entries.Num(), 1);

	return true;
}

#endif
