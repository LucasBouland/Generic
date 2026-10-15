#include "Components/PEPEquipmentAbilityBridgeComponent.h"

#include "Components/GMCAbilityComponent.h"
#include "Components/PEPEquipmentComponent.h"
#include "Core/PEPCoreLogChannels.h"
#include "Core/PEPCoreTags.h"
#include "Effects/GMCAbilityEffect.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Item/Fragments/PEPItemFragment_EquipmentAbilities.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemInstance.h"
#include "TimerManager.h"

namespace
{
	/** Same resolution as the equipment component: resident only, never a synchronous load. */
	const UPEPItemDefinition* ResolveLoadedDefinition(const FPrimaryAssetId DefinitionId)
	{
		if (!DefinitionId.IsValid())
		{
			return nullptr;
		}
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

	/** The ASC finishes its BeginPlay in the same frame as the pawn; a few frames is generous. */
	constexpr int32 MaxInitialSyncAttempts = 30;
}

UPEPEquipmentAbilityBridgeComponent::UPEPEquipmentAbilityBridgeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	/* Every machine runs the same projection-driven logic; nothing of this component replicates. */
	SetIsReplicatedByDefault(false);
}

UPEPEquipmentComponent* UPEPEquipmentAbilityBridgeComponent::GetEquipmentComponent() const
{
	return ResolveEquipment();
}

UGMC_AbilitySystemComponent* UPEPEquipmentAbilityBridgeComponent::GetAbilitySystemComponent() const
{
	return ResolveAbilitySystem();
}

bool UPEPEquipmentAbilityBridgeComponent::IsItemApplied(const FPEPItemId ItemId) const
{
	return AppliedItems.Contains(ItemId);
}

TArray<int32> UPEPEquipmentAbilityBridgeComponent::GetAppliedEffectIds(const FPEPItemId ItemId) const
{
	const FAppliedEquipment* Applied = AppliedItems.Find(ItemId);
	return Applied ? Applied->EffectIds : TArray<int32>();
}

UGMC_AbilitySystemComponent* UPEPEquipmentAbilityBridgeComponent::ResolveAbilitySystem() const
{
	if (UGMC_AbilitySystemComponent* Cached = CachedAbilitySystem.Get())
	{
		return Cached;
	}
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UGMC_AbilitySystemComponent>() : nullptr;
}

UPEPEquipmentComponent* UPEPEquipmentAbilityBridgeComponent::ResolveEquipment() const
{
	if (UPEPEquipmentComponent* Cached = CachedEquipment.Get())
	{
		return Cached;
	}
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UPEPEquipmentComponent>() : nullptr;
}

bool UPEPEquipmentAbilityBridgeComponent::HasOwnerAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

bool UPEPEquipmentAbilityBridgeComponent::NeedsAbilityMap() const
{
	/*
	 * By role first: a listen server's view of another player's pawn is the authority and must
	 * grant, and a client's own pawn is its autonomous proxy. Local control as well, because right
	 * after possession the owning client can see its controller before the new role arrives.
	 */
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}
	if (Owner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		return true;
	}
	const APawn* Pawn = Cast<APawn>(Owner);
	return Pawn && Pawn->IsLocallyControlled();
}

bool UPEPEquipmentAbilityBridgeComponent::IsReadyToApply() const
{
	const UGMC_AbilitySystemComponent* ASC = CachedAbilitySystem.Get();
	return bInitialSyncDone && CachedEquipment.IsValid() && ASC && ASC->HasBegunPlay()
		&& NeedsAbilityMap();
}

void UPEPEquipmentAbilityBridgeComponent::BeginPlay()
{
	Super::BeginPlay();

	CachedEquipment = ResolveEquipment();
	CachedAbilitySystem = ResolveAbilitySystem();
	UPEPEquipmentComponent* EquipmentComponent = CachedEquipment.Get();
	if (!EquipmentComponent || !CachedAbilitySystem.IsValid())
	{
		UE_LOG(
			LogPEP,
			Warning,
			TEXT("%s: '%s' needs both a UPEPEquipmentComponent and a UGMC_AbilitySystemComponent; "
				"equipment will grant nothing through GMAS."),
			*GetName(),
			GetOwner() ? *GetOwner()->GetName() : TEXT("<no owner>"));
		return;
	}

	EquipmentComponent->OnItemEquipped.AddDynamic(
		this,
		&UPEPEquipmentAbilityBridgeComponent::HandleItemEquipped);
	EquipmentComponent->OnItemUnequipped.AddDynamic(
		this,
		&UPEPEquipmentAbilityBridgeComponent::HandleItemUnequipped);
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveControllerChangedDelegate.AddDynamic(
			this,
			&UPEPEquipmentAbilityBridgeComponent::HandleControllerChanged);
	}
	TryInitialSync();
}

void UPEPEquipmentAbilityBridgeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UPEPEquipmentComponent* EquipmentComponent = CachedEquipment.Get())
	{
		EquipmentComponent->OnItemEquipped.RemoveDynamic(
			this,
			&UPEPEquipmentAbilityBridgeComponent::HandleItemEquipped);
		EquipmentComponent->OnItemUnequipped.RemoveDynamic(
			this,
			&UPEPEquipmentAbilityBridgeComponent::HandleItemUnequipped);
	}
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(
			this,
			&UPEPEquipmentAbilityBridgeComponent::HandleControllerChanged);
	}
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
	CancelDefinitionLoads();

	/*
	 * Only this bridge leaving gives back what it granted. When the whole pawn goes, the ability
	 * system goes with it, and queueing removals on a component being torn down helps nobody.
	 */
	const AActor* Owner = GetOwner();
	const UGMC_AbilitySystemComponent* ASC = CachedAbilitySystem.Get();
	if (Owner && !Owner->IsActorBeingDestroyed() && ASC && ASC->HasBegunPlay()
		&& !ASC->IsBeingDestroyed())
	{
		RevokeAll();
	}
	AppliedItems.Reset();
	AbilityMapLedger.Reset();
	bInitialSyncDone = false;

	Super::EndPlay(EndPlayReason);
}

void UPEPEquipmentAbilityBridgeComponent::TryInitialSync()
{
	UGMC_AbilitySystemComponent* ASC = CachedAbilitySystem.Get();
	if (bInitialSyncDone || !ASC || !CachedEquipment.IsValid())
	{
		return;
	}
	if (!ASC->HasBegunPlay())
	{
		UWorld* World = GetWorld();
		if (World && ++InitialSyncAttempts <= MaxInitialSyncAttempts)
		{
			World->GetTimerManager().SetTimerForNextTick(
				this,
				&UPEPEquipmentAbilityBridgeComponent::TryInitialSync);
		}
		else
		{
			UE_LOG(
				LogPEP,
				Warning,
				TEXT("%s: the ability system on '%s' never began play; equipment grants nothing."),
				*GetName(),
				GetOwner() ? *GetOwner()->GetName() : TEXT("<no owner>"));
		}
		return;
	}
	bInitialSyncDone = true;
	ReconcileWithProjection();
}

void UPEPEquipmentAbilityBridgeComponent::ReconcileWithProjection()
{
	const UPEPEquipmentComponent* EquipmentComponent = CachedEquipment.Get();
	const UGMC_AbilitySystemComponent* ASC = CachedAbilitySystem.Get();
	if (!bInitialSyncDone || !EquipmentComponent || !ASC || !ASC->HasBegunPlay())
	{
		return;
	}
	if (!NeedsAbilityMap())
	{
		/*
		 * Not, or no longer, a pawn this machine drives: drop the local entries earlier writes left
		 * (never a grant: a simulated proxy is not the authority).
		 */
		RevokeAll();
		return;
	}

	/* Diffing from nothing yields the projection as an ordered list of Equipped changes. */
	TArray<FPEPEquipmentChange> Unused;
	TArray<FPEPEquipmentChange> Desired;
	PEPEquipmentEvents::DiffPublicProjection(
		TArray<FPEPEquippedItemInfo>(),
		EquipmentComponent->GetPublicPresentationNodes(),
		Unused,
		Desired);

	TArray<FPEPItemId> Stale;
	for (const TPair<FPEPItemId, FAppliedEquipment>& Pair : AppliedItems)
	{
		const FPEPEquipmentChange* Wanted = Desired.FindByPredicate(
			[&Pair](const FPEPEquipmentChange& Change)
			{
				return Change.Node.ItemId == Pair.Key;
			});
		if (!Wanted || !PEPEquipmentEvents::IsSameEquippedNode(Wanted->Node, Pair.Value.Node))
		{
			Stale.Add(Pair.Key);
		}
	}
	for (const FPEPItemId ItemId : Stale)
	{
		RevokeItem(ItemId);
	}
	for (const FPEPEquipmentChange& Change : Desired)
	{
		ApplyChange(Change);
	}
}

void UPEPEquipmentAbilityBridgeComponent::HandleItemEquipped(const FPEPEquipmentChange& Change)
{
	/* Before the initial sync, the projection that sync reads already contains this change. */
	if (IsReadyToApply())
	{
		ApplyChange(Change);
	}
}

void UPEPEquipmentAbilityBridgeComponent::HandleItemUnequipped(const FPEPEquipmentChange& Change)
{
	if (IsReadyToApply())
	{
		RevokeItem(Change.Node.ItemId);
	}
}

void UPEPEquipmentAbilityBridgeComponent::HandleControllerChanged(
	APawn* Pawn,
	AController* OldController,
	AController* NewController)
{
	(void)Pawn;
	(void)OldController;
	(void)NewController;
	/*
	 * Next tick: on a client the new role can land in the same bunch as the controller, after this
	 * callback. Reconciling is idempotent, so a role that did not change costs one diff.
	 */
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(
			this,
			&UPEPEquipmentAbilityBridgeComponent::ReconcileWithProjection);
	}
}

void UPEPEquipmentAbilityBridgeComponent::ApplyChange(const FPEPEquipmentChange& Change)
{
	const FPEPEquippedItemInfo& Node = Change.Node;
	UGMC_AbilitySystemComponent* ASC = CachedAbilitySystem.Get();
	if (!ASC || !Node.ItemId.IsValid())
	{
		return;
	}
	if (const FAppliedEquipment* Existing = AppliedItems.Find(Node.ItemId))
	{
		if (PEPEquipmentEvents::IsSameEquippedNode(Existing->Node, Node))
		{
			return;
		}
		RevokeItem(Node.ItemId);
	}

	if (!ResolveLoadedDefinition(Node.DefinitionId))
	{
		/* Clients receive only the DefinitionId; the grant waits for the asset, never stalls on it. */
		RequestDefinitionLoad(Node.DefinitionId);
		return;
	}
	const FPEPItemFragment_EquipmentAbilities* Fragment = FindLoadedFragment(Node.DefinitionId);
	if (!Fragment || !Fragment->AppliesTo(Change.RootSlotTag, Node.IsAttachment()))
	{
		return;
	}

	FAppliedEquipment& Applied = AppliedItems.Add(Node.ItemId);
	Applied.Node = Node;

	TArray<FGameplayTag> TouchedInputTags;
	AbilityMapLedger.AddItem(
		Node.ItemId,
		Fragment->AbilityMaps,
		Fragment->AbilityMapPriority,
		TouchedInputTags);
	for (const FGameplayTag InputTag : TouchedInputTags)
	{
		SyncInputTag(InputTag);
	}

	if (!HasOwnerAuthority())
	{
		return;
	}
	TArray<int32> EffectIds;
	for (const TSubclassOf<UGMCAbilityEffect>& EffectClass : Fragment->EquippedEffects)
	{
		if (!EffectClass)
		{
			continue;
		}
		int32 EffectHandle = INDEX_NONE;
		int32 EffectId = INDEX_NONE;
		UGMCAbilityEffect* Effect = nullptr;
		/* Empty initialization data: GMAS falls back to the effect class defaults. */
		if (ASC->ApplyAbilityEffect(
			EffectClass,
			FGMCAbilityEffectData{},
			EGMCAbilityEffectQueueType::ServerAuth,
			EffectHandle,
			EffectId,
			Effect)
			&& EffectId != INDEX_NONE)
		{
			EffectIds.Add(EffectId);
		}
	}
	/* Looked up again: SyncInputTag never touches AppliedItems, but the reference is cheap to renew. */
	if (FAppliedEquipment* Stored = AppliedItems.Find(Node.ItemId))
	{
		Stored->EffectIds = MoveTemp(EffectIds);
	}
}

void UPEPEquipmentAbilityBridgeComponent::RevokeItem(const FPEPItemId ItemId)
{
	FAppliedEquipment Applied;
	if (!AppliedItems.RemoveAndCopyValue(ItemId, Applied))
	{
		return;
	}

	TArray<FGameplayTag> TouchedInputTags;
	AbilityMapLedger.RemoveItem(ItemId, TouchedInputTags);
	for (const FGameplayTag InputTag : TouchedInputTags)
	{
		SyncInputTag(InputTag);
	}

	UGMC_AbilitySystemComponent* ASC = CachedAbilitySystem.Get();
	if (ASC && HasOwnerAuthority() && !Applied.EffectIds.IsEmpty())
	{
		/* Same queue as the apply, so removal is ordered after it even if both are still queued. */
		ASC->RemoveEffectByIdSafe(Applied.EffectIds, EGMCAbilityEffectQueueType::ServerAuth);
	}
}

void UPEPEquipmentAbilityBridgeComponent::RevokeAll()
{
	TArray<FPEPItemId> ItemIds;
	AppliedItems.GetKeys(ItemIds);
	ItemIds.Sort([](const FPEPItemId& A, const FPEPItemId& B)
	{
		return A.Value < B.Value;
	});
	for (const FPEPItemId ItemId : ItemIds)
	{
		RevokeItem(ItemId);
	}
}

void UPEPEquipmentAbilityBridgeComponent::SyncInputTag(const FGameplayTag InputTag)
{
	UGMC_AbilitySystemComponent* ASC = CachedAbilitySystem.Get();
	if (!ASC || !InputTag.IsValid())
	{
		return;
	}

	if (AbilityMapLedger.NeedsBaseline(InputTag))
	{
		const TMap<FGameplayTag, FAbilityMapData> CurrentMap = ASC->GetAbilityMap();
		AbilityMapLedger.CaptureBaseline(InputTag, CurrentMap.Find(InputTag));
	}

	/*
	 * Always with bGrantedByDefault cleared: AddAbilityMapData would otherwise add the tag to the
	 * GMC-bound GrantedAbilityTags, which a client must never write.
	 */
	FAbilityMapData DesiredEntry;
	if (AbilityMapLedger.ResolveDesiredEntry(InputTag, DesiredEntry))
	{
		ASC->AddAbilityMapData(DesiredEntry);
	}

	if (HasOwnerAuthority())
	{
		const bool bWantsGrant = AbilityMapLedger.WantsGrant(InputTag);
		const bool bGrantedByLedger = AbilityMapLedger.IsGrantedByLedger(InputTag);
		if (bWantsGrant && !bGrantedByLedger)
		{
			/* A tag already granted by something else stays that something's to remove. */
			if (!ASC->HasGrantedAbilityTag(InputTag))
			{
				ASC->GrantAbilityByTag(InputTag);
				AbilityMapLedger.SetGrantedByLedger(InputTag, true);
			}
		}
		else if (!bWantsGrant && bGrantedByLedger)
		{
			ASC->RemoveGrantedAbilityByTag(InputTag);
			AbilityMapLedger.SetGrantedByLedger(InputTag, false);
		}
	}
	AbilityMapLedger.ForgetIfIdle(InputTag);
}

const FPEPItemFragment_EquipmentAbilities* UPEPEquipmentAbilityBridgeComponent::FindLoadedFragment(
	const FPrimaryAssetId DefinitionId) const
{
	const UPEPItemDefinition* Definition = ResolveLoadedDefinition(DefinitionId);
	return Definition
		? Definition->GetDefinitionFragment<FPEPItemFragment_EquipmentAbilities>()
		: nullptr;
}

void UPEPEquipmentAbilityBridgeComponent::RequestDefinitionLoad(const FPrimaryAssetId DefinitionId)
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
				/* The item may have moved or gone while loading: ask the projection, not the past. */
				ReconcileWithProjection();
			}
			else
			{
				FailedDefinitionLoads.Add(DefinitionId);
			}
		}));
	/*
	 * A load that completes synchronously runs the delegate before this point; keep the handle
	 * only while it is actually pending.
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

void UPEPEquipmentAbilityBridgeComponent::CancelDefinitionLoads()
{
	for (TPair<FPrimaryAssetId, TSharedPtr<FStreamableHandle>>& Pair : DefinitionLoadHandles)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->CancelHandle();
		}
	}
	DefinitionLoadHandles.Reset();
	FailedDefinitionLoads.Reset();
}

FPEPItemResult UPEPEquipmentAbilityBridgeComponent::CanEquipItem_Implementation(
	const UPEPEquipmentComponent* Equipment,
	const FPEPItemInstance& Item,
	const UPEPItemDefinition* Definition,
	const FGameplayTag SlotTag) const
{
	(void)SlotTag;
	/* Only the equipment this bridge serves is its business. */
	if (Equipment && Equipment != ResolveEquipment())
	{
		return FPEPItemResult::Success(Item.ItemId);
	}
	const UPEPItemDefinition* ResolvedDefinition = Definition
		? Definition
		: ResolveLoadedDefinition(Item.DefinitionId);
	const FPEPItemFragment_EquipmentAbilities* Fragment = ResolvedDefinition
		? ResolvedDefinition->GetDefinitionFragment<FPEPItemFragment_EquipmentAbilities>()
		: nullptr;
	if (!Fragment || !Fragment->HasEligibilityRequirements())
	{
		return FPEPItemResult::Success(Item.ItemId);
	}

	/* A requirement that cannot be checked is not met. */
	const UGMC_AbilitySystemComponent* ASC = ResolveAbilitySystem();
	if (!ASC)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, Item.ItemId);
	}
	if (!Fragment->RequiredActiveTags.IsEmpty() && !ASC->HasAllTags(Fragment->RequiredActiveTags))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, Item.ItemId);
	}
	if (!Fragment->BlockedActiveTags.IsEmpty() && ASC->HasAnyTag(Fragment->BlockedActiveTags))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, Item.ItemId);
	}
	return FPEPItemResult::Success(Item.ItemId);
}
