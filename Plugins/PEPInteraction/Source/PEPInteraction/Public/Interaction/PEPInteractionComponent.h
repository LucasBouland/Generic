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

UCLASS(
	ClassGroup=(PEP),
	meta=(BlueprintSpawnableComponent))
class PEPINTERACTION_API UPEPInteractionComponent
	: public UActorComponent
{
	GENERATED_BODY()

public:
	UPEPInteractionComponent();

	virtual void BeginPlay() override;

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
	 * Point d'entrée depuis l'input local.
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Interaction")
	EPEPRequestResult RequestInteractWithInteractionTag(
		FGameplayTag InteractionTag);

	/*
	 * Exécuteur autoritaire.
	 *
	 * Peut aussi être appelé depuis une ability déjà exécutée côté serveur.
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Interaction")
	FPEPInteractionResult ExecuteInteractWithInteractionTag(
		AActor* RequestedTarget,
		FGameplayTag InteractionTag);

protected:
	UFUNCTION(Server, Reliable)
	void Server_RequestInteractWithInteractionTag(
		AActor* RequestedTarget,
		FGameplayTag InteractionTag);

	FPEPInteractionResult ValidateInteraction(
		AActor* RequestedTarget,
		FGameplayTag InteractionTag) const;

	bool ValidateLineOfSight(
		AActor* RequestedTarget) const;

	bool HasLineOfSightToTarget(
		AActor* RequestedTarget,
		const FVector& ViewLocation) const;

	void RefreshTarget();

	FVector GetTraceStartLocation() const;

	FVector GetSelectionAxis() const;

	bool IsWithinSelectionVolume(
		AActor* Candidate,
		const FVector& Start,
		const FVector& Axis) const;

	float CalculateCandidateScore(
		AActor* Candidate,
		const FVector& Start,
		const FVector& ViewForward,
		float& OutAimAlignment,
		float& OutDistance) const;

	bool ShouldPerformLocalSelection() const;

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
