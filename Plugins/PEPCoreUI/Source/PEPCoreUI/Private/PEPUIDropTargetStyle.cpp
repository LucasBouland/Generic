// Copyright Epic Games, Inc. All Rights Reserved.

#include "PEPUIDropTargetStyle.h"

FLinearColor FPEPUIDropTargetVisualStyle::GetColor(EPEPUIDropTargetVisualState State) const
{
	switch (State)
	{
	case EPEPUIDropTargetVisualState::Hovered:
		return Hovered;
	case EPEPUIDropTargetVisualState::Valid:
		return Valid;
	case EPEPUIDropTargetVisualState::Invalid:
		return Invalid;
	case EPEPUIDropTargetVisualState::Neutral:
	default:
		return Neutral;
	}
}
