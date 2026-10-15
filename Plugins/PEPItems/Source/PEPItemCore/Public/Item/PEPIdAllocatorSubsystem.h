#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "PEPIdAllocatorSubsystem.generated.h"

/** Snapshot to include in the project's normal save data. */
USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemIdAllocatorSaveState
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="PEP|Item|Save")
	int32 NextId = 0;
};

/** Legacy standalone counter record. Read once for migration; new saves use the project's save system. */
UCLASS()
class UPEPItemIdAllocatorSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
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
 * process; every allocation after that is a memory increment. IDs are never recycled; billions are
 * assumed enough for the life of a project, and AllocateNext refuses to wrap when they are not.
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

	/** Restore before loading or creating items; never moves the live process counter backwards. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Item|Save")
	void RestoreFromSaveState(const FPEPItemIdAllocatorSaveState& SaveState);

private:
	friend struct FPEPItemInstance;

	/** Allocates in-memory IDs only for FPEPItemInstance's internal creation paths. */
	static int32 AllocateNext();

	static void EnsureSeeded();

	static int32 NextId;
	static bool bSeeded;
};
