#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;
class IPropertyHandleArray;

/**
 * Adds a "prefix + count -> Add" toolbar above a Container fragment's Slots array, so a jacket
 * that needs six near-identical pockets doesn't mean six manual "Add" clicks and six rounds of
 * typing a name. Everything else in the fragment renders exactly as it would without this
 * customization.
 */
class FPEPItemFragmentContainerCustomization : public IPropertyTypeCustomization
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
	class FReply OnAddSlotsClicked();
	TOptional<int32> GetPendingCount() const;
	void OnPendingCountChanged(int32 NewCount);
	void OnPendingPrefixChanged(const FText& NewText);

	/** The Slots array property on the fragment this instance is currently customizing. */
	TSharedPtr<IPropertyHandle> SlotsHandle;

	FString PendingPrefix = TEXT("Pocket");
	int32 PendingCount = 1;
};
