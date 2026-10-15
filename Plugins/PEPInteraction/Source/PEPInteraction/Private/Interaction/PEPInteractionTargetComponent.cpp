#include "Interaction/PEPInteractionTargetComponent.h"

#include "Net/UnrealNetwork.h"

UPEPInteractionTargetComponent::UPEPInteractionTargetComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPEPInteractionTargetComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPEPInteractionTargetComponent, SelectionPriority);
}
