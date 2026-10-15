#include "Camera/PEPCameraModeStack.h"

#include "Camera/PEPCameraMode.h"

void UPEPCameraModeStack::PushCameraMode(UPEPCameraMode* Mode, const FPEPCameraModeContext& Context)
{
	if (!Mode || GetTopMode() == Mode)
	{
		return;
	}

	float StartWeight = 0.0f;
	const int32 ExistingIndex = Entries.IndexOfByPredicate(
		[Mode](const FPEPCameraModeStackEntry& Entry)
		{
			return Entry.Mode == Mode;
		});

	if (ExistingIndex != INDEX_NONE)
	{
		/*
		 * Coming back to a mode that is still being blended out. The current top contributes its
		 * alpha over everything below, so the returning mode must start at the complement of that
		 * alpha. Its own curve shapes weights into alphas, hence the inverse: the returning mode's
		 * alpha equals 1 - TopAlpha on the first frame whatever either blend function is. Exact with
		 * two modes in the stack; with more, removing the returning mode from the middle can still
		 * shift the result slightly.
		 */
		const FPEPCameraModeStackEntry& Top = Entries.Last();
		StartWeight = Mode->InverseBlendAlpha(1.0f - Top.Mode->EvaluateBlendAlpha(Top.Weight));
		Entries.RemoveAt(ExistingIndex);
	}
	else
	{
		Mode->NotifyActivated(Context);
	}

	if (Entries.IsEmpty() || Mode->GetBlendTime() <= 0.0f)
	{
		StartWeight = 1.0f;
	}

	FPEPCameraModeStackEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Mode = Mode;
	Entry.Weight = StartWeight;
}

FPEPCameraView UPEPCameraModeStack::EvaluateStack(const FPEPCameraModeContext& Context)
{
	FPEPCameraView Result;
	Result.Location = Context.PivotLocation;
	Result.Rotation = Context.ControlRotation;

	Entries.RemoveAll(
		[](const FPEPCameraModeStackEntry& Entry)
		{
			return !IsValid(Entry.Mode);
		});
	if (Entries.IsEmpty())
	{
		return Result;
	}

	for (FPEPCameraModeStackEntry& Entry : Entries)
	{
		const float BlendTime = Entry.Mode->GetBlendTime();
		Entry.Weight = BlendTime > 0.0f
			? FMath::Min(1.0f, Entry.Weight + Context.DeltaTime / BlendTime)
			: 1.0f;
	}

	/* Every mode updates, even one being blended out, so its lag and zoom keep moving. */
	Result = Entries[0].Mode->UpdateView(Context);
	for (int32 Index = 1; Index < Entries.Num(); ++Index)
	{
		const FPEPCameraModeStackEntry& Entry = Entries[Index];
		const FPEPCameraView View = Entry.Mode->UpdateView(Context);
		Result.Blend(View, Entry.Mode->EvaluateBlendAlpha(Entry.Weight));
	}

	/* A fully blended entry hides everything below it. */
	int32 FirstVisibleIndex = 0;
	for (int32 Index = Entries.Num() - 1; Index > 0; --Index)
	{
		if (Entries[Index].Weight >= 1.0f)
		{
			FirstVisibleIndex = Index;
			break;
		}
	}
	for (int32 Index = 0; Index < FirstVisibleIndex; ++Index)
	{
		Entries[Index].Mode->NotifyDeactivated();
	}
	if (FirstVisibleIndex > 0)
	{
		Entries.RemoveAt(0, FirstVisibleIndex);
	}

	return Result;
}

void UPEPCameraModeStack::Reset()
{
	for (const FPEPCameraModeStackEntry& Entry : Entries)
	{
		if (IsValid(Entry.Mode))
		{
			Entry.Mode->NotifyDeactivated();
		}
	}
	Entries.Reset();
}

UPEPCameraMode* UPEPCameraModeStack::GetTopMode() const
{
	return Entries.IsEmpty() ? nullptr : Entries.Last().Mode.Get();
}

bool UPEPCameraModeStack::ContainsMode(const UPEPCameraMode* Mode) const
{
	return Entries.ContainsByPredicate(
		[Mode](const FPEPCameraModeStackEntry& Entry)
		{
			return Entry.Mode == Mode;
		});
}
