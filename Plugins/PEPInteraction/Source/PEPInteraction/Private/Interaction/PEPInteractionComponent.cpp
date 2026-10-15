#include "Interaction/PEPInteractionComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Core/PEPCoreTags.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/PEPInteractionInteractable.h"
#include "Interaction/PEPInteractionLogChannels.h"
#include "Interaction/PEPInteractionTags.h"
#include "Interaction/PEPInteractionTargetComponent.h"
#include "Net/UnrealNetwork.h"

namespace PEPInteractionSelection
{
	/*
	 * Bounds of the actor's registered primitives, or its location when it has none, so that range
	 * and scoring never measure against the world origin for a bare actor.
	 */
	FBox GetInteractionBounds(const AActor* Actor)
	{
		FBox Bounds(ForceInit);
		bool bHasBounds = false;

		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Actor);
		for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (IsValid(PrimitiveComponent) && PrimitiveComponent->IsRegistered())
			{
				Bounds += PrimitiveComponent->Bounds.GetBox();
				bHasBounds = true;
			}
		}

		if (!bHasBounds)
		{
			const FVector Location = Actor->GetActorLocation();
			Bounds = FBox(Location, Location);
		}
		return Bounds;
	}
}

namespace PEPInteractionExecution
{
	/* Enough for every end whose notification can overtake the replicated state. */
	constexpr int32 RecentlyEndedInteractionCapacity = 16;

	/*
	 * Listeners match outcomes to requests by id; an interactable that returns a bare
	 * Failure(Reason) still gets its target and option filled in, and the id is always the
	 * component's own, whatever the interactable returned.
	 */
	FPEPInteractionResult WithRequestIdentity(
		FPEPInteractionResult Result,
		AActor* RequestedTarget,
		const FGameplayTag InteractionTag,
		const int32 InteractionId)
	{
		if (!Result.Target)
		{
			Result.Target = RequestedTarget;
		}
		if (!Result.InteractionTag.IsValid())
		{
			Result.InteractionTag = InteractionTag;
		}
		Result.InteractionId = InteractionId;
		return Result;
	}
}

UPEPInteractionComponent::UPEPInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	/* Enabled by RefreshTickEnabled once there is local selection or a timed interaction to run. */
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void UPEPInteractionComponent::BeginPlay()
{
	Super::BeginPlay();

	/* Tick functions are registered by now: apply the actual need over the registration default. */
	RefreshTickEnabled();
}

void UPEPInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	/* The target may hold state for the interaction (a door half-open, a reserved user). */
	if (HasInteractionAuthority())
	{
		CancelInteraction(PEPInteractionTags::Reason_Interaction_Interrupted);
	}
	UnbindInteractorController();
	Super::EndPlay(EndPlayReason);
}

void UPEPInteractionComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	/* Progress is only of interest to the interacting player; nobody else is told. */
	DOREPLIFETIME_CONDITION(ThisClass, ActiveInteraction, COND_OwnerOnly);
}

void UPEPInteractionComponent::Init(AActor* InInteractor)
{
	UnbindInteractorController();
	CachedTraceStartComponent.Reset();

	Interactor = InInteractor;
	if (!Interactor)
	{
		UE_LOG(LogPEPInteraction, Error, TEXT("%s: Init needs an interactor."), *GetPathName());
		RefreshTickEnabled();
		return;
	}
	Camera = Interactor->FindComponentByClass<UCameraComponent>();

	BindInteractorController();
	RefreshTickEnabled();
}

void UPEPInteractionComponent::BindInteractorController()
{
	/* Possession decides IsLocallyControlled, and it often changes after Init (late possession, respawn). */
	if (APawn* Pawn = Cast<APawn>(Interactor.Get()))
	{
		Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(
			this,
			&ThisClass::HandleInteractorControllerChanged);
		BoundInteractorPawn = Pawn;
	}
}

void UPEPInteractionComponent::UnbindInteractorController()
{
	if (APawn* Pawn = BoundInteractorPawn.Get())
	{
		Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(
			this,
			&ThisClass::HandleInteractorControllerChanged);
	}
	BoundInteractorPawn.Reset();
}

void UPEPInteractionComponent::HandleInteractorControllerChanged(
	APawn* Pawn,
	AController* OldController,
	AController* NewController)
{
	RefreshTickEnabled();
}

void UPEPInteractionComponent::RefreshTickEnabled()
{
	const bool bLocalSelection = ShouldPerformLocalSelection();
	const bool bNeedsTick = bLocalSelection
		|| (HasInteractionAuthority() && IsInteractionInProgress());

	if (IsComponentTickEnabled() != bNeedsTick)
	{
		SetComponentTickEnabled(bNeedsTick);
	}

	/*
	 * A selection made while local (before an unpossession, say) must not outlive it. Silent when
	 * nothing was ever selected here, which is always the case on simulated proxies.
	 */
	if (!bLocalSelection)
	{
		Target.Reset();
		NotifySelectionIfChanged();
	}
}

void UPEPInteractionComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(
		DeltaTime,
		TickType,
		ThisTickFunction);

	/*
	 * Before the local selection gate on purpose: on a dedicated server the interactor is a remote
	 * pawn with no local selection, and its timed interaction must still progress there.
	 */
	if (IsInteractionInProgress() && HasInteractionAuthority())
	{
		TickActiveInteraction(GetInteractionClockSeconds());
	}

	if (!ShouldPerformLocalSelection())
	{
		Target.Reset();
		NotifySelectionIfChanged();
		return;
	}

	RefreshTarget();
	NotifySelectionIfChanged();

#if ENABLE_DRAW_DEBUG
	if (bDebugDrawInteraction)
	{
		DrawInteractionDebug();
	}
#endif
}

EPEPRequestResult
UPEPInteractionComponent::RequestInteractWithInteractionTag(
	const FGameplayTag InteractionTag)
{
	AActor* RequestedTarget = Target.Get();

	const auto RecordAndReturn = [this, RequestedTarget, InteractionTag](const EPEPRequestResult Result)
	{
		RecordDebugInteractionAttempt(RequestedTarget, InteractionTag, Result);
		return Result;
	};

	if (!Interactor
		|| !IsValid(RequestedTarget)
		|| !InteractionTag.IsValid())
	{
		return RecordAndReturn(EPEPRequestResult::Failed);
	}

	const TArray<FGameplayTag> Options =
		IPEPInteractionInteractable::
		Execute_GetInteractOptionTags(
			RequestedTarget,
			Interactor);

	if (!Options.Contains(InteractionTag))
	{
		return RecordAndReturn(EPEPRequestResult::Failed);
	}

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return RecordAndReturn(EPEPRequestResult::Failed);
	}

	const int32 RequestId = ReserveInteractionId();
	LastRequestedInteractionId = RequestId;

	if (Owner->HasAuthority())
	{
		const FPEPInteractionResult Result =
			BeginInteractWithInteractionTag(
				RequestedTarget,
				InteractionTag,
				RequestId);

		if (!Result.bSuccess)
		{
			return RecordAndReturn(EPEPRequestResult::Failed);
		}

		/* A timed interaction accepted but not executed yet is pending, like a client request. */
		const bool bStartedTimedInteraction = IsInteractionInProgress()
			&& ActiveInteraction.InteractionId == Result.InteractionId;
		return RecordAndReturn(
			bStartedTimedInteraction
				? EPEPRequestResult::Submitted
				: EPEPRequestResult::Executed);
	}

	Server_RequestInteractWithInteractionTag(
		RequestedTarget,
		InteractionTag,
		RequestId);

	return RecordAndReturn(EPEPRequestResult::Submitted);
}

void UPEPInteractionComponent::
Server_RequestInteractWithInteractionTag_Implementation(
	AActor* RequestedTarget,
	const FGameplayTag InteractionTag,
	const int32 InteractionId)
{
	/* Owning-client ids are negative; anything else is not one that client could have reserved. */
	const int32 ClientInteractionId = InteractionId < 0 ? InteractionId : 0;

	/* Holding the input may repeat the request in progress: it is absorbed, never limited. */
	const bool bRepeatsActiveInteraction = IsInteractionInProgress()
		&& ActiveInteraction.Target == RequestedTarget
		&& ActiveInteraction.InteractionTag == InteractionTag;

	if (!bRepeatsActiveInteraction && IsClientRequestRateLimited())
	{
		NotifyInteractionCompleted(
			PEPInteractionExecution::WithRequestIdentity(
				FPEPInteractionResult::Failure(
					PEPInteractionTags::Reason_Interaction_RateLimited,
					RequestedTarget,
					InteractionTag),
				RequestedTarget,
				InteractionTag,
				AcceptInteractionId(ClientInteractionId)));
		return;
	}

	BeginInteractWithInteractionTag(
		RequestedTarget,
		InteractionTag,
		ClientInteractionId);
}

bool UPEPInteractionComponent::IsClientRequestRateLimited()
{
	const UWorld* World = GetWorld();
	if (MinClientRequestInterval <= 0.0f || !World)
	{
		return false;
	}

	/* Real time: requests keep coming while the game is paused or slowed down. */
	const double Now = World->GetRealTimeSeconds();
	if (LastClientRequestRealTime >= 0.0
		&& Now - LastClientRequestRealTime < MinClientRequestInterval)
	{
		return true;
	}

	LastClientRequestRealTime = Now;
	return false;
}

EPEPRequestResult UPEPInteractionComponent::RequestCancelInteraction()
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return EPEPRequestResult::Failed;
	}

	if (Owner->HasAuthority())
	{
		return CancelInteraction(PEPInteractionTags::Reason_Interaction_Cancelled)
			? EPEPRequestResult::Executed
			: EPEPRequestResult::Failed;
	}

	/*
	 * Sent even when nothing looks in progress here: the replicated state lags the server, and
	 * reliable RPCs keep this cancel ordered after the request that started the interaction.
	 */
	Server_RequestCancelInteraction();
	return EPEPRequestResult::Submitted;
}

void UPEPInteractionComponent::Server_RequestCancelInteraction_Implementation()
{
	CancelInteraction(PEPInteractionTags::Reason_Interaction_Cancelled);
}

FPEPInteractionResult
UPEPInteractionComponent::ValidateInteractionReach(
	AActor* RequestedTarget,
	const FGameplayTag InteractionTag) const
{
	if (!HasInteractionAuthority())
	{
		return FPEPInteractionResult::Failure(
			PEPCoreTags::Reason_NotAuthority,
			RequestedTarget,
			InteractionTag);
	}

	if (!Interactor
		|| !IsValid(RequestedTarget)
		|| !InteractionTag.IsValid()
		|| RequestedTarget == Interactor
		|| RequestedTarget->GetWorld() != Interactor->GetWorld()
		|| !RequestedTarget->GetClass()->ImplementsInterface(
			UPEPInteractionInteractable::StaticClass()))
	{
		return FPEPInteractionResult::Failure(
			PEPCoreTags::Reason_InvalidTarget,
			RequestedTarget,
			InteractionTag);
	}

	/*
	 * Measured like the local selection is: from the shared view point, to the nearest part of the
	 * target rather than its origin, so a large target selected by its edge is not refused.
	 */
	FVector ViewLocation;
	FRotator ViewRotation;
	GetInteractionViewPoint(
		ViewLocation,
		ViewRotation);

	const FBox TargetBounds =
		PEPInteractionSelection::GetInteractionBounds(RequestedTarget);
	const float MaxDistance =
		Range + ServerRangeTolerance;

	if (TargetBounds.ComputeSquaredDistanceToPoint(ViewLocation) > FMath::Square(MaxDistance))
	{
		return FPEPInteractionResult::Failure(
			PEPCoreTags::Reason_OutOfRange,
			RequestedTarget,
			InteractionTag);
	}

	return FPEPInteractionResult::Success(
		RequestedTarget,
		InteractionTag);
}

FPEPInteractionResult
UPEPInteractionComponent::ValidateInteraction(
	AActor* RequestedTarget,
	const FGameplayTag InteractionTag) const
{
	const FPEPInteractionResult Reach =
		ValidateInteractionReach(
			RequestedTarget,
			InteractionTag);

	if (!Reach.bSuccess)
	{
		return Reach;
	}

	const TArray<FGameplayTag> Options =
		IPEPInteractionInteractable::
		Execute_GetInteractOptionTags(
			RequestedTarget,
			Interactor);

	if (!Options.Contains(InteractionTag))
	{
		return FPEPInteractionResult::Failure(
			PEPCoreTags::Reason_UnsupportedMode,
			RequestedTarget,
			InteractionTag);
	}

	if (!ValidateLineOfSight(RequestedTarget))
	{
		return FPEPInteractionResult::Failure(
			PEPInteractionTags::Reason_Interaction_NoLineOfSight,
			RequestedTarget,
			InteractionTag);
	}

	return IPEPInteractionInteractable::
		Execute_CanInteract(
			RequestedTarget,
			Interactor,
			InteractionTag);
}

bool UPEPInteractionComponent::ValidateLineOfSight(
	AActor* RequestedTarget) const
{
	if (!Interactor || !RequestedTarget)
	{
		return false;
	}

	return HasLineOfSightToTarget(
		RequestedTarget,
		GetTraceStartLocation());
}

bool UPEPInteractionComponent::HasLineOfSightToTarget(
	AActor* RequestedTarget,
	const FVector& ViewLocation) const
{
	const UWorld* World = GetWorld();
	if (!RequestedTarget || !World)
	{
		return false;
	}

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(RequestedTarget);

	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(PEPInteractionLineOfSight),
		false);
	QueryParams.AddIgnoredActor(Interactor);

	for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!IsValid(PrimitiveComponent))
		{
			continue;
		}

		FVector ComponentClosestPoint;
		const float Distance = PrimitiveComponent->GetClosestPointOnCollision(
			ViewLocation,
			ComponentClosestPoint);
		if (Distance < 0.0f)
		{
			continue;
		}

		if (FVector::DistSquared(ViewLocation, ComponentClosestPoint) <= KINDA_SMALL_NUMBER)
		{
			return true;
		}

		const FVector TraceDirection = (ComponentClosestPoint - ViewLocation).GetSafeNormal();
		const FVector TraceEnd = ComponentClosestPoint + (TraceDirection * 2.0f);
		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(
			Hit,
			ViewLocation,
			TraceEnd,
			TraceChannel,
			QueryParams);

		if (bHit && Hit.GetActor() == RequestedTarget)
		{
			return true;
		}
	}

	return false;
}

int32 UPEPInteractionComponent::ReserveInteractionId()
{
	/* Two sequences that never meet: the authority counts up, the owning client counts down. */
	return HasInteractionAuthority()
		? ++LastAllocatedInteractionId
		: --LastProposedInteractionId;
}

int32 UPEPInteractionComponent::AcceptInteractionId(const int32 RequestedId)
{
	const bool bReservedByAuthority = RequestedId > 0
		&& RequestedId <= LastAllocatedInteractionId;
	const bool bReservedByOwningClient = RequestedId < 0;
	const bool bAlreadyUsed = RequestedId == ActiveInteraction.InteractionId
		|| HasInteractionIdEnded(RequestedId);

	if ((bReservedByAuthority || bReservedByOwningClient) && !bAlreadyUsed)
	{
		return RequestedId;
	}

	/* Unidentified or unusable: handled under a fresh id, which the requester will not recognise. */
	return ++LastAllocatedInteractionId;
}

int32 UPEPInteractionComponent::GetLastRequestedInteractionId() const
{
	return LastRequestedInteractionId;
}

FPEPInteractionResult
UPEPInteractionComponent::ExecuteInteractWithInteractionTag(
	AActor* RequestedTarget,
	const FGameplayTag InteractionTag,
	const int32 InteractionId)
{
	if (!HasInteractionAuthority())
	{
		return FPEPInteractionResult::Failure(
			PEPCoreTags::Reason_NotAuthority,
			RequestedTarget,
			InteractionTag);
	}

	const int32 AcceptedId = AcceptInteractionId(InteractionId);

	const FPEPInteractionResult Validation =
		ValidateInteraction(
			RequestedTarget,
			InteractionTag);

	const FPEPInteractionResult Result = Validation.bSuccess
		? ExecuteValidatedInteraction(
			RequestedTarget,
			InteractionTag,
			AcceptedId)
		: PEPInteractionExecution::WithRequestIdentity(
			Validation,
			RequestedTarget,
			InteractionTag,
			AcceptedId);

	NotifyInteractionCompleted(Result);
	return Result;
}

FPEPInteractionResult
UPEPInteractionComponent::ExecuteValidatedInteraction(
	AActor* RequestedTarget,
	const FGameplayTag InteractionTag,
	const int32 InteractionId)
{
	return PEPInteractionExecution::WithRequestIdentity(
		IPEPInteractionInteractable::
		Execute_ExecuteInteraction(
			RequestedTarget,
			Interactor,
			InteractionTag),
		RequestedTarget,
		InteractionTag,
		InteractionId);
}

FPEPInteractionResult
UPEPInteractionComponent::BeginInteractWithInteractionTag(
	AActor* RequestedTarget,
	const FGameplayTag InteractionTag,
	const int32 InteractionId)
{
	if (!HasInteractionAuthority())
	{
		return FPEPInteractionResult::Failure(
			PEPCoreTags::Reason_NotAuthority,
			RequestedTarget,
			InteractionTag);
	}

	/*
	 * A repeated request (input repeat, duplicate RPC) must not restart the hold from zero. It
	 * joins the interaction in progress, whose id the result carries.
	 */
	if (IsInteractionInProgress()
		&& ActiveInteraction.Target == RequestedTarget
		&& ActiveInteraction.InteractionTag == InteractionTag)
	{
		FPEPInteractionResult Joined = FPEPInteractionResult::Success(
			RequestedTarget,
			InteractionTag);
		Joined.InteractionId = ActiveInteraction.InteractionId;
		return Joined;
	}

	const int32 AcceptedId = AcceptInteractionId(InteractionId);

	const FPEPInteractionResult Validation =
		ValidateInteraction(
			RequestedTarget,
			InteractionTag);

	if (!Validation.bSuccess)
	{
		const FPEPInteractionResult Failure =
			PEPInteractionExecution::WithRequestIdentity(
				Validation,
				RequestedTarget,
				InteractionTag,
				AcceptedId);
		NotifyInteractionCompleted(Failure);
		return Failure;
	}

	const float Duration =
		ResolveInteractionDuration(
			RequestedTarget,
			InteractionTag);

	if (Duration <= 0.0f)
	{
		const FPEPInteractionResult Result =
			ExecuteValidatedInteraction(
				RequestedTarget,
				InteractionTag,
				AcceptedId);
		NotifyInteractionCompleted(Result);
		return Result;
	}

	/* One timed interaction at a time: the newer request wins. */
	CancelInteraction(PEPInteractionTags::Reason_Interaction_Cancelled);

	StartActiveInteraction(
		RequestedTarget,
		InteractionTag,
		Duration,
		AcceptedId);

	FPEPInteractionResult Started = FPEPInteractionResult::Success(
		RequestedTarget,
		InteractionTag);
	Started.InteractionId = AcceptedId;
	return Started;
}

bool UPEPInteractionComponent::CancelInteraction(const FGameplayTag ReasonTag)
{
	if (!HasInteractionAuthority() || !IsInteractionInProgress())
	{
		return false;
	}

	const FPEPActiveInteraction Cancelled = ActiveInteraction;
	ClearActiveInteraction();

	const FGameplayTag Reason = ReasonTag.IsValid()
		? ReasonTag
		: PEPInteractionTags::Reason_Interaction_Cancelled.GetTag();

	AActor* CancelledTarget = Cancelled.Target;
	if (IsValid(CancelledTarget)
		&& CancelledTarget->GetClass()->ImplementsInterface(
			UPEPInteractionInteractable::StaticClass()))
	{
		IPEPInteractionInteractable::
			Execute_OnInteractionCancelled(
				CancelledTarget,
				Interactor,
				Cancelled.InteractionTag,
				Reason);
	}

	NotifyInteractionCancelled(
		Cancelled.InteractionId,
		CancelledTarget,
		Cancelled.InteractionTag,
		Reason);
	return true;
}

float UPEPInteractionComponent::ResolveInteractionDuration(
	AActor* RequestedTarget,
	const FGameplayTag InteractionTag) const
{
	if (!IsValid(RequestedTarget)
		|| !RequestedTarget->GetClass()->ImplementsInterface(
			UPEPInteractionInteractable::StaticClass()))
	{
		return 0.0f;
	}

	return FMath::Max(
		0.0f,
		IPEPInteractionInteractable::
		Execute_GetInteractionDuration(
			RequestedTarget,
			Interactor,
			InteractionTag));
}

void UPEPInteractionComponent::StartActiveInteraction(
	AActor* RequestedTarget,
	const FGameplayTag InteractionTag,
	const float Duration,
	const int32 InteractionId)
{
	ActiveInteraction.InteractionId = InteractionId;
	ActiveInteraction.Target = RequestedTarget;
	ActiveInteraction.InteractionTag = InteractionTag;
	ActiveInteraction.StartServerTime = GetInteractionClockSeconds();
	ActiveInteraction.Duration = Duration;
	LastStartedInteractionId = InteractionId;

	/* The request has just been fully validated: the next full check is one interval away. */
	NextActiveInteractionValidationTime =
		ActiveInteraction.StartServerTime + ActiveInteractionValidationInterval;
	ActiveInteractionFailingSince = -1.0;

	/* The authority now has an interaction to drive, even for a remote pawn it does not select for. */
	RefreshTickEnabled();

	IPEPInteractionInteractable::
		Execute_OnInteractionStarted(
			RequestedTarget,
			Interactor,
			InteractionTag);

	OnInteractionStarted.Broadcast(
		RequestedTarget,
		InteractionTag,
		Duration);
}

void UPEPInteractionComponent::TickActiveInteraction(const double ServerTimeSeconds)
{
	if (!HasInteractionAuthority() || !IsInteractionInProgress())
	{
		return;
	}

	AActor* ActiveTarget = ActiveInteraction.Target;
	const FGameplayTag ActiveTag = ActiveInteraction.InteractionTag;

	/* Every tick: losing the target or walking out of range cancels at once. */
	const FPEPInteractionResult Reach =
		ValidateInteractionReach(
			ActiveTarget,
			ActiveTag);

	if (!Reach.bSuccess)
	{
		CancelInteraction(
			Reach.ReasonTag.IsValid()
				? Reach.ReasonTag
				: PEPInteractionTags::Reason_Interaction_Interrupted.GetTag());
		return;
	}

	const bool bDurationElapsed =
		ServerTimeSeconds - ActiveInteraction.StartServerTime >= ActiveInteraction.Duration;

	/* The full check (options, line of sight, CanInteract) is rate limited, except before executing. */
	if (!bDurationElapsed && ServerTimeSeconds < NextActiveInteractionValidationTime)
	{
		return;
	}

	NextActiveInteractionValidationTime =
		ServerTimeSeconds + ActiveInteractionValidationInterval;

	const FPEPInteractionResult Validation =
		ValidateInteraction(
			ActiveTarget,
			ActiveTag);

	if (Validation.bSuccess)
	{
		ActiveInteractionFailingSince = -1.0;
		if (bDurationElapsed)
		{
			/* Validated just above: executes without validating a second time. */
			CompleteActiveInteraction();
		}
		return;
	}

	/*
	 * A brief failure (someone walking through the line of sight) is tolerated for the grace
	 * period. Completion waits meanwhile: a failing interaction never executes.
	 */
	if (ActiveInteractionFailingSince < 0.0)
	{
		ActiveInteractionFailingSince = ServerTimeSeconds;
	}

	if (ServerTimeSeconds - ActiveInteractionFailingSince >= ActiveInteractionValidationGracePeriod)
	{
		CancelInteraction(
			Validation.ReasonTag.IsValid()
				? Validation.ReasonTag
				: PEPInteractionTags::Reason_Interaction_Interrupted.GetTag());
	}
}

void UPEPInteractionComponent::CompleteActiveInteraction()
{
	const FPEPActiveInteraction Completed = ActiveInteraction;

	/* Cleared first, so whatever the execution triggers sees no interaction in progress. */
	ClearActiveInteraction();

	NotifyInteractionCompleted(
		ExecuteValidatedInteraction(
			Completed.Target,
			Completed.InteractionTag,
			Completed.InteractionId));
}

void UPEPInteractionComponent::ClearActiveInteraction()
{
	MarkInteractionIdEnded(ActiveInteraction.InteractionId);
	ActiveInteraction = FPEPActiveInteraction();
	ActiveInteractionFailingSince = -1.0;

	/* Nothing left to drive on the authority unless this machine also selects. */
	RefreshTickEnabled();
}

bool UPEPInteractionComponent::HasInteractionIdEnded(const int32 InteractionId) const
{
	return InteractionId != 0
		&& RecentlyEndedInteractionIds.Contains(InteractionId);
}

void UPEPInteractionComponent::MarkInteractionIdEnded(const int32 InteractionId)
{
	if (InteractionId == 0)
	{
		return;
	}

	RecentlyEndedInteractionIds.Remove(InteractionId);
	if (RecentlyEndedInteractionIds.Num() >= PEPInteractionExecution::RecentlyEndedInteractionCapacity)
	{
		RecentlyEndedInteractionIds.RemoveAt(0);
	}
	RecentlyEndedInteractionIds.Add(InteractionId);
}

void UPEPInteractionComponent::NotifyInteractionCompleted(
	const FPEPInteractionResult& Result)
{
	MarkInteractionIdEnded(Result.InteractionId);
	OnInteractionCompleted.Broadcast(Result);

	/* Runs locally, and is then ignored, when the owner is not a remote client. */
	Client_NotifyInteractionCompleted(Result);
}

void UPEPInteractionComponent::NotifyInteractionCancelled(
	const int32 InteractionId,
	AActor* CancelledTarget,
	const FGameplayTag InteractionTag,
	const FGameplayTag ReasonTag)
{
	MarkInteractionIdEnded(InteractionId);
	OnInteractionCancelled.Broadcast(
		InteractionId,
		CancelledTarget,
		InteractionTag,
		ReasonTag);

	Client_NotifyInteractionCancelled(
		InteractionId,
		CancelledTarget,
		InteractionTag,
		ReasonTag);
}

void UPEPInteractionComponent::Client_NotifyInteractionCompleted_Implementation(
	const FPEPInteractionResult& Result)
{
	/* The authority broadcast when it happened (listen server host, standalone, unowned actor). */
	if (HasInteractionAuthority())
	{
		return;
	}

	MarkInteractionIdEnded(Result.InteractionId);
	OnInteractionCompleted.Broadcast(Result);
}

void UPEPInteractionComponent::Client_NotifyInteractionCancelled_Implementation(
	const int32 InteractionId,
	AActor* CancelledTarget,
	const FGameplayTag InteractionTag,
	const FGameplayTag ReasonTag)
{
	if (HasInteractionAuthority())
	{
		return;
	}

	MarkInteractionIdEnded(InteractionId);
	OnInteractionCancelled.Broadcast(
		InteractionId,
		CancelledTarget,
		InteractionTag,
		ReasonTag);
}

void UPEPInteractionComponent::OnRep_ActiveInteraction()
{
	/*
	 * Started is announced from the replicated state, so listeners can already query progress.
	 * An interaction whose end was already notified (the RPC overtook the state) stays silent.
	 */
	if (!IsInteractionInProgress()
		|| ActiveInteraction.InteractionId == LastStartedInteractionId)
	{
		return;
	}

	LastStartedInteractionId = ActiveInteraction.InteractionId;
	OnInteractionStarted.Broadcast(
		ActiveInteraction.Target,
		ActiveInteraction.InteractionTag,
		ActiveInteraction.Duration);
}

bool UPEPInteractionComponent::IsInteractionInProgress() const
{
	return ActiveInteraction.IsActive()
		&& !HasInteractionIdEnded(ActiveInteraction.InteractionId);
}

int32 UPEPInteractionComponent::GetActiveInteractionId() const
{
	return IsInteractionInProgress() ? ActiveInteraction.InteractionId : 0;
}

AActor* UPEPInteractionComponent::GetActiveInteractionTarget() const
{
	return IsInteractionInProgress() ? ActiveInteraction.Target.Get() : nullptr;
}

FGameplayTag UPEPInteractionComponent::GetActiveInteractionTag() const
{
	return IsInteractionInProgress() ? ActiveInteraction.InteractionTag : FGameplayTag();
}

float UPEPInteractionComponent::GetActiveInteractionDuration() const
{
	return IsInteractionInProgress() ? ActiveInteraction.Duration : 0.0f;
}

float UPEPInteractionComponent::GetInteractionProgress() const
{
	return GetInteractionProgressAtTime(GetInteractionClockSeconds());
}

float UPEPInteractionComponent::GetInteractionProgressAtTime(const double ServerTimeSeconds) const
{
	if (!IsInteractionInProgress())
	{
		return 0.0f;
	}
	if (ActiveInteraction.Duration <= KINDA_SMALL_NUMBER)
	{
		return 1.0f;
	}

	const double Elapsed = ServerTimeSeconds - ActiveInteraction.StartServerTime;
	return FMath::Clamp(
		static_cast<float>(Elapsed / ActiveInteraction.Duration),
		0.0f,
		1.0f);
}

double UPEPInteractionComponent::GetInteractionClockSeconds() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}

	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

bool UPEPInteractionComponent::HasInteractionAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

bool UPEPInteractionComponent::IsSelectionCandidateVisible(
	FPEPInteractionSelectionCandidate& Candidate,
	const FVector& Start) const
{
	if (!Candidate.bVisibilityKnown)
	{
		Candidate.bVisible = HasLineOfSightToTarget(Candidate.Actor, Start);
		Candidate.bVisibilityKnown = true;
	}
	return Candidate.bVisible;
}

void UPEPInteractionComponent::RefreshTarget()
{
	AActor* PreviousTarget = Target.Get();
	Target.Reset();
	TargetSelectionScore = 0.0f;
	SelectionCandidates.Reset();

	if (!Camera && Interactor)
	{
		Camera = Interactor->FindComponentByClass<UCameraComponent>();
	}

	UWorld* World = GetWorld();
	if (!Interactor || !Camera || !World)
	{
		return;
	}

	/* The same view point the authority validates from, so a selected target is not refused there. */
	const FVector Start = GetTraceStartLocation();
	const FVector ViewForward = Camera->GetForwardVector().GetSafeNormal();
	const FVector SelectionAxis = GetSelectionAxis();
	const FVector End = Start + SelectionAxis * Range;

	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(PEPInteractionSelection),
		false);

	QueryParams.AddIgnoredActor(Interactor);

	const bool bUseLineSelection = SelectionShape == EPEPInteractionSelectionShape::Line
		|| SelectionRadius <= KINDA_SMALL_NUMBER;
	if (bUseLineSelection)
	{
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(
			Hit,
			Start,
			End,
			TraceChannel,
			QueryParams))
		{
			AActor* HitActor = Hit.GetActor();
			if (IsValid(HitActor)
				&& HitActor->GetClass()->ImplementsInterface(
					UPEPInteractionInteractable::StaticClass()))
			{
				FPEPInteractionSelectionCandidate& Candidate = SelectionCandidates.AddDefaulted_GetRef();
				Candidate.Actor = HitActor;
				Candidate.Score = CalculateCandidateScore(
					HitActor,
					PEPInteractionSelection::GetInteractionBounds(HitActor),
					Start,
					ViewForward,
					Candidate.AimAlignment,
					Candidate.Distance);
				/* The trace itself reached it. */
				Candidate.bVisibilityKnown = true;
				Candidate.bVisible = true;
			}
		}
	}
	else if (Range > KINDA_SMALL_NUMBER && SelectionRadius > 0.0f)
	{
		const FVector Center = Start + SelectionAxis * (Range * 0.5f);
		const FQuat ShapeRotation = FQuat::FindBetweenVectors(
			FVector::UpVector,
			SelectionAxis);
		const FCollisionShape CollisionShape = SelectionShape == EPEPInteractionSelectionShape::SphereSweep
			? FCollisionShape::MakeCapsule(SelectionRadius, (Range * 0.5f) + SelectionRadius)
			: FCollisionShape::MakeBox(FVector(SelectionRadius, SelectionRadius, Range * 0.5f));

		SelectionOverlaps.Reset();
		World->OverlapMultiByChannel(
			SelectionOverlaps,
			Center,
			ShapeRotation,
			TraceChannel,
			CollisionShape,
			QueryParams);

		SelectionSeenActors.Reset();
		for (const FOverlapResult& Overlap : SelectionOverlaps)
		{
			AActor* CandidateActor = Overlap.GetActor();
			if (!IsValid(CandidateActor)
				|| CandidateActor == Interactor
				|| !CandidateActor->GetClass()->ImplementsInterface(
					UPEPInteractionInteractable::StaticClass()))
			{
				continue;
			}

			bool bAlreadySeen = false;
			SelectionSeenActors.Add(CandidateActor, &bAlreadySeen);
			if (bAlreadySeen)
			{
				continue;
			}

			/* Bounds read once per candidate, for both the volume test and the score. */
			const FBox CandidateBounds = PEPInteractionSelection::GetInteractionBounds(CandidateActor);
			if (!IsWithinSelectionVolume(CandidateActor, CandidateBounds, Start, SelectionAxis))
			{
				continue;
			}

			FPEPInteractionSelectionCandidate& Candidate = SelectionCandidates.AddDefaulted_GetRef();
			Candidate.Actor = CandidateActor;
			Candidate.Score = CalculateCandidateScore(
				CandidateActor,
				CandidateBounds,
				Start,
				ViewForward,
				Candidate.AimAlignment,
				Candidate.Distance);
		}
		SelectionOverlaps.Reset();
		SelectionSeenActors.Reset();
	}

	/*
	 * Best first: highest score, then nearest. The selection is the best visible candidate, unless
	 * the previous target is still visible and within SelectionHysteresis of it. Line of sight is
	 * traced lazily, so usually only the winner is traced instead of every candidate.
	 */
	SelectionCandidates.Sort(
		[](const FPEPInteractionSelectionCandidate& A, const FPEPInteractionSelectionCandidate& B)
		{
			if (A.Score != B.Score)
			{
				return A.Score > B.Score;
			}
			return A.Distance < B.Distance;
		});

	int32 SelectedIndex = INDEX_NONE;
	for (int32 Index = 0; Index < SelectionCandidates.Num(); ++Index)
	{
		if (IsSelectionCandidateVisible(SelectionCandidates[Index], Start))
		{
			SelectedIndex = Index;
			break;
		}
	}

	if (SelectedIndex != INDEX_NONE && PreviousTarget)
	{
		const float BestScore = SelectionCandidates[SelectedIndex].Score;
		for (int32 Index = SelectedIndex + 1; Index < SelectionCandidates.Num(); ++Index)
		{
			FPEPInteractionSelectionCandidate& Candidate = SelectionCandidates[Index];
			if (Candidate.Actor != PreviousTarget)
			{
				continue;
			}

			/* Ranked before the winner, it was traced already and is not visible. */
			if (Candidate.Score + SelectionHysteresis >= BestScore
				&& IsSelectionCandidateVisible(Candidate, Start))
			{
				SelectedIndex = Index;
			}
			break;
		}
	}

	if (SelectedIndex != INDEX_NONE)
	{
		Target = SelectionCandidates[SelectedIndex].Actor;
		TargetSelectionScore = SelectionCandidates[SelectedIndex].Score;
	}
	SelectionCandidates.Reset();

#if ENABLE_DRAW_DEBUG
	if (bDebugDrawInteraction)
	{
		const FColor SelectionColor = Target.IsValid() ? FColor::Green : FColor::Red;
		if (bUseLineSelection)
		{
			DrawDebugLine(World, Start, End, SelectionColor, false, -1.0f, 0, 1.5f);
		}
		else if (SelectionShape == EPEPInteractionSelectionShape::SphereSweep)
		{
			DrawDebugCapsule(
				World,
				Start + SelectionAxis * (Range * 0.5f),
				(Range * 0.5f) + SelectionRadius,
				SelectionRadius,
				FQuat::FindBetweenVectors(FVector::UpVector, SelectionAxis),
				SelectionColor,
				false,
				-1.0f,
				0,
				1.0f);
		}
		else
		{
			DrawDebugCylinder(
				World,
				Start,
				End,
				SelectionRadius,
				16,
				SelectionColor,
				false,
				-1.0f,
				0,
				1.0f);
		}
	}
#endif
}

const USceneComponent* UPEPInteractionComponent::ResolveTraceStartComponent() const
{
	if (!Interactor || TraceStartSocketName.IsNone())
	{
		return nullptr;
	}

	const USceneComponent* Cached = CachedTraceStartComponent.Get();
	if (Cached
		&& Cached->GetOwner() == Interactor
		&& Cached->DoesSocketExist(TraceStartSocketName))
	{
		return Cached;
	}

	CachedTraceStartComponent.Reset();
	TInlineComponentArray<USceneComponent*> SceneComponents(Interactor.Get());
	for (const USceneComponent* SceneComponent : SceneComponents)
	{
		if (IsValid(SceneComponent)
			&& SceneComponent->DoesSocketExist(TraceStartSocketName))
		{
			CachedTraceStartComponent = SceneComponent;
			return SceneComponent;
		}
	}
	return nullptr;
}

void UPEPInteractionComponent::GetInteractionViewPoint(
	FVector& OutLocation,
	FRotator& OutRotation) const
{
	OutLocation = FVector::ZeroVector;
	OutRotation = FRotator::ZeroRotator;
	if (!Interactor)
	{
		return;
	}

	FTransform ViewTransform;
	if (const USceneComponent* SocketComponent = ResolveTraceStartComponent())
	{
		ViewTransform = SocketComponent->GetSocketTransform(
			TraceStartSocketName,
			RTS_World);
	}
	else
	{
		/* Replicated state only (location, eye height, view rotation): the authority computes the same. */
		FVector EyesLocation;
		FRotator EyesRotation;
		Interactor->GetActorEyesViewPoint(
			EyesLocation,
			EyesRotation);
		ViewTransform = FTransform(EyesRotation, EyesLocation);
	}

	OutLocation = ViewTransform.TransformPosition(TraceStartOffset);
	OutRotation = ViewTransform.Rotator();
}

FVector UPEPInteractionComponent::GetTraceStartLocation() const
{
	FVector ViewLocation;
	FRotator ViewRotation;
	GetInteractionViewPoint(
		ViewLocation,
		ViewRotation);
	return ViewLocation;
}

FVector UPEPInteractionComponent::GetSelectionAxis() const
{
	if (SelectionShape == EPEPInteractionSelectionShape::Cylinder
		&& CylinderAxisSource == EPEPInteractionCylinderAxisSource::InteractorForward
		&& Interactor)
	{
		return Interactor->GetActorForwardVector().GetSafeNormal();
	}

	if (Camera)
	{
		return Camera->GetForwardVector().GetSafeNormal();
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	GetInteractionViewPoint(
		ViewLocation,
		ViewRotation);
	return ViewRotation.Vector();
}

bool UPEPInteractionComponent::IsWithinSelectionVolume(
	AActor* Candidate,
	const FBox& CandidateBounds,
	const FVector& Start,
	const FVector& Axis) const
{
	if (!IsValid(Candidate))
	{
		return false;
	}

	const FVector BoundsOrigin = CandidateBounds.GetCenter();
	const float BoundsRadius = CandidateBounds.GetExtent().Size();
	const FVector ToBounds = BoundsOrigin - Start;
	const float AxialDistance = FVector::DotProduct(ToBounds, Axis);
	const FVector RadialOffset = ToBounds - Axis * AxialDistance;
	const float ExpandedRadius = SelectionRadius + BoundsRadius;

	if (SelectionShape == EPEPInteractionSelectionShape::SphereSweep)
	{
		const float ClosestAxisDistance = FMath::Clamp(AxialDistance, 0.0f, Range);
		const FVector ClosestPoint = Start + Axis * ClosestAxisDistance;
		return FVector::DistSquared(BoundsOrigin, ClosestPoint)
			<= FMath::Square(ExpandedRadius);
	}

	return AxialDistance >= -BoundsRadius
		&& AxialDistance <= Range + BoundsRadius
		&& RadialOffset.SizeSquared() <= FMath::Square(ExpandedRadius);
}

float UPEPInteractionComponent::CalculateCandidateScore(
	AActor* Candidate,
	const FBox& CandidateBounds,
	const FVector& Start,
	const FVector& ViewForward,
	float& OutAimAlignment,
	float& OutDistance) const
{
	const FVector ToCandidate = CandidateBounds.GetCenter() - Start;
	OutDistance = ToCandidate.Size();
	OutAimAlignment = OutDistance > KINDA_SMALL_NUMBER
		? FMath::Clamp(FVector::DotProduct(ViewForward, ToCandidate / OutDistance), 0.0f, 1.0f)
		: 1.0f;

	float SelectionPriority = 0.0f;
	if (const UPEPInteractionTargetComponent* TargetSettings =
		Candidate ? Candidate->FindComponentByClass<UPEPInteractionTargetComponent>() : nullptr)
	{
		SelectionPriority = FMath::Clamp(TargetSettings->SelectionPriority, 0.0f, 1.0f);
	}

	const float Proximity = Range > KINDA_SMALL_NUMBER
		? 1.0f - FMath::Clamp(OutDistance / Range, 0.0f, 1.0f)
		: 1.0f;
	const float TotalWeight = PriorityScoreWeight + AimScoreWeight + DistanceScoreWeight;
	if (TotalWeight <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float WeightedScore =
		(SelectionPriority * PriorityScoreWeight)
		+ (OutAimAlignment * AimScoreWeight)
		+ (Proximity * DistanceScoreWeight);
	return WeightedScore / TotalWeight;
}

bool UPEPInteractionComponent::ShouldPerformLocalSelection() const
{
	const AActor* SelectingActor = Interactor.Get();
	if (!SelectingActor)
	{
		return false;
	}

	/* False on a server for a remote player's pawn, and on every simulated proxy. */
	if (const APawn* Pawn = Cast<APawn>(SelectingActor))
	{
		return Pawn->IsLocallyControlled();
	}

	/* Any other interactor selects for the player controller owning it, directly or up the chain. */
	for (const AActor* Candidate = SelectingActor; Candidate; Candidate = Candidate->GetOwner())
	{
		if (const APlayerController* PlayerController = Cast<APlayerController>(Candidate))
		{
			return PlayerController->IsLocalController();
		}
	}
	return false;
}

void UPEPInteractionComponent::NotifySelectionIfChanged()
{
	AActor* CurrentTarget =
		Target.Get();

	const bool bHasCurrentTarget =
		IsValid(CurrentTarget);

	AActor* PreviousTarget =
		LastNotifiedTarget.Get();
	const bool bTargetChanged = bSelectionNotificationHadTarget != bHasCurrentTarget
		|| PreviousTarget != CurrentTarget;

	TArray<FGameplayTag> Options;
	if (bHasCurrentTarget)
	{
		Options =
			GetAvailableInteractionTags();
	}
	if (!bTargetChanged && Options == LastNotifiedOptions)
	{
		return;
	}

	/*
	 * Broadcast order matters here: a listener rebuilding a menu from OnInteractionSelectionChanged
	 * needs its entries to exist before OnSelectedOptionChanged asks it to highlight one of them.
	 */
	OnInteractionSelectionChanged.Broadcast(
		bHasCurrentTarget ? CurrentTarget : nullptr,
		Options);

	const FGameplayTag PreviousOption = SelectedOptionTag;
	if (Options.IsEmpty())
	{
		SelectedOptionTag = FGameplayTag();
	}
	else if (!Options.Contains(SelectedOptionTag))
	{
		SelectedOptionTag = Options[0];
	}
	if (SelectedOptionTag != PreviousOption)
	{
		OnSelectedOptionChanged.Broadcast(SelectedOptionTag);
	}

	LastNotifiedTarget =
		bHasCurrentTarget ? CurrentTarget : nullptr;
	bSelectionNotificationHadTarget =
		bHasCurrentTarget;
	LastNotifiedOptions = Options;
}

void UPEPInteractionComponent::ResetSelectedOption(const TArray<FGameplayTag>& Options)
{
	const FGameplayTag PreviousTag = SelectedOptionTag;
	SelectedOptionTag = Options.IsEmpty() ? FGameplayTag() : Options[0];

	if (SelectedOptionTag != PreviousTag)
	{
		OnSelectedOptionChanged.Broadcast(SelectedOptionTag);
	}
}

FGameplayTag UPEPInteractionComponent::GetSelectedInteractionTag() const
{
	return SelectedOptionTag;
}

void UPEPInteractionComponent::CycleSelectedOption(const int32 Delta)
{
	const TArray<FGameplayTag> Options = GetAvailableInteractionTags();
	if (Options.IsEmpty())
	{
		ResetSelectedOption(Options);
		return;
	}

	int32 CurrentIndex = Options.IndexOfByKey(SelectedOptionTag);
	if (CurrentIndex == INDEX_NONE)
	{
		CurrentIndex = 0;
	}

	const int32 Num = Options.Num();
	const int32 NewIndex = ((CurrentIndex + Delta) % Num + Num) % Num;

	const FGameplayTag PreviousTag = SelectedOptionTag;
	SelectedOptionTag = Options[NewIndex];

	if (SelectedOptionTag != PreviousTag)
	{
		OnSelectedOptionChanged.Broadcast(SelectedOptionTag);
	}
}

void UPEPInteractionComponent::ResetTargetSelection()
{
	const bool bHadSelection = bSelectionNotificationHadTarget
		|| !LastNotifiedOptions.IsEmpty()
		|| SelectedOptionTag.IsValid();

	Target.Reset();
	LastNotifiedTarget.Reset();
	bSelectionNotificationHadTarget = false;
	LastNotifiedOptions.Reset();

	/* Machines that do not select (servers for remote pawns, simulated proxies) stay silent. */
	if (!bHadSelection && !ShouldPerformLocalSelection())
	{
		return;
	}

	OnInteractionSelectionChanged.Broadcast(
		nullptr,
		{});

	ResetSelectedOption({});
}

AActor* UPEPInteractionComponent::GetSelectedTarget() const
{
	return Target.Get();
}

TArray<FGameplayTag> UPEPInteractionComponent::GetAvailableInteractionTags() const
{
	AActor* SelectedTarget = Target.Get();

	if (!Interactor
		|| !IsValid(SelectedTarget)
		|| !SelectedTarget->GetClass()->ImplementsInterface(
			UPEPInteractionInteractable::StaticClass()))
	{
		return {};
	}

	return IPEPInteractionInteractable::
		Execute_GetInteractOptionTags(
			SelectedTarget,
			Interactor);
}

void UPEPInteractionComponent::RecordDebugInteractionAttempt(
	AActor* RequestedTarget,
	const FGameplayTag InteractionTag,
	const EPEPRequestResult Result)
{
	DebugLastInteractionTarget = RequestedTarget;
	DebugLastInteractionTag = InteractionTag;
	DebugLastInteractionResult = Result;
	DebugLastInteractionTime = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f;
}

#if ENABLE_DRAW_DEBUG
void UPEPInteractionComponent::DrawInteractionDebug() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	AActor* CurrentTarget = Target.Get();
	const TArray<FGameplayTag> Options =
		IsValid(CurrentTarget) ? GetAvailableInteractionTags() : TArray<FGameplayTag>();

	if (IsValid(CurrentTarget))
	{
		DrawDebugSphere(
			World,
			CurrentTarget->GetActorLocation(),
			20.0f,
			12,
			FColor::Green,
			false,
			-1.0f,
			0,
			1.0f);

		TArray<FString> OptionStrings;
		for (const FGameplayTag& Option : Options)
		{
			OptionStrings.Add(Option.ToString());
		}

		DrawDebugString(
			World,
			CurrentTarget->GetActorLocation() + FVector(0.0f, 0.0f, 30.0f),
			FString::Printf(
				TEXT("Target: %s\nScore: %.2f\nOptions: %s"),
				*CurrentTarget->GetName(),
				TargetSelectionScore,
				*FString::Join(OptionStrings, TEXT(", "))),
			nullptr,
			FColor::Green,
			0.0f);
	}

	if (!GEngine)
	{
		return;
	}

	const int32 BaseKey = GetOwner() ? static_cast<int32>(GetOwner()->GetUniqueID() * 10) : 0;

	GEngine->AddOnScreenDebugMessage(
		BaseKey + 1,
		0.0f,
		FColor::Cyan,
		FString::Printf(
			TEXT("[PEPInteraction] Target: %s"),
			IsValid(CurrentTarget) ? *CurrentTarget->GetName() : TEXT("None")));

	const bool bHasRecentAttempt =
		DebugLastInteractionTag.IsValid()
		&& World->GetTimeSeconds() - DebugLastInteractionTime < 2.0f;

	FString InteractionLine;
	if (bHasRecentAttempt)
	{
		/* An actual attempt just happened: show what it was and how it resolved. */
		InteractionLine = FString::Printf(
			TEXT("[PEPInteraction] Interaction: %s on %s -> %s"),
			*DebugLastInteractionTag.ToString(),
			DebugLastInteractionTarget.IsValid() ? *DebugLastInteractionTarget->GetName() : TEXT("None"),
			*UEnum::GetValueAsString(DebugLastInteractionResult));
	}
	else if (GetSelectedInteractionTag().IsValid())
	{
		/* Nothing attempted yet: show what pressing interact right now would use. */
		InteractionLine = FString::Printf(
			TEXT("[PEPInteraction] Interaction: %s (ready)"),
			*GetSelectedInteractionTag().ToString());
	}
	else
	{
		InteractionLine = TEXT("[PEPInteraction] Interaction: none");
	}

	GEngine->AddOnScreenDebugMessage(BaseKey + 2, 0.0f, FColor::Yellow, InteractionLine);

	if (IsInteractionInProgress())
	{
		const AActor* ActiveTarget = GetActiveInteractionTarget();
		GEngine->AddOnScreenDebugMessage(
			BaseKey + 3,
			0.0f,
			FColor::Orange,
			FString::Printf(
				TEXT("[PEPInteraction] In progress: %s on %s (%.0f%% of %.2fs)"),
				*GetActiveInteractionTag().ToString(),
				ActiveTarget ? *ActiveTarget->GetName() : TEXT("None"),
				GetInteractionProgress() * 100.0f,
				GetActiveInteractionDuration()));
	}
}
#endif
