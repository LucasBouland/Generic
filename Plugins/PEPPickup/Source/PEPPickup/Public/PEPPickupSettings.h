#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "UObject/ObjectKey.h"

#include "PEPPickupSettings.generated.h"

class UPEPItemDefinition;

/**
 * One row of the default-options table: an item tag and the interaction options it grants.
 * A TMap can't do this, since a single Categories meta filters both its key and value pickers
 * identically — this needs two different tag roots (Item.* for the key, Interaction.* for the
 * value), which only two separately-configured FGameplayTag/FGameplayTagContainer fields allow.
 */
USTRUCT(BlueprintType)
struct FPEPDefaultInteractionOptionsEntry
{
	GENERATED_BODY()

	/** Owned tag an item must have for this row to apply, matched hierarchically. */
	UPROPERTY(EditAnywhere, Category="Interaction Options", meta=(Categories="Item"))
	FGameplayTag ItemTag;

	/** Interaction options granted when ItemTag matches. */
	UPROPERTY(EditAnywhere, Category="Interaction Options", meta=(Categories="Interaction"))
	FGameplayTagContainer OptionTags;
};

/**
 * Project-wide interaction defaults: structural actions by slot purpose plus optional actions by
 * item tag. Designers can give every attachment-bearing item Inspect without editing each asset.
 *
 * Lives here rather than in PEPInteraction (which stays item-agnostic, usable for doors and
 * levers with no notion of Item.* tags) or PEPItems (which stays interaction-agnostic) — PEPPickup
 * is already the adapter between the two, so this table belongs next to it.
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="PEP Pickup"))
class PEPPICKUP_API UPEPPickupSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPEPPickupSettings();

	virtual void PostInitProperties() override;
	virtual void BeginDestroy() override;
	virtual void PostReloadConfig(FProperty* PropertyThatWasLoaded) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Whether items offer Pickup when no tag below decides otherwise. */
	UPROPERTY(Config, EditAnywhere, Category="Pickup", meta=(DisplayName="Pickup By Default"))
	bool bDefaultCanPickup = true;

	/** When not empty, only items owning all these tags can be picked up. */
	UPROPERTY(Config, EditAnywhere, Category="Pickup", meta=(Categories="Item"))
	FGameplayTagContainer RequiredPickupTags;

	/** Items owning any of these tags can never be picked up. */
	UPROPERTY(Config, EditAnywhere, Category="Pickup", meta=(Categories="Item"))
	FGameplayTagContainer BlockedPickupTags;

	/** Actions offered by items with at least one Content slot. Defaults to Interaction.Open. */
	UPROPERTY(Config, EditAnywhere, Category="Interaction Options", meta=(Categories="Interaction"))
	FGameplayTagContainer ContentSlotInteractionOptions;

	/** Actions offered by items with at least one Attachment slot. Defaults to Interaction.Inspect. */
	UPROPERTY(Config, EditAnywhere, Category="Interaction Options", meta=(Categories="Interaction"))
	FGameplayTagContainer AttachmentSlotInteractionOptions;

	/**
	 * An item's final option list is the union of every row whose ItemTag it owns (hierarchy-aware:
	 * an item tagged Item.Type.Weapon.Rifle matches a row keyed Item.Type.Weapon — project tags under
	 * ItemCore's Item.Type root, which declares no children of its own). Pickup is never
	 * read from here: the pickup actors offer it exactly when CanPickupDefinition allows it (the
	 * Pickup category above), and ignore a Pickup entry in this table.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Interaction Options")
	TArray<FPEPDefaultInteractionOptionsEntry> DefaultInteractionOptionsByItemTag;

	/**
	 * Unions the configured option tags for every owned tag of Definition that matches a key.
	 * Cached per definition: the table is walked once per definition until the settings, or in
	 * the editor any item definition, change. Game thread only.
	 */
	void GetDefaultInteractionOptionTags(
		const UPEPItemDefinition* Definition,
		TArray<FGameplayTag>& OutOptionTags) const;

	/** Applies project pickup tag policy to a definition; missing definitions fail closed. */
	bool CanPickupDefinition(const UPEPItemDefinition* Definition) const;

	/**
	 * Forgets the cached option lists. Automatic when the settings are edited or reloaded; call it
	 * after changing DefaultInteractionOptionsByItemTag, or a definition's owned tags, at runtime.
	 */
	void InvalidateInteractionOptionCache() const;

private:
	/* Option lists already resolved, by definition. Never serialized. */
	mutable TMap<FObjectKey, TArray<FGameplayTag>> DefaultInteractionOptionCache;

#if WITH_EDITOR
	/* An edited item definition may own different tags now. */
	void HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);

	FDelegateHandle ObjectPropertyChangedHandle;
#endif
};
