#pragma once

#include "Modules/ModuleManager.h"

/** Registers the item definition browser as a Window tab. Nothing outside this module needs it. */
class FPEPItemCoreEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void CheckAssetManagerConfiguration();
	bool bRegisteredMessageLog = false;
};
