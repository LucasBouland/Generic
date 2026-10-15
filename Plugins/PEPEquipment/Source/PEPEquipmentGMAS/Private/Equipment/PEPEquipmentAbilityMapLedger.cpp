#include "Equipment/PEPEquipmentAbilityMapLedger.h"

#include "Algo/Sort.h"

void FPEPEquipmentAbilityMapLedger::AddItem(
	const FPEPItemId ItemId,
	const TArray<FAbilityMapData>& Entries,
	const int32 Priority,
	TArray<FGameplayTag>& OutTouchedInputTags)
{
	RemoveItem(ItemId, OutTouchedInputTags);
	if (!ItemId.IsValid())
	{
		return;
	}
	for (const FAbilityMapData& Entry : Entries)
	{
		if (!Entry.InputTag.IsValid())
		{
			continue;
		}
		FInputTagState& State = InputTags.FindOrAdd(Entry.InputTag);
		FContribution* Existing = State.Contributions.FindByPredicate(
			[ItemId](const FContribution& Candidate)
			{
				return Candidate.ItemId == ItemId;
			});
		if (!Existing)
		{
			Existing = &State.Contributions.AddDefaulted_GetRef();
			Existing->ItemId = ItemId;
		}
		Existing->Data = Entry;
		Existing->Priority = Priority;
		OutTouchedInputTags.Add(Entry.InputTag);
	}
	SortAndDedupe(OutTouchedInputTags);
}

void FPEPEquipmentAbilityMapLedger::RemoveItem(
	const FPEPItemId ItemId,
	TArray<FGameplayTag>& OutTouchedInputTags)
{
	for (TPair<FGameplayTag, FInputTagState>& Pair : InputTags)
	{
		const int32 Removed = Pair.Value.Contributions.RemoveAll(
			[ItemId](const FContribution& Candidate)
			{
				return Candidate.ItemId == ItemId;
			});
		if (Removed > 0)
		{
			OutTouchedInputTags.Add(Pair.Key);
		}
	}
	SortAndDedupe(OutTouchedInputTags);
}

bool FPEPEquipmentAbilityMapLedger::HasItem(const FPEPItemId ItemId) const
{
	for (const TPair<FGameplayTag, FInputTagState>& Pair : InputTags)
	{
		if (Pair.Value.Contributions.ContainsByPredicate(
			[ItemId](const FContribution& Candidate)
			{
				return Candidate.ItemId == ItemId;
			}))
		{
			return true;
		}
	}
	return false;
}

const FPEPEquipmentAbilityMapLedger::FContribution* FPEPEquipmentAbilityMapLedger::FindWinner(
	const FGameplayTag InputTag) const
{
	const FInputTagState* State = InputTags.Find(InputTag);
	if (!State)
	{
		return nullptr;
	}
	const FContribution* Winner = nullptr;
	for (const FContribution& Candidate : State->Contributions)
	{
		if (!Winner
			|| Candidate.Priority > Winner->Priority
			|| (Candidate.Priority == Winner->Priority
				&& Candidate.ItemId.Value < Winner->ItemId.Value))
		{
			Winner = &Candidate;
		}
	}
	return Winner;
}

bool FPEPEquipmentAbilityMapLedger::NeedsBaseline(const FGameplayTag InputTag) const
{
	const FInputTagState* State = InputTags.Find(InputTag);
	return State && !State->bBaselineCaptured;
}

void FPEPEquipmentAbilityMapLedger::CaptureBaseline(
	const FGameplayTag InputTag,
	const FAbilityMapData* ExistingEntry)
{
	FInputTagState* State = InputTags.Find(InputTag);
	if (!State || State->bBaselineCaptured)
	{
		return;
	}
	State->bBaselineCaptured = true;
	State->bHadBaselineEntry = ExistingEntry != nullptr;
	if (ExistingEntry)
	{
		State->BaselineEntry = *ExistingEntry;
	}
}

bool FPEPEquipmentAbilityMapLedger::ResolveDesiredEntry(
	const FGameplayTag InputTag,
	FAbilityMapData& OutEntry) const
{
	const FInputTagState* State = InputTags.Find(InputTag);
	if (!State)
	{
		return false;
	}
	if (const FContribution* Winner = FindWinner(InputTag))
	{
		OutEntry = Winner->Data;
	}
	else if (State->bHadBaselineEntry)
	{
		OutEntry = State->BaselineEntry;
	}
	else
	{
		OutEntry = FAbilityMapData();
		OutEntry.InputTag = InputTag;
	}
	OutEntry.InputTag = InputTag;
	OutEntry.bGrantedByDefault = false;
	return true;
}

bool FPEPEquipmentAbilityMapLedger::WantsGrant(const FGameplayTag InputTag) const
{
	const FContribution* Winner = FindWinner(InputTag);
	return Winner && Winner->Data.bGrantedByDefault;
}

bool FPEPEquipmentAbilityMapLedger::IsGrantedByLedger(const FGameplayTag InputTag) const
{
	const FInputTagState* State = InputTags.Find(InputTag);
	return State && State->bGrantedByLedger;
}

void FPEPEquipmentAbilityMapLedger::SetGrantedByLedger(
	const FGameplayTag InputTag,
	const bool bGranted)
{
	if (FInputTagState* State = InputTags.Find(InputTag))
	{
		State->bGrantedByLedger = bGranted;
	}
}

void FPEPEquipmentAbilityMapLedger::ForgetIfIdle(const FGameplayTag InputTag)
{
	const FInputTagState* State = InputTags.Find(InputTag);
	if (State && State->Contributions.IsEmpty() && !State->bGrantedByLedger)
	{
		InputTags.Remove(InputTag);
	}
}

void FPEPEquipmentAbilityMapLedger::GetTrackedInputTags(TArray<FGameplayTag>& OutInputTags) const
{
	InputTags.GetKeys(OutInputTags);
	SortAndDedupe(OutInputTags);
}

void FPEPEquipmentAbilityMapLedger::Reset()
{
	InputTags.Reset();
}

void FPEPEquipmentAbilityMapLedger::SortAndDedupe(TArray<FGameplayTag>& InOutTags)
{
	Algo::Sort(InOutTags, [](const FGameplayTag& A, const FGameplayTag& B)
	{
		return A.GetTagName().LexicalLess(B.GetTagName());
	});
	for (int32 Index = InOutTags.Num() - 1; Index > 0; --Index)
	{
		if (InOutTags[Index] == InOutTags[Index - 1])
		{
			InOutTags.RemoveAt(Index);
		}
	}
}
