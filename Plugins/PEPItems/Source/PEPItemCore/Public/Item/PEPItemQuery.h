#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemQueryTypes.h"
#include "PEPItemQuery.generated.h"

namespace PEPItemQuery
{
	PEPITEMCORE_API const UPEPItemDefinition* GetDefinition(
		const FPEPItemInstance& Item);

	PEPITEMCORE_API bool GetDisplayDataFromDefinition(
		const UPEPItemDefinition* Definition,
		FPEPItemDisplayData& OutData);

	PEPITEMCORE_API bool GetDisplayData(
		const FPEPItemInstance& Item,
		FPEPItemDisplayData& OutData);

	PEPITEMCORE_API bool HasOwnedTag(
		const FPEPItemInstance& Item,
		FGameplayTag Tag);

	PEPITEMCORE_API bool HasOwnedTagExact(
		const FPEPItemInstance& Item,
		FGameplayTag Tag);

	PEPITEMCORE_API bool HasAnyOwnedTags(
		const FPEPItemInstance& Item,
		const FGameplayTagContainer& Tags);

	PEPITEMCORE_API bool HasAllOwnedTags(
		const FPEPItemInstance& Item,
		const FGameplayTagContainer& Tags);

	PEPITEMCORE_API bool GetOwnedTags(
		const FPEPItemInstance& Item,
		FGameplayTagContainer& OutTags);

	PEPITEMCORE_API bool DoesItemMatchRequiredBlockedTags(
		const FPEPItemInstance& Item,
		const FGameplayTagContainer& RequiredTags,
		const FGameplayTagContainer& BlockedTags);

	PEPITEMCORE_API bool GetStackData(
		const FPEPItemInstance& Item,
		FPEPItemStackData& OutData);

	PEPITEMCORE_API bool GetDurabilityData(
		const FPEPItemInstance& Item,
		FPEPItemDurabilityData& OutData);

	PEPITEMCORE_API bool GetResourceData(
		const FPEPItemInstance& Item,
		FPEPItemResourceData& OutData);

	/** Always succeeds for a valid item: a definition without a Weight fragment uses the project default. */
	PEPITEMCORE_API bool GetWeightData(
		const FPEPItemInstance& Item,
		FPEPItemWeightData& OutData);

	PEPITEMCORE_API bool GetWorldPresentationData(
		const FPEPItemInstance& Item,
		FPEPItemWorldPresentationData& OutData);


	template <typename TFragment>
	const TFragment* GetFragment(const FPEPItemInstance& Item)
	{
		static_assert(TIsDerivedFrom<TFragment, FPEPItemFragment>::IsDerived,
		              "TFragment must derive from FPEPItemFragment");
		return Item.GetDefinitionFragment<TFragment>();
	}

	template <typename TFragment>
	bool HasFragment(const FPEPItemInstance& Item)
	{
		return GetFragment<TFragment>(Item) != nullptr;
	}

	template <typename TState>
	const TState* GetState(const FPEPItemInstance& Item)
	{
		static_assert(TIsDerivedFrom<TState, FPEPItemStateFragment>::IsDerived,
		              "TState must derive from FPEPItemStateFragment");
		return Item.GetState<TState>();
	}

	template <typename TState>
	bool HasState(const FPEPItemInstance& Item)
	{
		return GetState<TState>(Item) != nullptr;
	}

	inline bool IsStackable(const FPEPItemInstance& Item)
	{
		return HasFragment<FPEPItemFragment_Stackable>(Item);
	}

	inline bool HasDurability(const FPEPItemInstance& Item)
	{
		return HasFragment<FPEPItemFragment_Durability>(Item);
	}

	inline bool HasResource(const FPEPItemInstance& Item)
	{
		return HasFragment<FPEPItemFragment_Resource>(Item);
	}

	inline bool HasWorldPresentation(const FPEPItemInstance& Item)
	{
		return HasFragment<FPEPItemFragment_WorldPresentation>(Item);
	}
}

UCLASS()
class PEPITEMCORE_API UPEPItemQueryLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="PEP|Item|Query")
	static UPEPItemDefinition* GetItemDefinition(
		const FPEPItemInstance& Item);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Query")
	static bool GetItemDisplayData(
		const FPEPItemInstance& Item,
		FPEPItemDisplayData& OutData);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Query")
	static bool ItemHasOwnedTag(
		const FPEPItemInstance& Item,
		FGameplayTag Tag);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Query")
	static bool ItemHasOwnedTagExact(
		const FPEPItemInstance& Item,
		FGameplayTag Tag);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Query")
	static bool ItemHasAnyOwnedTags(
		const FPEPItemInstance& Item,
		const FGameplayTagContainer& Tags);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Query")
	static bool ItemHasAllOwnedTags(
		const FPEPItemInstance& Item,
		const FGameplayTagContainer& Tags);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Query")
	static bool GetItemOwnedTags(
		const FPEPItemInstance& Item,
		FGameplayTagContainer& OutTags);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Query")
	static bool DoesItemMatchRequiredBlockedTags(
		const FPEPItemInstance& Item,
		const FGameplayTagContainer& RequiredTags,
		const FGameplayTagContainer& BlockedTags);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Query")
	static bool IsItemStackable(
		const FPEPItemInstance& Item);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Query")
	static bool DoesItemHaveDurability(
		const FPEPItemInstance& Item);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Query")
	static bool GetItemStackData(
		const FPEPItemInstance& Item,
		FPEPItemStackData& OutData);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Query")
	static bool GetItemDurabilityData(
		const FPEPItemInstance& Item,
		FPEPItemDurabilityData& OutData);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Query")
	static bool GetItemWorldPresentationData(
		const FPEPItemInstance& Item,
		FPEPItemWorldPresentationData& OutData);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Query")
	static bool GetItemResourceData(
		const FPEPItemInstance& Item,
		FPEPItemResourceData& OutData);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Query")
	static bool GetItemWeightData(
		const FPEPItemInstance& Item,
		FPEPItemWeightData& OutData);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Query")
	static bool IsItemDepleted(
		const FPEPItemInstance& Item);
};
