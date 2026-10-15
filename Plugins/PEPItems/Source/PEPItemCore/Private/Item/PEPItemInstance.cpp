#include "Item/PEPItemInstance.h"
#include "Item/Fragments/PEPItemFragment_Durability.h"
#include "Item/Fragments/PEPItemFragment_Resource.h"
#include "Item/Fragments/PEPItemFragment_Stackable.h"
#include "Item/Internal/PEPItemMutableAccess.h"
#include "Item/PEPItemSettings.h"
#include "Item/PEPItemLogChannels.h"
#include "Item/PEPIdAllocatorSubsystem.h"
#include "Engine/AssetManager.h"
#include "Core/PEPTypes.h"


FPEPItemInstance::FPEPItemInstance() = default;

FPEPItemInstance::~FPEPItemInstance() = default;

FPEPItemInstance::FPEPItemInstance(const FPEPItemInstance& Other) = default;

FPEPItemInstance::FPEPItemInstance(FPEPItemInstance&& Other) = default;

FPEPItemInstance& FPEPItemInstance::operator=(const FPEPItemInstance& Other) = default;

FPEPItemInstance& FPEPItemInstance::operator=(FPEPItemInstance&& Other) = default;

bool FPEPItemInstance::IsValid() const
{
	return ItemId.IsValid() && DefinitionId.IsValid();
}

bool FPEPItemInstance::IsDepleted() const
{
	for (const FInstancedStruct& Entry : StateFragments)
	{
		const FPEPItemStateFragment* State = Entry.GetPtr<FPEPItemStateFragment>();
		if (State && State->IsDepleted())
		{
			return true;
		}
	}
	return false;
}

#if WITH_DEV_AUTOMATION_TESTS
namespace PEPItemAutomationTestSupport
{
	static TMap<FPrimaryAssetId, TWeakObjectPtr<const UPEPItemDefinition>> GTransientDefinitions;
	static int32 NextTestItemId = -2;

	FPEPItemInstance MakeItemForTests(const UPEPItemDefinition* Definition)
	{
		return FPEPItemInstance::MakeNew(Definition);
	}

	FPEPItemId MakeItemIdForTests()
	{
		FPEPItemId Id;
		Id.Value = NextTestItemId--;
		return Id;
	}

	void RegisterTransientDefinitionForTests(const UPEPItemDefinition* Definition)
	{
		if (!Definition)
		{
			return;
		}

		const FPrimaryAssetId DefinitionId = Definition->GetPrimaryAssetId();
		if (DefinitionId.IsValid())
		{
			GTransientDefinitions.Add(DefinitionId, Definition);
		}
	}

	void UnregisterTransientDefinitionForTests(const FPrimaryAssetId& DefinitionId)
	{
		if (DefinitionId.IsValid())
		{
			GTransientDefinitions.Remove(DefinitionId);
		}
	}

	void ClearTransientDefinitionsForTests()
	{
		GTransientDefinitions.Reset();
	}

	const UPEPItemDefinition* FindTransientDefinitionForTests(const FPrimaryAssetId& DefinitionId)
	{
		if (const TWeakObjectPtr<const UPEPItemDefinition>* Found = GTransientDefinitions.Find(DefinitionId))
		{
			return Found->Get();
		}
		return nullptr;
	}
}
#endif

FPEPItemInstance FPEPItemInstance::MakeNew(const UPEPItemDefinition* Definition)
{
	FPEPItemInstance Item;
	Item.InitializeNew(Definition);
	return Item;
}

FPEPItemInstance FPEPItemInstance::DuplicateWithNewId() const
{
	FPEPItemInstance Duplicate;
	if (!IsValid())
	{
		return Duplicate;
	}
	Duplicate = *this;
	Duplicate.ItemId.Value = UPEPIdAllocatorSubsystem::AllocateNext();
	Duplicate.NormalizeStateFragments();
	return Duplicate;
}

bool FPEPItemInstance::TryDuplicateWithNewId(FPEPItemInstance& OutItem) const
{
	OutItem = DuplicateWithNewId();
	return OutItem.IsValid();
}

void FPEPItemInstance::Reset()
{
	SchemaVersion = CurrentSchemaVersion;
	ItemId.Reset();
	DefinitionId = FPrimaryAssetId();
	StateFragments.Reset();
}

void FPEPItemInstance::InitializeNew(const UPEPItemDefinition* Definition)
{
	Reset();
	if (!Definition)
	{
		return;
	}
	ItemId.Value = UPEPIdAllocatorSubsystem::AllocateNext();
	DefinitionId = Definition->GetPrimaryAssetId();
	Definition->BuildDefaultStateFragments(StateFragments);
	NormalizeStateFragments();
}

const UPEPItemDefinition* FPEPItemInstance::GetDefinitionIfLoaded() const
{
	if (!DefinitionId.IsValid())
	{
		return nullptr;
	}
	if (CachedDefinitionId == DefinitionId)
	{
		if (const UPEPItemDefinition* Cached = CachedDefinition.Get())
		{
			return Cached;
		}
	}

	const UPEPItemDefinition* Definition = nullptr;
#if WITH_DEV_AUTOMATION_TESTS
	Definition = PEPItemAutomationTestSupport::FindTransientDefinitionForTests(DefinitionId);
#endif
	if (!Definition)
	{
		UAssetManager& AssetManager = UAssetManager::Get();
		Definition = Cast<UPEPItemDefinition>(AssetManager.GetPrimaryAssetObject(DefinitionId));
	}
	/* Only a hit is cached: a miss means "not loaded yet", which the next call must re-check. */
	if (Definition)
	{
		CachedDefinition = Definition;
		CachedDefinitionId = DefinitionId;
	}
	return Definition;
}

const UPEPItemDefinition* FPEPItemInstance::LoadDefinitionSynchronous_EditorOrSetupOnly() const
{
	if (!DefinitionId.IsValid())
	{
		return nullptr;
	}
	const UPEPItemSettings& Settings = PEPItemSettings();
	if (!Settings.bAllowSynchronousDefinitionLoad)
	{
		return nullptr;
	}

	ensureMsgf(!GWorld || !GWorld->IsGameWorld(),
	           TEXT("LoadDefinitionSynchronous_EditorOrSetupOnly called during gameplay for item '%s'. "
		           "This function is intended for editor and setup use only."),
	           *DefinitionId.ToString());

	if (Settings.bWarnOnSynchronousDefinitionLoad)
	{
		UE_LOG(LogPEPItems, Warning,
		       TEXT("PEP item definition %s was synchronously loaded."),
		       *DefinitionId.ToString());
	}
	UAssetManager& AssetManager = UAssetManager::Get();
	const FSoftObjectPath AssetPath = AssetManager.GetPrimaryAssetPath(DefinitionId);
	if (!AssetPath.IsValid())
	{
		return nullptr;
	}
	return Cast<UPEPItemDefinition>(AssetPath.TryLoad());
}

const FPEPItemStateFragment* FPEPItemInstance::FindStateByStruct(const UScriptStruct* StateStruct) const
{
	if (!StateStruct)
	{
		return nullptr;
	}
	for (const FInstancedStruct& Entry : StateFragments)
	{
		if (Entry.GetScriptStruct() != StateStruct)
		{
			continue;
		}
		if (const FPEPItemStateFragment* State = Entry.GetPtr<FPEPItemStateFragment>())
		{
			return State;
		}
	}
	return nullptr;
}

namespace
{
	bool AreStructsInSameFamily(const UScriptStruct* A, const UScriptStruct* B)
	{
		return A && B && (A == B || A->IsChildOf(B) || B->IsChildOf(A));
	}

	void RepairCoreStateValues(FPEPItemInstance& Item, const UPEPItemDefinition* Definition)
	{
		if (!Definition)
		{
			return;
		}
		if (const FPEPItemFragment_Stackable* StackFragment = Definition->GetDefinitionFragment<
			FPEPItemFragment_Stackable>())
		{
			if (FPEPItemState_Stack* StackState = FPEPItemMutableAccess::GetMutableState<
				FPEPItemState_Stack>(Item))
			{
				// Zero is legal: it means depleted, and the owner destroys the item.
				const int32 MaxStackSize = FMath::Max(1, StackFragment->GetEffectiveMaxStackSize());
				StackState->Count = FMath::Clamp(StackState->Count, 0, MaxStackSize);
			}
		}
		if (Definition->GetDefinitionFragment<FPEPItemFragment_Durability>())
		{
			if (FPEPItemState_Durability* DurabilityState = FPEPItemMutableAccess::GetMutableState<
				FPEPItemState_Durability>(Item))
			{
				if (!FMath::IsFinite(DurabilityState->Durability))
				{
					const FPEPItemFragment_Durability* DurabilityFragment = Definition->GetDefinitionFragment<
						FPEPItemFragment_Durability>();
					const float StartingDurability = DurabilityFragment
						? PEPItemSettings().GetEffectiveStartingDurability(
							DurabilityFragment->StartingDurability,
							DurabilityFragment->bUseProjectDefaultStartingDurability)
						: 1.0f;
					DurabilityState->Durability = FMath::IsFinite(StartingDurability)
						? FMath::Clamp(StartingDurability, 0.0f, 1.0f)
						: 1.0f;
				}
				else
				{
					DurabilityState->Durability = FMath::Clamp(DurabilityState->Durability, 0.0f, 1.0f);
				}
			}
		}
		if (const FPEPItemFragment_Resource* ResourceFragment = Definition->GetDefinitionFragment<
			FPEPItemFragment_Resource>())
		{
			if (FPEPItemState_Resource* ResourceState = FPEPItemMutableAccess::GetMutableState<
				FPEPItemState_Resource>(Item))
			{
				const int32 MaxResource = FMath::Max(1, ResourceFragment->MaxResource);
				ResourceState->Current = FMath::Clamp(ResourceState->Current, 0, MaxResource);
			}
		}
	}
}

void FPEPItemInstance::NormalizeStateFragments()
{
	TArray<const UScriptStruct*> SeenStructs;
	StateFragments.RemoveAll(
		[&SeenStructs](const FInstancedStruct& Entry)
		{
			const UScriptStruct* StructType = Entry.GetScriptStruct();
			if (!StructType || !StructType->IsChildOf(FPEPItemStateFragment::StaticStruct()))
			{
				return true;
			}
			for (const UScriptStruct* SeenStruct : SeenStructs)
			{
				if (AreStructsInSameFamily(SeenStruct, StructType))
				{
					UE_LOG(LogPEPItems, Warning,
					       TEXT(
						       "NormalizeStateFragments: dropping duplicate state family '%s' (conflicts with '%s'). Check fragment setup on this definition."
					       ),
					       *StructType->GetName(), *SeenStruct->GetName());
					return true;
				}
			}
			SeenStructs.Add(StructType);
			return false;
		});
}

bool FPEPItemInstance::ValidateInstance(
	FPEPItemInstanceValidationResult& OutResult) const
{
	OutResult = FPEPItemInstanceValidationResult();
	if (SchemaVersion <= 0 || SchemaVersion > CurrentSchemaVersion)
	{
		OutResult.AddError(FText::Format(
			NSLOCTEXT("PEPItemValidation", "InvalidSchemaVersion", "Invalid item schema version: {0}."),
			FText::AsNumber(SchemaVersion)));
	}
	if (!ItemId.IsValid())
	{
		OutResult.AddError(NSLOCTEXT("PEPItemValidation", "InvalidItemId", "ItemId is invalid."));
	}
	if (!DefinitionId.IsValid())
	{
		OutResult.AddError(NSLOCTEXT("PEPItemValidation", "InvalidDefinitionId", "DefinitionId is invalid."));
	}

	const UPEPItemDefinition* Definition = GetDefinitionIfLoaded();
	if (!Definition)
	{
		OutResult.AddError(NSLOCTEXT("PEPItemValidation", "DefinitionNotLoaded",
		                             "Item definition is missing or not loaded."));
	}

	TArray<const UScriptStruct*> SeenStateTypes;
	for (const FInstancedStruct& Entry : StateFragments)
	{
		const UScriptStruct* StateStruct = Entry.GetScriptStruct();
		if (!StateStruct || !StateStruct->IsChildOf(FPEPItemStateFragment::StaticStruct()))
		{
			OutResult.AddError(NSLOCTEXT("PEPItemValidation", "InvalidStateType",
			                             "A state entry does not derive from FPEPItemStateFragment."));
			continue;
		}
		for (const UScriptStruct* SeenStateType : SeenStateTypes)
		{
			if (AreStructsInSameFamily(SeenStateType, StateStruct))
			{
				OutResult.AddError(FText::Format(
					NSLOCTEXT("PEPItemValidation", "DuplicateStateFamily",
					          "Duplicate/conflicting state fragment inheritance family: {0}."),
					FText::FromString(StateStruct->GetName())));
				break;
			}
		}
		SeenStateTypes.Add(StateStruct);
	}

	if (Definition)
	{
		TArray<const UScriptStruct*> OwnedStateTypes;
		for (const FInstancedStruct& FragmentEntry : Definition->GetDefinitionFragments())
		{
			const FPEPItemFragment* Fragment = FragmentEntry.GetPtr<FPEPItemFragment>();
			if (!Fragment)
			{
				continue;
			}
			TArray<const UScriptStruct*> FragmentOwnedStates;
			Fragment->GetOwnedStateStructs(FragmentOwnedStates);
			for (const UScriptStruct* OwnedState : FragmentOwnedStates)
			{
				if (!OwnedState)
				{
					continue;
				}
				OwnedStateTypes.Add(OwnedState);
				if (!FindStateByStruct(OwnedState))
				{
					OutResult.AddError(FText::Format(
						NSLOCTEXT("PEPItemValidation", "MissingOwnedState",
						          "Missing state fragment required by definition: {0}."),
						FText::FromString(OwnedState->GetName())));
				}
			}
		}

		for (const UScriptStruct* StateType : SeenStateTypes)
		{
			bool bOwned = false;
			for (const UScriptStruct* OwnedStateType : OwnedStateTypes)
			{
				if (AreStructsInSameFamily(OwnedStateType, StateType))
				{
					bOwned = true;
					break;
				}
			}
			if (!bOwned)
			{
				OutResult.AddWarning(FText::Format(
					NSLOCTEXT("PEPItemValidation", "OrphanState",
					          "State fragment is not owned by any definition fragment: {0}."),
					FText::FromString(StateType ? StateType->GetName() : TEXT("None"))));
			}
		}

		if (const FPEPItemFragment_Stackable* StackFragment = Definition->GetDefinitionFragment<
			FPEPItemFragment_Stackable>())
		{
			if (const FPEPItemState_Stack* StackState = GetState<FPEPItemState_Stack>())
			{
				// Zero is a legal depleted state, so the valid range starts at zero.
				const int32 MaxStackSize = FMath::Max(1, StackFragment->GetEffectiveMaxStackSize());
				if (StackState->Count < 0 || StackState->Count > MaxStackSize)
				{
					OutResult.AddError(FText::Format(
						NSLOCTEXT("PEPItemValidation", "InvalidStackCount",
						          "Stack count {0} is outside valid range 0..{1}."),
						FText::AsNumber(StackState->Count),
						FText::AsNumber(MaxStackSize)));
				}
			}

			for (const FInstancedStruct& StateEntry : StateFragments)
			{
				const FPEPItemStateFragment* State = StateEntry.GetPtr<FPEPItemStateFragment>();
				if (State && State->GetStackability() == EPEPStateStackability::Divisible)
				{
					OutResult.AddError(FText::Format(
						NSLOCTEXT("PEPItemValidation", "StackableItemHasPerUnitState",
							"Stackable item carries per-unit state that cannot be split safely: {0}."),
						FText::FromString(StateEntry.GetScriptStruct()->GetName())));
				}
			}
		}

		if (const FPEPItemFragment_Resource* ResourceFragment = Definition->GetDefinitionFragment<
			FPEPItemFragment_Resource>())
		{
			if (ResourceFragment->MaxResource <= 0)
			{
				OutResult.AddError(NSLOCTEXT("PEPItemValidation", "InvalidMaxResource",
					"MaxResource must be greater than zero."));
			}

			if (const FPEPItemState_Resource* ResourceState = GetState<FPEPItemState_Resource>())
			{
				if (ResourceState->Current < 0 || ResourceState->Current > ResourceFragment->MaxResource)
				{
					OutResult.AddError(FText::Format(
						NSLOCTEXT("PEPItemValidation", "InvalidResourceValue",
							"Resource current value {0} is outside valid range 0..{1}."),
						FText::AsNumber(ResourceState->Current),
						FText::AsNumber(ResourceFragment->MaxResource)));
				}
			}
		}

		if (Definition->GetDefinitionFragment<FPEPItemFragment_Durability>())
		{
			if (const FPEPItemState_Durability* DurabilityState = GetState<FPEPItemState_Durability>())
			{
				if (!FMath::IsFinite(DurabilityState->Durability)
					|| DurabilityState->Durability < 0.0f
					|| DurabilityState->Durability > 1.0f)
				{
					OutResult.AddError(NSLOCTEXT("PEPItemValidation", "InvalidDurability",
					                             "Durability is outside valid range 0..1."));
				}
			}
		}
	}

	return OutResult.bValid;
}

bool FPEPItemInstance::MigrateInstanceFromVersion(const int32 LoadedSchemaVersion)
{
	if (LoadedSchemaVersion > CurrentSchemaVersion || LoadedSchemaVersion <= 0)
	{
		return false;
	}
	SchemaVersion = CurrentSchemaVersion;
	NormalizeStateFragments();
	return true;
}

void FPEPItemInstance::PostSerialize(const FArchive& Ar)
{
	/*
	 * Archive loads (save games, object duplication) are where a record written by an older build
	 * enters the game, so the upgrade happens here once instead of in every importer. A version
	 * newer than this build is left untouched: ValidateInstance then rejects it rather than the data
	 * being silently relabelled as current.
	 */
	if (Ar.IsLoading() && SchemaVersion < CurrentSchemaVersion)
	{
		MigrateInstanceFromVersion(SchemaVersion);
	}
}

bool FPEPItemInstance::RepairLoadedInstance()
{
	NormalizeStateFragments();
	const UPEPItemDefinition* Definition = GetDefinitionIfLoaded();
	if (Definition)
	{
		TArray<FInstancedStruct> DefaultStates;
		Definition->BuildDefaultStateFragments(DefaultStates);
		for (const FInstancedStruct& DefaultState : DefaultStates)
		{
			const UScriptStruct* DefaultStateStruct = DefaultState.GetScriptStruct();
			if (!DefaultStateStruct || FindStateByStruct(DefaultStateStruct))
			{
				continue;
			}
			FInstancedStruct& AddedState = StateFragments.AddDefaulted_GetRef();
			AddedState.InitializeAs(DefaultStateStruct);
			DefaultStateStruct->CopyScriptStruct(AddedState.GetMutableMemory(), DefaultState.GetMemory());
		}
		NormalizeStateFragments();
		RepairCoreStateValues(*this, Definition);
	}
	FPEPItemInstanceValidationResult Validation;
	return ValidateInstance(Validation);
}
