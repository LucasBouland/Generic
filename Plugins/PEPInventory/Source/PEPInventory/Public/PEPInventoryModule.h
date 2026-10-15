#pragma once

#include "Modules/ModuleManager.h"

struct FStreamableHandle;

class FPEPInventoryModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void HandlePostEngineInit();
    TSharedPtr<FStreamableHandle> WorldItemActorClassPreloadHandle;
};
