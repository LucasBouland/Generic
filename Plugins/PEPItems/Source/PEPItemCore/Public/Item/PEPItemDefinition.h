#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "PEPItemDefinition.generated.h"
struct FPEPItemFragment;
class UTexture2D;

/**
 * Item definitions declare capabilities through DefinitionFragments and identity through OwnedTags.
 * Domain plugins extend this definition through fragments, without adding dependencies back into
 * PEPItemCore.
 */
UCLASS(BlueprintType)
class PEPITEMCORE_API UPEPItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType ItemAssetType;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item")
	FText DisplayName;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item", meta=(MultiLine=true))
	FText Description;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item")
	TSoftObjectPtr<UTexture2D> ItemIcon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item", meta=(Categories="Item"))
	FGameplayTagContainer OwnedTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Advanced",
		meta=(AdvancedDisplay, BaseStruct="/Script/PEPItemCore.PEPItemFragment", ExcludeBaseStruct))
	TArray<FInstancedStruct> DefinitionFragments;
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	UFUNCTION(BlueprintPure, Category="PEP|Item")
	bool HasOwnedTag(FGameplayTag Tag) const;

	UFUNCTION(BlueprintPure, Category="PEP|Item")
	bool HasOwnedTagExact(FGameplayTag Tag) const;

	UFUNCTION(BlueprintPure, Category="PEP|Item")
	bool HasAnyOwnedTags(const FGameplayTagContainer& Tags) const;

	UFUNCTION(BlueprintPure, Category="PEP|Item")
	bool HasAllOwnedTags(const FGameplayTagContainer& Tags) const;

	const TArray<FInstancedStruct>& GetDefinitionFragments() const
	{
		return DefinitionFragments;
	}

	void BuildDefaultStateFragments(TArray<FInstancedStruct>& OutStates) const;

	template <typename TFragment>
	const TFragment* GetDefinitionFragment() const
	{
		static_assert(TIsDerivedFrom<TFragment, FPEPItemFragment>::IsDerived,
		              "TFragment must derive from FPEPItemFragment");
		for (const FInstancedStruct& Entry : DefinitionFragments)
		{
			if (Entry.GetScriptStruct() != TFragment::StaticStruct())
			{
				continue;
			}
			if (const TFragment* Typed = Entry.GetPtr<TFragment>())
			{
				return Typed;
			}
		}
		return nullptr;
	}

	template <typename TFragment>
	bool HasDefinitionFragment() const
	{
		return GetDefinitionFragment<TFragment>() != nullptr;
	}
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
	/** Runs the selected definition-level and fragment-level validation passes. */
	EDataValidationResult ValidateForStartup(
		FDataValidationContext& Context,
		bool bValidateDefinition,
		bool bValidateFragments) const;
#endif
};
