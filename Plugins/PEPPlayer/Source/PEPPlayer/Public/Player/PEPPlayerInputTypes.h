#pragma once

#include "CoreMinimal.h"
#include "PEPPlayerInputTypes.generated.h"

/** Local input state owned by the PEP player controller. */
UENUM(BlueprintType)
enum class EPEPPlayerInputState : uint8 { Gameplay, Menu };

/** Input mode applied outside the player menu (BeginPlay and after closing the menu). */
UENUM(BlueprintType)
enum class EPEPGameplayInputMode : uint8
{
	/** Viewport only: the viewport captures the mouse and hides the cursor while captured. */
	GameOnly,
	/** Slate first, then the viewport: keeps a visible cursor usable during gameplay (strategy/RTS). */
	GameAndUI UMETA(DisplayName="Game And UI")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPEPPlayerInputStateChanged, EPEPPlayerInputState, InputState);
