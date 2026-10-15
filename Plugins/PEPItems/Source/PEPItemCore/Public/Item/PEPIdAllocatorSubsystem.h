#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "PEPIdAllocatorSubsystem.generated.h"

/** Snapshot to include in the project's normal save data. Produced by GetSaveState, never authored. */
USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemIdAllocatorSaveState
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="PEP|Item|Save")
	int32 NextId = 0;
};

/**
 * Owns the process-wide FPEPItemId counter.
 *
 * The counter is static on this class rather than per-instance state, deliberately: correctness
 * only requires that every id handed out anywhere in this process is unique, and one shared
 * counter guarantees that regardless of how many GameInstances share the process — multi-instance
 * PIE included. Nothing here needs to attribute an id to a particular instance, only to never
 * reuse one.
 *
 * Item instances persist their own IDs. The project's save system captures this small counter
 * state once with the rest of a save and restores it before creating or loading items. The only
 * disk read is the one-time legacy slot migration in EnsureSeeded, on the first allocation of the
 * process; every allocation after that is an atomic increment. IDs are never recycled; billions are
 * assumed enough for the life of a project, and AllocateNext refuses to wrap when they are not.
 *
 * Invariants:
 * - AllocateNext never returns MAX_int32. That value is reserved for preview records
 *   (PEPItemMutation::GetPreviewItemId), so a preview can never collide with a real item.
 * - The counter only moves forwards: allocation, ObserveLoadedId, RestoreFromSaveState and the legacy
 *   seed all raise it, none lowers it.
 *
 * Thread safety: the counter is atomic, so allocation and ObserveLoadedId (reached from
 * FPEPItemInstance::PostSerialize, which may run on a loading thread) are safe from any thread. The
 * one-time legacy seed reads a save slot and creates a UObject, so it only runs on the game thread;
 * Initialize() performs it at startup.
 *
 * Initialize() marks the counter ready. AllocateNext() also initializes lazily so tests and
 * commandlets do not depend on a GameInstance having started.
 */
UCLASS()
class PEPITEMCORE_API UPEPIdAllocatorSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Capture this alongside the project's normal save data. */
	UFUNCTION(BlueprintPure, Category="PEP|Item|Save")
	FPEPItemIdAllocatorSaveState GetSaveState() const;

	/**
	 * Restore before loading or creating items. Monotonic: it never moves the live process counter
	 * backwards, so it cannot drop below ids already allocated or observed in loaded records.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Item|Save")
	void RestoreFromSaveState(const FPEPItemIdAllocatorSaveState& SaveState);

	/**
	 * Records that an item with this id exists, so the counter never hands it out again:
	 * NextId becomes max(NextId, Id + 1). Called by FPEPItemInstance::PostSerialize for every record
	 * loaded from an archive, which keeps loaded saves safe even when the project restores the
	 * allocator state late or not at all. Negative ids (automation tests) and the reserved preview
	 * id are ignored: the allocator can never produce them. Thread-safe.
	 */
	static void ObserveLoadedId(int32 Id);

private:
	friend struct FPEPItemInstance;

	/** Allocates in-memory IDs only for FPEPItemInstance's internal creation paths. */
	static int32 AllocateNext();

	static void EnsureSeeded();
};
