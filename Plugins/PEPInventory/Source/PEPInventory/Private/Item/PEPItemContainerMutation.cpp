#include "Item/PEPItemContainerMutation.h"
#include "Item/PEPItemResultTypes.h"
#include "Core/PEPCoreTags.h"

#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Item/Internal/PEPItemMutableAccess.h"

namespace PEPItemContainerMutation
{
	namespace Private
	{
		FPEPItemResult SetLocked(FPEPItemInstance& ContainerItem, const bool bLocked)
		{
			FPEPItemState_ContainerRuntime* State = FPEPItemMutableAccess::GetMutableState<
				FPEPItemState_ContainerRuntime>(ContainerItem);
			if (!State)
			{
				return FPEPItemResult::Failure(PEPCoreTags::Reason_BlockedByState, ContainerItem.ItemId);
			}

			State->bLocked = bLocked;
			return FPEPItemResult::Success(ContainerItem.ItemId, PEPCoreTags::Reason_None);
		}
	}

	FPEPItemResult UnlockContainer(
		FPEPItemInstance& ContainerItem,
		const FPEPItemInstance* KeyItem)
	{
		const FPEPItemResult RuleResult = PEPItemContainerRules::CanUnlockContainer(ContainerItem, KeyItem);
		if (!RuleResult.bSuccess)
		{
			return RuleResult;
		}
		return Private::SetLocked(ContainerItem, false);
	}

	FPEPItemResult LockContainer(FPEPItemInstance& ContainerItem)
	{
		const FPEPItemResult RuleResult = PEPItemContainerRules::CanLockContainer(ContainerItem);
		if (!RuleResult.bSuccess)
		{
			return RuleResult;
		}
		return Private::SetLocked(ContainerItem, true);
	}
}
