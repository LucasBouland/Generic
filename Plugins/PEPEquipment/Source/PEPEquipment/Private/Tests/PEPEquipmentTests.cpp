#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/PEPEquipmentComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/PEPCoreTags.h"
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
#include "NativeGameplayTags.h"
#include "StructUtils/InstancedStruct.h"
#include "Tests/PEPTestItemStoreComponent.h"
#include "UObject/UnrealType.h"

namespace PEPEquipmentTest
{
	/* Test-only gameplay access: Equipment declares no access vocabulary of its own. */
	UE_DEFINE_GAMEPLAY_TAG_STATIC(Access_Reach, "Test.Equipment.Access.Reach");

	/** The access filters are designer settings with no setter; tests reach them by reflection. */
	FGameplayTagContainer* FindAccessTags(UPEPEquipmentComponent* Equipment, const FName PropertyName)
	{
		const FStructProperty* Property =
			FindFProperty<FStructProperty>(UPEPEquipmentComponent::StaticClass(), PropertyName);
		return Equipment && Property && Property->Struct == FGameplayTagContainer::StaticStruct()
			? Property->ContainerPtrToValuePtr<FGameplayTagContainer>(Equipment)
			: nullptr;
	}

	struct FFixture
	{
		UWorld* World = nullptr;
		AActor* Owner = nullptr;
		UPEPTestItemStoreComponent* Store = nullptr;
		UPEPEquipmentComponent* Equipment = nullptr;
		TArray<TObjectPtr<UPEPItemDefinition>> Definitions;

		explicit FFixture(
			const bool bEnablePresentation = false,
			UPEPEquipmentProfile* EquipmentProfile = nullptr,
			const bool bBeginPlay = true)
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
			/* The fixture world has no game mode, so nothing begins play unless told to. */
			if (bBeginPlay)
			{
				BeginPlay();
			}
		}

		void BeginPlay()
		{
			Owner->DispatchBeginPlay();
		}

		/* Store writes fold into one projection rebuild at the next tick; tests do not tick. */
		void Flush()
		{
			Equipment->FlushDeferredUpdates();
		}

		bool Add(const FPEPItemInstance& Item, const FPEPItemLocation& Location)
		{
			const bool bAdded = Store->AddItem(Item, Location, FPEPItemAccessContext::System()).bSuccess;
			Flush();
			return bAdded;
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

	TestTrue(TEXT("left hand accepts its item"), Fixture.Add(
		LeftItem,
		PEPEquipmentTest::HandLocation(EPEPEquipmentHand::Left)));
	TestTrue(TEXT("right hand accepts a different item"), Fixture.Add(
		RightItem,
		PEPEquipmentTest::HandLocation(EPEPEquipmentHand::Right)));

	FPEPEquippedItemInfo LeftPublic;
	FPEPEquippedItemInfo RightPublic;
	TestTrue(TEXT("left hand is occupied"),
		Fixture.Equipment->FindItemInHand(EPEPEquipmentHand::Left, LeftPublic));
	TestTrue(TEXT("right hand is occupied"),
		Fixture.Equipment->FindItemInHand(EPEPEquipmentHand::Right, RightPublic));
	TestTrue(TEXT("the hands expose two different item identities"),
		LeftPublic.ItemId == LeftId && RightPublic.ItemId == RightId && LeftId != RightId);
	FPEPEquippedItemInfo Unused;
	TestFalse(TEXT("no item is ever in the None hand"),
		Fixture.Equipment->FindItemInHand(EPEPEquipmentHand::None, Unused));

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
	TestNull(TEXT("equipment presentation does not own weapon aim offsets"),
		EquipmentPresentation->FindPropertyByName(TEXT("RightHandAimLocationOverride")));

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

	TestTrue(TEXT("bag equips on the back"), Fixture.Add(
		Bag,
		FPEPItemLocation::MakeNamedSlot(PEPEquipmentTags::Equipment_Slot_Back)));
	TestTrue(TEXT("content enters the equipped bag"), Fixture.Add(
		Content,
		FPEPItemLocation::MakeItemContainer(BagId, TEXT("content"))));

	const TArray<FPEPEquippedItemInfo> PublicEntries =
		Fixture.Equipment->GetPublicPresentationNodes();
	TestEqual(TEXT("only the equipped bag is publicly projected"), PublicEntries.Num(), 1);
	TestTrue(TEXT("the public identity is the bag"),
		PublicEntries.Num() == 1 && PublicEntries[0].ItemId == BagId);
	TestFalse(TEXT("the child item is absent from the public projection"),
		PublicEntries.ContainsByPredicate([ContentId](const FPEPEquippedItemInfo& Entry)
		{
			return Entry.ItemId == ContentId;
		}));

	const TArray<const UScriptStruct*> PublicStructs {
		FPEPEquippedItemPublic::StaticStruct(),
		FPEPEquippedItemInfo::StaticStruct() };
	for (const UScriptStruct* PublicStruct : PublicStructs)
	{
		TestNull(TEXT("the public struct has no full Item field"),
			PublicStruct->FindPropertyByName(TEXT("Item")));
		TestNull(TEXT("the public struct has no state fragment field"),
			PublicStruct->FindPropertyByName(TEXT("StateFragments")));
		TestNull(TEXT("the public struct has no child/content field"),
			PublicStruct->FindPropertyByName(TEXT("Contents")));
		TestNull(TEXT("the hand is derived from the profile, never replicated"),
			PublicStruct->FindPropertyByName(TEXT("Hand")));
	}

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

	TestTrue(TEXT("weapon equips as the public root"), Fixture.Add(
		Weapon,
		PEPEquipmentTest::HandLocation(EPEPEquipmentHand::Right)));
	TestTrue(TEXT("optic enters an attachment slot"), Fixture.Add(
		Optic,
		FPEPItemLocation::MakeItemContainer(WeaponId, OpticSlotName)));
	TestTrue(TEXT("cover enters a nested attachment slot"), Fixture.Add(
		Cover,
		FPEPItemLocation::MakeItemContainer(OpticId, CoverSlotName)));
	TestTrue(TEXT("private item enters a content slot"), Fixture.Add(
		PrivateContent,
		FPEPItemLocation::MakeItemContainer(WeaponId, ContentSlotName)));
	TestTrue(TEXT("an attachment may exist inside the private content"), Fixture.Add(
		HiddenAttachment,
		FPEPItemLocation::MakeItemContainer(
			PrivateContentId,
			HiddenAttachmentSlotName)));

	const TArray<FPEPEquippedItemInfo> PublicEntries =
		Fixture.Equipment->GetPublicPresentationNodes();
	TestEqual(TEXT("root and two recursive attachments are public"), PublicEntries.Num(), 3);
	TestFalse(TEXT("content remains absent"), PublicEntries.ContainsByPredicate(
		[PrivateContentId](const FPEPEquippedItemInfo& Entry)
		{
			return Entry.ItemId == PrivateContentId;
		}));
	TestFalse(TEXT("an attachment below private content also remains absent"),
		PublicEntries.ContainsByPredicate(
			[HiddenAttachmentId](const FPEPEquippedItemInfo& Entry)
			{
				return Entry.ItemId == HiddenAttachmentId;
			}));

	const FPEPEquippedItemInfo* PublicOptic = PublicEntries.FindByPredicate(
		[OpticId](const FPEPEquippedItemInfo& Entry)
		{
			return Entry.ItemId == OpticId;
		});
	const FPEPEquippedItemInfo* PublicCover = PublicEntries.FindByPredicate(
		[CoverId](const FPEPEquippedItemInfo& Entry)
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
	FPEPEquippedItemInfo IndexedCover;
	TestTrue(TEXT("a nested attachment is found by id"),
		Fixture.Equipment->FindPublicPresentationNode(CoverId, IndexedCover)
		&& IndexedCover.ParentItemId == OpticId);
	TestFalse(TEXT("private content is not found by id"),
		Fixture.Equipment->FindPublicPresentationNode(PrivateContentId, IndexedCover));

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

	TestTrue(TEXT("modular item equips"), Fixture.Add(
		Item,
		FPEPItemLocation::MakeNamedSlot(PEPEquipmentTags::Equipment_Slot_Body_Head)));
	TestTrue(TEXT("equipment applies the skeletal override"),
		Target->GetSkeletalMeshAsset() == EquippedMesh);

	FPEPLocalPresentationTarget LocalTarget;
	TestTrue(TEXT("modular equipment exposes a generic local target"),
		Fixture.Equipment->FindLocalPresentationTarget(ItemId, LocalTarget));
	TestEqual(TEXT("generic target reports modular policy"),
		LocalTarget.Policy, EPEPEquipmentPresentationPolicy::ModularSkeletal);
	TestTrue(TEXT("modular target supplies its component for sockets"),
		LocalTarget.SocketProvider.Get() == Target);
	TestFalse(TEXT("modular presentation does not require an actor"),
		LocalTarget.PresentationActor.IsValid());

	TestTrue(TEXT("modular item can be removed"), Fixture.Store->RemoveItem(
		ItemId,
		FPEPItemAccessContext::System()).bSuccess);
	Fixture.Flush();
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

	UPEPEquipmentProfile* HiddenSlotProfile =
		NewObject<UPEPEquipmentProfile>(GetTransientPackage());
	FPEPEquipmentSlotDefinition& HiddenSlot = HiddenSlotProfile->Slots.AddDefaulted_GetRef();
	HiddenSlot.SlotTag = PEPEquipmentTags::Equipment_Slot_Belt;
	HiddenSlot.bShowPublicly = false;
	FDataValidationContext HiddenContext;
	TestTrue(TEXT("a slot hidden from the projection is legitimate"),
		HiddenSlotProfile->IsDataValid(HiddenContext) != EDataValidationResult::Invalid);
	TestEqual(TEXT("and raises no warning: its occupant still grants through the owner view"),
		HiddenContext.GetNumWarnings(), 0u);
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

	TestTrue(TEXT("tagged item equips"), Fixture.Add(
		Item,
		FPEPItemLocation::MakeNamedSlot(PEPEquipmentTags::Equipment_Slot_Primary)));

	FPEPLocalPresentationTarget LocalTarget;
	Fixture.Equipment->FindLocalPresentationTarget(ItemId, LocalTarget);
	const APEPEquippedPresentationActor* Actor =
		Cast<APEPEquippedPresentationActor>(LocalTarget.PresentationActor.Get());
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
	TestTrue(TEXT("the generic store accepts a source slot item"), Fixture.Add(
		Item,
		FPEPItemLocation::MakeNamedSlot(PEPEquipmentTags::Equipment_Slot_Belt)));
	TestTrue(TEXT("the target hand starts occupied"), Fixture.Add(
		PreviousHandItem,
		FPEPItemLocation::MakeNamedSlot(PEPEquipmentTags::Equipment_Slot_Hand_Right)));
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

	/* An equip this component executes rebuilds the projection before returning: no flush. */
	FPEPEquippedItemInfo RightHand;
	TestTrue(TEXT("the public projection reflects the generic store move"),
		Fixture.Equipment->FindItemInHand(EPEPEquipmentHand::Right, RightHand));
	TestTrue(TEXT("the right hand carries the requested item"), RightHand.ItemId == ItemId);
	FPEPItemLocation DisplacedLocation;
	TestTrue(TEXT("the displaced item remains in the generic store"),
		Fixture.Store->FindItemLocation(PreviousHandItemId, DisplacedLocation));
	TestTrue(TEXT("the displaced item atomically takes the source slot"),
		DisplacedLocation.LocationTag == PEPEquipmentTags::Equipment_Slot_Belt);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentHandFromProfileTest,
	"PEP.Equipment.PublicProjection.HandIsDerivedFromTheProfileDeterministically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentHandFromProfileTest::RunTest(const FString& Parameters)
{
	/* Invalid as an asset (one hand on two slots), which is exactly what must stay deterministic. */
	UPEPEquipmentProfile* Profile = NewObject<UPEPEquipmentProfile>(GetTransientPackage());
	FPEPEquipmentSlotDefinition& PrimarySlot = Profile->Slots.AddDefaulted_GetRef();
	PrimarySlot.SlotTag = PEPEquipmentTags::Equipment_Slot_Primary;
	PrimarySlot.Hand = EPEPEquipmentHand::Right;
	FPEPEquipmentSlotDefinition& HandSlot = Profile->Slots.AddDefaulted_GetRef();
	HandSlot.SlotTag = PEPEquipmentTags::Equipment_Slot_Hand_Right;
	HandSlot.Hand = EPEPEquipmentHand::Right;
	FFixture Fixture(false, Profile);

	TestEqual(TEXT("the profile gives the slot its hand"),
		Fixture.Equipment->GetSlotHand(PEPEquipmentTags::Equipment_Slot_Primary),
		EPEPEquipmentHand::Right);
	TestEqual(TEXT("a slot outside the profile has no hand"),
		Fixture.Equipment->GetSlotHand(PEPEquipmentTags::Equipment_Slot_Head),
		EPEPEquipmentHand::None);

	UPEPItemDefinition* Definition = Fixture.MakeDefinition(
		TEXT("EquipmentHandFromProfile"),
		FPEPEquipmentPresentationFragment());
	FPEPItemInstance InHand = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	FPEPItemInstance InPrimary = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	const FPEPItemId InHandId = InHand.ItemId;
	const FPEPItemId InPrimaryId = InPrimary.ItemId;

	/* Added in the opposite order to the profile, so array order would give the other answer. */
	TestTrue(TEXT("the hand slot takes an item"), Fixture.Add(
		InHand,
		FPEPItemLocation::MakeNamedSlot(PEPEquipmentTags::Equipment_Slot_Hand_Right)));
	TestTrue(TEXT("the primary slot takes an item"), Fixture.Add(
		InPrimary,
		FPEPItemLocation::MakeNamedSlot(PEPEquipmentTags::Equipment_Slot_Primary)));

	FPEPEquippedItemInfo Found;
	TestTrue(TEXT("the right hand holds something"),
		Fixture.Equipment->FindItemInHand(EPEPEquipmentHand::Right, Found));
	TestTrue(TEXT("the first slot in profile order wins"), Found.ItemId == InPrimaryId);

	TestTrue(TEXT("the primary item leaves"), Fixture.Store->RemoveItem(
		InPrimaryId,
		FPEPItemAccessContext::System()).bSuccess);
	Fixture.Flush();
	TestTrue(TEXT("the next declaring slot answers"),
		Fixture.Equipment->FindItemInHand(EPEPEquipmentHand::Right, Found)
		&& Found.ItemId == InHandId);
	TestFalse(TEXT("an undeclared hand holds nothing"),
		Fixture.Equipment->FindItemInHand(EPEPEquipmentHand::Left, Found));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentTemporaryPresentationGuardTest,
	"PEP.Equipment.Presentation.TemporaryPropsAreCappedAndNeedAccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentTemporaryPresentationGuardTest::RunTest(const FString& Parameters)
{
	FFixture Fixture;
	UPEPItemDefinition* Definition = Fixture.MakeDefinition(
		TEXT("EquipmentTemporaryGuard"),
		FPEPEquipmentPresentationFragment());

	FPEPItemInstance Bag = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	const FPEPItemId BagId = Bag.ItemId;
	TestTrue(TEXT("a bag equips"), Fixture.Add(
		Bag,
		FPEPItemLocation::MakeNamedSlot(PEPEquipmentTags::Equipment_Slot_Back)));
	TArray<FPEPItemId> Pocketed;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		FPEPItemInstance Item = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
		Pocketed.Add(Item.ItemId);
		TestTrue(TEXT("an item enters the bag"), Fixture.Add(
			Item,
			FPEPItemLocation::MakeItemContainer(
				BagId,
				FName(*FString::Printf(TEXT("pocket_%d"), Index)))));
	}
	const FName Socket(TEXT("hand_r"));
	const auto Show = [&Fixture, Socket](const FPEPItemId ItemId, const TCHAR* Key)
	{
		return Fixture.Equipment->RequestShowTemporaryItemPresentation(ItemId, FName(Key), Socket);
	};

	TestTrue(TEXT("first key"), Show(Pocketed[0], TEXT("K1")));
	TestTrue(TEXT("second key"), Show(Pocketed[1], TEXT("K2")));
	TestTrue(TEXT("third key"), Show(Pocketed[2], TEXT("K3")));
	TestTrue(TEXT("fourth key reaches the default cap"), Show(Pocketed[3], TEXT("K4")));
	TestFalse(TEXT("a fifth key is refused"), Show(Pocketed[4], TEXT("K5")));
	TestTrue(TEXT("a live key may still be replaced"), Show(Pocketed[4], TEXT("K1")));

	Fixture.Equipment->RequestHideTemporaryItemPresentation(TEXT("K2"));
	TestTrue(TEXT("hiding a key frees its room"), Show(Pocketed[5], TEXT("K5")));

	Fixture.Equipment->RequestHideTemporaryItemPresentation(TEXT("K3"));
	Fixture.Store->DeniedGameplayAccess.Add(Pocketed[0]);
	TestTrue(TEXT("with no access tag (the default) any item of the store qualifies"),
		Show(Pocketed[0], TEXT("K6")));
	Fixture.Equipment->RequestHideTemporaryItemPresentation(TEXT("K6"));

	FGameplayTagContainer* AccessTags = PEPEquipmentTest::FindAccessTags(
		Fixture.Equipment,
		TEXT("TemporaryPresentationAccessTags"));
	TestNotNull(TEXT("the temporary presentation access filter exists"), AccessTags);
	if (!AccessTags)
	{
		return false;
	}
	TestTrue(TEXT("the access filter is empty by default"), AccessTags->IsEmpty());
	AccessTags->AddTag(PEPEquipmentTest::Access_Reach);
	TestFalse(TEXT("an item out of reach for every configured access is refused"),
		Show(Pocketed[0], TEXT("K6")));
	FPEPItemId Foreign;
	Foreign.Value = 987654;
	TestFalse(TEXT("an item outside the store is refused"), Show(Foreign, TEXT("K6")));
	TestFalse(TEXT("a key is required"),
		Fixture.Equipment->RequestShowTemporaryItemPresentation(Pocketed[1], NAME_None, Socket));
	TestFalse(TEXT("a socket is required"),
		Fixture.Equipment->RequestShowTemporaryItemPresentation(
			Pocketed[1],
			TEXT("K6"),
			NAME_None));
	TestTrue(TEXT("a reachable item still fits the freed room"), Show(Pocketed[1], TEXT("K6")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentPresentationWaitsForBeginPlayTest,
	"PEP.Equipment.Presentation.WaitsForBeginPlayAndAttachesToTheMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentPresentationWaitsForBeginPlayTest::RunTest(const FString& Parameters)
{
	FFixture Fixture(true, nullptr, false);

	/* A capsule-like root and the character mesh under it, as on a pawn. */
	USceneComponent* Root = NewObject<USceneComponent>(Fixture.Owner);
	Fixture.Owner->SetRootComponent(Root);
	Root->RegisterComponent();
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(Fixture.Owner);
	Mesh->SetupAttachment(Root);
	Mesh->RegisterComponent();

	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(GetTransientPackage());
	FPEPStaticMeshEquipmentVisual Visual;
	Visual.Mesh = StaticMesh;
	FPEPEquipmentPresentationFragment Presentation;
	Presentation.Visual = FInstancedStruct::Make(Visual);
	UPEPItemDefinition* Definition = Fixture.MakeDefinition(
		TEXT("EquipmentWaitsForBeginPlay"),
		Presentation);
	FPEPItemInstance Item = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	const FPEPItemId ItemId = Item.ItemId;

	/* The store fills before BeginPlay, as on a server spawning a loadout or a late joiner. */
	TestTrue(TEXT("the item equips before BeginPlay"), Fixture.Add(
		Item,
		FPEPItemLocation::MakeNamedSlot(PEPEquipmentTags::Equipment_Slot_Primary)));
	FPEPLocalPresentationTarget Target;
	TestFalse(TEXT("no visual is built before BeginPlay"),
		Fixture.Equipment->FindLocalPresentationTarget(ItemId, Target));

	Fixture.BeginPlay();
	TestTrue(TEXT("BeginPlay builds the visual"),
		Fixture.Equipment->FindLocalPresentationTarget(ItemId, Target));
	const AActor* Actor = Target.PresentationActor.Get();
	TestTrue(TEXT("the visual hangs off the discovered mesh, not the root"),
		Actor && Actor->GetRootComponent()
		&& Actor->GetRootComponent()->GetAttachParent() == Mesh);
	FPEPEquippedItemInfo Node;
	TestTrue(TEXT("the projection includes the item equipped before BeginPlay"),
		Fixture.Equipment->FindPublicPresentationNode(ItemId, Node));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPEquipmentEquipAccessTagsTest,
	"PEP.Equipment.Request.EquipAccessTagsGateRequestsOnlyWhenSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPEquipmentEquipAccessTagsTest::RunTest(const FString& Parameters)
{
	FFixture Fixture;
	UPEPItemDefinition* Definition = Fixture.MakeDefinition(
		TEXT("EquipmentEquipAccess"),
		FPEPEquipmentPresentationFragment());
	FPEPItemInstance Pistol = PEPItemAutomationTestSupport::MakeItemForTests(Definition);
	const FPEPItemId PistolId = Pistol.ItemId;
	TestTrue(TEXT("the pistol sits on the belt"), Fixture.Add(
		Pistol,
		FPEPItemLocation::MakeNamedSlot(PEPEquipmentTags::Equipment_Slot_Belt)));
	Fixture.Store->DeniedGameplayAccess.Add(PistolId);

	const FGameplayTag Hand = PEPEquipmentTags::Equipment_Slot_Hand_Right;
	TestTrue(TEXT("with no equip access tag (the default) the store's own rules decide"),
		Fixture.Equipment->PreviewEquipItem(PistolId, Hand).bSuccess);

	FGameplayTagContainer* AccessTags =
		PEPEquipmentTest::FindAccessTags(Fixture.Equipment, TEXT("EquipAccessTags"));
	TestNotNull(TEXT("the equip access filter exists"), AccessTags);
	if (!AccessTags)
	{
		return false;
	}
	AccessTags->AddTag(PEPEquipmentTest::Access_Reach);
	const FPEPItemResult Preview = Fixture.Equipment->PreviewEquipItem(PistolId, Hand);
	TestTrue(TEXT("an item the store grants none of the accesses is refused in preview"),
		!Preview.bSuccess && Preview.ReasonTag == PEPCoreTags::Reason_InvalidTarget);
	int32 RequestId = 0;
	TestEqual(TEXT("and as a request"),
		static_cast<int32>(Fixture.Equipment->RequestEquipItem(PistolId, Hand, RequestId)),
		static_cast<int32>(EPEPRequestResult::Failed));

	Fixture.Store->DeniedGameplayAccess.Remove(PistolId);
	TestEqual(TEXT("once the store grants the access, the equip executes"),
		static_cast<int32>(Fixture.Equipment->RequestEquipItem(PistolId, Hand, RequestId)),
		static_cast<int32>(EPEPRequestResult::Executed));
	return true;
}

#endif
