#include "Inventory/PEPInventorySettings.h"

#include "Item/PEPItemDefinition.h"
#include "Item/Actor/PEPItemWorldActor.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Item/Fragments/PEPItemFragment_InventoryShape.h"
#include "Item/PEPItemLogChannels.h"

TSubclassOf<APEPItemWorldActor> UPEPInventorySettings::ResolveWorldItemActorClass() const
{
	if (DefaultWorldItemActorClass.IsNull())
	{
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogPEPItems, Warning,
				TEXT("PEP Inventory: DefaultWorldItemActorClass is unset. Configure it in Project Settings before dropping items."));
		}
		return nullptr;
	}
	if (UClass* Resident = DefaultWorldItemActorClass.Get())
	{
		return Resident;
	}
	return DefaultWorldItemActorClass.LoadSynchronous();
}

TSubclassOf<APEPItemWorldActor> UPEPInventorySettings::ResolveWorldItemActorClassForDefinition(
	const UPEPItemDefinition* Definition) const
{
	if (Definition && Definition->GetDefinitionFragment<FPEPItemFragment_Container>())
	{
		return DefaultWorldContainerActorClass.LoadSynchronous();
	}
	return ResolveWorldItemActorClass();
}

bool UPEPInventorySettings::GetEffectiveInventoryShape(
	const FPEPItemFragment_InventoryShape* Fragment,
	FIntPoint& OutSize,
	bool& bOutCanRotate) const
{
	if (Fragment)
	{
		OutSize = Fragment->Size;
		bOutCanRotate = Fragment->bCanRotate;
	}
	else
	{
		OutSize = DefaultInventorySize;
		bOutCanRotate = bDefaultInventoryCanRotate;
	}
	if (OutSize.X <= 0 || OutSize.Y <= 0)
	{
		OutSize = FIntPoint(1, 1);
	}
	return OutSize.X > 0 && OutSize.Y > 0;
}
