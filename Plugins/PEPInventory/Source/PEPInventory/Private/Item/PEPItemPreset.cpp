#include "Item/PEPItemPreset.h"

#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Item/PEPItemDefinition.h"
#include "Item/Internal/PEPItemMutableAccess.h"
#include "Item/PEPItemInstanceLibrary.h"
#include "Inventory/PEPInventorySettings.h"
#include "Item/Fragments/PEPItemFragment_InventoryShape.h"
#include "Item/PEPItemStateFragment.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FPrimaryAssetType UPEPItemPreset::PresetAssetType(TEXT("ItemPreset"));

namespace PEPItemPresetPrivate
{
	bool Fail(FText& OutError, const FString& Message)
	{
		OutError = FText::FromString(Message);
		return false;
	}

	bool ApplyStateOverrides(
		const UPEPItemPreset& Preset,
		FPEPItemInstance& Item,
		FText& OutError)
	{
		TSet<const UScriptStruct*> SeenOverrides;
		for (const FInstancedStruct& Override : Preset.StateOverrides)
		{
			const UScriptStruct* OverrideType = Override.GetScriptStruct();
			if (!OverrideType
				|| !OverrideType->IsChildOf(FPEPItemStateFragment::StaticStruct())
				|| SeenOverrides.Contains(OverrideType))
			{
				return Fail(OutError, FString::Printf(
					TEXT("Preset '%s' contains an invalid or duplicate state override."),
					*Preset.GetName()));
			}

			SeenOverrides.Add(OverrideType);
			FInstancedStruct* ExistingState = FPEPItemMutableAccess::GetMutableStateFragments(Item).FindByPredicate(
				[OverrideType](const FInstancedStruct& State)
				{
					return State.GetScriptStruct() == OverrideType;
				});

			if (!ExistingState)
			{
				return Fail(OutError, FString::Printf(
					TEXT("Preset '%s' overrides state '%s', but its definition does not own it."),
					*Preset.GetName(),
					*OverrideType->GetName()));
			}

			*ExistingState = Override;
		}

		Item.NormalizeStateFragments();
		FPEPItemInstanceValidationResult Validation;
		if (!Item.ValidateInstance(Validation))
		{
			const FString Detail = Validation.Errors.IsEmpty()
				? TEXT("unknown validation error")
				: Validation.Errors[0].ToString();
			return Fail(OutError, FString::Printf(
				TEXT("Preset '%s' produced an invalid item: %s"),
				*Preset.GetName(),
				*Detail));
		}

		return true;
	}

	bool MatchesTags(
		const UPEPItemDefinition& Definition,
		const FGameplayTagContainer& Required,
		const FGameplayTagContainer& Blocked)
	{
		return Definition.HasAllOwnedTags(Required)
			&& !Definition.HasAnyOwnedTags(Blocked);
	}

	bool RectsOverlap(const FIntRect& A, const FIntRect& B)
	{
		return A.Min.X < B.Max.X
			&& A.Max.X > B.Min.X
			&& A.Min.Y < B.Max.Y
			&& A.Max.Y > B.Min.Y;
	}

	bool CreateNode(
		const UObject* WorldContextObject,
		const bool bEditorPreview,
		const UPEPItemPreset& Preset,
		const FPEPItemId ParentItemId,
		const FName ParentSlotName,
		const FIntPoint GridPosition,
		const bool bRotated,
		FPEPItemInstanceTree& OutTree,
		TSet<const UPEPItemPreset*>& ActivePresets,
		FText& OutError)
	{
		if (ActivePresets.Contains(&Preset))
		{
			return Fail(OutError, FString::Printf(
				TEXT("Preset graph contains a cycle through '%s'."),
				*Preset.GetName()));
		}

		if (!Preset.RootDefinition)
		{
			return Fail(OutError, FString::Printf(
				TEXT("Preset '%s' has no root definition."),
				*Preset.GetName()));
		}

		ActivePresets.Add(&Preset);
		FPEPItemInstance Item;
		bool bCreated = false;
		if (bEditorPreview)
		{
#if WITH_EDITOR
			bCreated = UPEPItemInstanceLibrary::CreateItemInstanceForEditor(Preset.RootDefinition, Item);
#endif
		}
		else
		{
			bCreated = UPEPItemInstanceLibrary::CreateItemInstance(
				WorldContextObject, Preset.RootDefinition, Item);
		}
		if (!bCreated)
		{
			ActivePresets.Remove(&Preset);
			return Fail(OutError, FString::Printf(
				TEXT("Preset '%s' could not create its root item on the authority."),
				*Preset.GetName()));
		}
		if (!ApplyStateOverrides(Preset, Item, OutError))
		{
			ActivePresets.Remove(&Preset);
			return false;
		}

		const FPEPItemId NewItemId = Item.ItemId;
		if (!ParentItemId.IsValid())
		{
			OutTree.RootItem = MoveTemp(Item);
		}
		else
		{
			FPEPItemInstanceTreeEntry& Entry = OutTree.Children.AddDefaulted_GetRef();
			Entry.Item = MoveTemp(Item);
			Entry.Location = FPEPItemLocation::MakeItemContainer(
				ParentItemId,
				ParentSlotName,
				GridPosition,
				bRotated);
		}

		const FPEPItemFragment_Container* Container =
			Preset.RootDefinition->GetDefinitionFragment<FPEPItemFragment_Container>();
		if (!Preset.Children.IsEmpty() && !Container)
		{
			ActivePresets.Remove(&Preset);
			return Fail(OutError, FString::Printf(
				TEXT("Preset '%s' has children, but its definition is not a container."),
				*Preset.GetName()));
		}

		TSet<FName> OccupiedSingleSlots;
		TMap<FName, TArray<FIntRect>> OccupiedGridRects;
		for (const FPEPItemPresetChild& Child : Preset.Children)
		{
			const bool bHasPresetChild = Child.Preset != nullptr;
			const bool bHasDirectItemChild = Child.ItemDefinition != nullptr;
			if (bHasPresetChild == bHasDirectItemChild || Child.ContainerSlotName.IsNone())
			{
				ActivePresets.Remove(&Preset);
				return Fail(OutError, FString::Printf(
					TEXT("Preset '%s' has a child that must specify exactly one source (a preset or a direct item) and a slot name."),
					*Preset.GetName()));
			}

			if (bHasPresetChild && !Child.Preset->RootDefinition)
			{
				ActivePresets.Remove(&Preset);
				return Fail(OutError, FString::Printf(
					TEXT("Preset '%s' has a child preset with no root definition."),
					*Preset.GetName()));
			}

			const FPEPContainerSlotDefinition* Slot = Container
				? Container->FindSlot(Child.ContainerSlotName)
				: nullptr;
			if (!Slot || !Slot->IsValid())
			{
				ActivePresets.Remove(&Preset);
				return Fail(OutError, FString::Printf(
					TEXT("Preset '%s' targets missing or invalid slot '%s'."),
					*Preset.GetName(),
					*Child.ContainerSlotName.ToString()));
			}

			const UPEPItemDefinition& ChildDefinition = bHasPresetChild
				? *Child.Preset->RootDefinition
				: *Child.ItemDefinition;
			if (!MatchesTags(ChildDefinition, Container->RequiredContainedItemTags,
					Container->BlockedContainedItemTags)
				|| !MatchesTags(ChildDefinition, Slot->RequiredItemTags, Slot->BlockedItemTags))
			{
				ActivePresets.Remove(&Preset);
				return Fail(OutError, FString::Printf(
					TEXT("Preset '%s' has a child rejected by slot '%s' tag rules."),
					*Preset.GetName(),
					*Child.ContainerSlotName.ToString()));
			}

			FIntPoint EffectivePosition = FIntPoint::ZeroValue;
			bool bEffectiveRotation = false;
			if (Slot->Mode == EPEPContainerSlotMode::Single)
			{
				if (OccupiedSingleSlots.Contains(Child.ContainerSlotName))
				{
					ActivePresets.Remove(&Preset);
					return Fail(OutError, FString::Printf(
						TEXT("Preset '%s' places more than one child in single slot '%s'."),
						*Preset.GetName(),
						*Child.ContainerSlotName.ToString()));
				}
				OccupiedSingleSlots.Add(Child.ContainerSlotName);
			}
			else
			{
				FIntPoint ChildSize;
				bool bChildCanRotate = false;
				PEPInventorySettings().GetEffectiveInventoryShape(
					ChildDefinition.GetDefinitionFragment<FPEPItemFragment_InventoryShape>(),
					ChildSize,
					bChildCanRotate);
				if (Child.bRotated)
				{
					if (!Slot->bAllowRotation || !bChildCanRotate)
					{
						ActivePresets.Remove(&Preset);
						return Fail(OutError, FString::Printf(
							TEXT("Preset '%s' rotates a child that slot '%s' does not allow to rotate."),
							*Preset.GetName(),
							*Child.ContainerSlotName.ToString()));
					}
					Swap(ChildSize.X, ChildSize.Y);
				}

				EffectivePosition = Child.GridPosition;
				bEffectiveRotation = Child.bRotated;
				const FIntRect ChildRect(EffectivePosition, EffectivePosition + ChildSize);
				if (EffectivePosition.X < 0
					|| EffectivePosition.Y < 0
					|| ChildRect.Max.X > Slot->GridSize.X
					|| ChildRect.Max.Y > Slot->GridSize.Y)
				{
					ActivePresets.Remove(&Preset);
					return Fail(OutError, FString::Printf(
						TEXT("Preset '%s' places a child outside grid slot '%s'."),
						*Preset.GetName(),
						*Child.ContainerSlotName.ToString()));
				}

				TArray<FIntRect>& Rects = OccupiedGridRects.FindOrAdd(Child.ContainerSlotName);
				if (Rects.ContainsByPredicate(
					[&ChildRect](const FIntRect& Existing)
					{
						return RectsOverlap(Existing, ChildRect);
					}))
				{
					ActivePresets.Remove(&Preset);
					return Fail(OutError, FString::Printf(
						TEXT("Preset '%s' has overlapping children in grid slot '%s'."),
						*Preset.GetName(),
						*Child.ContainerSlotName.ToString()));
				}
				Rects.Add(ChildRect);
			}

			if (bHasPresetChild)
			{
				if (!CreateNode(
					WorldContextObject,
					bEditorPreview,
					*Child.Preset,
					NewItemId,
					Child.ContainerSlotName,
					EffectivePosition,
					bEffectiveRotation,
					OutTree,
					ActivePresets,
					OutError))
				{
					ActivePresets.Remove(&Preset);
					return false;
				}
			}
			else
			{
				FPEPItemInstanceTreeEntry& ChildEntry = OutTree.Children.AddDefaulted_GetRef();
				bool bChildCreated = false;
				if (bEditorPreview)
				{
#if WITH_EDITOR
					bChildCreated = UPEPItemInstanceLibrary::CreateItemInstanceForEditor(
						Child.ItemDefinition, ChildEntry.Item);
#endif
				}
				else
				{
					bChildCreated = UPEPItemInstanceLibrary::CreateItemInstance(
						WorldContextObject, Child.ItemDefinition, ChildEntry.Item);
				}
				if (!bChildCreated)
				{
					OutTree.Children.Pop();
					ActivePresets.Remove(&Preset);
					return Fail(OutError, FString::Printf(
						TEXT("Preset '%s' could not create child item '%s' on the authority."),
						*Preset.GetName(),
						*Child.ItemDefinition->GetName()));
				}
				ChildEntry.Location = FPEPItemLocation::MakeItemContainer(
					NewItemId,
					Child.ContainerSlotName,
					EffectivePosition,
					bEffectiveRotation);
			}
		}

		ActivePresets.Remove(&Preset);
		return true;
	}
}

FPrimaryAssetId UPEPItemPreset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PresetAssetType, GetFName());
}

namespace
{
	bool BuildPresetTree(
		const UPEPItemPreset& Preset,
		const UObject* WorldContextObject,
		const bool bEditorPreview,
		FPEPItemInstanceTree& OutTree,
		FText& OutError)
{
	OutTree.Reset();
	OutError = FText::GetEmpty();
	TSet<const UPEPItemPreset*> ActivePresets;
	if (!PEPItemPresetPrivate::CreateNode(
		WorldContextObject,
		bEditorPreview,
		Preset,
		FPEPItemId(),
		NAME_None,
		FIntPoint::ZeroValue,
		false,
		OutTree,
		ActivePresets,
		OutError))
	{
		OutTree.Reset();
		return false;
	}

	FPEPItemInstanceTreeValidationResult ValidationResult;
	if (!OutTree.ValidateDetailed(ValidationResult))
	{
		OutError = ValidationResult.Errors.IsEmpty()
			? FText::FromString(TEXT("Preset produced an invalid item tree."))
			: ValidationResult.Errors[0];
		OutTree.Reset();
		return false;
	}

	return true;
}
}

bool UPEPItemPreset::CreateItemTree(
	const UObject* WorldContextObject,
	FPEPItemInstanceTree& OutTree,
	FText& OutError) const
{
	return BuildPresetTree(*this, WorldContextObject, false, OutTree, OutError);
}

#if WITH_EDITOR
bool UPEPItemPreset::CreateItemTreeForEditor(FPEPItemInstanceTree& OutTree, FText& OutError) const
{
	return BuildPresetTree(*this, nullptr, true, OutTree, OutError);
}

EDataValidationResult UPEPItemPreset::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult ParentResult = Super::IsDataValid(Context);
	FPEPItemInstanceTree Tree;
	FText Error;
	if (!CreateItemTreeForEditor(Tree, Error))
	{
		Context.AddError(Error);
		return EDataValidationResult::Invalid;
	}

	return ParentResult == EDataValidationResult::NotValidated
		? EDataValidationResult::Valid
		: ParentResult;
}
#endif
