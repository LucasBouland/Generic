// Copyright Epic Games, Inc. All Rights Reserved.

#include "PEPItemCoreModule.h"
#if WITH_EDITOR
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemSettings.h"
#include "Item/PEPItemLogChannels.h"
#include "Misc/CoreDelegates.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "FPEPItemCoreModule"

void FPEPItemCoreModule::StartupModule()
{
#if WITH_EDITOR
	const UPEPItemSettings& Settings = PEPItemSettings();
	if (Settings.bValidateItemDefinitionsOnStartup
		|| Settings.bValidateItemFragmentsOnStartup
	)
	{
		FCoreDelegates::GetOnPostEngineInit().AddRaw(
			this, &FPEPItemCoreModule::HandlePostEngineInit);
	}
#endif
}

void FPEPItemCoreModule::ShutdownModule()
{
#if WITH_EDITOR
	FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);
#endif
}

#if WITH_EDITOR
void FPEPItemCoreModule::HandlePostEngineInit()
{
	ValidateConfiguredItemDefinitionsOnStartup();
}

void FPEPItemCoreModule::ValidateConfiguredItemDefinitionsOnStartup()
{
	const UPEPItemSettings& Settings = PEPItemSettings();
	if (!Settings.bValidateItemDefinitionsOnStartup && !Settings.bValidateItemFragmentsOnStartup)
	{
		return;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
		TEXT("AssetRegistry"));
	TArray<FAssetData> ItemDefinitionAssets;
	AssetRegistryModule.Get().GetAssetsByClass(
		UPEPItemDefinition::StaticClass()->GetClassPathName(),
		ItemDefinitionAssets,
		true);

	int32 InvalidCount = 0;
	for (const FAssetData& AssetData : ItemDefinitionAssets)
	{
		UPEPItemDefinition* Definition = Cast<UPEPItemDefinition>(AssetData.GetAsset());
		if (!Definition)
		{
			continue;
		}
		FDataValidationContext ValidationContext;
		const EDataValidationResult Result = Definition->ValidateForStartup(
			ValidationContext,
			Settings.bValidateItemDefinitionsOnStartup,
			Settings.bValidateItemFragmentsOnStartup);
		if (Result == EDataValidationResult::Invalid)
		{
			++InvalidCount;
			UE_LOG(LogPEPItems, Error, TEXT("PEP item startup validation failed for '%s'."),
			       *Definition->GetPathName());
		}
	}

	if (InvalidCount > 0)
	{
		UE_LOG(LogPEPItems, Error, TEXT("PEP item startup validation found %d invalid item definitions."),
		       InvalidCount);
	}
}
#endif

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPEPItemCoreModule, PEPItemCore)
