#pragma once

#include "CoreMinimal.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemLocation.h"
#include "Net/Serialization/FastArraySerializer.h"

#include "PEPInventoryNetUtility.generated.h"

class UPEPInventoryComponent;
class UPEPInventorySessionComponent;
struct FPEPInventoryView;

/**
 * One item at one location. This is the single representation: the array below is both the
 * authoritative store and what replicates, so there is no projection to keep in step with it.
 * Confidentiality is a matter of which entries a viewer is sent, not of trimming fields here.
 */
USTRUCT(BlueprintType)
struct PEPINVENTORY_API FPEPInventoryEntry
    : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Inventory")
    FPEPItemInstance Item;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Inventory")
    FPEPItemLocation Location;

    /**
     * Location as this machine last saw it. A delta is applied to the entry before the callbacks
     * run, so the value it replaced exists nowhere else — without keeping it, a receiver cannot
     * tell an item that moved from one whose state changed where it stood.
     *
     * NotReplicated, so it costs nothing on the wire and survives a delta untouched. Maintained by
     * the replication callbacks only: an authority already knows the previous location, because it
     * is the one that just changed it.
     */
    UPROPERTY(NotReplicated)
    FPEPItemLocation LastObservedLocation;

    void PreReplicatedRemove(const FPEPInventoryView& Serializer);
    void PostReplicatedAdd(const FPEPInventoryView& Serializer);
    void PostReplicatedChange(const FPEPInventoryView& Serializer);
};

USTRUCT(BlueprintType)
struct PEPINVENTORY_API FPEPInventoryView
    : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Inventory")
    TArray<FPEPInventoryEntry> Entries;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
    {
        return FastArrayDeltaSerialize<
            FPEPInventoryEntry,
            FPEPInventoryView>(Entries, DeltaParams, *this);
    }

    /* Local-only callback targets: the store this array is, or the session it mirrors. */
    UPEPInventoryComponent* OwnerComponent = nullptr;
    UPEPInventorySessionComponent* SessionOwner = nullptr;
    bool bIsSessionView = false;

    void PostReplicatedReceive(
        const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters);
};

template<>
struct TStructOpsTypeTraits<FPEPInventoryView>
    : TStructOpsTypeTraitsBase2<FPEPInventoryView>
{
    enum
    {
        WithNetDeltaSerializer = true
    };
};
