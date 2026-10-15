#include "Core/PEPStructUtils.h"

namespace PEPStructUtils
{
	bool IsStructMatchOrChild(const UScriptStruct* Candidate, const UScriptStruct* Requested)
	{
		return Candidate && Requested && (Candidate == Requested || Candidate->IsChildOf(Requested));
	}
}
