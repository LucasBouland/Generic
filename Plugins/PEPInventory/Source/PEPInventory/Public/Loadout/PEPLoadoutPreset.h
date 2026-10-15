#pragma once

#include "CoreMinimal.h"
#include "Components/PEPInventoryTypes.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "PEPLoadoutPreset.generated.h"

class UPEPItemPreset;
class UPEPItemDefinition;

/** One item recipe and the named slot its root occupies. */
USTRUCT(BlueprintType)
struct PEPINVENTORY_API FPEPLoadoutEntry
{
	GENERATED_BODY()

	/**
	 * Only a tag: the store gives named slots no meaning of its own, and whoever claimed the tag —
	 * equipment, for a pawn — decides what may go there. That is what lets a loadout be authored
	 * without this asset knowing equipment exists.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Loadout",
		meta=(Categories="Equipment.Slot"))
	FGameplayTag SlotTag;

	/**
	 * Optional complete item recipe. Set either Preset or ItemDefinition, never both.
	 * Hard by design: loading a loadout loads the whole recipe.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Loadout")
	TObjectPtr<UPEPItemPreset> Preset = nullptr;

	/**
	 * Optional direct item for entries that need no preset, children, or state overrides.
	 * A fresh instance using the definition's default state is created on every application.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Loadout",
		meta=(DisplayName="Item (Direct)"))
	TObjectPtr<UPEPItemDefinition> ItemDefinition = nullptr;
};

/**
 * A designer-authored set of items and where they are worn: a starting kit, a bot's gear, the
 * contents of a prepared corpse.
 *
 * It carries no runtime ids. Each application builds fresh instances, so the same loadout can
 * outfit a hundred pawns without any two of them sharing an item.
 *
 * Named a loadout rather than an equipment preset because nothing here is specific to equipment —
 * it places roots at named slots, and equipment merely happens to be what claims those tags on a
 * pawn. A crafting station or a vehicle rack would use the same asset.
 */
UCLASS(BlueprintType)
class PEPINVENTORY_API UPEPLoadoutPreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType LoadoutAssetType;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Loadout")
	TArray<FPEPLoadoutEntry> Entries;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	/**
	 * Builds the placements this loadout describes, touching no store.
	 *
	 * Separate from applying them so the same recipe can be checked in the editor, previewed, or
	 * handed to a store that is not an inventory at all.
	 */
	bool CreatePlacements(
		const UObject* WorldContextObject,
		TArray<FPEPInventoryTreePlacement>& OutPlacements,
		FText& OutError) const;

#if WITH_EDITOR
	/** Editor validation/preview path; it creates transient IDs and must not be used for gameplay. */
	bool CreatePlacementsForEditor(
		TArray<FPEPInventoryTreePlacement>& OutPlacements,
		FText& OutError) const;
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

private:
	bool CreatePlacementsInternal(
		const UObject* WorldContextObject,
		bool bEditorPreview,
		TArray<FPEPInventoryTreePlacement>& OutPlacements,
		FText& OutError) const;
};
