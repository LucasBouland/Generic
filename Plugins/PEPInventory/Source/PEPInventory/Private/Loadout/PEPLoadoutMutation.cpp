#include "Loadout/PEPLoadoutMutation.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/AssetManager.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemLogChannels.h"

namespace
{
    /*
     * Three answers, not two. Unknown is everything the registry cannot vouch for either way —
     * its scan still running, a type nobody registered with the asset manager — and it is never
     * pruned: absence of proof is not proof of absence, and a save rewritten on the strength of a
     * question nobody could answer loses its items for good.
     */
    enum class EDefinitionPresence : uint8
    {
        Present,
        Missing,
        Unknown
    };

    /** Each definition is resolved once per prune, however many items in the forest name it. */
    class FDefinitionPresenceResolver
    {
    public:
        explicit FDefinitionPresenceResolver(FPEPInventoryPruneReport& InReport)
            : Report(InReport)
        {
        }

        /*
         * Resolved up front for the whole forest so the answer for one id never depends on the
         * order the forest is walked in: the test registry below vouches for a type the moment it
         * resolves any definition of it.
         */
        void Prepare(const TArray<FPEPInventoryTreePlacement>& Placements)
        {
#if WITH_DEV_AUTOMATION_TESTS
            const auto Vouch = [this](const FPrimaryAssetId& DefinitionId)
            {
                if (DefinitionId.IsValid()
                    && !Presence.Contains(DefinitionId)
                    && PEPItemAutomationTestSupport::FindTransientDefinitionForTests(DefinitionId))
                {
                    Presence.Add(DefinitionId, EDefinitionPresence::Present);
                    TypesVouchedByTests.Add(DefinitionId.PrimaryAssetType);
                }
            };
            for (const FPEPInventoryTreePlacement& Placement : Placements)
            {
                Vouch(Placement.Tree.RootItem.DefinitionId);
                for (const FPEPItemInstanceTreeEntry& Child : Placement.Tree.Children)
                {
                    Vouch(Child.Item.DefinitionId);
                }
            }
#else
            (void)Placements;
#endif
        }

        bool IsMissing(const FPrimaryAssetId& DefinitionId)
        {
            if (const EDefinitionPresence* Cached = Presence.Find(DefinitionId))
            {
                return *Cached == EDefinitionPresence::Missing;
            }

            const EDefinitionPresence Resolved = Resolve(DefinitionId);
            Presence.Add(DefinitionId, Resolved);

            if (Resolved == EDefinitionPresence::Missing)
            {
                /* An invalid id is reported too: it names nothing, and the loss must still be told. */
                Report.MissingDefinitions.AddUnique(DefinitionId);
                UE_LOG(
                    LogPEPItems,
                    Warning,
                    TEXT("Pruning items: definition '%s' does not exist."),
                    *DefinitionId.ToString());
            }
            else if (Resolved == EDefinitionPresence::Unknown)
            {
                UE_LOG(
                    LogPEPItems,
                    Log,
                    TEXT("Not pruning items naming '%s': whether it exists cannot be established yet."),
                    *DefinitionId.ToString());
            }

            return Resolved == EDefinitionPresence::Missing;
        }

    private:
        EDefinitionPresence Resolve(const FPrimaryAssetId& DefinitionId) const
        {
            /* Names nothing at all, so there is nothing it could ever be restored as. */
            if (!DefinitionId.IsValid())
            {
                return EDefinitionPresence::Missing;
            }

            if (!UAssetManager::IsInitialized())
            {
                return EDefinitionPresence::Unknown;
            }

            UAssetManager& AssetManager = UAssetManager::Get();
            FPrimaryAssetTypeInfo TypeInfo;
            if (!AssetManager.GetPrimaryAssetTypeInfo(DefinitionId.PrimaryAssetType, TypeInfo))
            {
#if WITH_DEV_AUTOMATION_TESTS
                /*
                 * Test definitions live in a transient registry the asset manager never sees. For a
                 * type that registry vouches for, it is the registry: a definition of that type it
                 * does not hold is one a test deliberately removed.
                 */
                if (TypesVouchedByTests.Contains(DefinitionId.PrimaryAssetType))
                {
                    return EDefinitionPresence::Missing;
                }
#endif
                /* Nobody told the asset manager about this type, so it cannot say what exists. */
                return EDefinitionPresence::Unknown;
            }

            /* Until the registry has seen every asset, not finding one proves nothing. */
            const IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
            if (!AssetRegistry || AssetRegistry->IsLoadingAssets())
            {
                return EDefinitionPresence::Unknown;
            }

            /*
             * Established against the registry, never against whether the asset happens to be
             * loaded: a definition that has not streamed in yet is present and must never be pruned.
             */
            return AssetManager.GetPrimaryAssetPath(DefinitionId).IsValid()
                ? EDefinitionPresence::Present
                : EDefinitionPresence::Missing;
        }

        FPEPInventoryPruneReport& Report;
        TMap<FPrimaryAssetId, EDefinitionPresence> Presence;
#if WITH_DEV_AUTOMATION_TESTS
        TSet<FPrimaryAssetType> TypesVouchedByTests;
#endif
    };

    /**
     * Drops every child whose definition is missing, together with everything below it.
     *
     * One pass builds a parent-to-children index; a breadth-first walk from the condemned items
     * then marks their descendants, each child at most once, in whatever order the tree lists
     * them. The survivors are compacted in place, in their original order.
     */
    void PruneChildren(
        FPEPItemInstanceTree& Tree,
        FDefinitionPresenceResolver& Resolver,
        FPEPInventoryPruneReport& OutReport)
    {
        TArray<FPEPItemInstanceTreeEntry>& Children = Tree.Children;

        TArray<bool> bDead;
        bDead.Init(false, Children.Num());
        TArray<int32> Pending;
        for (int32 Index = 0; Index < Children.Num(); ++Index)
        {
            if (Resolver.IsMissing(Children[Index].Item.DefinitionId))
            {
                bDead[Index] = true;
                Pending.Add(Index);
            }
        }

        if (Pending.IsEmpty())
        {
            return;
        }

        TMap<FPEPItemId, TArray<int32>> ChildIndicesByParent;
        ChildIndicesByParent.Reserve(Children.Num());
        for (int32 Index = 0; Index < Children.Num(); ++Index)
        {
            ChildIndicesByParent.FindOrAdd(Children[Index].Location.ParentItemId).Add(Index);
        }

        /* Walked by index while it grows; a child is queued once, when first condemned. */
        for (int32 Cursor = 0; Cursor < Pending.Num(); ++Cursor)
        {
            const TArray<int32>* Held = ChildIndicesByParent.Find(Children[Pending[Cursor]].Item.ItemId);
            if (!Held)
            {
                continue;
            }

            for (const int32 HeldIndex : *Held)
            {
                if (!bDead[HeldIndex])
                {
                    bDead[HeldIndex] = true;
                    Pending.Add(HeldIndex);
                }
            }
        }

        int32 WriteIndex = 0;
        for (int32 ReadIndex = 0; ReadIndex < Children.Num(); ++ReadIndex)
        {
            if (bDead[ReadIndex])
            {
                OutReport.RemovedItemIds.Add(Children[ReadIndex].Item.ItemId);
                continue;
            }

            if (WriteIndex != ReadIndex)
            {
                Children[WriteIndex] = MoveTemp(Children[ReadIndex]);
            }
            ++WriteIndex;
        }
        Children.SetNum(WriteIndex, EAllowShrinking::No);
    }
}

void PEPLoadoutMutation::PruneMissingDefinitions(
    TArray<FPEPInventoryTreePlacement>& Placements,
    FPEPInventoryPruneReport& OutReport)
{
    OutReport = FPEPInventoryPruneReport();

    FDefinitionPresenceResolver Resolver(OutReport);
    Resolver.Prepare(Placements);

    int32 WriteIndex = 0;
    for (int32 ReadIndex = 0; ReadIndex < Placements.Num(); ++ReadIndex)
    {
        FPEPInventoryTreePlacement& Placement = Placements[ReadIndex];

        /* A dead root takes the whole tree: nothing under it has anywhere left to be. */
        if (Resolver.IsMissing(Placement.Tree.RootItem.DefinitionId))
        {
            OutReport.RemovedItemIds.Add(Placement.Tree.RootItem.ItemId);
            for (const FPEPItemInstanceTreeEntry& Child : Placement.Tree.Children)
            {
                /* Resolved only so that a definition missing in here is named in the report too. */
                Resolver.IsMissing(Child.Item.DefinitionId);
                OutReport.RemovedItemIds.Add(Child.Item.ItemId);
            }
            continue;
        }

        PruneChildren(Placement.Tree, Resolver, OutReport);

        /* Compacted in one pass, where removing each dead tree in place shifted the rest every time. */
        if (WriteIndex != ReadIndex)
        {
            Placements[WriteIndex] = MoveTemp(Placement);
        }
        ++WriteIndex;
    }
    Placements.SetNum(WriteIndex, EAllowShrinking::No);

    if (!OutReport.IsEmpty())
    {
        UE_LOG(
            LogPEPItems,
            Warning,
            TEXT("Pruned %d item(s) referencing %d missing definition(s)."),
            OutReport.RemovedItemIds.Num(),
            OutReport.MissingDefinitions.Num());
    }
}
