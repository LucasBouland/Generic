#include "Net/PEPInventoryNetUtility.h"

#include "Components/PEPInventoryComponent.h"
#include "Components/PEPInventorySessionComponent.h"

void FPEPInventoryEntry::PreReplicatedRemove(
    const FPEPInventoryView& Serializer)
{
    if (Serializer.OwnerComponent && !Serializer.bIsSessionView)
    {
        Serializer.OwnerComponent->QueueReplicatedEntryRemoved(*this);
    }
}

void FPEPInventoryView::PostReplicatedReceive(
    const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters)
{
    if (SessionOwner)
    {
        SessionOwner->HandleReplicatedSessionViewChanged();
    }
    else if (OwnerComponent && !bIsSessionView)
    {
        OwnerComponent->FlushReplicatedEntryRemovals();
    }
}

void FPEPInventoryEntry::PostReplicatedAdd(
    const FPEPInventoryView& Serializer)
{
    /*
     * Seeded here or the first state-only change would compare against a default location, find it
     * different, and report a move the item never made.
     */
    LastObservedLocation = Location;

    if (Serializer.OwnerComponent && !Serializer.bIsSessionView)
    {
        Serializer.OwnerComponent->HandleReplicatedEntryAdded(*this);
    }
}

void FPEPInventoryEntry::PostReplicatedChange(
    const FPEPInventoryView& Serializer)
{
    const FPEPItemLocation PreviousLocation = LastObservedLocation;
    LastObservedLocation = Location;

    if (Serializer.OwnerComponent && !Serializer.bIsSessionView)
    {
        Serializer.OwnerComponent->HandleReplicatedEntryChanged(
            *this,
            PreviousLocation);
    }
}
