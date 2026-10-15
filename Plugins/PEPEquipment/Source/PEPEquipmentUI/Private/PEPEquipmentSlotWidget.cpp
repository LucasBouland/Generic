#include "PEPEquipmentSlotWidget.h"

#include "Components/PEPEquipmentComponent.h"
#include "Core/PEPCoreTags.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Equipment/PEPEquipmentEventTypes.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemStore.h"
#include "PEPDropTargetWidget.h"
#include "PEPItemViewWidget.h"

void UPEPEquipmentSlotWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bConstructed = true;
	SubscribeToEquipment();
	/* Whatever happened while the widget was out of the tree, it shows the slot as it is now. */
	RefreshSlotInternal(true);
}

void UPEPEquipmentSlotWidget::NativeDestruct()
{
	UnsubscribeFromEquipment();
	CancelDefinitionLoad();
	bConstructed = false;
	Super::NativeDestruct();
}

void UPEPEquipmentSlotWidget::SetSlotTag(const FGameplayTag InSlotTag)
{
	if (SlotTag == InSlotTag)
	{
		return;
	}
	SlotTag = InSlotTag;
	ClearDropPreview();
	RefreshSlotInternal(true);
}

void UPEPEquipmentSlotWidget::BindToEquipment(UPEPEquipmentComponent* InEquipment)
{
	if (Equipment.Get() == InEquipment)
	{
		RefreshSlotInternal(false);
		return;
	}
	UnsubscribeFromEquipment();
	CancelDefinitionLoad();
	Equipment = InEquipment;
	/* Before construction the binding is only remembered; NativeConstruct follows it. */
	if (bConstructed)
	{
		SubscribeToEquipment();
	}
	RefreshSlotInternal(true);
}

void UPEPEquipmentSlotWidget::Unbind()
{
	UnsubscribeFromEquipment();
	CancelDefinitionLoad();
	Equipment.Reset();
	ClearDropPreview();
	RefreshSlotInternal(false);
}

UPEPEquipmentComponent* UPEPEquipmentSlotWidget::GetBoundEquipment() const
{
	return Equipment.Get();
}

void UPEPEquipmentSlotWidget::RefreshSlot()
{
	RefreshSlotInternal(false);
}

bool UPEPEquipmentSlotWidget::GetEquippedItemInfo(FPEPEquippedItemInfo& OutInfo) const
{
	OutInfo = bSlotOccupied ? ShownItem : FPEPEquippedItemInfo();
	return bSlotOccupied;
}

FPEPEquipmentSlotDisplayData UPEPEquipmentSlotWidget::GetSlotDisplayData() const
{
	return UPEPEquipmentUILibrary::GetEquipmentSlotDisplayData(Equipment.Get(), SlotTag);
}

FPEPItemResult UPEPEquipmentSlotWidget::CanAcceptItem(const FPEPItemId ItemId) const
{
	const UPEPEquipmentComponent* EquipmentComponent = Equipment.Get();
	if (!EquipmentComponent || !SlotTag.IsValid())
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
	}
	return EquipmentComponent->PreviewEquipItem(ItemId, SlotTag);
}

EPEPRequestResult UPEPEquipmentSlotWidget::AcceptItem(const FPEPItemId ItemId, int32& OutRequestId)
{
	OutRequestId = 0;
	UPEPEquipmentComponent* EquipmentComponent = Equipment.Get();
	if (!EquipmentComponent || !SlotTag.IsValid())
	{
		return EPEPRequestResult::Failed;
	}
	ClearDropPreview();
	return EquipmentComponent->RequestEquipItem(ItemId, SlotTag, OutRequestId);
}

FPEPItemResult UPEPEquipmentSlotWidget::CanUnequipTo(const FPEPItemLocation TargetLocation) const
{
	const UPEPEquipmentComponent* EquipmentComponent = Equipment.Get();
	FPEPEquippedItemInfo Occupant;
	if (!EquipmentComponent || !EquipmentComponent->FindEquippedRootInSlot(SlotTag, Occupant))
	{
		return FPEPItemResult::Failure(PEPCoreTags::Reason_StateMismatch);
	}
	return EquipmentComponent->PreviewUnequipItem(Occupant.ItemId, TargetLocation);
}

EPEPRequestResult UPEPEquipmentSlotWidget::RequestUnequipTo(
	const FPEPItemLocation TargetLocation,
	int32& OutRequestId)
{
	OutRequestId = 0;
	UPEPEquipmentComponent* EquipmentComponent = Equipment.Get();
	/* Read now rather than from the last refresh: the occupant is the one the view holds. */
	FPEPEquippedItemInfo Occupant;
	if (!EquipmentComponent || !EquipmentComponent->FindEquippedRootInSlot(SlotTag, Occupant))
	{
		return EPEPRequestResult::Failed;
	}
	return EquipmentComponent->RequestUnequipItem(Occupant.ItemId, TargetLocation, OutRequestId);
}

FPEPItemResult UPEPEquipmentSlotWidget::UpdateDropPreview(const FPEPItemId ItemId)
{
	const bool bHasCandidate = ItemId.IsValid();
	const FPEPItemResult Verdict = bHasCandidate
		? CanAcceptItem(ItemId)
		: FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, ItemId);
	ApplyDropVisualState(UPEPEquipmentUILibrary::GetDropTargetVisualState(bHasCandidate, Verdict));
	return Verdict;
}

void UPEPEquipmentSlotWidget::ClearDropPreview()
{
	ApplyDropVisualState(EPEPUIDropTargetVisualState::Neutral);
}

void UPEPEquipmentSlotWidget::ApplyDropVisualState(const EPEPUIDropTargetVisualState NewState)
{
	DropVisualState = NewState;
	if (DropTarget)
	{
		DropTarget->SetVisualState(NewState);
	}
}

void UPEPEquipmentSlotWidget::SubscribeToEquipment()
{
	UPEPEquipmentComponent* EquipmentComponent = Equipment.Get();
	if (!EquipmentComponent || SubscribedEquipment.Get() == EquipmentComponent)
	{
		return;
	}
	UnsubscribeFromEquipment();
	/*
	 * Both views: the slot is read from the owner view where this machine keeps one and from the
	 * projection elsewhere, and which one that is can change with possession. A rebuild raising
	 * both costs one extra comparison, never a second notification.
	 */
	EquipmentComponent->OnPublicEquipmentChanged.AddUniqueDynamic(
		this,
		&UPEPEquipmentSlotWidget::HandleEquipmentChanged);
	EquipmentComponent->OnOwnerEquipmentChanged.AddUniqueDynamic(
		this,
		&UPEPEquipmentSlotWidget::HandleEquipmentChanged);
	SubscribedEquipment = EquipmentComponent;
	SubscribeToItemStore();
}

void UPEPEquipmentSlotWidget::UnsubscribeFromEquipment()
{
	UnsubscribeFromItemStore();
	if (UPEPEquipmentComponent* EquipmentComponent = SubscribedEquipment.Get())
	{
		EquipmentComponent->OnPublicEquipmentChanged.RemoveDynamic(
			this,
			&UPEPEquipmentSlotWidget::HandleEquipmentChanged);
		EquipmentComponent->OnOwnerEquipmentChanged.RemoveDynamic(
			this,
			&UPEPEquipmentSlotWidget::HandleEquipmentChanged);
	}
	SubscribedEquipment.Reset();
}

void UPEPEquipmentSlotWidget::SubscribeToItemStore()
{
	const UPEPEquipmentComponent* EquipmentComponent = SubscribedEquipment.Get();
	UObject* ItemStoreObject = EquipmentComponent ? EquipmentComponent->GetItemStoreObject() : nullptr;
	if (SubscribedItemStore.Get() == ItemStoreObject && ItemStoreChangedHandle.IsValid())
	{
		return;
	}
	UnsubscribeFromItemStore();
	if (IPEPItemStore* ItemStore = Cast<IPEPItemStore>(ItemStoreObject))
	{
		ItemStoreChangedHandle = ItemStore->OnItemStoreChanged().AddUObject(
			this,
			&UPEPEquipmentSlotWidget::HandleItemStoreChanged);
		SubscribedItemStore = ItemStoreObject;
	}
}

void UPEPEquipmentSlotWidget::UnsubscribeFromItemStore()
{
	if (IPEPItemStore* ItemStore = Cast<IPEPItemStore>(SubscribedItemStore.Get()))
	{
		ItemStore->OnItemStoreChanged().Remove(ItemStoreChangedHandle);
	}
	ItemStoreChangedHandle.Reset();
	SubscribedItemStore.Reset();
}

void UPEPEquipmentSlotWidget::HandleEquipmentChanged()
{
	/* A component whose store was replaced: follow the new one. */
	SubscribeToItemStore();
	RefreshSlotInternal(false);
}

void UPEPEquipmentSlotWidget::HandleItemStoreChanged(const FPEPItemStoreChange& Change)
{
	/*
	 * Only the slot's own item: its state (a stack count, durability) is not part of either view,
	 * so no view event would ever report it. Nested contents are someone else's widget.
	 */
	EPEPEquipmentSlotItemUpdate ItemUpdate = EPEPEquipmentSlotItemUpdate::None;
	if (UPEPEquipmentUILibrary::FilterEquipmentSlotChange(Change, SlotTag, ItemUpdate)
		&& ItemUpdate != EPEPEquipmentSlotItemUpdate::None)
	{
		RefreshSlotInternal(true);
	}
}

void UPEPEquipmentSlotWidget::RefreshSlotInternal(const bool bForceNotify)
{
	UPEPEquipmentComponent* EquipmentComponent = Equipment.Get();
	FPEPEquippedItemInfo Occupant;
	const bool bFound = EquipmentComponent
		&& SlotTag.IsValid()
		&& EquipmentComponent->FindEquippedRootInSlot(SlotTag, Occupant);

	FPEPItemViewData NewViewData;
	const bool bViewDataBuilt = bFound
		&& UPEPEquipmentUILibrary::BuildEquippedItemViewData(EquipmentComponent, Occupant, NewViewData);

	const bool bChanged = bFound != bSlotOccupied
		|| (bFound && !PEPEquipmentEvents::IsSameEquippedNode(Occupant, ShownItem))
		|| NewViewData.bValid != ItemViewData.bValid;
	bSlotOccupied = bFound;
	ShownItem = bFound ? Occupant : FPEPEquippedItemInfo();
	ItemViewData = NewViewData;

	if (bChanged || bForceNotify)
	{
		if (ItemView)
		{
			FPEPItemInstance Item;
			if (bFound
				&& UPEPEquipmentUILibrary::MakeEquippedItemSnapshot(EquipmentComponent, Occupant, Item))
			{
				ItemView->SetItemInstance(Item);
			}
			else
			{
				ItemView->ClearItem();
			}
		}
		NativeOnSlotItemChanged();
	}

	/*
	 * Last, once this refresh is complete: a load that finishes synchronously refreshes again from
	 * inside the call. Never a synchronous load itself; the item is shown once its definition lands.
	 */
	if (bFound && !bViewDataBuilt)
	{
		RequestDefinitionLoad(Occupant.DefinitionId);
	}
}

void UPEPEquipmentSlotWidget::NativeOnSlotItemChanged()
{
	OnSlotItemChanged(bSlotOccupied, ShownItem, ItemViewData);
}

void UPEPEquipmentSlotWidget::RequestDefinitionLoad(const FPrimaryAssetId DefinitionId)
{
	if (!DefinitionId.IsValid() || PendingDefinitionId == DefinitionId
		|| AttemptedDefinitionLoads.Contains(DefinitionId))
	{
		return;
	}
	CancelDefinitionLoad();
	/*
	 * Asked once per widget, whatever the outcome: the asset manager keeps a loaded primary asset
	 * resident, and one it cannot load (or that does not build view data) is not worth a retry
	 * loop — in particular not one a synchronous completion would turn into recursion.
	 */
	AttemptedDefinitionLoads.Add(DefinitionId);
	PendingDefinitionId = DefinitionId;
	TSharedPtr<FStreamableHandle> Handle = UAssetManager::Get().LoadPrimaryAsset(
		DefinitionId,
		TArray<FName>(),
		FStreamableDelegate::CreateWeakLambda(this, [this, DefinitionId]()
		{
			if (PendingDefinitionId != DefinitionId)
			{
				return;
			}
			PendingDefinitionId = FPrimaryAssetId();
			DefinitionLoadHandle.Reset();
			RefreshSlotInternal(false);
		}));
	if (!Handle.IsValid())
	{
		/* No handle: the asset manager does not know the definition. Nothing is pending. */
		if (PendingDefinitionId == DefinitionId)
		{
			PendingDefinitionId = FPrimaryAssetId();
		}
		return;
	}
	/*
	 * Kept only while pending. A load that completed synchronously has run (or will run) the
	 * delegate, which clears PendingDefinitionId itself.
	 */
	if (!Handle->HasLoadCompleted())
	{
		DefinitionLoadHandle = Handle;
	}
}

void UPEPEquipmentSlotWidget::CancelDefinitionLoad()
{
	/*
	 * Let go rather than CancelHandle: the asset manager hands every caller loading the same
	 * primary asset the same pending handle, and cancelling it would cancel the equipment
	 * component's own load too. The completion is ignored once PendingDefinitionId moved on.
	 */
	if (PendingDefinitionId.IsValid())
	{
		/* Cut short, never answered: it may be asked for again. */
		AttemptedDefinitionLoads.Remove(PendingDefinitionId);
	}
	DefinitionLoadHandle.Reset();
	PendingDefinitionId = FPrimaryAssetId();
}
