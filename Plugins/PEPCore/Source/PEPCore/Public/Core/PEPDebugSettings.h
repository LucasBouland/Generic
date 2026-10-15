#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PEPDebugSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="PEP Debug"))
class PEPCORE_API UPEPDebugSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="PEP|Debug")
	bool bEnablePEPDebug = false;

	UFUNCTION(BlueprintPure, Category="PEP|Debug|Settings")
	bool IsEnabled() const { return bEnablePEPDebug; }
};
