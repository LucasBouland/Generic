#include "PEPItemFragmentContainerCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PEPItemFragmentContainerCustomization"

TSharedRef<IPropertyTypeCustomization> FPEPItemFragmentContainerCustomization::MakeInstance()
{
	return MakeShared<FPEPItemFragmentContainerCustomization>();
}

void FPEPItemFragmentContainerCustomization::CustomizeHeader(
	const TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		];
}

void FPEPItemFragmentContainerCustomization::CustomizeChildren(
	const TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	SlotsHandle = PropertyHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FPEPItemFragment_Container, Slots));

	ChildBuilder.AddCustomRow(LOCTEXT("BulkAddSearch", "Add Slots Bulk Pocket"))
		.NameContent()
		[
			SNew(STextBlock).Text(LOCTEXT("BulkAddLabel", "Add Slots"))
		]
		.ValueContent()
		.MinDesiredWidth(420.f)
		.MaxDesiredWidth(700.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SEditableTextBox)
				.Text(FText::FromString(PendingPrefix))
				.HintText(LOCTEXT("PrefixHint", "Name prefix, e.g. Pocket"))
				.OnTextChanged(this, &FPEPItemFragmentContainerCustomization::OnPendingPrefixChanged)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SBox)
				.WidthOverride(60.f)
				[
					SNew(SNumericEntryBox<int32>)
					.MinValue(1)
					.MaxValue(64)
					.Value(this, &FPEPItemFragmentContainerCustomization::GetPendingCount)
					.OnValueChanged(this, &FPEPItemFragmentContainerCustomization::OnPendingCountChanged)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("AddSlotsButton", "Add"))
				.OnClicked(this, &FPEPItemFragmentContainerCustomization::OnAddSlotsClicked)
			]
		];

	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);

	for (uint32 Index = 0; Index < NumChildren; ++Index)
	{
		const TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(Index);
		if (ChildHandle.IsValid())
		{
			ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
	}
}

TOptional<int32> FPEPItemFragmentContainerCustomization::GetPendingCount() const
{
	return PendingCount;
}

void FPEPItemFragmentContainerCustomization::OnPendingCountChanged(const int32 NewCount)
{
	PendingCount = NewCount;
}

void FPEPItemFragmentContainerCustomization::OnPendingPrefixChanged(const FText& NewText)
{
	PendingPrefix = NewText.ToString();
}

FReply FPEPItemFragmentContainerCustomization::OnAddSlotsClicked()
{
	if (!SlotsHandle.IsValid())
	{
		return FReply::Handled();
	}

	const TSharedPtr<IPropertyHandleArray> ArrayHandle = SlotsHandle->AsArray();
	if (!ArrayHandle.IsValid())
	{
		return FReply::Handled();
	}

	const FString Prefix = PendingPrefix.IsEmpty() ? TEXT("Slot") : PendingPrefix;
	const int32 Count = FMath::Max(1, PendingCount);

	uint32 NumBefore = 0;
	ArrayHandle->GetNumElements(NumBefore);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		ArrayHandle->AddItem();
	}

	uint32 NumAfter = 0;
	ArrayHandle->GetNumElements(NumAfter);

	for (uint32 ElementIndex = NumBefore; ElementIndex < NumAfter; ++ElementIndex)
	{
		const TSharedPtr<IPropertyHandle> ElementHandle = SlotsHandle->GetChildHandle(ElementIndex);
		if (!ElementHandle.IsValid())
		{
			continue;
		}

		const TSharedPtr<IPropertyHandle> NameHandle = ElementHandle->GetChildHandle(
			GET_MEMBER_NAME_CHECKED(FPEPContainerSlotDefinition, SlotName));

		if (NameHandle.IsValid())
		{
			const FName NewName(*FString::Printf(TEXT("%s_%d"), *Prefix, ElementIndex + 1));
			NameHandle->SetValue(NewName);
		}
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
