#pragma once
#include "CoreMinimal.h"
#include "Item/PEPItemFragment.h"
#include "PEPItemFragment_InventoryShape.generated.h"

USTRUCT(BlueprintType, meta=(DisplayName="Inventory Shape"))
struct PEPINVENTORY_API FPEPItemFragment_InventoryShape : public FPEPItemFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item")
	FIntPoint Size = FIntPoint(1, 1);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item")
	bool bCanRotate = true;
#if WITH_EDITOR
	virtual EDataValidationResult ValidateFragmentData(
		FDataValidationContext& Context,
		const int32 FragmentIndex) const override;
#endif
};
