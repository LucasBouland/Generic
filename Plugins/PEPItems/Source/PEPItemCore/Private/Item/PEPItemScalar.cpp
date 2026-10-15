#include "Item/PEPItemScalar.h"

#include "Item/PEPItemScalarTypes.h"
#include "Core/PEPCoreTags.h"
#include "Item/PEPItemFragment.h"

namespace PEPItemScalarPrivate
{
	/*
	 * Les scalaires int et float partagent tout leur flux. Seuls deux points diffèrent :
	 * la fonction d'accès exposée par le fragment et le test « cette valeur est-elle exploitable ».
	 * Ils sont isolés dans les traits ci-dessous, le reste est écrit une fois.
	 *
	 * Les champs des accessors et des ValueData portent les mêmes noms dans les deux variantes,
	 * donc les fonctions génériques y accèdent directement.
	 */

	template <typename T>
	struct TScalarTraits;

	template <>
	struct TScalarTraits<int32>
	{
		using FAccessor = FPEPItemIntValueAccessor;
		using FValueData = FPEPItemIntValueData;

		static bool TryGetAccessor(
			const FPEPItemFragment& Fragment,
			const FGameplayTag ValueTag,
			FAccessor& OutAccessor)
		{
			return Fragment.TryGetIntValueAccessor(ValueTag, OutAccessor);
		}

		static bool IsUsable(int32)
		{
			return true;
		}
	};

	template <>
	struct TScalarTraits<float>
	{
		using FAccessor = FPEPItemFloatValueAccessor;
		using FValueData = FPEPItemFloatValueData;

		static bool TryGetAccessor(
			const FPEPItemFragment& Fragment,
			const FGameplayTag ValueTag,
			FAccessor& OutAccessor)
		{
			return Fragment.TryGetFloatValueAccessor(ValueTag, OutAccessor);
		}

		static bool IsUsable(const float Value)
		{
			return FMath::IsFinite(Value);
		}
	};

	template <typename T>
	bool ResolveAccessor(
		const UPEPItemDefinition* Definition,
		const FGameplayTag ValueTag,
		typename TScalarTraits<T>::FAccessor& OutAccessor)
	{
		using FTraits = TScalarTraits<T>;

		OutAccessor = typename FTraits::FAccessor();

		if (!Definition || !ValueTag.IsValid())
		{
			return false;
		}

		bool bFound = false;

		for (const FInstancedStruct& Entry :
		     Definition->GetDefinitionFragments())
		{
			const FPEPItemFragment* Fragment =
				Entry.GetPtr<FPEPItemFragment>();

			if (!Fragment)
			{
				continue;
			}

			typename FTraits::FAccessor Candidate;

			if (!FTraits::TryGetAccessor(
				*Fragment,
				ValueTag,
				Candidate))
			{
				continue;
			}

			// Un fragment qui annonce le tag sans rien offrir n'est pas un fournisseur :
			// l'ignorer avant le contrôle de doublon, sinon le résultat dépendrait de l'ordre des
			// fragments dans l'asset (placé après un vrai fournisseur, il le ferait échouer).
			if (!Candidate.bHasDefinitionValue
				&& !Candidate.HasRuntimeValue())
			{
				continue;
			}

			// Deux fournisseurs réels ne doivent pas fournir le même ValueTag.
			if (bFound)
			{
				OutAccessor = typename FTraits::FAccessor();
				return false;
			}

			OutAccessor = Candidate;
			bFound = true;
		}

		return bFound;
	}

	template <typename T>
	void FillMetadata(
		const FGameplayTag ValueTag,
		const typename TScalarTraits<T>::FAccessor& Accessor,
		typename TScalarTraits<T>::FValueData& OutData)
	{
		OutData.ValueTag = ValueTag;
		OutData.bHasDefinitionValue = Accessor.bHasDefinitionValue;
		OutData.DefinitionValue = Accessor.DefinitionValue;
		OutData.bHasMinValue = Accessor.bHasMinValue;
		OutData.MinValue = Accessor.MinValue;
		OutData.bHasMaxValue = Accessor.bHasMaxValue;
		OutData.MaxValue = Accessor.MaxValue;
	}

	template <typename T>
	bool GetValueFromDefinition(
		const UPEPItemDefinition* Definition,
		const FGameplayTag ValueTag,
		typename TScalarTraits<T>::FValueData& OutData)
	{
		using FTraits = TScalarTraits<T>;

		OutData = typename FTraits::FValueData();
		OutData.ValueTag = ValueTag;

		typename FTraits::FAccessor Accessor;

		if (!ResolveAccessor<T>(Definition, ValueTag, Accessor))
		{
			return false;
		}

		FillMetadata<T>(ValueTag, Accessor, OutData);

		if (!Accessor.bHasDefinitionValue
			|| !Accessor.IsValueAllowed(Accessor.DefinitionValue))
		{
			return false;
		}

		OutData.EffectiveValue = Accessor.DefinitionValue;
		OutData.bCanMutate = Accessor.CanMutate();
		OutData.bValid = true;
		return true;
	}

	/**
	 * Reads the item's value through an already resolved accessor. Shared by GetItemValue and
	 * ValidateSetValue so a write resolves the providing fragment once, not once per check.
	 */
	template <typename T>
	bool ReadItemValue(
		const FPEPItemInstance& Item,
		const FGameplayTag ValueTag,
		const typename TScalarTraits<T>::FAccessor& Accessor,
		typename TScalarTraits<T>::FValueData& OutData)
	{
		using FTraits = TScalarTraits<T>;

		OutData = typename FTraits::FValueData();
		FillMetadata<T>(ValueTag, Accessor, OutData);

		if (Accessor.ReadRuntimeValue)
		{
			T RuntimeValue{};

			if (Accessor.ReadRuntimeValue(Item, RuntimeValue))
			{
				OutData.bHasRuntimeValue = true;
				OutData.RuntimeValue = RuntimeValue;
			}
		}

		if (!OutData.bHasRuntimeValue
			&& !OutData.bHasDefinitionValue)
		{
			return false;
		}

		OutData.EffectiveValue =
			OutData.bHasRuntimeValue
				? OutData.RuntimeValue
				: OutData.DefinitionValue;

		if (!Accessor.IsValueAllowed(OutData.EffectiveValue))
		{
			return false;
		}

		OutData.bCanMutate = Accessor.CanMutate();
		OutData.bValid = true;
		return true;
	}

	template <typename T>
	bool GetItemValue(
		const FPEPItemInstance& Item,
		const FGameplayTag ValueTag,
		typename TScalarTraits<T>::FValueData& OutData)
	{
		using FTraits = TScalarTraits<T>;

		OutData = typename FTraits::FValueData();
		OutData.ValueTag = ValueTag;

		if (!Item.IsValid() || !ValueTag.IsValid())
		{
			return false;
		}

		typename FTraits::FAccessor Accessor;

		if (!ResolveAccessor<T>(
			Item.GetDefinitionIfLoaded(),
			ValueTag,
			Accessor))
		{
			return false;
		}

		return ReadItemValue<T>(Item, ValueTag, Accessor, OutData);
	}

	/**
	 * Front half shared by the rule and the mutation: everything that must hold before a write,
	 * up to but not including the write itself. Leaves the resolved accessor with the caller so
	 * the mutation does not have to look it up a second time.
	 */
	template <typename T>
	FPEPItemResult ValidateSetValue(
		const FPEPItemInstance& Item,
		const FGameplayTag ValueTag,
		const T NewValue,
		typename TScalarTraits<T>::FAccessor& OutAccessor)
	{
		using FTraits = TScalarTraits<T>;

		OutAccessor = typename FTraits::FAccessor();

		if (!Item.IsValid()
			|| !ValueTag.IsValid()
			|| !FTraits::IsUsable(NewValue))
		{
			return FPEPItemResult::Failure(
				PEPCoreTags::Reason_InvalidTarget,
				Item.ItemId);
		}

		if (!ResolveAccessor<T>(
			Item.GetDefinitionIfLoaded(),
			ValueTag,
			OutAccessor))
		{
			return FPEPItemResult::Failure(
				PEPCoreTags::Reason_BlockedByState,
				Item.ItemId);
		}

		typename FTraits::FValueData Data;

		if (!ReadItemValue<T>(Item, ValueTag, OutAccessor, Data))
		{
			return FPEPItemResult::Failure(
				PEPCoreTags::Reason_BlockedByState,
				Item.ItemId);
		}

		if (!Data.bCanMutate
			|| !Data.bHasRuntimeValue)
		{
			return FPEPItemResult::Failure(
				PEPCoreTags::Reason_BlockedByState,
				Item.ItemId);
		}

		if (!OutAccessor.IsValueAllowed(NewValue))
		{
			return FPEPItemResult::Failure(
				PEPCoreTags::Reason_InvalidTarget,
				Item.ItemId);
		}

		return FPEPItemResult::Success(
			PEPCoreTags::Reason_None,
			Item.ItemId);
	}

	template <typename T>
	FPEPItemResult CanSetValue(
		const FPEPItemInstance& Item,
		const FGameplayTag ValueTag,
		const T NewValue)
	{
		typename TScalarTraits<T>::FAccessor Accessor;
		return ValidateSetValue<T>(Item, ValueTag, NewValue, Accessor);
	}

	template <typename T>
	FPEPItemResult SetValue(
		FPEPItemInstance& Item,
		const FGameplayTag ValueTag,
		const T NewValue)
	{
		typename TScalarTraits<T>::FAccessor Accessor;

		const FPEPItemResult Result =
			ValidateSetValue<T>(Item, ValueTag, NewValue, Accessor);

		if (!Result.bSuccess)
		{
			return Result;
		}

		if (!Accessor.WriteRuntimeValue(Item, NewValue))
		{
			return FPEPItemResult::Failure(
				PEPCoreTags::Reason_BlockedByState,
				Item.ItemId);
		}

		return FPEPItemResult::Success(
			PEPCoreTags::Reason_None,
			Item.ItemId);
	}

	/**
	 * Everything CanConsumeIntValue checks, with a single accessor resolution: read the live value,
	 * take Amount off, then apply to the remainder exactly the checks ValidateSetValue would.
	 * Consumption is the hot path (ammunition, charges), so it does not re-resolve per step.
	 */
	FPEPItemResult ValidateConsumeIntValue(
		const FPEPItemInstance& Item,
		const FGameplayTag ValueTag,
		const int32 Amount,
		int32& OutRemaining,
		FPEPItemIntValueAccessor& OutAccessor)
	{
		OutRemaining = 0;
		OutAccessor = FPEPItemIntValueAccessor();

		if (Amount <= 0)
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, Item.ItemId);
		}

		FPEPItemIntValueData Data;
		if (!Item.IsValid()
			|| !ValueTag.IsValid()
			|| !ResolveAccessor<int32>(Item.GetDefinitionIfLoaded(), ValueTag, OutAccessor)
			|| !ReadItemValue<int32>(Item, ValueTag, OutAccessor, Data)
			|| !Data.bHasRuntimeValue)
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, Item.ItemId);
		}

		if (Amount > Data.RuntimeValue)
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, Item.ItemId);
		}

		if (!Data.bCanMutate)
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, Item.ItemId);
		}

		const int32 Remaining = Data.RuntimeValue - Amount;
		if (!OutAccessor.IsValueAllowed(Remaining))
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_InvalidTarget, Item.ItemId);
		}

		OutRemaining = Remaining;
		return FPEPItemResult::Success(PEPCoreTags::Reason_None, Item.ItemId);
	}
}

namespace PEPItemScalarQuery
{
	bool GetIntValueFromDefinition(
		const UPEPItemDefinition* Definition,
		const FGameplayTag ValueTag,
		FPEPItemIntValueData& OutData)
	{
		return PEPItemScalarPrivate::GetValueFromDefinition<int32>(
			Definition,
			ValueTag,
			OutData);
	}

	bool GetFloatValueFromDefinition(
		const UPEPItemDefinition* Definition,
		const FGameplayTag ValueTag,
		FPEPItemFloatValueData& OutData)
	{
		return PEPItemScalarPrivate::GetValueFromDefinition<float>(
			Definition,
			ValueTag,
			OutData);
	}

	bool GetItemIntValue(
		const FPEPItemInstance& Item,
		const FGameplayTag ValueTag,
		FPEPItemIntValueData& OutData)
	{
		return PEPItemScalarPrivate::GetItemValue<int32>(
			Item,
			ValueTag,
			OutData);
	}

	bool GetItemFloatValue(
		const FPEPItemInstance& Item,
		const FGameplayTag ValueTag,
		FPEPItemFloatValueData& OutData)
	{
		return PEPItemScalarPrivate::GetItemValue<float>(
			Item,
			ValueTag,
			OutData);
	}
}

namespace PEPItemScalarRules
{
	FPEPItemResult CanSetIntValue(
		const FPEPItemInstance& Item,
		const FGameplayTag ValueTag,
		const int32 NewValue)
	{
		return PEPItemScalarPrivate::CanSetValue<int32>(Item, ValueTag, NewValue);
	}

	FPEPItemResult CanSetFloatValue(
		const FPEPItemInstance& Item,
		const FGameplayTag ValueTag,
		const float NewValue)
	{
		return PEPItemScalarPrivate::CanSetValue<float>(Item, ValueTag, NewValue);
	}

	FPEPItemResult CanConsumeIntValue(
		const FPEPItemInstance& Item,
		const FGameplayTag ValueTag,
		const int32 Amount,
		int32& OutRemaining)
	{
		FPEPItemIntValueAccessor Accessor;
		return PEPItemScalarPrivate::ValidateConsumeIntValue(Item, ValueTag, Amount, OutRemaining, Accessor);
	}
}

namespace PEPItemScalarMutation
{
	FPEPItemResult SetIntValue(
		FPEPItemInstance& Item,
		const FGameplayTag ValueTag,
		const int32 NewValue)
	{
		return PEPItemScalarPrivate::SetValue<int32>(Item, ValueTag, NewValue);
	}

	FPEPItemResult SetFloatValue(
		FPEPItemInstance& Item,
		const FGameplayTag ValueTag,
		const float NewValue)
	{
		return PEPItemScalarPrivate::SetValue<float>(Item, ValueTag, NewValue);
	}

	FPEPItemResult ConsumeIntValue(
		FPEPItemInstance& Item,
		const FGameplayTag ValueTag,
		const int32 Amount,
		int32& OutRemaining)
	{
		OutRemaining = 0;

		// The rule's accessor is reused for the write: one resolution for the whole operation.
		int32 Remaining = 0;
		FPEPItemIntValueAccessor Accessor;
		const FPEPItemResult RuleResult = PEPItemScalarPrivate::ValidateConsumeIntValue(
			Item, ValueTag, Amount, Remaining, Accessor);
		if (!RuleResult.bSuccess)
		{
			return RuleResult;
		}

		if (!Accessor.WriteRuntimeValue(Item, Remaining))
		{
			return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, Item.ItemId);
		}

		OutRemaining = Remaining;
		return FPEPItemResult::Success(PEPCoreTags::Reason_None, Item.ItemId);
	}
}

bool UPEPItemScalarLibrary::GetDefinitionIntValue(
	const UPEPItemDefinition* Definition,
	const FGameplayTag ValueTag,
	FPEPItemIntValueData& OutData)
{
	return PEPItemScalarQuery::GetIntValueFromDefinition(
		Definition,
		ValueTag,
		OutData);
}

bool UPEPItemScalarLibrary::GetDefinitionFloatValue(
	const UPEPItemDefinition* Definition,
	const FGameplayTag ValueTag,
	FPEPItemFloatValueData& OutData)
{
	return PEPItemScalarQuery::GetFloatValueFromDefinition(
		Definition,
		ValueTag,
		OutData);
}

bool UPEPItemScalarLibrary::GetItemIntValue(
	const FPEPItemInstance& Item,
	const FGameplayTag ValueTag,
	FPEPItemIntValueData& OutData)
{
	return PEPItemScalarQuery::GetItemIntValue(
		Item,
		ValueTag,
		OutData);
}

bool UPEPItemScalarLibrary::GetItemFloatValue(
	const FPEPItemInstance& Item,
	const FGameplayTag ValueTag,
	FPEPItemFloatValueData& OutData)
{
	return PEPItemScalarQuery::GetItemFloatValue(
		Item,
		ValueTag,
		OutData);
}

FPEPItemResult UPEPItemScalarLibrary::CanSetItemIntValue(
	const FPEPItemInstance& Item,
	const FGameplayTag ValueTag,
	const int32 NewValue)
{
	return PEPItemScalarRules::CanSetIntValue(Item, ValueTag, NewValue);
}

FPEPItemResult UPEPItemScalarLibrary::CanSetItemFloatValue(
	const FPEPItemInstance& Item,
	const FGameplayTag ValueTag,
	const float NewValue)
{
	return PEPItemScalarRules::CanSetFloatValue(Item, ValueTag, NewValue);
}
