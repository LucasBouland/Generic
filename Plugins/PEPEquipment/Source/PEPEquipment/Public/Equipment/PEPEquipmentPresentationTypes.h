#pragma once

#include "CoreMinimal.h"
#include "Core/PEPTypes.h"
#include "Equipment/PEPEquipmentTypes.h"

#include "PEPEquipmentPresentationTypes.generated.h"

class AActor;
class USceneComponent;

/** Ephemeral local result. Query it again after OnLocalPresentationChanged. */
USTRUCT(BlueprintType)
struct PEPEQUIPMENT_API FPEPLocalPresentationTarget
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	FPEPItemId ItemId;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	EPEPEquipmentPresentationPolicy Policy = EPEPEquipmentPresentationPolicy::Attached;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	TWeakObjectPtr<AActor> PresentationActor;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	TWeakObjectPtr<USceneComponent> VisualComponent;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	TWeakObjectPtr<USceneComponent> SocketProvider;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	int32 Generation = 0;

	bool IsValid() const
	{
		return ItemId.IsValid() && VisualComponent.IsValid() && SocketProvider.IsValid();
	}
};
