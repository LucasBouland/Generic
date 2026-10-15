#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemContainerQueryTypes.h"
#include "PEPItemContainerQuery.generated.h"

namespace PEPItemContainerQuery
{
	PEPINVENTORY_API bool GetContainerData(
		const FPEPItemInstance& Item,
		FPEPItemContainerData& OutData);

	inline bool IsContainer(const FPEPItemInstance& Item)
	{
		return Item.GetDefinitionFragment<FPEPItemFragment_Container>() != nullptr;
	}
}

UCLASS()
class PEPINVENTORY_API UPEPItemContainerQueryLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="PEP|Item|Container|Query")
	static bool IsItemContainer(const FPEPItemInstance& Item);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Container|Query")
	static bool GetItemContainerData(
		const FPEPItemInstance& Item,
		FPEPItemContainerData& OutData);
};
