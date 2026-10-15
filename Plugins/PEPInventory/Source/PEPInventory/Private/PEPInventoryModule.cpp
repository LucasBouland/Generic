#include "PEPInventoryModule.h"

#include "Engine/AssetManager.h"
#include "Inventory/PEPInventorySettings.h"
#include "Misc/CoreDelegates.h"

void FPEPInventoryModule::StartupModule()
{
	if (PEPInventorySettings().bPreloadWorldItemActorClasses)
	{
		FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FPEPInventoryModule::HandlePostEngineInit);
	}
}

void FPEPInventoryModule::HandlePostEngineInit()
{
	const UPEPInventorySettings& Settings = PEPInventorySettings();
	TArray<FSoftObjectPath> ClassPaths;
	if (!Settings.DefaultWorldItemActorClass.IsNull())
	{
		ClassPaths.AddUnique(Settings.DefaultWorldItemActorClass.ToSoftObjectPath());
	}
	if (!Settings.DefaultWorldContainerActorClass.IsNull())
	{
		ClassPaths.AddUnique(Settings.DefaultWorldContainerActorClass.ToSoftObjectPath());
	}
	if (!ClassPaths.IsEmpty())
	{
		WorldItemActorClassPreloadHandle = UAssetManager::GetStreamableManager().RequestSyncLoad(
			ClassPaths, true, TEXT("PEP Inventory world actor classes"));
	}
}

void FPEPInventoryModule::ShutdownModule()
{
	FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);
	WorldItemActorClassPreloadHandle.Reset();
}

IMPLEMENT_MODULE(FPEPInventoryModule, PEPInventory)
