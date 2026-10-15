// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/WorldSettings.h"
#include "PEPUIDropTargetStyle.h"
#include "PEPUIWorldSettings.generated.h"

/** Optional WorldSettings base for projects that want per-world PEP UI styling. */
UCLASS(Blueprintable, BlueprintType)
class PEPCOREUI_API APEPUIWorldSettings : public AWorldSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PEP UI|Drop Target")
	FPEPUIDropTargetVisualStyle DropTargetVisualStyle;

	UFUNCTION(BlueprintPure, Category = "PEP UI|Drop Target")
	FLinearColor GetDropTargetVisualColor(EPEPUIDropTargetVisualState State) const;
};
