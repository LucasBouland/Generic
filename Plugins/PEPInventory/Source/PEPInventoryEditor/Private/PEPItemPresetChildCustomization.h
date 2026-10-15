#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;

/**
 * Replaces ContainerSlotName's free-typed FName with a dropdown scoped to the slots actually
 * declared on the owning preset's RootDefinition container fragment, so a fat-fingered slot name
 * fails at edit time instead of silently dropping the child the first time CreateItemTree runs.
 */
class FPEPItemPresetChildCustomization : public IPropertyTypeCustomization
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
	FText GetSlotButtonText() const;
	TSharedRef<class SWidget> BuildSlotMenu();
	void OnSlotSelected(FName NewSlot);

	/** The RootDefinition of the UPEPItemPreset that owns this row's Children array, if any. */
	class UPEPItemDefinition* GetOwningRootDefinition() const;

	/** The handle for this row's whole FPEPItemPresetChild struct. */
	TSharedPtr<IPropertyHandle> StructHandle;
	TSharedPtr<IPropertyHandle> SlotNameHandle;
};
