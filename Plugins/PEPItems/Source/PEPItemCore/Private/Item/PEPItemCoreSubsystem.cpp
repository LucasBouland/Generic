// Copyright Epic Games, Inc. All Rights Reserved.

#include "Item/PEPItemCoreSubsystem.h"

#include "Engine/AssetManager.h"
#include "Engine/AssetManagerSettings.h"
#include "Engine/StreamableManager.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemLogChannels.h"
#include "Item/PEPItemSettings.h"

bool UPEPItemCoreSubsystem::IsItemDefinitionScanConfigured()
{
	const UAssetManagerSettings* Settings = GetDefault<UAssetManagerSettings>();
	if (!Settings)
	{
		return false;
	}

	return Settings->PrimaryAssetTypesToScan.ContainsByPredicate(
		[](const FPrimaryAssetTypeInfo& Entry)
		{
			return Entry.PrimaryAssetType == UPEPItemDefinition::ItemAssetType
				&& Entry.GetAssetBaseClass().ToSoftObjectPath().GetAssetPath()
					== UPEPItemDefinition::StaticClass()->GetClassPathName()
				&& (!Entry.GetDirectories().IsEmpty() || !Entry.GetSpecificAssets().IsEmpty());
		});
}

void UPEPItemCoreSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!PEPItemSettings().bPreloadItemDefinitions)
	{
		SetPreloadState(EPEPItemDefinitionPreloadState::Disabled);
		return;
	}

	SetPreloadState(EPEPItemDefinitionPreloadState::Loading);
	DefinitionPreloadHandle = UAssetManager::Get().LoadPrimaryAssetsWithType(
		UPEPItemDefinition::ItemAssetType,
		TArray<FName>(),
		FStreamableDelegate::CreateUObject(this, &ThisClass::HandleItemDefinitionPreloadFinished));

	if (!DefinitionPreloadHandle.IsValid()
		&& PreloadState == EPEPItemDefinitionPreloadState::Loading)
	{
		HandleItemDefinitionPreloadFinished();
	}
	else if (DefinitionPreloadHandle.IsValid()
		&& DefinitionPreloadHandle->HasLoadCompleted()
		&& PreloadState == EPEPItemDefinitionPreloadState::Loading)
	{
		// The request may complete before its queued delegate is dispatched.
		HandleItemDefinitionPreloadFinished();
	}
}

void UPEPItemCoreSubsystem::Deinitialize()
{
	DefinitionPreloadHandle.Reset();
	Super::Deinitialize();
}

void UPEPItemCoreSubsystem::HandleItemDefinitionPreloadFinished()
{
	if (PreloadState != EPEPItemDefinitionPreloadState::Loading)
	{
		return;
	}

	UAssetManager& AssetManager = UAssetManager::Get();
	TArray<FPrimaryAssetId> ItemDefinitionIds;
	AssetManager.GetPrimaryAssetIdList(UPEPItemDefinition::ItemAssetType, ItemDefinitionIds);
	if (ItemDefinitionIds.IsEmpty())
	{
		if (IsItemDefinitionScanConfigured())
		{
			UE_LOG(LogPEPItems, Warning,
				TEXT("The configured ItemDef Asset Manager scan found no definitions. The PEP item catalogue is empty."));
			SetPreloadState(EPEPItemDefinitionPreloadState::Empty);
		}
		else
		{
			UE_LOG(LogPEPItems, Error,
				TEXT("No valid ItemDef Asset Manager scan is configured. Use the clickable PEP Items Message Log action to add one to DefaultGame.ini."));
			SetPreloadState(EPEPItemDefinitionPreloadState::ConfigurationError);
		}
		return;
	}

	for (const FPrimaryAssetId& ItemDefinitionId : ItemDefinitionIds)
	{
		if (!AssetManager.GetPrimaryAssetObject(ItemDefinitionId))
		{
			UE_LOG(LogPEPItems, Error,
				TEXT("PEP ItemDef preload failed for '%s'. Check that the asset can be loaded and is included in the Asset Manager scan."),
				*ItemDefinitionId.ToString());
			SetPreloadState(EPEPItemDefinitionPreloadState::Failed);
			return;
		}
	}

	UE_LOG(LogPEPItems, Log, TEXT("PEP ItemDef preload completed for %d definitions."), ItemDefinitionIds.Num());
	SetPreloadState(EPEPItemDefinitionPreloadState::Ready);
}

void UPEPItemCoreSubsystem::SetPreloadState(const EPEPItemDefinitionPreloadState NewState)
{
	PreloadState = NewState;
	OnItemDefinitionPreloadFinished.Broadcast(PreloadState);
}
