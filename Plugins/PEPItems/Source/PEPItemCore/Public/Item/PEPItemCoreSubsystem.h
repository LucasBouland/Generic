// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PEPItemCoreSubsystem.generated.h"

struct FStreamableHandle;

UENUM(BlueprintType)
enum class EPEPItemDefinitionPreloadState : uint8
{
	NotStarted,
	Loading,
	Ready,
	Disabled,
	Failed,
	/** A valid Asset Manager scan exists, but it discovered no ItemDef assets. */
	Empty,
	/** No matching Asset Manager ItemDef scan is configured, or its base class/path is invalid. */
	ConfigurationError
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnPEPItemDefinitionPreloadFinished, EPEPItemDefinitionPreloadState, State);

/** Starts the ItemDef preload early in each game instance and exposes its completion to gameplay/UI. */
UCLASS(BlueprintType)
class PEPITEMCORE_API UPEPItemCoreSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** True when Asset Manager settings contain a usable scan for the ItemDef primary asset type. */
	static bool IsItemDefinitionScanConfigured();

	/** True only when the configured ItemDef catalogue is non-empty and every discovered definition is loaded. */
	UFUNCTION(BlueprintPure, Category="PEP|Items|Loading")
	bool AreItemDefinitionsLoaded() const
	{
		return PreloadState == EPEPItemDefinitionPreloadState::Ready;
	}

	/** True after preload has reached any terminal state, including an empty or misconfigured catalogue. */
	UFUNCTION(BlueprintPure, Category="PEP|Items|Loading")
	bool IsItemDefinitionPreloadComplete() const
	{
		return PreloadState != EPEPItemDefinitionPreloadState::NotStarted
			&& PreloadState != EPEPItemDefinitionPreloadState::Loading;
	}

	UFUNCTION(BlueprintPure, Category="PEP|Items|Loading")
	EPEPItemDefinitionPreloadState GetItemDefinitionPreloadState() const
	{
		return PreloadState;
	}

	/** Bind when obtaining the subsystem, then query the state before allowing item-dependent gameplay. */
	UPROPERTY(BlueprintAssignable, Category="PEP|Items|Loading")
	FOnPEPItemDefinitionPreloadFinished OnItemDefinitionPreloadFinished;

private:
	void HandleItemDefinitionPreloadFinished();
	void SetPreloadState(EPEPItemDefinitionPreloadState NewState);

	EPEPItemDefinitionPreloadState PreloadState = EPEPItemDefinitionPreloadState::NotStarted;
	TSharedPtr<FStreamableHandle> DefinitionPreloadHandle;
};
