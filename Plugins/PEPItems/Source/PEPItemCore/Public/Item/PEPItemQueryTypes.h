#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManagerTypes.h"
#include "GameplayTagContainer.h"
#include "Item/Fragments/PEPItemFragment_Durability.h"
#include "Item/Fragments/PEPItemFragment_Resource.h"
#include "Item/Fragments/PEPItemFragment_Stackable.h"
#include "Item/Fragments/PEPItemFragment_Weight.h"
#include "Item/Fragments/PEPItemFragment_WorldPresentation.h"
#include "PEPItemQueryTypes.generated.h"

class UTexture2D;

USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemDisplayData
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category="PEP|Item")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item")
	FPrimaryAssetId DefinitionId;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item")
	FText Description;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item", meta=(Categories="Item"))
	FGameplayTagContainer OwnedTags;
};

USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemStackData
{
	GENERATED_BODY()

public:
	FPEPItemStackData();

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Stack")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Stack", meta=(ClampMin="0"))
	int32 Count = 0;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Stack", meta=(ClampMin="1"))
	int32 MaxStackSize = 1;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Stack", meta=(ClampMin="0"))
	int32 FreeSpace = 0;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Stack")
	bool bFull = false;
};

USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemDurabilityData
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Durability")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Durability", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Durability = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Durability")
	bool bBroken = false;
};

USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemResourceData
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Resource")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Resource", meta=(ClampMin="0"))
	int32 Current = 0;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Resource", meta=(ClampMin="1"))
	int32 MaxResource = 1;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Resource")
	bool bDepleted = false;
};

USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemWeightData
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Weight")
	bool bValid = false;

	/** Mass of one unit. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Weight", meta=(ClampMin="0.0"))
	float UnitWeight = 0.0f;

	/** UnitWeight multiplied by the stack count, or by one when the item does not stack. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Weight", meta=(ClampMin="0.0"))
	float StackWeight = 0.0f;

	/** True when no Weight fragment was present and the project default was applied. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Weight")
	bool bUsesDefaultWeight = true;
};

USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemWorldPresentationData
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|WorldPresentation")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|WorldPresentation")
	TSoftObjectPtr<UStaticMesh> StaticMesh;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|WorldPresentation")
	FTransform MeshTransform = FTransform::Identity;
};
