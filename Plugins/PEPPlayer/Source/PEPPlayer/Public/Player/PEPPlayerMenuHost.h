#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Player/PEPPlayerInputTypes.h"
#include "PEPPlayerMenuHost.generated.h"

UINTERFACE(MinimalAPI, BlueprintType, meta=(CannotImplementInterfaceInBlueprint))
class UPEPPlayerMenuHost : public UInterface
{
	GENERATED_BODY()
};

/**
 * Player menu state contract implemented by APEPPlayerController.
 * UI modules use it through Cast<IPEPPlayerMenuHost>(PlayerController) instead of including the concrete GMC controller.
 */
class PEPPLAYER_API IPEPPlayerMenuHost
{
	GENERATED_BODY()

public:
	/** Returns true when the menu is open after the call. */
	virtual bool OpenMenu() = 0;
	virtual void CloseMenu() = 0;
	virtual bool IsMenuOpen() const = 0;
	virtual EPEPPlayerInputState GetPlayerInputState() const = 0;
	/** Broadcast after each completed Gameplay/Menu transition. */
	virtual FOnPEPPlayerInputStateChanged& GetOnPlayerInputStateChanged() = 0;
};
