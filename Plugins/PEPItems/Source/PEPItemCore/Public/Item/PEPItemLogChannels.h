#pragma once
#include "CoreMinimal.h"

struct FPEPItemInstance;

PEPITEMCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogPEPItems, Log, All);

namespace PEPItemLog
{
	/**
	 * Verbose trace for a query that returned nothing, gated by UPEPItemSettings::bLogItemQueries.
	 * Shared by every query namespace, including those in other modules.
	 *
	 * @param QualifiedQueryName  Fully qualified, e.g. TEXT("PEPItemQuery.GetStackData").
	 */
	PEPITEMCORE_API void LogQueryFailure(const TCHAR* QualifiedQueryName, const FPEPItemInstance& Item);
}
