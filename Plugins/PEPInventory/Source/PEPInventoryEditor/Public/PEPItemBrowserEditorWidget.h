#pragma once

#include "CoreMinimal.h"
#include "EditorUtilityWidget.h"
#include "PEPItemBrowserEditorWidget.generated.h"

/**
 * Native Slate implementation hosted by the EUW asset. Keeping the browser in the editor module
 * prevents editor-only APIs and the PEP inventory authoring UI from entering runtime modules.
 */
UCLASS(Blueprintable)
class PEPINVENTORYEDITOR_API UPEPItemBrowserEditorWidget : public UEditorUtilityWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TSharedPtr<SWidget> NativeContent;
};
