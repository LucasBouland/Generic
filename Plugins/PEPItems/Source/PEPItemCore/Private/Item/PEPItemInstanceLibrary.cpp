#include "Item/PEPItemInstanceLibrary.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Item/PEPItemLogChannels.h"

bool UPEPItemInstanceLibrary::CreateItemInstance(
	const UObject* WorldContextObject,
	const UPEPItemDefinition* Definition,
	FPEPItemInstance& OutItem)
{
	OutItem.Reset();

	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_Client)
	{
		UE_LOG(LogPEPItems, Warning,
			TEXT("CreateItemInstance rejected: item IDs can only be allocated with an authoritative world context."));
		return false;
	}

	OutItem = FPEPItemInstance::MakeNew(Definition);
	FPEPItemInstanceValidationResult Validation;
	if (!OutItem.ValidateInstance(Validation))
	{
		UE_LOG(LogPEPItems, Warning,
			TEXT("CreateItemInstance failed for definition '%s': %s"),
			Definition ? *Definition->GetPathName() : TEXT("<null>"),
			Validation.Errors.IsEmpty() ? TEXT("unknown validation error") : *Validation.Errors[0].ToString());
		OutItem.Reset();
		return false;
	}

	return true;
}

#if WITH_EDITOR
bool UPEPItemInstanceLibrary::CreateItemInstanceForEditor(
	const UPEPItemDefinition* Definition,
	FPEPItemInstance& OutItem)
{
	OutItem.Reset();
	OutItem = FPEPItemInstance::MakeNew(Definition);
	FPEPItemInstanceValidationResult Validation;
	if (!OutItem.ValidateInstance(Validation))
	{
		OutItem.Reset();
		return false;
	}
	return true;
}
#endif
