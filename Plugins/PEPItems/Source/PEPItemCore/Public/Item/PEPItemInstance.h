#pragma once

#include "CoreMinimal.h"
#include "Core/PEPTypes.h"
#include "Engine/AssetManagerTypes.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemFragment.h"
#include "StructUtils/InstancedStruct.h"
#include "PEPItemInstance.generated.h"

struct FPEPItemMutableAccess;
struct FPEPItemInstance;

#if WITH_DEV_AUTOMATION_TESTS
namespace PEPItemAutomationTestSupport
{
	PEPITEMCORE_API FPEPItemInstance MakeItemForTests(const UPEPItemDefinition* Definition);
	PEPITEMCORE_API FPEPItemId MakeItemIdForTests();
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
namespace PEPItemAutomationTestSupport
{
	/**
	 * Registers a transient in-memory item definition for automation tests.
	 * This is only for tests that create definitions with GetTransientPackage().
	 * Runtime/game code should use real primary assets resolved by UAssetManager.
	 */
	PEPITEMCORE_API void RegisterTransientDefinitionForTests(const UPEPItemDefinition* Definition);
	PEPITEMCORE_API void UnregisterTransientDefinitionForTests(const FPrimaryAssetId& DefinitionId);
	PEPITEMCORE_API void ClearTransientDefinitionsForTests();
	PEPITEMCORE_API const UPEPItemDefinition*
	FindTransientDefinitionForTests(const FPrimaryAssetId& DefinitionId);
}
#endif

USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemInstanceValidationResult
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Validation")
	bool bValid = true;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Validation")
	TArray<FText> Errors;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Item|Validation")
	TArray<FText> Warnings;

	void AddError(const FText& Error)
	{
		bValid = false;
		Errors.Add(Error);
	}

	void AddWarning(const FText& Warning)
	{
		Warnings.Add(Warning);
	}
};

/**
 * Runtime item record. This is data only; it does not own gameplay authority.
 * Read through PEPItemQuery. Write through PEPItemMutation (or PEPItemScalarMutation for tagged values).
 */
USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemInstance
{
	GENERATED_BODY()

	friend struct FPEPItemMutableAccess;
	friend class UPEPItemInstanceLibrary;
	friend struct FPEPItemMutationAccess;

#if WITH_DEV_AUTOMATION_TESTS
	friend FPEPItemInstance PEPItemAutomationTestSupport::MakeItemForTests(
		const UPEPItemDefinition* Definition);
#endif

public:
	FPEPItemInstance();
	~FPEPItemInstance();
	FPEPItemInstance(const FPEPItemInstance& Other);
	FPEPItemInstance(FPEPItemInstance&& Other);
	FPEPItemInstance& operator=(const FPEPItemInstance& Other);
	FPEPItemInstance& operator=(FPEPItemInstance&& Other);

	/**
	 * Layout of a record that carries no version: one written before SchemaVersion was persisted, or
	 * one a delta archive skipped because it matched the old default. Both hold version 1 data.
	 * A record still at this value has not been migrated, and ValidateInstance refuses it.
	 */
	static constexpr int32 UnversionedSchemaVersion = 0;
	static constexpr int32 CurrentSchemaVersion = 1;

	/**
	 * Defaults to UnversionedSchemaVersion so that an archive missing the field reads as legacy data
	 * rather than being silently labelled current, and so that every real record differs from the
	 * default and is always written. Creation paths (MakeNew, InitializeNew) stamp
	 * CurrentSchemaVersion; archive loads upgrade older records in PostSerialize.
	 */
	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item")
	int32 SchemaVersion = UnversionedSchemaVersion;

	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item")
	FPEPItemId ItemId;

	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item")
	FPrimaryAssetId DefinitionId;

	bool IsValid() const;

	/**
	 * True when any runtime state reports itself empty (stack at zero, resource at zero).
	 * A depleted item is still structurally valid; the owner is expected to destroy it.
	 */
	bool IsDepleted() const;

	bool HasDefinition() const
	{
		return DefinitionId.IsValid();
	}

	/** Returns the record to its default-constructed, unversioned and invalid state. */
	void Reset();
	/**
	 * Returns the definition only if it is already resident. Gameplay code must rely on definitions
	 * being preloaded by the asset manager; a null result means "not loaded", not "does not exist".
	 * Editor and setup code that genuinely needs the asset resolved calls
	 * LoadDefinitionSynchronous_EditorOrSetupOnly() explicitly, so the stall is visible at the call site.
	 */
	const UPEPItemDefinition* GetDefinitionIfLoaded() const;
	const UPEPItemDefinition* LoadDefinitionSynchronous_EditorOrSetupOnly() const;
	const TArray<FInstancedStruct>& GetStateFragments() const
	{
		return StateFragments;
	}

	const FPEPItemStateFragment* FindStateByStruct(const UScriptStruct* StateStruct) const;

	void NormalizeStateFragments();

	/**
	 * Structural and range validation against the loaded definition. Rejects an unversioned record
	 * (migrate it first) and one written by a newer build.
	 */
	bool ValidateInstance(FPEPItemInstanceValidationResult& OutResult) const;

	/**
	 * Upgrades a record read with LoadedSchemaVersion to CurrentSchemaVersion.
	 * Accepts UnversionedSchemaVersion as legacy version 1 data. Returns false, leaving the record
	 * untouched, for a negative version or one newer than this build.
	 */
	bool MigrateInstanceFromVersion(int32 LoadedSchemaVersion);

	/**
	 * Explicit, project-driven repair of a loaded record: migrates an older schema, restores missing
	 * owned states and clamps core values into the ranges ValidateInstance accepts.
	 * @return the validation result after repair.
	 */
	bool RepairLoadedInstance();

	/**
	 * On archive loads: upgrades records written by an older schema and reports the loaded ItemId to
	 * the allocator so it is never handed out again. See TStructOpsTypeTraits below.
	 */
	void PostSerialize(const FArchive& Ar);

	template <typename TState>
	bool HasState() const
	{
		return GetState<TState>() != nullptr;
	}

	template <typename TState>
	const TState* GetState() const
	{
		static_assert(TIsDerivedFrom<TState, FPEPItemStateFragment>::IsDerived,
		              "TState must derive from FPEPItemStateFragment");
		for (const FInstancedStruct& Entry : StateFragments)
		{
			if (Entry.GetScriptStruct() == TState::StaticStruct())
			{
				return Entry.GetPtr<TState>();
			}
		}
		return nullptr;
	}

	template <typename TFragment>
	const TFragment* GetDefinitionFragment() const
	{
		static_assert(TIsDerivedFrom<TFragment, FPEPItemFragment>::IsDerived,
		              "TFragment must derive from FPEPItemFragment");
		const UPEPItemDefinition* Definition = GetDefinitionIfLoaded();
		return Definition ? Definition->GetDefinitionFragment<TFragment>() : nullptr;
	}

private:
	/** Low-level construction is reserved for the authority-aware library, trusted PEP internals and tests. */
	static FPEPItemInstance MakeNew(const UPEPItemDefinition* Definition);
	FPEPItemInstance DuplicateWithNewId() const;
	bool TryDuplicateWithNewId(FPEPItemInstance& OutItem) const;
	void InitializeNew(const UPEPItemDefinition* Definition);

	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item",
		meta=(AllowPrivateAccess="true", BaseStruct="/Script/PEPItemCore.PEPItemStateFragment", ExcludeBaseStruct))
	TArray<FInstancedStruct> StateFragments;

	/*
	 * Last definition resolved for DefinitionId. Every fragment query goes through
	 * GetDefinitionIfLoaded, and the asset manager answers it with two hash lookups plus a soft
	 * pointer resolve; placement asks it once per candidate cell per occupant. A weak pointer turns
	 * that into a serial-number check. Not reflected: it is per-copy and never crosses the wire,
	 * and the key keeps it honest when DefinitionId is reassigned.
	 */
	mutable TWeakObjectPtr<const UPEPItemDefinition> CachedDefinition;
	mutable FPrimaryAssetId CachedDefinitionId;

	template <typename TState>
	TState* Internal_GetMutableState()
	{
		static_assert(TIsDerivedFrom<TState, FPEPItemStateFragment>::IsDerived,
		              "TState must derive from FPEPItemStateFragment");
		for (FInstancedStruct& Entry : StateFragments)
		{
			if (Entry.GetScriptStruct() == TState::StaticStruct())
			{
				return Entry.GetMutablePtr<TState>();
			}
		}
		return nullptr;
	}

	template <typename TState>
	TState& Internal_FindOrAddState()
	{
		static_assert(TIsDerivedFrom<TState, FPEPItemStateFragment>::IsDerived,
		              "TState must derive from FPEPItemStateFragment");
		if (TState* Existing = Internal_GetMutableState<TState>())
		{
			return *Existing;
		}
		FInstancedStruct& NewEntry = StateFragments.AddDefaulted_GetRef();
		NewEntry.InitializeAs(TState::StaticStruct());
		return *NewEntry.GetMutablePtr<TState>();
	}
};

template <>
struct TStructOpsTypeTraits<FPEPItemInstance> : public TStructOpsTypeTraitsBase2<FPEPItemInstance>
{
	enum
	{
		WithPostSerialize = true
	};
};
