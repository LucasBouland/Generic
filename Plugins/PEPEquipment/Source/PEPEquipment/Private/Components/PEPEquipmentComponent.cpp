#include "Components/PEPEquipmentComponent.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/PEPCoreLogChannels.h"
#include "Core/PEPCoreTags.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Equipment/PEPEquipmentPresentationFragment.h"
#include "Equipment/PEPEquipmentProfile.h"
#include "Equipment/PEPEquipmentTags.h"
#include "Equipment/PEPEquippedPresentationActor.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Item/PEPItemAccess.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemLocation.h"
#include "Item/PEPItemQuery.h"
#include "Item/PEPItemStore.h"
#include "Item/PEPItemTags.h"
#include "Net/UnrealNetwork.h"

namespace
{
	const UPEPItemDefinition* ResolveLoadedDefinition(const FPrimaryAssetId DefinitionId)
	{
		if (const UPEPItemDefinition* Definition = Cast<UPEPItemDefinition>(
			UAssetManager::Get().GetPrimaryAssetObject(DefinitionId)))
		{
			return Definition;
		}
#if WITH_DEV_AUTOMATION_TESTS
		return PEPItemAutomationTestSupport::FindTransientDefinitionForTests(DefinitionId);
#else
		return nullptr;
#endif
	}

	/** Everything a local visual is built from. Hand is bookkeeping for queries, not a visual input. */
	bool IsSamePresentationNode(
		const FPEPEquippedItemPublic& A,
		const FPEPEquippedItemPublic& B)
	{
		return A.ItemId == B.ItemId
			&& A.DefinitionId == B.DefinitionId
			&& A.SlotTag == B.SlotTag
			&& A.ParentItemId == B.ParentItemId
			&& A.ParentSlotName == B.ParentSlotName;
	}

	/**
	 * The named slot an item ultimately sits under, walked through the store. A store that fills
	 * FPEPItemStoreChange's root tags makes this unnecessary; one that does not still gets the
	 * right answer, at the cost of one lookup per level of nesting.
	 */
	FGameplayTag ResolveRootSlotTagViaStore(
		const IPEPItemStore& Store,
		const FPEPItemLocation& Location)
	{
		FPEPItemLocation Current = Location;
		TSet<FPEPItemId> Visited;
		while (Current.Type == EPEPItemLocationType::ItemContainer)
		{
			const FPEPItemId ParentItemId = Current.ParentItemId;
			if (!ParentItemId.IsValid() || Visited.Contains(ParentItemId)
				|| !Store.FindItemLocation(ParentItemId, Current))
			{
				return FGameplayTag();
			}
			Visited.Add(ParentItemId);
		}
		return Current.Type == EPEPItemLocationType::NamedSlot ? Current.LocationTag : FGameplayTag();
	}

	FPEPEquipmentSlotDefinition MakeSlot(
		const FGameplayTag SlotTag,
		const FName Socket,
		const EPEPEquipmentHand Hand = EPEPEquipmentHand::None,
		const EPEPEquipmentPresentationPolicy Policy =
			EPEPEquipmentPresentationPolicy::Attached,
		const FGameplayTag PresentationTarget = FGameplayTag())
	{
		FPEPEquipmentSlotDefinition Slot;
		Slot.SlotTag = SlotTag;
		Slot.AttachSocket = Socket;
		Slot.Hand = Hand;
		Slot.PresentationPolicy = Policy;
		Slot.PresentationTargetTag = PresentationTarget;
		return Slot;
	}
}

UPEPEquipmentComponent::UPEPEquipmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	PublicEquipmentView.OwnerComponent = this;
	TemporaryPresentationView.OwnerComponent = this;

	SlotDefinitions.Add(MakeSlot(
		PEPEquipmentTags::Equipment_Slot_Hand_Left,
		TEXT("hand_l"),
		EPEPEquipmentHand::Left));
	SlotDefinitions.Add(MakeSlot(
		PEPEquipmentTags::Equipment_Slot_Hand_Right,
		TEXT("hand_r"),
		EPEPEquipmentHand::Right));
	SlotDefinitions.Add(MakeSlot(PEPEquipmentTags::Equipment_Slot_Head, TEXT("head")));
	SlotDefinitions.Add(MakeSlot(PEPEquipmentTags::Equipment_Slot_Chest, TEXT("spine_03")));
	SlotDefinitions.Add(MakeSlot(PEPEquipmentTags::Equipment_Slot_Back, TEXT("spine_03")));
	SlotDefinitions.Add(MakeSlot(PEPEquipmentTags::Equipment_Slot_Belt, TEXT("pelvis")));
	SlotDefinitions.Add(MakeSlot(
		PEPEquipmentTags::Equipment_Slot_Primary,
		TEXT("weapon_primary_socket")));
	SlotDefinitions.Add(MakeSlot(
		PEPEquipmentTags::Equipment_Slot_Secondary,
		TEXT("weapon_secondary_socket")));
	SlotDefinitions.Add(MakeSlot(
		PEPEquipmentTags::Equipment_Slot_Body_Head,
		NAME_None,
		EPEPEquipmentHand::None,
		EPEPEquipmentPresentationPolicy::ModularSkeletal,
		PEPEquipmentTags::Equipment_PresentationTarget_Body_Head));
	SlotDefinitions.Add(MakeSlot(
		PEPEquipmentTags::Equipment_Slot_Body_Torso,
		NAME_None,
		EPEPEquipmentHand::None,
		EPEPEquipmentPresentationPolicy::ModularSkeletal,
		PEPEquipmentTags::Equipment_PresentationTarget_Body_Torso));
	SlotDefinitions.Add(MakeSlot(
		PEPEquipmentTags::Equipment_Slot_Body_Arms,
		NAME_None,
		EPEPEquipmentHand::None,
		EPEPEquipmentPresentationPolicy::ModularSkeletal,
		PEPEquipmentTags::Equipment_PresentationTarget_Body_Arms));
	SlotDefinitions.Add(MakeSlot(
		PEPEquipmentTags::Equipment_Slot_Body_Legs,
		NAME_None,
		EPEPEquipmentHand::None,
		EPEPEquipmentPresentationPolicy::ModularSkeletal,
		PEPEquipmentTags::Equipment_PresentationTarget_Body_Legs));
}

void UPEPEquipmentComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, PublicEquipmentView);
	DOREPLIFETIME(ThisClass, TemporaryPresentationView);
}

void UPEPEquipmentComponent::OnRegister()
{
	Super::OnRegister();
	// Component instances can be duplicated from an archetype in PIE. This raw back-reference is
	// intentionally not serialized, so always restore it on the live instance before Fast Array
	// receive callbacks can run.
	PublicEquipmentView.OwnerComponent = this;
	TemporaryPresentationView.OwnerComponent = this;
	InitializeItemStoreIntegration();
}

void UPEPEquipmentComponent::OnUnregister()
{
	UninitializeItemStoreIntegration();
	Super::OnUnregister();
}

void UPEPEquipmentComponent::BeginPlay()
{
	Super::BeginPlay();
	PublicEquipmentView.OwnerComponent = this;
	TemporaryPresentationView.OwnerComponent = this;
	bEquipmentProfileLocked = true;
	InitializeItemStoreIntegration();

	if (!PresentationMesh)
	{
		PresentationMesh = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		RefreshPublicProjection();
	}
	else
	{
		ReconcileLocalPresentations();
	}
}

void UPEPEquipmentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UninitializeItemStoreIntegration();

	for (TPair<FPrimaryAssetId, TSharedPtr<FStreamableHandle>>& Pair : DefinitionLoadHandles)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->CancelHandle();
		}
	}
	DefinitionLoadHandles.Reset();
	FailedDefinitionLoads.Reset();
	for (TPair<FSoftObjectPath, TSharedPtr<FStreamableHandle>>& Pair : PresentationAssetLoadHandles)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->CancelHandle();
		}
	}
	PresentationAssetLoadHandles.Reset();
	FailedPresentationAssetLoads.Reset();
	DestroyAllTemporaryPresentations();
	DestroyAllLocalPresentations();

	Super::EndPlay(EndPlayReason);
}

bool UPEPEquipmentComponent::SetItemStore(UObject* InItemStoreObject)
{
	if (InItemStoreObject
		&& !InItemStoreObject->GetClass()->ImplementsInterface(UPEPItemStore::StaticClass()))
	{
		return false;
	}
	if (ItemStoreObject == InItemStoreObject)
	{
		return true;
	}

	UninitializeItemStoreIntegration();
	ItemStoreObject = InItemStoreObject;

	/* A running instance naming its own store, including naming none. Discovery stops guessing. */
	bItemStoreExplicitlySet = true;

	if (IsRegistered())
	{
		InitializeItemStoreIntegration();
		RefreshPublicProjection();
	}
	return true;
}

void UPEPEquipmentComponent::SetPresentationMesh(USkeletalMeshComponent* InPresentationMesh)
{
	if (PresentationMesh != InPresentationMesh)
	{
		DestroyAllLocalPresentations();
	}
	PresentationMesh = InPresentationMesh;
	if (InPresentationMesh)
	{
		RegisterPresentationTarget(
			PEPEquipmentTags::Equipment_PresentationTarget_Body_Main,
			InPresentationMesh);
	}
	ReconcileLocalPresentations();
}

void UPEPEquipmentComponent::SetEquipmentProfile(UPEPEquipmentProfile* InProfile)
{
	if (!ensureMsgf(
		!bEquipmentProfileLocked,
		TEXT("EquipmentProfile is setup-only and cannot be changed after BeginPlay on '%s'."),
		*GetNameSafe(GetOwner())))
	{
		return;
	}
	if (EquipmentProfile == InProfile)
	{
		return;
	}
	UninitializeItemStoreIntegration();
	EquipmentProfile = InProfile;
	if (IsRegistered())
	{
		InitializeItemStoreIntegration();
	}
	DestroyAllLocalPresentations();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		RefreshPublicProjection();
	}
	ReconcileLocalPresentations();
}

bool UPEPEquipmentComponent::RegisterPresentationTarget(
	const FGameplayTag TargetTag,
	USceneComponent* Component)
{
	if (!TargetTag.IsValid() || !Component
		|| (GetOwner() && Component->GetOwner() != GetOwner()))
	{
		return false;
	}
	if (const TObjectPtr<USceneComponent>* Existing =
		RegisteredPresentationTargets.Find(TargetTag);
		Existing && Existing->Get() != Component)
	{
		if (USkeletalMeshComponent* ExistingSkeletal =
			Cast<USkeletalMeshComponent>(Existing->Get()))
		{
			ExistingSkeletal->SetSkeletalMesh(ModularBaseMeshes.FindRef(TargetTag), false);
		}
		AppliedModularItems.Remove(TargetTag);
		ModularBaseMeshes.Remove(TargetTag);
	}
	RegisteredPresentationTargets.Add(TargetTag, Component);
	if (USkeletalMeshComponent* SkeletalTarget = Cast<USkeletalMeshComponent>(Component);
		SkeletalTarget && !ModularBaseMeshes.Contains(TargetTag))
	{
		ModularBaseMeshes.Add(TargetTag, SkeletalTarget->GetSkeletalMeshAsset());
	}
	/* A target moved under every visual that used it: the only honest answer is a rebuild. */
	DestroyAllLocalPresentations();
	ReconcileLocalPresentations();
	return true;
}

void UPEPEquipmentComponent::UnregisterPresentationTarget(const FGameplayTag TargetTag)
{
	if (const TObjectPtr<USceneComponent>* Registered =
		RegisteredPresentationTargets.Find(TargetTag))
	{
		if (USkeletalMeshComponent* SkeletalTarget =
			Cast<USkeletalMeshComponent>(Registered->Get()))
		{
			SkeletalTarget->SetSkeletalMesh(ModularBaseMeshes.FindRef(TargetTag), false);
		}
	}
	RegisteredPresentationTargets.Remove(TargetTag);
	ModularBaseMeshes.Remove(TargetTag);
	AppliedModularItems.Remove(TargetTag);
	DestroyAllLocalPresentations();
	ReconcileLocalPresentations();
}

bool UPEPEquipmentComponent::SetModularPresentationBaseMesh(
	const FGameplayTag TargetTag,
	USkeletalMesh* BaseMesh)
{
	const TObjectPtr<USceneComponent>* Registered =
		RegisteredPresentationTargets.Find(TargetTag);
	USkeletalMeshComponent* SkeletalTarget = Registered
		? Cast<USkeletalMeshComponent>(Registered->Get())
		: nullptr;
	if (!SkeletalTarget)
	{
		return false;
	}
	ModularBaseMeshes.Add(TargetTag, BaseMesh);
	if (!AppliedModularItems.Contains(TargetTag))
	{
		SkeletalTarget->SetSkeletalMesh(BaseMesh, false);
	}
	return true;
}

void UPEPEquipmentComponent::SetLocalPresentationEnabled(const bool bEnabled)
{
	bEnableLocalPresentation = bEnabled;
	ReconcileLocalPresentations();
}



bool UPEPEquipmentComponent::RequestShowTemporaryItemPresentation(
	const FPEPItemId ItemId,
	const FName PresentationKey,
	const FName AttachSocket,
	const bool bHideOriginalPresentation)
{
	AActor* Owner = GetOwner();
	const IPEPItemStore* ItemStore = GetItemStoreConst();
	const FPEPItemInstance* Item = ItemStore ? ItemStore->FindItem(ItemId) : nullptr;
	if (!Owner || !Item || PresentationKey.IsNone() || AttachSocket.IsNone())
	{
		return false;
	}

	FPEPEquippedItemPublic PublicItem;
	const FPEPItemId SuppressedItemId = bHideOriginalPresentation
		&& FindPublicPresentationNode(ItemId, PublicItem)
		? ItemId
		: FPEPItemId();
	if (Owner->HasAuthority())
	{
		SetTemporaryPresentationState(
			PresentationKey,
			Item->DefinitionId,
			AttachSocket,
			SuppressedItemId);
		return true;
	}
	if (!Owner->HasLocalNetOwner())
	{
		return false;
	}

	// Immediate owner feedback, recorded as applied so the replicated state does not rebuild it.
	FPEPTemporaryPresentationEntry& Applied = AppliedTemporaryPresentations.FindOrAdd(PresentationKey);
	Applied.PresentationKey = PresentationKey;
	Applied.DefinitionId = Item->DefinitionId;
	Applied.AttachSocket = AttachSocket;
	Applied.SuppressedItemId = SuppressedItemId;
	ShowTemporaryPresentationLocal(
		PresentationKey,
		Item->DefinitionId,
		AttachSocket,
		SuppressedItemId);
	Server_ShowTemporaryItemPresentation(
		ItemId,
		PresentationKey,
		AttachSocket,
		bHideOriginalPresentation);
	return true;
}

void UPEPEquipmentComponent::RequestHideTemporaryItemPresentation(
	const FName PresentationKey)
{
	AActor* Owner = GetOwner();
	if (!Owner || PresentationKey.IsNone())
	{
		return;
	}
	if (Owner->HasAuthority())
	{
		ClearTemporaryPresentationState(PresentationKey);
		return;
	}
	if (Owner->HasLocalNetOwner())
	{
		AppliedTemporaryPresentations.Remove(PresentationKey);
		HideTemporaryPresentationLocal(PresentationKey);
		Server_HideTemporaryItemPresentation(PresentationKey);
	}
}

bool UPEPEquipmentComponent::Server_ShowTemporaryItemPresentation_Validate(
	const FPEPItemId ItemId,
	const FName PresentationKey,
	const FName AttachSocket,
	const bool bHideOriginalPresentation)
{
	(void)bHideOriginalPresentation;
	return ItemId.IsValid() && !PresentationKey.IsNone() && !AttachSocket.IsNone();
}

void UPEPEquipmentComponent::Server_ShowTemporaryItemPresentation_Implementation(
	const FPEPItemId ItemId,
	const FName PresentationKey,
	const FName AttachSocket,
	const bool bHideOriginalPresentation)
{
	/*
	 * The socket name is client-chosen and replicated to every client. Checked here, where the
	 * server has the mesh, so a bogus name is dropped. A server with no mesh (a dedicated server
	 * whose pawn class strips it) cannot check and lets it through.
	 */
	if (PresentationMesh && !PresentationMesh->DoesSocketExist(AttachSocket))
	{
		return;
	}
	const IPEPItemStore* ItemStore = GetItemStoreConst();
	if (const FPEPItemInstance* Item = ItemStore ? ItemStore->FindItem(ItemId) : nullptr)
	{
		FPEPEquippedItemPublic PublicItem;
		const FPEPItemId SuppressedItemId = bHideOriginalPresentation
			&& FindPublicPresentationNode(ItemId, PublicItem)
			? ItemId
			: FPEPItemId();
		SetTemporaryPresentationState(
			PresentationKey,
			Item->DefinitionId,
			AttachSocket,
			SuppressedItemId);
	}
}

bool UPEPEquipmentComponent::Server_HideTemporaryItemPresentation_Validate(
	const FName PresentationKey)
{
	return !PresentationKey.IsNone();
}

void UPEPEquipmentComponent::Server_HideTemporaryItemPresentation_Implementation(
	const FName PresentationKey)
{
	ClearTemporaryPresentationState(PresentationKey);
}

void UPEPEquipmentComponent::SetTemporaryPresentationState(
	const FName PresentationKey,
	const FPrimaryAssetId DefinitionId,
	const FName AttachSocket,
	const FPEPItemId SuppressedItemId)
{
	FPEPTemporaryPresentationEntry* Entry = TemporaryPresentationView.Entries.FindByPredicate(
		[PresentationKey](const FPEPTemporaryPresentationEntry& Candidate)
		{
			return Candidate.PresentationKey == PresentationKey;
		});
	if (!Entry)
	{
		Entry = &TemporaryPresentationView.Entries.AddDefaulted_GetRef();
		Entry->PresentationKey = PresentationKey;
	}
	Entry->DefinitionId = DefinitionId;
	Entry->AttachSocket = AttachSocket;
	Entry->SuppressedItemId = SuppressedItemId;
	TemporaryPresentationView.MarkItemDirty(*Entry);
	ApplyTemporaryPresentations();
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

void UPEPEquipmentComponent::ClearTemporaryPresentationState(const FName PresentationKey)
{
	const int32 Removed = TemporaryPresentationView.Entries.RemoveAll(
		[PresentationKey](const FPEPTemporaryPresentationEntry& Candidate)
		{
			return Candidate.PresentationKey == PresentationKey;
		});
	if (Removed == 0)
	{
		return;
	}
	TemporaryPresentationView.MarkArrayDirty();
	ApplyTemporaryPresentations();
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

void UPEPEquipmentComponent::HandleTemporaryPresentationsReplicated()
{
	ApplyTemporaryPresentations();
}

void UPEPEquipmentComponent::ApplyTemporaryPresentations()
{
	TSet<FName> LiveKeys;
	for (const FPEPTemporaryPresentationEntry& Entry : TemporaryPresentationView.Entries)
	{
		LiveKeys.Add(Entry.PresentationKey);
		const FPEPTemporaryPresentationEntry* Applied =
			AppliedTemporaryPresentations.Find(Entry.PresentationKey);
		if (Applied && Applied->HasSameSpec(Entry))
		{
			continue;
		}
		AppliedTemporaryPresentations.Add(Entry.PresentationKey, Entry);
		ShowTemporaryPresentationLocal(
			Entry.PresentationKey,
			Entry.DefinitionId,
			Entry.AttachSocket,
			Entry.SuppressedItemId);
	}
	for (TMap<FName, FPEPTemporaryPresentationEntry>::TIterator It(AppliedTemporaryPresentations); It; ++It)
	{
		if (!LiveKeys.Contains(It.Key()))
		{
			HideTemporaryPresentationLocal(It.Key());
			It.RemoveCurrent();
		}
	}
}

void UPEPEquipmentComponent::InitializeItemStoreIntegration()
{
	if (ItemStoreChangedHandle.IsValid())
	{
		return;
	}
	/*
	 * Resolved from the owner unless this instance was told, rather than merely when the pointer is
	 * empty. A pointer inherited from an archetype is not empty and is not ours: guarding on null
	 * let it through, and the slots were then claimed on a store this pawn does not own.
	 */
	if (!bItemStoreExplicitlySet)
	{
		ItemStoreObject = FindItemStoreObjectOnOwner();
	}
	IPEPItemStore* ItemStore = GetItemStore();
	if (!ItemStore)
	{
		/*
		 * Said out loud, because the consequence surfaces nowhere near here: an equipment slot only
		 * means something once this has claimed its tag, so a store that never got the claim
		 * reports every equip as an unsupported location — in another plugin, with no mention of
		 * equipment. Declining silently made that a very long way to find.
		 */
		UE_LOG(
			LogPEP,
			Warning,
			TEXT("%s: no item store found on '%s'. No equipment slot will accept anything until "
				"SetItemStore names one, or the owner carries a component implementing IPEPItemStore."),
			*GetName(),
			GetOwner() ? *GetOwner()->GetName() : TEXT("<no owner>"));
		return;
	}

	int32 RegisteredSlots = 0;
	for (const FPEPEquipmentSlotDefinition& Slot : GetEffectiveSlotDefinitions())
	{
		if (!Slot.IsValid())
		{
			continue;
		}
		if (ItemStore->RegisterLocationValidator(
			Slot.SlotTag,
			FPEPItemLocationValidator::CreateUObject(
				this,
				&UPEPEquipmentComponent::ValidateEquipmentLocation),
			Slot.bAllowAutoEquipOnPickup))
		{
			++RegisteredSlots;
		}
	}

	UE_LOG(
		LogPEP,
		Verbose,
		TEXT("%s: claimed %d equipment slot(s) on store '%s'."),
		*GetName(),
		RegisteredSlots,
		*ItemStoreObject->GetName());

	if (RegisteredSlots == 0)
	{
		UE_LOG(
			LogPEP,
			Warning,
			TEXT("%s: found a store but claimed no slot. Check the equipment profile, or the "
				"fallback slot definitions if none is assigned."),
			*GetName());
	}

	ItemStoreChangedHandle = ItemStore->OnItemStoreChanged().AddUObject(
		this,
		&UPEPEquipmentComponent::HandleItemStoreChanged);
}

void UPEPEquipmentComponent::UninitializeItemStoreIntegration()
{
	if (IPEPItemStore* ItemStore = GetItemStore())
	{
		ItemStore->OnItemStoreChanged().Remove(ItemStoreChangedHandle);
		for (const FPEPEquipmentSlotDefinition& Slot : GetEffectiveSlotDefinitions())
		{
			if (Slot.IsValid())
			{
				ItemStore->UnregisterLocationValidator(Slot.SlotTag, this);
			}
		}
	}
	ItemStoreChangedHandle.Reset();
}

UObject* UPEPEquipmentComponent::FindItemStoreObjectOnOwner() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	if (Owner->GetClass()->ImplementsInterface(UPEPItemStore::StaticClass()))
	{
		return Owner;
	}

	TInlineComponentArray<UActorComponent*> Components(Owner);
	for (UActorComponent* Component : Components)
	{
		if (Component
			&& Component != this
			&& Component->GetClass()->ImplementsInterface(UPEPItemStore::StaticClass()))
		{
			return Component;
		}
	}
	return nullptr;
}

IPEPItemStore* UPEPEquipmentComponent::GetItemStore() const
{
	return ItemStoreObject ? Cast<IPEPItemStore>(ItemStoreObject.Get()) : nullptr;
}

const IPEPItemStore* UPEPEquipmentComponent::GetItemStoreConst() const
{
	return GetItemStore();
}

const TArray<FPEPEquipmentSlotDefinition>&
UPEPEquipmentComponent::GetEffectiveSlotDefinitions() const
{
	return EquipmentProfile ? EquipmentProfile->Slots : SlotDefinitions;
}

FPEPItemResult UPEPEquipmentComponent::ValidateEquipmentLocation(
	const FPEPItemInstance& Item,
	const FPEPItemLocation& Location) const
{
	const FPEPEquipmentSlotDefinition* Slot = FindSlotDefinition(Location.LocationTag);
	if (!Slot)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, Item.ItemId);
	}
	return PEPItemQuery::DoesItemMatchRequiredBlockedTags(
		Item,
		Slot->RequiredItemTags,
		Slot->BlockedItemTags)
		? FPEPItemResult::Success(Item.ItemId)
		: FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, Item.ItemId);
}

const FPEPEquipmentSlotDefinition* UPEPEquipmentComponent::FindSlotDefinition(
	const FGameplayTag SlotTag) const
{
	return GetEffectiveSlotDefinitions().FindByPredicate(
		[SlotTag](const FPEPEquipmentSlotDefinition& Slot)
		{
			return Slot.SlotTag == SlotTag;
		});
}

void UPEPEquipmentComponent::HandleItemStoreChanged(const FPEPItemStoreChange& Change)
{
	// A bandage moving between two pockets never reaches the projection; only an item under one
	// of this component's slots, before or after the change, can.
	if (GetOwner() && GetOwner()->HasAuthority() && DoesChangeTouchEquipment(Change))
	{
		RefreshPublicProjection();
	}
}

void UPEPEquipmentComponent::RefreshPublicProjection()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	TMap<FPEPItemId, FPEPEquippedItemPublic> DesiredEntries;
	const IPEPItemStore* ItemStore = GetItemStoreConst();
	if (ItemStore)
	{
		for (const FPEPEquipmentSlotDefinition& Slot : GetEffectiveSlotDefinitions())
		{
			if (!Slot.IsValid() || !Slot.bShowPublicly)
			{
				continue;
			}

			FPEPItemId ItemId;
			if (!ItemStore->FindItemAtLocation(
				FPEPItemLocation::MakeNamedSlot(Slot.SlotTag),
				ItemId))
			{
				continue;
			}

			const FPEPItemInstance* Item = ItemStore->FindItem(ItemId);
			if (!Item)
			{
				continue;
			}

			FPEPEquippedItemPublic PublicEntry;
			PublicEntry.ItemId = Item->ItemId;
			PublicEntry.DefinitionId = Item->DefinitionId;
			PublicEntry.SlotTag = Slot.SlotTag;
			PublicEntry.Hand = Slot.Hand;
			DesiredEntries.Add(PublicEntry.ItemId, PublicEntry);

			TSet<FPEPItemId> VisitedItemIds;
			VisitedItemIds.Add(PublicEntry.ItemId);
			AppendPublicAttachmentEntries(
				*ItemStore,
				PublicEntry.ItemId,
				DesiredEntries,
				VisitedItemIds);
		}
	}

	bool bProjectionChanged = false;
	bool bRemovedEntry = false;
	for (int32 Index = PublicEquipmentView.Entries.Num() - 1; Index >= 0; --Index)
	{
		if (!DesiredEntries.Contains(PublicEquipmentView.Entries[Index].ItemId))
		{
			PublicEquipmentView.Entries.RemoveAtSwap(Index);
			bProjectionChanged = true;
			bRemovedEntry = true;
		}
	}

	for (const TPair<FPEPItemId, FPEPEquippedItemPublic>& Pair : DesiredEntries)
	{
		FPEPEquippedItemPublic* Existing = FindMutablePublicEntry(Pair.Key);
		if (!Existing)
		{
			FPEPEquippedItemPublic& Added = PublicEquipmentView.Entries.Add_GetRef(Pair.Value);
			PublicEquipmentView.MarkItemDirty(Added);
			bProjectionChanged = true;
			continue;
		}

		if (Existing->DefinitionId != Pair.Value.DefinitionId
			|| Existing->SlotTag != Pair.Value.SlotTag
			|| Existing->Hand != Pair.Value.Hand
			|| Existing->ParentItemId != Pair.Value.ParentItemId
			|| Existing->ParentSlotName != Pair.Value.ParentSlotName)
		{
			Existing->DefinitionId = Pair.Value.DefinitionId;
			Existing->SlotTag = Pair.Value.SlotTag;
			Existing->Hand = Pair.Value.Hand;
			Existing->ParentItemId = Pair.Value.ParentItemId;
			Existing->ParentSlotName = Pair.Value.ParentSlotName;
			PublicEquipmentView.MarkItemDirty(*Existing);
			bProjectionChanged = true;
		}
	}

	if (!bProjectionChanged)
	{
		return;
	}

	if (bRemovedEntry)
	{
		PublicEquipmentView.MarkArrayDirty();
	}

	ReconcileLocalPresentations();
	OnPublicEquipmentChanged.Broadcast();
	GetOwner()->ForceNetUpdate();
}

void UPEPEquipmentComponent::AppendPublicAttachmentEntries(
	const IPEPItemStore& ItemStore,
	const FPEPItemId ParentItemId,
	TMap<FPEPItemId, FPEPEquippedItemPublic>& OutEntries,
	TSet<FPEPItemId>& VisitedItemIds) const
{
	/* Which children are public is the store's knowledge, not a container vocabulary read here. */
	TArray<FPEPItemId> ChildItemIds;
	ItemStore.GetPublicChildren(ParentItemId, ChildItemIds);
	for (const FPEPItemId ChildItemId : ChildItemIds)
	{
		if (!ChildItemId.IsValid() || VisitedItemIds.Contains(ChildItemId))
		{
			continue;
		}

		FPEPItemLocation ChildLocation;
		const FPEPItemInstance* ChildItem = ItemStore.FindItem(ChildItemId);
		if (!ChildItem
			|| !ItemStore.FindItemLocation(ChildItemId, ChildLocation)
			|| ChildLocation.Type != EPEPItemLocationType::ItemContainer
			|| ChildLocation.ParentItemId != ParentItemId)
		{
			continue;
		}

		VisitedItemIds.Add(ChildItemId);
		FPEPEquippedItemPublic ChildEntry;
		ChildEntry.ItemId = ChildItemId;
		ChildEntry.DefinitionId = ChildItem->DefinitionId;
		ChildEntry.ParentItemId = ParentItemId;
		ChildEntry.ParentSlotName = ChildLocation.ContainerSlotName;
		OutEntries.Add(ChildItemId, ChildEntry);

		AppendPublicAttachmentEntries(
			ItemStore,
			ChildItemId,
			OutEntries,
			VisitedItemIds);
	}
}

FPEPEquippedItemPublic* UPEPEquipmentComponent::FindMutablePublicEntry(const FPEPItemId ItemId)
{
	return PublicEquipmentView.Entries.FindByPredicate(
		[ItemId](const FPEPEquippedItemPublic& Entry)
		{
			return Entry.ItemId == ItemId;
		});
}

TArray<FPEPEquippedItemPublic> UPEPEquipmentComponent::GetPublicPresentationNodes() const
{
	return PublicEquipmentView.Entries;
}

TArray<FPEPEquippedItemPublic> UPEPEquipmentComponent::GetPublicEquipmentRoots() const
{
	TArray<FPEPEquippedItemPublic> Roots;
	for (const FPEPEquippedItemPublic& Entry : PublicEquipmentView.Entries)
	{
		if (!Entry.IsAttachment())
		{
			Roots.Add(Entry);
		}
	}
	return Roots;
}

TArray<FPEPEquippedItemPublic> UPEPEquipmentComponent::GetPublicAttachments(
	const FPEPItemId ParentItemId,
	const bool bRecursive) const
{
	TArray<FPEPEquippedItemPublic> Result;
	if (!ParentItemId.IsValid())
	{
		return Result;
	}

	TArray<FPEPItemId> PendingParents { ParentItemId };
	TSet<FPEPItemId> VisitedParents;
	while (!PendingParents.IsEmpty())
	{
		const FPEPItemId CurrentParent = PendingParents.Pop(EAllowShrinking::No);
		if (VisitedParents.Contains(CurrentParent))
		{
			continue;
		}
		VisitedParents.Add(CurrentParent);
		for (const FPEPEquippedItemPublic& Entry : PublicEquipmentView.Entries)
		{
			if (Entry.ParentItemId != CurrentParent)
			{
				continue;
			}
			Result.Add(Entry);
			if (bRecursive)
			{
				PendingParents.Add(Entry.ItemId);
			}
		}
		if (!bRecursive)
		{
			break;
		}
	}
	return Result;
}

bool UPEPEquipmentComponent::FindPublicPresentationNode(
	const FPEPItemId ItemId,
	FPEPEquippedItemPublic& OutEntry) const
{
	const FPEPEquippedItemPublic* Entry = PublicEquipmentView.Entries.FindByPredicate(
		[ItemId](const FPEPEquippedItemPublic& Candidate)
		{
			return Candidate.ItemId == ItemId;
		});
	if (!Entry)
	{
		OutEntry = FPEPEquippedItemPublic();
		return false;
	}
	OutEntry = *Entry;
	return true;
}

bool UPEPEquipmentComponent::FindPublicEquippedItem(
	const FPEPItemId ItemId,
	FPEPEquippedItemPublic& OutEntry) const
{
	return FindPublicPresentationNode(ItemId, OutEntry);
}

bool UPEPEquipmentComponent::GetActiveHandItem(
	const EPEPEquipmentHand Hand,
	FPEPEquippedItemPublic& OutEntry) const
{
	const FPEPEquippedItemPublic* Entry = PublicEquipmentView.Entries.FindByPredicate(
		[Hand](const FPEPEquippedItemPublic& Candidate)
		{
			return Hand != EPEPEquipmentHand::None && Candidate.Hand == Hand;
		});
	if (!Entry)
	{
		OutEntry = FPEPEquippedItemPublic();
		return false;
	}
	OutEntry = *Entry;
	return true;
}

EPEPRequestResult UPEPEquipmentComponent::RequestEquipItem(
	const FPEPItemId ItemId,
	const FGameplayTag SlotTag,
	int32& OutRequestId)
{
	OutRequestId = 0;
	AActor* Owner = GetOwner();
	IPEPItemStore* ItemStore = GetItemStore();
	if (!Owner || !ItemStore || !ItemId.IsValid() || !FindSlotDefinition(SlotTag)
		|| !ItemStore->HasGrantedGameplayAccess(ItemId, PEPItemTags::GameplayAccess_Equip))
	{
		return EPEPRequestResult::Failed;
	}

	OutRequestId = AllocateRequestId();
	if (Owner->HasAuthority())
	{
		const FPEPItemLocation Destination = FPEPItemLocation::MakeNamedSlot(SlotTag);
		FPEPItemId OccupantId;
		const bool bSwap = ItemStore->FindItemAtLocation(Destination, OccupantId)
			&& OccupantId != ItemId;
		const FPEPItemResult Result = bSwap
			? ItemStore->SwapItems(ItemId, OccupantId, GetOwnerRequestContext())
			: ItemStore->MoveItem(ItemId, Destination, GetOwnerRequestContext());
		ResolveEquipRequest(OutRequestId, Result);
		return Result.bSuccess ? EPEPRequestResult::Executed : EPEPRequestResult::Failed;
	}

	if (!Owner->HasLocalNetOwner())
	{
		OutRequestId = 0;
		return EPEPRequestResult::Failed;
	}

	Server_RequestEquipItem(ItemId, SlotTag, OutRequestId);
	return EPEPRequestResult::Submitted;
}






FPEPItemResult UPEPEquipmentComponent::PreviewEquipItem(
	const FPEPItemId ItemId,
	const FGameplayTag SlotTag) const
{
	const IPEPItemStore* ItemStore = GetItemStoreConst();
	if (!ItemStore || !ItemId.IsValid() || !FindSlotDefinition(SlotTag)
		|| !ItemStore->HasGrantedGameplayAccess(ItemId, PEPItemTags::GameplayAccess_Equip))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
	}

	const FPEPItemLocation Destination = FPEPItemLocation::MakeNamedSlot(SlotTag);
	FPEPItemId OccupantId;
	return ItemStore->FindItemAtLocation(Destination, OccupantId) && OccupantId != ItemId
		? ItemStore->CanSwapItems(ItemId, OccupantId, GetOwnerRequestContext())
		: ItemStore->CanMoveItem(ItemId, Destination, GetOwnerRequestContext());
}

bool UPEPEquipmentComponent::Server_RequestEquipItem_Validate(
	const FPEPItemId ItemId,
	const FGameplayTag SlotTag,
	const int32 RequestId)
{
	return ItemId.IsValid() && SlotTag.IsValid() && RequestId > 0;
}

void UPEPEquipmentComponent::Server_RequestEquipItem_Implementation(
	const FPEPItemId ItemId,
	const FGameplayTag SlotTag,
	const int32 RequestId)
{
	IPEPItemStore* ItemStore = GetItemStore();
	FPEPItemResult Result = FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
	if (ItemStore && FindSlotDefinition(SlotTag)
		&& ItemStore->HasGrantedGameplayAccess(ItemId, PEPItemTags::GameplayAccess_Equip))
	{
		const FPEPItemLocation Destination = FPEPItemLocation::MakeNamedSlot(SlotTag);
		FPEPItemId OccupantId;
		Result = ItemStore->FindItemAtLocation(Destination, OccupantId) && OccupantId != ItemId
			? ItemStore->SwapItems(ItemId, OccupantId, GetOwnerRequestContext())
			: ItemStore->MoveItem(ItemId, Destination, GetOwnerRequestContext());
	}
	ResolveEquipRequest(RequestId, Result);
}






void UPEPEquipmentComponent::Client_ResolveEquipRequest_Implementation(
	const FPEPEquipmentRequestResult RequestResult)
{
	OnEquipmentRequestResolved.Broadcast(RequestResult);
}

const AController* UPEPEquipmentComponent::GetOwningController() const
{
	AActor* Owner = GetOwner();
	if (const APawn* Pawn = Cast<APawn>(Owner))
	{
		return Pawn->GetController();
	}
	if (const AController* Controller = Cast<AController>(Owner))
	{
		return Controller;
	}
	if (const APlayerState* PlayerState = Cast<APlayerState>(Owner))
	{
		return PlayerState->GetOwningController();
	}
	return nullptr;
}

FPEPItemAccessContext UPEPEquipmentComponent::GetOwnerRequestContext() const
{
	return FPEPItemAccessContext::From(GetOwningController());
}

int32 UPEPEquipmentComponent::AllocateRequestId()
{
	if (NextRequestId <= 0)
	{
		NextRequestId = 1;
	}
	return NextRequestId++;
}

void UPEPEquipmentComponent::ResolveEquipRequest(
	const int32 RequestId,
	const FPEPItemResult& Result)
{
	FPEPEquipmentRequestResult RequestResult;
	RequestResult.RequestId = RequestId;
	RequestResult.Result = Result;

	AActor* Owner = GetOwner();
	if (Owner
		&& Owner->HasAuthority()
		&& Owner->GetNetMode() != NM_Standalone
		&& Owner->HasNetOwner())
	{
		Client_ResolveEquipRequest(RequestResult);
		return;
	}

	OnEquipmentRequestResolved.Broadcast(RequestResult);
}

void UPEPEquipmentComponent::HandlePublicEquipmentReplicated()
{
	ReconcileLocalPresentations();
	OnPublicEquipmentChanged.Broadcast();
}

APEPEquippedPresentationActor* UPEPEquipmentComponent::FindLocalPresentationActor(
	const FPEPItemId ItemId) const
{
	const TObjectPtr<APEPEquippedPresentationActor>* Actor = PresentationActors.Find(ItemId);
	return Actor ? Actor->Get() : nullptr;
}

bool UPEPEquipmentComponent::FindLocalPresentationTarget(
	const FPEPItemId ItemId,
	FPEPLocalPresentationTarget& OutTarget) const
{
	const FPEPLocalPresentationTarget* Target = LocalPresentationTargets.Find(ItemId);
	if (!Target || !Target->IsValid())
	{
		OutTarget = FPEPLocalPresentationTarget();
		return false;
	}
	OutTarget = *Target;
	return true;
}

void UPEPEquipmentComponent::ReconcileLocalPresentations()
{
	++LocalPresentationGeneration;
	if (LocalPresentationGeneration <= 0)
	{
		LocalPresentationGeneration = 1;
	}
	if (!bEnableLocalPresentation || !GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		DestroyAllLocalPresentations();
		LocalPresentationChanged.Broadcast();
		return;
	}

	/*
	 * A diff, not a rebuild. This runs on every replicated delta and on every asset that finishes
	 * loading, so tearing down eight props to add a ninth was the cost of every loot. A visual is
	 * kept when the node it was built from is unchanged and it is still alive; it goes when its
	 * node changed, vanished, or was suppressed by a temporary prop - and so does everything
	 * attached beneath it, since a child rides its parent's actor.
	 */
	TMap<FPEPItemId, const FPEPEquippedItemPublic*> Desired;
	Desired.Reserve(PublicEquipmentView.Entries.Num());
	for (const FPEPEquippedItemPublic& Entry : PublicEquipmentView.Entries)
	{
		if (Entry.ItemId.IsValid() && !SuppressedPresentationItems.Contains(Entry.ItemId))
		{
			Desired.Add(Entry.ItemId, &Entry);
		}
	}
	TArray<FPEPItemId> Stale;
	for (const TPair<FPEPItemId, FPEPLocalPresentationTarget>& Pair : LocalPresentationTargets)
	{
		const FPEPEquippedItemPublic* const* Node = Desired.Find(Pair.Key);
		const FPEPEquippedItemPublic* Built = PresentedNodes.Find(Pair.Key);
		if (!Node || !Built || !IsSamePresentationNode(**Node, *Built) || !Pair.Value.IsValid())
		{
			Stale.Add(Pair.Key);
		}
	}
	for (int32 Index = 0; Index < Stale.Num(); ++Index)
	{
		for (const TPair<FPEPItemId, FPEPEquippedItemPublic>& Pair : PresentedNodes)
		{
			if (Pair.Value.ParentItemId == Stale[Index] && !Stale.Contains(Pair.Key))
			{
				Stale.Add(Pair.Key);
			}
		}
	}
	for (const FPEPItemId& ItemId : Stale)
	{
		DestroyLocalPresentation(ItemId);
	}

	TSet<FPEPItemId> ResolvingItems;
	for (const FPEPEquipmentSlotDefinition& Slot : GetEffectiveSlotDefinitions())
	{
		const FPEPEquippedItemPublic* Root = PublicEquipmentView.Entries.FindByPredicate(
			[&Slot](const FPEPEquippedItemPublic& Entry)
			{
				return !Entry.IsAttachment() && Entry.SlotTag == Slot.SlotTag;
			});
		if (Root)
		{
			EnsureLocalPresentation(*Root, ResolvingItems);
		}
	}
	for (const FPEPEquippedItemPublic& Entry : PublicEquipmentView.Entries)
	{
		if (Entry.IsAttachment())
		{
			EnsureLocalPresentation(Entry, ResolvingItems);
		}
	}
	for (const TPair<FPEPItemId, const FPEPEquippedItemPublic*>& Pair : Desired)
	{
		if (LocalPresentationTargets.Contains(Pair.Key))
		{
			PresentedNodes.Add(Pair.Key, *Pair.Value);
		}
	}
	LocalPresentationChanged.Broadcast();
}

bool UPEPEquipmentComponent::EnsureLocalPresentation(
	const FPEPEquippedItemPublic& Entry,
	TSet<FPEPItemId>& ResolvingItems)
{
	if (SuppressedPresentationItems.Contains(Entry.ItemId))
	{
		return false;
	}
	if (LocalPresentationTargets.Contains(Entry.ItemId))
	{
		return true;
	}
	if (!Entry.ItemId.IsValid() || ResolvingItems.Contains(Entry.ItemId))
	{
		return false;
	}
	ResolvingItems.Add(Entry.ItemId);

	const UPEPItemDefinition* Definition = ResolveLoadedDefinition(Entry.DefinitionId);
	if (!Definition)
	{
		RequestDefinitionLoad(Entry.DefinitionId);
		return false;
	}

	const FPEPEquipmentPresentationFragment* Presentation =
		Definition->GetDefinitionFragment<FPEPEquipmentPresentationFragment>();
	if (!Presentation)
	{
		return false;
	}

	USceneComponent* AttachParent = nullptr;
	FName AttachSocket;
	if (Entry.IsAttachment())
	{
		const FPEPEquippedItemPublic* ParentEntry =
			PublicEquipmentView.Entries.FindByPredicate(
				[&Entry](const FPEPEquippedItemPublic& Candidate)
				{
					return Candidate.ItemId == Entry.ParentItemId;
				});
		if (!ParentEntry || !EnsureLocalPresentation(*ParentEntry, ResolvingItems))
		{
			return false;
		}
		const FPEPLocalPresentationTarget* ParentTarget =
			LocalPresentationTargets.Find(Entry.ParentItemId);
		if (!ParentTarget || !ParentTarget->SocketProvider.IsValid())
		{
			return false;
		}

		const UPEPItemDefinition* ParentDefinition =
			ResolveLoadedDefinition(ParentEntry->DefinitionId);
		const FPEPEquipmentPresentationFragment* ParentPresentation = ParentDefinition
			? ParentDefinition->GetDefinitionFragment<FPEPEquipmentPresentationFragment>()
			: nullptr;
		if (!ParentPresentation)
		{
			return false;
		}
		AttachParent = ParentTarget->SocketProvider.Get();
		AttachSocket = ParentPresentation->ResolveAttachmentSocket(Entry.ParentSlotName);
	}
	else
	{
		const FPEPEquipmentSlotDefinition* Slot = FindSlotDefinition(Entry.SlotTag);
		if (!Slot)
		{
			return false;
		}
		if (Slot->PresentationPolicy == EPEPEquipmentPresentationPolicy::ModularSkeletal)
		{
			return ApplyModularPresentation(Entry, *Slot, *Presentation);
		}
		const bool bUseItemLeaderPose =
			Presentation->PoseFollowMode == EPEPEquipmentPoseFollowMode::LeaderPose;
		if (Slot->PresentationPolicy == EPEPEquipmentPresentationPolicy::LayeredSkeletal
			|| bUseItemLeaderPose)
		{
			return EnsureLayeredPresentation(
				Entry,
				*Slot,
				*Presentation,
				bUseItemLeaderPose);
		}
		if (Slot->PresentationTargetTag.IsValid())
		{
			AttachParent = RegisteredPresentationTargets.FindRef(Slot->PresentationTargetTag);
		}
		if (!AttachParent)
		{
			AttachParent = PresentationMesh;
		}
		if (!AttachParent && GetOwner())
		{
			AttachParent = GetOwner()->GetRootComponent();
		}
		AttachSocket = Slot->AttachSocket;
	}
	if (!AttachParent)
	{
		return false;
	}
	return EnsureAttachedPresentation(Entry, *Presentation, AttachParent, AttachSocket);
}

bool UPEPEquipmentComponent::EnsureAttachedPresentation(
	const FPEPEquippedItemPublic& Entry,
	const FPEPEquipmentPresentationFragment& Presentation,
	USceneComponent* AttachParent,
	const FName AttachSocket)
{
	if (Presentation.IsExplicitlyInvisible())
	{
		FPEPLocalPresentationTarget& Target = LocalPresentationTargets.Add(Entry.ItemId);
		Target.ItemId = Entry.ItemId;
		Target.Policy = EPEPEquipmentPresentationPolicy::Attached;
		Target.VisualComponent = AttachParent;
		Target.SocketProvider = AttachParent;
		Target.Generation = LocalPresentationGeneration;
		return true;
	}
	if (!Presentation.HasVisual())
	{
		return false;
	}

	/*
	 * An Actor visual names its own class; a mesh visual rides the default carrier, which is just
	 * a mesh component with the plumbing equipment expects. Resolved through the same streaming
	 * path as a mesh, so a class not yet in memory defers this frame instead of stalling.
	 */
	UClass* CarrierClass = APEPEquippedPresentationActor::StaticClass();
	if (const TSoftClassPtr<APEPEquippedPresentationActor>* ActorVisual =
		Presentation.GetActorClassVisual())
	{
		if (ActorVisual->IsNull())
		{
			return false;
		}

		CarrierClass = ActorVisual->Get();
		if (!CarrierClass)
		{
			RequestPresentationAssetLoad(ActorVisual->ToSoftObjectPath());
			return false;
		}
	}

	// A parent presentation must have its actual mesh before it can be exposed
	// as a socket provider. Defer this node (and therefore its children) until
	// the existing asset-load completion event reconciles the presentation tree.
	if (const TSoftObjectPtr<USkeletalMesh>* SkeletalVisual =
		Presentation.GetSkeletalMeshVisual();
		SkeletalVisual && !SkeletalVisual->IsNull() && !SkeletalVisual->Get())
	{
		RequestPresentationAssetLoad(SkeletalVisual->ToSoftObjectPath());
		return false;
	}
	if (const TSoftObjectPtr<UStaticMesh>* StaticVisual =
		Presentation.GetStaticMeshVisual();
		StaticVisual && !StaticVisual->IsNull() && !StaticVisual->Get())
	{
		RequestPresentationAssetLoad(StaticVisual->ToSoftObjectPath());
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = GetOwner();
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APEPEquippedPresentationActor* Actor = GetWorld()->SpawnActor<APEPEquippedPresentationActor>(
		CarrierClass,
		FTransform::Identity,
		SpawnParameters);
	if (!Actor)
	{
		return false;
	}

	Actor->AttachToComponent(
		AttachParent,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		AttachSocket);
	Actor->InitializePresentation(
		Entry,
		Presentation);
	if (!Actor->IsPresentationReady())
	{
		Actor->Destroy();
		return false;
	}
	PresentationActors.Add(Entry.ItemId, Actor);

	USceneComponent* SocketProvider = Actor->GetPresentationAttachComponent();
	FPEPLocalPresentationTarget& Target = LocalPresentationTargets.Add(Entry.ItemId);
	Target.ItemId = Entry.ItemId;
	Target.Policy = EPEPEquipmentPresentationPolicy::Attached;
	Target.PresentationActor = Actor;
	Target.VisualComponent = SocketProvider;
	Target.SocketProvider = SocketProvider;
	Target.Generation = LocalPresentationGeneration;
	return true;
}


USkeletalMeshComponent* UPEPEquipmentComponent::ResolveLayerLeader(
	const FPEPEquipmentSlotDefinition& Slot) const
{
	/*
	 * A slot may name the part it lies over — a sleeve following the arms rather than the whole
	 * body. Unnamed, it follows the body itself, which is what a plate carrier wants.
	 */
	if (Slot.PresentationTargetTag.IsValid())
	{
		if (const TObjectPtr<USceneComponent>* Registered =
			RegisteredPresentationTargets.Find(Slot.PresentationTargetTag))
		{
			if (USkeletalMeshComponent* Named = Cast<USkeletalMeshComponent>(Registered->Get()))
			{
				return Named;
			}
		}
	}

	return PresentationMesh;
}

bool UPEPEquipmentComponent::EnsureLayeredPresentation(
	const FPEPEquippedItemPublic& Entry,
	const FPEPEquipmentSlotDefinition& Slot,
	const FPEPEquipmentPresentationFragment& Presentation,
	const bool bApplyPresentationTransform)
{
	USkeletalMeshComponent* Leader = ResolveLayerLeader(Slot);
	if (!Leader)
	{
		return false;
	}

	if (Presentation.IsExplicitlyInvisible())
	{
		FPEPLocalPresentationTarget& Hidden = LocalPresentationTargets.Add(Entry.ItemId);
		Hidden.ItemId = Entry.ItemId;
		Hidden.Policy = EPEPEquipmentPresentationPolicy::LayeredSkeletal;
		Hidden.VisualComponent = Leader;
		Hidden.SocketProvider = Leader;
		Hidden.Generation = LocalPresentationGeneration;
		return true;
	}

	const TSoftObjectPtr<USkeletalMesh>* SkeletalVisual = Presentation.GetSkeletalMeshVisual();
	if (!SkeletalVisual || SkeletalVisual->IsNull())
	{
		return false;
	}

	USkeletalMesh* Mesh = SkeletalVisual->Get();
	if (!Mesh)
	{
		RequestPresentationAssetLoad(SkeletalVisual->ToSoftObjectPath());
		return false;
	}
	if (bApplyPresentationTransform)
	{
		const USkeletalMesh* LeaderMesh = Leader->GetSkeletalMeshAsset();
		if (!LeaderMesh || !Mesh->GetSkeleton()
			|| Mesh->GetSkeleton() != LeaderMesh->GetSkeleton())
		{
			UE_LOG(
				LogPEP,
				Warning,
				TEXT("Item '%s' requested Leader Pose, but its mesh and leader do not share the same skeleton."),
				*Entry.DefinitionId.ToString());
			return false;
		}
	}

	TObjectPtr<USkeletalMeshComponent>& Layer = LayeredMeshComponents.FindOrAdd(Entry.ItemId);
	if (!Layer)
	{
		Layer = NewObject<USkeletalMeshComponent>(GetOwner());
		Layer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Layer->SetGenerateOverlapEvents(false);
		Layer->SetCanEverAffectNavigation(false);

		/* Bounds come from the leader: a layer that computed its own would drive its own culling. */
		Layer->bUseBoundsFromLeaderPoseComponent = true;
		Layer->RegisterComponent();
		Layer->AttachToComponent(
			Leader,
			FAttachmentTransformRules::SnapToTargetIncludingScale);
	}

	Layer->SetSkeletalMesh(Mesh, false);
	Layer->SetRelativeTransform(
		bApplyPresentationTransform ? Presentation.MeshTransform : FTransform::Identity);

	/*
	 * The layer is skinned to the body's skeleton and never evaluates an animation of its own —
	 * it copies the leader's pose, which is what makes it deform with the character instead of
	 * riding on a bone like an attached prop.
	 */
	Layer->SetLeaderPoseComponent(Leader);

	FPEPLocalPresentationTarget& Target = LocalPresentationTargets.Add(Entry.ItemId);
	Target.ItemId = Entry.ItemId;
	Target.Policy = EPEPEquipmentPresentationPolicy::LayeredSkeletal;
	Target.VisualComponent = Layer;
	Target.SocketProvider = Layer;
	Target.Generation = LocalPresentationGeneration;
	return true;
}

bool UPEPEquipmentComponent::ApplyModularPresentation(
	const FPEPEquippedItemPublic& Entry,
	const FPEPEquipmentSlotDefinition& Slot,
	const FPEPEquipmentPresentationFragment& Presentation)
{
	const TObjectPtr<USceneComponent>* Registered =
		RegisteredPresentationTargets.Find(Slot.PresentationTargetTag);
	USkeletalMeshComponent* TargetComponent = Registered
		? Cast<USkeletalMeshComponent>(Registered->Get())
		: nullptr;
	if (!TargetComponent)
	{
		return false;
	}

	USkeletalMesh* Mesh = nullptr;
	if (!Presentation.IsExplicitlyInvisible())
	{
		const TSoftObjectPtr<USkeletalMesh>* SkeletalVisual =
			Presentation.GetSkeletalMeshVisual();
		if (!SkeletalVisual || SkeletalVisual->IsNull())
		{
			return false;
		}
		Mesh = SkeletalVisual->Get();
		if (!Mesh)
		{
			RequestPresentationAssetLoad(SkeletalVisual->ToSoftObjectPath());
			return false;
		}
	}

	TargetComponent->SetSkeletalMesh(Mesh, false);
	AppliedModularItems.Add(Slot.PresentationTargetTag, Entry.ItemId);

	FPEPLocalPresentationTarget& Target = LocalPresentationTargets.Add(Entry.ItemId);
	Target.ItemId = Entry.ItemId;
	Target.Policy = EPEPEquipmentPresentationPolicy::ModularSkeletal;
	Target.VisualComponent = TargetComponent;
	Target.SocketProvider = TargetComponent;
	Target.Generation = LocalPresentationGeneration;
	return true;
}

void UPEPEquipmentComponent::RequestDefinitionLoad(const FPrimaryAssetId DefinitionId)
{
	if (!DefinitionId.IsValid() || DefinitionLoadHandles.Contains(DefinitionId)
		|| FailedDefinitionLoads.Contains(DefinitionId))
	{
		return;
	}
	DefinitionLoadHandles.Add(
		DefinitionId,
		UAssetManager::Get().LoadPrimaryAsset(
			DefinitionId,
			TArray<FName>(),
			FStreamableDelegate::CreateWeakLambda(this, [this, DefinitionId]()
			{
				DefinitionLoadHandles.Remove(DefinitionId);
				if (ResolveLoadedDefinition(DefinitionId))
				{
					ReconcileLocalPresentations();
				}
				else
				{
					FailedDefinitionLoads.Add(DefinitionId);
				}
			})));
}

void UPEPEquipmentComponent::RequestPresentationAssetLoad(
	const FSoftObjectPath& AssetPath)
{
	if (!AssetPath.IsValid() || PresentationAssetLoadHandles.Contains(AssetPath)
		|| FailedPresentationAssetLoads.Contains(AssetPath))
	{
		return;
	}
	PresentationAssetLoadHandles.Add(
		AssetPath,
		UAssetManager::GetStreamableManager().RequestAsyncLoad(
			AssetPath,
			FStreamableDelegate::CreateWeakLambda(this, [this, AssetPath]()
			{
				PresentationAssetLoadHandles.Remove(AssetPath);
				if (AssetPath.ResolveObject())
				{
					ReconcileLocalPresentations();
				}
				else
				{
					FailedPresentationAssetLoads.Add(AssetPath);
				}
			})));
}

bool UPEPEquipmentComponent::ShowTemporaryPresentationLocal(
	const FName PresentationKey,
	const FPrimaryAssetId DefinitionId,
	const FName AttachSocket,
	const FPEPItemId SuppressedItemId)
{
	const FPEPItemId PreviousSuppressedItemId =
		TemporarySuppressedItems.FindRef(PresentationKey);
	const FPEPItemId EffectiveSuppressedItemId = SuppressedItemId.IsValid()
		? SuppressedItemId
		: PreviousSuppressedItemId;
	HideTemporaryPresentationLocal(PresentationKey, false);
	if (EffectiveSuppressedItemId.IsValid())
	{
		TemporarySuppressedItems.Add(PresentationKey, EffectiveSuppressedItemId);
		SuppressedPresentationItems.Add(EffectiveSuppressedItemId);
	}
	if (!bEnableLocalPresentation || !PresentationMesh || !DefinitionId.IsValid()
		|| PresentationKey.IsNone() || AttachSocket.IsNone() || !GetWorld()
		|| GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	const UPEPItemDefinition* Definition = ResolveLoadedDefinition(DefinitionId);
	if (!Definition)
	{
		TemporaryPresentationLoadHandles.Add(
			PresentationKey,
			UAssetManager::Get().LoadPrimaryAsset(
				DefinitionId,
				TArray<FName>(),
				FStreamableDelegate::CreateWeakLambda(
					this,
					[this, PresentationKey, DefinitionId, AttachSocket, EffectiveSuppressedItemId]()
					{
						TemporaryPresentationLoadHandles.Remove(PresentationKey);
						ShowTemporaryPresentationLocal(
							PresentationKey,
							DefinitionId,
							AttachSocket,
							EffectiveSuppressedItemId);
					})));
		return true;
	}

	const FPEPEquipmentPresentationFragment* Presentation =
		Definition->GetDefinitionFragment<FPEPEquipmentPresentationFragment>();
	if (!Presentation || !Presentation->HasVisual() || Presentation->IsExplicitlyInvisible())
	{
		return false;
	}

	UClass* CarrierClass = APEPEquippedPresentationActor::StaticClass();
	if (const TSoftClassPtr<APEPEquippedPresentationActor>* ActorVisual =
		Presentation->GetActorClassVisual())
	{
		CarrierClass = ActorVisual->Get();
		if (!CarrierClass)
		{
			TemporaryPresentationLoadHandles.Add(
				PresentationKey,
				UAssetManager::GetStreamableManager().RequestAsyncLoad(
					ActorVisual->ToSoftObjectPath(),
					FStreamableDelegate::CreateWeakLambda(
						this,
						[this, PresentationKey, DefinitionId, AttachSocket, EffectiveSuppressedItemId]()
						{
							TemporaryPresentationLoadHandles.Remove(PresentationKey);
							ShowTemporaryPresentationLocal(
								PresentationKey,
								DefinitionId,
								AttachSocket,
								EffectiveSuppressedItemId);
						})));
			return true;
		}
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = GetOwner();
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APEPEquippedPresentationActor* Actor = GetWorld()->SpawnActor<APEPEquippedPresentationActor>(
		CarrierClass,
		FTransform::Identity,
		SpawnParameters);
	if (!Actor)
	{
		return false;
	}

	Actor->AttachToComponent(
		PresentationMesh,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		AttachSocket);
	FPEPEquippedItemPublic TemporaryState;
	TemporaryState.DefinitionId = DefinitionId;
	Actor->InitializePresentation(TemporaryState, *Presentation);
	TemporaryPresentationActors.Add(PresentationKey, Actor);
	if (EffectiveSuppressedItemId.IsValid())
	{
		TemporarySuppressedItems.Add(PresentationKey, EffectiveSuppressedItemId);
		SuppressedPresentationItems.Add(EffectiveSuppressedItemId);
		// Rebuild the regular projection so the suppressed item and any visual children attached
		// to it disappear together. Temporary props are intentionally preserved by reconciliation.
		ReconcileLocalPresentations();
	}
	return true;
}

void UPEPEquipmentComponent::HideTemporaryPresentationLocal(
	const FName PresentationKey,
	const bool bRestoreSuppressedPresentation)
{
	bool bHadSuppressedPresentation = false;
	if (const FPEPItemId* SuppressedItemId = TemporarySuppressedItems.Find(PresentationKey))
	{
		SuppressedPresentationItems.Remove(*SuppressedItemId);
		TemporarySuppressedItems.Remove(PresentationKey);
		bHadSuppressedPresentation = true;
	}
	if (TSharedPtr<FStreamableHandle>* Handle =
		TemporaryPresentationLoadHandles.Find(PresentationKey))
	{
		if (Handle->IsValid())
		{
			(*Handle)->CancelHandle();
		}
		TemporaryPresentationLoadHandles.Remove(PresentationKey);
	}
	if (TObjectPtr<APEPEquippedPresentationActor>* Actor =
		TemporaryPresentationActors.Find(PresentationKey))
	{
		if (*Actor)
		{
			(*Actor)->Destroy();
		}
		TemporaryPresentationActors.Remove(PresentationKey);
	}
	if (bRestoreSuppressedPresentation && bHadSuppressedPresentation)
	{
		ReconcileLocalPresentations();
	}
}

void UPEPEquipmentComponent::DestroyAllTemporaryPresentations()
{
	for (const TPair<FName, TSharedPtr<FStreamableHandle>>& Pair :
		TemporaryPresentationLoadHandles)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->CancelHandle();
		}
	}
	TemporaryPresentationLoadHandles.Reset();
	for (const TPair<FName, TObjectPtr<APEPEquippedPresentationActor>>& Pair :
		TemporaryPresentationActors)
	{
		if (Pair.Value)
		{
			Pair.Value->Destroy();
		}
	}
	TemporaryPresentationActors.Reset();
	TemporarySuppressedItems.Reset();
	SuppressedPresentationItems.Reset();
}

void UPEPEquipmentComponent::DestroyAllLocalPresentations()
{
	for (const TPair<FPEPItemId, TObjectPtr<APEPEquippedPresentationActor>>& Pair : PresentationActors)
	{
		if (Pair.Value)
		{
			Pair.Value->Destroy();
		}
	}
	PresentationActors.Reset();
	for (const TPair<FGameplayTag, FPEPItemId>& Pair : AppliedModularItems)
	{
		const TObjectPtr<USceneComponent>* Registered =
			RegisteredPresentationTargets.Find(Pair.Key);
		if (USkeletalMeshComponent* Target = Registered
			? Cast<USkeletalMeshComponent>(Registered->Get())
			: nullptr)
		{
			Target->SetSkeletalMesh(ModularBaseMeshes.FindRef(Pair.Key), false);
		}
	}
	AppliedModularItems.Reset();

	/*
	 * Destroyed rather than emptied: a layer belongs to the item that brought it, so it goes with
	 * it. Clearing the mesh instead would leave a component per item ever equipped, invisible and
	 * accumulating for the life of the pawn.
	 */
	for (const TPair<FPEPItemId, TObjectPtr<USkeletalMeshComponent>>& Pair : LayeredMeshComponents)
	{
		if (Pair.Value)
		{
			Pair.Value->DestroyComponent();
		}
	}
	LayeredMeshComponents.Reset();
	LocalPresentationTargets.Reset();
	PresentedNodes.Reset();
}

void UPEPEquipmentComponent::DestroyLocalPresentation(const FPEPItemId ItemId)
{
	if (TObjectPtr<APEPEquippedPresentationActor>* Actor = PresentationActors.Find(ItemId))
	{
		if (*Actor)
		{
			(*Actor)->Destroy();
		}
		PresentationActors.Remove(ItemId);
	}
	if (TObjectPtr<USkeletalMeshComponent>* Layer = LayeredMeshComponents.Find(ItemId))
	{
		if (*Layer)
		{
			(*Layer)->DestroyComponent();
		}
		LayeredMeshComponents.Remove(ItemId);
	}
	for (TMap<FGameplayTag, FPEPItemId>::TIterator It(AppliedModularItems); It; ++It)
	{
		if (It.Value() != ItemId)
		{
			continue;
		}
		const TObjectPtr<USceneComponent>* Registered = RegisteredPresentationTargets.Find(It.Key());
		if (USkeletalMeshComponent* Target = Registered
			? Cast<USkeletalMeshComponent>(Registered->Get())
			: nullptr)
		{
			Target->SetSkeletalMesh(ModularBaseMeshes.FindRef(It.Key()), false);
		}
		It.RemoveCurrent();
	}
	LocalPresentationTargets.Remove(ItemId);
	PresentedNodes.Remove(ItemId);
}

bool UPEPEquipmentComponent::DoesChangeTouchEquipment(const FPEPItemStoreChange& Change) const
{
	const IPEPItemStore* ItemStore = GetItemStoreConst();
	const auto TouchesSlot = [this, ItemStore](FGameplayTag RootTag, const FPEPItemLocation& Location)
	{
		if (!RootTag.IsValid() && ItemStore)
		{
			RootTag = ResolveRootSlotTagViaStore(*ItemStore, Location);
		}
		return RootTag.IsValid() && FindSlotDefinition(RootTag) != nullptr;
	};
	switch (Change.ChangeType)
	{
	case EPEPItemStoreChangeType::Removed:
		return TouchesSlot(Change.PreviousRootSlotTag, Change.PreviousLocation);
	case EPEPItemStoreChangeType::Moved:
		return TouchesSlot(Change.PreviousRootSlotTag, Change.PreviousLocation)
			|| TouchesSlot(Change.CurrentRootSlotTag, Change.CurrentLocation);
	default:
		return TouchesSlot(Change.CurrentRootSlotTag, Change.CurrentLocation);
	}
}
