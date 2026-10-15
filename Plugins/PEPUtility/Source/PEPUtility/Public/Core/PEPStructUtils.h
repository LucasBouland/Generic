#pragma once
#include "CoreMinimal.h"

class UScriptStruct;

namespace PEPStructUtils
{
	/** True when Candidate is Requested or derives from it; false when either is null. */
	PEPUTILITY_API bool IsStructMatchOrChild(const UScriptStruct* Candidate, const UScriptStruct* Requested);
}
