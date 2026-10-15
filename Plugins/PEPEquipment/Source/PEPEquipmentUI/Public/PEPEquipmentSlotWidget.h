#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "Core/PEPTypes.h"
#include "Equipment/PEPEquipmentTypes.h"
#include "GameplayTagContainer.h"
#include "Item/PEPItemLocation.h"
#include "Item/PEPItemResultTypes.h"
#include "PEPEquipmentUILibrary.h"
#include "PEPItemUILibrary.h"
#include "PEPUIDropTargetStyle.h"

#include "PEPEquipmentSlotWidget.generated.h"

class UPEPDropTargetWidget;
class UPEPEquipmentComponent;
class UPEPItemViewWidget;
struct FPEPItemStoreChange;
struct FStreamableHandle;

/**
 * One equipment slot on screen: what occupies it, and the drop target an item is dragged onto.
 *
 * Configure SlotTag, then BindToEquipment. The widget reads the slot through
 * UPEPEquipmentComponent::FindEquippedRootInSlot, so its owner (authority or owning client) sees
 * its hidden slots too, while another player's pawn shows only its public projection. It follows
 * the component's public and owner-view changes and the store's changes to this slot (stack count,
 * durability), raising OnSlotItemChanged when what it shows changed.
 *
 * Presentation only. It holds the component weakly, owns no equipment state and decides nothing:
 * the drag hooks ask the component (PreviewEquipItem, RequestEquipItem, RequestUnequipItem), and
 * the server's answer arrives through the same events as any other change.
 *
 * Lifecycle: events are followed between NativeConstruct and NativeDestruct; a binding made
 * before construction is kept and followed once constructed. BindToEquipment and RefreshSlot read
 * the current state at once, with or without Slate.
 */
UCLASS(Abstract, Blueprintable)
class PEPEQUIPMENTUI_API UPEPEquipmentSlotWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	/** The slot shown. Refreshes at once when it changes. */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|UI")
	void SetSlotTag(FGameplayTag InSlotTag);

	UFUNCTION(BlueprintPure, Category="PEP|Equipment|UI")
	FGameplayTag GetSlotTag() const
	{
		return SlotTag;
	}

	/** Shows SlotTag of InEquipment and follows it until Unbind, another bind, or destruction. */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|UI")
	void BindToEquipment(UPEPEquipmentComponent* InEquipment);

	/** Stops following the component and shows an empty slot. */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|UI")
	void Unbind();

	UFUNCTION(BlueprintPure, Category="PEP|Equipment|UI")
	UPEPEquipmentComponent* GetBoundEquipment() const;

	/** Reads the slot again now. The widget calls it on every relevant event. */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|UI")
	void RefreshSlot();

	/** The root occupying the slot, as last read. False for an empty slot. */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment|UI")
	bool GetEquippedItemInfo(FPEPEquippedItemInfo& OutInfo) const;

	/**
	 * PEPItemUI view data for the occupant, as last read. bValid stays false while the slot is
	 * empty, and while the item's definition loads (the widget refreshes once it lands).
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment|UI")
	const FPEPItemViewData& GetItemViewData() const
	{
		return ItemViewData;
	}

	/** Label, hand and visibility of the slot from the bound component's profile. */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment|UI")
	FPEPEquipmentSlotDisplayData GetSlotDisplayData() const;

	/**
	 * Whether ItemId could be equipped here: the component's PreviewEquipItem for this slot.
	 * Local validation only; the server decides.
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|UI|Drop")
	FPEPItemResult CanAcceptItem(FPEPItemId ItemId) const;

	/**
	 * Drop: asks the component to equip ItemId here (RequestEquipItem). Executed on the
	 * authority, Submitted from the owning client, which then hears the result through
	 * OnEquipmentRequestResolved and the slot through the usual refresh.
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|UI|Drop")
	EPEPRequestResult AcceptItem(FPEPItemId ItemId, int32& OutRequestId);

	/** Whether the occupant could be moved to TargetLocation: PreviewUnequipItem. */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|UI|Drop")
	FPEPItemResult CanUnequipTo(FPEPItemLocation TargetLocation) const;

	/**
	 * Drag out: asks the component to move the slot's occupant to TargetLocation
	 * (RequestUnequipItem). Fails at once on an empty slot.
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|UI|Drop")
	EPEPRequestResult RequestUnequipTo(FPEPItemLocation TargetLocation, int32& OutRequestId);

	/**
	 * Drag over: shows the verdict for ItemId on the optional DropTarget (Hovered for an invalid
	 * ItemId, then Valid or Invalid from CanAcceptItem) and returns it.
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|UI|Drop")
	FPEPItemResult UpdateDropPreview(FPEPItemId ItemId);

	/** Drag left or dropped: the optional DropTarget goes back to Neutral. */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|UI|Drop")
	void ClearDropPreview();

	UFUNCTION(BlueprintPure, Category="PEP|Equipment|UI|Drop")
	EPEPUIDropTargetVisualState GetDropVisualState() const
	{
		return DropVisualState;
	}

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/**
	 * Raised when what the slot shows changed: occupant, view data, or a store change to this
	 * slot. Calls OnSlotItemChanged; override in C++ to drive native widgets, calling Super.
	 */
	virtual void NativeOnSlotItemChanged();

	/** The slot's occupant or its presentation changed. bHasItem false: the slot is empty. */
	UFUNCTION(BlueprintImplementableEvent, Category="PEP|Equipment|UI")
	void OnSlotItemChanged(
		bool bHasItem,
		const FPEPEquippedItemInfo& ItemInfo,
		const FPEPItemViewData& ViewData);

	/**
	 * Starts and stops following the bound component and its store. Called from NativeConstruct
	 * and NativeDestruct; protected for subclasses that host the widget outside Slate.
	 */
	void SubscribeToEquipment();
	void UnsubscribeFromEquipment();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment|UI",
		meta=(Categories="Equipment.Slot"))
	FGameplayTag SlotTag;

	/** Optional: shown the occupant (SetItemInstance) or cleared. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Equipment|UI", meta=(BindWidgetOptional))
	TObjectPtr<UPEPItemViewWidget> ItemView;

	/** Optional: PEPCoreUI drop feedback driven by UpdateDropPreview / ClearDropPreview. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Equipment|UI", meta=(BindWidgetOptional))
	TObjectPtr<UPEPDropTargetWidget> DropTarget;

private:
	/* Bound to OnPublicEquipmentChanged and OnOwnerEquipmentChanged alike. */
	UFUNCTION()
	void HandleEquipmentChanged();

	void HandleItemStoreChanged(const FPEPItemStoreChange& Change);
	/** Follows the component's current store, which SetItemStore may have replaced. */
	void SubscribeToItemStore();
	void UnsubscribeFromItemStore();
	/** Notifies when what is shown changed, or always with bForceNotify. */
	void RefreshSlotInternal(bool bForceNotify);
	void RequestDefinitionLoad(FPrimaryAssetId DefinitionId);
	void CancelDefinitionLoad();
	void ApplyDropVisualState(EPEPUIDropTargetVisualState NewState);

	TWeakObjectPtr<UPEPEquipmentComponent> Equipment;
	/** The component and store whose delegates this widget is bound to, if any. */
	TWeakObjectPtr<UPEPEquipmentComponent> SubscribedEquipment;
	TWeakObjectPtr<UObject> SubscribedItemStore;
	FDelegateHandle ItemStoreChangedHandle;

	bool bConstructed = false;
	/* Not bHasItem: OnSlotItemChanged's generated thunk takes a parameter of that name. */
	bool bSlotOccupied = false;
	FPEPEquippedItemInfo ShownItem;
	FPEPItemViewData ItemViewData;
	EPEPUIDropTargetVisualState DropVisualState = EPEPUIDropTargetVisualState::Neutral;

	FPrimaryAssetId PendingDefinitionId;
	TSharedPtr<FStreamableHandle> DefinitionLoadHandle;
	/** Definitions this widget already asked the asset manager for: never asked again. */
	TSet<FPrimaryAssetId> AttemptedDefinitionLoads;
};
