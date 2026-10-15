#include "PEPItemPresetChildCustomization.h"

#include "DetailWidgetRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "Item/PEPItemContainerRules.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemPreset.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PEPItemPresetChildCustomization"

TSharedRef<IPropertyTypeCustomization> FPEPItemPresetChildCustomization::MakeInstance()
{
	return MakeShared<FPEPItemPresetChildCustomization>();
}

void FPEPItemPresetChildCustomization::CustomizeHeader(
	const TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	StructHandle = PropertyHandle;
	SlotNameHandle = PropertyHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FPEPItemPresetChild, ContainerSlotName));

	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(420.f)
		.MaxDesiredWidth(700.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(STextBlock)
				.Text(this, &FPEPItemPresetChildCustomization::GetSummaryText)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SComboButton)
				.ButtonContent()
				[
					SNew(STextBlock).Text(this, &FPEPItemPresetChildCustomization::GetSlotButtonText)
				]
				.OnGetMenuContent(this, &FPEPItemPresetChildCustomization::BuildSlotMenu)
			]
		];
}

void FPEPItemPresetChildCustomization::CustomizeChildren(
	const TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
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

FText FPEPItemPresetChildCustomization::GetSummaryText() const
{
	if (!StructHandle.IsValid())
	{
		return FText::GetEmpty();
	}

	void* RawData = nullptr;
	if (StructHandle->GetValueData(RawData) != FPropertyAccess::Success || !RawData)
	{
		return FText::GetEmpty();
	}

	const FPEPItemPresetChild* Child = static_cast<const FPEPItemPresetChild*>(RawData);
	if (Child->Preset)
	{
		return FText::FromString(FString::Printf(TEXT("Preset: %s"), *Child->Preset->GetName()));
	}
	if (Child->ItemDefinition)
	{
		return FText::FromString(FString::Printf(TEXT("Item: %s"), *Child->ItemDefinition->GetName()));
	}

	return LOCTEXT("NoSource", "<no source set>");
}

FText FPEPItemPresetChildCustomization::GetSlotButtonText() const
{
	FName Current = NAME_None;
	if (SlotNameHandle.IsValid())
	{
		SlotNameHandle->GetValue(Current);
	}

	return Current.IsNone() ? LOCTEXT("NoSlot", "<no slot>") : FText::FromName(Current);
}

UPEPItemDefinition* FPEPItemPresetChildCustomization::GetOwningRootDefinition() const
{
	if (!StructHandle.IsValid())
	{
		return nullptr;
	}

	TArray<UObject*> OuterObjects;
	StructHandle->GetOuterObjects(OuterObjects);
	if (OuterObjects.Num() != 1)
	{
		return nullptr;
	}

	const UPEPItemPreset* OwningPreset = Cast<UPEPItemPreset>(OuterObjects[0]);
	return OwningPreset ? OwningPreset->RootDefinition : nullptr;
}

TSharedRef<SWidget> FPEPItemPresetChildCustomization::BuildSlotMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	const UPEPItemDefinition* RootDefinition = GetOwningRootDefinition();
	if (!RootDefinition)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("NoRoot", "Owning preset has no Root Definition"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction());
		return MenuBuilder.MakeWidget();
	}

	/* Read in place; the Blueprint accessor copies every slot just to list their names. */
	const TConstArrayView<FPEPContainerSlotDefinition> Slots =
		PEPItemContainerRules::FindContainerSlots(RootDefinition);

	if (Slots.IsEmpty())
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("NoSlots", "Root Definition declares no container slots"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction());
	}
	else
	{
		for (const FPEPContainerSlotDefinition& Slot : Slots)
		{
			const FName SlotName = Slot.SlotName;
			MenuBuilder.AddMenuEntry(
				FText::FromName(SlotName),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(
					this,
					&FPEPItemPresetChildCustomization::OnSlotSelected,
					SlotName)));
		}
	}

	return MenuBuilder.MakeWidget();
}

void FPEPItemPresetChildCustomization::OnSlotSelected(const FName NewSlot)
{
	if (SlotNameHandle.IsValid())
	{
		SlotNameHandle->SetValue(NewSlot);
	}
}

#undef LOCTEXT_NAMESPACE
