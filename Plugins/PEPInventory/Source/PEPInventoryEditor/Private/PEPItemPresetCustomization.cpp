#include "PEPItemPresetCustomization.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IPropertyUtilities.h"
#include "Item/PEPItemContainerRules.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemPreset.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PEPItemPresetCustomization"

TSharedRef<IDetailCustomization> FPEPItemPresetCustomization::MakeInstance()
{
	return MakeShared<FPEPItemPresetCustomization>();
}

void FPEPItemPresetCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	EditedPreset = Objects.Num() == 1 ? Cast<UPEPItemPreset>(Objects[0].Get()) : nullptr;

	PropertyUtilities = DetailBuilder.GetPropertyUtilities();

	ChildrenHandle = DetailBuilder.GetProperty(
		GET_MEMBER_NAME_CHECKED(UPEPItemPreset, Children));

	IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(TEXT("Preset"));

	CategoryBuilder.AddCustomRow(LOCTEXT("BuildResultSearch", "Build Result Validate CreateItemTree"))
		.WholeRowContent()
		[
			SNew(STextBlock)
			.Text(this, &FPEPItemPresetCustomization::GetValidationText)
			.ColorAndOpacity(this, &FPEPItemPresetCustomization::GetValidationColor)
			.AutoWrapText(true)
		];

	CategoryBuilder.AddCustomRow(LOCTEXT("AddChildSearch", "Add Child"))
		.NameContent()
		[
			SNew(STextBlock).Text(LOCTEXT("AddChildLabel", "Add Child"))
		]
		.ValueContent()
		.MinDesiredWidth(200.f)
		[
			SNew(SComboButton)
			.ButtonContent()
			[
				SNew(STextBlock).Text(LOCTEXT("AddChildButton", "Add Child"))
			]
			.OnGetMenuContent(this, &FPEPItemPresetCustomization::BuildAddChildMenu)
		];
}

FText FPEPItemPresetCustomization::GetValidationText() const
{
	const UPEPItemPreset* Preset = EditedPreset.Get();
	if (!Preset)
	{
		return FText::GetEmpty();
	}

	FPEPItemInstanceTree Tree;
	FText Error;
	if (Preset->CreateItemTreeForEditor(Tree, Error))
	{
		return LOCTEXT("BuildOk", "Builds successfully.");
	}

	return FText::Format(LOCTEXT("BuildFailed", "Build failed: {0}"), Error);
}

FSlateColor FPEPItemPresetCustomization::GetValidationColor() const
{
	const UPEPItemPreset* Preset = EditedPreset.Get();
	if (!Preset)
	{
		return FSlateColor::UseForeground();
	}

	FPEPItemInstanceTree Tree;
	FText Error;
	return Preset->CreateItemTreeForEditor(Tree, Error)
		? FSlateColor(FLinearColor(0.3f, 0.85f, 0.3f))
		: FSlateColor(FLinearColor(0.95f, 0.35f, 0.3f));
}

TSharedRef<SWidget> FPEPItemPresetCustomization::BuildAddChildMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	const UPEPItemPreset* Preset = EditedPreset.Get();
	UPEPItemDefinition* RootDefinition = Preset ? Preset->RootDefinition : nullptr;
	if (!RootDefinition)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("NoRoot", "Set a Root Definition first"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction());
		return MenuBuilder.MakeWidget();
	}

	const TArray<FPEPContainerSlotDefinition> Slots =
		UPEPItemContainerRulesLibrary::GetContainerSlots(RootDefinition);
	if (Slots.IsEmpty())
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("NoSlotsRoot", "Root Definition declares no container slots"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction());
		return MenuBuilder.MakeWidget();
	}

	/*
	 * Loaded once per menu open, not cached: a preset editor is opened rarely enough that a full
	 * synchronous load of every candidate asset here is cheaper than the bookkeeping needed to
	 * keep a cache correct across asset creation, deletion and tag edits elsewhere in the editor.
	 */
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	TArray<FAssetData> PresetAssets;
	AssetRegistryModule.Get().GetAssetsByClass(
		UPEPItemPreset::StaticClass()->GetClassPathName(), PresetAssets, /*bSearchSubClasses=*/true);

	TArray<FAssetData> ItemAssets;
	AssetRegistryModule.Get().GetAssetsByClass(
		UPEPItemDefinition::StaticClass()->GetClassPathName(), ItemAssets, /*bSearchSubClasses=*/true);

	for (const FPEPContainerSlotDefinition& Slot : Slots)
	{
		const FName SlotName = Slot.SlotName;

		MenuBuilder.AddSubMenu(
			FText::FromName(SlotName),
			FText::GetEmpty(),
			FNewMenuDelegate::CreateLambda(
				[this, SlotName, RootDefinition, PresetAssets, ItemAssets](FMenuBuilder& SubMenuBuilder)
				{
					FText Unused;
					bool bAnyEntry = false;

					for (const FAssetData& AssetData : PresetAssets)
					{
						UPEPItemPreset* CandidatePreset = Cast<UPEPItemPreset>(AssetData.GetAsset());
						if (!CandidatePreset || !CandidatePreset->RootDefinition
							|| !UPEPItemContainerRulesLibrary::CanPlaceItemInSlot(
								RootDefinition, SlotName, CandidatePreset->RootDefinition, Unused))
						{
							continue;
						}

						bAnyEntry = true;
						SubMenuBuilder.AddMenuEntry(
							FText::FromString(FString::Printf(TEXT("[Preset] %s"), *CandidatePreset->GetName())),
							FText::GetEmpty(),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateSP(
								this,
								&FPEPItemPresetCustomization::AddChildEntry,
								SlotName,
								CandidatePreset,
								static_cast<UPEPItemDefinition*>(nullptr))));
					}

					for (const FAssetData& AssetData : ItemAssets)
					{
						UPEPItemDefinition* CandidateItem = Cast<UPEPItemDefinition>(AssetData.GetAsset());
						if (!CandidateItem
							|| !UPEPItemContainerRulesLibrary::CanPlaceItemInSlot(
								RootDefinition, SlotName, CandidateItem, Unused))
						{
							continue;
						}

						bAnyEntry = true;
						SubMenuBuilder.AddMenuEntry(
							FText::FromString(FString::Printf(TEXT("[Item] %s"), *CandidateItem->GetName())),
							FText::GetEmpty(),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateSP(
								this,
								&FPEPItemPresetCustomization::AddChildEntry,
								SlotName,
								static_cast<UPEPItemPreset*>(nullptr),
								CandidateItem)));
					}

					if (!bAnyEntry)
					{
						SubMenuBuilder.AddMenuEntry(
							LOCTEXT("NoCandidates", "Nothing matches this slot's tag rules"),
							FText::GetEmpty(),
							FSlateIcon(),
							FUIAction());
					}
				}));
	}

	return MenuBuilder.MakeWidget();
}

void FPEPItemPresetCustomization::AddChildEntry(
	const FName SlotName,
	UPEPItemPreset* PresetSource,
	UPEPItemDefinition* ItemSource)
{
	if (!ChildrenHandle.IsValid())
	{
		return;
	}

	const TSharedPtr<IPropertyHandleArray> ArrayHandle = ChildrenHandle->AsArray();
	if (!ArrayHandle.IsValid())
	{
		return;
	}

	uint32 NumElements = 0;
	ArrayHandle->GetNumElements(NumElements);
	ArrayHandle->AddItem();

	const TSharedPtr<IPropertyHandle> NewElementHandle = ChildrenHandle->GetChildHandle(NumElements);
	if (!NewElementHandle.IsValid())
	{
		return;
	}

	const TSharedPtr<IPropertyHandle> SlotHandle = NewElementHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FPEPItemPresetChild, ContainerSlotName));
	const TSharedPtr<IPropertyHandle> PresetHandle = NewElementHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FPEPItemPresetChild, Preset));
	const TSharedPtr<IPropertyHandle> ItemHandle = NewElementHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FPEPItemPresetChild, ItemDefinition));

	if (SlotHandle.IsValid())
	{
		SlotHandle->SetValue(SlotName);
	}
	if (PresetHandle.IsValid() && PresetSource)
	{
		PresetHandle->SetValue(static_cast<UObject*>(PresetSource));
	}
	if (ItemHandle.IsValid() && ItemSource)
	{
		ItemHandle->SetValue(static_cast<UObject*>(ItemSource));
	}

	if (PropertyUtilities.IsValid())
	{
		PropertyUtilities->ForceRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
