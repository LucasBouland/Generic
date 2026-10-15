#include "PEPItemCoreEditorModule.h"

#include "Engine/AssetManagerSettings.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemCoreSubsystem.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "MessageLogModule.h"
#include "Misc/CoreDelegates.h"
#include "PEPItemDefinitionCustomization.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "PEPItemCoreEditor"

namespace PEPItemCoreEditorPrivate
{
	const FName AssetManagerLogName(TEXT("PEPItems"));

	void AddItemDefinitionScan()
	{
		UAssetManagerSettings* Settings = GetMutableDefault<UAssetManagerSettings>();
		if (!Settings || UPEPItemCoreSubsystem::IsItemDefinitionScanConfigured())
		{
			return;
		}

		FPrimaryAssetTypeInfo* Existing = Settings->PrimaryAssetTypesToScan.FindByPredicate(
			[](const FPrimaryAssetTypeInfo& Entry)
			{
				return Entry.PrimaryAssetType == UPEPItemDefinition::ItemAssetType;
			});
		if (Existing)
		{
			Existing->SetAssetBaseClass(TSoftClassPtr<UObject>(UPEPItemDefinition::StaticClass()));
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
				UPEPItemDefinition::ItemAssetType.GetName(),
				UPEPItemDefinition::StaticClass(), false, false,
				MoveTemp(Directories), TArray<FSoftObjectPath>());
		}

		/*
		 * Written to the settings' own default config file the way Project Settings writes it,
		 * checked out or made writable as needed, instead of SaveConfig to a hand-built path.
		 */
		if (!Settings->TryUpdateDefaultConfigFile())
		{
			FMessageLog(AssetManagerLogName).Error(LOCTEXT(
				"ItemDefinitionScanNotSaved",
				"Could not write the PEP ItemDef scan to the project's DefaultGame.ini. Check that the file is writable or checked out."));
			return;
		}
		FMessageLog(AssetManagerLogName).Info(LOCTEXT(
			"ItemDefinitionScanAdded",
			"Added the PEP ItemDef scan to Config/DefaultGame.ini. Restart the editor for Asset Manager to apply it."));
	}
}

void FPEPItemCoreEditorModule::StartupModule()
{
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
	if (!MessageLogModule.IsRegisteredLogListing(PEPItemCoreEditorPrivate::AssetManagerLogName))
	{
		MessageLogModule.RegisterLogListing(
			PEPItemCoreEditorPrivate::AssetManagerLogName,
			LOCTEXT("AssetManagerLogLabel", "PEP Items"));
		bRegisteredMessageLog = true;
	}
	FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FPEPItemCoreEditorModule::CheckAssetManagerConfiguration);

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	PropertyModule.RegisterCustomClassLayout(
		UPEPItemDefinition::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FPEPItemDefinitionCustomization::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();
}

void FPEPItemCoreEditorModule::ShutdownModule()
{
	FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);
	if (bRegisteredMessageLog && FModuleManager::Get().IsModuleLoaded(TEXT("MessageLog")))
	{
		FModuleManager::GetModuleChecked<FMessageLogModule>(TEXT("MessageLog"))
			.UnregisterLogListing(PEPItemCoreEditorPrivate::AssetManagerLogName);
	}
	if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
	{
		FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"))
			.UnregisterCustomClassLayout(UPEPItemDefinition::StaticClass()->GetFName());
	}
}

void FPEPItemCoreEditorModule::CheckAssetManagerConfiguration()
{
	const UAssetManagerSettings* Settings = GetDefault<UAssetManagerSettings>();
	if (!Settings || UPEPItemCoreSubsystem::IsItemDefinitionScanConfigured())
	{
		return;
	}

	FMessageLog MessageLog(PEPItemCoreEditorPrivate::AssetManagerLogName);
	TSharedRef<FTokenizedMessage> Warning = MessageLog.Warning(LOCTEXT(
		"MissingItemDefinitionScan",
		"Asset Manager has no scan configured for PEP ItemDef. Definitions may not be discovered, preloaded, or cooked."));
	Warning->AddToken(FActionToken::Create(
		LOCTEXT("AddItemDefinitionScan", "Add ItemDef scan to DefaultGame.ini"),
		LOCTEXT("AddItemDefinitionScanTooltip", "Adds the /Game PEP ItemDef scan to the project's Asset Manager settings."),
		FOnActionTokenExecuted::CreateStatic(&PEPItemCoreEditorPrivate::AddItemDefinitionScan), true));
	MessageLog.Notify(LOCTEXT("MissingItemDefinitionScanNotification", "PEP ItemDef Asset Manager configuration is incomplete."), EMessageSeverity::Warning);
}

IMPLEMENT_MODULE(FPEPItemCoreEditorModule, PEPItemCoreEditor)

#undef LOCTEXT_NAMESPACE
