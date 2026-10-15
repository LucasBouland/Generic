// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "PEPUIDropTargetStyle.h"
#include "PEPDropTargetWidget.generated.h"

class UBorder;

/** Visual-only drop target. The owning domain decides whether a drop is accepted. */
UCLASS(Abstract, Blueprintable, BlueprintType)
class PEPCOREUI_API UPEPDropTargetWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "PEP UI|Drop Target")
	void SetVisualState(EPEPUIDropTargetVisualState NewState);

	UFUNCTION(BlueprintPure, Category = "PEP UI|Drop Target")
	EPEPUIDropTargetVisualState GetVisualState() const;

	/** Reapplies the current world's colors, for example after a style is changed at runtime. */
	UFUNCTION(BlueprintCallable, Category = "PEP UI|Drop Target")
	void RefreshVisualStyle();

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBorder> DropTargetBorder;

private:
	UPROPERTY(Transient)
	EPEPUIDropTargetVisualState VisualState = EPEPUIDropTargetVisualState::Neutral;

	void ApplyVisualState();
};
