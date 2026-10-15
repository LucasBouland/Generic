#pragma once

#include "EditorValidatorBase.h"

#include "PEPEquipmentDefinitionValidator.generated.h"

/** Cross-fragment validation that needs both Equipment and Item definition data. */
UCLASS()
class UPEPEquipmentDefinitionValidator final : public UEditorValidatorBase
{
	GENERATED_BODY()

protected:
	virtual bool CanValidateAsset_Implementation(
		const FAssetData& InAssetData,
		UObject* InObject,
		FDataValidationContext& InContext) const override;

	virtual EDataValidationResult ValidateLoadedAsset_Implementation(
		const FAssetData& InAssetData,
		UObject* InAsset,
		FDataValidationContext& Context) override;
};
