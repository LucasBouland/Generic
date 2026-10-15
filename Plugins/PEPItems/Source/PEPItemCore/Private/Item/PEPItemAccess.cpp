#include "Item/PEPItemAccess.h"

#include "GameFramework/Controller.h"

FPEPItemAccessContext FPEPItemAccessContext::From(
	const AController* InRequester)
{
	FPEPItemAccessContext Context;
	Context.Requester = InRequester;
	return Context;
}
