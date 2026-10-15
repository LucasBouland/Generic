#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;

/**
 * Replaces the raw array row for one FPEPContainerSlotDefinition with a compact summary line —
 * name, grid size, purpose, owner/other access shorthand — plus an "Apply Preset" button. Still
 * expandable to the full, unmodified property list underneath: nothing is hidden, just given a
 * readable header so a jacket with six pockets is scannable at a glance.
 */
class FPEPContainerSlotDefinitionCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> PropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> PropertyHandle,
		class IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	FText GetSummaryText() const;
	TSharedRef<class SWidget> BuildPresetMenu();
	void OnApplyPreset(FName PresetName);

	/** The handle for this row's whole FPEPContainerSlotDefinition struct. */
	TSharedPtr<IPropertyHandle> StructHandle;
};
