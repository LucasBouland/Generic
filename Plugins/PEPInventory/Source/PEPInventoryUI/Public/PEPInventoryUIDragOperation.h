#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PEPInventoryUIDragOperation.generated.h"

/** Implement on the UMG drag operation to handle inventory UI commands. */
UINTERFACE(BlueprintType, Blueprintable)
class PEPINVENTORYUI_API UPEPInventoryUIDragOperation : public UInterface
{
	GENERATED_BODY()
};

class PEPINVENTORYUI_API IPEPInventoryUIDragOperation
{
	GENERATED_BODY()

public:
	/** Rotate the preview and refresh its visual/hover target. Return true if input was handled. */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="PEP|Inventory|UI|Drag")
	bool RotateDraggedItem();
	virtual bool RotateDraggedItem_Implementation();
};