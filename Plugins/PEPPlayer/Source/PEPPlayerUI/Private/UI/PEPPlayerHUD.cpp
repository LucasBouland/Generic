#include "UI/PEPPlayerHUD.h"

#include "Actors/PEPPlayerController.h"
#include "Components/WidgetSwitcher.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"

namespace
{
	/** Printable characters would be typed into a focused text field; control characters (Escape, Tab) and keys without a character (gamepad) are not. */
	bool CanTypeText(const FKeyEvent& InKeyEvent)
	{
		const uint32 Character = InKeyEvent.GetCharacter();
		return Character >= 32 && Character != 127;
	}
}

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
	// A focused text field sees keys first; whatever it leaves unhandled comes back through NativeOnKeyDown.
	APEPPlayerController* PC = Cast<APEPPlayerController>(GetOwningPlayer());
	if (PC && PC->IsMenuOpen() && !IsTextEntryFocused(InKeyEvent)
		&& PC->RouteMenuShortcut(InKeyEvent.GetKey(), InKeyEvent.IsRepeat(), InKeyEvent.GetModifierKeys()))
	{
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

FReply UPEPPlayerHUD::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// Focused children declined this key; past this widget it bubbles to the viewport and Enhanced Input.
	APEPPlayerController* PC = Cast<APEPPlayerController>(GetOwningPlayer());
	if (PC && PC->IsMenuOpen())
	{
		const bool bTextEntryFocused = IsTextEntryFocused(InKeyEvent);
		// Non-typing keys a focused text field left unhandled (Escape, Tab, gamepad buttons) still trigger their shortcut.
		if (bTextEntryFocused && !CanTypeText(InKeyEvent)
			&& PC->RouteMenuShortcut(InKeyEvent.GetKey(), InKeyEvent.IsRepeat(), InKeyEvent.GetModifierKeys()))
		{
			return FReply::Handled();
		}
		// Shortcut keys typed into a text field (text widgets type on KeyChar and leave KeyDown unhandled) and shortcuts
		// suppressed by Ctrl/Alt/Cmd stop here, so Enhanced Input cannot fire the plain mapping through the viewport.
		UInputAction* Action = nullptr;
		if (PC->ResolveMenuShortcut(InKeyEvent.GetKey(), InKeyEvent.GetModifierKeys(), Action) != EPEPMenuShortcutMatch::None)
		{
			return FReply::Handled();
		}
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

bool UPEPPlayerHUD::IsTextEntryFocused(const FKeyEvent& InKeyEvent) const
{
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}
	const TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetUserFocusedWidget(InKeyEvent.GetUserIndex());
	if (!FocusedWidget.IsValid())
	{
		return false;
	}
	// Same type test CommonUI's analog cursor applies; text, search and spin boxes focus these inner widgets.
	static const FName EditableTextType(TEXT("SEditableText"));
	static const FName MultiLineEditableTextType(TEXT("SMultiLineEditableText"));
	const FName FocusedType = FocusedWidget->GetType();
	return FocusedType == EditableTextType || FocusedType == MultiLineEditableTextType;
}
