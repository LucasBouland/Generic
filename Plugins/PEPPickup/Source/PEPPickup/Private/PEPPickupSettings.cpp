#include "PEPPickupSettings.h"

#include "Interaction/PEPInteractionTags.h"
#include "Item/PEPItemDefinition.h"
#include "UObject/UObjectGlobals.h"

UPEPPickupSettings::UPEPPickupSettings()
{
	ContentSlotInteractionOptions.AddTag(PEPInteractionTags::Open);
	AttachmentSlotInteractionOptions.AddTag(PEPInteractionTags::Inspect);
}

void UPEPPickupSettings::PostInitProperties()
{
	Super::PostInitProperties();

#if WITH_EDITOR
	/* GetDefault<> hands out the class default object: it is the only instance that caches. */
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(
			this,
			&ThisClass::HandleObjectPropertyChanged);
	}
#endif
}

void UPEPPickupSettings::BeginDestroy()
{
#if WITH_EDITOR
	if (ObjectPropertyChangedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
		ObjectPropertyChangedHandle.Reset();
	}
#endif
	DefaultInteractionOptionCache.Empty();

	Super::BeginDestroy();
}

void UPEPPickupSettings::PostReloadConfig(FProperty* PropertyThatWasLoaded)
{
	Super::PostReloadConfig(PropertyThatWasLoaded);
	InvalidateInteractionOptionCache();
}

#if WITH_EDITOR
void UPEPPickupSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	InvalidateInteractionOptionCache();
}

void UPEPPickupSettings::HandleObjectPropertyChanged(
	UObject* Object,
	FPropertyChangedEvent& PropertyChangedEvent)
{
	if (Object && Object->IsA<UPEPItemDefinition>())
	{
		InvalidateInteractionOptionCache();
	}
}
#endif

void UPEPPickupSettings::InvalidateInteractionOptionCache() const
{
	DefaultInteractionOptionCache.Reset();
}

void UPEPPickupSettings::GetDefaultInteractionOptionTags(
	const UPEPItemDefinition* Definition,
	TArray<FGameplayTag>& OutOptionTags) const
{
	OutOptionTags.Reset();
	if (!Definition)
	{
		return;
	}

	const FObjectKey DefinitionKey(Definition);
	if (const TArray<FGameplayTag>* Cached = DefaultInteractionOptionCache.Find(DefinitionKey))
	{
		OutOptionTags = *Cached;
		return;
	}

	for (const FPEPDefaultInteractionOptionsEntry& Entry : DefaultInteractionOptionsByItemTag)
	{
		if (!Definition->HasOwnedTag(Entry.ItemTag))
		{
			continue;
		}

		for (const FGameplayTag& OptionTag : Entry.OptionTags.GetGameplayTagArray())
		{
			OutOptionTags.AddUnique(OptionTag);
		}
	}

	DefaultInteractionOptionCache.Add(DefinitionKey, OutOptionTags);
}

bool UPEPPickupSettings::CanPickupDefinition(const UPEPItemDefinition* Definition) const
{
	if (!Definition || Definition->HasAnyOwnedTags(BlockedPickupTags))
	{
		return false;
	}
	if (!RequiredPickupTags.IsEmpty())
	{
		return Definition->HasAllOwnedTags(RequiredPickupTags);
	}
	return bDefaultCanPickup;
}
