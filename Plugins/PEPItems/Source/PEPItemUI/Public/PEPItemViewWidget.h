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

	/** The presentation snapshot currently shown. Changed only by SetItemInstance and ClearItem. */
	UFUNCTION(BlueprintPure, Category="PEP|Item|UI")
	const FPEPItemViewData& GetViewData() const
	{
		return ViewData;
	}

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

	/**
	 * Pushes ViewData into the bound widgets and starts the icon load. Override in C++ to drive
	 * extra native widgets; call Super to keep the defaults.
	 */
	virtual void RefreshView();

	/**
	 * Raised after SetItemInstance or ClearItem changed the snapshot and the native widgets were
	 * refreshed. Blueprint children add their own presentation here instead of polling ViewData.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category="PEP|Item|UI")
	void OnItemViewDataChanged(const FPEPItemViewData& NewViewData);

	/** Readable by Blueprint children; everyone else goes through GetViewData. */
	UPROPERTY(BlueprintReadOnly, Transient, Category="PEP|Item|UI")
	FPEPItemViewData ViewData;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UImage> ItemIcon;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> ItemNameText;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> ItemStackText;

private:
	void HandleViewDataChanged();
	void RequestItemIcon();
	uint64 IconRequestGeneration = 0;
	TSharedPtr<struct FStreamableHandle> IconLoadHandle;
};
