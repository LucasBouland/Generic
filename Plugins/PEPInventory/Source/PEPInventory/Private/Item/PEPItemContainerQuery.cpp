#include "Item/PEPItemContainerQuery.h"

#include "Item/PEPItemLogChannels.h"

namespace PEPItemContainerQuery
{
	bool GetContainerData(const FPEPItemInstance& Item, FPEPItemContainerData& OutData)
	{
		OutData = FPEPItemContainerData();
		const FPEPItemFragment_Container* Fragment = Item.GetDefinitionFragment<FPEPItemFragment_Container>();
		if (!Fragment)
		{
			PEPItemLog::LogQueryFailure(TEXT("PEPItemContainerQuery.GetContainerData"), Item);
			return false;
		}

		const FPEPItemState_ContainerRuntime* State = Item.GetState<FPEPItemState_ContainerRuntime>();
		OutData.bValid = true;
		OutData.bLocked = State ? State->bLocked : Fragment->bStartsLocked;
		OutData.bCanRelock = Fragment->bCanRelock;
		OutData.ExternalLootVisibility = Fragment->ExternalLootVisibility;
		OutData.Slots = Fragment->Slots;
		return true;
	}
}

bool UPEPItemContainerQueryLibrary::IsItemContainer(const FPEPItemInstance& Item)
{
	return PEPItemContainerQuery::IsContainer(Item);
}

bool UPEPItemContainerQueryLibrary::GetItemContainerData(
	const FPEPItemInstance& Item,
	FPEPItemContainerData& OutData)
{
	return PEPItemContainerQuery::GetContainerData(Item, OutData);
}
