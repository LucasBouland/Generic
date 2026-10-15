#include "SPEPItemDefinitionBrowser.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "IDetailsView.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Item/PEPItemDefinition.h"
#include "PropertyEditorModule.h"
#include "Styling/CoreStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"

#define LOCTEXT_NAMESPACE "PEPItemDefinitionBrowser"

const FName SPEPItemDefinitionBrowser::TabId(TEXT("PEPItemDefinitionBrowser"));

namespace
{
	/** "PEPItemFragment_Stackable, PEPItemFragment_Weight" trimmed down to "Stackable, Weight". */
	FText BuildFragmentSummary(const UPEPItemDefinition* Definition)
	{
		if (!Definition)
		{
			return FText::GetEmpty();
		}

		TArray<FString> Names;
		for (const FInstancedStruct& Fragment : Definition->GetDefinitionFragments())
		{
			if (const UScriptStruct* Struct = Fragment.GetScriptStruct())
			{
				FString Name = Struct->GetName();
				Name.RemoveFromStart(TEXT("PEPItemFragment_"));
				Names.Add(MoveTemp(Name));
			}
		}

		return FText::FromString(FString::Join(Names, TEXT(", ")));
	}

	/**
	 * The path right after "Item.Type." on the first matching tag —
	 * Item.Type.Weapon.Rifle becomes Weapon.Rifle. Definitions without this tag appear under All.
	 */
	FString ExtractItemTypePath(const UPEPItemDefinition* Definition)
	{
		if (!Definition)
		{
		return FString();
		}

		static const FString Prefix(TEXT("Item.Type."));

		for (const FGameplayTag& Tag : Definition->OwnedTags.GetGameplayTagArray())
		{
			const FString TagString = Tag.ToString();
			if (!TagString.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				continue;
			}

			const FString TypePath = TagString.RightChop(Prefix.Len());
			if (!TypePath.IsEmpty())
			{
				return TypePath;
			}
		}

		return FString();
	}

	FName ExtractCategory(const UPEPItemDefinition* Definition)
	{
		FString TypePath = ExtractItemTypePath(Definition);
		if (TypePath.IsEmpty())
		{
			return NAME_None;
		}

		FString Category;
		if (!TypePath.Split(TEXT("."), &Category, nullptr))
		{
			Category = MoveTemp(TypePath);
		}
		return FName(*Category);
	}

	/** One row, three columns. Kept file-local: nothing outside this browser needs it. */
	class SPEPItemDefinitionRow : public SMultiColumnTableRow<TWeakObjectPtr<UPEPItemDefinition>>
	{
	public:
		SLATE_BEGIN_ARGS(SPEPItemDefinitionRow)
		{
		}

		SLATE_ARGUMENT(TWeakObjectPtr<UPEPItemDefinition>, Item)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
		{
			Item = InArgs._Item;
			SMultiColumnTableRow<TWeakObjectPtr<UPEPItemDefinition>>::Construct(
				FSuperRowType::FArguments(),
				OwnerTable);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
		{
			if (!Item.IsValid())
			{
				return SNullWidget::NullWidget;
			}

			/*
			 * A live attribute, not a value computed once here: SListView reuses this row's widgets
			 * across refreshes whenever the item's identity is unchanged, which a property edit never
			 * changes. The lambda re-reads the definition when Slate paints this row, so text edits are
			 * reflected without regenerating the row.
			 */
			TWeakObjectPtr<UPEPItemDefinition> WeakItem = Item;
			FName Column = ColumnName;

			return SNew(SBox)
				.Padding(FMargin(4.f, 2.f))
				[
					SNew(STextBlock)
					.Text_Lambda(
						[WeakItem, Column]() -> FText
						{
							const UPEPItemDefinition* Definition = WeakItem.Get();
							if (!Definition)
							{
								return FText::GetEmpty();
							}

							if (Column == TEXT("Name"))
							{
								return Definition->DisplayName;
							}
							if (Column == TEXT("Asset"))
							{
								return FText::FromString(Definition->GetName());
							}
							if (Column == TEXT("Type"))
							{
								const FString TypePath = ExtractItemTypePath(Definition);
								return TypePath.IsEmpty()
									? LOCTEXT("NoItemType", "—")
									: FText::FromString(TypePath);
							}
							if (Column == TEXT("Fragments"))
							{
								return BuildFragmentSummary(Definition);
							}

							return FText::GetEmpty();
						})
				];
		}

	private:
		TWeakObjectPtr<UPEPItemDefinition> Item;
	};
}

void SPEPItemDefinitionBrowser::Construct(const FArguments& InArgs)
{
	SelectedCategory = NAME_None;

	RefreshItemList();

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		+ SSplitter::Slot()
		.Value(0.22f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.f)
			[
				SNew(STextBlock).Text(LOCTEXT("TypeHeading", "Item.Type"))
			]
			+ SVerticalBox::Slot()
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(CategoryBox, SVerticalBox)
				]
			]
		]
		+ SSplitter::Slot()
		.Value(0.78f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Refresh", "Refresh"))
					.OnClicked(this, &SPEPItemDefinitionBrowser::OnRefreshClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.f, 0.f)
				[
					SNew(SButton)
					.Text(LOCTEXT("OpenDetails", "Open Details Window"))
					.IsEnabled_Lambda([this] { return SelectedItem.IsValid(); })
					.OnClicked(this, &SPEPItemDefinitionBrowser::OnOpenDetailsClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("OpenAssetEditor", "Open Asset Editor"))
					.IsEnabled_Lambda([this] { return SelectedItem.IsValid(); })
					.OnClicked(this, &SPEPItemDefinitionBrowser::OnOpenAssetEditorClicked)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				.Padding(8.f, 0.f)
				[
					SNew(STextBlock)
					.Text_Lambda(
						[this]
						{
							return FText::Format(
								LOCTEXT("Count", "{0} of {1} item definition(s)"),
								FText::AsNumber(FilteredItems.Num()),
								FText::AsNumber(AllItems.Num()));
						})
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SAssignNew(ListView, SListView<TWeakObjectPtr<UPEPItemDefinition>>)
					.ListItemsSource(&FilteredItems)
					.OnGenerateRow(this, &SPEPItemDefinitionBrowser::OnGenerateRow)
					.OnMouseButtonDoubleClick(this, &SPEPItemDefinitionBrowser::OnRowDoubleClicked)
					.OnSelectionChanged(this, &SPEPItemDefinitionBrowser::OnSelectionChanged)
					.HeaderRow(
						SNew(SHeaderRow)
						+ SHeaderRow::Column(TEXT("Name"))
							.DefaultLabel(LOCTEXT("ColName", "Display Name"))
							.FillWidth(0.25f)
						+ SHeaderRow::Column(TEXT("Type"))
							.DefaultLabel(LOCTEXT("ColType", "Item.Type"))
							.FillWidth(0.25f)
						+ SHeaderRow::Column(TEXT("Asset"))
							.DefaultLabel(LOCTEXT("ColAsset", "Asset"))
							.FillWidth(0.25f)
						+ SHeaderRow::Column(TEXT("Fragments"))
							.DefaultLabel(LOCTEXT("ColFragments", "Fragments"))
							.FillWidth(0.25f))
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Visibility_Lambda([this]
					{
						return FilteredItems.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
					})
					.Text_Lambda([this]
					{
						return AllItems.IsEmpty()
							? LOCTEXT("NoDefinitions", "No item definitions found. Create UPEPItemDefinition assets to populate this list.")
							: LOCTEXT("NoDefinitionsForType", "No item definitions match this Item.Type.");
					})
				]
			]
		]
	];

	RebuildCategoryButtons();
	RefreshPreview();
}

SPEPItemDefinitionBrowser::~SPEPItemDefinitionBrowser()
{
	if (DetailsWindow.IsValid() && FSlateApplication::IsInitialized())
	{
		DetailsWindow->RequestDestroyWindow();
	}
}

TSharedRef<ITableRow> SPEPItemDefinitionBrowser::OnGenerateRow(
	TWeakObjectPtr<UPEPItemDefinition> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SPEPItemDefinitionRow, OwnerTable).Item(Item);
}

void SPEPItemDefinitionBrowser::OnRowDoubleClicked(TWeakObjectPtr<UPEPItemDefinition> Item)
{
	if (!Item.IsValid())
	{
		return;
	}

	SelectedItem = Item;
	OpenDetailsWindow();
}

void SPEPItemDefinitionBrowser::OnSelectionChanged(
	TWeakObjectPtr<UPEPItemDefinition> Item,
	ESelectInfo::Type SelectInfo)
{
	SelectedItem = Item;

	if (DetailsWindow.IsValid())
	{
		UpdateDetailsWindow();
	}
	else
	{
		RefreshPreview();
	}
}

FReply SPEPItemDefinitionBrowser::OnOpenDetailsClicked()
{
	OpenDetailsWindow();
	return FReply::Handled();
}

FReply SPEPItemDefinitionBrowser::OnOpenAssetEditorClicked()
{
	UPEPItemDefinition* Definition = SelectedItem.Get();
	if (Definition && GEditor)
	{
		if (UAssetEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			Subsystem->OpenEditorForAsset(Definition);
		}
	}
	return FReply::Handled();
}

void SPEPItemDefinitionBrowser::OpenDetailsWindow()
{
	if (!SelectedItem.IsValid())
	{
		return;
	}

	if (DetailsWindow.IsValid())
	{
		UpdateDetailsWindow();
		DetailsWindow->BringToFront();
		return;
	}

	FPropertyEditorModule& PropertyModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = true;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsView = PropertyModule.CreateDetailView(DetailsViewArgs);
	DetailsView->OnFinishedChangingProperties().AddSP(
		this, &SPEPItemDefinitionBrowser::OnDefinitionPropertiesChanged);

	SAssignNew(PreviewContainer, SBox);

	/*
	 * Bound through the shared pointer rather than a lambda capturing this: the window is its own
	 * top-level object, and its destruction is only requested when the browser goes away.
	 */
	SAssignNew(DetailsWindow, SWindow)
		.Title(this, &SPEPItemDefinitionBrowser::GetDetailsWindowTitle)
		.ClientSize(FVector2D(820.f, 720.f))
		.MinWidth(520.f)
		.MinHeight(400.f)
		.SupportsMaximize(true)
		.SupportsMinimize(true)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			.Padding(6.f)
			[
				DetailsView.ToSharedRef()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(6.f, 0.f, 6.f, 6.f)
			[
				SNew(STextBlock).Text(LOCTEXT("PreviewHeader", "Container slot preview"))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(6.f, 0.f, 6.f, 6.f)
			[
				SNew(SBorder)
				.Padding(4.f)
				[
					SNew(SBox)
					.HeightOverride(180.f)
					[
						PreviewContainer.ToSharedRef()
					]
				]
			]
		];

	DetailsWindow->SetOnWindowClosed(
		FOnWindowClosed::CreateSP(this, &SPEPItemDefinitionBrowser::OnDetailsWindowClosed));
	UpdateDetailsWindow();

	FSlateApplication& SlateApplication = FSlateApplication::Get();
	const TSharedPtr<SWindow> ParentWindow = SlateApplication.GetActiveTopLevelWindow();
	if (ParentWindow.IsValid())
	{
		SlateApplication.AddWindowAsNativeChild(DetailsWindow.ToSharedRef(), ParentWindow.ToSharedRef(), true);
	}
	else
	{
		SlateApplication.AddWindow(DetailsWindow.ToSharedRef(), true);
	}
}

FText SPEPItemDefinitionBrowser::GetDetailsWindowTitle() const
{
	const UPEPItemDefinition* Definition = SelectedItem.Get();
	return Definition
		? FText::Format(LOCTEXT("DetailsWindowTitle", "Item Definition — {0}"), Definition->DisplayName)
		: LOCTEXT("DetailsWindowTitleEmpty", "Item Definition Details");
}

void SPEPItemDefinitionBrowser::OnDetailsWindowClosed(const TSharedRef<SWindow>& Window)
{
	(void)Window;
	DetailsWindow.Reset();
	DetailsView.Reset();
	PreviewContainer.Reset();
}

void SPEPItemDefinitionBrowser::OnDefinitionPropertiesChanged(const FPropertyChangedEvent& Event)
{
	(void)Event;
	if (!SelectedItem.IsValid())
	{
		return;
	}

	SelectedCategory = ExtractCategory(SelectedItem.Get());
	RefreshItemList();
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

void SPEPItemDefinitionBrowser::UpdateDetailsWindow()
{
	if (DetailsView.IsValid())
	{
		DetailsView->SetObject(SelectedItem.Get());
	}
	RefreshPreview();
}

void SPEPItemDefinitionBrowser::RefreshPreview()
{
	if (PreviewContainer.IsValid())
	{
		PreviewContainer->SetContent(BuildPreviewWidget(SelectedItem.Get()));
	}
}

TSharedRef<SWidget> SPEPItemDefinitionBrowser::BuildPreviewWidget(
	const UPEPItemDefinition* Definition) const
{
	if (!Definition)
	{
		return SNew(STextBlock)
			.Text(LOCTEXT("NoSelection", "Select an item above to preview its container slots."));
	}

	TArray<const FPEPItemFragment_Container*> ContainerFragments;
	for (const FInstancedStruct& Fragment : Definition->GetDefinitionFragments())
	{
		if (const FPEPItemFragment_Container* Container =
			Fragment.GetPtr<FPEPItemFragment_Container>())
		{
			ContainerFragments.Add(Container);
		}
	}

	if (ContainerFragments.IsEmpty())
	{
		return SNew(STextBlock)
			.Text(LOCTEXT("NoContainer", "This item has no Container fragment."));
	}

	/*
	 * FPEPContainerSlotDefinition carries a size (GridSize) but no canvas position — unlike
	 * AGIS's ContainerGroup, which stores one. This lays slots out left to right, wrapping as
	 * needed; it is a scale-accurate read of the numbers, not a spatial mockup of the garment.
	 */
	constexpr float CellSize = 24.f;

	const TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);

	for (const FPEPItemFragment_Container* Container : ContainerFragments)
	{
		const TSharedRef<SWrapBox> Wrap = SNew(SWrapBox);

		for (const FPEPContainerSlotDefinition& Slot : Container->Slots)
		{
			const FIntPoint Size = Slot.Mode == EPEPContainerSlotMode::Grid
				? Slot.GridSize
				: FIntPoint(1, 1);

			const FLinearColor BorderColor = Slot.Purpose == EPEPContainerSlotPurpose::Attachment
				? FLinearColor(0.85f, 0.55f, 0.15f)
				: FLinearColor(0.2f, 0.5f, 0.85f);

			Wrap->AddSlot()
				.Padding(4.f)
				[
					SNew(SBox)
					.WidthOverride(FMath::Max(1, Size.X) * CellSize)
					.HeightOverride(FMath::Max(1, Size.Y) * CellSize)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()
						[
							SNew(SBorder)
							.BorderBackgroundColor(BorderColor)
						]
						+ SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromName(Slot.SlotName))
							.AutoWrapText(true)
							.Justification(ETextJustify::Center)
						]
					]
				];
		}

		Root->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 8.f)
			[
				Wrap
			];
	}

	return SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			Root
		];
}

FReply SPEPItemDefinitionBrowser::OnRefreshClicked()
{
	RefreshItemList();
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
	return FReply::Handled();
}

void SPEPItemDefinitionBrowser::RefreshItemList()
{
	AllItems.Reset();

	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	TArray<FAssetData> AssetDataList;
	AssetRegistryModule.Get().GetAssetsByClass(
		UPEPItemDefinition::StaticClass()->GetClassPathName(),
		AssetDataList,
		/*bSearchSubClasses=*/true);

	AllItems.Reserve(AssetDataList.Num());
	for (const FAssetData& AssetData : AssetDataList)
	{
		if (UPEPItemDefinition* Definition = Cast<UPEPItemDefinition>(AssetData.GetAsset()))
		{
			AllItems.Add(Definition);
		}
	}

	AllItems.Sort(
		[](const TWeakObjectPtr<UPEPItemDefinition>& A, const TWeakObjectPtr<UPEPItemDefinition>& B)
		{
			const UPEPItemDefinition* DefA = A.Get();
			const UPEPItemDefinition* DefB = B.Get();
			if (!DefA || !DefB)
			{
				return DefA != nullptr;
			}
			return DefA->DisplayName.CompareTo(DefB->DisplayName) < 0;
		});

	TSet<FName> CategorySet;
	for (const TWeakObjectPtr<UPEPItemDefinition>& WeakDefinition : AllItems)
	{
		const FName Category = ExtractCategory(WeakDefinition.Get());
		if (!Category.IsNone())
		{
			CategorySet.Add(Category);
		}
	}

	Categories = CategorySet.Array();
	Categories.Sort(
		[](const FName& A, const FName& B)
		{
			return A.ToString() < B.ToString();
		});

	/* The category the filter was on may no longer exist (its last item was deleted/retagged). */
	if (!SelectedCategory.IsNone() && !Categories.Contains(SelectedCategory))
	{
		SelectedCategory = NAME_None;
	}

	RebuildCategoryButtons();
	ApplyFilter();
}

void SPEPItemDefinitionBrowser::RebuildCategoryButtons()
{
	if (!CategoryBox.IsValid())
	{
		return;
	}

	CategoryBox->ClearChildren();

	const FCheckBoxStyle& ToggleStyle =
		FCoreStyle::Get().GetWidgetStyle<FCheckBoxStyle>(TEXT("ToggleButtonCheckbox"));

	auto AddCategoryButton = [this, &ToggleStyle](const FName Category, const FText& Label)
	{
		CategoryBox->AddSlot()
			.AutoHeight()
			.Padding(2.f)
			[
				SNew(SCheckBox)
				.Style(&ToggleStyle)
				.HAlign(HAlign_Fill)
				.Padding(FMargin(6.f, 4.f))
				.IsChecked(this, &SPEPItemDefinitionBrowser::GetCategoryCheckState, Category)
				.OnCheckStateChanged(
					this,
					&SPEPItemDefinitionBrowser::OnCategoryCheckStateChanged,
					Category)
				[
					SNew(STextBlock).Text(Label)
				]
			];
	};

	AddCategoryButton(NAME_None, LOCTEXT("CategoryAll", "All items"));

	for (const FName& Category : Categories)
	{
		AddCategoryButton(
			Category,
			FText::Format(LOCTEXT("CategoryLabel", "Item.Type.{0}"), FText::FromName(Category)));
	}
}

void SPEPItemDefinitionBrowser::ApplyFilter()
{
	if (SelectedCategory.IsNone())
	{
		FilteredItems = AllItems;
		return;
	}

	FilteredItems.Reset();
	for (const TWeakObjectPtr<UPEPItemDefinition>& WeakDefinition : AllItems)
	{
		if (ExtractCategory(WeakDefinition.Get()) == SelectedCategory)
		{
			FilteredItems.Add(WeakDefinition);
		}
	}
}

ECheckBoxState SPEPItemDefinitionBrowser::GetCategoryCheckState(const FName Category) const
{
	return SelectedCategory == Category ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SPEPItemDefinitionBrowser::OnCategoryCheckStateChanged(
	const ECheckBoxState NewState,
	const FName Category)
{
	if (NewState != ECheckBoxState::Checked || SelectedCategory == Category)
	{
		return;
	}

	SelectedCategory = Category;
	ApplyFilter();
	if (SelectedItem.IsValid() && !FilteredItems.Contains(SelectedItem))
	{
		SelectedItem.Reset();
		if (DetailsView.IsValid())
		{
			DetailsView->SetObject(nullptr);
		}
		RefreshPreview();
	}

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

TSharedRef<SDockTab> SPEPItemDefinitionBrowser::SpawnTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SPEPItemDefinitionBrowser)
		];
}

#undef LOCTEXT_NAMESPACE
