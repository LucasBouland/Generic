#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"

#include "PEPItemIdAllocatorLegacySaveGame.generated.h"

/**
 * Legacy standalone counter record. Read once for migration; new saves use the project's save system.
 *
 * Private to PEPItemCore: only the allocator reads it. The reflected path stays
 * /Script/PEPItemCore.PEPItemIdAllocatorSaveGame (plus the PEPCore redirect in DefaultEngine.ini), so
 * slots written by older builds still resolve to this class.
 */
UCLASS()
class UPEPItemIdAllocatorSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 NextId = 0;
};
