#include "Item/PEPIdAllocatorSubsystem.h"

#include "CoreGlobals.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Item/PEPItemIdAllocatorLegacySaveGame.h"
#include "Item/PEPItemLogChannels.h"

#include <atomic>

namespace
{
	const TCHAR* LegacyAllocatorSaveSlotName = TEXT("PEPItemIdAllocator");

	/*
	 * Process-wide state, kept out of the header so nothing but this file can touch it.
	 *
	 * NextId never exceeds MAX_int32 and is never handed out once it reaches it, so MAX_int32 itself
	 * is never allocated: PEPItemMutation::GetPreviewItemId relies on that to stay collision-free.
	 */
	std::atomic<int32> GPEPItemNextId{0};
	std::atomic<bool> GPEPItemIdSeeded{false};

	/** Raises the counter to at least Floor. Never lowers it, whatever the interleaving. */
	void RaisePEPItemNextIdTo(const int32 Floor)
	{
		int32 Current = GPEPItemNextId.load(std::memory_order_relaxed);
		while (Current < Floor
			&& !GPEPItemNextId.compare_exchange_weak(Current, Floor, std::memory_order_relaxed))
		{
		}
	}
}

void UPEPIdAllocatorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EnsureSeeded();
}

int32 UPEPIdAllocatorSubsystem::AllocateNext()
{
	EnsureSeeded();

	int32 Current = GPEPItemNextId.load(std::memory_order_relaxed);
	do
	{
		// Wrapping would hand out INDEX_NONE, then negatives that collide with test ids, and the
		// last positive value is the reserved preview id. An invalid id is refused by every store,
		// which is the loud failure this deserves.
		if (Current < 0 || Current >= MAX_int32)
		{
			ensureMsgf(false, TEXT("PEP item id counter exhausted (%d); refusing to allocate."), Current);
			return INDEX_NONE;
		}
	}
	while (!GPEPItemNextId.compare_exchange_weak(Current, Current + 1, std::memory_order_relaxed));

	return Current;
}

void UPEPIdAllocatorSubsystem::ObserveLoadedId(const int32 Id)
{
	// Negative ids come from automation tests and MAX_int32 is the preview id: the allocator never
	// returns either, so neither can collide with a future allocation.
	if (Id < 0 || Id >= MAX_int32)
	{
		return;
	}
	RaisePEPItemNextIdTo(Id + 1);
}

void UPEPIdAllocatorSubsystem::EnsureSeeded()
{
	if (GPEPItemIdSeeded.load(std::memory_order_acquire))
	{
		return;
	}

	/*
	 * The legacy slot read goes through the save game system and creates a UObject, neither of
	 * which is safe off the game thread. Initialize() seeds on the game thread at startup, so this
	 * only fires for an allocation made off-thread before any GameInstance exists. The allocation
	 * itself stays unique (the counter is atomic); the legacy floor is applied by the next
	 * game-thread call and only ever raises the counter.
	 */
	if (!ensureMsgf(IsInGameThread(),
		TEXT("PEP item id allocator seeded off the game thread; deferring the legacy slot migration.")))
	{
		return;
	}

	// Game thread only from here, and nothing below re-enters the allocator, so the flag is set
	// once the floor is in place rather than guarded by a lock.
	if (const UPEPItemIdAllocatorSaveGame* LegacySave = Cast<UPEPItemIdAllocatorSaveGame>(
		UGameplayStatics::LoadGameFromSlot(LegacyAllocatorSaveSlotName, 0)))
	{
		// Preserve IDs from projects that used the former per-allocation local save slot.
		RaisePEPItemNextIdTo(FMath::Max(0, LegacySave->NextId));
	}
	GPEPItemIdSeeded.store(true, std::memory_order_release);
}

FPEPItemIdAllocatorSaveState UPEPIdAllocatorSubsystem::GetSaveState() const
{
	EnsureSeeded();
	FPEPItemIdAllocatorSaveState State;
	State.NextId = GPEPItemNextId.load(std::memory_order_relaxed);
	return State;
}

void UPEPIdAllocatorSubsystem::RestoreFromSaveState(const FPEPItemIdAllocatorSaveState& SaveState)
{
	if (const UWorld* World = GetWorld(); World && World->GetNetMode() == NM_Client)
	{
		UE_LOG(LogPEPItems, Warning,
			TEXT("RestoreFromSaveState rejected on a client; only the authority owns the item ID counter."));
		return;
	}

	EnsureSeeded();
	// Monotonic: a save older than the items already loaded or created cannot pull the counter back.
	RaisePEPItemNextIdTo(FMath::Max(0, SaveState.NextId));
}
