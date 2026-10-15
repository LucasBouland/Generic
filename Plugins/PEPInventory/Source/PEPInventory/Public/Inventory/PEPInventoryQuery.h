#pragma once

#include "CoreMinimal.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemInstance.h"
#include "Inventory/PEPInventoryQueryTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PEPInventoryQuery.generated.h"

namespace PEPInventoryQuery
{
	PEPINVENTORY_API bool GetInventoryShapeData(
		const FPEPItemInstance& Item,
		FPEPItemInventoryShapeData& OutData);
}

UCLASS()
class PEPINVENTORY_API UPEPInventoryQueryLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Query")
	static bool GetItemInventoryShapeData(
		const FPEPItemInstance& Item,
		FPEPItemInventoryShapeData& OutData);

	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Query")
	static bool GetDefinitionInventoryShapeData(
		const UPEPItemDefinition* Definition,
		FIntPoint& OutSize,
		bool& bOutCanRotate);

	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Query|Capabilities")
	static bool DoesItemHaveContainer(const FPEPItemInstance& Item);

	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Query|Capabilities")
	static bool DoesItemHaveContentSlots(const FPEPItemInstance& Item);

	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Query|Capabilities")
	static bool DoesItemHaveAttachmentSlots(const FPEPItemInstance& Item);

	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Query|Capabilities")
	static bool GetItemDefinitionContainerCapabilities(
		const UPEPItemDefinition* Definition,
		UPARAM(DisplayName="Has Content Slots") bool& bOutHasContentSlots,
		UPARAM(DisplayName="Has Attachment Slots") bool& bOutHasAttachmentSlots);
};
