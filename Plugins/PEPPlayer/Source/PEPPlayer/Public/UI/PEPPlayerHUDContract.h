#pragma once

#include "UObject/Interface.h"
#include "PEPPlayerHUDContract.generated.h"

class APawn;
class UUserWidget;
class UWidget;

UINTERFACE(MinimalAPI, BlueprintType)
class UPEPPlayerHUDContract : public UInterface
{
	GENERATED_BODY()
};

/** Contract between player input/lifecycle and the optional player-specific UI module. */
class PEPPLAYER_API IPEPPlayerHUDContract
{
	GENERATED_BODY()

public:
	virtual bool CanShowMenu() const = 0;
	virtual UUserWidget* GetMenuWidget() const = 0;
	virtual UWidget* ShowMenu(bool bOpen) = 0;
	virtual void NotifyControlledPawnChanged(APawn* PreviousPawn, APawn* NewPawn) = 0;
};
