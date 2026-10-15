#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Algo/Reverse.h"
#include "Components/PEPEquipmentComponent.h"
#include "Core/PEPCoreTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Equipment/PEPEquipmentEventTypes.h"
#include "Equipment/PEPEquipmentPresentationFragment.h"
#include "Equipment/PEPEquipmentTags.h"
#include "GameFramework/Actor.h"
#include "Item/PEPItemAccess.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemInstance.h"
#include "StructUtils/InstancedStruct.h"
#include "Tests/PEPTestEquipmentListenerComponent.h"
#include "Tests/PEPTestItemStoreComponent.h"

namespace PEPEquipmentEventTest
{
	/** Same shape as the projection tests' fixture, plus a listener registered on the owner. */
	struct FEventFixture
	{
		UWorld* World = nullptr;
		AActor* Owner = nullptr;
		UPEPTestItemStoreComponent* Store = nullptr;
		UPEPEquipmentComponent* Equipment = nullptr;
		UPEPTestEquipmentListenerComponent* Listener = nullptr;
		TArray<TObjectPtr<UPEPItemDefinition>> Definitions;

		FEventFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());

			Owner = World->SpawnActor<AActor>();
			Store = NewObject<UPEPTestItemStoreComponent>(Owner);
			Store->RegisterComponent();

			Equipment = NewObject<UPEPEquipmentComponent>(Owner);
			Equipment->SetLocalPresentationEnabled(false);
			Equipment->RegisterComponent();

			Listener = NewObject<UPEPTestEquipmentListenerComponent>(Owner);
			Listener->RegisterComponent();
			Listener->Listen(Equipment);
			World->BeginPlay();
		}

		~FEventFixture()
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

		FPEPItemInstance MakeItem(const FName DefinitionName)
		{
			UPEPItemDefinition* Definition =
				NewObject<UPEPItemDefinition>(GetTransientPackage(), DefinitionName);
			Definition->DefinitionFragments.Add(
				FInstancedStruct::Make(FPEPEquipmentPresentationFragment()));
			PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(Definition);
			Definitions.Add(Definition);
			return PEPItemAutomationTestSupport::MakeItemForTests(Definition);
		}

		bool Add(const FPEPItemInstance& Item, const FPEPItemLocation& Location)
		{
			return Store->AddItem(Item, Location, FPEPItemAccessContext::System()).bSuccess;
		}
	};

	FPEPItemLocation Slot(const FGameplayTag SlotTag)
	{
		return FPEPItemLocation::MakeNamedSlot(SlotTag);
	}

	FPEPEquippedItemPublic MakeNode(
		const int32 ItemValue,
		const FGameplayTag SlotTag,
		const int32 ParentValue = INDEX_NONE,
		const FName ParentSlotName = NAME_None)
	{
		FPEPEquippedItemPublic Node;
		Node.ItemId.Value = ItemValue;
		Node.SlotTag = SlotTag;
		Node.ParentItemId.Value = ParentValue;
		Node.ParentSlotName = ParentSlotName;
		return Node;
	}

	bool IsChange(
		const FPEPEquipmentChange& Change,
		const EPEPEquipmentChangeType Type,
		const FPEPItemId ItemId,
		const FGameplayTag RootSlotTag)
	{
		return Change.ChangeType == Type
			&& Change.Node.ItemId == ItemId
			&& Change.RootSlotTag == RootSlotTag;
	}
}

using PEPEquipmentEventTest::FEventFixture;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentEventDiffOrderTest,
	"PEP.Equipment.Events.DiffIsDeterministicAndCascades",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentEventDiffOrderTest::RunTest(const FString& Parameters)
{
	using PEPEquipmentEventTest::MakeNode;
	const FGameplayTag Hand = PEPEquipmentTags::Equipment_Slot_Hand_Right;
	const FGameplayTag Back = PEPEquipmentTags::Equipment_Slot_Back;
	const FGameplayTag Head = PEPEquipmentTags::Equipment_Slot_Head;

	/* Rifle 10 with optic 11 moves hand -> back; helmet 5 stays; array order is scrambled. */
	const TArray<FPEPEquippedItemPublic> Previous {
		MakeNode(11, FGameplayTag(), 10, TEXT("optic")),
		MakeNode(5, Head),
		MakeNode(10, Hand) };
	const TArray<FPEPEquippedItemPublic> Current {
		MakeNode(5, Head),
		MakeNode(10, Back),
		MakeNode(11, FGameplayTag(), 10, TEXT("optic")) };

	TArray<FPEPEquipmentChange> Unequipped;
	TArray<FPEPEquipmentChange> Equipped;
	PEPEquipmentEvents::DiffPublicProjection(Previous, Current, Unequipped, Equipped);

	TestEqual(TEXT("the moved rifle and its optic leave"), Unequipped.Num(), 2);
	TestEqual(TEXT("the moved rifle and its optic enter"), Equipped.Num(), 2);
	if (Unequipped.Num() == 2 && Equipped.Num() == 2)
	{
		TestTrue(TEXT("the attachment leaves before its root"),
			Unequipped[0].Node.ItemId.Value == 11 && Unequipped[0].Depth == 1
			&& Unequipped[0].RootSlotTag == Hand);
		TestTrue(TEXT("the root leaves from its old slot"),
			Unequipped[1].Node.ItemId.Value == 10 && Unequipped[1].Node.SlotTag == Hand);
		TestTrue(TEXT("the root enters its new slot first"),
			Equipped[0].Node.ItemId.Value == 10 && Equipped[0].Node.SlotTag == Back);
		TestTrue(TEXT("the unchanged attachment is re-announced under the new root slot"),
			Equipped[1].Node.ItemId.Value == 11 && Equipped[1].RootSlotTag == Back);
	}
	TestFalse(TEXT("an untouched helmet is never announced"),
		Unequipped.ContainsByPredicate([](const FPEPEquipmentChange& Change)
		{
			return Change.Node.ItemId.Value == 5;
		}) || Equipped.ContainsByPredicate([](const FPEPEquipmentChange& Change)
		{
			return Change.Node.ItemId.Value == 5;
		}));

	TArray<FPEPEquippedItemPublic> Reordered = Current;
	Algo::Reverse(Reordered);
	TArray<FPEPEquipmentChange> UnequippedAgain;
	TArray<FPEPEquipmentChange> EquippedAgain;
	PEPEquipmentEvents::DiffPublicProjection(Previous, Reordered, UnequippedAgain, EquippedAgain);
	TestTrue(TEXT("array order does not change the announced order"),
		EquippedAgain.Num() == Equipped.Num()
		&& EquippedAgain.Num() == 2
		&& EquippedAgain[0].Node.ItemId == Equipped[0].Node.ItemId
		&& EquippedAgain[1].Node.ItemId == Equipped[1].Node.ItemId);

	PEPEquipmentEvents::DiffPublicProjection(Current, Reordered, UnequippedAgain, EquippedAgain);
	TestTrue(TEXT("an identical projection announces nothing"),
		UnequippedAgain.IsEmpty() && EquippedAgain.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentEventAuthorityTest,
	"PEP.Equipment.Events.RaisesTypedEventsOnceInOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentEventAuthorityTest::RunTest(const FString& Parameters)
{
	using PEPEquipmentEventTest::IsChange;
	using PEPEquipmentEventTest::Slot;
	FEventFixture Fixture;
	const FName OpticSlotName(TEXT("optic"));
	Fixture.Store->PublicSlotNames = { OpticSlotName };
	const FGameplayTag Hand = PEPEquipmentTags::Equipment_Slot_Hand_Right;
	const FGameplayTag Back = PEPEquipmentTags::Equipment_Slot_Back;

	FPEPItemInstance Rifle = Fixture.MakeItem(TEXT("EquipmentEventsRifle"));
	FPEPItemInstance Optic = Fixture.MakeItem(TEXT("EquipmentEventsOptic"));
	const FPEPItemId RifleId = Rifle.ItemId;
	const FPEPItemId OpticId = Optic.ItemId;

	TestTrue(TEXT("rifle enters the right hand"), Fixture.Add(Rifle, Slot(Hand)));
	TestTrue(TEXT("optic enters a public attachment slot"), Fixture.Add(
		Optic,
		FPEPItemLocation::MakeItemContainer(RifleId, OpticSlotName)));

	TArray<FPEPEquipmentChange>& Changes = Fixture.Listener->Changes;
	TestEqual(TEXT("one Equipped per node"), Changes.Num(), 2);
	if (Changes.Num() == 2)
	{
		TestTrue(TEXT("the root is announced in its slot"),
			IsChange(Changes[0], EPEPEquipmentChangeType::Equipped, RifleId, Hand));
		TestTrue(TEXT("the attachment is announced with its parent and root slot"),
			IsChange(Changes[1], EPEPEquipmentChangeType::Equipped, OpticId, Hand)
			&& Changes[1].Node.ParentItemId == RifleId);
	}

	/* A receive that changed nothing — or a listen server never receiving at all — is silent. */
	Changes.Reset();
	Fixture.Equipment->HandlePublicEquipmentReplicated();
	TestEqual(TEXT("re-announcing an unchanged projection raises nothing"), Changes.Num(), 0);

	int32 RequestId = 0;
	TestEqual(TEXT("moving the rifle to the back executes"),
		static_cast<int32>(Fixture.Equipment->RequestEquipItem(RifleId, Back, RequestId)),
		static_cast<int32>(EPEPRequestResult::Executed));
	TestEqual(TEXT("a slot move is two leaves then two entries"), Changes.Num(), 4);
	if (Changes.Num() == 4)
	{
		TestTrue(TEXT("the optic leaves first"),
			IsChange(Changes[0], EPEPEquipmentChangeType::Unequipped, OpticId, Hand));
		TestTrue(TEXT("the rifle leaves the hand"),
			IsChange(Changes[1], EPEPEquipmentChangeType::Unequipped, RifleId, Hand));
		TestTrue(TEXT("the rifle enters the back"),
			IsChange(Changes[2], EPEPEquipmentChangeType::Equipped, RifleId, Back));
		TestTrue(TEXT("the optic follows under the back"),
			IsChange(Changes[3], EPEPEquipmentChangeType::Equipped, OpticId, Back));
	}

	Changes.Reset();
	TestTrue(TEXT("the optic leaves the store"), Fixture.Store->RemoveItem(
		OpticId,
		FPEPItemAccessContext::System()).bSuccess);
	TestTrue(TEXT("removing the optic announces only the optic"),
		Changes.Num() == 1
		&& IsChange(Changes[0], EPEPEquipmentChangeType::Unequipped, OpticId, Back));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentUnequipTest,
	"PEP.Equipment.Request.UnequipNeedsAnExplicitTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentUnequipTest::RunTest(const FString& Parameters)
{
	using PEPEquipmentEventTest::IsChange;
	using PEPEquipmentEventTest::Slot;
	FEventFixture Fixture;
	const FGameplayTag Hand = PEPEquipmentTags::Equipment_Slot_Hand_Right;
	const FGameplayTag Back = PEPEquipmentTags::Equipment_Slot_Back;
	const FName MainSlotName(TEXT("main"));

	FPEPItemInstance Rifle = Fixture.MakeItem(TEXT("EquipmentUnequipRifle"));
	FPEPItemInstance Backpack = Fixture.MakeItem(TEXT("EquipmentUnequipBackpack"));
	const FPEPItemId RifleId = Rifle.ItemId;
	const FPEPItemId BackpackId = Backpack.ItemId;
	TestTrue(TEXT("rifle equips"), Fixture.Add(Rifle, Slot(Hand)));
	TestTrue(TEXT("backpack equips"), Fixture.Add(Backpack, Slot(Back)));
	const FPEPItemLocation IntoBackpack =
		FPEPItemLocation::MakeItemContainer(BackpackId, MainSlotName);

	int32 RequestId = 0;
	TestEqual(TEXT("an unset target is refused"),
		static_cast<int32>(Fixture.Equipment->RequestUnequipItem(
			RifleId,
			FPEPItemLocation(),
			RequestId)),
		static_cast<int32>(EPEPRequestResult::Failed));
	TestTrue(TEXT("the refusal is resolved with its request id and says why"),
		Fixture.Listener->Resolutions.Num() == 1
		&& Fixture.Listener->Resolutions[0].RequestId == RequestId
		&& Fixture.Listener->Resolutions[0].Result.ReasonTag
			== PEPCoreTags::Reason_UnsupportedMode);
	TestTrue(TEXT("an equipment slot is not an unequip target"),
		Fixture.Equipment->PreviewUnequipItem(
			RifleId,
			Slot(PEPEquipmentTags::Equipment_Slot_Belt)).ReasonTag
			== PEPCoreTags::Reason_InvalidTarget);

	TestTrue(TEXT("preview accepts a container target"),
		Fixture.Equipment->PreviewUnequipItem(RifleId, IntoBackpack).bSuccess);
	FPEPItemLocation RifleLocation;
	Fixture.Store->FindItemLocation(RifleId, RifleLocation);
	TestTrue(TEXT("preview leaves the rifle in hand"), RifleLocation == Slot(Hand));

	Fixture.Listener->Changes.Reset();
	const FPEPEquipmentRequestResult Result = Fixture.Equipment->UnequipItem(RifleId, IntoBackpack);
	TestTrue(TEXT("authority unequip moves the rifle"), Result.Result.bSuccess);
	Fixture.Store->FindItemLocation(RifleId, RifleLocation);
	TestTrue(TEXT("the rifle is now in the backpack"), RifleLocation == IntoBackpack);
	FPEPEquippedItemPublic Unused;
	TestFalse(TEXT("the rifle left the projection"),
		Fixture.Equipment->FindPublicPresentationNode(RifleId, Unused));
	TestTrue(TEXT("unequip announces the rifle leaving its slot"),
		Fixture.Listener->Changes.Num() == 1
		&& IsChange(
			Fixture.Listener->Changes[0],
			EPEPEquipmentChangeType::Unequipped,
			RifleId,
			Hand));

	TestTrue(TEXT("an item not in an equipment slot cannot be unequipped"),
		Fixture.Equipment->UnequipItem(RifleId, Slot(Hand)).Result.ReasonTag
			== PEPCoreTags::Reason_StateMismatch);

	Fixture.Listener->Resolutions.Reset();
	TestEqual(TEXT("unequipping the backpack into itself fails"),
		static_cast<int32>(Fixture.Equipment->RequestUnequipItem(
			BackpackId,
			IntoBackpack,
			RequestId)),
		static_cast<int32>(EPEPRequestResult::Failed));
	TestTrue(TEXT("a container cannot be unequipped into itself"),
		Fixture.Listener->Resolutions.Num() == 1
		&& Fixture.Listener->Resolutions[0].Result.ReasonTag == PEPCoreTags::Reason_InvalidTarget);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentEligibilityProviderTest,
	"PEP.Equipment.Eligibility.ProvidersGateEveryPathIntoASlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentEligibilityProviderTest::RunTest(const FString& Parameters)
{
	using PEPEquipmentEventTest::Slot;
	FEventFixture Fixture;
	const FGameplayTag Hand = PEPEquipmentTags::Equipment_Slot_Hand_Right;
	const FGameplayTag Belt = PEPEquipmentTags::Equipment_Slot_Belt;
	Fixture.Listener->RefusedSlotTag = Hand;
	Fixture.Listener->RefusalReasonTag = PEPCoreTags::Reason_OutOfRange;

	FPEPItemInstance Pistol = Fixture.MakeItem(TEXT("EquipmentEligibilityPistol"));
	FPEPItemInstance Spare = Fixture.MakeItem(TEXT("EquipmentEligibilitySpare"));
	const FPEPItemId PistolId = Pistol.ItemId;
	TestTrue(TEXT("an allowed slot still accepts"), Fixture.Add(Pistol, Slot(Belt)));

	const FPEPItemResult Preview = Fixture.Equipment->PreviewEquipItem(PistolId, Hand);
	TestFalse(TEXT("preview is refused by the provider"), Preview.bSuccess);
	TestTrue(TEXT("preview reports the provider's reason"),
		Preview.ReasonTag == PEPCoreTags::Reason_OutOfRange);

	int32 RequestId = 0;
	TestEqual(TEXT("the authoritative equip is refused"),
		static_cast<int32>(Fixture.Equipment->RequestEquipItem(PistolId, Hand, RequestId)),
		static_cast<int32>(EPEPRequestResult::Failed));
	TestTrue(TEXT("the refusal resolves with the provider's reason"),
		Fixture.Listener->Resolutions.Num() == 1
		&& Fixture.Listener->Resolutions[0].Result.ReasonTag == PEPCoreTags::Reason_OutOfRange);
	FPEPItemLocation PistolLocation;
	Fixture.Store->FindItemLocation(PistolId, PistolLocation);
	TestTrue(TEXT("the pistol stayed on the belt"), PistolLocation == Slot(Belt));

	/* An inventory UI writes to the store directly; the registered validator is what stops it. */
	TestFalse(TEXT("a direct store move into the slot is refused"), Fixture.Store->MoveItem(
		PistolId,
		Slot(Hand),
		FPEPItemAccessContext::System()).bSuccess);
	TestFalse(TEXT("a direct store add into the slot is refused"), Fixture.Add(Spare, Slot(Hand)));

	Fixture.Listener->RefusedSlotTag = FGameplayTag();
	const int32 QueriesBefore = Fixture.Listener->EligibilityQueries;
	TestEqual(TEXT("once allowed, the equip executes"),
		static_cast<int32>(Fixture.Equipment->RequestEquipItem(PistolId, Hand, RequestId)),
		static_cast<int32>(EPEPRequestResult::Executed));
	TestTrue(TEXT("the provider was consulted for the allowed equip"),
		Fixture.Listener->EligibilityQueries > QueriesBefore);
	return true;
}

#endif
