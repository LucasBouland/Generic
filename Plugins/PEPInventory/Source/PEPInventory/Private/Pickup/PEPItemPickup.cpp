#include "Pickup/PEPItemPickup.h"

#include "Components/PEPInventoryComponent.h"
#include "Core/PEPCoreTags.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Item/Actor/PEPContainerItemWorldActor.h"
#include "Item/Actor/PEPItemWorldActor.h"
#include "Item/PEPItemAccess.h"
#include "Item/PEPItemLogChannels.h"
#include "Item/PEPItemPresetTypes.h"
#include "Item/PEPItemResultTypes.h"

namespace PEPItemPickupPrivate
{
	/** The refusals both halves share. On success the result names the root being picked up. */
	FPEPItemResult CheckPreconditions(
		const APEPItemWorldActor* WorldItem,
		const UPEPInventoryComponent* Inventory,
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

		/*
		 * Someone has to be asking. A missing controller used to reach the store as
		 * FPEPItemAccessContext::From(nullptr), which was the System() bypass under another name:
		 * an unpossessed pawn walking into a crate took it with the server's own permissions.
		 */
		if (!IsValid(RequestingController) || !IsValid(Inventory))
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget);
		}

		const FPEPItemInstance* RootItem = WorldItem->FindAuthorityItemInstance();
		if (!RootItem)
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget);
		}

		return FPEPItemResult::Success(RootItem->ItemId);
	}
}

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
	const FPEPItemResult Preconditions = PEPItemPickupPrivate::CheckPreconditions(
		WorldItem, Inventory, RequestingController);
	if (!Preconditions.bSuccess)
	{
		return Preconditions;
	}

	/*
	 * The store resolves the role from the identity, so this names the controller and lets the
	 * inventory decide what it may do — a pickup into one's own inventory is an Owner operation,
	 * and the same call into someone else's is not.
	 */
	const FPEPItemAccessContext Access = FPEPItemAccessContext::From(RequestingController);

	if (const APEPContainerItemWorldActor* Container = Cast<APEPContainerItemWorldActor>(WorldItem))
	{
		if (!Container->Inventory)
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, Preconditions.ItemId);
		}
		return Container->Inventory->CanTransferPickupToInventory(
			Inventory, Preconditions.ItemId, Access);
	}

	return Inventory->CanAddPickupItemTree(WorldItem->GetAuthorityItemTree(), Access);
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

	check(WorldItem);
	check(Inventory);

	const FPEPItemAccessContext Access = FPEPItemAccessContext::From(RequestingController);

	if (APEPContainerItemWorldActor* Container = Cast<APEPContainerItemWorldActor>(WorldItem))
	{
		/*
		 * The container's store is the only copy, and the transfer moves the tree out of it whole —
		 * already atomic. It leaves the actor empty and standing, like the branch below.
		 */
		return Container->TransferTreeForPickup(Inventory, Access);
	}

	/*
	 * Any other actor holds its own copy, so it is emptied first and the tree added afterwards.
	 * Added first, the item would sit in both places for as long as the store's change broadcast
	 * ran, and a listener reacting to the arrival could find the actor still holding it and pick it
	 * up a second time.
	 */
	FPEPItemInstanceTree Tree;
	if (!WorldItem->TakeAuthorityItemTree(Tree))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget);
	}

	const FPEPItemResult Added = Inventory->AddPickupItemTree(Tree, Access);
	if (!Added.bSuccess && !WorldItem->SetItemTree(Tree))
	{
		/* Validated a moment ago and refused now: a bug, and the one place an item could vanish. */
		UE_LOG(LogPEPItems, Error,
			TEXT("PEPItemPickup: '%s' refused item %s after validating it, and the item could not be returned to the actor."),
			*WorldItem->GetName(),
			*Tree.RootItem.ItemId.ToString());
	}
	return Added;
}
