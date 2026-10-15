#pragma once

#include "CoreMinimal.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemResultTypes.h"
#include "Item/PEPItemScalarTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "PEPItemScalar.generated.h"

namespace PEPItemScalarQuery
{
	PEPITEMCORE_API bool GetIntValueFromDefinition(
		const UPEPItemDefinition* Definition,
		FGameplayTag ValueTag,
		FPEPItemIntValueData& OutData);

	PEPITEMCORE_API bool GetFloatValueFromDefinition(
		const UPEPItemDefinition* Definition,
		FGameplayTag ValueTag,
		FPEPItemFloatValueData& OutData);

	PEPITEMCORE_API bool GetItemIntValue(
		const FPEPItemInstance& Item,
		FGameplayTag ValueTag,
		FPEPItemIntValueData& OutData);

	PEPITEMCORE_API bool GetItemFloatValue(
		const FPEPItemInstance& Item,
		FGameplayTag ValueTag,
		FPEPItemFloatValueData& OutData);
}

namespace PEPItemScalarRules
{
	PEPITEMCORE_API FPEPItemResult CanSetIntValue(
		const FPEPItemInstance& Item,
		FGameplayTag ValueTag,
		int32 NewValue);

	PEPITEMCORE_API FPEPItemResult CanSetFloatValue(
		const FPEPItemInstance& Item,
		FGameplayTag ValueTag,
		float NewValue);

	/**
	 * Whether Amount units can be taken off an integer value, and what would be left.
	 * Reaching zero is allowed and means depleted; taking more than is there fails.
	 */
	PEPITEMCORE_API FPEPItemResult CanConsumeIntValue(
		const FPEPItemInstance& Item,
		FGameplayTag ValueTag,
		int32 Amount,
		int32& OutRemaining);
}

namespace PEPItemScalarMutation
{
	PEPITEMCORE_API FPEPItemResult SetIntValue(
		FPEPItemInstance& Item,
		FGameplayTag ValueTag,
		int32 NewValue);

	PEPITEMCORE_API FPEPItemResult SetFloatValue(
		FPEPItemInstance& Item,
		FGameplayTag ValueTag,
		float NewValue);

	/** @param OutRemaining  What is left after the write; zero means the item is now depleted. */
	PEPITEMCORE_API FPEPItemResult ConsumeIntValue(
		FPEPItemInstance& Item,
		FGameplayTag ValueTag,
		int32 Amount,
		int32& OutRemaining);
}

UCLASS()
class PEPITEMCORE_API UPEPItemScalarLibrary
	: public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="PEP|Item|Scalar|Query")
	static bool GetDefinitionIntValue(
		const UPEPItemDefinition* Definition,
		FGameplayTag ValueTag,
		FPEPItemIntValueData& OutData);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Scalar|Query")
	static bool GetDefinitionFloatValue(
		const UPEPItemDefinition* Definition,
		FGameplayTag ValueTag,
		FPEPItemFloatValueData& OutData);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Scalar|Query")
	static bool GetItemIntValue(
		const FPEPItemInstance& Item,
		FGameplayTag ValueTag,
		FPEPItemIntValueData& OutData);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Scalar|Query")
	static bool GetItemFloatValue(
		const FPEPItemInstance& Item,
		FGameplayTag ValueTag,
		FPEPItemFloatValueData& OutData);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Scalar|Rules")
	static FPEPItemResult CanSetItemIntValue(
		const FPEPItemInstance& Item,
		FGameplayTag ValueTag,
		int32 NewValue);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Scalar|Rules")
	static FPEPItemResult CanSetItemFloatValue(
		const FPEPItemInstance& Item,
		FGameplayTag ValueTag,
		float NewValue);

};
