#include "Equipment/PEPEquipmentNetTypes.h"

#include "Components/PEPEquipmentComponent.h"

void FPEPPublicEquipmentView::PostReplicatedReceive(
	const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters)
{
	if (OwnerComponent)
	{
		OwnerComponent->HandlePublicEquipmentReplicated();
	}
}

void FPEPTemporaryPresentationView::PostReplicatedReceive(
	const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters)
{
	if (OwnerComponent)
	{
		OwnerComponent->HandleTemporaryPresentationsReplicated();
	}
}
