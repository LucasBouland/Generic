#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "PEPItemUILibrary.h"
#include "PEPItemViewWidget.generated.h"

class UImage;
class UTextBlock;

/** CommonUI base for the reusable, read-only item presentation widget. */
UCLASS(Abstract, Blueprintable)
class PEPITEMUI_API UPEPItemViewWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="PEP|Item|UI")
	void SetItemInstance(const FPEPItemInstance& Item);

	UFUNCTION(BlueprintCallable, Category="PEP|Item|UI")
	void ClearItem();

	UPROPERTY(BlueprintReadOnly, Transient, Category="PEP|Item|UI")
	FPEPItemViewData ViewData;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UImage> ItemIcon;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> ItemNameText;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> ItemStackText;

private:
	void RefreshView();
	void RequestItemIcon();
	uint64 IconRequestGeneration = 0;
	TSharedPtr<struct FStreamableHandle> IconLoadHandle;
};
