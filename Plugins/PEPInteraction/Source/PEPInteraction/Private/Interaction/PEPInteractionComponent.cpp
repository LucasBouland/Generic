#include "Interaction/PEPInteractionComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Core/PEPCoreTags.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Interaction/PEPInteractionInteractable.h"
#include "Interaction/PEPInteractionTargetComponent.h"

namespace PEPInteractionSelection
{
	struct FCandidate
	{
		AActor* Actor = nullptr;
		float Score = -1.0f;
		float AimAlignment = 0.0f;
		float Distance = 0.0f;
	};
}

UPEPInteractionComponent::UPEPInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UPEPInteractionComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UPEPInteractionComponent::Init(AActor* InInteractor)
{
	Interactor = InInteractor;
	if (!Interactor)
	{
		UE_LOG(LogTemp, Error, TEXT("Can't find Interactor"));
		return;
	}
	Camera = Interactor->FindComponentByClass<UCameraComponent>();
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

	if (Owner->HasAuthority())
	{
		const FPEPInteractionResult Result =
			ExecuteInteractWithInteractionTag(
				RequestedTarget,
				InteractionTag);

		return RecordAndReturn(
			Result.bSuccess
				? EPEPRequestResult::Executed
				: EPEPRequestResult::Failed);
	}

	Server_RequestInteractWithInteractionTag(
		RequestedTarget,
		InteractionTag);

	return RecordAndReturn(EPEPRequestResult::Submitted);
}

void UPEPInteractionComponent::
Server_RequestInteractWithInteractionTag_Implementation(
	AActor* RequestedTarget,
	const FGameplayTag InteractionTag)
{
	ExecuteInteractWithInteractionTag(
		RequestedTarget,
		InteractionTag);
}

FPEPInteractionResult
UPEPInteractionComponent::ValidateInteraction(
	AActor* RequestedTarget,
	const FGameplayTag InteractionTag) const
{
	const AActor* Owner = GetOwner();

	if (!Owner || !Owner->HasAuthority())
	{
		return FPEPInteractionResult::Failure(
			PEPCoreTags::Reason_NotAuthority,
			RequestedTarget,
			InteractionTag);
	}

	if (!Interactor
		|| !IsValid(RequestedTarget)
		|| !InteractionTag.IsValid()
		|| RequestedTarget == Interactor)
	{
		return FPEPInteractionResult::Failure(
			PEPCoreTags::Reason_InvalidTarget,
			RequestedTarget,
			InteractionTag);
	}

	if (RequestedTarget->GetWorld() != Interactor->GetWorld())
	{
		return FPEPInteractionResult::Failure(
			PEPCoreTags::Reason_InvalidTarget,
			RequestedTarget,
			InteractionTag);
	}

	if (!RequestedTarget->GetClass()->ImplementsInterface(
			UPEPInteractionInteractable::StaticClass()))
	{
		return FPEPInteractionResult::Failure(
			PEPCoreTags::Reason_InvalidTarget,
			RequestedTarget,
			InteractionTag);
	}

	const float MaxDistance =
		Range + ServerRangeTolerance;

	const float DistanceSquared =
		FVector::DistSquared(
			Interactor->GetActorLocation(),
			RequestedTarget->GetActorLocation());

	if (DistanceSquared > FMath::Square(MaxDistance))
	{
		return FPEPInteractionResult::Failure(
			PEPCoreTags::Reason_OutOfRange,
			RequestedTarget,
			InteractionTag);
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
			PEPCoreTags::Reason_InvalidTarget,
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

	FVector ViewLocation;
	FRotator ViewRotation;
	if (TraceStartSocketName.IsNone())
	{
		Interactor->GetActorEyesViewPoint(
			ViewLocation,
			ViewRotation);
		ViewLocation += ViewRotation.RotateVector(TraceStartOffset);
	}
	else
	{
		ViewLocation = GetTraceStartLocation();
	}
	return HasLineOfSightToTarget(RequestedTarget, ViewLocation);
}

bool UPEPInteractionComponent::HasLineOfSightToTarget(
	AActor* RequestedTarget,
	const FVector& ViewLocation) const
{
	if (!RequestedTarget || !GetWorld())
	{
		return false;
	}

	TArray<UPrimitiveComponent*> PrimitiveComponents;
	RequestedTarget->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(PEPInteractionServerValidation),
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
		const bool bHit = GetWorld()->LineTraceSingleByChannel(
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

FPEPInteractionResult
UPEPInteractionComponent::ExecuteInteractWithInteractionTag(
	AActor* RequestedTarget,
	const FGameplayTag InteractionTag)
{
	const FPEPInteractionResult Validation =
		ValidateInteraction(
			RequestedTarget,
			InteractionTag);

	if (!Validation.bSuccess)
	{
		return Validation;
	}

	return IPEPInteractionInteractable::
		Execute_ExecuteInteraction(
			RequestedTarget,
			Interactor,
			InteractionTag);
}

void UPEPInteractionComponent::RefreshTarget()
{
	AActor* PreviousTarget = Target.Get();
	Target.Reset();
	TargetSelectionScore = 0.0f;
	if (!Camera && Interactor)
	{
		Camera = Interactor->FindComponentByClass<UCameraComponent>();
	}

	if (!Interactor || !Camera || !GetWorld())
	{
		return;
	}

	const FVector Start = GetTraceStartLocation();
	const FVector ViewForward = Camera->GetForwardVector().GetSafeNormal();
	const FVector SelectionAxis = GetSelectionAxis();
	const FVector End = Start + SelectionAxis * Range;

	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(PEPInteractionSelection),
		false);

	QueryParams.AddIgnoredActor(Interactor);

	TArray<PEPInteractionSelection::FCandidate> Candidates;
	const bool bUseLineSelection = SelectionShape == EPEPInteractionSelectionShape::Line
		|| SelectionRadius <= KINDA_SMALL_NUMBER;
	if (bUseLineSelection)
	{
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(
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
				float AimAlignment = 0.0f;
				float Distance = 0.0f;
				const float Score = CalculateCandidateScore(
					HitActor,
					Start,
					ViewForward,
					AimAlignment,
					Distance);
				Candidates.Add({HitActor, Score, AimAlignment, Distance});
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

		TArray<FOverlapResult> Overlaps;
		GetWorld()->OverlapMultiByChannel(
			Overlaps,
			Center,
			ShapeRotation,
			TraceChannel,
			CollisionShape,
			QueryParams);

		TSet<TWeakObjectPtr<AActor>> SeenActors;
		for (const FOverlapResult& Overlap : Overlaps)
		{
			AActor* Candidate = Overlap.GetActor();
			if (!IsValid(Candidate)
				|| Candidate == Interactor
				|| !Candidate->GetClass()->ImplementsInterface(
					UPEPInteractionInteractable::StaticClass())
				|| SeenActors.Contains(Candidate))
			{
				continue;
			}
			SeenActors.Add(Candidate);

			if (!IsWithinSelectionVolume(Candidate, Start, SelectionAxis)
				|| !HasLineOfSightToTarget(Candidate, Start))
			{
				continue;
			}

			float AimAlignment = 0.0f;
			float Distance = 0.0f;
			const float Score = CalculateCandidateScore(
				Candidate,
				Start,
				ViewForward,
				AimAlignment,
				Distance);
			Candidates.Add({Candidate, Score, AimAlignment, Distance});
		}
	}

	PEPInteractionSelection::FCandidate BestCandidate;
	PEPInteractionSelection::FCandidate PreviousCandidate;
	for (const PEPInteractionSelection::FCandidate& Candidate : Candidates)
	{
		if (Candidate.Score > BestCandidate.Score
			|| (FMath::IsNearlyEqual(Candidate.Score, BestCandidate.Score)
				&& Candidate.Distance < BestCandidate.Distance))
		{
			BestCandidate = Candidate;
		}

		if (Candidate.Actor == PreviousTarget)
		{
			PreviousCandidate = Candidate;
		}
	}

	if (PreviousCandidate.Actor
		&& BestCandidate.Actor != PreviousCandidate.Actor
		&& PreviousCandidate.Score + SelectionHysteresis >= BestCandidate.Score)
	{
		BestCandidate = PreviousCandidate;
	}

	if (BestCandidate.Actor)
	{
		Target = BestCandidate.Actor;
		TargetSelectionScore = BestCandidate.Score;
	}

#if ENABLE_DRAW_DEBUG
	if (bDebugDrawInteraction)
	{
		const FColor SelectionColor = BestCandidate.Actor ? FColor::Green : FColor::Red;
		if (bUseLineSelection)
		{
			DrawDebugLine(GetWorld(), Start, End, SelectionColor, false, -1.0f, 0, 1.5f);
		}
		else if (SelectionShape == EPEPInteractionSelectionShape::SphereSweep)
		{
			DrawDebugCapsule(
				GetWorld(),
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
				GetWorld(),
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

FVector UPEPInteractionComponent::GetTraceStartLocation() const
{
	FTransform StartTransform = Camera
		? Camera->GetComponentTransform()
		: FTransform::Identity;

	if (!Camera && Interactor)
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		Interactor->GetActorEyesViewPoint(
			ViewLocation,
			ViewRotation);
		StartTransform = FTransform(ViewRotation, ViewLocation);
	}

	if (Interactor && !TraceStartSocketName.IsNone())
	{
		TArray<USceneComponent*> SceneComponents;
		Interactor->GetComponents<USceneComponent>(SceneComponents);

		for (const USceneComponent* SceneComponent : SceneComponents)
		{
			if (IsValid(SceneComponent)
				&& SceneComponent->DoesSocketExist(TraceStartSocketName))
			{
				StartTransform = SceneComponent->GetSocketTransform(
					TraceStartSocketName,
					RTS_World);
				break;
			}
		}
	}

	return StartTransform.TransformPosition(TraceStartOffset);
}

FVector UPEPInteractionComponent::GetSelectionAxis() const
{
	if (SelectionShape == EPEPInteractionSelectionShape::Cylinder
		&& CylinderAxisSource == EPEPInteractionCylinderAxisSource::InteractorForward
		&& Interactor)
	{
		return Interactor->GetActorForwardVector().GetSafeNormal();
	}

	return Camera ? Camera->GetForwardVector().GetSafeNormal() : FVector::ForwardVector;
}

bool UPEPInteractionComponent::IsWithinSelectionVolume(
	AActor* Candidate,
	const FVector& Start,
	const FVector& Axis) const
{
	if (!IsValid(Candidate))
	{
		return false;
	}

	FVector BoundsOrigin;
	FVector BoundsExtent;
	Candidate->GetActorBounds(false, BoundsOrigin, BoundsExtent);
	const float BoundsRadius = BoundsExtent.Size();
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
	const FVector& Start,
	const FVector& ViewForward,
	float& OutAimAlignment,
	float& OutDistance) const
{
	FVector BoundsOrigin;
	FVector BoundsExtent;
	Candidate->GetActorBounds(false, BoundsOrigin, BoundsExtent);

	const FVector ToCandidate = BoundsOrigin - Start;
	OutDistance = ToCandidate.Size();
	OutAimAlignment = OutDistance > KINDA_SMALL_NUMBER
		? FMath::Clamp(FVector::DotProduct(ViewForward, ToCandidate / OutDistance), 0.0f, 1.0f)
		: 1.0f;

	float SelectionPriority = 0.0f;
	if (const UPEPInteractionTargetComponent* TargetSettings =
		Candidate->FindComponentByClass<UPEPInteractionTargetComponent>())
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
	return true;
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
	Target.Reset();
	LastNotifiedTarget.Reset();
	bSelectionNotificationHadTarget = false;
	LastNotifiedOptions.Reset();

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
}
#endif
