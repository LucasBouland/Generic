#include "PEPPickupSettings.h"

#include "Interaction/PEPInteractionTags.h"
#include "Item/PEPItemDefinition.h"

UPEPPickupSettings::UPEPPickupSettings()
{
	ContentSlotInteractionOptions.AddTag(PEPInteractionTags::Open);
	AttachmentSlotInteractionOptions.AddTag(PEPInteractionTags::Inspect);
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
