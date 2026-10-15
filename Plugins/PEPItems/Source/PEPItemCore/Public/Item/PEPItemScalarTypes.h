#pragma once

#include "CoreMinimal.h"
#include "Core/PEPTypes.h"
#include "Engine/AssetManagerTypes.h"
#include "GameplayTagContainer.h"

#include "PEPItemScalarTypes.generated.h"

struct FPEPItemInstance;

/*
 * Résultats publics de query.
 */

USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemIntValueData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar",
		meta=(Categories="Item.Value"))
	FGameplayTag ValueTag;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	bool bHasDefinitionValue = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	int32 DefinitionValue = 0;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	bool bHasRuntimeValue = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	int32 RuntimeValue = 0;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	int32 EffectiveValue = 0;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	bool bHasMinValue = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	int32 MinValue = 0;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	bool bHasMaxValue = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	int32 MaxValue = 0;

	/** Le fragment sait écrire cette valeur. Orthogonal à bHasRuntimeValue, qui dit si l'instance la porte. */
	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	bool bCanMutate = false;
};

USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemFloatValueData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar",
		meta=(Categories="Item.Value"))
	FGameplayTag ValueTag;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	bool bHasDefinitionValue = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	float DefinitionValue = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	bool bHasRuntimeValue = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	float RuntimeValue = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	float EffectiveValue = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	bool bHasMinValue = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	float MinValue = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	bool bHasMaxValue = false;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	float MaxValue = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Scalar")
	bool bCanMutate = false;
};

/*
 * Accessors internes fournis par les fragments de définition.
 *
 * Ils ne sont ni USTRUCT ni répliqués.
 * Ils sont temporaires et contiennent seulement des métadonnées
 * et des pointeurs vers des fonctions statiques.
 */

using FPEPItemIntValueReader =
	bool (*)(const FPEPItemInstance& Item, int32& OutValue);

using FPEPItemIntValueWriter =
	bool (*)(FPEPItemInstance& Item, int32 NewValue);

using FPEPItemFloatValueReader =
	bool (*)(const FPEPItemInstance& Item, float& OutValue);

using FPEPItemFloatValueWriter =
	bool (*)(FPEPItemInstance& Item, float NewValue);

struct PEPITEMCORE_API FPEPItemIntValueAccessor
{
	bool bHasDefinitionValue = false;
	int32 DefinitionValue = 0;

	bool bHasMinValue = false;
	int32 MinValue = 0;

	bool bHasMaxValue = false;
	int32 MaxValue = 0;

	FPEPItemIntValueReader ReadRuntimeValue = nullptr;
	FPEPItemIntValueWriter WriteRuntimeValue = nullptr;

	bool HasRuntimeValue() const
	{
		return ReadRuntimeValue != nullptr;
	}

	bool CanMutate() const
	{
		return ReadRuntimeValue != nullptr
			&& WriteRuntimeValue != nullptr;
	}

	bool IsValueAllowed(const int32 Value) const
	{
		if (bHasMinValue && Value < MinValue)
		{
			return false;
		}

		if (bHasMaxValue && Value > MaxValue)
		{
			return false;
		}

		return true;
	}
};

struct PEPITEMCORE_API FPEPItemFloatValueAccessor
{
	bool bHasDefinitionValue = false;
	float DefinitionValue = 0.0f;

	bool bHasMinValue = false;
	float MinValue = 0.0f;

	bool bHasMaxValue = false;
	float MaxValue = 0.0f;

	FPEPItemFloatValueReader ReadRuntimeValue = nullptr;
	FPEPItemFloatValueWriter WriteRuntimeValue = nullptr;

	bool HasRuntimeValue() const
	{
		return ReadRuntimeValue != nullptr;
	}

	bool CanMutate() const
	{
		return ReadRuntimeValue != nullptr
			&& WriteRuntimeValue != nullptr;
	}

	bool IsValueAllowed(const float Value) const
	{
		if (!FMath::IsFinite(Value))
		{
			return false;
		}

		if (bHasMinValue && Value < MinValue)
		{
			return false;
		}

		if (bHasMaxValue && Value > MaxValue)
		{
			return false;
		}

		return true;
	}
};
