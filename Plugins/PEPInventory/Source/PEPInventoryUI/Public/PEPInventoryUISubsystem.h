#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Actors/PEPPlayerController.h"
#include "Components/PEPInventoryRequest.h"
#include "Components/PEPInventoryTypes.h"
#include "PEPInventoryUISubsystem.generated.h"

class UPEPInventoryComponent;
class UPEPInventorySessionComponent;
class UPEPInventoryUISettings;
class UDragDropOperation;
class UUserWidget;
class UPEPInventoryWindow;
class UPEPInventorySessionLayer;
class APawn;

UENUM(BlueprintType)
enum class EPEPInventoryUIContextType : uint8
{
	None,
	PlayerInventory,
	TransferSession
};

/** Capture when creating a container or starting a drag; do not refresh during that drag. */
USTRUCT(BlueprintType)
struct PEPINVENTORYUI_API FPEPInventoryUIContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="PEP|Inventory|UI")
	EPEPInventoryUIContextType Type = EPEPInventoryUIContextType::None;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Inventory|UI")
	FGuid InventoryBindingId;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Inventory|UI")
	FGuid TransferSessionId;

	/** Transfer or read-only Inspect; None for the player inventory context. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Inventory|UI")
	EPEPInventorySessionMode SessionMode = EPEPInventorySessionMode::None;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPEPInventoryUIBindingChanged);

/** Context of the session now shown; Type is None when the loot side should hide. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPEPTransferSessionPresented,
	const FPEPInventoryUIContext&, SessionContext);

/** Local presentation/routing service. Network authority remains on the inventory component. */
UCLASS(BlueprintType)
class PEPINVENTORYUI_API UPEPInventoryUISubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;

	/** Main inventory screen. Set when created; clear when permanently discarded. */
	UPROPERTY(Transient, BlueprintReadWrite, Category="PEP|Inventory|UI")
	TObjectPtr<UUserWidget> InventoryScreen = nullptr;

	/** Call when this local player's inventory becomes available (and again after replacement). */
	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|UI")
	bool RegisterInventoryComponent(UPEPInventoryComponent* InventoryComponent);

	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|UI")
	void UnregisterInventoryComponent();

	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI")
	UPEPInventoryComponent* GetInventoryComponent() const;

	/** The session component on the observed controller; null without one, or without a controller. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI")
	UPEPInventorySessionComponent* GetSessionComponent() const;

	/** Returns None until a local inventory is registered. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI")
	FPEPInventoryUIContext GetPlayerInventoryContext() const;

	/** Returns None until a session with an identity is available. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI")
	FPEPInventoryUIContext GetTransferSessionContext() const;

	/** Presentation tag supplied by the remote inventory; invalid without an active session. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI")
	FGameplayTag GetTransferSessionPanelTag() const;

	/** Lets widgets disable drag/drop affordances while an Inspect projection is shown. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI")
	EPEPInventorySessionMode GetTransferSessionMode() const;

	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI")
	bool IsTransferSessionReadOnly() const;


	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI")
	bool IsContextCurrent(const FPEPInventoryUIContext& Context) const;

	/** The world item a session was opened on. False for sessions without one, or before it replicates. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI")
	bool GetTransferSessionRootItem(FPEPItemInstance& OutRootItem) const;

	/** Read only the selected local/session view; stale contexts return no entries. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI")
	void GetEntriesInContainerSlot(const FPEPInventoryUIContext& Context,
		FPEPItemId ParentItemId, FName SlotName, TArray<FPEPInventoryEntry>& OutEntries) const;

	/**
	 * The item in named slot SlotTag (an equipment slot, say) of the selected local/session view.
	 * Lets a widget fill itself from a session, which announces no per-item changes.
	 * False when the slot is empty or the context is stale.
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI")
	bool FindEntryInNamedSlot(const FPEPInventoryUIContext& Context, FGameplayTag SlotTag,
		FPEPInventoryEntry& OutEntry) const;

	/**
	 * Supports player->player, session->player, player->session and session->session. Does not mutate inventory.
	 * Repeating the last question answers from cache until the drag ends or either store changes.
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|UI")
	FPEPItemResult PreviewPlacement(const FPEPInventoryUIContext& SourceContext,
		const FPEPInventoryUIContext& DestinationContext, FPEPItemId ItemId,
		FPEPItemLocation Destination) const;

	/** Rechecks the preview, then uses the owning player's existing network requests. */
	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|UI")
	EPEPRequestResult RequestPlacement(const FPEPInventoryUIContext& SourceContext,
		const FPEPInventoryUIContext& DestinationContext, FPEPItemId ItemId,
		FPEPItemLocation Destination, int32& OutRequestId);

	/** Live project settings, readable from widgets. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI|Style")
	const UPEPInventoryUISettings* GetUISettings() const;

	/** Valid/invalid drop tint; alpha already includes PreviewOpacity. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI|Style")
	FLinearColor GetDropPreviewColor(bool bIsValidPlacement) const;

	/** Register before returning from OnDragDetected. Requires PEPInventoryUIDragOperation. */
	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|UI|Drag")
	bool BeginDrag(UDragDropOperation* Operation);

	/** Calls RotateDraggedItem on the active operation. Returns its handled result. */
	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|UI|Drag")
	bool RotateActiveDrag();

	/** Clear only this operation. Also called automatically on Drop and DragCancelled. */
	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|UI|Drag")
	void EndDrag(UDragDropOperation* Operation);

	UFUNCTION(BlueprintPure, Category="PEP|Inventory|UI|Drag")
	UDragDropOperation* GetActiveDragOperation() const;

	UPROPERTY(BlueprintAssignable, Category="PEP|Inventory|UI")
	FOnPEPInventoryUIBindingChanged OnInventoryBindingChanged;

	UPROPERTY(BlueprintAssignable, Category="PEP|Inventory|UI")
	FOnPEPInventoryChangedBP OnInventoryChanged;

	UPROPERTY(BlueprintAssignable, Category="PEP|Inventory|UI")
	FOnPEPInventorySessionViewChanged OnSessionViewChanged;

	/**
	 * Player-menu presentation only: fires once when a session opens, before the menu does, and
	 * once with a None context when it ends. Contents changes still arrive on OnSessionViewChanged.
	 */
	UPROPERTY(BlueprintAssignable, Category="PEP|Inventory|UI")
	FOnPEPTransferSessionPresented OnTransferSessionPresented;

	/** May fire synchronously on authority; bind before calling RequestPlacement. */
	UPROPERTY(BlueprintAssignable, Category="PEP|Inventory|UI")
	FOnPEPInventoryRequestResolved OnRequestResolved;

private:
	TWeakObjectPtr<APlayerController> ObservedController;
	UFUNCTION()
	void HandlePawnChanged(APawn* OldPawn, APawn* NewPawn);
	UPROPERTY(Transient)
	TObjectPtr<UPEPInventoryWindow> SessionWindow;
	UPROPERTY(Transient)
	TObjectPtr<UPEPInventorySessionLayer> SessionLayer;
	void UpdateSessionWindow();

	/**
	 * Last PreviewPlacement answer. A drag asks on every mouse move, nearly always about the cell
	 * it asked about last time; only a different question or a store change recomputes.
	 */
	struct FPlacementPreviewCache
	{
		FPEPInventoryUIContext Source;
		FPEPInventoryUIContext Destination;
		FPEPItemId ItemId;
		FPEPItemLocation Location;
		FPEPItemResult Result;
		bool bValid = false;
	};
	mutable FPlacementPreviewCache PlacementPreviewCache;
	FPEPItemResult ComputePlacementPreview(const FPEPInventoryUIContext& SourceContext,
		const FPEPInventoryUIContext& DestinationContext, FPEPItemId ItemId,
		const FPEPItemLocation& Destination) const;
	void InvalidatePlacementPreview() { PlacementPreviewCache.bValid = false; }
	/** Session id OnTransferSessionPresented last announced; invalid while nothing is shown. */
	FGuid PresentedSessionId;
	void UpdateSessionMenu();
	UFUNCTION()
	void HandlePlayerInputStateChanged(EPEPPlayerInputState InputState);
	UPROPERTY(Transient)
	TWeakObjectPtr<UDragDropOperation> ActiveDragOperation;

	void ClearActiveDrag();

	UFUNCTION()
	void HandleDragEnded(UDragDropOperation* Operation);

	UPROPERTY(Transient)
	TWeakObjectPtr<UPEPInventoryComponent> PlayerInventory;

	/** Bound per controller, not per inventory: a session outlives the pawn that opened it. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UPEPInventorySessionComponent> ObservedSession;
	void AttachSession(APlayerController* Controller);
	void DetachSession();

	FGuid InventoryBindingId;

	void DetachInventory();

	UFUNCTION()
	void HandleInventoryChanged(const FPEPItemStoreChange& Change);

	UFUNCTION()
	void HandleSessionViewChanged();

	UFUNCTION()
	void HandleRequestResolved(const FPEPInventoryRequestResult& RequestResult);
};
