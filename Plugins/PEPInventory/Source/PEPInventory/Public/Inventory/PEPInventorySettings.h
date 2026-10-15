#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Item/PEPContainerAccessTypes.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "PEPInventorySettings.generated.h"

class APEPItemWorldActor;
class UPEPItemDefinition;
struct FPEPItemFragment_InventoryShape;

/** Project defaults for inventory placement and item actors carried by inventory stores. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="PEP Inventory"))
class PEPINVENTORY_API UPEPInventorySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Grid", meta=(ClampMin="1"))
	FIntPoint DefaultInventorySize = FIntPoint(1, 1);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Grid")
	bool bDefaultInventoryCanRotate = true;

	/** Carrier for definitions without a Container fragment. Prefer a pickup Blueprint. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="World Items")
	TSoftClassPtr<APEPItemWorldActor> DefaultWorldItemActorClass;

	/** Carrier for Container definitions, including empty attachment-only containers. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="World Items")
	TSoftClassPtr<APEPItemWorldActor> DefaultWorldContainerActorClass;

	/** Applied to the store of every world container actor. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="World Containers")
	EPEPExternalAccessMode WorldContainerExternalAccess = EPEPExternalAccessMode::SameAsOwner;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Loading")
	bool bPreloadWorldItemActorClasses = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Validation")
	bool bWarnOnMissingInventoryShape = false;

	/** Reusable slot policies for authored Container fragments. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Container Slots")
	TArray<FPEPContainerSlotPreset> SlotPresets;

	TSubclassOf<APEPItemWorldActor> ResolveWorldItemActorClassForDefinition(
		const UPEPItemDefinition* Definition) const;
	TSubclassOf<APEPItemWorldActor> ResolveWorldItemActorClass() const;
	bool GetEffectiveInventoryShape(
		const FPEPItemFragment_InventoryShape* Fragment,
		FIntPoint& OutSize,
		bool& bOutCanRotate) const;
};

inline const UPEPInventorySettings& PEPInventorySettings()
{
	return *GetDefault<UPEPInventorySettings>();
}
