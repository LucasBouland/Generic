#include "Loadout/PEPLoadoutPreset.h"

#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemInstanceLibrary.h"
#include "Item/PEPItemPreset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FPrimaryAssetType UPEPLoadoutPreset::LoadoutAssetType(TEXT("Loadout"));

FPrimaryAssetId UPEPLoadoutPreset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(LoadoutAssetType, GetFName());
}

bool UPEPLoadoutPreset::CreatePlacementsInternal(
	const UObject* WorldContextObject,
	const bool bEditorPreview,
	TArray<FPEPInventoryTreePlacement>& OutPlacements,
	FText& OutError) const
{
	OutPlacements.Reset();
	OutError = FText::GetEmpty();

	if (Entries.IsEmpty())
	{
		OutError = FText::FromString(FString::Printf(
			TEXT("Loadout '%s' describes no entries."), *GetName()));
		return false;
	}

	/*
	 * Two entries on one slot cannot both be worn, and the store would only report the second as
	 * having nowhere to go — a confusing way to be told the recipe contradicts itself.
	 */
	TSet<FGameplayTag> SeenSlots;

	OutPlacements.Reserve(Entries.Num());

	for (const FPEPLoadoutEntry& Entry : Entries)
	{
		if (!Entry.SlotTag.IsValid())
		{
			OutError = FText::FromString(FString::Printf(
				TEXT("Loadout '%s' has an entry with no slot tag."), *GetName()));
			return false;
		}

		const bool bHasPreset = Entry.Preset != nullptr;
		const bool bHasDirectItem = Entry.ItemDefinition != nullptr;
		if (bHasPreset == bHasDirectItem)
		{
			OutError = FText::FromString(FString::Printf(
				TEXT("Loadout '%s', slot '%s' must specify exactly one source: a preset or a direct item."),
				*GetName(),
				*Entry.SlotTag.ToString()));
			return false;
		}

		bool bAlreadySeen = false;
		SeenSlots.Add(Entry.SlotTag, &bAlreadySeen);
		if (bAlreadySeen)
		{
			OutError = FText::FromString(FString::Printf(
				TEXT("Loadout '%s' fills slot '%s' twice."),
				*GetName(),
				*Entry.SlotTag.ToString()));
			return false;
		}

		FPEPItemInstanceTree Tree;
		if (bHasPreset)
		{
			FText PresetError;
			bool bBuilt = false;
			if (bEditorPreview)
			{
#if WITH_EDITOR
				bBuilt = Entry.Preset->CreateItemTreeForEditor(Tree, PresetError);
#else
				PresetError = FText::FromString(TEXT("Editor preview is unavailable in this build."));
#endif
			}
			else
			{
				bBuilt = Entry.Preset->CreateItemTree(WorldContextObject, Tree, PresetError);
			}
			if (!bBuilt)
			{
				OutError = FText::FromString(FString::Printf(
					TEXT("Loadout '%s', slot '%s': %s"),
					*GetName(),
					*Entry.SlotTag.ToString(),
					*PresetError.ToString()));
				return false;
			}
		}
		else
		{
			bool bCreated = false;
			if (bEditorPreview)
			{
#if WITH_EDITOR
				bCreated = UPEPItemInstanceLibrary::CreateItemInstanceForEditor(
					Entry.ItemDefinition, Tree.RootItem);
#endif
			}
			else
			{
				bCreated = UPEPItemInstanceLibrary::CreateItemInstance(
					WorldContextObject, Entry.ItemDefinition, Tree.RootItem);
			}
			if (!bCreated)
			{
				OutError = FText::FromString(FString::Printf(
					TEXT("Loadout '%s', slot '%s' could not create its item on the authority."),
					*GetName(),
					*Entry.SlotTag.ToString()));
				return false;
			}
			if (!Tree.IsStructurallyValid())
			{
				OutError = FText::FromString(FString::Printf(
					TEXT("Loadout '%s', slot '%s' failed to create direct item '%s'."),
					*GetName(),
					*Entry.SlotTag.ToString(),
					*Entry.ItemDefinition->GetName()));
				return false;
			}
		}

		FPEPInventoryTreePlacement& Placement = OutPlacements.AddDefaulted_GetRef();
		Placement.RootLocation = FPEPItemLocation::MakeNamedSlot(Entry.SlotTag);
		Placement.Tree = MoveTemp(Tree);
	}

	return true;
}

bool UPEPLoadoutPreset::CreatePlacements(
	const UObject* WorldContextObject,
	TArray<FPEPInventoryTreePlacement>& OutPlacements,
	FText& OutError) const
{
	return CreatePlacementsInternal(WorldContextObject, false, OutPlacements, OutError);
}

#if WITH_EDITOR
bool UPEPLoadoutPreset::CreatePlacementsForEditor(
	TArray<FPEPInventoryTreePlacement>& OutPlacements,
	FText& OutError) const
{
	return CreatePlacementsInternal(nullptr, true, OutPlacements, OutError);
}

EDataValidationResult UPEPLoadoutPreset::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult ParentResult = Super::IsDataValid(Context);

	/*
	 * Only what is decidable without a store: the recipes build, the slots are named once each.
	 * Whether a rig actually fits on this pawn depends on the slot definitions it will meet, which
	 * an asset cannot know — that answer comes from applying it.
	 */
	TArray<FPEPInventoryTreePlacement> Placements;
	FText Error;
	if (!CreatePlacementsForEditor(Placements, Error))
	{
		Context.AddError(Error);
		return EDataValidationResult::Invalid;
	}

	return ParentResult == EDataValidationResult::NotValidated
		? EDataValidationResult::Valid
		: ParentResult;
}
#endif
