#pragma once
#include "Blueprint/UserWidget.h"
#include "PEPInventoryUISubsystem.h"
#include "PEPInventoryWindow.generated.h"

/**
 * Shared data for item windows; player windows do not acquire a transfer session.
 *
 * A window follows its item wherever the player can see it: the transfer session or their own
 * inventory. It switches InventoryContext when the item moves between the two, as when the player
 * takes a bag from a looted body, and closes once the item is in neither.
 */
UCLASS(Abstract, Blueprintable)
class PEPINVENTORYUI_API UPEPInventoryWindow : public UUserWidget
{
	GENERATED_BODY()
public:
	/**
	 * Set only by the session presenter: its window takes the cursor and focus, and closing it ends
	 * the session.
	 */
	bool bManageTransferSession = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PEP|Inventory|UI", meta=(ExposeOnSpawn=true))
	FPEPItemInstance OwnerItem;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PEP|Inventory|UI", meta=(ExposeOnSpawn=true))
	FPEPInventoryUIContext InventoryContext;
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;
	/**
	 * Re-read the contents from InventoryContext: the session changed, which announces no per-item
	 * changes, or the item moved between the session and the player's inventory.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category="PEP|Inventory|UI")
	void RefreshSessionContents();
private:
	UFUNCTION()
	void HandleSessionChanged();
	UFUNCTION()
	void HandleItemStoreChanged(const FPEPItemStoreChange& Change);
	/** Moves the window to the store now holding its item; false once the item is in neither. */
	bool FollowItem(bool bSessionChanged);
	/** The subsystem the handlers are bound to; weak because it can go first on teardown. */
	TWeakObjectPtr<UPEPInventoryUISubsystem> BoundUI;
	bool bAcquiredInput = false;
};

/** Canvas host preserves the existing floating window's natural size and panel slot. */
UCLASS()
class PEPINVENTORYUI_API UPEPInventorySessionLayer : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetWindow(UPEPInventoryWindow* Window);
};
