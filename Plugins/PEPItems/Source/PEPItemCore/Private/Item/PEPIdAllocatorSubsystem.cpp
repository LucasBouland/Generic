#include "Item/PEPIdAllocatorSubsystem.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Item/PEPItemLogChannels.h"

namespace
{
	const TCHAR* LegacyAllocatorSaveSlotName = TEXT("PEPItemIdAllocator");
}

int32 UPEPIdAllocatorSubsystem::NextId = 0;
bool UPEPIdAllocatorSubsystem::bSeeded = false;

void UPEPIdAllocatorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EnsureSeeded();
}

int32 UPEPIdAllocatorSubsystem::AllocateNext()
{
	EnsureSeeded();

	// Wrapping would hand out INDEX_NONE, then negatives that collide with test ids. An invalid id
	// is refused by every store, which is the loud failure this deserves.
	if (!ensureMsgf(NextId >= 0 && NextId < MAX_int32,
		TEXT("PEP item id counter exhausted (%d); refusing to allocate."), NextId))
	{
		return INDEX_NONE;
	}
	return NextId++;
}

void UPEPIdAllocatorSubsystem::EnsureSeeded()
{
	if (bSeeded)
	{
		return;
	}
	bSeeded = true;

	// Preserve IDs from projects that used the former per-allocation local save slot.
	if (const UPEPItemIdAllocatorSaveGame* LegacySave = Cast<UPEPItemIdAllocatorSaveGame>(
		UGameplayStatics::LoadGameFromSlot(LegacyAllocatorSaveSlotName, 0)))
	{
		NextId = FMath::Max(0, LegacySave->NextId);
	}
}

FPEPItemIdAllocatorSaveState UPEPIdAllocatorSubsystem::GetSaveState() const
{
	EnsureSeeded();
	FPEPItemIdAllocatorSaveState State;
	State.NextId = NextId;
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
	NextId = FMath::Max(NextId, FMath::Max(0, SaveState.NextId));
}
