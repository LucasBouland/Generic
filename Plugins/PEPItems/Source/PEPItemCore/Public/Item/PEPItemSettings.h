#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Item/PEPItemFragment.h"
#include "PEPItemSettings.generated.h"
class UStaticMesh;

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="PEP Items"))
class PEPITEMCORE_API UPEPItemSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Stacking", meta=(ClampMin="1"))
	int32 DefaultMaxStackSize = 1;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Durability", meta=(ClampMin="0.0", ClampMax="1.0"))
	float DefaultStartingDurability = 1.0f;

	/** Applied to any definition without a Weight fragment, so every item has a mass. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Weight", meta=(ClampMin="0.0"))
	float DefaultUnitWeight = 0.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="World Presentation")
	bool bUseDefaultWorldPresentationWhenMissing = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="World Presentation")
	TSoftObjectPtr<UStaticMesh> DefaultWorldStaticMesh;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="World Presentation")
	FTransform DefaultWorldMeshTransform = FTransform::Identity;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Loading")
	bool bAllowSynchronousDefinitionLoad = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Loading")
	bool bWarnOnSynchronousDefinitionLoad = false;

	/** Preloads registered ItemDef assets asynchronously from UPEPItemCoreSubsystem at game-instance startup. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Loading")
	bool bPreloadItemDefinitions = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Validation")
	bool bValidateItemDefinitionsOnStartup = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Validation")
	bool bValidateItemFragmentsOnStartup = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Validation")
	bool bWarnOnMissingWorldPresentation = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Validation")
	bool bWarnOnMissingItemIcon = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Validation")
	bool bTreatItemValidationWarningsAsErrors = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Debug|Log")
	bool bLogItemQueries = false;

	/**
	 * Starter kits of DefinitionFragments for common item archetypes — a wearable container, a
	 * stackable consumable — applied from the item definition's Details panel in one click.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Fragment Bundles")
	TArray<FPEPItemFragmentBundle> FragmentBundles;

	/*
	 * Effective values: the fragment's own value, or the project default when it opts in. Always
	 * in range and finite, whatever an ini or an asset holds: NaN or infinite durability falls back
	 * to 1, NaN or infinite weight to 0.
	 */
	int32 GetEffectiveMaxStackSize(int32 FragmentMaxStackSize, bool bUseProjectDefault) const;
	float GetEffectiveStartingDurability(float FragmentStartingDurability, bool bUseProjectDefault) const;
	float GetEffectiveUnitWeight(float FragmentUnitWeight, bool bUseProjectDefault) const;
	bool GetEffectiveWorldPresentation(const struct FPEPItemFragment_WorldPresentation* Fragment,
	                                   struct FPEPItemWorldPresentationData& OutData) const;
};

/** The settings CDO always exists, so callers never need to null-check GetDefault<UPEPItemSettings>(). */
inline const UPEPItemSettings& PEPItemSettings()
{
	return *GetDefault<UPEPItemSettings>();
}
