// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PEPUIDropTargetStyle.generated.h"

UENUM(BlueprintType)
enum class EPEPUIDropTargetVisualState : uint8
{
	Neutral UMETA(DisplayName = "Neutral"),
	Hovered UMETA(DisplayName = "Hovered"),
	Valid UMETA(DisplayName = "Valid Drop"),
	Invalid UMETA(DisplayName = "Invalid Drop")
};

/** World-configurable colors used by the shared, purely visual drop target widget. */
USTRUCT(BlueprintType)
struct PEPCOREUI_API FPEPUIDropTargetVisualStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PEP UI|Drop Target")
	FLinearColor Neutral = FLinearColor(0.08f, 0.08f, 0.08f, 0.24f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PEP UI|Drop Target")
	FLinearColor Hovered = FLinearColor(0.14f, 0.34f, 0.52f, 0.45f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PEP UI|Drop Target")
	FLinearColor Valid = FLinearColor(0.08f, 0.42f, 0.18f, 0.55f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PEP UI|Drop Target")
	FLinearColor Invalid = FLinearColor(0.52f, 0.08f, 0.08f, 0.55f);

	FLinearColor GetColor(EPEPUIDropTargetVisualState State) const;
};
