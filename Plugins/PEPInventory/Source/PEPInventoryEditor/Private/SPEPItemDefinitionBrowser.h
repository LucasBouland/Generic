#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class UPEPItemDefinition;
class ITableRow;
class STableViewBase;
class SDockTab;
class SHorizontalBox;
class SVerticalBox;
class SBox;
class FSpawnTabArgs;
class IDetailsView;
class SWindow;
struct FPropertyChangedEvent;

/**
 * A browser over every UPEPItemDefinition in the project: display name, Item.Type path, asset
 * name, and capability fragments. Item.Type root categories filter the list. Selecting a row
 * and opening Details (or double-clicking it) shows an editable Property Editor in a separate
 * floating window, alongside the container-slot preview.
 *
 * Reads live from the Asset Registry rather than caching a snapshot, so Refresh always reflects
 * whatever definitions currently exist on disk.
 *
 * A vertical type list filters by the item's OwnedTags: Item.Type.Weapon.Rifle displays the full
 * Weapon.Rifle path and is listed under Item.Type.Weapon. Nothing new to author — this reads the
 * tag vocabulary the plugin already asks every item to carry.
 */
class SPEPItemDefinitionBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPEPItemDefinitionBrowser)
	{
	}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SPEPItemDefinitionBrowser() override;

	static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);

	static const FName TabId;

private:
	TSharedRef<ITableRow> OnGenerateRow(
		TWeakObjectPtr<UPEPItemDefinition> Item,
		const TSharedRef<STableViewBase>& OwnerTable);

	void OnRowDoubleClicked(TWeakObjectPtr<UPEPItemDefinition> Item);
	void OnSelectionChanged(
		TWeakObjectPtr<UPEPItemDefinition> Item,
		ESelectInfo::Type SelectInfo);
	FReply OnRefreshClicked();
	FReply OnOpenDetailsClicked();
	FReply OnOpenAssetEditorClicked();
	void OpenDetailsWindow();
	FText GetDetailsWindowTitle() const;
	void OnDetailsWindowClosed(const TSharedRef<SWindow>& Window);
	void OnDefinitionPropertiesChanged(const FPropertyChangedEvent& Event);
	void UpdateDetailsWindow();

	/** Rebuilds the slot-layout preview for whatever is currently selected. */
	void RefreshPreview();
	TSharedRef<SWidget> BuildPreviewWidget(const UPEPItemDefinition* Definition) const;

	/** Re-queries the Asset Registry, rebuilds the category buttons, then re-applies the filter. */
	void RefreshItemList();

	/** Rebuilds the Item.Type navigation list from the current Categories list. */
	void RebuildCategoryButtons();

	/** Narrows AllItems into FilteredItems according to SelectedCategory. */
	void ApplyFilter();

	ECheckBoxState GetCategoryCheckState(FName Category) const;
	void OnCategoryCheckStateChanged(ECheckBoxState NewState, FName Category);

	/** Every definition found by the last Refresh, unfiltered. */
	TArray<TWeakObjectPtr<UPEPItemDefinition>> AllItems;

	/** What the list view actually shows: AllItems narrowed to SelectedCategory. */
	TArray<TWeakObjectPtr<UPEPItemDefinition>> FilteredItems;

	/** Distinct root types after "Item.Type.", found across AllItems and sorted alphabetically. */
	TArray<FName> Categories;

	/** NAME_None means "All" — no filter applied. */
	FName SelectedCategory;

	TSharedPtr<SVerticalBox> CategoryBox;
	TSharedPtr<SListView<TWeakObjectPtr<UPEPItemDefinition>>> ListView;

	/** Currently selected row, edited in the separate details window. */
	TWeakObjectPtr<UPEPItemDefinition> SelectedItem;
	TSharedPtr<SBox> PreviewContainer;

	/** Floating, editable details window for SelectedItem. */
	TSharedPtr<SWindow> DetailsWindow;
	TSharedPtr<IDetailsView> DetailsView;
};
