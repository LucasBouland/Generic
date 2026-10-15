#include "Loadout/PEPLoadoutMutation.h"

#include "Engine/AssetManager.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemLogChannels.h"

namespace
{
    /**
     * Whether the registry knows this definition at all. Distinct from whether it is loaded: a
     * definition that simply has not streamed in yet is present and must never be pruned.
     *
     * @param OutKnown  Left untouched when the question cannot be answered.
     * @return          false when there is no registry to ask, in which case nothing may be pruned.
     */
    bool TryResolveDefinitionExists(const FPrimaryAssetId& DefinitionId, bool& OutKnown)
    {
#if WITH_DEV_AUTOMATION_TESTS
        /* Test definitions live in a transient registry the asset manager never sees. */
        if (PEPItemAutomationTestSupport::FindTransientDefinitionForTests(DefinitionId))
        {
            OutKnown = true;
            return true;
        }
#endif

        if (!UAssetManager::IsInitialized())
        {
            return false;
        }

        OutKnown = UAssetManager::Get().GetPrimaryAssetPath(DefinitionId).IsValid();
        return true;
    }
}

void PEPLoadoutMutation::PruneMissingDefinitions(
    TArray<FPEPInventoryTreePlacement>& Placements,
    FPEPInventoryPruneReport& OutReport)
{
    OutReport = FPEPInventoryPruneReport();

    /* A stash names the same few definitions hundreds of times; the registry is asked once each. */
    TMap<FPrimaryAssetId, bool> Resolved;

    const auto DefinitionIsMissing =
        [&Resolved, &OutReport](const FPrimaryAssetId& DefinitionId) -> bool
        {
            if (!DefinitionId.IsValid())
            {
                return true;
            }

            if (const bool* Cached = Resolved.Find(DefinitionId))
            {
                return !*Cached;
            }

            bool bKnown = true;
            if (!TryResolveDefinitionExists(DefinitionId, bKnown))
            {
                /* No registry to ask. Treat every definition as present and prune nothing. */
                return false;
            }

            Resolved.Add(DefinitionId, bKnown);

            if (!bKnown)
            {
                OutReport.MissingDefinitions.AddUnique(DefinitionId);
                UE_LOG(
                    LogPEPItems,
                    Warning,
                    TEXT("Pruning items: definition '%s' no longer exists."),
                    *DefinitionId.ToString());
            }

            return !bKnown;
        };

    for (int32 PlacementIndex = Placements.Num() - 1; PlacementIndex >= 0; --PlacementIndex)
    {
        FPEPInventoryTreePlacement& Placement = Placements[PlacementIndex];

        /* A dead root takes the whole tree: nothing under it has anywhere left to be. */
        if (DefinitionIsMissing(Placement.Tree.RootItem.DefinitionId))
        {
            OutReport.RemovedItemIds.Add(Placement.Tree.RootItem.ItemId);
            for (const FPEPItemInstanceTreeEntry& Child : Placement.Tree.Children)
            {
                DefinitionIsMissing(Child.Item.DefinitionId);
                OutReport.RemovedItemIds.Add(Child.Item.ItemId);
            }

            Placements.RemoveAt(PlacementIndex);
            continue;
        }

        TSet<FPEPItemId> Dead;
        for (const FPEPItemInstanceTreeEntry& Child : Placement.Tree.Children)
        {
            if (DefinitionIsMissing(Child.Item.DefinitionId))
            {
                Dead.Add(Child.Item.ItemId);
            }
        }

        if (Dead.IsEmpty())
        {
            continue;
        }

        /*
         * A dead container takes its contents with it. Closed to a fixed point rather than in one
         * pass, because the children are in no particular order: a grandchild may be visited before
         * the parent that condemns it. It terminates — Dead only grows, and is bounded by the
         * number of children.
         */
        bool bChanged = true;
        while (bChanged)
        {
            bChanged = false;
            for (const FPEPItemInstanceTreeEntry& Child : Placement.Tree.Children)
            {
                if (!Dead.Contains(Child.Item.ItemId)
                    && Dead.Contains(Child.Location.ParentItemId))
                {
                    Dead.Add(Child.Item.ItemId);
                    bChanged = true;
                }
            }
        }

        for (const FPEPItemInstanceTreeEntry& Child : Placement.Tree.Children)
        {
            if (Dead.Contains(Child.Item.ItemId))
            {
                OutReport.RemovedItemIds.Add(Child.Item.ItemId);
            }
        }

        Placement.Tree.Children.RemoveAll(
            [&Dead](const FPEPItemInstanceTreeEntry& Child)
            {
                return Dead.Contains(Child.Item.ItemId);
            });
    }

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
