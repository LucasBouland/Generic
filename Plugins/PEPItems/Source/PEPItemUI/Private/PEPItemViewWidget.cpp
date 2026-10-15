#include "PEPItemViewWidget.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Styling/SlateBrush.h"

void UPEPItemViewWidget::SetItemInstance(const FPEPItemInstance& Item)
{
	UPEPItemUILibrary::BuildItemViewData(Item, ViewData);
	HandleViewDataChanged();
}

void UPEPItemViewWidget::ClearItem()
{
	ViewData = FPEPItemViewData();
	++IconRequestGeneration;
	IconLoadHandle.Reset();
	HandleViewDataChanged();
}

void UPEPItemViewWidget::HandleViewDataChanged()
{
	RefreshView();
	OnItemViewDataChanged(ViewData);
}

void UPEPItemViewWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	RefreshView();
}

void UPEPItemViewWidget::NativeDestruct()
{
	++IconRequestGeneration;
	IconLoadHandle.Reset();
	Super::NativeDestruct();
}

void UPEPItemViewWidget::RefreshView()
{
	if (ItemNameText)
	{
		ItemNameText->SetText(ViewData.bValid ? ViewData.DisplayData.DisplayName : FText::GetEmpty());
	}
	if (ItemStackText)
	{
		ItemStackText->SetText(ViewData.bValid && ViewData.bHasStack
			? FText::Format(NSLOCTEXT("PEPItemUI", "StackCount", "{0} / {1}"),
				FText::AsNumber(ViewData.StackData.Count), FText::AsNumber(ViewData.StackData.MaxStackSize))
			: FText::GetEmpty());
	}
	if (!ViewData.bValid)
	{
		if (ItemIcon)
		{
			ItemIcon->SetBrush(FSlateBrush());
		}
		++IconRequestGeneration;
		IconLoadHandle.Reset();
		return;
	}
	RequestItemIcon();
}

void UPEPItemViewWidget::RequestItemIcon()
{
	if (!ItemIcon)
	{
		return;
	}

	++IconRequestGeneration;
	const uint64 RequestGeneration = IconRequestGeneration;
	IconLoadHandle.Reset();
	ItemIcon->SetBrush(FSlateBrush());

	if (UTexture2D* LoadedIcon = ViewData.DisplayData.Icon.Get())
	{
		ItemIcon->SetBrushFromTexture(LoadedIcon, true);
		return;
	}

	const FSoftObjectPath IconPath = ViewData.DisplayData.Icon.ToSoftObjectPath();
	if (!IconPath.IsValid())
	{
		return;
	}

	IconLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		IconPath,
		FStreamableDelegate::CreateWeakLambda(this, [this, RequestGeneration]()
		{
			if (RequestGeneration != IconRequestGeneration || !ItemIcon)
			{
				return;
			}
			if (UTexture2D* LoadedIcon = ViewData.DisplayData.Icon.Get())
			{
				ItemIcon->SetBrushFromTexture(LoadedIcon, true);
			}
		}));
}
