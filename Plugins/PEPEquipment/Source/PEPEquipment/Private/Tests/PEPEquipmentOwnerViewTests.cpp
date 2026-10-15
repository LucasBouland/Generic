#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/PEPEquipmentComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Equipment/PEPEquipmentEventTypes.h"
#include "Equipment/PEPEquipmentPresentationFragment.h"
#include "Equipment/PEPEquipmentProfile.h"
#include "Equipment/PEPEquipmentTags.h"
#include "GameFramework/Actor.h"
#include "Item/PEPItemAccess.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemInstance.h"
#include "StructUtils/InstancedStruct.h"
#include "Tests/PEPTestEquipmentListenerComponent.h"
#include "Tests/PEPTestItemStoreComponent.h"

namespace PEPEquipmentOwnerViewTest
{
	const TCHAR* const OpticSlotName = TEXT("optic");
	const TCHAR* const ContentSlotName = TEXT("content");

	/**
	 * A profile with one slot hidden from the projection (the belt) between two public hands, and
	 * a listener recording public and owner events apart.
	 */
	struct FOwnerViewFixture
	{
		UWorld* World = nullptr;
		AActor* Owner = nullptr;
		UPEPTestItemStoreComponent* Store = nullptr;
		UPEPEquipmentComponent* Equipment = nullptr;
		UPEPTestEquipmentListenerComponent* Listener = nullptr;
		TArray<TObjectPtr<UPEPItemDefinition>> Definitions;

		FOwnerViewFixture()
		{
			UPEPEquipmentProfile* Profile = NewObject<UPEPEquipmentProfile>(GetTransientPackage());
			Profile->Slots.AddDefaulted_GetRef().SlotTag = PEPEquipmentTags::Equipment_Slot_Hand_Right;
			FPEPEquipmentSlotDefinition& Belt = Profile->Slots.AddDefaulted_GetRef();
			Belt.SlotTag = PEPEquipmentTags::Equipment_Slot_Belt;
			Belt.bShowPublicly = false;
			Profile->Slots.AddDefaulted_GetRef().SlotTag = PEPEquipmentTags::Equipment_Slot_Hand_Left;

			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());

			Owner = World->SpawnActor<AActor>();
			Store = NewObject<UPEPTestItemStoreComponent>(Owner);
			Store->PublicSlotNames = { FName(OpticSlotName) };
			Store->RegisterComponent();

			Equipment = NewObject<UPEPEquipmentComponent>(Owner);
			Equipment->SetLocalPresentationEnabled(false);
			Equipment->SetEquipmentProfile(Profile);
			Equipment->RegisterComponent();

			Listener = NewObject<UPEPTestEquipmentListenerComponent>(Owner);
			Listener->RegisterComponent();
			Listener->Listen(Equipment);
			Listener->ListenToOwnerView(Equipment);
			World->BeginPlay();
			/* The fixture world has no game mode: nothing begins play unless told to. */
			Owner->DispatchBeginPlay();
		}

		~FOwnerViewFixture()
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

		/** A direct store write, left for the next tick's rebuild like any other. */
		bool Write(const FPEPItemInstance& Item, const FPEPItemLocation& Location)
		{
			return Store->AddItem(Item, Location, FPEPItemAccessContext::System()).bSuccess;
		}

		void Flush()
		{
			Equipment->FlushDeferredUpdates();
		}
	};

	bool Contains(const TArray<FPEPEquippedItemInfo>& Nodes, const FPEPItemId ItemId)
	{
		return Nodes.ContainsByPredicate([ItemId](const FPEPEquippedItemInfo& Node)
		{
			return Node.ItemId == ItemId;
		});
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

using PEPEquipmentOwnerViewTest::FOwnerViewFixture;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentOwnerViewHiddenSlotTest,
	"PEP.Equipment.OwnerView.IncludesHiddenSlotsThePublicProjectionLeavesOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentOwnerViewHiddenSlotTest::RunTest(const FString& Parameters)
{
	using PEPEquipmentOwnerViewTest::Contains;
	using PEPEquipmentOwnerViewTest::IsChange;
	FOwnerViewFixture Fixture;
	const FGameplayTag Hand = PEPEquipmentTags::Equipment_Slot_Hand_Right;
	const FGameplayTag Belt = PEPEquipmentTags::Equipment_Slot_Belt;

	FPEPItemInstance Rifle = Fixture.MakeItem(TEXT("EquipmentOwnerViewRifle"));
	FPEPItemInstance Pistol = Fixture.MakeItem(TEXT("EquipmentOwnerViewPistol"));
	FPEPItemInstance Optic = Fixture.MakeItem(TEXT("EquipmentOwnerViewOptic"));
	FPEPItemInstance Ammo = Fixture.MakeItem(TEXT("EquipmentOwnerViewAmmo"));
	const FPEPItemId RifleId = Rifle.ItemId;
	const FPEPItemId PistolId = Pistol.ItemId;
	const FPEPItemId OpticId = Optic.ItemId;
	const FPEPItemId AmmoId = Ammo.ItemId;

	TestTrue(TEXT("this machine keeps an owner view: it is the authority"),
		Fixture.Equipment->HasOwnerEquipmentView());

	/* Four direct writes, then one tick's worth of rebuild. */
	TestTrue(TEXT("the rifle enters the public hand"),
		Fixture.Write(Rifle, FPEPItemLocation::MakeNamedSlot(Hand)));
	TestTrue(TEXT("the pistol enters the hidden belt"),
		Fixture.Write(Pistol, FPEPItemLocation::MakeNamedSlot(Belt)));
	TestTrue(TEXT("an optic is attached to the pistol"), Fixture.Write(
		Optic,
		FPEPItemLocation::MakeItemContainer(
			PistolId,
			FName(PEPEquipmentOwnerViewTest::OpticSlotName))));
	TestTrue(TEXT("ammo goes into the pistol's private content"), Fixture.Write(
		Ammo,
		FPEPItemLocation::MakeItemContainer(
			PistolId,
			FName(PEPEquipmentOwnerViewTest::ContentSlotName))));
	TestTrue(TEXT("the owner view waits for the next tick like the projection"),
		Fixture.Equipment->GetOwnerEquipmentNodes().IsEmpty()
		&& Fixture.Listener->OwnerChanges.IsEmpty());

	Fixture.Flush();
	const TArray<FPEPEquippedItemInfo> PublicNodes = Fixture.Equipment->GetPublicPresentationNodes();
	const TArray<FPEPEquippedItemInfo> OwnerNodes = Fixture.Equipment->GetOwnerEquipmentNodes();
	TestTrue(TEXT("the public projection holds only the rifle"),
		PublicNodes.Num() == 1 && Contains(PublicNodes, RifleId));
	TestEqual(TEXT("the owner view holds the rifle, the pistol and its optic"), OwnerNodes.Num(), 3);
	TestTrue(TEXT("roots come in profile order, each followed by its attachments"),
		OwnerNodes.Num() == 3
		&& OwnerNodes[0].ItemId == RifleId
		&& OwnerNodes[1].ItemId == PistolId && OwnerNodes[1].SlotTag == Belt
		&& OwnerNodes[2].ItemId == OpticId && OwnerNodes[2].ParentItemId == PistolId);
	TestFalse(TEXT("private content stays out of the owner view too"), Contains(OwnerNodes, AmmoId));

	FPEPEquippedItemInfo Node;
	TestFalse(TEXT("the hidden pistol is not a public node"),
		Fixture.Equipment->FindPublicPresentationNode(PistolId, Node));
	TestTrue(TEXT("but is an owner node"),
		Fixture.Equipment->FindOwnerEquipmentNode(PistolId, Node) && Node.SlotTag == Belt);
	TestTrue(TEXT("the slot lookup reads the owner view here"),
		Fixture.Equipment->FindEquippedRootInSlot(Belt, Node) && Node.ItemId == PistolId);

	const TArray<FPEPEquipmentChange>& Public = Fixture.Listener->Changes;
	const TArray<FPEPEquipmentChange>& Owned = Fixture.Listener->OwnerChanges;
	TestTrue(TEXT("one rebuild announces only the rifle publicly"),
		Public.Num() == 1 && IsChange(Public[0], EPEPEquipmentChangeType::Equipped, RifleId, Hand));
	TestEqual(TEXT("and all three nodes to the owner"), Owned.Num(), 3);
	if (Owned.Num() == 3)
	{
		TestTrue(TEXT("roots before attachments"),
			Owned[0].Depth == 0 && Owned[1].Depth == 0
			&& (Owned[0].Node.ItemId == PistolId || Owned[1].Node.ItemId == PistolId));
		TestTrue(TEXT("the optic follows under the hidden root slot"),
			IsChange(Owned[2], EPEPEquipmentChangeType::Equipped, OpticId, Belt)
			&& Owned[2].Depth == 1);
	}
	TestEqual(TEXT("the owner view changed once"), Fixture.Listener->OwnerViewChangedCount, 1);

	/* A state change in place moves neither view. */
	Fixture.Listener->Changes.Reset();
	Fixture.Listener->OwnerChanges.Reset();
	TestTrue(TEXT("the pistol's state ticks"), Fixture.Store->MutateItem(
		PistolId,
		[](FPEPItemInstance&)
		{
			return true;
		},
		FPEPItemAccessContext::System()).bSuccess);
	Fixture.Flush();
	TestTrue(TEXT("a state change on a hidden item is silent"),
		Fixture.Listener->Changes.IsEmpty() && Fixture.Listener->OwnerChanges.IsEmpty());

	/* An equip request this component executes rebuilds both views before returning. */
	int32 RequestId = 0;
	TestEqual(TEXT("the rifle moves into the hidden belt, displacing the pistol to the hand"),
		static_cast<int32>(Fixture.Equipment->RequestEquipItem(RifleId, Belt, RequestId)),
		static_cast<int32>(EPEPRequestResult::Executed));
	TestTrue(TEXT("publicly, the rifle leaves the hand and the pistol arrives with its optic"),
		Public.Num() == 3
		&& IsChange(Public[0], EPEPEquipmentChangeType::Unequipped, RifleId, Hand)
		&& IsChange(Public[1], EPEPEquipmentChangeType::Equipped, PistolId, Hand)
		&& IsChange(Public[2], EPEPEquipmentChangeType::Equipped, OpticId, Hand));
	TestEqual(TEXT("the owner sees every node move: three leave, three enter"), Owned.Num(), 6);
	if (Owned.Num() == 6)
	{
		TestTrue(TEXT("the owner's Unequipped come first, the optic before its root"),
			IsChange(Owned[0], EPEPEquipmentChangeType::Unequipped, OpticId, Belt)
			&& Owned[1].ChangeType == EPEPEquipmentChangeType::Unequipped
			&& Owned[2].ChangeType == EPEPEquipmentChangeType::Unequipped);
		TestTrue(TEXT("then the Equipped, the optic last under the hand"),
			Owned[3].ChangeType == EPEPEquipmentChangeType::Equipped
			&& Owned[4].ChangeType == EPEPEquipmentChangeType::Equipped
			&& IsChange(Owned[5], EPEPEquipmentChangeType::Equipped, OpticId, Hand));
	}
	TestTrue(TEXT("the rifle is now hidden from the projection"),
		!Fixture.Equipment->FindPublicPresentationNode(RifleId, Node)
		&& Fixture.Equipment->FindOwnerEquipmentNode(RifleId, Node) && Node.SlotTag == Belt);
	return true;
}

#endif
