#include "PEPItemDefinitionCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IPropertyUtilities.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemFragment.h"
#include "Item/PEPItemSettings.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PEPItemDefinitionCustomization"

TSharedRef<IDetailCustomization> FPEPItemDefinitionCustomization::MakeInstance()
{
	return MakeShared<FPEPItemDefinitionCustomization>();
}

void FPEPItemDefinitionCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	EditedDefinition = Objects.Num() == 1 ? Cast<UPEPItemDefinition>(Objects[0].Get()) : nullptr;

	PropertyUtilities = DetailBuilder.GetPropertyUtilities();

	FragmentsHandle = DetailBuilder.GetProperty(
		GET_MEMBER_NAME_CHECKED(UPEPItemDefinition, DefinitionFragments));

	IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(TEXT("Advanced"));

	CategoryBuilder.AddCustomRow(LOCTEXT("AddFragmentSearch", "Add Fragment"))
		.NameContent()
		[
			SNew(STextBlock).Text(LOCTEXT("AddFragmentLabel", "Add Fragment"))
		]
		.ValueContent()
		.MinDesiredWidth(200.f)
		[
			SNew(SComboButton)
			.ButtonContent()
			[
				SNew(STextBlock).Text(LOCTEXT("AddFragmentButton", "Add Fragment"))
			]
			.OnGetMenuContent(this, &FPEPItemDefinitionCustomization::BuildAddFragmentMenu)
		];

	CategoryBuilder.AddCustomRow(LOCTEXT("ApplyBundleSearch", "Apply Bundle"))
		.NameContent()
		[
			SNew(STextBlock).Text(LOCTEXT("ApplyBundleLabel", "Apply Bundle"))
		]
		.ValueContent()
		.MinDesiredWidth(200.f)
		[
			SNew(SComboButton)
			.ButtonContent()
			[
				SNew(STextBlock).Text(LOCTEXT("ApplyBundleButton", "Apply Bundle"))
			]
			.OnGetMenuContent(this, &FPEPItemDefinitionCustomization::BuildApplyBundleMenu)
		];
}

TSharedRef<SWidget> FPEPItemDefinitionCustomization::BuildAddFragmentMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	TArray<const UScriptStruct*> FragmentStructs;
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;
		if (Struct != FPEPItemFragment::StaticStruct()
			&& Struct->IsChildOf(FPEPItemFragment::StaticStruct()))
		{
			FragmentStructs.Add(Struct);
		}
	}

	FragmentStructs.Sort(
		[](const UScriptStruct& A, const UScriptStruct& B)
		{
			return A.GetDisplayNameText().CompareTo(B.GetDisplayNameText()) < 0;
		});

	for (const UScriptStruct* Struct : FragmentStructs)
	{
		const bool bAlreadyPresent = IsFragmentPresent(Struct);

		MenuBuilder.AddMenuEntry(
			Struct->GetDisplayNameText(),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(
					this,
					&FPEPItemDefinitionCustomization::OnAddFragment,
					Struct),
				FCanExecuteAction::CreateLambda([bAlreadyPresent] { return !bAlreadyPresent; })));
	}

	return MenuBuilder.MakeWidget();
}

bool FPEPItemDefinitionCustomization::IsFragmentPresent(const UScriptStruct* FragmentStruct) const
{
	const UPEPItemDefinition* Definition = EditedDefinition.Get();
	if (!Definition || !FragmentStruct)
	{
		return false;
	}

	for (const FInstancedStruct& Entry : Definition->GetDefinitionFragments())
	{
		const UScriptStruct* Existing = Entry.GetScriptStruct();
		if (!Existing)
		{
			continue;
		}
		if (Existing == FragmentStruct
			|| Existing->IsChildOf(FragmentStruct)
			|| FragmentStruct->IsChildOf(Existing))
		{
			return true;
		}
	}

	return false;
}

namespace
{
	/**
	 * Appends one element to the DefinitionFragments array and assigns it Value, wholesale —
	 * works identically whether Value is an empty-but-typed instance (a plain "Add Fragment") or
	 * a fully pre-configured one copied from a bundle. Returns false only on a handle failure;
	 * the caller decides what "nothing added" means for its own refresh/skip bookkeeping.
	 */
	bool AppendFragmentElement(
		const TSharedPtr<IPropertyHandle>& FragmentsHandle,
		const FInstancedStruct& Value)
	{
		if (!FragmentsHandle.IsValid())
		{
			return false;
		}

		const TSharedPtr<IPropertyHandleArray> ArrayHandle = FragmentsHandle->AsArray();
		if (!ArrayHandle.IsValid())
		{
			return false;
		}

		uint32 NumElements = 0;
		ArrayHandle->GetNumElements(NumElements);
		ArrayHandle->AddItem();

		const TSharedPtr<IPropertyHandle> NewElementHandle =
			FragmentsHandle->GetChildHandle(NumElements);
		if (!NewElementHandle.IsValid())
		{
			return false;
		}

		void* RawData = nullptr;
		if (NewElementHandle->GetValueData(RawData) != FPropertyAccess::Success || !RawData)
		{
			return false;
		}

		NewElementHandle->NotifyPreChange();
		*static_cast<FInstancedStruct*>(RawData) = Value;
		NewElementHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
		NewElementHandle->NotifyFinishedChangingProperties();
		return true;
	}
}

void FPEPItemDefinitionCustomization::OnAddFragment(const UScriptStruct* FragmentStruct)
{
	if (!FragmentStruct)
	{
		return;
	}

	FInstancedStruct NewValue;
	NewValue.InitializeAs(FragmentStruct);

	if (AppendFragmentElement(FragmentsHandle, NewValue) && PropertyUtilities.IsValid())
	{
		PropertyUtilities->ForceRefresh();
	}
}

TSharedRef<SWidget> FPEPItemDefinitionCustomization::BuildApplyBundleMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	const TArray<FPEPItemFragmentBundle>& Bundles = GetDefault<UPEPItemSettings>()->FragmentBundles;

	if (Bundles.IsEmpty())
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("NoBundles", "No bundles defined (Project Settings > PEP Items)"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction());
	}
	else
	{
		for (const FPEPItemFragmentBundle& Bundle : Bundles)
		{
			const FName BundleName = Bundle.BundleName;
			MenuBuilder.AddMenuEntry(
				FText::FromName(BundleName),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(
					this,
					&FPEPItemDefinitionCustomization::OnApplyBundle,
					BundleName)));
		}
	}

	return MenuBuilder.MakeWidget();
}

void FPEPItemDefinitionCustomization::OnApplyBundle(const FName BundleName)
{
	const TArray<FPEPItemFragmentBundle>& Bundles = GetDefault<UPEPItemSettings>()->FragmentBundles;
	const FPEPItemFragmentBundle* Bundle = Bundles.FindByPredicate(
		[BundleName](const FPEPItemFragmentBundle& Candidate)
		{
			return Candidate.BundleName == BundleName;
		});

	if (!Bundle)
	{
		return;
	}

	/*
	 * Checked against live state, not a snapshot taken before the loop: appending one bundle
	 * fragment updates EditedDefinition immediately (property handles write straight into the
	 * object), so a bundle that mistakenly carries two fragments of the same family still only
	 * ever gets the first one — the second sees it as already present, same as a fragment that
	 * was already on the item before the bundle was applied.
	 */
	bool bAddedAny = false;
	for (const FInstancedStruct& BundleFragment : Bundle->Fragments)
	{
		const UScriptStruct* FragmentStruct = BundleFragment.GetScriptStruct();
		if (!FragmentStruct || IsFragmentPresent(FragmentStruct))
		{
			continue;
		}

		if (AppendFragmentElement(FragmentsHandle, BundleFragment))
		{
			bAddedAny = true;
		}
	}

	if (bAddedAny && PropertyUtilities.IsValid())
	{
		PropertyUtilities->ForceRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
