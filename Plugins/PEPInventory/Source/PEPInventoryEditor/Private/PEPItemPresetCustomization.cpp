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
#include "Misc/PackageName.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PEPItemPresetCustomization"

TSharedRef<IDetailCustomization> FPEPItemPresetCustomization::MakeInstance()
{
	return MakeShared<FPEPItemPresetCustomization>();
}

FPEPItemPresetCustomization::~FPEPItemPresetCustomization()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
}

void FPEPItemPresetCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	EditedPreset = Objects.Num() == 1 ? Cast<UPEPItemPreset>(Objects[0].Get()) : nullptr;

	PropertyUtilities = DetailBuilder.GetPropertyUtilities();

	ChildrenHandle = DetailBuilder.GetProperty(
		GET_MEMBER_NAME_CHECKED(UPEPItemPreset, Children));

	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
	ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(
		SharedThis(this),
		&FPEPItemPresetCustomization::HandleObjectPropertyChanged);
	RefreshBuildResult();

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

void FPEPItemPresetCustomization::RefreshBuildResult()
{
	const UPEPItemPreset* Preset = EditedPreset.Get();
	if (!Preset)
	{
		CachedBuildText = FText::GetEmpty();
		bCachedBuildSucceeded = false;
		return;
	}

	FPEPItemInstanceTree Tree;
	FText Error;
	bCachedBuildSucceeded = Preset->CreateItemTreeForEditor(Tree, Error);
	CachedBuildText = bCachedBuildSucceeded
		? LOCTEXT("BuildOk", "Builds successfully.")
		: FText::Format(LOCTEXT("BuildFailed", "Build failed: {0}"), Error);
}

void FPEPItemPresetCustomization::HandleObjectPropertyChanged(
	UObject* Object,
	FPropertyChangedEvent& PropertyChangedEvent)
{
	/* A drag reports every frame of itself; the value it settles on arrives as a final change. */
	if (!Object || PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
	{
		return;
	}

	/*
	 * A build reads the edited preset, the presets nested in it and every definition they name,
	 * so an edit to any of those can change the answer. Undo and redo arrive here as well.
	 */
	if (Object == EditedPreset.Get()
		|| Object->IsA<UPEPItemPreset>()
		|| Object->IsA<UPEPItemDefinition>())
	{
		RefreshBuildResult();
	}
}

FText FPEPItemPresetCustomization::GetValidationText() const
{
	return EditedPreset.IsValid() ? CachedBuildText : FText::GetEmpty();
}

FSlateColor FPEPItemPresetCustomization::GetValidationColor() const
{
	if (!EditedPreset.IsValid() || CachedBuildText.IsEmpty())
	{
		return FSlateColor::UseForeground();
	}

	return bCachedBuildSucceeded
		? FSlateColor(FLinearColor(0.3f, 0.85f, 0.3f))
		: FSlateColor(FLinearColor(0.95f, 0.35f, 0.3f));
}

TSharedRef<SWidget> FPEPItemPresetCustomization::BuildAddChildMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	const UPEPItemPreset* Preset = EditedPreset.Get();
	const UPEPItemDefinition* RootDefinition = Preset ? Preset->RootDefinition.Get() : nullptr;
	if (!RootDefinition)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("NoRoot", "Set a Root Definition first"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction());
		return MenuBuilder.MakeWidget();
	}

	const TConstArrayView<FPEPContainerSlotDefinition> Slots =
		PEPItemContainerRules::FindContainerSlots(RootDefinition);
	if (Slots.IsEmpty())
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("NoSlotsRoot", "Root Definition declares no container slots"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction());
		return MenuBuilder.MakeWidget();
	}

	/* Bound through the shared pointer, never a raw this: the menu can outlive the details panel. */
	const TWeakObjectPtr<const UPEPItemDefinition> WeakRootDefinition(RootDefinition);
	for (const FPEPContainerSlotDefinition& Slot : Slots)
	{
		MenuBuilder.AddSubMenu(
			FText::FromName(Slot.SlotName),
			FText::GetEmpty(),
			FNewMenuDelegate::CreateSP(
				this,
				&FPEPItemPresetCustomization::FillSlotSubMenu,
				Slot.SlotName,
				WeakRootDefinition));
	}

	return MenuBuilder.MakeWidget();
}

void FPEPItemPresetCustomization::FillSlotSubMenu(
	FMenuBuilder& SubMenuBuilder,
	const FName SlotName,
	const TWeakObjectPtr<const UPEPItemDefinition> WeakRootDefinition)
{
	const UPEPItemDefinition* RootDefinition = WeakRootDefinition.Get();
	if (!RootDefinition)
	{
		SubMenuBuilder.AddMenuEntry(
			LOCTEXT("RootGone", "The Root Definition is no longer loaded"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction());
		return;
	}

	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FText Unused;
	bool bAnyEntry = false;

	/*
	 * Presets are judged by their RootDefinition registry tag and loaded only once picked: a preset
	 * pulls its whole recipe graph in, and opening this menu used to load every one in the project.
	 * The definitions themselves still have to be resident, since the slot rules read their owned
	 * tags, but those are small and normally preloaded already.
	 */
	TArray<FAssetData> PresetAssets;
	AssetRegistry.GetAssetsByClass(
		UPEPItemPreset::StaticClass()->GetClassPathName(), PresetAssets, /*bSearchSubClasses=*/true);
	for (const FAssetData& AssetData : PresetAssets)
	{
		const UPEPItemDefinition* CandidateRoot = nullptr;
		FString RootDefinitionTag;
		if (AssetData.GetTagValue(GET_MEMBER_NAME_CHECKED(UPEPItemPreset, RootDefinition), RootDefinitionTag))
		{
			const FSoftObjectPath RootPath(FPackageName::ExportTextPathToObjectPath(RootDefinitionTag));
			CandidateRoot = RootPath.IsValid() ? Cast<UPEPItemDefinition>(RootPath.TryLoad()) : nullptr;
		}
		else if (const UPEPItemPreset* CandidatePreset = Cast<UPEPItemPreset>(AssetData.GetAsset()))
		{
			/* Saved before the tag existed: the only way to know what it builds is to look. */
			CandidateRoot = CandidatePreset->RootDefinition;
		}

		if (!CandidateRoot
			|| !UPEPItemContainerRulesLibrary::CanPlaceItemInSlot(
				RootDefinition, SlotName, CandidateRoot, Unused))
		{
			continue;
		}

		bAnyEntry = true;
		SubMenuBuilder.AddMenuEntry(
			FText::FromString(FString::Printf(TEXT("[Preset] %s"), *AssetData.AssetName.ToString())),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(
				this,
				&FPEPItemPresetCustomization::AddChildEntry,
				SlotName,
				AssetData.GetSoftObjectPath(),
				FSoftObjectPath())));
	}

	/* No registry tag carries an item's owned tags, so a definition must be resident to be judged. */
	TArray<FAssetData> ItemAssets;
	AssetRegistry.GetAssetsByClass(
		UPEPItemDefinition::StaticClass()->GetClassPathName(), ItemAssets, /*bSearchSubClasses=*/true);
	for (const FAssetData& AssetData : ItemAssets)
	{
		const UPEPItemDefinition* CandidateItem = Cast<UPEPItemDefinition>(AssetData.GetAsset());
		if (!CandidateItem
			|| !UPEPItemContainerRulesLibrary::CanPlaceItemInSlot(
				RootDefinition, SlotName, CandidateItem, Unused))
		{
			continue;
		}

		bAnyEntry = true;
		SubMenuBuilder.AddMenuEntry(
			FText::FromString(FString::Printf(TEXT("[Item] %s"), *AssetData.AssetName.ToString())),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(
				this,
				&FPEPItemPresetCustomization::AddChildEntry,
				SlotName,
				FSoftObjectPath(),
				AssetData.GetSoftObjectPath())));
	}

	if (!bAnyEntry)
	{
		SubMenuBuilder.AddMenuEntry(
			LOCTEXT("NoCandidates", "Nothing matches this slot's tag rules"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction());
	}
}

void FPEPItemPresetCustomization::AddChildEntry(
	const FName SlotName,
	const FSoftObjectPath PresetSource,
	const FSoftObjectPath ItemSource)
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

	/* Loaded now that it has been picked; a source that fails to load adds nothing. */
	UObject* PresetObject = PresetSource.IsValid() ? PresetSource.TryLoad() : nullptr;
	UObject* ItemObject = ItemSource.IsValid() ? ItemSource.TryLoad() : nullptr;
	if (!Cast<UPEPItemPreset>(PresetObject) && !Cast<UPEPItemDefinition>(ItemObject))
	{
		return;
	}

	/* One undo step for the new element and every field set on it, not one per handle write. */
	const FScopedTransaction Transaction(LOCTEXT("AddPresetChild", "Add Preset Child"));

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
	if (PresetHandle.IsValid() && Cast<UPEPItemPreset>(PresetObject))
	{
		PresetHandle->SetValue(PresetObject);
	}
	if (ItemHandle.IsValid() && Cast<UPEPItemDefinition>(ItemObject))
	{
		ItemHandle->SetValue(ItemObject);
	}

	if (PropertyUtilities.IsValid())
	{
		PropertyUtilities->ForceRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
