#pragma once

#include "CoreMinimal.h"
#include "Core/PEPTypes.h"
#include "Item/PEPItemResultTypes.h"
#include "GameplayTagContainer.h"
#include "Item/PEPItemInstance.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PEPItemRules.generated.h"

class UPEPItemDefinition;

/**
 * Rules answer "is this allowed", never "here is a decision to replay later". They read current state
 * and return a result; the matching PEPItemMutation entry point re-derives everything from live state.
 * A caller that wants to show the outcome before committing calls the rule, reads the out parameter,
 * and accepts that the authority may reach a different number by the time it executes.
 */
namespace PEPItemRules
{
	PEPITEMCORE_API bool AreSameDefinition(
		const FPEPItemInstance& FirstItem,
		const FPEPItemInstance& SecondItem);

	/**
	 * Whether two instances may share a stack: same definition, and identical stack-identity state.
	 * IsDataValid and runtime instance validation keep per-unit state off stackable definitions.
	 */
	PEPITEMCORE_API bool AreStacksCompatible(
		const FPEPItemInstance& FirstItem,
		const FPEPItemInstance& SecondItem);

	PEPITEMCORE_API bool DoesDefinitionMatchRequiredBlockedTags(
		const UPEPItemDefinition* Definition,
		const FGameplayTagContainer& RequiredTags,
		const FGameplayTagContainer& BlockedTags);

	PEPITEMCORE_API bool DoesItemMatchRequiredBlockedTags(
		const FPEPItemInstance& Item,
		const FGameplayTagContainer& RequiredTags,
		const FGameplayTagContainer& BlockedTags);

	/**
	 * @param RequestedTransferCount  0 means "as much as fits".
	 * @param OutTransferCount        How many units would move. The only figure a caller cannot derive
	 *                                from GetStackData, since it applies the clamping policy.
	 */
	PEPITEMCORE_API FPEPItemResult CanStackItems(
		const FPEPItemInstance& SourceItem,
		const FPEPItemInstance& TargetItem,
		int32 RequestedTransferCount,
		int32& OutTransferCount);

	PEPITEMCORE_API FPEPItemResult CanSplitStack(
		const FPEPItemInstance& SourceItem,
		int32 RequestedSplitCount);

	PEPITEMCORE_API FPEPItemResult CanSetDurability(
		const FPEPItemInstance& Item,
		float NewDurability);

	/**
	 * Resolves the durability a damage or repair of Amount would land on, clamped to 0..1.
	 * Shared by the rule and the mutation so the offset policy exists in one place.
	 *
	 * @param Amount     Magnitude of the change; must be positive whichever direction is asked for.
	 * @param bIsRepair  True adds, false subtracts.
	 */
	PEPITEMCORE_API FPEPItemResult ResolveDurabilityOffset(
		const FPEPItemInstance& Item,
		float Amount,
		bool bIsRepair,
		float& OutTargetDurability);

	PEPITEMCORE_API FPEPItemResult CanDamageDurability(
		const FPEPItemInstance& Item,
		float DamageAmount);

	PEPITEMCORE_API FPEPItemResult CanRepairDurability(
		const FPEPItemInstance& Item,
		float RepairAmount);
}


UCLASS()
class PEPITEMCORE_API UPEPItemRulesLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="PEP|Item|Rules")
	static FPEPItemResult CanStackItems(
		const FPEPItemInstance& SourceItem,
		const FPEPItemInstance& TargetItem,
		int32 RequestedTransferCount,
		int32& OutTransferCount);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Rules")
	static FPEPItemResult CanSplitStack(
		const FPEPItemInstance& SourceItem,
		int32 RequestedSplitCount);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Rules")
	static FPEPItemResult CanSetDurability(
		const FPEPItemInstance& Item,
		float NewDurability);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Rules")
	static FPEPItemResult CanDamageDurability(
		const FPEPItemInstance& Item,
		float DamageAmount);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Rules")
	static FPEPItemResult CanRepairDurability(
		const FPEPItemInstance& Item,
		float RepairAmount);
};
