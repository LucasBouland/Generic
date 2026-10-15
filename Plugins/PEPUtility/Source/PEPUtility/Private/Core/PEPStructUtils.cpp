#include "Core/PEPStructUtils.h"

#include "UObject/Class.h"

namespace PEPStructUtils
{
	bool IsStructMatchOrChild(const UScriptStruct* Candidate, const UScriptStruct* Requested)
	{
		// IsChildOf is true for the struct itself.
		return Candidate && Requested && Candidate->IsChildOf(Requested);
	}
}
