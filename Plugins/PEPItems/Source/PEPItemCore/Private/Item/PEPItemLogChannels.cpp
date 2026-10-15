#include "Item/PEPItemLogChannels.h"

#include "Item/PEPItemInstance.h"
#include "Item/PEPItemSettings.h"

DEFINE_LOG_CATEGORY(LogPEPItems);

namespace PEPItemLog
{
	void LogQueryFailure(const TCHAR* QualifiedQueryName, const FPEPItemInstance& Item)
	{
		if (!PEPItemSettings().bLogItemQueries)
		{
			return;
		}

		UE_LOG(LogPEPItems, Verbose, TEXT("%s failed for item '%s'."),
		       QualifiedQueryName, *Item.ItemId.ToString());
	}
}
