#include "UI/PEPPlayerHUD.h"

#include "Actors/PEPPlayerController.h"
#include "Components/WidgetSwitcher.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"

bool UPEPPlayerHUD::CanShowMenu() const
{
	return IsValid(HUDSwitcher)
		&& IsValid(GameplayScreen)
		&& IsValid(PlayerMenuScreen)
		&& GameplayScreen != PlayerMenuScreen
		&& GameplayScreen->GetParent() == HUDSwitcher.Get()
		&& PlayerMenuScreen->GetParent() == HUDSwitcher.Get();
}

UUserWidget* UPEPPlayerHUD::GetMenuWidget() const
{
	return PlayerMenuScreen.Get();
}

UWidget* UPEPPlayerHUD::ShowMenu(const bool bOpen)
{
	if (!CanShowMenu())
	{
		return nullptr;
	}
	UUserWidget* Screen = bOpen ? PlayerMenuScreen.Get() : GameplayScreen.Get();
	HUDSwitcher->SetActiveWidget(Screen);
	Screen->SetIsFocusable(true);
	return Screen;
}

void UPEPPlayerHUD::NotifyControlledPawnChanged(APawn* PreviousPawn, APawn* NewPawn)
{
	OnControlledPawnChanged(PreviousPawn, NewPawn);
}

FReply UPEPPlayerHUD::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	APEPPlayerController* PC = Cast<APEPPlayerController>(GetOwningPlayer());
	if (PC && PC->RouteMenuShortcut(InKeyEvent.GetKey(), InKeyEvent.IsRepeat()))
	{
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}
