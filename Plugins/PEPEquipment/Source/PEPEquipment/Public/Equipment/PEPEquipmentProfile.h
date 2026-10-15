#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Equipment/PEPEquipmentTypes.h"
#include "Equipment/PEPPresentationTargetDefinition.h"

#include "PEPEquipmentProfile.generated.h"

/** Fixed, designer-authored slot and presentation policy for one family of pawns. */
UCLASS(BlueprintType, meta=(DisplayName="PEP Equipment Profile"))
class PEPEQUIPMENT_API UPEPEquipmentProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Targets exposed by the pawn family using this profile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment")
	TArray<FPEPPresentationTargetDefinition> PresentationTargets;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment")
	TArray<FPEPEquipmentSlotDefinition> Slots;

	const FPEPPresentationTargetDefinition* FindPresentationTarget(
		FGameplayTag TargetTag) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
