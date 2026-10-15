#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "UObject/SoftObjectPath.h"

class FMenuBuilder;
class IPropertyHandle;
class IPropertyUtilities;
class SWidget;
class UPEPItemDefinition;
class UPEPItemPreset;
struct FPropertyChangedEvent;

/**
 * Adds two rows above the Children array on a preset: a live editor-preview build result, so a
 * broken recipe is visible without saving and re-validating, and an "Add Child" menu that only
 * offers presets and item definitions the currently declared slots would actually accept — the
 * same tag rules CreateNode enforces at build time, surfaced before the asset is even saved.
 *
 * The array itself still renders normally below these rows, with FPEPItemPresetChildCustomization
 * handling each element's own slot dropdown.
 */
class FPEPItemPresetCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual ~FPEPItemPresetCustomization() override;

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	/**
	 * Builds the preview once and keeps the answer. The row used to build the whole tree twice per
	 * painted frame — once for its text, once for its colour — allocating fresh item ids each time.
	 */
	void RefreshBuildResult();

	/** Rebuilds the cached answer when the preset, or anything a preset build reads, is edited. */
	void HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);

	FText GetValidationText() const;
	FSlateColor GetValidationColor() const;

	TSharedRef<SWidget> BuildAddChildMenu();
	void FillSlotSubMenu(
		FMenuBuilder& SubMenuBuilder,
		FName SlotName,
		TWeakObjectPtr<const UPEPItemDefinition> RootDefinition);

	/** Sources are paths, loaded only once picked: building the menu loads no preset at all. */
	void AddChildEntry(FName SlotName, FSoftObjectPath PresetSource, FSoftObjectPath ItemSource);

	TWeakObjectPtr<UPEPItemPreset> EditedPreset;
	TSharedPtr<IPropertyHandle> ChildrenHandle;
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
	FDelegateHandle ObjectPropertyChangedHandle;
	FText CachedBuildText;
	bool bCachedBuildSucceeded = false;
};
