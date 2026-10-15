// Copyright Epic Games, Inc. All Rights Reserved.

#include "PEPDropTargetWidget.h"

#include "Components/Border.h"
#include "Engine/World.h"
#include "PEPUIWorldSettings.h"

void UPEPDropTargetWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ApplyVisualState();
}

void UPEPDropTargetWidget::SetVisualState(EPEPUIDropTargetVisualState NewState)
{
	if (VisualState == NewState)
	{
		return;
	}

	VisualState = NewState;
	ApplyVisualState();
}

EPEPUIDropTargetVisualState UPEPDropTargetWidget::GetVisualState() const
{
	return VisualState;
}

void UPEPDropTargetWidget::RefreshVisualStyle()
{
	ApplyVisualState();
}

void UPEPDropTargetWidget::ApplyVisualState()
{
	if (!DropTargetBorder)
	{
		return;
	}

	FLinearColor Color = FPEPUIDropTargetVisualStyle().GetColor(VisualState);
	if (const UWorld* World = GetWorld())
	{
		if (const APEPUIWorldSettings* UIWorldSettings = Cast<APEPUIWorldSettings>(World->GetWorldSettings()))
		{
			Color = UIWorldSettings->GetDropTargetVisualColor(VisualState);
		}
	}

	DropTargetBorder->SetBrushColor(Color);
}
