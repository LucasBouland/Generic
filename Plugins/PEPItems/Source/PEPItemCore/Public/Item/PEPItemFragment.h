#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "Item/PEPItemStateFragment.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "PEPItemFragment.generated.h"

struct FPEPItemFloatValueAccessor;
struct FPEPItemIntValueAccessor;
/**
 * Static item capability data owned by a UPEPItemDefinition.
 * Fragments may create default runtime state, but must not mutate item instances directly.
 * Queries read fragments. Rules validate operations. Mutation helpers apply validated state changes.
 */
USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemFragment
{
	GENERATED_BODY()

public:
	virtual ~FPEPItemFragment() = default;
	virtual void BuildDefaultStates(TArray<FInstancedStruct>& OutStates) const;
	virtual void GetOwnedStateStructs(TArray<const UScriptStruct*>& OutStateStructs) const;
	
	virtual bool TryGetIntValueAccessor(
	FGameplayTag ValueTag,
	FPEPItemIntValueAccessor& OutAccessor) const;

	virtual bool TryGetFloatValueAccessor(
		FGameplayTag ValueTag,
		FPEPItemFloatValueAccessor& OutAccessor) const;
	
#if WITH_EDITOR
	virtual EDataValidationResult ValidateFragmentData(
		FDataValidationContext& Context,
		int32 FragmentIndex) const;
#endif

protected:
	template <typename TState>
	TState* FindOrAddDefaultState(TArray<FInstancedStruct>& OutStates) const
	{
		static_assert(
			TIsDerivedFrom<TState, FPEPItemStateFragment>::IsDerived,
			"TState must derive from FPEPItemStateFragment");
		for (FInstancedStruct& Entry : OutStates)
		{
			if (Entry.GetScriptStruct() == TState::StaticStruct())
			{
				return Entry.GetMutablePtr<TState>();
			}
		}
		FInstancedStruct& NewEntry = OutStates.AddDefaulted_GetRef();
		NewEntry.InitializeAs<TState>();
		return NewEntry.GetMutablePtr<TState>();
	}
};

/**
 * A named, reusable set of pre-configured DefinitionFragments — a starter kit for a common item
 * archetype ("Wearable Container", "Stackable Consumable") so building another item of that
 * shape means picking one bundle instead of adding and configuring each fragment by hand.
 * Authored in UPEPItemSettings::FragmentBundles.
 *
 * Applying a bundle only adds fragments the target does not already carry — one whose type (or
 * inheritance family) conflicts with something already present is skipped rather than replacing
 * configured data, the same non-destructive spirit as the container slot presets.
 */
USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemFragmentBundle
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="PEP|Item")
	FName BundleName;

	UPROPERTY(EditAnywhere, Category="PEP|Item",
		meta=(BaseStruct="/Script/PEPItemCore.PEPItemFragment", ExcludeBaseStruct))
	TArray<FInstancedStruct> Fragments;
};
