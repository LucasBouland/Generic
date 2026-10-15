#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/PEPTypes.h"
#include "Engine/OverlapResult.h"
#include "Interaction/PEPInteractionTypes.h"
#include "Interaction/PEPInteractionSelectionTypes.h"

#include "PEPInteractionComponent.generated.h"

class AController;
class APawn;
class UCameraComponent;
class USceneComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnPEPInteractionSelectionChanged,
	AActor*, Target,
	const TArray<FGameplayTag>&, Options);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnPEPInteractionSelectedOptionChanged,
	FGameplayTag, SelectedOption);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnPEPInteractionStarted,
	AActor*, Target,
	FGameplayTag, InteractionTag,
	float, Duration);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnPEPInteractionCompleted,
	const FPEPInteractionResult&, Result);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FOnPEPInteractionCancelled,
	int32, InteractionId,
	AActor*, Target,
	FGameplayTag, InteractionTag,
	FGameplayTag, ReasonTag);

/**
 * Selects interaction targets for the local player and runs interactions on the authority.
 *
 * Selection is local and never replicated: it only runs for a locally controlled interactor
 * (ShouldPerformLocalSelection), never on a server for a remote pawn nor on a simulated proxy.
 * The component ticks only while it has something to do: local selection, or a timed interaction
 * running on the authority.
 *
 * Execution is authoritative: instant options execute as soon as the authority accepts the
 * request; timed options (GetInteractionDuration > 0) start an active interaction that the
 * authority re-validates while it runs and executes when its duration elapses. The active
 * interaction replicates to the owning client for progress display.
 *
 * Events: OnInteractionStarted, OnInteractionCompleted and OnInteractionCancelled fire on the
 * authority and on the owning client. Every request the authority handles ends with exactly one
 * OnInteractionCompleted (carrying the execution result, success or failure, including requests
 * rejected by validation) or, for a timed interaction that started, OnInteractionCancelled. Both
 * carry the request's InteractionId. The only exception is a repeated request for the interaction
 * already in progress, which is absorbed into it. Changing the local selection never cancels a
 * timed interaction: cancellation is explicit (RequestCancelInteraction, CancelInteraction) or the
 * result of failed re-validation.
 */
UCLASS(
	Blueprintable,
	ClassGroup=(PEP),
	meta=(BlueprintSpawnableComponent))
class PEPINTERACTION_API UPEPInteractionComponent
	: public UActorComponent
{
	GENERATED_BODY()

public:
	UPEPInteractionComponent();

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Call on every machine with the actor that interacts (usually the owning pawn). */
	UFUNCTION(BlueprintCallable, Category="PEP|Interaction")
	void Init(AActor* InInteractor);

	/**
	 * Re-evaluates whether the component needs to tick (local selection, or a timed interaction on
	 * the authority). Done automatically on Init, BeginPlay, controller changes of a pawn
	 * interactor and timed interaction start/end; call it after changing anything else
	 * ShouldPerformLocalSelection depends on (the owner of a non-pawn interactor, say).
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Interaction")
	void RefreshTickEnabled();

	UFUNCTION(BlueprintPure, Category="PEP|Interaction")
	AActor* GetSelectedTarget() const;

	UFUNCTION(BlueprintPure, Category="PEP|Interaction")
	TArray<FGameplayTag> GetAvailableInteractionTags() const;

	UFUNCTION(BlueprintCallable, Category="PEP|Interaction")
	void ResetTargetSelection();

	UPROPERTY(BlueprintAssignable, Category="PEP|Interaction")
	FOnPEPInteractionSelectionChanged OnInteractionSelectionChanged;

	/** The option Interact would act on right now. Invalid if the target has none. */
	UFUNCTION(BlueprintPure, Category="PEP|Interaction")
	FGameplayTag GetSelectedInteractionTag() const;

	/** Moves the selection within the current target's options, wrapping at either end. */
	UFUNCTION(BlueprintCallable, Category="PEP|Interaction")
	void CycleSelectedOption(int32 Delta);

	UPROPERTY(BlueprintAssignable, Category="PEP|Interaction")
	FOnPEPInteractionSelectedOptionChanged OnSelectedOptionChanged;

	/*
	 * Entry point for local input, acting on the currently selected target.
	 *
	 * The authority handles the request through BeginInteractWithInteractionTag: an instant option
	 * returns Executed or Failed, a timed option that started returns Submitted. A client always
	 * returns Submitted once the request is sent; the outcome arrives through the events, carrying
	 * GetLastRequestedInteractionId().
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Interaction")
	EPEPRequestResult RequestInteractWithInteractionTag(
		FGameplayTag InteractionTag);

	/*
	 * Authority executor: validates, then executes immediately, ignoring GetInteractionDuration.
	 *
	 * For server-side code (an ability) that owns its own timing. Fires OnInteractionCompleted.
	 * A timed interaction already in progress is left running. InteractionId is an id from
	 * ReserveInteractionId, or 0 to let the component allocate one; the result carries the id used.
	 */
	UFUNCTION(
		BlueprintCallable,
		BlueprintAuthorityOnly,
		Category="PEP|Interaction",
		meta=(AdvancedDisplay="InteractionId"))
	FPEPInteractionResult ExecuteInteractWithInteractionTag(
		AActor* RequestedTarget,
		FGameplayTag InteractionTag,
		int32 InteractionId = 0);

	/*
	 * Authority entry point honouring GetInteractionDuration.
	 *
	 * Validates, then executes immediately when the option is instant, or starts a timed
	 * interaction (returning success without executing yet). Starting one cancels the previous one
	 * with Reason.Interaction.Cancelled; requesting the interaction already in progress keeps it
	 * running and returns success with that interaction's id. Rejections fire
	 * OnInteractionCompleted with the failure. InteractionId is an id from ReserveInteractionId (on
	 * this machine, or the owning client's when relaying its request), or 0 to let the component
	 * allocate one; the result carries the id actually used.
	 */
	UFUNCTION(
		BlueprintCallable,
		BlueprintAuthorityOnly,
		Category="PEP|Interaction",
		meta=(AdvancedDisplay="InteractionId"))
	FPEPInteractionResult BeginInteractWithInteractionTag(
		AActor* RequestedTarget,
		FGameplayTag InteractionTag,
		int32 InteractionId = 0);

	/*
	 * Entry point for local input releasing a timed interaction.
	 *
	 * The authority cancels directly (Executed, or Failed if nothing was in progress). A client
	 * always sends the request (Submitted), even when it has not yet seen the interaction start,
	 * so that a quick tap still stops a hold the server has just begun.
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Interaction")
	EPEPRequestResult RequestCancelInteraction();

	/*
	 * Authority executor: stops the timed interaction in progress, if any, without executing it.
	 * An invalid ReasonTag falls back to Reason.Interaction.Cancelled. Returns false if nothing was
	 * in progress or this is not the authority.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Interaction")
	bool CancelInteraction(
		UPARAM(meta=(Categories="Reason")) FGameplayTag ReasonTag);

	/**
	 * Reserves the id of a request this machine is about to make, so that its outcome can be told
	 * apart from any other: positive on the authority, negative on a client. Unique per component.
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Interaction")
	int32 ReserveInteractionId();

	/** Id of the last request sent by RequestInteractWithInteractionTag on this machine; 0 if none. */
	UFUNCTION(BlueprintPure, Category="PEP|Interaction")
	int32 GetLastRequestedInteractionId() const;

	/** True while a timed interaction runs. Valid on the authority and on the owning client. */
	UFUNCTION(BlueprintPure, Category="PEP|Interaction")
	bool IsInteractionInProgress() const;

	/** Id of the timed interaction in progress; 0 when none. */
	UFUNCTION(BlueprintPure, Category="PEP|Interaction")
	int32 GetActiveInteractionId() const;

	UFUNCTION(BlueprintPure, Category="PEP|Interaction")
	AActor* GetActiveInteractionTarget() const;

	UFUNCTION(BlueprintPure, Category="PEP|Interaction")
	FGameplayTag GetActiveInteractionTag() const;

	/** Duration of the timed interaction in progress, in seconds; 0 when none. */
	UFUNCTION(BlueprintPure, Category="PEP|Interaction")
	float GetActiveInteractionDuration() const;

	/**
	 * Progress of the timed interaction in progress, from 0 to 1; 0 when none. On the owning client
	 * this is an estimate from the replicated server clock: the authority decides completion.
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Interaction")
	float GetInteractionProgress() const;

	UPROPERTY(BlueprintAssignable, Category="PEP|Interaction")
	FOnPEPInteractionStarted OnInteractionStarted;

	UPROPERTY(BlueprintAssignable, Category="PEP|Interaction")
	FOnPEPInteractionCompleted OnInteractionCompleted;

	UPROPERTY(BlueprintAssignable, Category="PEP|Interaction")
	FOnPEPInteractionCancelled OnInteractionCancelled;

protected:
	/* InteractionId is the client's own reservation (negative), echoed back in the outcome. */
	UFUNCTION(Server, Reliable)
	void Server_RequestInteractWithInteractionTag(
		AActor* RequestedTarget,
		FGameplayTag InteractionTag,
		int32 InteractionId);

	UFUNCTION(Server, Reliable)
	void Server_RequestCancelInteraction();

	/*
	 * Outcome notifications for the owning client. Reliable RPCs rather than replicated state:
	 * the cleared ActiveInteraction cannot say how an interaction ended, and an instant one never
	 * appears in it at all. The completion's id travels in Result.InteractionId.
	 */
	UFUNCTION(Client, Reliable)
	void Client_NotifyInteractionCompleted(
		const FPEPInteractionResult& Result);

	UFUNCTION(Client, Reliable)
	void Client_NotifyInteractionCancelled(
		int32 InteractionId,
		AActor* CancelledTarget,
		FGameplayTag InteractionTag,
		FGameplayTag ReasonTag);

	UFUNCTION()
	void OnRep_ActiveInteraction();

	UFUNCTION()
	void HandleInteractorControllerChanged(
		APawn* Pawn,
		AController* OldController,
		AController* NewController);

	/*
	 * Full authority validation: ValidateInteractionReach, then offered options, line of sight and
	 * CanInteract. Used for requests, and by a timed interaction at ActiveInteractionValidationInterval
	 * and before it executes.
	 */
	virtual FPEPInteractionResult ValidateInteraction(
		AActor* RequestedTarget,
		FGameplayTag InteractionTag) const;

	/*
	 * The cheap part of the validation, also run on every tick of a timed interaction: authority,
	 * a valid interactable target in the same world, and range (Range + ServerRangeTolerance) from
	 * the interaction view point to the closest point of the target's bounds.
	 */
	virtual FPEPInteractionResult ValidateInteractionReach(
		AActor* RequestedTarget,
		FGameplayTag InteractionTag) const;

	/* Line of sight from the interaction view point (GetInteractionViewPoint). */
	virtual bool ValidateLineOfSight(
		AActor* RequestedTarget) const;

	virtual bool HasLineOfSightToTarget(
		AActor* RequestedTarget,
		const FVector& ViewLocation) const;

	void RefreshTarget();

	/*
	 * The point interaction is measured from, shared by local selection and authority validation
	 * so that both agree: TraceStartSocketName on the interactor's scene components when set, else
	 * the interactor's eyes view point, with TraceStartOffset applied in that frame. Never the
	 * camera, which only exists locally and may sit far behind the interactor in third person.
	 */
	virtual void GetInteractionViewPoint(
		FVector& OutLocation,
		FRotator& OutRotation) const;

	/* Location part of GetInteractionViewPoint. */
	FVector GetTraceStartLocation() const;

	/* Direction of the selection shape: the camera's (the player's aim), or the interactor's for a cylinder set so. */
	virtual FVector GetSelectionAxis() const;

	virtual bool IsWithinSelectionVolume(
		AActor* Candidate,
		const FBox& CandidateBounds,
		const FVector& Start,
		const FVector& Axis) const;

	virtual float CalculateCandidateScore(
		AActor* Candidate,
		const FBox& CandidateBounds,
		const FVector& Start,
		const FVector& ViewForward,
		float& OutAimAlignment,
		float& OutDistance) const;

	/*
	 * Whether this machine selects for the interactor: true only for a locally controlled pawn, or
	 * for a non-pawn interactor owned by a local player controller. False on a server for a remote
	 * pawn and on simulated proxies. An override should call RefreshTickEnabled when its answer changes.
	 */
	virtual bool ShouldPerformLocalSelection() const;

	/* Asks the target for the option's duration. Override to scale it (skills, buffs, ...). */
	virtual float ResolveInteractionDuration(
		AActor* RequestedTarget,
		FGameplayTag InteractionTag) const;

	/*
	 * Authority: drives the timed interaction in progress at ServerTimeSeconds. Every call checks
	 * ValidateInteractionReach and cancels at once on failure. ValidateInteraction runs every
	 * ActiveInteractionValidationInterval and once the duration has elapsed; its failures cancel
	 * only once they have lasted ActiveInteractionValidationGracePeriod. Once the duration has
	 * elapsed, the first successful validation executes the interaction without validating again.
	 * Called from TickComponent with GetInteractionClockSeconds().
	 */
	virtual void TickActiveInteraction(double ServerTimeSeconds);

	/* Server world time from the GameState when there is one, else the world's own time. */
	double GetInteractionClockSeconds() const;

	float GetInteractionProgressAtTime(double ServerTimeSeconds) const;

	bool HasInteractionAuthority() const;

	/* Authority: the id a request is handled under, RequestedId when usable, else a fresh one. */
	int32 AcceptInteractionId(int32 RequestedId);

	/* Authority: whether a client request arriving now must be refused by MinClientRequestInterval. */
	bool IsClientRequestRateLimited();

	/* Calls ExecuteInteraction on an already validated target, without notifying anyone. */
	FPEPInteractionResult ExecuteValidatedInteraction(
		AActor* RequestedTarget,
		FGameplayTag InteractionTag,
		int32 InteractionId);

	void StartActiveInteraction(
		AActor* RequestedTarget,
		FGameplayTag InteractionTag,
		float Duration,
		int32 InteractionId);

	/* Executes the active interaction, which the caller has just validated. */
	void CompleteActiveInteraction();

	/* Forgets the active interaction, remembering its id as ended. */
	void ClearActiveInteraction();

	bool HasInteractionIdEnded(int32 InteractionId) const;

	void MarkInteractionIdEnded(int32 InteractionId);

	void NotifyInteractionCompleted(
		const FPEPInteractionResult& Result);

	void NotifyInteractionCancelled(
		int32 InteractionId,
		AActor* CancelledTarget,
		FGameplayTag InteractionTag,
		FGameplayTag ReasonTag);

	void NotifySelectionIfChanged();

	/* Réaligne l'option sélectionnée sur la première des options fournies, en broadcastant
	 * OnSelectedOptionChanged si elle diffère de l'ancienne. */
	void ResetSelectedOption(const TArray<FGameplayTag>& Options);

	/* Enregistre la dernière tentative d'interaction locale, pour l'affichage debug. */
	void RecordDebugInteractionAttempt(
		AActor* RequestedTarget,
		FGameplayTag InteractionTag,
		EPEPRequestResult Result);

#if ENABLE_DRAW_DEBUG
	/* Dessine la cible, la forme de sélection et la dernière interaction tentée. */
	void DrawInteractionDebug() const;
#endif

	UPROPERTY(Transient)
	TObjectPtr<AActor> Interactor = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> Camera = nullptr;

	/*
	 * Sélection locale seulement.
	 * Ne pas répliquer.
	 */
	UPROPERTY(VisibleInstanceOnly, Transient)
	TWeakObjectPtr<AActor> Target;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> LastNotifiedTarget;

	UPROPERTY(Transient)
	TArray<FGameplayTag> LastNotifiedOptions;

	UPROPERTY(Transient)
	bool bSelectionNotificationHadTarget = false;

	/* Option courante parmi GetAvailableInteractionTags(), ce que Interact enverrait maintenant. */
	UPROPERTY(Transient)
	FGameplayTag SelectedOptionTag;

	UPROPERTY(Transient)
	float TargetSelectionScore = 0.0f;

	/*
	 * Timed interaction run by the authority, replicated to the owning client only.
	 * Independent of the local selection: changing the selected target never cancels it.
	 */
	UPROPERTY(
		VisibleInstanceOnly,
		Transient,
		ReplicatedUsing=OnRep_ActiveInteraction,
		Category="PEP|Interaction")
	FPEPActiveInteraction ActiveInteraction;

	/* Authority: last id reserved here, counting up from 1. */
	UPROPERTY(Transient)
	int32 LastAllocatedInteractionId = 0;

	/* Owning client: last id reserved here, counting down from -1. */
	UPROPERTY(Transient)
	int32 LastProposedInteractionId = 0;

	/* Id of the last request sent by RequestInteractWithInteractionTag on this machine. */
	UPROPERTY(Transient)
	int32 LastRequestedInteractionId = 0;

	/*
	 * Ids of the latest interactions known to have ended, oldest first. On the owning client the
	 * end notification can arrive before the replicated state clears; this keeps that stale state
	 * from reading as active or from broadcasting a late OnInteractionStarted. A list rather than
	 * a high-water mark because client and authority ids come from two separate sequences.
	 */
	TArray<int32> RecentlyEndedInteractionIds;

	/* Last id OnInteractionStarted was broadcast for, so a re-sent state does not broadcast twice. */
	UPROPERTY(Transient)
	int32 LastStartedInteractionId = 0;

	/* Authority: server time of the next full validation of the timed interaction in progress. */
	double NextActiveInteractionValidationTime = 0.0;

	/* Authority: server time since which full validations keep failing; negative while they pass. */
	double ActiveInteractionFailingSince = -1.0;

	/* Authority: real time of the last client request accepted by the rate limit; negative if none. */
	double LastClientRequestRealTime = -1.0;

	/*
	 * Per-pass selection buffers, reset rather than reallocated on every local selection. Their
	 * contents never outlive the pass that filled them.
	 */
	TArray<FOverlapResult> SelectionOverlaps;
	TArray<FPEPInteractionSelectionCandidate> SelectionCandidates;
	TSet<const AActor*> SelectionSeenActors;

	/* Scene component carrying TraceStartSocketName, found once and checked on each use. */
	mutable TWeakObjectPtr<const USceneComponent> CachedTraceStartComponent;

	/* Pawn interactor whose controller changes re-evaluate RefreshTickEnabled. */
	TWeakObjectPtr<APawn> BoundInteractorPawn;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category="PEP|Interaction",
		meta=(ClampMin="0.0"))
	float Range = 1000.0f;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category="PEP|Interaction|Selection")
	EPEPInteractionSelectionShape SelectionShape = EPEPInteractionSelectionShape::SphereSweep;

	/** Optional bone or socket on the Interactor's scene components. None uses the interactor's eyes view point. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category="PEP|Interaction|Selection")
	FName TraceStartSocketName = NAME_None;

	/** Local-space offset from the selected socket, or from the eyes view point when no socket is set. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category="PEP|Interaction|Selection")
	FVector TraceStartOffset = FVector::ZeroVector;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category="PEP|Interaction|Selection",
		meta=(ClampMin="0.0"))
	float SelectionRadius = 35.0f;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category="PEP|Interaction|Selection",
		meta=(EditCondition="SelectionShape == EPEPInteractionSelectionShape::Cylinder", EditConditionHides))
	EPEPInteractionCylinderAxisSource CylinderAxisSource = EPEPInteractionCylinderAxisSource::CameraForward;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category="PEP|Interaction|Selection|Scoring",
		meta=(ClampMin="0.0"))
	float PriorityScoreWeight = 0.5f;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category="PEP|Interaction|Selection|Scoring",
		meta=(ClampMin="0.0"))
	float AimScoreWeight = 0.3f;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category="PEP|Interaction|Selection|Scoring",
		meta=(ClampMin="0.0"))
	float DistanceScoreWeight = 0.2f;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category="PEP|Interaction|Selection|Scoring",
		meta=(ClampMin="0.0", ClampMax="1.0"))
	float SelectionHysteresis = 0.05f;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category="PEP|Interaction",
		meta=(ClampMin="0.0"))
	float ServerRangeTolerance = 100.0f;

	/**
	 * Seconds between two checks of offered options, line of sight and CanInteract while a timed
	 * interaction runs (range is checked every tick). 0 checks every tick.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category="PEP|Interaction|Timed",
		meta=(ClampMin="0.0", Units="s"))
	float ActiveInteractionValidationInterval = 0.1f;

	/**
	 * How long those checks may keep failing before the timed interaction is cancelled, so that a
	 * brief occlusion does not end a hold. 0 cancels on the first failure. Never lets a failing
	 * interaction execute: completion waits for a passing check.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category="PEP|Interaction|Timed",
		meta=(ClampMin="0.0", Units="s"))
	float ActiveInteractionValidationGracePeriod = 0.25f;

	/**
	 * Minimum real time between two interaction requests the authority accepts from the owning
	 * client; earlier ones complete as failed with Reason.Interaction.RateLimited. A repeat of the
	 * interaction in progress is never limited. 0 disables the limit.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category="PEP|Interaction|Network",
		meta=(ClampMin="0.0", Units="s"))
	float MinClientRequestInterval = 0.1f;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category="PEP|Interaction")
	TEnumAsByte<ECollisionChannel> TraceChannel =
		ECC_Visibility;

	/* Affiche la cible courante, la forme de sélection et la dernière interaction tentée. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PEP|Interaction|Debug")
	bool bDebugDrawInteraction = false;

	/* Ne sert qu'à l'affichage debug ci-dessus. */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> DebugLastInteractionTarget;

	UPROPERTY(Transient)
	FGameplayTag DebugLastInteractionTag;

	UPROPERTY(Transient)
	EPEPRequestResult DebugLastInteractionResult = EPEPRequestResult::Failed;

	UPROPERTY(Transient)
	float DebugLastInteractionTime = -1.0f;

private:
	/* Scene component of the interactor carrying TraceStartSocketName; null when none is set or found. */
	const USceneComponent* ResolveTraceStartComponent() const;

	void BindInteractorController();

	void UnbindInteractorController();

	/* Lazily traces line of sight for a candidate of the current selection pass. */
	bool IsSelectionCandidateVisible(
		FPEPInteractionSelectionCandidate& Candidate,
		const FVector& Start) const;
};
