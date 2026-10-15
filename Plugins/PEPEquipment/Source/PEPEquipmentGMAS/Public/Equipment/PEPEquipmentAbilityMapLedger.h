#pragma once

#include "CoreMinimal.h"
#include "Ability/GMCAbilityMapData.h"
#include "Core/PEPTypes.h"
#include "GameplayTagContainer.h"

/**
 * Bookkeeping for what equipped items contribute to a GMAS ability map, kept apart from the ASC so
 * it can be reasoned about — and tested — without one.
 *
 * The ASC's AbilityMap is keyed by InputTag and holds one entry per tag, so two equipped items
 * mapping the same tag collide. The ledger keeps every contribution, decides which one answers
 * each tag, and remembers what the ASC held for that tag before equipment first touched it, so the
 * last item leaving restores it instead of erasing it.
 *
 * The winner is chosen from the set of contributions alone (priority, then lowest ItemId), never
 * from the order they arrived in: the server and the owning client can see one equipment change
 * split or batched differently, and must still agree on which ability answers an input.
 */
class PEPEQUIPMENTGMAS_API FPEPEquipmentAbilityMapLedger
{
public:
	struct FContribution
	{
		FPEPItemId ItemId;
		FAbilityMapData Data;
		int32 Priority = 0;
	};

	/**
	 * Records ItemId's entries, replacing any it already had. Entries without an InputTag are
	 * ignored; a second entry for the same tag replaces the first.
	 * @param OutTouchedInputTags  Tags whose answer may have changed, without duplicates, sorted.
	 */
	void AddItem(
		FPEPItemId ItemId,
		const TArray<FAbilityMapData>& Entries,
		int32 Priority,
		TArray<FGameplayTag>& OutTouchedInputTags);

	/** Forgets every entry of ItemId. Same output contract as AddItem. */
	void RemoveItem(FPEPItemId ItemId, TArray<FGameplayTag>& OutTouchedInputTags);

	bool HasItem(FPEPItemId ItemId) const;

	/** The contribution that answers InputTag, or null when no equipped item maps it. */
	const FContribution* FindWinner(FGameplayTag InputTag) const;

	/** True until CaptureBaseline has run for a tag the ledger tracks. */
	bool NeedsBaseline(FGameplayTag InputTag) const;

	/**
	 * Remembers what the ASC held for InputTag before equipment wrote to it.
	 * @param ExistingEntry  The ASC's entry, or null when it had none.
	 */
	void CaptureBaseline(FGameplayTag InputTag, const FAbilityMapData* ExistingEntry);

	/**
	 * The entry the ASC should now hold for InputTag: the winner's, else the baseline, else an
	 * entry with no ability (the public GMAS API cannot remove a single map entry). Always has
	 * bGrantedByDefault cleared: writing it must never grant locally, granting is the authority's
	 * explicit call.
	 * @return false when the ledger has never tracked InputTag and the ASC should be left alone.
	 */
	bool ResolveDesiredEntry(FGameplayTag InputTag, FAbilityMapData& OutEntry) const;

	/** Whether InputTag should be granted because of equipment: the winner asks for it. */
	bool WantsGrant(FGameplayTag InputTag) const;

	/** Whether the ledger's owner granted InputTag itself, and so owns removing it. */
	bool IsGrantedByLedger(FGameplayTag InputTag) const;
	void SetGrantedByLedger(FGameplayTag InputTag, bool bGranted);

	/** Drops InputTag once nothing contributes to it and no grant is owed back. */
	void ForgetIfIdle(FGameplayTag InputTag);

	/** Every tag still tracked, sorted. */
	void GetTrackedInputTags(TArray<FGameplayTag>& OutInputTags) const;

	void Reset();

private:
	struct FInputTagState
	{
		TArray<FContribution> Contributions;
		FAbilityMapData BaselineEntry;
		bool bBaselineCaptured = false;
		bool bHadBaselineEntry = false;
		bool bGrantedByLedger = false;
	};

	static void SortAndDedupe(TArray<FGameplayTag>& InOutTags);

	TMap<FGameplayTag, FInputTagState> InputTags;
};
