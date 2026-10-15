#pragma once

#include "CoreMinimal.h"
#include "Item/PEPContainerAccessTypes.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "PEPItemContainerQueryTypes.generated.h"

USTRUCT(BlueprintType)
struct PEPINVENTORY_API FPEPItemContainerData
{
	GENERATED_BODY()

public:
	FPEPItemContainerData();
	~FPEPItemContainerData();
	FPEPItemContainerData(const FPEPItemContainerData& Other);
	FPEPItemContainerData(FPEPItemContainerData&& Other);
	FPEPItemContainerData& operator=(const FPEPItemContainerData& Other);
	FPEPItemContainerData& operator=(FPEPItemContainerData&& Other);

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Container")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Container")
	bool bLocked = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Container")
	bool bCanRelock = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Container")
	EPEPContainerLootVisibility ExternalLootVisibility = EPEPContainerLootVisibility::ShowContents;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Container")
	TArray<FPEPContainerSlotDefinition> Slots;
};
