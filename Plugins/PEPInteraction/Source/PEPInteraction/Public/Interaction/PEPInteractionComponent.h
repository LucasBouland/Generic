#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/PEPTypes.h"
#include "Interaction/PEPInteractionTypes.h"
#include "Interaction/PEPInteractionSelectionTypes.h"

#include "PEPInteractionComponent.generated.h"

class UCameraComponent;

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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnPEPInteractionCancelled,
	AActor*, Target,
	FGameplayTag, InteractionTag,
	FGameplayTag, ReasonTag);

/**
 * Selects interaction targets for the local player and runs interactions on the authority.
 *
 * Selection is local and never replicated. Execution is authoritative: instant options execute
 * as soon as the authority accepts the request; timed options (GetInteractionDuration > 0) start
 * an active interaction that the authority re-validates every tick and executes when its duration
 * elapses. The active interaction replicates to the owning client for progress display.
 *
 * Events: OnInteractionStarted, OnInteractionCompleted and OnInteractionCancelled fire on the
 * authority and on the owning client. Every request the authority handles ends with exactly one
 * OnInteractionCompleted (carrying the execution result, success or failure, including requests
 * rejected by validation) or, for a timed interaction that started, OnInteractionCancelled. The
 * only exception is a repeated request for the interaction already in progress, which is absorbed.
 * Changing the local selection never cancels a timed interaction: cancellation is explicit
 * (RequestCancelInteraction, CancelInteraction) or the result of failed re-validation.
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

	UFUNCTION(BlueprintCallable, Category="PEP|Interaction")
	void Init(AActor* InInteractor);

	UFUNCTION(BlueprintPure, Category="PEP|Interaction")
	AActor* GetSelectedTarget() const;

	UFUNCTION(BlueprintCallable, Category="PEP|Interaction")
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
	 * returns Submitted once the request is sent; the outcome arrives through the events.
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Interaction")
	EPEPRequestResult RequestInteractWithInteractionTag(
		FGameplayTag InteractionTag);

	/*
	 * Authority executor: validates, then executes immediately, ignoring GetInteractionDuration.
	 *
	 * For server-side code (an ability) that owns its own timing. Fires OnInteractionCompleted.
	 * A timed interaction already in progress is left running.
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Interaction")
	FPEPInteractionResult ExecuteInteractWithInteractionTag(
		AActor* RequestedTarget,
		FGameplayTag InteractionTag);

	/*
	 * Authority entry point honouring GetInteractionDuration.
	 *
	 * Validates, then executes immediately when the option is instant, or starts a timed
	 * interaction (returning success without executing yet). Starting one cancels the previous one
	 * with Reason.Interaction.Cancelled; requesting the interaction already in progress keeps it
	 * running and returns success. Rejections fire OnInteractionCompleted with the failure.
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Interaction")
	FPEPInteractionResult BeginInteractWithInteractionTag(
		AActor* RequestedTarget,
		FGameplayTag InteractionTag);

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
	UFUNCTION(BlueprintCallable, Category="PEP|Interaction")
	bool CancelInteraction(
		UPARAM(meta=(Categories="Reason")) FGameplayTag ReasonTag);

	/** True while a timed interaction runs. Valid on the authority and on the owning client. */
	UFUNCTION(BlueprintPure, Category="PEP|Interaction")
	bool IsInteractionInProgress() const;

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
	UFUNCTION(Server, Reliable)
	void Server_RequestInteractWithInteractionTag(
		AActor* RequestedTarget,
		FGameplayTag InteractionTag);

	UFUNCTION(Server, Reliable)
	void Server_RequestCancelInteraction();

	/*
	 * Outcome notifications for the owning client. Reliable RPCs rather than replicated state:
	 * the cleared ActiveInteraction cannot say how an interaction ended, and an instant one never
	 * appears in it at all. InteractionId is 0 for instant executions.
	 */
	UFUNCTION(Client, Reliable)
	void Client_NotifyInteractionCompleted(
		int32 InteractionId,
		const FPEPInteractionResult& Result);

	UFUNCTION(Client, Reliable)
	void Client_NotifyInteractionCancelled(
		int32 InteractionId,
		AActor* CancelledTarget,
		FGameplayTag InteractionTag,
		FGameplayTag ReasonTag);

	UFUNCTION()
	void OnRep_ActiveInteraction();

	/*
	 * Full authority validation: target, range (Range + ServerRangeTolerance), offered options,
	 * line of sight and CanInteract. Used for requests and for every tick of a timed interaction.
	 */
	virtual FPEPInteractionResult ValidateInteraction(
		AActor* RequestedTarget,
		FGameplayTag InteractionTag) const;

	virtual bool ValidateLineOfSight(
		AActor* RequestedTarget) const;

	virtual bool HasLineOfSightToTarget(
		AActor* RequestedTarget,
		const FVector& ViewLocation) const;

	void RefreshTarget();

	virtual FVector GetTraceStartLocation() const;

	virtual FVector GetSelectionAxis() const;

	virtual bool IsWithinSelectionVolume(
		AActor* Candidate,
		const FVector& Start,
		const FVector& Axis) const;

	virtual float CalculateCandidateScore(
		AActor* Candidate,
		const FVector& Start,
		const FVector& ViewForward,
		float& OutAimAlignment,
		float& OutDistance) const;

	virtual bool ShouldPerformLocalSelection() const;

	/* Asks the target for the option's duration. Override to scale it (skills, buffs, ...). */
	virtual float ResolveInteractionDuration(
		AActor* RequestedTarget,
		FGameplayTag InteractionTag) const;

	/*
	 * Authority: re-validates the timed interaction in progress at ServerTimeSeconds, cancelling it
	 * with the validation reason on failure, and executes it once its duration has elapsed.
	 * Called from TickComponent with GetInteractionClockSeconds().
	 */
	virtual void TickActiveInteraction(double ServerTimeSeconds);

	/* Server world time from the GameState when there is one, else the world's own time. */
	double GetInteractionClockSeconds() const;

	float GetInteractionProgressAtTime(double ServerTimeSeconds) const;

	bool HasInteractionAuthority() const;

	/* Validates then calls ExecuteInteraction on the target, without notifying anyone. */
	FPEPInteractionResult ValidateAndExecuteInteraction(
		AActor* RequestedTarget,
		FGameplayTag InteractionTag);

	void StartActiveInteraction(
		AActor* RequestedTarget,
		FGameplayTag InteractionTag,
		float Duration);

	void CompleteActiveInteraction();

	/* Forgets the active interaction, remembering its id as ended. */
	void ClearActiveInteraction();

	void NotifyInteractionCompleted(
		int32 InteractionId,
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

	/* Authority: last id allocated to a timed interaction. */
	UPROPERTY(Transient)
	int32 LastAllocatedInteractionId = 0;

	/*
	 * Highest interaction id known to have ended. On the owning client the end notification can
	 * arrive before the replicated state clears; this keeps that stale state from reading as active
	 * or from broadcasting a late OnInteractionStarted.
	 */
	UPROPERTY(Transient)
	int32 LastEndedInteractionId = 0;

	/* Last id OnInteractionStarted was broadcast for, so a re-sent state does not broadcast twice. */
	UPROPERTY(Transient)
	int32 LastStartedInteractionId = 0;

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

	/** Optional bone or socket on the Interactor's scene components. None uses the camera. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category="PEP|Interaction|Selection")
	FName TraceStartSocketName = NAME_None;

	/** Local-space offset from the selected socket, or from the camera when no socket is set. */
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
};
