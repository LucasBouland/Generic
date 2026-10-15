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

/**
 * The fast array itself. Not a Blueprint type: nothing reflected hands it out, and Blueprint reads
 * a store through the component's accessors, never through its replication payload.
 */
USTRUCT()
struct PEPINVENTORY_API FPEPInventoryView
    : public FFastArraySerializer
{
    GENERATED_BODY()

    /*
     * The callback targets decide which component a received delta is announced on. Only the two
     * components that own a view bind them, and only the entries read them: anything else able to
     * point a view at a store could make that store announce changes no delta made.
     */
    friend struct FPEPInventoryEntry;
    friend class UPEPInventoryComponent;
    friend class UPEPInventorySessionComponent;
    /* Defined by the automation tests alone, which drive the callbacks on a detached view. */
    friend struct FPEPInventoryViewTestAccess;

public:
    UPROPERTY(VisibleAnywhere, Category="PEP|Inventory")
    TArray<FPEPInventoryEntry> Entries;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
    {
        return FastArrayDeltaSerialize<
            FPEPInventoryEntry,
            FPEPInventoryView>(Entries, DeltaParams, *this);
    }

    void PostReplicatedReceive(
        const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters);

    /* Read-only views of the binding, for code that has to confirm it rather than set it. */
    const UPEPInventoryComponent* GetOwnerComponent() const
    {
        return OwnerComponent;
    }

    const UPEPInventorySessionComponent* GetSessionOwner() const
    {
        return SessionOwner;
    }

    bool IsSessionView() const
    {
        return bIsSessionView;
    }

private:
    /* Local-only callback targets: the store this array is, or the session it mirrors. */
    UPEPInventoryComponent* OwnerComponent = nullptr;
    UPEPInventorySessionComponent* SessionOwner = nullptr;
    bool bIsSessionView = false;
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
