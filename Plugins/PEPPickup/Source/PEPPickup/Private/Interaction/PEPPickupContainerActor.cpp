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
			if ((Tag == PEPInteractionTags::Open && !bHasContentSlots)
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

	OptionTags.AddUnique(PEPInteractionTags::Pickup);
	return OptionTags;
}
FPEPInteractionResult APEPPickupContainerActor::CanInteract_Implementation(
	AActor* Interactor, FGameplayTag InteractionTag) const
{
	UPEPInventoryComponent* PlayerInventory = PEPItemPickup::ResolveInteractorInventory(Interactor);
	FGameplayTag Reason = PEPCoreTags::Reason_UnsupportedMode;
	if (!HasAuthority()) Reason = PEPCoreTags::Reason_NotAuthority;
	else if (InteractionTag == PEPInteractionTags::Pickup)
	{
		const FPEPItemResult Result = PEPItemPickup::CanPickupWorldItem(this, PlayerInventory,
			PEPItemPickup::ResolveInteractorController(Interactor));
		if (Result.bSuccess) return FPEPInteractionResult::Success(const_cast<ThisClass*>(this), InteractionTag);
		Reason = Result.ReasonTag;
	}
	else if (InteractionTag == PEPInteractionTags::Open
		|| InteractionTag == PEPInteractionTags::Inspect)
	{
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
			if (Result.bSuccess) return FPEPInteractionResult::Success(const_cast<ThisClass*>(this), InteractionTag);
			Reason = Result.ReasonTag;
		}
	}
	return FPEPInteractionResult::Failure(Reason, const_cast<ThisClass*>(this), InteractionTag);
}
FPEPInteractionResult APEPPickupContainerActor::ExecuteInteraction_Implementation(
	AActor* Interactor, FGameplayTag InteractionTag)
{
	const FPEPInteractionResult Validation = CanInteract_Implementation(Interactor, InteractionTag);
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
	const FPEPItemResult Result = PEPItemPickup::ExecutePickupWorldItem(this, PlayerInventory,
		PEPItemPickup::ResolveInteractorController(Interactor));
	if (!Result.bSuccess) return FPEPInteractionResult::Failure(Result.ReasonTag, this, InteractionTag);
	Destroy();
	return FPEPInteractionResult::Success(this, InteractionTag);
}
