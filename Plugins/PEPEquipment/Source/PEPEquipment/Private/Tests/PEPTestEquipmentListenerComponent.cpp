#include "Tests/PEPTestEquipmentListenerComponent.h"

#include "Components/PEPEquipmentComponent.h"

void UPEPTestEquipmentListenerComponent::Listen(UPEPEquipmentComponent* Equipment)
{
	if (!Equipment)
	{
		return;
	}
	Equipment->OnItemEquipped.AddDynamic(this, &UPEPTestEquipmentListenerComponent::HandleItemEquipped);
	Equipment->OnItemUnequipped.AddDynamic(this, &UPEPTestEquipmentListenerComponent::HandleItemUnequipped);
	Equipment->OnEquipmentRequestResolved.AddDynamic(
		this,
		&UPEPTestEquipmentListenerComponent::HandleRequestResolved);
}

void UPEPTestEquipmentListenerComponent::HandleItemEquipped(const FPEPEquipmentChange& Change)
{
	Changes.Add(Change);
}

void UPEPTestEquipmentListenerComponent::HandleItemUnequipped(const FPEPEquipmentChange& Change)
{
	Changes.Add(Change);
}

void UPEPTestEquipmentListenerComponent::HandleRequestResolved(
	const FPEPEquipmentRequestResult& RequestResult)
{
	Resolutions.Add(RequestResult);
}

FPEPItemResult UPEPTestEquipmentListenerComponent::CanEquipItem_Implementation(
	const UPEPEquipmentComponent* Equipment,
	const FPEPItemInstance& Item,
	const UPEPItemDefinition* Definition,
	const FGameplayTag SlotTag) const
{
	++EligibilityQueries;
	return RefusedSlotTag.IsValid() && SlotTag == RefusedSlotTag
		? FPEPItemResult::Failure(RefusalReasonTag, Item.ItemId)
		: FPEPItemResult::Success(Item.ItemId);
}
