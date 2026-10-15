#include "PEPContainerSlotDefinitionCustomization.h"

#include "DetailWidgetRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Inventory/PEPInventorySettings.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PEPContainerSlotDefinitionCustomization"

TSharedRef<IPropertyTypeCustomization> FPEPContainerSlotDefinitionCustomization::MakeInstance()
{
	return MakeShared<FPEPContainerSlotDefinitionCustomization>();
}

void FPEPContainerSlotDefinitionCustomization::CustomizeHeader(
	const TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	StructHandle = PropertyHandle;

	const TSharedPtr<IPropertyHandle> NameHandle = PropertyHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FPEPContainerSlotDefinition, SlotName));

	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(520.f)
		.MaxDesiredWidth(900.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 0.f, 8.f, 0.f)
			[
				NameHandle.IsValid()
					? NameHandle->CreatePropertyValueWidget()
					: SNullWidget::NullWidget
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(STextBlock)
				.Text(this, &FPEPContainerSlotDefinitionCustomization::GetSummaryText)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SComboButton)
				.ButtonContent()
				[
					SNew(STextBlock).Text(LOCTEXT("ApplyPreset", "Apply Preset"))
				]
				.OnGetMenuContent(this, &FPEPContainerSlotDefinitionCustomization::BuildPresetMenu)
			]
		];
}

void FPEPContainerSlotDefinitionCustomization::CustomizeChildren(
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

FText FPEPContainerSlotDefinitionCustomization::GetSummaryText() const
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

	const FPEPContainerSlotDefinition* Slot =
		static_cast<const FPEPContainerSlotDefinition*>(RawData);

	FString Summary = Slot->Mode == EPEPContainerSlotMode::Grid
		? FString::Printf(TEXT("Grid %dx%d"), Slot->GridSize.X, Slot->GridSize.Y)
		: FString(TEXT("Single"));

	if (Slot->Purpose == EPEPContainerSlotPurpose::Attachment)
	{
		Summary += TEXT(" · Attachment");
	}

	Summary += FString::Printf(
		TEXT(" · Owner %s%s%s · Other %s%s%s"),
		Slot->bOwnerCanTake ? TEXT("T") : TEXT("-"),
		Slot->bOwnerCanPut ? TEXT("P") : TEXT("-"),
		Slot->bOwnerCanMoveInside ? TEXT("M") : TEXT("-"),
		Slot->bOtherCanTake ? TEXT("T") : TEXT("-"),
		Slot->bOtherCanPut ? TEXT("P") : TEXT("-"),
		Slot->bOtherCanMoveInside ? TEXT("M") : TEXT("-"));

	return FText::FromString(Summary);
}

TSharedRef<SWidget> FPEPContainerSlotDefinitionCustomization::BuildPresetMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	const TArray<FPEPContainerSlotPreset>& Presets = GetDefault<UPEPInventorySettings>()->SlotPresets;

	if (Presets.IsEmpty())
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("NoPresets", "No presets defined (Project Settings > PEP Inventory > Container Slots)"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction());
	}
	else
	{
		for (const FPEPContainerSlotPreset& Preset : Presets)
		{
			const FName PresetName = Preset.PresetName;
			MenuBuilder.AddMenuEntry(
				FText::FromName(PresetName),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(
					this,
					&FPEPContainerSlotDefinitionCustomization::OnApplyPreset,
					PresetName)));
		}
	}

	return MenuBuilder.MakeWidget();
}

void FPEPContainerSlotDefinitionCustomization::OnApplyPreset(const FName PresetName)
{
	if (!StructHandle.IsValid())
	{
		return;
	}

	const TArray<FPEPContainerSlotPreset>& Presets = GetDefault<UPEPInventorySettings>()->SlotPresets;
	const FPEPContainerSlotPreset* Preset = Presets.FindByPredicate(
		[PresetName](const FPEPContainerSlotPreset& Candidate)
		{
			return Candidate.PresetName == PresetName;
		});

	if (!Preset)
	{
		return;
	}

	/* Every selected object's copy of this slot, not only the first one's. */
	TArray<void*> RawData;
	StructHandle->AccessRawData(RawData);
	RawData.RemoveAll([](const void* Data) { return Data == nullptr; });
	if (RawData.IsEmpty())
	{
		return;
	}

	/*
	 * Written straight into the struct, so no property handle opens a transaction on its behalf:
	 * without this one the change could not be undone. NotifyPreChange marks the owners modified
	 * inside it.
	 */
	const FScopedTransaction Transaction(FText::Format(
		LOCTEXT("ApplySlotPreset", "Apply Slot Preset '{0}'"),
		FText::FromName(PresetName)));

	StructHandle->NotifyPreChange();
	for (void* Data : RawData)
	{
		Preset->ApplyTo(*static_cast<FPEPContainerSlotDefinition*>(Data));
	}
	StructHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
	StructHandle->NotifyFinishedChangingProperties();
}

#undef LOCTEXT_NAMESPACE
