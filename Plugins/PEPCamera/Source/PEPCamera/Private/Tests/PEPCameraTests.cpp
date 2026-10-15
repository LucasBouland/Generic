#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Camera/Modes/PEPCameraMode_FirstPerson.h"
#include "Camera/Modes/PEPCameraMode_Strategy.h"
#include "Camera/Modes/PEPCameraMode_ThirdPerson.h"
#include "Camera/PEPCameraComponent.h"
#include "Camera/PEPCameraModeStack.h"
#include "UObject/Package.h"

/*
 * These tests run without a world: modes then skip their traces (collision, terrain) and read the
 * view target from the context alone, which keeps the camera math deterministic.
 */
namespace PEPCameraTests
{
	FPEPCameraModeContext MakeContext(const float DeltaTime)
	{
		FPEPCameraModeContext Context;
		Context.DeltaTime = DeltaTime;
		return Context;
	}

	UPEPCameraMode_FirstPerson* MakeFixedMode(UObject* Outer, const float X, const float BlendTime)
	{
		UPEPCameraMode_FirstPerson* Mode = NewObject<UPEPCameraMode_FirstPerson>(Outer);
		Mode->ViewOffset = FVector(X, 0.0f, 0.0f);
		Mode->BlendTime = BlendTime;
		Mode->BlendFunction = EPEPCameraBlendFunction::Linear;
		return Mode;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPCameraStackBlendTest,
	"PEP.Camera.Stack.BlendsAndPrunes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPCameraStackBlendTest::RunTest(const FString& Parameters)
{
	UPEPCameraModeStack* Stack = NewObject<UPEPCameraModeStack>(GetTransientPackage());
	UPEPCameraMode_FirstPerson* A = PEPCameraTests::MakeFixedMode(Stack, 0.0f, 0.0f);
	UPEPCameraMode_FirstPerson* B = PEPCameraTests::MakeFixedMode(Stack, 100.0f, 1.0f);

	Stack->PushCameraMode(A, PEPCameraTests::MakeContext(0.0f));
	TestEqual(TEXT("the first mode is shown at once"),
		Stack->EvaluateStack(PEPCameraTests::MakeContext(0.5f)).Location.X, 0.0);

	Stack->PushCameraMode(B, PEPCameraTests::MakeContext(0.0f));
	TestEqual(TEXT("half the blend time gives half the blend"),
		Stack->EvaluateStack(PEPCameraTests::MakeContext(0.5f)).Location.X, 50.0, 0.01);
	TestTrue(TEXT("both modes are in the stack while blending"), Stack->IsBlending());

	TestEqual(TEXT("the full blend time reaches the new mode"),
		Stack->EvaluateStack(PEPCameraTests::MakeContext(0.5f)).Location.X, 100.0, 0.01);
	TestEqual(TEXT("the hidden mode is removed"), Stack->Num(), 1);
	TestTrue(TEXT("the new mode is on top"), Stack->GetTopMode() == B);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPCameraStackReturnTest,
	"PEP.Camera.Stack.ReturningMidBlendIsContinuous",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPCameraStackReturnTest::RunTest(const FString& Parameters)
{
	UPEPCameraModeStack* Stack = NewObject<UPEPCameraModeStack>(GetTransientPackage());
	UPEPCameraMode_FirstPerson* A = PEPCameraTests::MakeFixedMode(Stack, 0.0f, 1.0f);
	UPEPCameraMode_FirstPerson* B = PEPCameraTests::MakeFixedMode(Stack, 100.0f, 1.0f);

	Stack->PushCameraMode(A, PEPCameraTests::MakeContext(0.0f));
	Stack->EvaluateStack(PEPCameraTests::MakeContext(0.0f));
	Stack->PushCameraMode(B, PEPCameraTests::MakeContext(0.0f));
	const double MidBlend = Stack->EvaluateStack(PEPCameraTests::MakeContext(0.3f)).Location.X;
	TestEqual(TEXT("the blend is under way"), MidBlend, 30.0, 0.01);

	Stack->PushCameraMode(A, PEPCameraTests::MakeContext(0.0f));
	TestEqual(TEXT("switching back does not jump"),
		Stack->EvaluateStack(PEPCameraTests::MakeContext(0.0f)).Location.X, MidBlend, 0.01);
	TestEqual(TEXT("the returning mode then takes over"),
		Stack->EvaluateStack(PEPCameraTests::MakeContext(1.0f)).Location.X, 0.0, 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPCameraFirstPersonTest,
	"PEP.Camera.FirstPerson.LooksFromTheEyeAlongControlRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPCameraFirstPersonTest::RunTest(const FString& Parameters)
{
	UPEPCameraMode_FirstPerson* Mode = NewObject<UPEPCameraMode_FirstPerson>(GetTransientPackage());
	Mode->ViewOffset = FVector(10.0f, 0.0f, 0.0f);
	Mode->FieldOfView = 100.0f;

	FPEPCameraModeContext Context = PEPCameraTests::MakeContext(0.016f);
	Context.PivotLocation = FVector(0.0f, 0.0f, 150.0f);
	Context.ControlRotation = FRotator(-20.0f, 90.0f, 0.0f);

	const FPEPCameraView View = Mode->UpdateView(Context);
	TestTrue(TEXT("the offset follows the control yaw, not the pitch"),
		View.Location.Equals(FVector(0.0f, 10.0f, 150.0f), 0.01));
	TestTrue(TEXT("the camera looks along the control rotation"),
		View.Rotation.Equals(Context.ControlRotation, 0.01));
	TestEqual(TEXT("the mode's field of view is used"), View.FieldOfView, 100.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPCameraThirdPersonTest,
	"PEP.Camera.ThirdPerson.BoomShoulderAndZoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPCameraThirdPersonTest::RunTest(const FString& Parameters)
{
	UPEPCameraMode_ThirdPerson* Mode = NewObject<UPEPCameraMode_ThirdPerson>(GetTransientPackage());
	Mode->PivotOffset = FVector::ZeroVector;
	Mode->ArmLength = 300.0f;
	Mode->MinArmLength = 200.0f;
	Mode->MaxArmLength = 400.0f;
	Mode->ZoomStep = 50.0f;
	Mode->ZoomInterpSpeed = 0.0f;
	Mode->ShoulderOffset = FVector(0.0f, 50.0f, 0.0f);
	Mode->ShoulderInterpSpeed = 0.0f;
	Mode->bDoCollisionTest = false;

	const FPEPCameraModeContext Context = PEPCameraTests::MakeContext(0.016f);
	TestTrue(TEXT("the camera sits behind the pivot over the right shoulder"),
		Mode->UpdateView(Context).Location.Equals(FVector(-300.0f, 50.0f, 0.0f), 0.01));

	Mode->SwapShoulder();
	TestTrue(TEXT("swapping moves to the left shoulder"),
		Mode->UpdateView(Context).Location.Equals(FVector(-300.0f, -50.0f, 0.0f), 0.01));

	Mode->AddZoomInput(1.0f);
	TestTrue(TEXT("zooming in shortens the boom by one step"),
		Mode->UpdateView(Context).Location.Equals(FVector(-250.0f, -50.0f, 0.0f), 0.01));

	Mode->AddZoomInput(10.0f);
	TestEqual(TEXT("zoom is clamped to the minimum boom"), Mode->GetTargetArmLength(), 200.0f);
	Mode->AddZoomInput(-10.0f);
	TestEqual(TEXT("zoom is clamped to the maximum boom"), Mode->GetTargetArmLength(), 400.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPCameraStrategyTest,
	"PEP.Camera.Strategy.PanZoomAndPitch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPCameraStrategyTest::RunTest(const FString& Parameters)
{
	UPEPCameraMode_Strategy* Mode = NewObject<UPEPCameraMode_Strategy>(GetTransientPackage());
	Mode->MinDistance = 1000.0f;
	Mode->MaxDistance = 3000.0f;
	Mode->DefaultDistance = 2000.0f;
	Mode->PitchAtMinDistance = -30.0f;
	Mode->PitchAtMaxDistance = -60.0f;
	Mode->ZoomInterpSpeed = 0.0f;
	Mode->PanSpeed = 1000.0f;
	Mode->bScalePanWithDistance = false;

	FPEPCameraView View = Mode->UpdateView(PEPCameraTests::MakeContext(0.0f));
	TestEqual(TEXT("the default distance sits halfway between the pitch limits"), View.Rotation.Pitch, -45.0, 0.01);
	TestTrue(TEXT("the camera is Distance away from the focus"),
		FMath::IsNearlyEqual(FVector::Dist(View.Location, Mode->GetFocusLocation()), 2000.0, 0.1));

	Mode->AddPanInput(FVector2D(0.0f, 1.0f));
	Mode->UpdateView(PEPCameraTests::MakeContext(1.0f));
	TestTrue(TEXT("forward pan moves the focus along the camera yaw"),
		Mode->GetFocusLocation().Equals(FVector(1000.0f, 0.0f, 0.0f), 0.01));

	Mode->UpdateView(PEPCameraTests::MakeContext(1.0f));
	TestTrue(TEXT("pan input does not carry over to later frames"),
		Mode->GetFocusLocation().Equals(FVector(1000.0f, 0.0f, 0.0f), 0.01));

	Mode->SetYaw(90.0f);
	Mode->AddPanInput(FVector2D(0.0f, 1.0f));
	Mode->UpdateView(PEPCameraTests::MakeContext(1.0f));
	TestTrue(TEXT("pan follows the orbited yaw"),
		Mode->GetFocusLocation().Equals(FVector(1000.0f, 1000.0f, 0.0f), 0.01));

	Mode->AddZoomInput(100.0f);
	View = Mode->UpdateView(PEPCameraTests::MakeContext(0.0f));
	TestEqual(TEXT("zooming in is clamped to the minimum distance"), Mode->GetCurrentDistance(), 1000.0f);
	TestEqual(TEXT("zoomed in uses the near pitch"), View.Rotation.Pitch, -30.0, 0.01);

	Mode->FocusMode = EPEPStrategyFocusMode::FollowViewTarget;
	FPEPCameraModeContext Follow = PEPCameraTests::MakeContext(1.0f);
	Follow.PivotLocation = FVector(500.0f, 0.0f, 0.0f);
	Mode->AddPanInput(FVector2D(1.0f, 0.0f));
	Mode->UpdateView(Follow);
	TestTrue(TEXT("follow mode tracks the target and ignores pan"),
		Mode->GetFocusLocation().Equals(FVector(500.0f, 0.0f, 0.0f), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPEPCameraComponentCycleTest,
	"PEP.Camera.Component.CyclesModesAndRoutesInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPEPCameraComponentCycleTest::RunTest(const FString& Parameters)
{
	UPEPCameraComponent* Camera = NewObject<UPEPCameraComponent>(GetTransientPackage());
	Camera->DefaultCameraMode = UPEPCameraMode_FirstPerson::StaticClass();
	Camera->CycleCameraModes.Add(UPEPCameraMode_FirstPerson::StaticClass());
	Camera->CycleCameraModes.Add(UPEPCameraMode_ThirdPerson::StaticClass());

	FMinimalViewInfo View;
	Camera->GetCameraView(0.016f, View);
	TestTrue(TEXT("the default mode is used first"),
		Camera->GetCurrentCameraMode() && Camera->GetCurrentCameraMode()->IsA<UPEPCameraMode_FirstPerson>());

	Camera->CycleCameraMode(1);
	Camera->GetCameraView(0.016f, View);
	UPEPCameraMode_ThirdPerson* ThirdPerson = Cast<UPEPCameraMode_ThirdPerson>(Camera->GetCurrentCameraMode());
	TestNotNull(TEXT("cycling moves to third person"), ThirdPerson);
	TestTrue(TEXT("the switch blends rather than cuts"), Camera->IsBlendingCameraModes());

	if (ThirdPerson)
	{
		const float ArmBefore = ThirdPerson->GetTargetArmLength();
		Camera->AddZoomInput(1.0f);
		TestTrue(TEXT("zoom input reaches the current mode"), ThirdPerson->GetTargetArmLength() < ArmBefore);
		const bool bWasRight = ThirdPerson->IsRightShoulder();
		Camera->SwapShoulder();
		TestTrue(TEXT("shoulder swap reaches the third-person mode"), ThirdPerson->IsRightShoulder() != bWasRight);
	}

	Camera->CycleCameraMode(1);
	TestTrue(TEXT("cycling wraps around"),
		Camera->GetCameraModeClass() == UPEPCameraMode_FirstPerson::StaticClass());
	TestTrue(TEXT("instances are kept per mode class"),
		Camera->GetCameraModeInstance<UPEPCameraMode_ThirdPerson>() == ThirdPerson);
	return true;
}

#endif
