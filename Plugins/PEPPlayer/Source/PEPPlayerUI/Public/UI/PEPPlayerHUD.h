#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "UI/PEPPlayerHUDContract.h"
#include "PEPPlayerHUD.generated.h"

class UWidgetSwitcher;

/** Presentation owner. Input policy remains in the owning PEP controller. */
UCLASS(Abstract, Blueprintable)
class PEPPLAYERUI_API UPEPPlayerHUD : public UCommonUserWidget, public IPEPPlayerHUDContract
{
	GENERATED_BODY()

public:
	virtual bool CanShowMenu() const override;
	virtual UUserWidget* GetMenuWidget() const override;
	virtual UWidget* ShowMenu(bool bOpen) override;
	virtual void NotifyControlledPawnChanged(APawn* PreviousPawn, APawn* NewPawn) override;

	/** Detach old Pawn delegates before attaching the new Pawn's data. */
	UFUNCTION(BlueprintImplementableEvent, Category="PEP|UI")
	void OnControlledPawnChanged(APawn* PreviousPawn, APawn* NewPawn);

protected:
	/** Routes menu shortcuts before focused children, unless the event's Slate user is typing in an editable text. */
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	/** Keys a focused editable text leaves unhandled: non-typing keys route, typing keys mapped to shortcuts are swallowed. */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	/** True when the event's Slate user focuses an SEditableText or SMultiLineEditableText (text, search and spin boxes). */
	virtual bool IsTextEntryFocused(const FKeyEvent& InKeyEvent) const;

	UPROPERTY(BlueprintReadOnly, meta=(BindWidget), Category="PEP|UI")
	TObjectPtr<UWidgetSwitcher> HUDSwitcher;

	/** Required direct child of HUDSwitcher; its position does not matter. */
	UPROPERTY(BlueprintReadOnly, meta=(BindWidget), Category="PEP|UI")
	TObjectPtr<UUserWidget> GameplayScreen;

	/** Required direct child of HUDSwitcher; its position does not matter. */
	UPROPERTY(BlueprintReadOnly, meta=(BindWidget), Category="PEP|UI")
	TObjectPtr<UUserWidget> PlayerMenuScreen;
};
