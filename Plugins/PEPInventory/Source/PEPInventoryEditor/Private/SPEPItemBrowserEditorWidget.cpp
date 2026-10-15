#include "PEPItemBrowserEditorWidget.h"

#include "Editor.h"
#include "IDetailsView.h"
#include "Item/Actor/PEPItemWorldActor.h"
#include "Item/PEPItemDefinition.h"
#include "PropertyEditorModule.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "UObject/UnrealType.h"
#include "SPEPItemDefinitionBrowser.h"

#define LOCTEXT_NAMESPACE "PEPItemBrowserEditorWidget"

namespace
{
	FText GetSoftObjectPathText(const APEPItemWorldActor* Actor, const FName PropertyName)
	{
		if (!Actor)
		{
			return FText::GetEmpty();
		}

		const FSoftObjectProperty* Property = CastField<FSoftObjectProperty>(
			APEPItemWorldActor::StaticClass()->FindPropertyByName(PropertyName));
		if (!Property)
		{
			return FText::GetEmpty();
		}

		const FSoftObjectPtr& ObjectPtr = Property->GetPropertyValue_InContainer(Actor);
		return ObjectPtr.IsNull()
			? FText::GetEmpty()
			: FText::FromString(ObjectPtr.ToSoftObjectPath().ToString());
	}

	class SPEPItemWorldActorBrowser final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SPEPItemWorldActorBrowser) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			FPropertyEditorModule& PropertyModule =
				FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
			FDetailsViewArgs DetailsArgs;
			DetailsArgs.bAllowSearch = true;
			DetailsArgs.bHideSelectionTip = true;
			DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
			DetailsView = PropertyModule.CreateDetailView(DetailsArgs);
			DetailsView->SetIsPropertyEditingEnabledDelegate(
				FIsPropertyEditingEnabled::CreateLambda([] { return false; }));

			ChildSlot
			[
				SNew(SSplitter)
				.Orientation(Orient_Horizontal)
				+ SSplitter::Slot()
				.Value(0.4f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(6.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(LOCTEXT("RefreshActors", "Refresh"))
							.OnClicked(this, &SPEPItemWorldActorBrowser::OnRefreshClicked)
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						.VAlign(VAlign_Center)
						.Padding(8.f, 0.f)
						[
							SNew(STextBlock)
							.Text_Lambda([this]
							{
								return FText::Format(
									LOCTEXT("ActorCount", "{0} item actor(s) in the current level"),
									FText::AsNumber(Actors.Num()));
							})
						]
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					.Padding(4.f)
					[
						SAssignNew(ListView, SListView<TWeakObjectPtr<APEPItemWorldActor>>)
						.ListItemsSource(&Actors)
						.SelectionMode(ESelectionMode::Single)
						.OnGenerateRow(this, &SPEPItemWorldActorBrowser::GenerateRow)
						.OnSelectionChanged(this, &SPEPItemWorldActorBrowser::OnSelectionChanged)
						.OnMouseButtonDoubleClick(this, &SPEPItemWorldActorBrowser::OnRowDoubleClicked)
						.HeaderRow(
							SNew(SHeaderRow)
							+ SHeaderRow::Column(TEXT("Actor"))
								.DefaultLabel(LOCTEXT("ActorColumn", "Actor")))
					]
				]
				+ SSplitter::Slot()
				.Value(0.6f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(8.f)
					[
						SNew(SBorder)
						.Padding(8.f)
						[
							SNew(STextBlock)
							.AutoWrapText(true)
							.Text_Lambda([this] { return BuildSelectedActorSummary(); })
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(8.f, 0.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(LOCTEXT("SelectActor", "Select in Level"))
							.IsEnabled_Lambda([this] { return SelectedActor.IsValid(); })
							.OnClicked(this, &SPEPItemWorldActorBrowser::OnSelectClicked)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(6.f, 0.f)
						[
							SNew(SButton)
							.Text(LOCTEXT("FocusActor", "Focus Viewport"))
							.IsEnabled_Lambda([this] { return SelectedActor.IsValid(); })
							.OnClicked(this, &SPEPItemWorldActorBrowser::OnFocusClicked)
						]
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					.Padding(6.f)
					[
						DetailsView.ToSharedRef()
					]
				]
			];

			RefreshActors();
		}

	private:
		TSharedRef<ITableRow> GenerateRow(
			TWeakObjectPtr<APEPItemWorldActor> Actor,
			const TSharedRef<STableViewBase>& OwnerTable)
		{
			return SNew(STableRow<TWeakObjectPtr<APEPItemWorldActor>>, OwnerTable)
			[
				SNew(STextBlock)
				.Text_Lambda([Actor]
				{
					const APEPItemWorldActor* ItemActor = Actor.Get();
					return ItemActor ? FText::FromString(ItemActor->GetActorLabel()) : FText::GetEmpty();
				})
			];
		}

		void RefreshActors()
		{
			Actors.Reset();
			if (GEditor)
			{
				if (UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>())
				{
					for (AActor* Actor : ActorSubsystem->GetAllLevelActors())
					{
						if (APEPItemWorldActor* ItemActor = Cast<APEPItemWorldActor>(Actor))
						{
							Actors.Add(ItemActor);
						}
					}
				}
			}

			Actors.Sort([](const TWeakObjectPtr<APEPItemWorldActor>& A, const TWeakObjectPtr<APEPItemWorldActor>& B)
			{
				const APEPItemWorldActor* ActorA = A.Get();
				const APEPItemWorldActor* ActorB = B.Get();
				return ActorA && ActorB
					? ActorA->GetActorLabel().Compare(ActorB->GetActorLabel()) < 0
					: A.IsValid() != B.IsValid();
			});

			SelectedActor.Reset();
			if (DetailsView.IsValid())
			{
				DetailsView->SetObject(nullptr, true);
			}
			if (ListView.IsValid())
			{
				ListView->ClearSelection();
				ListView->RequestListRefresh();
			}
		}

		void OnSelectionChanged(TWeakObjectPtr<APEPItemWorldActor> Actor, ESelectInfo::Type)
		{
			SelectedActor = Actor;
			if (DetailsView.IsValid())
			{
				DetailsView->SetObject(Actor.Get(), true);
			}
		}

		void OnRowDoubleClicked(TWeakObjectPtr<APEPItemWorldActor> Actor)
		{
			SelectedActor = Actor;
			SelectActorInLevel();
			FocusActorInViewport();
		}

		FText BuildSelectedActorSummary() const
		{
			const APEPItemWorldActor* Actor = SelectedActor.Get();
			if (!Actor)
			{
				return LOCTEXT("SelectItemActorPrompt", "Select an item actor to inspect it. Double-click a row to select and focus it in the level.");
			}

			const UPEPItemDefinition* Definition = Actor->GetItemDefinition();
			FText DefinitionDescription = Definition
				? FText::Format(LOCTEXT("RuntimeDefinition", "{0} ({1})"), Definition->DisplayName,
					FText::FromString(Definition->GetPathName()))
				: GetSoftObjectPathText(Actor, TEXT("SpawnItemDefinition"));
			if (DefinitionDescription.IsEmpty())
			{
				DefinitionDescription = GetSoftObjectPathText(Actor, TEXT("SpawnPreset"));
			}
			if (DefinitionDescription.IsEmpty())
			{
				DefinitionDescription = LOCTEXT("NoDefinitionSource", "No definition or preset available");
			}
			return FText::Format(
				LOCTEXT("SelectedActorSummary", "{0}\nClass: {1}\nDefinition / spawn preset: {2}\nLocation: {3}"),
				FText::FromString(Actor->GetActorLabel()),
				FText::FromString(Actor->GetClass()->GetName()),
				DefinitionDescription,
				FText::FromString(Actor->GetActorLocation().ToString()));
		}

		FReply OnRefreshClicked()
		{
			RefreshActors();
			return FReply::Handled();
		}

		FReply OnSelectClicked()
		{
			SelectActorInLevel();
			return FReply::Handled();
		}

		void SelectActorInLevel() const
		{
			if (!GEditor)
			{
				return;
			}
			if (UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>())
			{
				if (APEPItemWorldActor* Actor = SelectedActor.Get())
				{
					ActorSubsystem->SetSelectedLevelActors({ Actor });
				}
			}
		}

		FReply OnFocusClicked()
		{
			FocusActorInViewport();
			return FReply::Handled();
		}

		void FocusActorInViewport() const
		{
			if (GEditor)
			{
				if (APEPItemWorldActor* Actor = SelectedActor.Get())
				{
					GEditor->MoveViewportCamerasToActor(*Actor, false);
				}
			}
		}

		TArray<TWeakObjectPtr<APEPItemWorldActor>> Actors;
		TWeakObjectPtr<APEPItemWorldActor> SelectedActor;
		TSharedPtr<SListView<TWeakObjectPtr<APEPItemWorldActor>>> ListView;
		TSharedPtr<IDetailsView> DetailsView;
	};

	class SPEPItemBrowserEditorRoot final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SPEPItemBrowserEditorRoot) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			ChildSlot
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(6.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("DefinitionsTab", "Item Definitions"))
						.OnClicked(this, &SPEPItemBrowserEditorRoot::ShowDefinitions)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("LevelItemsTab", "Items in Level"))
						.OnClicked(this, &SPEPItemBrowserEditorRoot::ShowLevelItems)
					]
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SAssignNew(ContentSwitcher, SWidgetSwitcher)
					+ SWidgetSwitcher::Slot()
					[
						SNew(SPEPItemDefinitionBrowser)
					]
					+ SWidgetSwitcher::Slot()
					[
						SNew(SPEPItemWorldActorBrowser)
					]
				]
			];
		}

	private:
		FReply ShowDefinitions()
		{
			ContentSwitcher->SetActiveWidgetIndex(0);
			return FReply::Handled();
		}

		FReply ShowLevelItems()
		{
			ContentSwitcher->SetActiveWidgetIndex(1);
			return FReply::Handled();
		}

		TSharedPtr<SWidgetSwitcher> ContentSwitcher;
	};
}

TSharedRef<SWidget> UPEPItemBrowserEditorWidget::RebuildWidget()
{
	NativeContent = SNew(SPEPItemBrowserEditorRoot);
	return NativeContent.ToSharedRef();
}

void UPEPItemBrowserEditorWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	NativeContent.Reset();
}

#undef LOCTEXT_NAMESPACE
