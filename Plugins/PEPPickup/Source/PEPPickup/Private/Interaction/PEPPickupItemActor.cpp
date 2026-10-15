#include "Interaction/PEPPickupItemActor.h"

#include "Components/PEPInventoryComponent.h"
#include "Core/PEPCoreTags.h"
#include "Interaction/PEPInteractionTags.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemResultTypes.h"
#include "PEPPickupSettings.h"
#include "Pickup/PEPItemPickup.h"

namespace
{
	/** Carries the store's verdict across the domain boundary instead of flattening it to a bool. */
	FPEPInteractionResult ToInteractionResult(
		const FPEPItemResult& ItemResult,
		AActor* Target,
		const FGameplayTag InteractionTag)
	{
		return ItemResult.bSuccess
			? FPEPInteractionResult::Success(Target, InteractionTag)
			: FPEPInteractionResult::Failure(
				ItemResult.ReasonTag,
				Target,
				InteractionTag);
	}
}

TArray<FGameplayTag> APEPPickupItemActor::GetInteractOptionTags_Implementation(
	AActor* Interactor) const
{
	TArray<FGameplayTag> OptionTags;

	/* Read once: on the authority each read copies the private item instance. */
	const UPEPItemDefinition* Definition = GetItemDefinition();
	if (!Definition)
	{
		return OptionTags;
	}

	const UPEPPickupSettings* Settings = GetDefault<UPEPPickupSettings>();
	Settings->GetDefaultInteractionOptionTags(Definition, OptionTags);

	/* Structural options belong to containers; Pickup follows the pickup policy, never the table. */
	OptionTags.Remove(PEPInteractionTags::Open);
	OptionTags.Remove(PEPInteractionTags::Inspect);
	OptionTags.Remove(PEPInteractionTags::Pickup);
	if (Settings->CanPickupDefinition(Definition))
	{
		OptionTags.Add(PEPInteractionTags::Pickup);
	}
	return OptionTags;
}

UPEPInventoryComponent* APEPPickupItemActor::ResolveInteractorInventory(AActor* Interactor) const
{
	return PEPItemPickup::ResolveInteractorInventory(Interactor);
}

FPEPInteractionResult APEPPickupItemActor::CanInteract_Implementation(
	AActor* Interactor,
	const FGameplayTag InteractionTag) const
{
	if (InteractionTag != PEPInteractionTags::Pickup)
	{
		return FPEPInteractionResult::Failure(
			PEPCoreTags::Reason_UnsupportedMode,
			const_cast<APEPPickupItemActor*>(this),
			InteractionTag);
	}
	if (!GetDefault<UPEPPickupSettings>()->CanPickupDefinition(GetItemDefinition()))
	{
		return FPEPInteractionResult::Failure(
			PEPCoreTags::Reason_BlockedByState,
			const_cast<APEPPickupItemActor*>(this),
			InteractionTag);
	}

	return ToInteractionResult(
		PEPItemPickup::CanPickupWorldItem(
			this,
			ResolveInteractorInventory(Interactor),
			PEPItemPickup::ResolveInteractorController(Interactor)),
		const_cast<APEPPickupItemActor*>(this),
		InteractionTag);
}

FPEPInteractionResult APEPPickupItemActor::ExecuteInteraction_Implementation(
	AActor* Interactor,
	const FGameplayTag InteractionTag)
{
	/*
	 * The tag check normally lives inside CanInteract; re-run here too since Execute no longer
	 * calls it directly — the store-side re-validation below covers everything else CanInteract
	 * would have checked.
	 */
	if (InteractionTag != PEPInteractionTags::Pickup)
	{
		return FPEPInteractionResult::Failure(
			PEPCoreTags::Reason_UnsupportedMode,
			this,
			InteractionTag);
	}
	if (!GetDefault<UPEPPickupSettings>()->CanPickupDefinition(GetItemDefinition()))
	{
		return FPEPInteractionResult::Failure(
			PEPCoreTags::Reason_BlockedByState,
			this,
			InteractionTag);
	}

	UPEPInventoryComponent* Inventory = ResolveInteractorInventory(Interactor);

	/*
	 * Re-run rather than trusted: the caller validated, but the same rule against live state is
	 * what the rest of the item code does, and the gap between the two is where an inventory that
	 * filled up in between would have been missed.
	 */
	const FPEPItemResult Added = PEPItemPickup::ExecutePickupWorldItem(
		this,
		Inventory,
		PEPItemPickup::ResolveInteractorController(Interactor));

	if (!Added.bSuccess)
	{
		return ToInteractionResult(Added, this, InteractionTag);
	}

	HandlePickedUp(Interactor);

	return FPEPInteractionResult::Success(this, InteractionTag);
}

void APEPPickupItemActor::HandlePickedUp_Implementation(AActor* Interactor)
{
	Destroy();
}
