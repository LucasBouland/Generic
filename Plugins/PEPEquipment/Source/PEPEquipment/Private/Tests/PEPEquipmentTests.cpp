#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/PEPEquipmentComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Equipment/PEPEquippedPresentationActor.h"
#include "Equipment/PEPEquipmentPresentationFragment.h"
#include "Equipment/PEPEquipmentProfile.h"
#include "Equipment/PEPEquipmentPresentationTypes.h"
#include "Equipment/PEPPresentationTargetDefinition.h"
#include "Equipment/PEPEquipmentTags.h"
#include "GameFramework/Actor.h"
#include "Item/PEPItemAccess.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemInstance.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "StructUtils/InstancedStruct.h"
#include "Tests/PEPTestItemStoreComponent.h"

namespace PEPEquipmentTest
{
	struct FFixture
	{
		UWorld* World = nullptr;
		AActor* Owner = nullptr;
		UPEPTestItemStoreComponent* Store = nullptr;
		UPEPEquipmentComponent* Equipment = nullptr;
		TArray<TObjectPtr<UPEPItemDefinition>> Definitions;

		explicit FFixture(
			const bool bEnablePresentation = false,
			UPEPEquipmentProfile* EquipmentProfile = nullptr)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());

			Owner = World->SpawnActor<AActor>();
			Store = NewObject<UPEPTestItemStoreComponent>(Owner);
			Store->RegisterComponent();

			Equipment = NewObject<UPEPEquipmentComponent>(Owner);
			Equipment->SetLocalPresentationEnabled(bEnablePresentation);
			Equipment->SetEquipmentProfile(EquipmentProfile);
			Equipment->RegisterComponent();
			World->BeginPlay();
		}

		~FFixture()
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

		UPEPItemDefinition* MakeDefinition(
			const FName Name,
			const FPEPEquipmentPresentationFragment& Presentation,
			const TArray<FInstancedStruct>& ExtraFragments = {})
		{
			UPEPItemDefinition* Definition = NewObject<UPEPItemDefinition>(GetTransientPackage(), Name);
			Definition->DefinitionFragments.Add(FInstancedStruct::Make(Presentation));
			Definition->DefinitionFragments.Append(ExtraFragments);
			PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(Definition);
			Definitions.Add(Definition);
			return Definition;
		}
	};

	FPEPItemLocation HandLocation(const EPEPEquipmentHand Hand)
	{
		return FPEPItemLocation::MakeNamedSlot(
			Hand == EPEPEquipmentHand::Left
				? PEPEquipmentTags::Equipment_Slot_Hand_Left
				: PEPEquipmentTags::Equipment_Slot_Hand_Right);
	}
}

using PEPEquipmentTest::FFixture;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentDualHandsTest,
	"PEP.Equipment.PublicProjection.SupportsIndependentDualHands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentDualHandsTest::RunTest(const FString& Parameters)
{
	FFixture Fixture;

	FPEPEquipmentPresentationFragment LampPresentation;
	UPEPItemDefinition* LampDefinition = Fixture.MakeDefinition(
		TEXT("EquipmentDualHandsLamp"),
		LampPresentation);

	FPEPEquipmentPresentationFragment LaserPresentation;
	UPEPItemDefinition* LaserDefinition = Fixture.MakeDefinition(
		TEXT("EquipmentDualHandsLaser"),
		LaserPresentation);

	FPEPItemInstance LeftItem = PEPItemAutomationTestSupport::MakeItemForTests(LampDefinition);
	FPEPItemInstance RightItem = PEPItemAutomationTestSupport::MakeItemForTests(LaserDefinition);
	const FPEPItemId LeftId = LeftItem.ItemId;
	const FPEPItemId RightId = RightItem.ItemId;

	TestTrue(TEXT("left hand accepts its item"), Fixture.Store->AddItem(
		LeftItem,
		PEPEquipmentTest::HandLocation(EPEPEquipmentHand::Left),
		FPEPItemAccessContext::System()).bSuccess);
	TestTrue(TEXT("right hand accepts a different item"), Fixture.Store->AddItem(
		RightItem,
		PEPEquipmentTest::HandLocation(EPEPEquipmentHand::Right),
		FPEPItemAccessContext::System()).bSuccess);

	FPEPEquippedItemPublic LeftPublic;
	FPEPEquippedItemPublic RightPublic;
	TestTrue(TEXT("left hand is active"),
		Fixture.Equipment->GetActiveHandItem(EPEPEquipmentHand::Left, LeftPublic));
	TestTrue(TEXT("right hand is active"),
		Fixture.Equipment->GetActiveHandItem(EPEPEquipmentHand::Right, RightPublic));
	TestTrue(TEXT("the hands expose two different item identities"),
		LeftPublic.ItemId == LeftId && RightPublic.ItemId == RightId && LeftId != RightId);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentPresentationSeparationTest,
	"PEP.Equipment.Presentation.FragmentIsVisualOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentPresentationSeparationTest::RunTest(const FString& Parameters)
{
	UScriptStruct* EquipmentPresentation = FPEPEquipmentPresentationFragment::StaticStruct();
	TestNull(TEXT("equipment presentation does not select an actor class"),
		EquipmentPresentation->FindPropertyByName(TEXT("PresentationActorClass")));
	TestNull(TEXT("equipment presentation does not own fire VFX"),
		EquipmentPresentation->FindPropertyByName(TEXT("FireVFX")));
	TestNull(TEXT("equipment presentation does not own light support"),
		EquipmentPresentation->FindPropertyByName(TEXT("bSupportsLight")));
	TestNull(TEXT("equipment presentation does not own laser support"),
		EquipmentPresentation->FindPropertyByName(TEXT("bSupportsLaser")));

	FPEPEquipmentPresentationFragment Presentation;
	Presentation.Visual = FInstancedStruct::Make(FPEPNoEquipmentVisual());
	TestTrue(TEXT("equipment presentation uses a typed visual family"),
		Presentation.Visual.GetScriptStruct()->IsChildOf(FPEPEquipmentVisual::StaticStruct()));
	TestTrue(TEXT("No Visual is explicit rather than an absent mesh"),
		Presentation.IsExplicitlyInvisible());
	TestEqual(TEXT("an attachment slot uses its own name as the default socket"),
		Presentation.ResolveAttachmentSocket(TEXT("optic")),
		FName(TEXT("optic")));
	FPEPEquipmentAttachmentSocketOverride& SocketOverride =
		Presentation.AttachmentSocketOverrides.AddDefaulted_GetRef();
	SocketOverride.SlotName = TEXT("optic");
	SocketOverride.SocketName = TEXT("rail_top_01");
	TestEqual(TEXT("equipment presentation may override an exceptional socket name"),
		Presentation.ResolveAttachmentSocket(TEXT("optic")),
		FName(TEXT("rail_top_01")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentContentPrivacyTest,
	"PEP.Equipment.PublicProjection.DoesNotExposeContainerContents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentContentPrivacyTest::RunTest(const FString& Parameters)
{
	FFixture Fixture;

	FPEPEquipmentPresentationFragment BagPresentation;
	UPEPItemDefinition* BagDefinition = Fixture.MakeDefinition(
		TEXT("EquipmentPrivacyBag"),
		BagPresentation);
	UPEPItemDefinition* ContentDefinition = Fixture.MakeDefinition(
		TEXT("EquipmentPrivacyContent"),
		FPEPEquipmentPresentationFragment());

	FPEPItemInstance Bag = PEPItemAutomationTestSupport::MakeItemForTests(BagDefinition);
	FPEPItemInstance Content = PEPItemAutomationTestSupport::MakeItemForTests(ContentDefinition);
	const FPEPItemId BagId = Bag.ItemId;
	const FPEPItemId ContentId = Content.ItemId;

	TestTrue(TEXT("bag equips on the back"), Fixture.Store->AddItem(
		Bag,
		FPEPItemLocation::MakeNamedSlot(PEPEquipmentTags::Equipment_Slot_Back),
		FPEPItemAccessContext::System()).bSuccess);
	TestTrue(TEXT("content enters the equipped bag"), Fixture.Store->AddItem(
		Content,
		FPEPItemLocation::MakeItemContainer(BagId, TEXT("content")),
		FPEPItemAccessContext::System()).bSuccess);

	const TArray<FPEPEquippedItemPublic> PublicEntries =
		Fixture.Equipment->GetPublicPresentationNodes();
	TestEqual(TEXT("only the equipped bag is publicly projected"), PublicEntries.Num(), 1);
	TestTrue(TEXT("the public identity is the bag"),
		PublicEntries.Num() == 1 && PublicEntries[0].ItemId == BagId);
	TestFalse(TEXT("the child item is absent from the public projection"),
		PublicEntries.ContainsByPredicate([ContentId](const FPEPEquippedItemPublic& Entry)
		{
			return Entry.ItemId == ContentId;
		}));

	TestNull(TEXT("the public struct has no full Item field"),
		FPEPEquippedItemPublic::StaticStruct()->FindPropertyByName(TEXT("Item")));
	TestNull(TEXT("the public struct has no state fragment field"),
		FPEPEquippedItemPublic::StaticStruct()->FindPropertyByName(TEXT("StateFragments")));
	TestNull(TEXT("the public struct has no child/content field"),
		FPEPEquippedItemPublic::StaticStruct()->FindPropertyByName(TEXT("Contents")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentVisibleAttachmentTreeTest,
	"PEP.Equipment.PublicProjection.ProjectsOnlyAttachmentSlotsRecursively",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentVisibleAttachmentTreeTest::RunTest(const FString& Parameters)
{
	FFixture Fixture;

	/* Which container slots are public is the store's knowledge; this store declares it by name. */
	const FName OpticSlotName(TEXT("optic"));
	const FName ContentSlotName(TEXT("content"));
	const FName CoverSlotName(TEXT("cover"));
	const FName HiddenAttachmentSlotName(TEXT("hidden_attachment"));
	Fixture.Store->PublicSlotNames = { OpticSlotName, CoverSlotName, HiddenAttachmentSlotName };

	UPEPItemDefinition* WeaponDefinition = Fixture.MakeDefinition(
		TEXT("EquipmentVisibleTreeWeapon"),
		FPEPEquipmentPresentationFragment());
	UPEPItemDefinition* OpticDefinition = Fixture.MakeDefinition(
		TEXT("EquipmentVisibleTreeOptic"),
		FPEPEquipmentPresentationFragment());
	UPEPItemDefinition* CoverDefinition = Fixture.MakeDefinition(
		TEXT("EquipmentVisibleTreeCover"),
		FPEPEquipmentPresentationFragment());
	UPEPItemDefinition* PrivateContentDefinition = Fixture.MakeDefinition(
		TEXT("EquipmentVisibleTreePrivateContent"),
		FPEPEquipmentPresentationFragment());
	UPEPItemDefinition* HiddenAttachmentDefinition = Fixture.MakeDefinition(
		TEXT("EquipmentVisibleTreeHiddenAttachment"),
		FPEPEquipmentPresentationFragment());

	FPEPItemInstance Weapon = PEPItemAutomationTestSupport::MakeItemForTests(WeaponDefinition);
	FPEPItemInstance Optic = PEPItemAutomationTestSupport::MakeItemForTests(OpticDefinition);
	FPEPItemInstance Cover = PEPItemAutomationTestSupport::MakeItemForTests(CoverDefinition);
	FPEPItemInstance PrivateContent = PEPItemAutomationTestSupport::MakeItemForTests(PrivateContentDefinition);
	FPEPItemInstance HiddenAttachment = PEPItemAutomationTestSupport::MakeItemForTests(HiddenAttachmentDefinition);
	const FPEPItemId WeaponId = Weapon.ItemId;
	const FPEPItemId OpticId = Optic.ItemId;
	const FPEPItemId CoverId = Cover.ItemId;
	const FPEPItemId PrivateContentId = PrivateContent.ItemId;
	const FPEPItemId HiddenAttachmentId = HiddenAttachment.ItemId;

	TestTrue(TEXT("weapon equips as the public root"), Fixture.Store->AddItem(
		Weapon,
		PEPEquipmentTest::HandLocation(EPEPEquipmentHand::Right),
		FPEPItemAccessContext::System()).bSuccess);
	TestTrue(TEXT("optic enters an attachment slot"), Fixture.Store->AddItem(
		Optic,
		FPEPItemLocation::MakeItemContainer(WeaponId, OpticSlotName),
		FPEPItemAccessContext::System()).bSuccess);
	TestTrue(TEXT("cover enters a nested attachment slot"), Fixture.Store->AddItem(
		Cover,
		FPEPItemLocation::MakeItemContainer(OpticId, CoverSlotName),
		FPEPItemAccessContext::System()).bSuccess);
	TestTrue(TEXT("private item enters a content slot"), Fixture.Store->AddItem(
		PrivateContent,
		FPEPItemLocation::MakeItemContainer(WeaponId, ContentSlotName),
		FPEPItemAccessContext::System()).bSuccess);
	TestTrue(TEXT("an attachment may exist inside the private content"), Fixture.Store->AddItem(
		HiddenAttachment,
		FPEPItemLocation::MakeItemContainer(
			PrivateContentId,
			HiddenAttachmentSlotName),
		FPEPItemAccessContext::System()).bSuccess);

	const TArray<FPEPEquippedItemPublic> PublicEntries =
		Fixture.Equipment->GetPublicPresentationNodes();
	TestEqual(TEXT("root and two recursive attachments are public"), PublicEntries.Num(), 3);
	TestFalse(TEXT("content remains absent"), PublicEntries.ContainsByPredicate(
		[PrivateContentId](const FPEPEquippedItemPublic& Entry)
		{
			return Entry.ItemId == PrivateContentId;
		}));
	TestFalse(TEXT("an attachment below private content also remains absent"),
		PublicEntries.ContainsByPredicate(
			[HiddenAttachmentId](const FPEPEquippedItemPublic& Entry)
			{
				return Entry.ItemId == HiddenAttachmentId;
			}));

	const FPEPEquippedItemPublic* PublicOptic = PublicEntries.FindByPredicate(
		[OpticId](const FPEPEquippedItemPublic& Entry)
		{
			return Entry.ItemId == OpticId;
		});
	const FPEPEquippedItemPublic* PublicCover = PublicEntries.FindByPredicate(
		[CoverId](const FPEPEquippedItemPublic& Entry)
		{
			return Entry.ItemId == CoverId;
		});
	TestTrue(TEXT("optic keeps its public parent and slot"),
		PublicOptic
			&& PublicOptic->ParentItemId == WeaponId
			&& PublicOptic->ParentSlotName == OpticSlotName);
	TestTrue(TEXT("nested cover keeps its public parent and slot"),
		PublicCover
			&& PublicCover->ParentItemId == OpticId
			&& PublicCover->ParentSlotName == CoverSlotName);
	TestEqual(TEXT("only the weapon occupies an Equipment slot"),
		Fixture.Equipment->GetPublicEquipmentRoots().Num(), 1);
	TestEqual(TEXT("recursive attachment query returns optic and cover"),
		Fixture.Equipment->GetPublicAttachments(WeaponId, true).Num(), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentModularPresentationTest,
	"PEP.Equipment.Presentation.AppliesAndRestoresModularMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentModularPresentationTest::RunTest(const FString& Parameters)
{
	UPEPEquipmentProfile* Profile = NewObject<UPEPEquipmentProfile>(GetTransientPackage());
	FPEPPresentationTargetDefinition& HeadTarget =
		Profile->PresentationTargets.AddDefaulted_GetRef();
	HeadTarget.TargetTag = PEPEquipmentTags::Equipment_PresentationTarget_Body_Head;
	FPEPEquipmentSlotDefinition& HeadSlot = Profile->Slots.AddDefaulted_GetRef();
	HeadSlot.SlotTag = PEPEquipmentTags::Equipment_Slot_Body_Head;
	HeadSlot.PresentationPolicy = EPEPEquipmentPresentationPolicy::ModularSkeletal;
	HeadSlot.PresentationTargetTag =
		PEPEquipmentTags::Equipment_PresentationTarget_Body_Head;
	FFixture Fixture(true, Profile);

	USkeletalMeshComponent* Target = NewObject<USkeletalMeshComponent>(Fixture.Owner);
	USkeletalMesh* BaseMesh = NewObject<USkeletalMesh>(GetTransientPackage());
	USkeletalMesh* EquippedMesh = NewObject<USkeletalMesh>(GetTransientPackage());
	Target->SetSkeletalMesh(BaseMesh, false);
	Target->RegisterComponent();
	TestTrue(TEXT("modular target registers"), Fixture.Equipment->RegisterPresentationTarget(
		PEPEquipmentTags::Equipment_PresentationTarget_Body_Head,
		Target));

	FPEPSkeletalMeshEquipmentVisual Visual;
	Visual.Mesh = EquippedMesh;
	FPEPEquipmentPresentationFragment Presentation;
	Presentation.Visual = FInstancedStruct::Make(Visual);
	UPEPItemDefinition* Definition = Fixture.MakeDefinition(
		TEXT("EquipmentModularHead"), Presentation);
	FPEPItemInstance Item = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	const FPEPItemId ItemId = Item.ItemId;

	TestTrue(TEXT("modular item equips"), Fixture.Store->AddItem(
		Item,
		FPEPItemLocation::MakeNamedSlot(PEPEquipmentTags::Equipment_Slot_Body_Head),
		FPEPItemAccessContext::System()).bSuccess);
	TestTrue(TEXT("equipment applies the skeletal override"),
		Target->GetSkeletalMeshAsset() == EquippedMesh);

	FPEPLocalPresentationTarget LocalTarget;
	TestTrue(TEXT("modular equipment exposes a generic local target"),
		Fixture.Equipment->FindLocalPresentationTarget(ItemId, LocalTarget));
	TestEqual(TEXT("generic target reports modular policy"),
		LocalTarget.Policy, EPEPEquipmentPresentationPolicy::ModularSkeletal);
	TestTrue(TEXT("modular target supplies its component for sockets"),
		LocalTarget.SocketProvider.Get() == Target);
	TestNull(TEXT("modular presentation does not require an actor"),
		Fixture.Equipment->FindLocalPresentationActor(ItemId));

	TestTrue(TEXT("modular item can be removed"), Fixture.Store->RemoveItem(
		ItemId,
		FPEPItemAccessContext::System()).bSuccess);
	TestTrue(TEXT("removing equipment restores the registered base mesh"),
		Target->GetSkeletalMeshAsset() == BaseMesh);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentActorMeshVisualTest,
	"PEP.Equipment.Presentation.ActorVisualProvidesNestedMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentActorMeshVisualTest::RunTest(const FString& Parameters)
{
	UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage());
	FPEPStaticMeshEquipmentVisual MeshVisual;
	MeshVisual.Mesh = Mesh;

	FPEPActorEquipmentVisual ActorVisual;
	ActorVisual.ActorClass = APEPEquippedPresentationActor::StaticClass();
	ActorVisual.MeshVisual = FInstancedStruct::Make(MeshVisual);

	FPEPEquipmentPresentationFragment Presentation;
	Presentation.Visual = FInstancedStruct::Make(ActorVisual);

	const TSoftObjectPtr<UStaticMesh>* ResolvedMesh = Presentation.GetStaticMeshVisual();
	TestNotNull(TEXT("actor visual exposes its nested static mesh"), ResolvedMesh);
	TestTrue(TEXT("actor visual resolves the selected mesh"),
		ResolvedMesh && ResolvedMesh->Get() == Mesh);
	TestNull(TEXT("actor static mesh does not resolve as skeletal"),
		Presentation.GetSkeletalMeshVisual());
	TestNotNull(TEXT("actor visual keeps its carrier class"),
		Presentation.GetActorClassVisual());
	TestTrue(TEXT("actor visual is considered present"), Presentation.HasVisual());
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentProfileTargetSchemaTest,
	"PEP.Equipment.Profile.ValidatesPresentationTargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentProfileTargetSchemaTest::RunTest(const FString& Parameters)
{
	UPEPEquipmentProfile* ValidProfile =
		NewObject<UPEPEquipmentProfile>(GetTransientPackage());
	FPEPPresentationTargetDefinition& Target =
		ValidProfile->PresentationTargets.AddDefaulted_GetRef();
	Target.TargetTag = PEPEquipmentTags::Equipment_PresentationTarget_Body_Head;
	FPEPEquipmentSlotDefinition& Slot = ValidProfile->Slots.AddDefaulted_GetRef();
	Slot.SlotTag = PEPEquipmentTags::Equipment_Slot_Body_Head;
	Slot.PresentationPolicy = EPEPEquipmentPresentationPolicy::ModularSkeletal;
	Slot.PresentationTargetTag = Target.TargetTag;

	TestNotNull(TEXT("profile resolves a declared presentation target"),
		ValidProfile->FindPresentationTarget(Target.TargetTag));
	FDataValidationContext ValidContext;
	TestTrue(TEXT("a declared modular target keeps the profile valid"),
		ValidProfile->IsDataValid(ValidContext) != EDataValidationResult::Invalid);
	TestEqual(TEXT("an optional missing skeleton emits one designer warning"),
		ValidContext.GetNumWarnings(), 1u);

	UPEPEquipmentProfile* InvalidProfile =
		NewObject<UPEPEquipmentProfile>(GetTransientPackage());
	FPEPEquipmentSlotDefinition& InvalidSlot = InvalidProfile->Slots.AddDefaulted_GetRef();
	InvalidSlot.SlotTag = PEPEquipmentTags::Equipment_Slot_Body_Head;
	InvalidSlot.PresentationPolicy = EPEPEquipmentPresentationPolicy::ModularSkeletal;
	InvalidSlot.PresentationTargetTag =
		PEPEquipmentTags::Equipment_PresentationTarget_Body_Head;
	FDataValidationContext InvalidContext;
	TestEqual(TEXT("a slot cannot reference an undeclared presentation target"),
		InvalidProfile->IsDataValid(InvalidContext), EDataValidationResult::Invalid);
	TestTrue(TEXT("the undeclared target reports a validation error"),
		InvalidContext.GetNumErrors() > 0);
	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentPresentationMeshComponentTagsTest,
	"PEP.Equipment.Presentation.AppliesMeshComponentTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentPresentationMeshComponentTagsTest::RunTest(const FString& Parameters)
{
	UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage());
	FPEPStaticMeshEquipmentVisual MeshVisual;
	MeshVisual.Mesh = Mesh;

	FPEPEquipmentPresentationFragment Presentation;
	Presentation.Visual = FInstancedStruct::Make(MeshVisual);
	Presentation.MeshComponentTags.Add(TEXT("MeleeWeapon"));

	FFixture Fixture(true);
	USceneComponent* PresentationRoot = NewObject<USceneComponent>(Fixture.Owner);
	Fixture.Owner->SetRootComponent(PresentationRoot);
	PresentationRoot->RegisterComponent();
	UPEPItemDefinition* Definition = Fixture.MakeDefinition(
		TEXT("EquipmentTaggedPresentation"), Presentation);
	FPEPItemInstance Item = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	const FPEPItemId ItemId = Item.ItemId;

	TestTrue(TEXT("tagged item equips"), Fixture.Store->AddItem(
		Item,
		FPEPItemLocation::MakeNamedSlot(PEPEquipmentTags::Equipment_Slot_Primary),
		FPEPItemAccessContext::System()).bSuccess);

	const APEPEquippedPresentationActor* Actor =
		Fixture.Equipment->FindLocalPresentationActor(ItemId);
	TestNotNull(TEXT("tagged item has a local presentation"), Actor);
	const USceneComponent* MeshComponent = Actor
		? Actor->GetPresentationAttachComponent()
		: nullptr;
	TestTrue(TEXT("the active presentation mesh receives its configured component tag"),
		MeshComponent && MeshComponent->ComponentHasTag(TEXT("MeleeWeapon")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentGenericStoreTest,
	"PEP.Equipment.Store.UsesGenericItemStoreContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentGenericStoreTest::RunTest(const FString& Parameters)
{
	FFixture Fixture;
	TestTrue(TEXT("equipment discovers a native item store without inventory knowledge"),
		Fixture.Equipment->GetItemStoreObject() == Fixture.Store);
	UPEPItemDefinition* Definition = Fixture.MakeDefinition(
		TEXT("EquipmentGenericStore"),
		FPEPEquipmentPresentationFragment());
	FPEPItemInstance Item = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	const FPEPItemId ItemId = Item.ItemId;
	FPEPItemInstance PreviousHandItem = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	const FPEPItemId PreviousHandItemId = PreviousHandItem.ItemId;

	/*
	 * Equipment registers its slots as open to automatic placement. A store with nothing equipped
	 * owns no container grid, so an equipment slot is the only destination a first pickup can have;
	 * what keeps the wrong item out of a slot is that slot's item tags, not a refusal to be filled
	 * automatically. A slot that must only ever be filled deliberately opts out individually.
	 */
	TestTrue(TEXT("equipment slots accept automatic placement"),
		Fixture.Store->DoesLocationAllowAutoPlacement(
			PEPEquipmentTags::Equipment_Slot_Hand_Right));
	TestTrue(TEXT("the generic store accepts a source slot item"), Fixture.Store->AddItem(
		Item,
		FPEPItemLocation::MakeNamedSlot(PEPEquipmentTags::Equipment_Slot_Belt),
		FPEPItemAccessContext::System()).bSuccess);
	TestTrue(TEXT("the target hand starts occupied"), Fixture.Store->AddItem(
		PreviousHandItem,
		FPEPItemLocation::MakeNamedSlot(PEPEquipmentTags::Equipment_Slot_Hand_Right),
		FPEPItemAccessContext::System()).bSuccess);
	TestTrue(TEXT("preview validates without moving the item"),
		Fixture.Equipment->PreviewEquipItem(
			ItemId,
			PEPEquipmentTags::Equipment_Slot_Hand_Right).bSuccess);

	FPEPItemLocation LocationBeforeRequest;
	Fixture.Store->FindItemLocation(ItemId, LocationBeforeRequest);
	TestTrue(TEXT("preview leaves the source location untouched"),
		LocationBeforeRequest.LocationTag == PEPEquipmentTags::Equipment_Slot_Belt);

	int32 RequestId = 0;
	TestEqual(TEXT("authority executes equip through IPEPItemStore"),
		static_cast<int32>(Fixture.Equipment->RequestEquipItem(
			ItemId,
			PEPEquipmentTags::Equipment_Slot_Hand_Right,
			RequestId)),
		static_cast<int32>(EPEPRequestResult::Executed));
	TestTrue(TEXT("the equipment request allocates a correlation id"), RequestId > 0);

	FPEPEquippedItemPublic RightHand;
	TestTrue(TEXT("the public projection reflects the generic store move"),
		Fixture.Equipment->GetActiveHandItem(EPEPEquipmentHand::Right, RightHand));
	TestTrue(TEXT("the right hand carries the requested item"), RightHand.ItemId == ItemId);
	FPEPItemLocation DisplacedLocation;
	TestTrue(TEXT("the displaced item remains in the generic store"),
		Fixture.Store->FindItemLocation(PreviousHandItemId, DisplacedLocation));
	TestTrue(TEXT("the displaced item atomically takes the source slot"),
		DisplacedLocation.LocationTag == PEPEquipmentTags::Equipment_Slot_Belt);
	return true;
}

#endif
