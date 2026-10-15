#include "Components/PEPEquipmentComponent.h"

#include "Algo/Sort.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/PEPCoreLogChannels.h"
#include "Core/PEPCoreTags.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Equipment/PEPEquipmentEligibilityProvider.h"
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

	/**
	 * Whether the store grants ItemId at least one of AccessTags. No tags means no requirement:
	 * Equipment owns no access vocabulary, so it asks only about the accesses it was given.
	 */
	bool HasAnyGrantedGameplayAccess(
		const IPEPItemStore& ItemStore,
		const FPEPItemId ItemId,
		const FGameplayTagContainer& AccessTags)
	{
		const TArray<FGameplayTag>& Tags = AccessTags.GetGameplayTagArray();
		if (Tags.IsEmpty())
		{
			return true;
		}
		for (const FGameplayTag& AccessTag : Tags)
		{
			if (ItemStore.HasGrantedGameplayAccess(ItemId, AccessTag))
			{
				return true;
			}
		}
		return false;
	}

	/** Order-independent equality of two views, whose ItemIds are unique. */
	bool AreSameEquippedNodes(
		const TArray<FPEPEquippedItemInfo>& Current,
		const TMap<FPEPItemId, int32>& CurrentIndex,
		const TArray<FPEPEquippedItemInfo>& Candidate)
	{
		if (Current.Num() != Candidate.Num())
		{
			return false;
		}
		for (const FPEPEquippedItemInfo& Node : Candidate)
		{
			const int32* Index = CurrentIndex.Find(Node.ItemId);
			if (!Index || !Current.IsValidIndex(*Index)
				|| !PEPEquipmentEvents::IsSameEquippedNode(Current[*Index], Node))
			{
				return false;
			}
		}
		return true;
	}

	/** A refusal always says why and names the item, whoever produced it. */
	FPEPItemResult NormalizeEligibilityFailure(FPEPItemResult Result, const FPEPItemId ItemId)
	{
		if (!Result.ReasonTag.IsValid())
		{
			Result.ReasonTag = PEPCoreTags::Reason_BlockedByState;
		}
		if (!Result.ItemId.IsValid())
		{
			Result.ItemId = ItemId;
		}
		return Result;
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

	/*
	 * TemporaryPresentationAccessTags and EquipAccessTags start empty: Equipment declares no
	 * gameplay access of its own, and a project names the accesses its store grants.
	 */

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
	InvalidateEligibilityProviders();
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

	/*
	 * Nothing local is built before this point (ReconcileLocalPresentations waits for BeginPlay),
	 * yet a late joiner's first bunch, or a store filled on the authority before BeginPlay, may
	 * already describe equipment. It is all built here, once, against the mesh just found rather
	 * than the root a visual would otherwise have fallen back to. Anything that slipped through
	 * is thrown away first.
	 */
	DestroyAllLocalPresentations();
	bool bReconciled = false;
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		/* Folds every store change made before BeginPlay into one rebuild, owner view included. */
		bReconciled = RefreshPublicProjection();
	}
	else
	{
		RebuildPublicEntryIndex();
		/* The owning client's store copy may already hold equipment; a proxy's view stays empty. */
		RefreshOwnerView();
	}
	if (!bReconciled)
	{
		ReconcileLocalPresentations();
	}
	ApplyTemporaryPresentations();

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(
			this,
			&UPEPEquipmentComponent::HandleOwnerControllerChanged);
	}

	if (bPublicProjectionDirty || bOwnerViewDirty || bLocalPresentationReconcilePending
		|| !PendingLocalResolutions.IsEmpty())
	{
		ScheduleDeferredUpdates();
	}
}

void UPEPEquipmentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(
			this,
			&UPEPEquipmentComponent::HandleOwnerControllerChanged);
	}
	UninitializeItemStoreIntegration();
	InvalidateEligibilityProviders();
	CancelDeferredUpdates();
	PendingLocalResolutions.Reset();
	bLocalPresentationReconcilePending = false;

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
		/* Before BeginPlay, BeginPlay builds the views from whatever store is set by then. */
		bPublicProjectionDirty = true;
		bOwnerViewDirty = true;
		if (HasBegunPlay())
		{
			RefreshDirtyEquipmentViews();
		}
	}
	return true;
}

void UPEPEquipmentComponent::SetPresentationMesh(USkeletalMeshComponent* InPresentationMesh)
{
	const bool bMeshChanged = PresentationMesh != InPresentationMesh;
	PresentationMesh = InPresentationMesh;

	bool bTargetChanged = false;
	if (InPresentationMesh)
	{
		RegisterPresentationTargetInternal(
			PEPEquipmentTags::Equipment_PresentationTarget_Body_Main,
			InPresentationMesh,
			bTargetChanged);
	}

	/* One rebuild for both: attached roots fall back to the mesh, modular parts use the target. */
	if (bMeshChanged || bTargetChanged)
	{
		RebuildLocalPresentations();
	}
	/* Temporary props hang off the mesh itself. */
	if (bMeshChanged)
	{
		RebuildTemporaryPresentations();
	}
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
	/* Setup only, so before BeginPlay: BeginPlay builds the views and the visuals from them. */
	bPublicProjectionDirty = true;
	bOwnerViewDirty = true;
	RebuildLocalPresentations();
}

bool UPEPEquipmentComponent::RegisterPresentationTarget(
	const FGameplayTag TargetTag,
	USceneComponent* Component)
{
	bool bChanged = false;
	if (!RegisterPresentationTargetInternal(TargetTag, Component, bChanged))
	{
		return false;
	}
	/* A target moved under every visual that used it: the only honest answer is a rebuild. */
	if (bChanged)
	{
		RebuildLocalPresentations();
	}
	return true;
}

bool UPEPEquipmentComponent::RegisterPresentationTargetInternal(
	const FGameplayTag TargetTag,
	USceneComponent* Component,
	bool& bOutChanged)
{
	bOutChanged = false;
	if (!TargetTag.IsValid() || !Component
		|| (GetOwner() && Component->GetOwner() != GetOwner()))
	{
		return false;
	}
	if (const TObjectPtr<USceneComponent>* Existing =
		RegisteredPresentationTargets.Find(TargetTag))
	{
		if (Existing->Get() == Component)
		{
			/* Already there: nothing moved, nothing to rebuild. */
			return true;
		}
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
	bOutChanged = true;
	return true;
}

void UPEPEquipmentComponent::UnregisterPresentationTarget(const FGameplayTag TargetTag)
{
	const TObjectPtr<USceneComponent>* Registered = RegisteredPresentationTargets.Find(TargetTag);
	if (!Registered)
	{
		return;
	}
	if (USkeletalMeshComponent* SkeletalTarget = Cast<USkeletalMeshComponent>(Registered->Get()))
	{
		SkeletalTarget->SetSkeletalMesh(ModularBaseMeshes.FindRef(TargetTag), false);
	}
	RegisteredPresentationTargets.Remove(TargetTag);
	ModularBaseMeshes.Remove(TargetTag);
	AppliedModularItems.Remove(TargetTag);
	RebuildLocalPresentations();
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
	if (bEnableLocalPresentation == bEnabled)
	{
		return;
	}
	bEnableLocalPresentation = bEnabled;
	ReconcileLocalPresentations();
	RebuildTemporaryPresentations();
}

bool UPEPEquipmentComponent::RequestShowTemporaryItemPresentation(
	const FPEPItemId ItemId,
	const FName PresentationKey,
	const FName AttachSocket,
	const bool bHideOriginalPresentation)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}
	const bool bAuthority = Owner->HasAuthority();
	if (!bAuthority && !Owner->HasLocalNetOwner())
	{
		return false;
	}

	FPrimaryAssetId DefinitionId;
	FPEPItemId SuppressedItemId;
	if (!ValidateTemporaryPresentationRequest(
		ItemId,
		PresentationKey,
		AttachSocket,
		bHideOriginalPresentation,
		DefinitionId,
		SuppressedItemId))
	{
		return false;
	}
	if (bAuthority)
	{
		SetTemporaryPresentationState(
			PresentationKey,
			DefinitionId,
			AttachSocket,
			SuppressedItemId);
		return true;
	}

	/*
	 * Immediate owner feedback, recorded as applied so the replicated state does not rebuild it.
	 * Before BeginPlay there is nothing to show it on yet; the replicated entry builds it then.
	 */
	if (HasBegunPlay())
	{
		FPEPTemporaryPresentationEntry& Applied =
			AppliedTemporaryPresentations.FindOrAdd(PresentationKey);
		Applied.PresentationKey = PresentationKey;
		Applied.DefinitionId = DefinitionId;
		Applied.AttachSocket = AttachSocket;
		Applied.SuppressedItemId = SuppressedItemId;
		ShowTemporaryPresentationLocal(
			PresentationKey,
			DefinitionId,
			AttachSocket,
			SuppressedItemId);
	}
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
	 * A reliable RPC the owning client may call as often as it likes: every refusal is silent and
	 * cheap, the key cap first and the store walk last. Nothing is written for a refused request,
	 * so a spamming client costs one validation per call and never grows the replicated view.
	 */
	FPrimaryAssetId DefinitionId;
	FPEPItemId SuppressedItemId;
	if (ValidateTemporaryPresentationRequest(
		ItemId,
		PresentationKey,
		AttachSocket,
		bHideOriginalPresentation,
		DefinitionId,
		SuppressedItemId))
	{
		SetTemporaryPresentationState(
			PresentationKey,
			DefinitionId,
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

bool UPEPEquipmentComponent::ValidateTemporaryPresentationRequest(
	const FPEPItemId ItemId,
	const FName PresentationKey,
	const FName AttachSocket,
	const bool bHideOriginalPresentation,
	FPrimaryAssetId& OutDefinitionId,
	FPEPItemId& OutSuppressedItemId) const
{
	OutDefinitionId = FPrimaryAssetId();
	OutSuppressedItemId = FPEPItemId();
	if (!ItemId.IsValid() || PresentationKey.IsNone() || AttachSocket.IsNone()
		|| !HasTemporaryPresentationRoom(PresentationKey))
	{
		return false;
	}
	/*
	 * The socket name is client-chosen and replicated to every client. Checked wherever the mesh
	 * is known, so a bogus name is dropped. A server with no mesh (a dedicated server whose pawn
	 * class strips it) cannot check and lets it through.
	 */
	if (PresentationMesh && !PresentationMesh->DoesSocketExist(AttachSocket))
	{
		return false;
	}
	const IPEPItemStore* ItemStore = GetItemStoreConst();
	const FPEPItemInstance* Item = ItemStore ? ItemStore->FindItem(ItemId) : nullptr;
	if (!Item || !HasTemporaryPresentationAccess(*ItemStore, ItemId))
	{
		return false;
	}
	OutDefinitionId = Item->DefinitionId;
	OutSuppressedItemId = bHideOriginalPresentation && FindPublicEntry(ItemId)
		? ItemId
		: FPEPItemId();
	return true;
}

bool UPEPEquipmentComponent::HasTemporaryPresentationRoom(const FName PresentationKey) const
{
	const AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		return FindTemporaryPresentationEntry(PresentationKey) != nullptr
			|| TemporaryPresentationView.Entries.Num() < MaxTemporaryPresentations;
	}
	/* The owning client counts what it shows, its own pending requests included. */
	return AppliedTemporaryPresentations.Contains(PresentationKey)
		|| AppliedTemporaryPresentations.Num() < MaxTemporaryPresentations;
}

bool UPEPEquipmentComponent::HasTemporaryPresentationAccess(
	const IPEPItemStore& ItemStore,
	const FPEPItemId ItemId) const
{
	return HasAnyGrantedGameplayAccess(ItemStore, ItemId, TemporaryPresentationAccessTags);
}

bool UPEPEquipmentComponent::HasEquipAccess(
	const IPEPItemStore& ItemStore,
	const FPEPItemId ItemId) const
{
	return HasAnyGrantedGameplayAccess(ItemStore, ItemId, EquipAccessTags);
}

const FPEPTemporaryPresentationEntry* UPEPEquipmentComponent::FindTemporaryPresentationEntry(
	const FName PresentationKey) const
{
	return TemporaryPresentationView.Entries.FindByPredicate(
		[PresentationKey](const FPEPTemporaryPresentationEntry& Candidate)
		{
			return Candidate.PresentationKey == PresentationKey;
		});
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
	/*
	 * Not before BeginPlay: there is no mesh to hang a prop on yet, and recording the entry as
	 * applied would keep BeginPlay from ever building it. BeginPlay applies the view.
	 */
	if (!HasBegunPlay())
	{
		return;
	}
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

	/*
	 * Setup problems are reported only once play has begun in a game world. OnRegister also runs
	 * in editor and preview worlds, and before the owner's other components are all there; a
	 * missing store at that point means nothing yet, and warning about it was noise.
	 */
	const UWorld* World = GetWorld();
	const bool bReportSetupProblems = HasBegunPlay() && World && World->IsGameWorld();

	IPEPItemStore* ItemStore = GetItemStore();
	if (!ItemStore)
	{
		/*
		 * Said out loud, because the consequence surfaces nowhere near here: an equipment slot only
		 * means something once this has claimed its tag, so a store that never got the claim
		 * reports every equip as an unsupported location — in another plugin, with no mention of
		 * equipment. Declining silently made that a very long way to find.
		 */
		if (bReportSetupProblems)
		{
			UE_LOG(
				LogPEP,
				Warning,
				TEXT("%s: no item store found on '%s'. No equipment slot will accept anything until "
					"SetItemStore names one, or the owner carries a component implementing IPEPItemStore."),
				*GetName(),
				GetOwner() ? *GetOwner()->GetName() : TEXT("<no owner>"));
		}
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

	if (RegisteredSlots == 0 && bReportSetupProblems)
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
	/*
	 * The store calls this for every write into one of the claimed slots — an equip request, a
	 * swap's displaced item, a drag from an inventory UI, an automatic pickup — so the full rule
	 * lives here and not only behind RequestEquipItem.
	 */
	return CheckEquipEligibility(Item, Location.LocationTag);
}

FPEPItemResult UPEPEquipmentComponent::CheckEquipEligibility(
	const FPEPItemInstance& Item,
	const FGameplayTag SlotTag) const
{
	const FPEPEquipmentSlotDefinition* Slot = FindSlotDefinition(SlotTag);
	if (!Slot)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, Item.ItemId);
	}
	if (!PEPItemQuery::DoesItemMatchRequiredBlockedTags(
		Item,
		Slot->RequiredItemTags,
		Slot->BlockedItemTags))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, Item.ItemId);
	}

	/* Never loads: eligibility runs inside store writes and on clients for previews. */
	const UPEPItemDefinition* Definition = ResolveLoadedDefinition(Item.DefinitionId);
	const FPEPItemResult HookResult = CanEquipItemInSlot(Item, Definition, SlotTag);
	if (!HookResult.bSuccess)
	{
		return NormalizeEligibilityFailure(HookResult, Item.ItemId);
	}

	TArray<const UObject*> Providers;
	GatherEligibilityProviders(Providers);
	for (const UObject* Provider : Providers)
	{
		const FPEPItemResult ProviderResult = IPEPEquipmentEligibilityProvider::Execute_CanEquipItem(
			Provider,
			this,
			Item,
			Definition,
			SlotTag);
		if (!ProviderResult.bSuccess)
		{
			return NormalizeEligibilityFailure(ProviderResult, Item.ItemId);
		}
	}
	return FPEPItemResult::Success(Item.ItemId);
}

FPEPItemResult UPEPEquipmentComponent::CanEquipItemInSlot_Implementation(
	const FPEPItemInstance& Item,
	const UPEPItemDefinition* Definition,
	const FGameplayTag SlotTag) const
{
	(void)Definition;
	(void)SlotTag;
	return FPEPItemResult::Success(Item.ItemId);
}

void UPEPEquipmentComponent::InvalidateEligibilityProviders()
{
	CachedEligibilityProviders.Reset();
	CachedEligibilityProviderOwner.Reset();
	CachedEligibilityComponentCount = INDEX_NONE;
}

bool UPEPEquipmentComponent::IsEligibilityProviderCacheCurrent(const AActor& Owner) const
{
	/*
	 * Cheap enough for every store write: a pointer, a count and one weak lookup per provider —
	 * usually none or one. A component added or removed moves the count; a provider destroyed
	 * goes stale. A same-frame swap that keeps the count and spares every provider is the one
	 * change this cannot see, which is what InvalidateEligibilityProviders is for.
	 */
	if (CachedEligibilityProviderOwner.Get() != &Owner
		|| CachedEligibilityComponentCount != Owner.GetComponents().Num())
	{
		return false;
	}
	for (const TWeakObjectPtr<const UObject>& Provider : CachedEligibilityProviders)
	{
		if (!Provider.IsValid())
		{
			return false;
		}
	}
	return true;
}

void UPEPEquipmentComponent::GatherEligibilityProviders(
	TArray<const UObject*>& OutProviders) const
{
	OutProviders.Reset();
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (!IsEligibilityProviderCacheCurrent(*Owner))
	{
		CachedEligibilityProviders.Reset();
		if (Owner->GetClass()->ImplementsInterface(UPEPEquipmentEligibilityProvider::StaticClass()))
		{
			const UObject* OwnerProvider = Owner;
			CachedEligibilityProviders.Emplace(OwnerProvider);
		}

		/*
		 * Filtered first, so the sort only ever sees the few components that answer — usually
		 * none or one, which need no sorting at all.
		 */
		TInlineComponentArray<UActorComponent*> Components(Owner);
		TArray<const UObject*, TInlineAllocator<4>> ComponentProviders;
		for (const UActorComponent* Component : Components)
		{
			if (Component
				&& Component->GetClass()->ImplementsInterface(
					UPEPEquipmentEligibilityProvider::StaticClass()))
			{
				ComponentProviders.Add(Component);
			}
		}
		/* Component storage order is a set's; the name gives one first failure per configuration. */
		if (ComponentProviders.Num() > 1)
		{
			Algo::Sort(ComponentProviders, [](const UObject* A, const UObject* B)
			{
				return A->GetFName().LexicalLess(B->GetFName());
			});
		}
		for (const UObject* ComponentProvider : ComponentProviders)
		{
			CachedEligibilityProviders.Emplace(ComponentProvider);
		}
		CachedEligibilityProviderOwner = Owner;
		CachedEligibilityComponentCount = Owner->GetComponents().Num();
	}

	OutProviders.Reserve(CachedEligibilityProviders.Num());
	for (const TWeakObjectPtr<const UObject>& Provider : CachedEligibilityProviders)
	{
		if (const UObject* Resolved = Provider.Get())
		{
			OutProviders.Add(Resolved);
		}
	}
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
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	/*
	 * The authority rebuilds both views from one flag. Any other machine only ever builds its owner
	 * view, from the store copy the owning client receives (PEPInventory replicates owner-only and
	 * raises these events on that copy). A simulated proxy's copy is empty and raises nothing; it
	 * is not filtered out here, so a copy that starts arriving just before the role does is not
	 * missed. Already dirty: the pending rebuild reads the store as it will be then.
	 */
	const bool bAuthority = Owner->HasAuthority();
	if (bAuthority ? bPublicProjectionDirty : bOwnerViewDirty)
	{
		return;
	}
	if (Change.ChangeType == EPEPItemStoreChangeType::StateChanged)
	{
		/*
		 * The item stayed where it was. The views carry no item state, and which children are
		 * shown is the store's decision by location (IPEPItemStore::GetPublicChildren), so the only
		 * state change that can show is a viewed node changing definition. Charges, ammo and
		 * durability ticking on an equipped item rebuild nothing.
		 */
		const FPrimaryAssetId DefinitionId = Change.ItemInstance.DefinitionId;
		const FPEPEquippedItemPublic* PublicNode = bAuthority ? FindPublicEntry(Change.ItemId) : nullptr;
		const FPEPEquippedItemInfo* OwnerNode = FindOwnerViewNode(Change.ItemId);
		if ((PublicNode && PublicNode->DefinitionId != DefinitionId)
			|| (OwnerNode && OwnerNode->DefinitionId != DefinitionId))
		{
			MarkEquipmentViewsDirty();
		}
		return;
	}
	// A bandage moving between two pockets never reaches either view; only an item under one of
	// this component's slots, hidden or not, before or after the change, can.
	if (DoesChangeTouchEquipment(Change))
	{
		MarkEquipmentViewsDirty();
	}
}

void UPEPEquipmentComponent::MarkEquipmentViewsDirty()
{
	const AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		/* One rebuild covers both views on the authority. */
		bPublicProjectionDirty = true;
	}
	else
	{
		bOwnerViewDirty = true;
	}
	/* Inside one of this component's own writes, the end of the batch rebuilds instead. */
	if (ProjectionBatchDepth == 0)
	{
		ScheduleDeferredUpdates();
	}
}

void UPEPEquipmentComponent::RefreshDirtyEquipmentViews()
{
	const AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		if (bPublicProjectionDirty || bOwnerViewDirty)
		{
			/* Reconciles the visuals itself when the projection changed. */
			RefreshPublicProjection();
		}
		return;
	}
	/* The projection is the server's to build: a client only ever rebuilds its owner view. */
	bPublicProjectionDirty = false;
	if (bOwnerViewDirty)
	{
		RefreshOwnerView();
	}
}

void UPEPEquipmentComponent::HandleOwnerControllerChanged(
	APawn* Pawn,
	AController* OldController,
	AController* NewController)
{
	(void)Pawn;
	(void)OldController;
	(void)NewController;
	/*
	 * The authority's owner view does not depend on who drives the pawn. A client's does: next
	 * tick, since the new role can land in the same bunch as the controller, after this callback.
	 */
	const AActor* Owner = GetOwner();
	if (Owner && !Owner->HasAuthority())
	{
		bOwnerViewDirty = true;
		ScheduleDeferredUpdates();
	}
}

void UPEPEquipmentComponent::BeginProjectionBatch()
{
	++ProjectionBatchDepth;
}

void UPEPEquipmentComponent::EndProjectionBatch()
{
	if (ProjectionBatchDepth <= 0)
	{
		ProjectionBatchDepth = 0;
		return;
	}
	--ProjectionBatchDepth;
	/* Before BeginPlay the views wait for BeginPlay, like any other store change. */
	if (ProjectionBatchDepth == 0 && HasBegunPlay())
	{
		RefreshDirtyEquipmentViews();
	}
}

bool UPEPEquipmentComponent::RefreshPublicProjection()
{
	bPublicProjectionDirty = false;
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	/* The owner view is rebuilt by this same walk. */
	bOwnerViewDirty = false;

	/*
	 * One walk of the store feeds both views: the owner view takes every slot, the projection
	 * only the slots shown publicly. A hidden slot projects nothing, so nothing public (visuals,
	 * OnItemEquipped) sees it; the owner view, and what is built on it, does.
	 */
	TArray<FPEPEquippedItemInfo> OwnerNodes;
	TArray<FPEPEquippedItemInfo> PublicNodes;
	if (const IPEPItemStore* ItemStore = GetItemStoreConst())
	{
		CollectEquippedNodes(*ItemStore, OwnerNodes, &PublicNodes);
	}

	TMap<FPEPItemId, FPEPEquippedItemPublic> DesiredEntries;
	DesiredEntries.Reserve(PublicNodes.Num());
	for (const FPEPEquippedItemInfo& Node : PublicNodes)
	{
		FPEPEquippedItemPublic PublicEntry;
		PublicEntry.ItemId = Node.ItemId;
		PublicEntry.DefinitionId = Node.DefinitionId;
		PublicEntry.SlotTag = Node.SlotTag;
		PublicEntry.ParentItemId = Node.ParentItemId;
		PublicEntry.ParentSlotName = Node.ParentSlotName;
		DesiredEntries.Add(PublicEntry.ItemId, PublicEntry);
	}

	/*
	 * Both views are current before anything is announced, so a listener of either set of
	 * events reads the other one already rebuilt.
	 */
	const bool bOwnerViewChanged = ApplyOwnerViewNodes(MoveTemp(OwnerNodes));

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
	/* Removal swapped entries around; additions below keep the index current as they go. */
	RebuildPublicEntryIndex();

	for (const TPair<FPEPItemId, FPEPEquippedItemPublic>& Pair : DesiredEntries)
	{
		FPEPEquippedItemPublic* Existing = FindMutablePublicEntry(Pair.Key);
		if (!Existing)
		{
			const int32 AddedIndex = PublicEquipmentView.Entries.Add(Pair.Value);
			PublicEntryIndex.Add(Pair.Key, AddedIndex);
			PublicEquipmentView.MarkItemDirty(PublicEquipmentView.Entries[AddedIndex]);
			bProjectionChanged = true;
			continue;
		}

		if (Existing->DefinitionId != Pair.Value.DefinitionId
			|| Existing->SlotTag != Pair.Value.SlotTag
			|| Existing->ParentItemId != Pair.Value.ParentItemId
			|| Existing->ParentSlotName != Pair.Value.ParentSlotName)
		{
			Existing->DefinitionId = Pair.Value.DefinitionId;
			Existing->SlotTag = Pair.Value.SlotTag;
			Existing->ParentItemId = Pair.Value.ParentItemId;
			Existing->ParentSlotName = Pair.Value.ParentSlotName;
			PublicEquipmentView.MarkItemDirty(*Existing);
			bProjectionChanged = true;
		}
	}

	if (bProjectionChanged)
	{
		if (bRemovedEntry)
		{
			PublicEquipmentView.MarkArrayDirty();
		}

		ReconcileLocalPresentations();
		BroadcastEquipmentItemChanges();
		OnPublicEquipmentChanged.Broadcast();
		if (AActor* Owner = GetOwner())
		{
			Owner->ForceNetUpdate();
		}
	}
	/* After the public events: the owner view only adds what the projection leaves out. */
	if (bOwnerViewChanged)
	{
		BroadcastOwnerItemChanges();
		OnOwnerEquipmentChanged.Broadcast();
	}
	return bProjectionChanged;
}

bool UPEPEquipmentComponent::RefreshOwnerView()
{
	bOwnerViewDirty = false;
	TArray<FPEPEquippedItemInfo> OwnerNodes;
	/* Where no owner view is kept (a simulated proxy, a client that lost the pawn) it is empty. */
	if (HasOwnerEquipmentView())
	{
		if (const IPEPItemStore* ItemStore = GetItemStoreConst())
		{
			CollectEquippedNodes(*ItemStore, OwnerNodes, nullptr);
		}
	}
	if (!ApplyOwnerViewNodes(MoveTemp(OwnerNodes)))
	{
		return false;
	}
	BroadcastOwnerItemChanges();
	OnOwnerEquipmentChanged.Broadcast();
	return true;
}

bool UPEPEquipmentComponent::ApplyOwnerViewNodes(TArray<FPEPEquippedItemInfo>&& Nodes)
{
	/*
	 * Compared as sets: a store may list children in another order from one walk to the next,
	 * which is no change to announce.
	 */
	if (AreSameEquippedNodes(OwnerViewNodes, OwnerViewIndex, Nodes))
	{
		return false;
	}
	OwnerViewNodes = MoveTemp(Nodes);
	OwnerViewIndex.Reset();
	OwnerViewIndex.Reserve(OwnerViewNodes.Num());
	for (int32 Index = 0; Index < OwnerViewNodes.Num(); ++Index)
	{
		OwnerViewIndex.Add(OwnerViewNodes[Index].ItemId, Index);
	}
	return true;
}

void UPEPEquipmentComponent::BroadcastOwnerItemChanges()
{
	/* Same re-entrancy rule as the public events: a nested change gets its own, later pass. */
	if (bBroadcastingOwnerItemChanges)
	{
		bOwnerItemChangesPending = true;
		return;
	}
	TGuardValue<bool> BroadcastGuard(bBroadcastingOwnerItemChanges, true);
	do
	{
		bOwnerItemChangesPending = false;
		TArray<FPEPEquippedItemInfo> Previous = MoveTemp(AnnouncedOwnerNodes);
		AnnouncedOwnerNodes = OwnerViewNodes;

		TArray<FPEPEquipmentChange> Unequipped;
		TArray<FPEPEquipmentChange> Equipped;
		PEPEquipmentEvents::DiffPublicProjection(
			Previous,
			AnnouncedOwnerNodes,
			Unequipped,
			Equipped);
		for (const FPEPEquipmentChange& Change : Unequipped)
		{
			OnOwnerItemUnequipped.Broadcast(Change);
		}
		for (const FPEPEquipmentChange& Change : Equipped)
		{
			OnOwnerItemEquipped.Broadcast(Change);
		}
	}
	while (bOwnerItemChangesPending);
}

void UPEPEquipmentComponent::BroadcastEquipmentItemChanges()
{
	/*
	 * A listener may itself change equipment — unequip the offhand when a two-hander lands. Its
	 * nested change is folded into another pass once this one has finished announcing, so no
	 * listener sees an Equipped of the new state in the middle of the old state's Unequipped.
	 */
	if (bBroadcastingEquipmentItemChanges)
	{
		bEquipmentItemChangesPending = true;
		return;
	}
	TGuardValue<bool> BroadcastGuard(bBroadcastingEquipmentItemChanges, true);
	do
	{
		bEquipmentItemChangesPending = false;
		TArray<FPEPEquippedItemInfo> Previous = MoveTemp(AnnouncedEquipmentNodes);
		AnnouncedEquipmentNodes.Reset();
		AnnouncedEquipmentNodes.Reserve(PublicEquipmentView.Entries.Num());
		for (const FPEPEquippedItemPublic& Entry : PublicEquipmentView.Entries)
		{
			AnnouncedEquipmentNodes.Add(Entry.ToInfo());
		}

		TArray<FPEPEquipmentChange> Unequipped;
		TArray<FPEPEquipmentChange> Equipped;
		PEPEquipmentEvents::DiffPublicProjection(
			Previous,
			AnnouncedEquipmentNodes,
			Unequipped,
			Equipped);
		for (const FPEPEquipmentChange& Change : Unequipped)
		{
			OnItemUnequipped.Broadcast(Change);
		}
		for (const FPEPEquipmentChange& Change : Equipped)
		{
			OnItemEquipped.Broadcast(Change);
		}
	}
	while (bEquipmentItemChangesPending);
}

void UPEPEquipmentComponent::CollectEquippedNodes(
	const IPEPItemStore& ItemStore,
	TArray<FPEPEquippedItemInfo>& OutOwnerNodes,
	TArray<FPEPEquippedItemInfo>* OutPublicNodes) const
{
	TSet<FPEPItemId> VisitedItemIds;
	for (const FPEPEquipmentSlotDefinition& Slot : GetEffectiveSlotDefinitions())
	{
		if (!Slot.IsValid())
		{
			continue;
		}

		FPEPItemId ItemId;
		if (!ItemStore.FindItemAtLocation(FPEPItemLocation::MakeNamedSlot(Slot.SlotTag), ItemId))
		{
			continue;
		}
		const FPEPItemInstance* Item = ItemStore.FindItem(ItemId);
		if (!Item || !Item->ItemId.IsValid() || VisitedItemIds.Contains(Item->ItemId))
		{
			continue;
		}

		const int32 FirstNodeIndex = OutOwnerNodes.Num();
		VisitedItemIds.Add(Item->ItemId);
		FPEPEquippedItemInfo& Root = OutOwnerNodes.AddDefaulted_GetRef();
		Root.ItemId = Item->ItemId;
		Root.DefinitionId = Item->DefinitionId;
		Root.SlotTag = Slot.SlotTag;
		AppendAttachmentNodes(ItemStore, Item->ItemId, OutOwnerNodes, VisitedItemIds);

		if (OutPublicNodes && Slot.bShowPublicly)
		{
			for (int32 Index = FirstNodeIndex; Index < OutOwnerNodes.Num(); ++Index)
			{
				OutPublicNodes->Add(OutOwnerNodes[Index]);
			}
		}
	}
}

void UPEPEquipmentComponent::AppendAttachmentNodes(
	const IPEPItemStore& ItemStore,
	const FPEPItemId ParentItemId,
	TArray<FPEPEquippedItemInfo>& OutNodes,
	TSet<FPEPItemId>& VisitedItemIds) const
{
	/*
	 * Which children are shown is the store's knowledge, not a container vocabulary read here.
	 * The owner view follows the same rule as the projection: an attachment, never the private
	 * content of a container, which is an inventory's business.
	 */
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
		/* Copied out before recursing: the recursion grows OutNodes and may move its storage. */
		FPEPEquippedItemInfo ChildNode;
		ChildNode.ItemId = ChildItemId;
		ChildNode.DefinitionId = ChildItem->DefinitionId;
		ChildNode.ParentItemId = ParentItemId;
		ChildNode.ParentSlotName = ChildLocation.ContainerSlotName;
		OutNodes.Add(ChildNode);

		AppendAttachmentNodes(ItemStore, ChildItemId, OutNodes, VisitedItemIds);
	}
}

const FPEPEquippedItemInfo* UPEPEquipmentComponent::FindOwnerViewNode(const FPEPItemId ItemId) const
{
	const int32* Index = ItemId.IsValid() ? OwnerViewIndex.Find(ItemId) : nullptr;
	return Index && OwnerViewNodes.IsValidIndex(*Index) && OwnerViewNodes[*Index].ItemId == ItemId
		? &OwnerViewNodes[*Index]
		: nullptr;
}

const FPEPEquippedItemPublic* UPEPEquipmentComponent::FindPublicEntry(const FPEPItemId ItemId) const
{
	if (!ItemId.IsValid())
	{
		return nullptr;
	}
	const TArray<FPEPEquippedItemPublic>& Entries = PublicEquipmentView.Entries;
	const int32* Index = PublicEntryIndex.Find(ItemId);
	if (Index && Entries.IsValidIndex(*Index) && Entries[*Index].ItemId == ItemId)
	{
		return &Entries[*Index];
	}
	/*
	 * The index is rebuilt with every change to the array — by the authority's rebuild, and by
	 * each replicated receive before anything reads it — so when it covers every entry a miss is
	 * a miss. Out of step (a duplicated instance whose index was not copied), it answers by scan.
	 */
	if (!Index && PublicEntryIndex.Num() == Entries.Num())
	{
		return nullptr;
	}
	return Entries.FindByPredicate(
		[ItemId](const FPEPEquippedItemPublic& Entry)
		{
			return Entry.ItemId == ItemId;
		});
}

FPEPEquippedItemPublic* UPEPEquipmentComponent::FindMutablePublicEntry(const FPEPItemId ItemId)
{
	return const_cast<FPEPEquippedItemPublic*>(
		static_cast<const UPEPEquipmentComponent*>(this)->FindPublicEntry(ItemId));
}

const FPEPEquippedItemPublic* UPEPEquipmentComponent::FindPublicRootInSlot(
	const FGameplayTag SlotTag) const
{
	if (!SlotTag.IsValid())
	{
		return nullptr;
	}
	return PublicEquipmentView.Entries.FindByPredicate(
		[SlotTag](const FPEPEquippedItemPublic& Entry)
		{
			return !Entry.IsAttachment() && Entry.SlotTag == SlotTag;
		});
}

void UPEPEquipmentComponent::RebuildPublicEntryIndex()
{
	PublicEntryIndex.Reset();
	PublicEntryIndex.Reserve(PublicEquipmentView.Entries.Num());
	for (int32 Index = 0; Index < PublicEquipmentView.Entries.Num(); ++Index)
	{
		PublicEntryIndex.Add(PublicEquipmentView.Entries[Index].ItemId, Index);
	}
}

TArray<FPEPEquippedItemInfo> UPEPEquipmentComponent::GetPublicPresentationNodes() const
{
	TArray<FPEPEquippedItemInfo> Nodes;
	Nodes.Reserve(PublicEquipmentView.Entries.Num());
	for (const FPEPEquippedItemPublic& Entry : PublicEquipmentView.Entries)
	{
		Nodes.Add(Entry.ToInfo());
	}
	return Nodes;
}

TArray<FPEPEquippedItemInfo> UPEPEquipmentComponent::GetPublicEquipmentRoots() const
{
	TArray<FPEPEquippedItemInfo> Roots;
	for (const FPEPEquippedItemPublic& Entry : PublicEquipmentView.Entries)
	{
		if (!Entry.IsAttachment())
		{
			Roots.Add(Entry.ToInfo());
		}
	}
	return Roots;
}

TArray<FPEPEquippedItemInfo> UPEPEquipmentComponent::GetPublicAttachments(
	const FPEPItemId ParentItemId,
	const bool bRecursive) const
{
	TArray<FPEPEquippedItemInfo> Result;
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
			Result.Add(Entry.ToInfo());
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
	FPEPEquippedItemInfo& OutEntry) const
{
	const FPEPEquippedItemPublic* Entry = FindPublicEntry(ItemId);
	OutEntry = Entry ? Entry->ToInfo() : FPEPEquippedItemInfo();
	return Entry != nullptr;
}

bool UPEPEquipmentComponent::FindItemInHand(
	const EPEPEquipmentHand Hand,
	FPEPEquippedItemInfo& OutEntry) const
{
	OutEntry = FPEPEquippedItemInfo();
	if (Hand == EPEPEquipmentHand::None)
	{
		return false;
	}
	/*
	 * Profile order, not the order of the replicated array, which differs between machines: the
	 * first slot declaring this hand that holds a public root wins, identically everywhere.
	 */
	for (const FPEPEquipmentSlotDefinition& Slot : GetEffectiveSlotDefinitions())
	{
		if (Slot.Hand != Hand)
		{
			continue;
		}
		if (const FPEPEquippedItemPublic* Root = FindPublicRootInSlot(Slot.SlotTag))
		{
			OutEntry = Root->ToInfo();
			return true;
		}
	}
	return false;
}

EPEPEquipmentHand UPEPEquipmentComponent::GetSlotHand(const FGameplayTag SlotTag) const
{
	const FPEPEquipmentSlotDefinition* Slot = FindSlotDefinition(SlotTag);
	return Slot ? Slot->Hand : EPEPEquipmentHand::None;
}

TArray<FGameplayTag> UPEPEquipmentComponent::GetEquipmentSlotTags() const
{
	TArray<FGameplayTag> SlotTags;
	for (const FPEPEquipmentSlotDefinition& Slot : GetEffectiveSlotDefinitions())
	{
		if (Slot.IsValid())
		{
			SlotTags.AddUnique(Slot.SlotTag);
		}
	}
	return SlotTags;
}

bool UPEPEquipmentComponent::FindEquipmentSlotDefinition(
	const FGameplayTag SlotTag,
	FPEPEquipmentSlotDefinition& OutSlot) const
{
	const FPEPEquipmentSlotDefinition* Slot = SlotTag.IsValid() ? FindSlotDefinition(SlotTag) : nullptr;
	OutSlot = Slot ? *Slot : FPEPEquipmentSlotDefinition();
	return Slot != nullptr;
}

bool UPEPEquipmentComponent::HasOwnerEquipmentView() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}
	if (Owner->HasAuthority())
	{
		return true;
	}
	/*
	 * The owning client holds the full store (owner-only replication) and so can compute every
	 * equipped item. By role, and by net owner as well: right after possession the owner can be
	 * known before the autonomous role lands.
	 */
	return Owner->GetLocalRole() == ROLE_AutonomousProxy || Owner->HasLocalNetOwner();
}

TArray<FPEPEquippedItemInfo> UPEPEquipmentComponent::GetOwnerEquipmentNodes() const
{
	return OwnerViewNodes;
}

bool UPEPEquipmentComponent::FindOwnerEquipmentNode(
	const FPEPItemId ItemId,
	FPEPEquippedItemInfo& OutNode) const
{
	const FPEPEquippedItemInfo* Node = FindOwnerViewNode(ItemId);
	OutNode = Node ? *Node : FPEPEquippedItemInfo();
	return Node != nullptr;
}

bool UPEPEquipmentComponent::FindEquippedRootInSlot(
	const FGameplayTag SlotTag,
	FPEPEquippedItemInfo& OutNode) const
{
	OutNode = FPEPEquippedItemInfo();
	if (!SlotTag.IsValid())
	{
		return false;
	}
	/*
	 * Strictly one view per machine: mixing them would show a slot the owner just emptied, still
	 * occupied in a projection that has not caught up yet.
	 */
	if (HasOwnerEquipmentView())
	{
		const FPEPEquippedItemInfo* Root = OwnerViewNodes.FindByPredicate(
			[SlotTag](const FPEPEquippedItemInfo& Node)
			{
				return !Node.IsAttachment() && Node.SlotTag == SlotTag;
			});
		if (Root)
		{
			OutNode = *Root;
		}
		return Root != nullptr;
	}
	if (const FPEPEquippedItemPublic* Root = FindPublicRootInSlot(SlotTag))
	{
		OutNode = Root->ToInfo();
		return true;
	}
	return false;
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
		|| !HasEquipAccess(*ItemStore, ItemId))
	{
		return EPEPRequestResult::Failed;
	}

	OutRequestId = AllocateRequestId();
	if (Owner->HasAuthority())
	{
		const FPEPItemResult Result = ExecuteEquipItem(ItemId, SlotTag, ERequestOrigin::Local);
		ResolveEquipRequest(OutRequestId, Result, ERequestOrigin::Local);
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
		|| !HasEquipAccess(*ItemStore, ItemId))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
	}

	const FPEPItemInstance* Item = ItemStore->FindItem(ItemId);
	if (!Item)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
	}
	/* Asked up front as well as through the store's validator: a preview must not depend on it. */
	const FPEPItemResult Eligibility = CheckEquipEligibility(*Item, SlotTag);
	if (!Eligibility.bSuccess)
	{
		return Eligibility;
	}

	FPEPItemAccessContext Access;
	if (!ResolveRequestContext(ERequestOrigin::Local, Access))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_NotAuthority, ItemId);
	}
	const FPEPItemLocation Destination = FPEPItemLocation::MakeNamedSlot(SlotTag);
	FPEPItemId OccupantId;
	return ItemStore->FindItemAtLocation(Destination, OccupantId) && OccupantId != ItemId
		? ItemStore->CanSwapItems(ItemId, OccupantId, Access)
		: ItemStore->CanMoveItem(ItemId, Destination, Access);
}

FPEPItemResult UPEPEquipmentComponent::ExecuteEquipItem(
	const FPEPItemId ItemId,
	const FGameplayTag SlotTag,
	const ERequestOrigin Origin)
{
	IPEPItemStore* ItemStore = GetItemStore();
	if (!ItemStore || !ItemId.IsValid() || !FindSlotDefinition(SlotTag)
		|| !HasEquipAccess(*ItemStore, ItemId))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
	}
	const FPEPItemInstance* Item = ItemStore->FindItem(ItemId);
	if (!Item)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
	}
	/*
	 * Checked before the store is asked, so the refusal is the same whether or not a given store
	 * honours its validators. A conforming store asks the same question again through
	 * ValidateEquipmentLocation; eligibility is a pure query, so asking twice is only a cost.
	 */
	const FPEPItemResult Eligibility = CheckEquipEligibility(*Item, SlotTag);
	if (!Eligibility.bSuccess)
	{
		return Eligibility;
	}
	FPEPItemAccessContext Access;
	if (!ResolveRequestContext(Origin, Access))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_NotAuthority, ItemId);
	}

	const FPEPItemLocation Destination = FPEPItemLocation::MakeNamedSlot(SlotTag);
	FPEPItemId OccupantId;
	/* A swap raises two store events: they rebuild the projection once, before this returns. */
	BeginProjectionBatch();
	const FPEPItemResult Result =
		ItemStore->FindItemAtLocation(Destination, OccupantId) && OccupantId != ItemId
			? ItemStore->SwapItems(ItemId, OccupantId, Access)
			: ItemStore->MoveItem(ItemId, Destination, Access);
	EndProjectionBatch();
	return Result;
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
	ResolveEquipRequest(
		RequestId,
		ExecuteEquipItem(ItemId, SlotTag, ERequestOrigin::RemoteClient),
		ERequestOrigin::RemoteClient);
}

EPEPRequestResult UPEPEquipmentComponent::RequestUnequipItem(
	const FPEPItemId ItemId,
	const FPEPItemLocation TargetLocation,
	int32& OutRequestId)
{
	OutRequestId = 0;
	AActor* Owner = GetOwner();
	if (!Owner || !ItemId.IsValid())
	{
		return EPEPRequestResult::Failed;
	}

	if (Owner->HasAuthority())
	{
		OutRequestId = AllocateRequestId();
		const FPEPItemResult Result =
			ExecuteUnequipItem(ItemId, TargetLocation, ERequestOrigin::Local);
		ResolveEquipRequest(OutRequestId, Result, ERequestOrigin::Local);
		return Result.bSuccess ? EPEPRequestResult::Executed : EPEPRequestResult::Failed;
	}

	/*
	 * A client refuses early only for what it can be sure of without the store: a store copy on
	 * the client may be partial, and a slot hidden from the projection is still a slot. The server
	 * runs the full check.
	 */
	if (!Owner->HasLocalNetOwner()
		|| !TargetLocation.IsStructurallyValid()
		|| (TargetLocation.Type == EPEPItemLocationType::NamedSlot
			&& FindSlotDefinition(TargetLocation.LocationTag)))
	{
		return EPEPRequestResult::Failed;
	}

	OutRequestId = AllocateRequestId();
	Server_RequestUnequipItem(ItemId, TargetLocation, OutRequestId);
	return EPEPRequestResult::Submitted;
}

FPEPEquipmentRequestResult UPEPEquipmentComponent::UnequipItem(
	const FPEPItemId ItemId,
	const FPEPItemLocation TargetLocation)
{
	FPEPEquipmentRequestResult RequestResult;
	const AActor* Owner = GetOwner();
	RequestResult.Result = Owner && Owner->HasAuthority()
		? ExecuteUnequipItem(ItemId, TargetLocation, ERequestOrigin::Local)
		: FPEPItemResult::Failure(PEPCoreTags::Reason_NotAuthority, ItemId);
	return RequestResult;
}

FPEPItemResult UPEPEquipmentComponent::PreviewUnequipItem(
	const FPEPItemId ItemId,
	const FPEPItemLocation TargetLocation) const
{
	const FPEPItemResult Validation = ValidateUnequipRequest(ItemId, TargetLocation);
	if (!Validation.bSuccess)
	{
		return Validation;
	}
	FPEPItemAccessContext Access;
	if (!ResolveRequestContext(ERequestOrigin::Local, Access))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_NotAuthority, ItemId);
	}
	return GetItemStoreConst()->CanMoveItem(ItemId, TargetLocation, Access);
}

FPEPItemResult UPEPEquipmentComponent::ValidateUnequipRequest(
	const FPEPItemId ItemId,
	const FPEPItemLocation& TargetLocation) const
{
	const IPEPItemStore* ItemStore = GetItemStoreConst();
	FPEPItemLocation CurrentLocation;
	if (!ItemStore || !ItemId.IsValid() || !ItemStore->FindItemLocation(ItemId, CurrentLocation))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
	}
	/* Only a direct occupant of one of this component's slots is equipped here. */
	if (CurrentLocation.Type != EPEPItemLocationType::NamedSlot
		|| !FindSlotDefinition(CurrentLocation.LocationTag))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_StateMismatch, ItemId);
	}
	/*
	 * IPEPItemStore has no automatic placement: AddItem and MoveItem both take an explicit
	 * destination, and the placement search some stores have (PEPInventory's MoveItemAuto) is not
	 * part of the contract — and may answer with another equipment slot. Equipment does not guess.
	 */
	if (TargetLocation.Type == EPEPItemLocationType::None)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_UnsupportedMode, ItemId);
	}
	if (!TargetLocation.IsStructurallyValid()
		|| (TargetLocation.Type == EPEPItemLocationType::ItemContainer
			&& TargetLocation.ParentItemId == ItemId))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
	}
	/* Slot to slot is an equip, with its swap and eligibility rules. */
	if (TargetLocation.Type == EPEPItemLocationType::NamedSlot
		&& FindSlotDefinition(TargetLocation.LocationTag))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
	}
	return FPEPItemResult::Success(ItemId);
}

FPEPItemResult UPEPEquipmentComponent::ExecuteUnequipItem(
	const FPEPItemId ItemId,
	const FPEPItemLocation& TargetLocation,
	const ERequestOrigin Origin)
{
	const FPEPItemResult Validation = ValidateUnequipRequest(ItemId, TargetLocation);
	if (!Validation.bSuccess)
	{
		return Validation;
	}
	FPEPItemAccessContext Access;
	if (!ResolveRequestContext(Origin, Access))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_NotAuthority, ItemId);
	}
	/* A move, never a swap: displacing the target's occupant into this slot would be an equip. */
	BeginProjectionBatch();
	const FPEPItemResult Result = GetItemStore()->MoveItem(ItemId, TargetLocation, Access);
	EndProjectionBatch();
	return Result;
}

bool UPEPEquipmentComponent::Server_RequestUnequipItem_Validate(
	const FPEPItemId ItemId,
	const FPEPItemLocation TargetLocation,
	const int32 RequestId)
{
	(void)TargetLocation;
	return ItemId.IsValid() && RequestId > 0;
}

void UPEPEquipmentComponent::Server_RequestUnequipItem_Implementation(
	const FPEPItemId ItemId,
	const FPEPItemLocation TargetLocation,
	const int32 RequestId)
{
	ResolveEquipRequest(
		RequestId,
		ExecuteUnequipItem(ItemId, TargetLocation, ERequestOrigin::RemoteClient),
		ERequestOrigin::RemoteClient);
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

bool UPEPEquipmentComponent::ResolveRequestContext(
	const ERequestOrigin Origin,
	FPEPItemAccessContext& OutContext) const
{
	/* Whoever drives the pawn — the player, an AI — is who the store is asked on behalf of. */
	if (const AController* Controller = GetOwningController())
	{
		OutContext = FPEPItemAccessContext::From(Controller);
		return true;
	}
	if (Origin == ERequestOrigin::RemoteClient)
	{
		/*
		 * A client-requested change answers to that client. With no controller to name it, it is
		 * refused rather than made as System(): that would hand the server's own authority to
		 * whoever sent the RPC.
		 */
		OutContext = FPEPItemAccessContext::From(nullptr);
		return false;
	}
	/*
	 * No controller, called on this machine. On the authority that is a server-driven change — an
	 * unpossessed pawn, a mannequin, a scripted loadout — made deliberately as System(). Anywhere
	 * else it is only a client-side preview, asked as nobody in particular (From(nullptr) is
	 * never System).
	 */
	const AActor* Owner = GetOwner();
	OutContext = Owner && Owner->HasAuthority()
		? FPEPItemAccessContext::System()
		: FPEPItemAccessContext::From(nullptr);
	return true;
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
	const FPEPItemResult& Result,
	const ERequestOrigin Origin)
{
	FPEPEquipmentRequestResult RequestResult;
	RequestResult.RequestId = RequestId;
	RequestResult.Result = Result;

	if (Origin == ERequestOrigin::RemoteClient)
	{
		/* The id is one the client allocated: only that client can match it. */
		Client_ResolveEquipRequest(RequestResult);
		return;
	}

	/*
	 * Issued on this machine, so resolved on it, and never sent to the owning client: the server
	 * allocates its own ids, which would collide with the client's. Raised at the next tick, so a
	 * caller already holds OutRequestId when the resolution arrives — a Blueprint node only hands
	 * its out pins over once it returns. Before BeginPlay, it waits for BeginPlay.
	 */
	PendingLocalResolutions.Add(RequestResult);
	ScheduleDeferredUpdates();
}

void UPEPEquipmentComponent::FlushDeferredUpdates()
{
	RefreshDirtyEquipmentViews();
	if (bLocalPresentationReconcilePending)
	{
		ReconcileLocalPresentations();
	}
	if (!PendingLocalResolutions.IsEmpty())
	{
		/* Taken first: a listener may issue the next request from inside the broadcast. */
		TArray<FPEPEquipmentRequestResult> Resolutions = MoveTemp(PendingLocalResolutions);
		PendingLocalResolutions.Reset();
		for (const FPEPEquipmentRequestResult& Resolution : Resolutions)
		{
			OnEquipmentRequestResolved.Broadcast(Resolution);
		}
	}
}

void UPEPEquipmentComponent::ScheduleDeferredUpdates()
{
	/* Before BeginPlay nothing ticks for this component; BeginPlay runs or schedules the rest. */
	if (bDeferredUpdatesScheduled || !HasBegunPlay())
	{
		return;
	}
	bDeferredUpdatesScheduled = true;
	/*
	 * The core ticker rather than a world timer: timers stop while the world is paused, and a
	 * paused inventory screen still equips. Bound to this object, so it never outlives it.
	 */
	DeferredUpdatesHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UPEPEquipmentComponent::TickDeferredUpdates));
}

bool UPEPEquipmentComponent::TickDeferredUpdates(const float DeltaTime)
{
	(void)DeltaTime;
	/* Cleared first, so a change raised while flushing schedules its own pass. */
	bDeferredUpdatesScheduled = false;
	DeferredUpdatesHandle.Reset();
	FlushDeferredUpdates();
	return false;
}

void UPEPEquipmentComponent::CancelDeferredUpdates()
{
	if (bDeferredUpdatesScheduled)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DeferredUpdatesHandle);
	}
	DeferredUpdatesHandle.Reset();
	bDeferredUpdatesScheduled = false;
}

void UPEPEquipmentComponent::HandlePublicEquipmentReplicated()
{
	RebuildPublicEntryIndex();
	ReconcileLocalPresentations();
	/*
	 * Clients only: the authority never receives its own Fast Array, so a listen server announces
	 * once from RefreshPublicProjection and each client once from here. A receive that changed
	 * nothing announces nothing, since the diff is against what this machine last announced.
	 */
	BroadcastEquipmentItemChanges();
	OnPublicEquipmentChanged.Broadcast();

	/*
	 * The owning client's owner view follows its store copy's events. The server's projection
	 * moving says that copy probably moved too, which keeps the view current even behind a store
	 * that raises nothing on its replicated copy. Folded into the next tick, like a store event.
	 */
	if (HasOwnerEquipmentView() && !bOwnerViewDirty)
	{
		bOwnerViewDirty = true;
		ScheduleDeferredUpdates();
	}
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
	bLocalPresentationReconcilePending = false;
	/*
	 * Not before BeginPlay. A Fast Array receive in a late joiner's first bunch, or a store write
	 * on the authority, can land before the mesh is discovered; a visual built then would hang off
	 * the root (the capsule) and stay there. BeginPlay builds everything once the mesh is known.
	 */
	if (!HasBegunPlay())
	{
		return;
	}
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
	 * A diff, not a rebuild. This runs on every replicated delta and after assets finish
	 * loading, so tearing down eight props to add a ninth was the cost of every loot. A visual is
	 * kept when the node it was built from is unchanged and it is still alive; it goes when its
	 * node changed, vanished, or was suppressed by a temporary prop - and so does everything
	 * attached beneath it, since a child rides its parent's actor.
	 */
	TMap<FPEPItemId, const FPEPEquippedItemPublic*> Desired;
	TMap<FGameplayTag, const FPEPEquippedItemPublic*> RootsBySlot;
	Desired.Reserve(PublicEquipmentView.Entries.Num());
	for (const FPEPEquippedItemPublic& Entry : PublicEquipmentView.Entries)
	{
		if (!Entry.ItemId.IsValid())
		{
			continue;
		}
		if (!Entry.IsAttachment() && !RootsBySlot.Contains(Entry.SlotTag))
		{
			RootsBySlot.Add(Entry.SlotTag, &Entry);
		}
		if (!SuppressedPresentationItems.Contains(Entry.ItemId))
		{
			Desired.Add(Entry.ItemId, &Entry);
		}
	}

	TSet<FPEPItemId> Stale;
	for (const TPair<FPEPItemId, FPEPLocalPresentationTarget>& Pair : LocalPresentationTargets)
	{
		const FPEPEquippedItemPublic* const* Node = Desired.Find(Pair.Key);
		const FPEPEquippedItemInfo* Built = PresentedNodes.Find(Pair.Key);
		if (!Node || !Built || !Pair.Value.IsValid()
			|| !PEPEquipmentEvents::IsSameEquippedNode((*Node)->ToInfo(), *Built))
		{
			Stale.Add(Pair.Key);
		}
	}
	if (!Stale.IsEmpty())
	{
		/* One walk down a parent -> children index, instead of rescanning for every stale node. */
		TMultiMap<FPEPItemId, FPEPItemId> ChildrenByParent;
		for (const TPair<FPEPItemId, FPEPEquippedItemInfo>& Pair : PresentedNodes)
		{
			if (Pair.Value.ParentItemId.IsValid())
			{
				ChildrenByParent.Add(Pair.Value.ParentItemId, Pair.Key);
			}
		}
		TArray<FPEPItemId> PendingParents = Stale.Array();
		TArray<FPEPItemId> Children;
		while (!PendingParents.IsEmpty())
		{
			const FPEPItemId ParentItemId = PendingParents.Pop(EAllowShrinking::No);
			Children.Reset();
			ChildrenByParent.MultiFind(ParentItemId, Children);
			for (const FPEPItemId ChildItemId : Children)
			{
				bool bAlreadyStale = false;
				Stale.Add(ChildItemId, &bAlreadyStale);
				if (!bAlreadyStale)
				{
					PendingParents.Add(ChildItemId);
				}
			}
		}
		for (const FPEPItemId ItemId : Stale)
		{
			DestroyLocalPresentation(ItemId);
		}
	}

	TSet<FPEPItemId> ResolvingItems;
	/* Roots in profile order, so two roots contending for one target resolve alike everywhere. */
	for (const FPEPEquipmentSlotDefinition& Slot : GetEffectiveSlotDefinitions())
	{
		if (const FPEPEquippedItemPublic* const* Root = RootsBySlot.Find(Slot.SlotTag))
		{
			EnsureLocalPresentation(**Root, ResolvingItems);
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
			PresentedNodes.Add(Pair.Key, Pair.Value->ToInfo());
		}
	}
	LocalPresentationChanged.Broadcast();
}

void UPEPEquipmentComponent::RequestLocalPresentationReconcile()
{
	bLocalPresentationReconcilePending = true;
	ScheduleDeferredUpdates();
}

void UPEPEquipmentComponent::RebuildLocalPresentations()
{
	DestroyAllLocalPresentations();
	ReconcileLocalPresentations();
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
		/* A load that completed synchronously is usable in this same pass. */
		Definition = ResolveLoadedDefinition(Entry.DefinitionId);
		if (!Definition)
		{
			return false;
		}
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
		const FPEPEquippedItemPublic* ParentEntry = FindPublicEntry(Entry.ParentItemId);
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
			CarrierClass = ActorVisual->Get();
			if (!CarrierClass)
			{
				return false;
			}
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
		if (!SkeletalVisual->Get())
		{
			return false;
		}
	}
	if (const TSoftObjectPtr<UStaticMesh>* StaticVisual =
		Presentation.GetStaticMeshVisual();
		StaticVisual && !StaticVisual->IsNull() && !StaticVisual->Get())
	{
		RequestPresentationAssetLoad(StaticVisual->ToSoftObjectPath());
		if (!StaticVisual->Get())
		{
			return false;
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
		AttachParent,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		AttachSocket);
	Actor->InitializePresentation(
		Entry.ToInfo(),
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
		Mesh = SkeletalVisual->Get();
		if (!Mesh)
		{
			return false;
		}
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
			Mesh = SkeletalVisual->Get();
			if (!Mesh)
			{
				return false;
			}
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
	TSharedPtr<FStreamableHandle> Handle = UAssetManager::Get().LoadPrimaryAsset(
		DefinitionId,
		TArray<FName>(),
		FStreamableDelegate::CreateWeakLambda(this, [this, DefinitionId]()
		{
			DefinitionLoadHandles.Remove(DefinitionId);
			if (ResolveLoadedDefinition(DefinitionId))
			{
				/* Never reconciled from here: several loads landing in one frame share one pass. */
				RequestLocalPresentationReconcile();
			}
			else
			{
				FailedDefinitionLoads.Add(DefinitionId);
			}
		}));
	/*
	 * A load that completes synchronously has already run the delegate; keep the handle only while
	 * it is pending, or a completed handle would block the id for good.
	 */
	if (Handle.IsValid() && !Handle->HasLoadCompleted())
	{
		DefinitionLoadHandles.Add(DefinitionId, Handle);
	}
	else if (!Handle.IsValid() && !ResolveLoadedDefinition(DefinitionId))
	{
		/* No handle and nothing resident: the asset manager does not know this definition. */
		FailedDefinitionLoads.Add(DefinitionId);
	}
}

void UPEPEquipmentComponent::RequestPresentationAssetLoad(
	const FSoftObjectPath& AssetPath)
{
	if (!AssetPath.IsValid() || PresentationAssetLoadHandles.Contains(AssetPath)
		|| FailedPresentationAssetLoads.Contains(AssetPath))
	{
		return;
	}
	TSharedPtr<FStreamableHandle> Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetPath,
		FStreamableDelegate::CreateWeakLambda(this, [this, AssetPath]()
		{
			PresentationAssetLoadHandles.Remove(AssetPath);
			if (AssetPath.ResolveObject())
			{
				RequestLocalPresentationReconcile();
			}
			else
			{
				FailedPresentationAssetLoads.Add(AssetPath);
			}
		}));
	if (Handle.IsValid() && !Handle->HasLoadCompleted())
	{
		PresentationAssetLoadHandles.Add(AssetPath, Handle);
	}
	else if (!Handle.IsValid() && !AssetPath.ResolveObject())
	{
		FailedPresentationAssetLoads.Add(AssetPath);
	}
}

bool UPEPEquipmentComponent::ShowTemporaryPresentationLocal(
	const FName PresentationKey,
	const FPrimaryAssetId DefinitionId,
	const FName AttachSocket,
	const FPEPItemId SuppressedItemId)
{
	const FPEPItemId PreviousSuppressedItemId =
		TemporarySuppressedItems.FindRef(PresentationKey);
	HideTemporaryPresentationLocal(PresentationKey, false);
	/*
	 * The original stays hidden for exactly as long as this key's current spec asks for it,
	 * whether or not the prop itself can be built. A spec that stops asking gives it back rather
	 * than inheriting the previous spec's hide.
	 */
	if (SuppressedItemId.IsValid())
	{
		TemporarySuppressedItems.Add(PresentationKey, SuppressedItemId);
	}
	RefreshSuppressedPresentationItems();
	if (PreviousSuppressedItemId != SuppressedItemId)
	{
		// Rebuild the regular projection so the suppressed item and any visual children attached
		// to it disappear (or return) together. Temporary props are preserved by reconciliation.
		ReconcileLocalPresentations();
	}

	if (!HasBegunPlay() || !bEnableLocalPresentation || !PresentationMesh
		|| !DefinitionId.IsValid() || PresentationKey.IsNone() || AttachSocket.IsNone()
		|| !GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	const UPEPItemDefinition* Definition = ResolveLoadedDefinition(DefinitionId);
	if (!Definition)
	{
		if (FailedDefinitionLoads.Contains(DefinitionId))
		{
			return false;
		}
		/* Shown by HandleTemporaryPresentationLoaded once it lands, if still wanted. */
		RequestTemporaryPresentationLoad(PresentationKey, DefinitionId, FSoftObjectPath());
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
		if (ActorVisual->IsNull())
		{
			return false;
		}
		CarrierClass = ActorVisual->Get();
		if (!CarrierClass)
		{
			const FSoftObjectPath ClassPath = ActorVisual->ToSoftObjectPath();
			if (FailedPresentationAssetLoads.Contains(ClassPath))
			{
				return false;
			}
			RequestTemporaryPresentationLoad(PresentationKey, DefinitionId, ClassPath);
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
	/* A temporary prop presents a definition, not a projected node: only DefinitionId is set. */
	FPEPEquippedItemInfo TemporaryState;
	TemporaryState.DefinitionId = DefinitionId;
	Actor->InitializePresentation(TemporaryState, *Presentation);
	TemporaryPresentationActors.Add(PresentationKey, Actor);
	return true;
}

void UPEPEquipmentComponent::RequestTemporaryPresentationLoad(
	const FName PresentationKey,
	const FPrimaryAssetId DefinitionId,
	const FSoftObjectPath& AssetPath)
{
	const bool bLoadDefinition = !AssetPath.IsValid();
	FStreamableDelegate OnLoaded = FStreamableDelegate::CreateWeakLambda(
		this,
		[this, PresentationKey, DefinitionId, AssetPath]()
		{
			HandleTemporaryPresentationLoaded(PresentationKey, DefinitionId, AssetPath);
		});
	TSharedPtr<FStreamableHandle> Handle;
	if (bLoadDefinition)
	{
		Handle = UAssetManager::Get().LoadPrimaryAsset(
			DefinitionId,
			TArray<FName>(),
			MoveTemp(OnLoaded));
	}
	else
	{
		Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
			AssetPath,
			MoveTemp(OnLoaded));
	}

	/*
	 * Kept only while pending. A load that completed synchronously already ran the delegate,
	 * which showed the prop or recorded the failure; holding its finished handle would only let a
	 * later hide cancel nothing.
	 */
	if (Handle.IsValid() && !Handle->HasLoadCompleted())
	{
		TemporaryPresentationLoadHandles.Add(PresentationKey, Handle);
		return;
	}
	const bool bResident = bLoadDefinition
		? ResolveLoadedDefinition(DefinitionId) != nullptr
		: AssetPath.ResolveObject() != nullptr;
	if (!Handle.IsValid() && !bResident)
	{
		/* Nothing started and nothing resident: unknown to the asset manager. Never asked again. */
		if (bLoadDefinition)
		{
			FailedDefinitionLoads.Add(DefinitionId);
		}
		else
		{
			FailedPresentationAssetLoads.Add(AssetPath);
		}
	}
}

void UPEPEquipmentComponent::HandleTemporaryPresentationLoaded(
	const FName PresentationKey,
	const FPrimaryAssetId DefinitionId,
	const FSoftObjectPath AssetPath)
{
	TemporaryPresentationLoadHandles.Remove(PresentationKey);
	const bool bLoadDefinition = !AssetPath.IsValid();
	const bool bLoaded = bLoadDefinition
		? ResolveLoadedDefinition(DefinitionId) != nullptr
		: AssetPath.ResolveObject() != nullptr;
	if (!bLoaded)
	{
		/*
		 * Recorded in the sets the regular presentation uses too, and the prop is not asked for
		 * again: re-requesting from here is what used to reload a missing asset forever.
		 */
		if (bLoadDefinition)
		{
			FailedDefinitionLoads.Add(DefinitionId);
		}
		else
		{
			FailedPresentationAssetLoads.Add(AssetPath);
		}
		return;
	}

	/* Only for the spec still wanted under this key: it may have been replaced or hidden. */
	const FPEPTemporaryPresentationEntry* Spec = AppliedTemporaryPresentations.Find(PresentationKey);
	if (!Spec || Spec->DefinitionId != DefinitionId)
	{
		return;
	}
	const FPEPTemporaryPresentationEntry Wanted = *Spec;
	ShowTemporaryPresentationLocal(
		Wanted.PresentationKey,
		Wanted.DefinitionId,
		Wanted.AttachSocket,
		Wanted.SuppressedItemId);
}

void UPEPEquipmentComponent::HideTemporaryPresentationLocal(
	const FName PresentationKey,
	const bool bRestoreSuppressedPresentation)
{
	const bool bHadSuppressedPresentation = TemporarySuppressedItems.Remove(PresentationKey) > 0;
	if (bHadSuppressedPresentation)
	{
		RefreshSuppressedPresentationItems();
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

void UPEPEquipmentComponent::RefreshSuppressedPresentationItems()
{
	SuppressedPresentationItems.Reset();
	for (const TPair<FName, FPEPItemId>& Pair : TemporarySuppressedItems)
	{
		SuppressedPresentationItems.Add(Pair.Value);
	}
}

void UPEPEquipmentComponent::RebuildTemporaryPresentations()
{
	if (!HasBegunPlay())
	{
		return;
	}
	TArray<FPEPTemporaryPresentationEntry> Specs;
	AppliedTemporaryPresentations.GenerateValueArray(Specs);
	for (const FPEPTemporaryPresentationEntry& Spec : Specs)
	{
		ShowTemporaryPresentationLocal(
			Spec.PresentationKey,
			Spec.DefinitionId,
			Spec.AttachSocket,
			Spec.SuppressedItemId);
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
