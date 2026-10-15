// Copyright Epic Games, Inc. All Rights Reserved.

#include "PEPUIWorldSettings.h"

FLinearColor APEPUIWorldSettings::GetDropTargetVisualColor(EPEPUIDropTargetVisualState State) const
{
	return DropTargetVisualStyle.GetColor(State);
}
