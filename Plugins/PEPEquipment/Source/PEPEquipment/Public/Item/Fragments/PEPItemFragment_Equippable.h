#pragma once

#include "CoreMinimal.h"
#include "Item/PEPItemFragment.h"
#include "PEPItemFragment_Equippable.generated.h"

/**
 * Declares that an item should offer an Equip action.
 * Actual equipment eligibility still depends on the equipment slot's tag policy and state.
 */
USTRUCT(BlueprintType, meta=(DisplayName="Equippable"))
struct PEPEQUIPMENT_API FPEPItemFragment_Equippable : public FPEPItemFragment
{
	GENERATED_BODY()
};