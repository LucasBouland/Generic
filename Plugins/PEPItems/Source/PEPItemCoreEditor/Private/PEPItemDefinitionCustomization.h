#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IPropertyHandle;
class IPropertyUtilities;
class UPEPItemDefinition;
class UScriptStruct;

/**
 * Replaces the default "Add" experience for DefinitionFragments with a single "Add Fragment"
 * menu, built by enumerating every FPEPItemFragment-derived struct known to the reflection
 * system — nothing to keep in sync by hand when a new fragment type is added elsewhere in the
 * project. A fragment already present (per the same family-conflict rule IsDataValid enforces)
 * is disabled rather than offered again, so the menu can't create a conflict IsDataValid would
 * otherwise only catch after the fact.
 *
 * The array itself still renders normally below this row — every existing way to reorder,
 * inspect or remove a fragment stays exactly as it was.
 *
 * A second row, "Apply Bundle", offers the starter kits authored in
 * UPEPItemSettings::FragmentBundles — a named set of pre-configured fragments for a common item
 * archetype. Applying one adds only the fragments the item does not already carry; anything that
 * would conflict with what is already configured is skipped rather than overwritten.
 */
class FPEPItemDefinitionCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	TSharedRef<class SWidget> BuildAddFragmentMenu();
	void OnAddFragment(const UScriptStruct* FragmentStruct);
	bool IsFragmentPresent(const UScriptStruct* FragmentStruct) const;

	TSharedRef<class SWidget> BuildApplyBundleMenu();
	void OnApplyBundle(FName BundleName);

	TWeakObjectPtr<UPEPItemDefinition> EditedDefinition;
	TSharedPtr<IPropertyHandle> FragmentsHandle;
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
};
