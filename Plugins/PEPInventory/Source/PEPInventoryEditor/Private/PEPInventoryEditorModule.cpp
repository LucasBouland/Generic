#include "PEPInventoryEditorModule.h"

#include "Engine/AssetManagerSettings.h"
#include "Editor.h"
#include "EditorUtilitySubsystem.h"
#include "EditorUtilityWidgetBlueprint.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Item/PEPItemPreset.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "MessageLogModule.h"
#include "Misc/CoreDelegates.h"
#include "PEPContainerSlotDefinitionCustomization.h"
#include "PEPItemFragmentContainerCustomization.h"
#include "PEPItemPresetChildCustomization.h"
#include "PEPItemPresetCustomization.h"
#include "PropertyEditorModule.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "PEPInventoryEditor"

namespace PEPInventoryEditorPrivate
{
	const FName AssetManagerLogName(TEXT("PEPInventory"));
	const TCHAR* ItemBrowserAssetPath = TEXT("/PEPInventory/Editor/UI/EUW_PEPItemBrowser.EUW_PEPItemBrowser");

	bool HasPresetScan(const UAssetManagerSettings& Settings)
	{
		return Settings.PrimaryAssetTypesToScan.ContainsByPredicate(
			[](const FPrimaryAssetTypeInfo& Entry)
			{
				return Entry.PrimaryAssetType == UPEPItemPreset::PresetAssetType
					&& Entry.GetAssetBaseClass().ToSoftObjectPath().GetAssetPath()
						== UPEPItemPreset::StaticClass()->GetClassPathName()
					&& (!Entry.GetDirectories().IsEmpty() || !Entry.GetSpecificAssets().IsEmpty());
			});
	}

	void AddPresetScan()
	{
		UAssetManagerSettings* Settings = GetMutableDefault<UAssetManagerSettings>();
		if (!Settings || HasPresetScan(*Settings))
		{
			return;
		}
		FPrimaryAssetTypeInfo* Existing = Settings->PrimaryAssetTypesToScan.FindByPredicate(
			[](const FPrimaryAssetTypeInfo& Entry)
			{
				return Entry.PrimaryAssetType == UPEPItemPreset::PresetAssetType;
			});
		if (Existing)
		{
			Existing->SetAssetBaseClass(TSoftClassPtr<UObject>(UPEPItemPreset::StaticClass()));
			if (Existing->GetDirectories().IsEmpty() && Existing->GetSpecificAssets().IsEmpty())
			{
				Existing->GetDirectories().AddDefaulted_GetRef().Path = TEXT("/Game");
			}
		}
		else
		{
			TArray<FDirectoryPath> Directories;
			Directories.AddDefaulted_GetRef().Path = TEXT("/Game");
			Settings->PrimaryAssetTypesToScan.Emplace(
				UPEPItemPreset::PresetAssetType.GetName(), UPEPItemPreset::StaticClass(),
				false, false, MoveTemp(Directories), TArray<FSoftObjectPath>());
		}

		/*
		 * Written to the settings' own default config file the way Project Settings writes it,
		 * checked out or made writable as needed. SaveConfig to a hand-built path wrote the
		 * section without regard to what the base ini files already provide.
		 */
		if (!Settings->TryUpdateDefaultConfigFile())
		{
			FMessageLog(AssetManagerLogName).Error(LOCTEXT(
				"PresetScanNotSaved",
				"Could not write the PEP ItemPreset scan to the project's DefaultGame.ini. Check that the file is writable or checked out."));
			return;
		}
		FMessageLog(AssetManagerLogName).Info(LOCTEXT(
			"PresetScanAdded",
			"Added the PEP ItemPreset scan to Config/DefaultGame.ini. Restart the editor for Asset Manager to apply it."));
	}
}

void FPEPInventoryEditorModule::StartupModule()
{
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
	if (!MessageLogModule.IsRegisteredLogListing(PEPInventoryEditorPrivate::AssetManagerLogName))
	{
		MessageLogModule.RegisterLogListing(
			PEPInventoryEditorPrivate::AssetManagerLogName,
			LOCTEXT("AssetManagerLogLabel", "PEP Inventory"));
		bRegisteredMessageLog = true;
	}
	FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FPEPInventoryEditorModule::CheckAssetManagerConfiguration);

	MenuStartupHandle = UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPEPInventoryEditorModule::RegisterMenus));

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FPEPContainerSlotDefinition::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPEPContainerSlotDefinitionCustomization::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FPEPItemFragment_Container::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPEPItemFragmentContainerCustomization::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FPEPItemPresetChild::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPEPItemPresetChildCustomization::MakeInstance));
	PropertyModule.RegisterCustomClassLayout(
		UPEPItemPreset::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FPEPItemPresetCustomization::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();
}

void FPEPInventoryEditorModule::ShutdownModule()
{
	FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);
	if (MenuStartupHandle.IsValid())
	{
		UToolMenus::UnRegisterStartupCallback(MenuStartupHandle);
		MenuStartupHandle.Reset();
	}
	UToolMenus::UnregisterOwner(this);
	if (bRegisteredMessageLog && FModuleManager::Get().IsModuleLoaded(TEXT("MessageLog")))
	{
		FModuleManager::GetModuleChecked<FMessageLogModule>(TEXT("MessageLog"))
			.UnregisterLogListing(PEPInventoryEditorPrivate::AssetManagerLogName);
	}
	if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		PropertyModule.UnregisterCustomPropertyTypeLayout(FPEPContainerSlotDefinition::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FPEPItemFragment_Container::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FPEPItemPresetChild::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UPEPItemPreset::StaticClass()->GetFName());
	}
}

void FPEPInventoryEditorModule::RegisterMenus()
{
	if (!UToolMenus::IsToolMenuUIEnabled())
	{
		return;
	}

	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	if (!ToolsMenu)
	{
		return;
	}

	FToolMenuSection& PEPSection = ToolsMenu->FindOrAddSection(TEXT("PEP"));
	PEPSection.AddSubMenu(
		TEXT("PEPTools"),
		LOCTEXT("PEPMenuLabel", "PEP"),
		LOCTEXT("PEPMenuTooltip", "Open PEP editor tools."),
		FNewToolMenuDelegate::CreateRaw(this, &FPEPInventoryEditorModule::PopulatePEPSubMenu));
}

void FPEPInventoryEditorModule::PopulatePEPSubMenu(UToolMenu* Menu)
{
	if (!Menu)
	{
		return;
	}

	FToolMenuSection& ItemsSection = Menu->AddSection(TEXT("PEPItemsSection"));
	ItemsSection.AddMenuEntry(
		TEXT("OpenPEPItemBrowser"),
		LOCTEXT("ItemsMenuLabel", "Items"),
		LOCTEXT("ItemsMenuTooltip", "Browse PEP item definitions and item actors in the current level."),
		FSlateIcon(),
		FToolMenuExecuteAction::CreateRaw(this, &FPEPInventoryEditorModule::OpenItemBrowser));
}

void FPEPInventoryEditorModule::OpenItemBrowser(const FToolMenuContext& Context)
{
	if (!GEditor)
	{
		return;
	}

	UEditorUtilityWidgetBlueprint* WidgetBlueprint = LoadObject<UEditorUtilityWidgetBlueprint>(
		nullptr, PEPInventoryEditorPrivate::ItemBrowserAssetPath);
	if (!WidgetBlueprint)
	{
		UE_LOG(LogTemp, Error,
			TEXT("PEP Items browser EUW was not found at %s. Enable PEPInventory content and resave the plugin assets."),
			PEPInventoryEditorPrivate::ItemBrowserAssetPath);
		return;
	}

	if (UEditorUtilitySubsystem* UtilitySubsystem = GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>())
	{
		UtilitySubsystem->SpawnAndRegisterTab(WidgetBlueprint);
	}
}

void FPEPInventoryEditorModule::CheckAssetManagerConfiguration()
{
	const UAssetManagerSettings* Settings = GetDefault<UAssetManagerSettings>();
	if (!Settings || PEPInventoryEditorPrivate::HasPresetScan(*Settings))
	{
		return;
	}
	FMessageLog MessageLog(PEPInventoryEditorPrivate::AssetManagerLogName);
	TSharedRef<FTokenizedMessage> Warning = MessageLog.Warning(LOCTEXT(
		"MissingPresetScan",
		"Asset Manager has no scan configured for PEP ItemPreset. Inventory presets may not be discovered, preloaded, or cooked."));
	Warning->AddToken(FActionToken::Create(
		LOCTEXT("AddPresetScanAction", "Add ItemPreset scan to DefaultGame.ini"),
		LOCTEXT("AddPresetScanTooltip", "Adds the /Game PEP ItemPreset scan to the project's Asset Manager settings."),
		FOnActionTokenExecuted::CreateStatic(&PEPInventoryEditorPrivate::AddPresetScan), true));
	MessageLog.Notify(LOCTEXT("MissingPresetScanNotification", "PEP ItemPreset Asset Manager configuration is incomplete."), EMessageSeverity::Warning);
}

IMPLEMENT_MODULE(FPEPInventoryEditorModule, PEPInventoryEditor)

#undef LOCTEXT_NAMESPACE
