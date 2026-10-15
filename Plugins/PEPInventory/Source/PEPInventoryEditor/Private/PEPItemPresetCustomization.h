#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IPropertyHandle;
class IPropertyUtilities;
class UPEPItemDefinition;
class UPEPItemPreset;

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

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FText GetValidationText() const;
	FSlateColor GetValidationColor() const;

	TSharedRef<class SWidget> BuildAddChildMenu();
	void AddChildEntry(FName SlotName, UPEPItemPreset* PresetSource, UPEPItemDefinition* ItemSource);

	TWeakObjectPtr<UPEPItemPreset> EditedPreset;
	TSharedPtr<IPropertyHandle> ChildrenHandle;
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
};
