#include "Interaction/PEPPickupContainerActor.h"
#include "Components/PEPInventoryComponent.h"
#include "Components/PEPInventorySessionComponent.h"
#include "Core/PEPCoreTags.h"
#include "Interaction/PEPInteractionTags.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemQuery.h"
#include "Pickup/PEPItemPickup.h"
#include "PEPPickupSettings.h"
#include "Inventory/PEPInventoryQuery.h"

TArray<FGameplayTag> APEPPickupContainerActor::GetInteractOptionTags_Implementation(AActor* Interactor) const
{
	TArray<FGameplayTag> OptionTags;
	/* Read once: on the authority each read copies the root instance out of the inventory. */
	const UPEPItemDefinition* Definition = GetItemDefinition();
	const UPEPPickupSettings* Settings = GetDefault<UPEPPickupSettings>();
	bool bHasContentSlots = false;
	bool bHasAttachmentSlots = false;
	if (Settings && UPEPInventoryQueryLibrary::GetItemDefinitionContainerCapabilities(
		Definition, bHasContentSlots, bHasAttachmentSlots))
	{
		const auto AddSupportedOption = [&OptionTags, bHasContentSlots, bHasAttachmentSlots](
			const FGameplayTag& Tag)
		{
			/* Pickup follows the pickup policy below, never the option tables. */
			if (Tag == PEPInteractionTags::Pickup
				|| (Tag == PEPInteractionTags::Open && !bHasContentSlots)
				|| (Tag == PEPInteractionTags::Inspect && !bHasAttachmentSlots))
			{
				return;
			}
			OptionTags.AddUnique(Tag);
		};

		if (bHasContentSlots)
		{
			for (const FGameplayTag& Tag : Settings->ContentSlotInteractionOptions.GetGameplayTagArray())
			{
				AddSupportedOption(Tag);
			}
		}
		if (bHasAttachmentSlots)
		{
			for (const FGameplayTag& Tag : Settings->AttachmentSlotInteractionOptions.GetGameplayTagArray())
			{
				AddSupportedOption(Tag);
			}
		}

		TArray<FGameplayTag> ItemTagOptions;
		Settings->GetDefaultInteractionOptionTags(Definition, ItemTagOptions);
		for (const FGameplayTag& Tag : ItemTagOptions)
		{
			AddSupportedOption(Tag);
		}
	}

	if (Settings && Settings->CanPickupDefinition(Definition))
	{
		OptionTags.AddUnique(PEPInteractionTags::Pickup);
	}
	return OptionTags;
}
FPEPInteractionResult APEPPickupContainerActor::CanInteract_Implementation(
	AActor* Interactor, FGameplayTag InteractionTag) const
{
	ThisClass* MutableThis = const_cast<ThisClass*>(this);
	if (!HasAuthority())
	{
		return FPEPInteractionResult::Failure(PEPCoreTags::Reason_NotAuthority, MutableThis, InteractionTag);
	}
	if (InteractionTag != PEPInteractionTags::Pickup
		&& InteractionTag != PEPInteractionTags::Open
		&& InteractionTag != PEPInteractionTags::Inspect)
	{
		return FPEPInteractionResult::Failure(PEPCoreTags::Reason_UnsupportedMode, MutableThis, InteractionTag);
	}

	UPEPInventoryComponent* PlayerInventory = PEPItemPickup::ResolveInteractorInventory(Interactor);
	FGameplayTag Reason = PEPCoreTags::Reason_UnsupportedMode;
	if (InteractionTag == PEPInteractionTags::Pickup)
	{
		if (!GetDefault<UPEPPickupSettings>()->CanPickupDefinition(GetItemDefinition()))
		{
			return FPEPInteractionResult::Failure(PEPCoreTags::Reason_BlockedByState, MutableThis, InteractionTag);
		}
		const FPEPItemResult Result = PEPItemPickup::CanPickupWorldItem(this, PlayerInventory,
			PEPItemPickup::ResolveInteractorController(Interactor));
		if (Result.bSuccess) return FPEPInteractionResult::Success(MutableThis, InteractionTag);
		Reason = Result.ReasonTag;
	}
	else
	{
		/* One copy of the private root per query: its slots and its id are read from it. */
		const FPEPItemInstance RootItem = GetAuthorityItemInstance();
		const FPEPItemFragment_Container* Container = RootItem.IsValid()
			? RootItem.GetDefinitionFragment<FPEPItemFragment_Container>()
			: nullptr;
		const bool bHasRequiredSlot = Container && Container->Slots.ContainsByPredicate(
			[InteractionTag](const FPEPContainerSlotDefinition& Slot)
			{
				return InteractionTag == PEPInteractionTags::Open
					? Slot.Purpose == EPEPContainerSlotPurpose::Content
					: Slot.Purpose == EPEPContainerSlotPurpose::Attachment;
			});
		/* Sessions belong to a player's controller; a bot or a controller without one cannot open. */
		const UPEPInventorySessionComponent* Session =
			UPEPInventorySessionComponent::FindSessionComponent(Interactor);
		if (!IsValid(PlayerInventory) || !Session || !RootItem.IsValid())
		{
			Reason = PEPCoreTags::Reason_InvalidTarget;
		}
		else if (!bHasRequiredSlot)
		{
			Reason = PEPCoreTags::Reason_UnsupportedMode;
		}
		else
		{
			const FPEPItemResult Result = InteractionTag == PEPInteractionTags::Open
				? Session->CanOpenTransferSession(Inventory, OpenDistance)
				: Session->CanOpenInspectionSession(
					Inventory, RootItem.ItemId, OpenDistance);
			if (Result.bSuccess) return FPEPInteractionResult::Success(MutableThis, InteractionTag);
			Reason = Result.ReasonTag;
		}
	}
	return FPEPInteractionResult::Failure(Reason, MutableThis, InteractionTag);
}
FPEPInteractionResult APEPPickupContainerActor::ExecuteInteraction_Implementation(
	AActor* Interactor, FGameplayTag InteractionTag)
{
	/* Through the interface, so that a Blueprint override of CanInteract is honoured here too. */
	const FPEPInteractionResult Validation =
		IPEPInteractionInteractable::Execute_CanInteract(this, Interactor, InteractionTag);
	if (!Validation.bSuccess) return Validation;
	UPEPInventoryComponent* PlayerInventory = PEPItemPickup::ResolveInteractorInventory(Interactor);
	UPEPInventorySessionComponent* Session = UPEPInventorySessionComponent::FindSessionComponent(Interactor);
	if (InteractionTag == PEPInteractionTags::Open)
	{
		return Session && Session->OpenTransferSession(Inventory, OpenDistance)
			? FPEPInteractionResult::Success(this, InteractionTag)
			: FPEPInteractionResult::Failure(PEPCoreTags::Reason_InvalidTarget, this, InteractionTag);
	}
	if (InteractionTag == PEPInteractionTags::Inspect)
	{
		return Session && Session->OpenInspectionSession(
			Inventory, GetAuthorityItemInstance().ItemId, OpenDistance)
			? FPEPInteractionResult::Success(this, InteractionTag)
			: FPEPInteractionResult::Failure(PEPCoreTags::Reason_InvalidTarget, this, InteractionTag);
	}
	if (InteractionTag != PEPInteractionTags::Pickup)
	{
		/* A Blueprint CanInteract may allow options this class does not execute. */
		return FPEPInteractionResult::Failure(PEPCoreTags::Reason_UnsupportedMode, this, InteractionTag);
	}
	const FPEPItemResult Result = PEPItemPickup::ExecutePickupWorldItem(this, PlayerInventory,
		PEPItemPickup::ResolveInteractorController(Interactor));
	if (!Result.bSuccess) return FPEPInteractionResult::Failure(Result.ReasonTag, this, InteractionTag);
	HandlePickedUp(Interactor);
	return FPEPInteractionResult::Success(this, InteractionTag);
}

void APEPPickupContainerActor::HandlePickedUp_Implementation(AActor* Interactor)
{
	Destroy();
}
