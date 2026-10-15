#include "Abilities/PEPInteractAbility.h"

#include "Ability/Tasks/SetTargetDataInstancedStruct.h"
#include "Ability/Tasks/WaitForInputKeyRelease.h"
#include "Components/GMCAbilityComponent.h"
#include "GameFramework/Actor.h"
#include "Interaction/PEPInteractionComponent.h"
#include "Interaction/PEPInteractionLogChannels.h"
#include "Interaction/PEPInteractionTags.h"

UPEPInteractAbility::UPEPInteractAbility()
{
	/* Interacting is irreversible: it must never run in a movement tick that can be replayed. */
	bActivateOnMovementTick = false;
}

void UPEPInteractAbility::BeginAbility()
{
	Super::BeginAbility();
	if (AbilityEnded())
	{
		return;
	}

	InteractionComponent = ResolveInteractionComponent();
	if (!InteractionComponent)
	{
		UE_LOG(LogPEPInteraction, Warning, TEXT("%s: no UPEPInteractionComponent on the ability owner."), *GetName());
		EndAbility();
		return;
	}

	/* Bound before anything is sent, so no outcome can arrive unheard. */
	BindInteractionEvents();

	/*
	 * Created on every machine so the task ids match on both sides. GMAS only sends the payload
	 * from the owning client or listen-server host; the authority's copy waits for it.
	 */
	UGMCAbilityTask_SetTargetDataInstancedStruct* TargetDataTask =
		UGMCAbilityTask_SetTargetDataInstancedStruct::SetTargetDataInstancedStruct(
			this,
			FInstancedStruct::Make(MakeLocalTargetData()));
	TargetDataTask->Completed.AddDynamic(this, &ThisClass::HandleTargetDataReceived);
	TargetDataTask->ReadyForActivation();
}

void UPEPInteractAbility::EndAbility()
{
	if (!AbilityEnded())
	{
		/* Unbound first: cancelling below must not re-enter this ability through its own events. */
		UnbindInteractionEvents();
		if (bCancelInteractionOnAbilityEnd)
		{
			CancelRequestedInteraction(PEPInteractionTags::Reason_Interaction_Interrupted);
		}
	}
	Super::EndAbility();
}

void UPEPInteractAbility::CancelAbility()
{
	if (!AbilityEnded())
	{
		UnbindInteractionEvents();
		if (bCancelInteractionOnAbilityEnd)
		{
			CancelRequestedInteraction(PEPInteractionTags::Reason_Interaction_Interrupted);
		}
	}
	Super::CancelAbility();
}

UPEPInteractionComponent* UPEPInteractAbility::GetInteractionComponent() const
{
	return InteractionComponent;
}

UPEPInteractionComponent* UPEPInteractAbility::ResolveInteractionComponent() const
{
	const AActor* Owner = OwnerAbilityComponent ? GetOwnerActor() : nullptr;
	return Owner ? Owner->FindComponentByClass<UPEPInteractionComponent>() : nullptr;
}

FPEPInteractAbilityTargetData UPEPInteractAbility::MakeLocalTargetData() const
{
	FPEPInteractAbilityTargetData Data;
	if (InteractionComponent)
	{
		Data.Target = InteractionComponent->GetSelectedTarget();
		Data.InteractionTag = InteractionComponent->GetSelectedInteractionTag();
		if (Data.Target && Data.InteractionTag.IsValid())
		{
			/* Reserved where the request is made: the owning client, or the listen-server host. */
			Data.InteractionId = InteractionComponent->ReserveInteractionId();
		}
	}
	return Data;
}

void UPEPInteractAbility::HandleTargetDataReceived(FInstancedStruct TargetData)
{
	if (AbilityEnded())
	{
		return;
	}

	const FInstancedStruct& Payload = TargetData;
	const FPEPInteractAbilityTargetData* Request =
		Payload.GetPtr<FPEPInteractAbilityTargetData>();

	/*
	 * An empty selection is still sent rather than skipped: both machines read the same payload
	 * and end here together, instead of leaving the authority waiting for data that never comes.
	 */
	if (!InteractionComponent
		|| !Request
		|| !Request->Target
		|| !Request->InteractionTag.IsValid())
	{
		EndAbility();
		return;
	}

	AActor* RequestedTarget = Request->Target;
	const FGameplayTag RequestedInteractionTag = Request->InteractionTag;
	RequestedInteractionId = Request->InteractionId;
	bInteractionRequested = true;

	/*
	 * Also created on every machine before the authority acts, to keep task ids paired. Without an
	 * input action GMAS completes this task at once, so it is skipped on every machine alike.
	 */
	if (bCancelOnInputRelease && AbilityInputAction)
	{
		UGMCAbilityTask_WaitForInputKeyRelease* ReleaseTask =
			UGMCAbilityTask_WaitForInputKeyRelease::WaitForKeyRelease(
				this,
				true,
				0.0f);
		ReleaseTask->Completed.AddDynamic(this, &ThisClass::HandleInputReleased);
		ReleaseTask->ReadyForActivation();
	}

	if (!CanRunAuthorityLogic())
	{
		/* The owning client waits for the outcome events, or for its input release. */
		return;
	}

	/*
	 * Executes now when the option is instant, otherwise starts the timed interaction. Instant
	 * executions and rejections report through OnInteractionCompleted during this call, which
	 * ends this ability when the component kept the requested id.
	 */
	const FPEPInteractionResult Result =
		InteractionComponent->BeginInteractWithInteractionTag(
			RequestedTarget,
			RequestedInteractionTag,
			RequestedInteractionId);

	if (AbilityEnded())
	{
		return;
	}

	/*
	 * The component may have handled the request under another id (none sent, one it could not
	 * accept, or a repeat joining the interaction in progress): follow that one from now on.
	 * Only a timed interaction still running under it keeps the ability alive.
	 */
	RequestedInteractionId = Result.InteractionId;
	const bool bTimedInteractionRunning = Result.bSuccess
		&& RequestedInteractionId != 0
		&& InteractionComponent->GetActiveInteractionId() == RequestedInteractionId;

	if (!bTimedInteractionRunning)
	{
		EndAbility();
	}
}

void UPEPInteractAbility::HandleInputReleased(float HeldDuration)
{
	if (AbilityEnded())
	{
		return;
	}

	/* The release payload reaches the authority after the target data, through the same queue. */
	CancelRequestedInteraction(PEPInteractionTags::Reason_Interaction_Cancelled);
	EndAbility();
}

void UPEPInteractAbility::HandleInteractionCompleted(const FPEPInteractionResult& Result)
{
	if (!bInteractionRequested
		|| AbilityEnded()
		|| !IsRequestedInteraction(Result.InteractionId))
	{
		return;
	}

	EndAbility();
}

void UPEPInteractAbility::HandleInteractionCancelled(
	const int32 InteractionId,
	AActor* CancelledTarget,
	const FGameplayTag InteractionTag,
	const FGameplayTag ReasonTag)
{
	if (!bInteractionRequested
		|| AbilityEnded()
		|| !IsRequestedInteraction(InteractionId))
	{
		return;
	}

	EndAbility();
}

bool UPEPInteractAbility::CanRunAuthorityLogic() const
{
	return OwnerAbilityComponent
		&& OwnerAbilityComponent->HasAuthority()
		&& !OwnerAbilityComponent->IsReplayingForGMASLogic();
}

bool UPEPInteractAbility::IsRequestedInteraction(const int32 InInteractionId) const
{
	return RequestedInteractionId != 0
		&& InInteractionId == RequestedInteractionId;
}

void UPEPInteractAbility::BindInteractionEvents()
{
	if (!InteractionComponent)
	{
		return;
	}

	InteractionComponent->OnInteractionCompleted.AddUniqueDynamic(
		this,
		&ThisClass::HandleInteractionCompleted);
	InteractionComponent->OnInteractionCancelled.AddUniqueDynamic(
		this,
		&ThisClass::HandleInteractionCancelled);
}

void UPEPInteractAbility::UnbindInteractionEvents()
{
	if (!InteractionComponent)
	{
		return;
	}

	InteractionComponent->OnInteractionCompleted.RemoveDynamic(
		this,
		&ThisClass::HandleInteractionCompleted);
	InteractionComponent->OnInteractionCancelled.RemoveDynamic(
		this,
		&ThisClass::HandleInteractionCancelled);
}

void UPEPInteractAbility::CancelRequestedInteraction(const FGameplayTag ReasonTag)
{
	if (!bInteractionRequested
		|| !InteractionComponent
		|| !CanRunAuthorityLogic()
		|| !IsRequestedInteraction(InteractionComponent->GetActiveInteractionId()))
	{
		return;
	}

	InteractionComponent->CancelInteraction(ReasonTag);
}
