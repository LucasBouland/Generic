#pragma once
#include "CoreMinimal.h"

namespace PEPStructUtils
{
	PEPUTILITY_API bool IsStructMatchOrChild(const UScriptStruct* Candidate, const UScriptStruct* Requested);
}
