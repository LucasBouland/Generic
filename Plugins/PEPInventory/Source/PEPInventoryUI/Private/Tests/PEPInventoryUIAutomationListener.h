#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "PEPInventoryUIDragOperation.h"
#include "Core/PEPTypes.h"
#include "Item/PEPItemStore.h"
#include "PEPInventoryUIAutomationListener.generated.h"

/** Reflection-compatible receiver for the UI subsystem delegate regression test. */
UCLASS(Transient, NotBlueprintable)
class UPEPInventoryUIAutomationListener : public UObject
{
	GENERATED_BODY()

public:
	int32 InventoryChangeCount = 0;
	FPEPItemId LastItemId;

	UFUNCTION()
	void RecordInventoryChange(const FPEPItemStoreChange& Change)
	{
		++InventoryChangeCount;
		LastItemId = Change.ItemId;
	}
};

/** Native implementation used to exercise command routing and UMG lifecycle callbacks. */
UCLASS(Transient, NotBlueprintable)
class UPEPInventoryUITestDragOperation : public UDragDropOperation, public IPEPInventoryUIDragOperation
{
	GENERATED_BODY()

public:
	int32 RotationCount = 0;
	bool bHandleRotation = true;

	virtual bool RotateDraggedItem_Implementation() override
	{
		++RotationCount;
		return bHandleRotation;
	}
};