#include "PEPPickupQueryLibrary.h"

#include "PEPPickupSettings.h"

bool UPEPPickupQueryLibrary::CanPickupByDefault(const UPEPItemDefinition* Definition)
{
	return GetDefault<UPEPPickupSettings>()->CanPickupDefinition(Definition);
}
