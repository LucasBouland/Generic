#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/PEPCoreTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/PEPInteractionComponent.h"
#include "Interaction/PEPInteractionInteractable.h"
#include "Interaction/PEPInteractionLibrary.h"
#include "Interaction/PEPInteractionTags.h"
#include "Tests/PEPInteractionTestTypes.h"
#include "UObject/UnrealType.h"

namespace PEPInteractionTest
{
	/**
	 * A throwaway game world with one authoritative interactor at the origin.
	 *
	 * Actors spawned in a standalone game world are authoritative, which is what the execution
	 * paths require. The world is never ticked: timed interactions are advanced explicitly with
	 * TickActiveInteractionForTest so that every step is deterministic.
	 */
	struct FFixture
	{
		UWorld* World = nullptr;
		AActor* Interactor = nullptr;
		UPEPInteractionTestComponent* Interaction = nullptr;
		UPEPInteractionTestListener* Listener = nullptr;

		FFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());

			Interactor = World->SpawnActor<AActor>();
			Interaction = NewObject<UPEPInteractionTestComponent>(Interactor);
			Interaction->RegisterComponent();
			Interaction->Init(Interactor);
			Interaction->SetRangeForTest(500.0f, 0.0f);

			Listener = NewObject<UPEPInteractionTestListener>(GetTransientPackage());
			Listener->AddToRoot();
			Listener->Bind(Interaction);

			World->BeginPlay();
		}

		~FFixture()
		{
			Listener->RemoveFromRoot();
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		/** A target offering Interaction.Open, placed within range unless told otherwise. */
		template <typename TargetType>
		TargetType* SpawnTarget(const FVector& Location = FVector(100.0f, 0.0f, 0.0f))
		{
			TargetType* Target = World->SpawnActor<TargetType>(Location, FRotator::ZeroRotator);
			Target->Options.Reset();
			Target->Options.Add(PEPInteractionTags::Open);
			return Target;
		}

		static FGameplayTag Open()
		{
			return PEPInteractionTags::Open;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInteractionDefaultsAreInstantTest,
	"PEP.Interaction.Interactable.DefaultsAreInstant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInteractionDefaultsAreInstantTest::RunTest(const FString& Parameters)
{
	using namespace PEPInteractionTest;
	FFixture Fixture;
	APEPInteractionTestTarget* Target = Fixture.SpawnTarget<APEPInteractionTestTarget>();

	TestEqual(
		TEXT("an interactable that does not say otherwise is instant"),
		IPEPInteractionInteractable::Execute_GetInteractionDuration(
			Target,
			Fixture.Interactor,
			FFixture::Open()),
		0.0f);

	/* The default notifications must be callable and inert. */
	IPEPInteractionInteractable::Execute_OnInteractionStarted(
		Target,
		Fixture.Interactor,
		FFixture::Open());
	IPEPInteractionInteractable::Execute_OnInteractionCancelled(
		Target,
		Fixture.Interactor,
		FFixture::Open(),
		PEPInteractionTags::Reason_Interaction_Cancelled);
	TestEqual(TEXT("default notifications execute nothing"), Target->ExecuteCount, 0);

	const FPEPInteractionResult Result =
		Fixture.Interaction->BeginInteractWithInteractionTag(Target, FFixture::Open());
	TestTrue(TEXT("beginning an instant option succeeds"), Result.bSuccess);
	TestEqual(TEXT("an instant option executes at once"), Target->ExecuteCount, 1);
	TestFalse(TEXT("an instant option leaves nothing in progress"),
		Fixture.Interaction->IsInteractionInProgress());
	TestEqual(TEXT("an instant option never announces a start"), Fixture.Listener->StartedCount, 0);
	TestEqual(TEXT("an instant option reports its completion"), Fixture.Listener->CompletedCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInteractionInstantExecutionTest,
	"PEP.Interaction.Component.InstantExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInteractionInstantExecutionTest::RunTest(const FString& Parameters)
{
	using namespace PEPInteractionTest;
	FFixture Fixture;
	APEPInteractionTestTarget* Target = Fixture.SpawnTarget<APEPInteractionTestTarget>();

	const FPEPInteractionResult Result =
		Fixture.Interaction->ExecuteInteractWithInteractionTag(Target, FFixture::Open());
	TestTrue(TEXT("an offered option executes"), Result.bSuccess);
	TestEqual(TEXT("the target executed once"), Target->ExecuteCount, 1);
	TestEqual(TEXT("completion is reported once"), Fixture.Listener->CompletedCount, 1);
	TestTrue(TEXT("the reported result names the target"),
		Fixture.Listener->LastResult.bSuccess
			&& Fixture.Listener->LastResult.Target.Get() == Target
			&& Fixture.Listener->LastResult.InteractionTag == FFixture::Open());

	const FPEPInteractionResult Unsupported =
		Fixture.Interaction->ExecuteInteractWithInteractionTag(Target, PEPInteractionTags::Inspect);
	TestFalse(TEXT("an option the target does not offer is refused"), Unsupported.bSuccess);
	TestTrue(TEXT("the refusal says the option is unsupported"),
		Unsupported.ReasonTag == PEPCoreTags::Reason_UnsupportedMode.GetTag());
	TestEqual(TEXT("a refused option executes nothing"), Target->ExecuteCount, 1);
	TestEqual(TEXT("the refusal is reported as the request's outcome"),
		Fixture.Listener->CompletedCount, 2);
	TestFalse(TEXT("the reported outcome is the failure"), Fixture.Listener->LastResult.bSuccess);

	/* The direct executor is for code that owns its own timing: it ignores the duration. */
	APEPInteractionTestTimedTarget* TimedTarget =
		Fixture.SpawnTarget<APEPInteractionTestTimedTarget>(FVector(0.0f, 100.0f, 0.0f));
	TestTrue(TEXT("a timed option executes immediately through the direct executor"),
		Fixture.Interaction->ExecuteInteractWithInteractionTag(TimedTarget, FFixture::Open()).bSuccess);
	TestEqual(TEXT("the timed target executed"), TimedTarget->ExecuteCount, 1);
	TestFalse(TEXT("nothing is left in progress"), Fixture.Interaction->IsInteractionInProgress());
	TestEqual(TEXT("no timed interaction was started"), TimedTarget->StartedCount, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInteractionTimedCompletionTest,
	"PEP.Interaction.Component.TimedCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInteractionTimedCompletionTest::RunTest(const FString& Parameters)
{
	using namespace PEPInteractionTest;
	FFixture Fixture;
	APEPInteractionTestTimedTarget* Target = Fixture.SpawnTarget<APEPInteractionTestTimedTarget>();
	Target->Duration = 2.0f;

	const FPEPInteractionResult Result =
		Fixture.Interaction->BeginInteractWithInteractionTag(Target, FFixture::Open());
	TestTrue(TEXT("a timed option starts"), Result.bSuccess);
	TestTrue(TEXT("the interaction is in progress"), Fixture.Interaction->IsInteractionInProgress());
	TestTrue(TEXT("the active target is the requested one"),
		Fixture.Interaction->GetActiveInteractionTarget() == Target);
	TestTrue(TEXT("the active option is the requested one"),
		Fixture.Interaction->GetActiveInteractionTag() == FFixture::Open());
	TestEqual(TEXT("the duration comes from the target"),
		Fixture.Interaction->GetActiveInteractionDuration(), 2.0f);
	TestEqual(TEXT("the target is told the interaction started"), Target->StartedCount, 1);
	TestEqual(TEXT("listeners are told the interaction started"), Fixture.Listener->StartedCount, 1);
	TestEqual(TEXT("starting executes nothing"), Target->ExecuteCount, 0);
	TestEqual(TEXT("starting completes nothing"), Fixture.Listener->CompletedCount, 0);

	const double StartTime = Fixture.Interaction->GetActiveInteractionStartTimeForTest();
	TestEqual(TEXT("progress is half way after half the duration"),
		Fixture.Interaction->GetInteractionProgressAtTimeForTest(StartTime + 1.0), 0.5f);

	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 1.0);
	TestTrue(TEXT("still in progress before the duration elapses"),
		Fixture.Interaction->IsInteractionInProgress());
	TestEqual(TEXT("nothing executed before the duration elapses"), Target->ExecuteCount, 0);

	TestTrue(TEXT("requesting the interaction in progress again succeeds"),
		Fixture.Interaction->BeginInteractWithInteractionTag(Target, FFixture::Open()).bSuccess);
	TestEqual(TEXT("and does not restart it"), Target->StartedCount, 1);
	TestTrue(TEXT("nor move its start time"),
		Fixture.Interaction->GetActiveInteractionStartTimeForTest() == StartTime);

	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 2.0);
	TestFalse(TEXT("the interaction ends once the duration elapses"),
		Fixture.Interaction->IsInteractionInProgress());
	TestEqual(TEXT("the target executed once"), Target->ExecuteCount, 1);
	TestEqual(TEXT("completion is reported once"), Fixture.Listener->CompletedCount, 1);
	TestTrue(TEXT("the reported completion succeeded on the target"),
		Fixture.Listener->LastResult.bSuccess && Fixture.Listener->LastResult.Target.Get() == Target);
	TestEqual(TEXT("a completed interaction is not cancelled"), Fixture.Listener->CancelledCount, 0);
	TestEqual(TEXT("progress reads 0 once nothing is in progress"),
		Fixture.Interaction->GetInteractionProgressAtTimeForTest(StartTime + 3.0), 0.0f);

	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 4.0);
	TestEqual(TEXT("further ticks execute nothing"), Target->ExecuteCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInteractionCancelTest,
	"PEP.Interaction.Component.CancelInteraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInteractionCancelTest::RunTest(const FString& Parameters)
{
	using namespace PEPInteractionTest;
	FFixture Fixture;
	APEPInteractionTestTimedTarget* Target = Fixture.SpawnTarget<APEPInteractionTestTimedTarget>();

	TestFalse(TEXT("cancelling with nothing in progress does nothing"),
		Fixture.Interaction->CancelInteraction(PEPInteractionTags::Reason_Interaction_Cancelled));
	TestTrue(TEXT("a local cancel request on the authority with nothing in progress fails"),
		Fixture.Interaction->RequestCancelInteraction() == EPEPRequestResult::Failed);

	TestTrue(TEXT("a timed option starts"),
		Fixture.Interaction->BeginInteractWithInteractionTag(Target, FFixture::Open()).bSuccess);
	const double StartTime = Fixture.Interaction->GetActiveInteractionStartTimeForTest();

	TestTrue(TEXT("the interaction in progress is cancelled"),
		Fixture.Interaction->CancelInteraction(PEPInteractionTags::Reason_Interaction_Cancelled));
	TestFalse(TEXT("nothing is in progress afterwards"), Fixture.Interaction->IsInteractionInProgress());
	TestTrue(TEXT("no active target remains"), Fixture.Interaction->GetActiveInteractionTarget() == nullptr);
	TestEqual(TEXT("the target is told once"), Target->CancelledCount, 1);
	TestTrue(TEXT("the target receives the reason"),
		Target->LastCancelReason == PEPInteractionTags::Reason_Interaction_Cancelled.GetTag());
	TestEqual(TEXT("listeners are told once"), Fixture.Listener->CancelledCount, 1);
	TestTrue(TEXT("listeners receive the reason"),
		Fixture.Listener->LastCancelReason == PEPInteractionTags::Reason_Interaction_Cancelled.GetTag());
	TestEqual(TEXT("a cancelled interaction does not complete"), Fixture.Listener->CompletedCount, 0);

	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 10.0);
	TestEqual(TEXT("a cancelled interaction never executes"), Target->ExecuteCount, 0);
	TestFalse(TEXT("a second cancel finds nothing"),
		Fixture.Interaction->CancelInteraction(PEPInteractionTags::Reason_Interaction_Cancelled));

	TestTrue(TEXT("the option can start again"),
		Fixture.Interaction->BeginInteractWithInteractionTag(Target, FFixture::Open()).bSuccess);
	TestTrue(TEXT("a local cancel request on the authority cancels directly"),
		Fixture.Interaction->RequestCancelInteraction() == EPEPRequestResult::Executed);
	TestTrue(TEXT("an input cancel reports Reason.Interaction.Cancelled"),
		Target->LastCancelReason == PEPInteractionTags::Reason_Interaction_Cancelled.GetTag());

	TestTrue(TEXT("the option starts a third time"),
		Fixture.Interaction->BeginInteractWithInteractionTag(Target, FFixture::Open()).bSuccess);
	TestTrue(TEXT("cancelling without a reason still cancels"),
		Fixture.Interaction->CancelInteraction(FGameplayTag()));
	TestTrue(TEXT("a missing reason falls back to Reason.Interaction.Cancelled"),
		Target->LastCancelReason == PEPInteractionTags::Reason_Interaction_Cancelled.GetTag());
	TestEqual(TEXT("every start ended in exactly one cancel"), Target->CancelledCount, 3);
	TestEqual(TEXT("and none executed"), Target->ExecuteCount, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInteractionCancelOnRevalidationTest,
	"PEP.Interaction.Component.CancelOnFailedRevalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInteractionCancelOnRevalidationTest::RunTest(const FString& Parameters)
{
	using namespace PEPInteractionTest;
	FFixture Fixture;
	APEPInteractionTestTimedTarget* Target = Fixture.SpawnTarget<APEPInteractionTestTimedTarget>();

	TestTrue(TEXT("a timed option starts within range"),
		Fixture.Interaction->BeginInteractWithInteractionTag(Target, FFixture::Open()).bSuccess);
	const double StartTime = Fixture.Interaction->GetActiveInteractionStartTimeForTest();

	Target->SetActorLocation(FVector(2000.0f, 0.0f, 0.0f));
	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 0.5);
	TestFalse(TEXT("leaving the range cancels the interaction"),
		Fixture.Interaction->IsInteractionInProgress());
	TestTrue(TEXT("the target is told it went out of range"),
		Target->LastCancelReason == PEPCoreTags::Reason_OutOfRange.GetTag());
	TestTrue(TEXT("listeners are told it went out of range"),
		Fixture.Listener->LastCancelReason == PEPCoreTags::Reason_OutOfRange.GetTag());
	TestEqual(TEXT("an interrupted interaction never executes"), Target->ExecuteCount, 0);

	Target->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	TestTrue(TEXT("back in range, the option starts again"),
		Fixture.Interaction->BeginInteractWithInteractionTag(Target, FFixture::Open()).bSuccess);
	const double SecondStartTime = Fixture.Interaction->GetActiveInteractionStartTimeForTest();

	Target->bAllowInteraction = false;
	Fixture.Interaction->TickActiveInteractionForTest(SecondStartTime + 0.5);
	TestTrue(TEXT("a refusal is first tolerated for the grace period"),
		Fixture.Interaction->IsInteractionInProgress());
	Fixture.Interaction->TickActiveInteractionForTest(SecondStartTime + 0.8);
	TestFalse(TEXT("the target still refusing after the grace period cancels the interaction"),
		Fixture.Interaction->IsInteractionInProgress());
	TestTrue(TEXT("the cancel carries the target's own reason"),
		Target->LastCancelReason == PEPCoreTags::Reason_BlockedByState.GetTag());

	Target->bAllowInteraction = true;
	Target->Options.Reset();
	TestFalse(TEXT("an option no longer offered cannot start"),
		Fixture.Interaction->BeginInteractWithInteractionTag(Target, FFixture::Open()).bSuccess);
	TestTrue(TEXT("its rejection is reported as the request's outcome"),
		Fixture.Listener->CompletedCount == 1 && !Fixture.Listener->LastResult.bSuccess);
	TestEqual(TEXT("each started interaction was cancelled once"), Target->CancelledCount, 2);
	TestEqual(TEXT("and none executed"), Target->ExecuteCount, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInteractionRevalidationGraceTest,
	"PEP.Interaction.Component.RevalidationGrace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInteractionRevalidationGraceTest::RunTest(const FString& Parameters)
{
	using namespace PEPInteractionTest;
	FFixture Fixture;
	Fixture.Interaction->SetValidationTimingForTest(0.1f, 0.25f);
	APEPInteractionTestTimedTarget* Target = Fixture.SpawnTarget<APEPInteractionTestTimedTarget>();
	Target->Duration = 2.0f;

	TestTrue(TEXT("a timed option starts"),
		Fixture.Interaction->BeginInteractWithInteractionTag(Target, FFixture::Open()).bSuccess);
	const double StartTime = Fixture.Interaction->GetActiveInteractionStartTimeForTest();
	TestEqual(TEXT("the request is validated once"), Target->CanInteractCount, 1);

	Target->bAllowInteraction = false;
	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 0.05);
	TestEqual(TEXT("between two checks only range is validated"), Target->CanInteractCount, 1);

	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 0.15);
	TestEqual(TEXT("the periodic check asks the target"), Target->CanInteractCount, 2);
	TestTrue(TEXT("a first refusal does not cancel"), Fixture.Interaction->IsInteractionInProgress());

	Target->bAllowInteraction = true;
	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 0.30);
	TestEqual(TEXT("the next check asks again"), Target->CanInteractCount, 3);
	TestTrue(TEXT("a recovered interaction keeps running"), Fixture.Interaction->IsInteractionInProgress());

	Target->bAllowInteraction = false;
	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 0.45);
	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 0.50);
	TestEqual(TEXT("checks keep their rate while failing"), Target->CanInteractCount, 4);
	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 0.60);
	TestTrue(TEXT("failures are counted from the first one after a success"),
		Fixture.Interaction->IsInteractionInProgress());

	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 0.75);
	TestFalse(TEXT("failures lasting the grace period cancel"),
		Fixture.Interaction->IsInteractionInProgress());
	TestTrue(TEXT("with the target's reason"),
		Target->LastCancelReason == PEPCoreTags::Reason_BlockedByState.GetTag());
	TestEqual(TEXT("the target is told once"), Target->CancelledCount, 1);
	TestEqual(TEXT("nothing executed"), Target->ExecuteCount, 0);
	TestEqual(TEXT("a cancelled interaction does not complete"), Fixture.Listener->CompletedCount, 0);

	/* Range is not graced: it is checked every tick and cancels at once. */
	Target->bAllowInteraction = true;
	TestTrue(TEXT("the option starts again"),
		Fixture.Interaction->BeginInteractWithInteractionTag(Target, FFixture::Open()).bSuccess);
	Target->SetActorLocation(FVector(2000.0f, 0.0f, 0.0f));
	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 0.01);
	TestFalse(TEXT("leaving the range cancels on the next tick"),
		Fixture.Interaction->IsInteractionInProgress());
	TestTrue(TEXT("as out of range"),
		Target->LastCancelReason == PEPCoreTags::Reason_OutOfRange.GetTag());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInteractionCompletionValidationTest,
	"PEP.Interaction.Component.CompletionValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInteractionCompletionValidationTest::RunTest(const FString& Parameters)
{
	using namespace PEPInteractionTest;
	FFixture Fixture;
	Fixture.Interaction->SetValidationTimingForTest(0.1f, 0.25f);
	APEPInteractionTestTimedTarget* Target = Fixture.SpawnTarget<APEPInteractionTestTimedTarget>();
	Target->Duration = 1.0f;

	TestTrue(TEXT("a timed option starts"),
		Fixture.Interaction->BeginInteractWithInteractionTag(Target, FFixture::Open()).bSuccess);
	const double StartTime = Fixture.Interaction->GetActiveInteractionStartTimeForTest();
	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 0.5);

	const int32 ChecksBeforeCompletion = Target->CanInteractCount;
	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 1.0);
	TestEqual(TEXT("the completion tick validates exactly once"),
		Target->CanInteractCount - ChecksBeforeCompletion, 1);
	TestEqual(TEXT("and executes"), Target->ExecuteCount, 1);
	TestEqual(TEXT("completion is reported once"), Fixture.Listener->CompletedCount, 1);

	/* A brief refusal at the end delays completion rather than executing or cancelling. */
	TestTrue(TEXT("the option starts again"),
		Fixture.Interaction->BeginInteractWithInteractionTag(Target, FFixture::Open()).bSuccess);
	Target->bAllowInteraction = false;
	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 1.05);
	TestEqual(TEXT("a failing interaction never executes"), Target->ExecuteCount, 1);
	TestTrue(TEXT("but keeps running through the grace period"),
		Fixture.Interaction->IsInteractionInProgress());
	Target->bAllowInteraction = true;
	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 1.10);
	TestEqual(TEXT("it executes as soon as a check passes"), Target->ExecuteCount, 2);
	TestFalse(TEXT("and ends"), Fixture.Interaction->IsInteractionInProgress());

	/* A refusal lasting the grace period at the end cancels: never Completed for a timed start. */
	TestTrue(TEXT("the option starts a third time"),
		Fixture.Interaction->BeginInteractWithInteractionTag(Target, FFixture::Open()).bSuccess);
	Target->bAllowInteraction = false;
	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 1.05);
	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 1.20);
	TestTrue(TEXT("still waiting within the grace period"),
		Fixture.Interaction->IsInteractionInProgress());
	Fixture.Interaction->TickActiveInteractionForTest(StartTime + 1.35);
	TestFalse(TEXT("cancelled once the grace period is over"),
		Fixture.Interaction->IsInteractionInProgress());
	TestEqual(TEXT("the target is told it was cancelled"), Target->CancelledCount, 1);
	TestEqual(TEXT("without executing"), Target->ExecuteCount, 2);
	TestEqual(TEXT("and without a completion"), Fixture.Listener->CompletedCount, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInteractionIdTest,
	"PEP.Interaction.Component.InteractionIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInteractionIdTest::RunTest(const FString& Parameters)
{
	using namespace PEPInteractionTest;
	FFixture Fixture;
	APEPInteractionTestTarget* Target = Fixture.SpawnTarget<APEPInteractionTestTarget>();
	APEPInteractionTestTimedTarget* TimedTarget =
		Fixture.SpawnTarget<APEPInteractionTestTimedTarget>(FVector(0.0f, 100.0f, 0.0f));

	const int32 Reserved = Fixture.Interaction->ReserveInteractionId();
	TestTrue(TEXT("the authority reserves positive ids"), Reserved > 0);

	const FPEPInteractionResult Result =
		Fixture.Interaction->BeginInteractWithInteractionTag(Target, FFixture::Open(), Reserved);
	TestEqual(TEXT("the outcome carries the reserved id"), Result.InteractionId, Reserved);
	TestEqual(TEXT("so does the completion event"), Fixture.Listener->LastResult.InteractionId, Reserved);

	const FPEPInteractionResult Unidentified =
		Fixture.Interaction->BeginInteractWithInteractionTag(Target, FFixture::Open());
	TestTrue(TEXT("a request without id gets a fresh one"), Unidentified.InteractionId > Reserved);

	const FPEPInteractionResult Reused =
		Fixture.Interaction->BeginInteractWithInteractionTag(Target, FFixture::Open(), Reserved);
	TestTrue(TEXT("an id that already ended is never reused"),
		Reused.InteractionId != Reserved && Reused.InteractionId > 0);

	const FPEPInteractionResult FromClient =
		Fixture.Interaction->BeginInteractWithInteractionTag(Target, FFixture::Open(), -7);
	TestEqual(TEXT("an owning-client id is echoed back"), FromClient.InteractionId, -7);

	const FPEPInteractionResult Started =
		Fixture.Interaction->BeginInteractWithInteractionTag(TimedTarget, FFixture::Open(), -8);
	TestEqual(TEXT("a timed interaction runs under the request's id"),
		Fixture.Interaction->GetActiveInteractionId(), -8);
	TestEqual(TEXT("which the start result carries"), Started.InteractionId, -8);

	const FPEPInteractionResult Joined =
		Fixture.Interaction->BeginInteractWithInteractionTag(TimedTarget, FFixture::Open(), -9);
	TestEqual(TEXT("a repeat joins the interaction in progress and says so"), Joined.InteractionId, -8);

	TestTrue(TEXT("the interaction is cancelled"),
		Fixture.Interaction->CancelInteraction(PEPInteractionTags::Reason_Interaction_Cancelled));
	TestEqual(TEXT("the cancel event carries its id"), Fixture.Listener->LastCancelledInteractionId, -8);
	TestEqual(TEXT("nothing is active any more"), Fixture.Interaction->GetActiveInteractionId(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInteractionClientRequestRateLimitTest,
	"PEP.Interaction.Component.ClientRequestRateLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInteractionClientRequestRateLimitTest::RunTest(const FString& Parameters)
{
	using namespace PEPInteractionTest;
	FFixture Fixture;
	APEPInteractionTestTarget* Target = Fixture.SpawnTarget<APEPInteractionTestTarget>();
	APEPInteractionTestTimedTarget* TimedTarget =
		Fixture.SpawnTarget<APEPInteractionTestTimedTarget>(FVector(0.0f, 100.0f, 0.0f));

	/* The test world is never ticked: its real time stands still, so every request is "at once". */
	Fixture.Interaction->ReceiveClientRequestForTest(Target, FFixture::Open(), -1);
	TestEqual(TEXT("a first client request executes"), Target->ExecuteCount, 1);
	TestEqual(TEXT("under the client's id"), Fixture.Listener->LastResult.InteractionId, -1);

	Fixture.Interaction->ReceiveClientRequestForTest(Target, FFixture::Open(), -2);
	TestEqual(TEXT("a request right after it executes nothing"), Target->ExecuteCount, 1);
	TestEqual(TEXT("but still gets its outcome"), Fixture.Listener->CompletedCount, 2);
	TestTrue(TEXT("refused as rate limited"),
		!Fixture.Listener->LastResult.bSuccess
		&& Fixture.Listener->LastResult.ReasonTag == PEPInteractionTags::Reason_Interaction_RateLimited.GetTag());
	TestEqual(TEXT("under the client's id"), Fixture.Listener->LastResult.InteractionId, -2);

	TestTrue(TEXT("a timed interaction starts on the authority"),
		Fixture.Interaction->BeginInteractWithInteractionTag(TimedTarget, FFixture::Open()).bSuccess);
	Fixture.Interaction->ReceiveClientRequestForTest(TimedTarget, FFixture::Open(), -3);
	TestTrue(TEXT("repeating the interaction in progress is never limited"),
		Fixture.Interaction->IsInteractionInProgress());
	TestEqual(TEXT("and does not restart it"), TimedTarget->StartedCount, 1);
	TestEqual(TEXT("nor report anything"), Fixture.Listener->CompletedCount, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInteractionLocalSelectionGatingTest,
	"PEP.Interaction.Component.LocalSelectionGating",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInteractionLocalSelectionGatingTest::RunTest(const FString& Parameters)
{
	using namespace PEPInteractionTest;
	FFixture Fixture;

	TestFalse(TEXT("an interactor no local player controls does not select"),
		Fixture.Interaction->ShouldPerformLocalSelectionForTest());
	TestFalse(TEXT("so the component does not tick"),
		Fixture.Interaction->IsComponentTickEnabled());

	Fixture.Interaction->ResetTargetSelection();
	TestEqual(TEXT("a machine that does not select broadcasts no selection"),
		Fixture.Listener->SelectionChangedCount, 0);

	/* A timed interaction on the authority needs the tick even without local selection. */
	APEPInteractionTestTimedTarget* Target = Fixture.SpawnTarget<APEPInteractionTestTimedTarget>();
	TestTrue(TEXT("a timed option starts"),
		Fixture.Interaction->BeginInteractWithInteractionTag(Target, FFixture::Open()).bSuccess);
	TestTrue(TEXT("the authority ticks while it runs"), Fixture.Interaction->IsComponentTickEnabled());
	Fixture.Interaction->CancelInteraction(PEPInteractionTags::Reason_Interaction_Cancelled);
	TestFalse(TEXT("and stops once it ends"), Fixture.Interaction->IsComponentTickEnabled());

	/* A local player controller in a standalone world. */
	APlayerController* Player = Fixture.World->SpawnActor<APlayerController>();
	Fixture.Interactor->SetOwner(Player);
	Fixture.Interaction->RefreshTickEnabled();
	TestTrue(TEXT("an interactor owned by a local player controller selects"),
		Fixture.Interaction->ShouldPerformLocalSelectionForTest());
	TestTrue(TEXT("and ticks"), Fixture.Interaction->IsComponentTickEnabled());
	Fixture.Interaction->ResetTargetSelection();
	TestEqual(TEXT("the local player's machine does broadcast"), Fixture.Listener->SelectionChangedCount, 1);

	Fixture.Interactor->SetOwner(nullptr);
	Fixture.Interaction->RefreshTickEnabled();
	TestFalse(TEXT("losing that owner stops the selection"), Fixture.Interaction->IsComponentTickEnabled());

	APawn* Pawn = Fixture.World->SpawnActor<APawn>();
	UPEPInteractionTestComponent* PawnInteraction = NewObject<UPEPInteractionTestComponent>(Pawn);
	PawnInteraction->RegisterComponent();
	PawnInteraction->Init(Pawn);
	TestFalse(TEXT("a pawn nobody controls does not select"),
		PawnInteraction->ShouldPerformLocalSelectionForTest());
	TestFalse(TEXT("nor tick"), PawnInteraction->IsComponentTickEnabled());

	Pawn->PossessedBy(Player);
	TestTrue(TEXT("a pawn controlled by a local player selects"),
		PawnInteraction->ShouldPerformLocalSelectionForTest());
	TestTrue(TEXT("its possession turns the tick on"), PawnInteraction->IsComponentTickEnabled());

	Pawn->UnPossessed();
	TestFalse(TEXT("an unpossessed pawn stops selecting"),
		PawnInteraction->ShouldPerformLocalSelectionForTest());
	TestFalse(TEXT("its unpossession turns the tick off"), PawnInteraction->IsComponentTickEnabled());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInteractionBlueprintResultLibraryTest,
	"PEP.Interaction.Interactable.BlueprintResultLibrary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInteractionBlueprintResultLibraryTest::RunTest(const FString& Parameters)
{
	using namespace PEPInteractionTest;
	FFixture Fixture;
	APEPInteractionTestTarget* Target = Fixture.SpawnTarget<APEPInteractionTestTarget>();

	const FPEPInteractionResult Success =
		UPEPInteractionLibrary::MakeInteractionSuccess(Target, FFixture::Open());
	TestTrue(TEXT("a success succeeds"), Success.bSuccess);
	TestTrue(TEXT("on the given target and option"),
		Success.Target.Get() == Target && Success.InteractionTag == FFixture::Open());
	TestFalse(TEXT("without a reason"), Success.ReasonTag.IsValid());
	TestEqual(TEXT("a built result answers no request yet"), Success.InteractionId, 0);

	const FPEPInteractionResult Failure =
		UPEPInteractionLibrary::MakeInteractionFailure(
			PEPCoreTags::Reason_BlockedByState,
			Target,
			FFixture::Open());
	TestFalse(TEXT("a failure fails"), Failure.bSuccess);
	TestTrue(TEXT("with its reason"), Failure.ReasonTag == PEPCoreTags::Reason_BlockedByState.GetTag());
	TestTrue(TEXT("on the given target and option"),
		Failure.Target.Get() == Target && Failure.InteractionTag == FFixture::Open());

	/* A Blueprint interactable may fail without naming itself: the component names it. */
	Target->bReturnBareFailure = true;
	const int32 Reserved = Fixture.Interaction->ReserveInteractionId();
	const FPEPInteractionResult Executed =
		Fixture.Interaction->ExecuteInteractWithInteractionTag(Target, FFixture::Open(), Reserved);
	TestFalse(TEXT("the interactable's failure is reported"), Executed.bSuccess);
	TestTrue(TEXT("with its reason"), Executed.ReasonTag == PEPCoreTags::Reason_BlockedByState.GetTag());
	TestTrue(TEXT("completed with the request's target and option"),
		Executed.Target.Get() == Target && Executed.InteractionTag == FFixture::Open());
	TestEqual(TEXT("and the request's id"), Executed.InteractionId, Reserved);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInteractionBlueprintContractTest,
	"PEP.Interaction.Interactable.BlueprintContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInteractionBlueprintContractTest::RunTest(const FString& Parameters)
{
	const UClass* Interface = UPEPInteractionInteractable::StaticClass();
	const UFunction* Execute = Interface->FindFunctionByName(TEXT("ExecuteInteraction"));
	const UFunction* CanInteract = Interface->FindFunctionByName(TEXT("CanInteract"));
	TestTrue(TEXT("ExecuteInteraction can be implemented in Blueprint"),
		Execute && Execute->HasAnyFunctionFlags(FUNC_BlueprintEvent));
	TestTrue(TEXT("but not called from Blueprint, which would skip validation"),
		Execute && !Execute->HasAnyFunctionFlags(FUNC_BlueprintCallable));
	TestTrue(TEXT("CanInteract stays a callable query"),
		CanInteract && CanInteract->HasAnyFunctionFlags(FUNC_BlueprintCallable));

	const UClass* Component = UPEPInteractionComponent::StaticClass();
	const TCHAR* const AuthorityOnlyFunctions[] = {
		TEXT("ExecuteInteractWithInteractionTag"),
		TEXT("BeginInteractWithInteractionTag"),
		TEXT("CancelInteraction")};
	for (const TCHAR* AuthorityOnly : AuthorityOnlyFunctions)
	{
		const UFunction* Function = Component->FindFunctionByName(AuthorityOnly);
		TestTrue(
			FString::Printf(TEXT("%s is authority only in Blueprint"), AuthorityOnly),
			Function && Function->HasAnyFunctionFlags(FUNC_BlueprintAuthorityOnly));
	}

	const UFunction* AvailableTags = Component->FindFunctionByName(TEXT("GetAvailableInteractionTags"));
	TestTrue(TEXT("GetAvailableInteractionTags is pure"),
		AvailableTags && AvailableTags->HasAnyFunctionFlags(FUNC_BlueprintPure));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInteractionNewInteractionReplacesActiveTest,
	"PEP.Interaction.Component.NewInteractionReplacesActive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInteractionNewInteractionReplacesActiveTest::RunTest(const FString& Parameters)
{
	using namespace PEPInteractionTest;
	FFixture Fixture;
	APEPInteractionTestTimedTarget* First =
		Fixture.SpawnTarget<APEPInteractionTestTimedTarget>(FVector(100.0f, 0.0f, 0.0f));
	APEPInteractionTestTimedTarget* Second =
		Fixture.SpawnTarget<APEPInteractionTestTimedTarget>(FVector(0.0f, 100.0f, 0.0f));

	TestTrue(TEXT("the first interaction starts"),
		Fixture.Interaction->BeginInteractWithInteractionTag(First, FFixture::Open()).bSuccess);
	TestTrue(TEXT("the second interaction starts"),
		Fixture.Interaction->BeginInteractWithInteractionTag(Second, FFixture::Open()).bSuccess);

	TestEqual(TEXT("the first one was cancelled"), First->CancelledCount, 1);
	TestTrue(TEXT("as superseded by a new request"),
		First->LastCancelReason == PEPInteractionTags::Reason_Interaction_Cancelled.GetTag());
	TestTrue(TEXT("only the second one is in progress"),
		Fixture.Interaction->GetActiveInteractionTarget() == Second);
	TestEqual(TEXT("both starts were announced"), Fixture.Listener->StartedCount, 2);

	Fixture.Interaction->TickActiveInteractionForTest(
		Fixture.Interaction->GetActiveInteractionStartTimeForTest() + Second->Duration);
	TestEqual(TEXT("the second one completes"), Second->ExecuteCount, 1);
	TestEqual(TEXT("the first one never executes"), First->ExecuteCount, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPInteractionReplicationContractTest,
	"PEP.Interaction.Component.ActiveInteractionReplicatesWithNotify",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPInteractionReplicationContractTest::RunTest(const FString& Parameters)
{
	const FProperty* ActiveInteractionProperty =
		UPEPInteractionComponent::StaticClass()->FindPropertyByName(TEXT("ActiveInteraction"));
	const FProperty* SelectionProperty =
		UPEPInteractionComponent::StaticClass()->FindPropertyByName(TEXT("Target"));

	TestNotNull(TEXT("the active interaction property exists"), ActiveInteractionProperty);
	TestTrue(
		TEXT("the active interaction is replicated"),
		ActiveInteractionProperty && ActiveInteractionProperty->HasAnyPropertyFlags(CPF_Net));
	TestTrue(
		TEXT("the active interaction notifies the owning client"),
		ActiveInteractionProperty && ActiveInteractionProperty->HasAnyPropertyFlags(CPF_RepNotify));
	TestTrue(
		TEXT("the local selection stays unreplicated"),
		SelectionProperty && !SelectionProperty->HasAnyPropertyFlags(CPF_Net));

	return true;
}

#endif
