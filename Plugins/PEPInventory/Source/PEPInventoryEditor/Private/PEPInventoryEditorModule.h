#pragma once

#include "Modules/ModuleManager.h"

class FPEPInventoryEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void CheckAssetManagerConfiguration();
	void RegisterMenus();
	void PopulatePEPSubMenu(class UToolMenu* Menu);
	void OpenItemBrowser(const struct FToolMenuContext& Context);
	FDelegateHandle MenuStartupHandle;
	bool bRegisteredMessageLog = false;
};
