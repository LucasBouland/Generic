#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Item/PEPItemPresetTypes.h"
#include "StructUtils/InstancedStruct.h"

#include "PEPItemPreset.generated.h"

class UPEPItemDefinition;
class UPEPItemPreset;

/** Placement of another preset inside a container slot owned by this preset's root item. */
USTRUCT(BlueprintType)
struct PEPINVENTORY_API FPEPItemPresetChild
{
	GENERATED_BODY()

	/**
	 * Optional nested preset. Hard by design: loading it loads its complete recipe graph
	 * atomically. Set either Preset or ItemDefinition, never both.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Preset")
	TObjectPtr<UPEPItemPreset> Preset = nullptr;

	/**
	 * Optional direct item for children that need no nested preset, further children, or state
	 * overrides. A fresh instance using the definition's default state is created on every build.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Preset",
		meta=(DisplayName="Item (Direct)"))
	TObjectPtr<UPEPItemDefinition> ItemDefinition = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Preset")
	FName ContainerSlotName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Preset")
	FIntPoint GridPosition = FIntPoint::ZeroValue;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Preset")
	bool bRotated = false;
};

/**
 * Designer-authored spawn recipe. It contains no runtime IDs: CreateItemTree generates a fresh
 * root instance and fresh descendants every time it is called.
 */
UCLASS(BlueprintType)
class PEPINVENTORY_API UPEPItemPreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType PresetAssetType;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Preset")
	TObjectPtr<UPEPItemDefinition> RootDefinition = nullptr;

	/** Replaces default state values of the same exact state type on the new root instance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Preset",
		meta=(BaseStruct="/Script/PEPItemCore.PEPItemStateFragment", ExcludeBaseStruct))
	TArray<FInstancedStruct> StateOverrides;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Preset")
	TArray<FPEPItemPresetChild> Children;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Item|Preset",
		meta=(WorldContext="WorldContextObject"))
	bool CreateItemTree(
		const UObject* WorldContextObject,
		FPEPItemInstanceTree& OutTree,
		FText& OutError) const;

#if WITH_EDITOR
	/** Editor validation/preview path; it creates transient IDs and must not be used for gameplay. */
	bool CreateItemTreeForEditor(FPEPItemInstanceTree& OutTree, FText& OutError) const;
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
