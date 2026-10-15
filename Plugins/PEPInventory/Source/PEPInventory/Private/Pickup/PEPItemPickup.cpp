#include "Pickup/PEPItemPickup.h"
#include "Item/PEPContainerItemWorldActor.h"

#include "Components/PEPInventoryComponent.h"
#include "Core/PEPCoreTags.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Item/Actor/PEPItemWorldActor.h"
#include "Item/PEPItemAccess.h"
#include "Item/PEPItemPresetTypes.h"
#include "Item/PEPItemResultTypes.h"

UPEPInventoryComponent* PEPItemPickup::ResolveInteractorInventory(AActor* Interactor)
{
	if (!Interactor)
	{
		return nullptr;
	}

	if (UPEPInventoryComponent* Inventory =
		Interactor->FindComponentByClass<UPEPInventoryComponent>())
	{
		return Inventory;
	}

	if (const APawn* Pawn = Cast<APawn>(Interactor))
	{
		if (AController* Controller = Pawn->GetController())
		{
			return Controller->FindComponentByClass<UPEPInventoryComponent>();
		}
	}

	return nullptr;
}

const AController* PEPItemPickup::ResolveInteractorController(const AActor* Interactor)
{
	if (const APawn* Pawn = Cast<APawn>(Interactor))
	{
		return Pawn->GetController();
	}

	return Cast<AController>(Interactor);
}

FPEPItemResult PEPItemPickup::CanPickupWorldItem(
	const APEPItemWorldActor* WorldItem,
	UPEPInventoryComponent* Inventory,
	const AController* RequestingController)
{
	if (!WorldItem)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget);
	}

	/*
	 * Checked even though most callers already refuse without it: this reads the private tree,
	 * and a tree read off a client is empty rather than absent — a failure that would look like
	 * an empty pickup instead of a call made in the wrong place.
	 */
	if (!WorldItem->HasAuthority())
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_NotAuthority);
	}

	if (!Inventory)
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget);
	}

	const FPEPItemInstanceTree Tree = WorldItem->GetAuthorityItemTree();
	if (!Tree.RootItem.IsValid())
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget);
	}

	/*
	 * The store resolves the role from the identity, so this names the controller and lets the
	 * inventory decide what it may do — a pickup into one's own inventory is an Owner operation,
	 * and the same call into someone else's is not.
	 */
	if (const APEPContainerItemWorldActor* Container = Cast<APEPContainerItemWorldActor>(WorldItem))
	{
		return Container->Inventory->CanTransferPickupToInventory(Inventory,
			Tree.RootItem.ItemId, FPEPItemAccessContext::From(RequestingController));
	}

	return Inventory->CanAddPickupItemTree(
		Tree,
		FPEPItemAccessContext::From(RequestingController));
}

FPEPItemResult PEPItemPickup::ExecutePickupWorldItem(
	APEPItemWorldActor* WorldItem,
	UPEPInventoryComponent* Inventory,
	const AController* RequestingController)
{
	const FPEPItemResult Validation =
		CanPickupWorldItem(WorldItem, Inventory, RequestingController);
	if (!Validation.bSuccess)
	{
		return Validation;
	}

	check(Inventory);

	if (APEPContainerItemWorldActor* Container = Cast<APEPContainerItemWorldActor>(WorldItem))
	{
		return Container->Inventory->TransferPickupToInventory(Inventory,
			Container->GetAuthorityItemInstance().ItemId,
			FPEPItemAccessContext::From(RequestingController));
	}

	return Inventory->AddPickupItemTree(
		WorldItem->GetAuthorityItemTree(),
		FPEPItemAccessContext::From(RequestingController));
}
