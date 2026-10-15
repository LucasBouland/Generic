#include "PEPItemUILibrary.h"

#include "Item/PEPItemQuery.h"

bool UPEPItemUILibrary::BuildItemViewData(
	const FPEPItemInstance& Item,
	FPEPItemViewData& OutData)
{
	OutData = FPEPItemViewData();
	if (!Item.IsValid() || !PEPItemQuery::GetDisplayData(Item, OutData.DisplayData))
	{
		return false;
	}

	OutData.bValid = true;
	OutData.ItemId = Item.ItemId;
	OutData.bHasStack = PEPItemQuery::IsStackable(Item)
		&& PEPItemQuery::GetStackData(Item, OutData.StackData);
	return true;
}
