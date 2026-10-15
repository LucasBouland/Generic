#pragma once

#include "CoreMinimal.h"
#include "Interaction/PEPInteractionInteractable.h"
#include "Item/Actor/PEPSimpleItemWorldActor.h"

#include "PEPPickupItemActor.generated.h"

class AController;
class UPEPInventoryComponent;

/**
 * A dropped item that can be picked up through PEPInteraction.
 *
 * The integration point between PEPInventory and PEPInteraction, kept out of both: PEPInventory
 * exposes the store-side pickup logic (PEPItemPickup) without any notion of interaction, and
 * PEPInteraction stays usable for doors and levers without any notion of items. This class is
 * the thin adapter between the two — a project that wants different pickup semantics (auto-pickup
 * on overlap, a GAS ability, ...) is free to skip this plugin entirely and wire PEPItemPickup to
 * whatever it uses instead.
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup=(PEP))
class PEPPICKUP_API APEPPickupItemActor
	: public APEPSimpleItemWorldActor
	, public IPEPInteractionInteractable
{
	GENERATED_BODY()

public:
	/**
	 * Pickup when allowed by UPEPPickupSettings::CanPickupDefinition, plus whatever
	 * DefaultInteractionOptionsByItemTag configures for this item's owned tags (except Open and
	 * Inspect, which need container slots, and Pickup, which only the pickup policy grants).
	 * Works identically on every machine: the interface separates what a thing could offer from
	 * what it will allow right now, and only the second question needs the store —
	 * GetItemDefinition() itself is safe without authority.
	 */
	virtual TArray<FGameplayTag> GetInteractOptionTags_Implementation(
		AActor* Interactor) const override;

	/** Authority only, like every caller of it: it reads the private tree and the target store. */
	virtual FPEPInteractionResult CanInteract_Implementation(
		AActor* Interactor,
		FGameplayTag InteractionTag) const override;

	virtual FPEPInteractionResult ExecuteInteraction_Implementation(
		AActor* Interactor,
		FGameplayTag InteractionTag) override;

protected:
	/**
	 * The store the item goes into. Delegates to PEPItemPickup::ResolveInteractorInventory by
	 * default (interactor, then its controller); override for a project that keeps it somewhere
	 * else entirely — the player state, or a vehicle the interactor is riding.
	 */
	virtual UPEPInventoryComponent* ResolveInteractorInventory(AActor* Interactor) const;

	/**
	 * Called on the authority once the item is in the store. The pickup already emptied this actor
	 * (the item exists in one place only), so GetItemDefinition() and the authority item are no
	 * longer available here. The default destroys the empty actor; an override may play effects or
	 * keep it around, but it holds nothing to pick up twice.
	 */
	UFUNCTION(BlueprintNativeEvent, Category="PEP|Interaction")
	void HandlePickedUp(AActor* Interactor);
	virtual void HandlePickedUp_Implementation(AActor* Interactor);
};
