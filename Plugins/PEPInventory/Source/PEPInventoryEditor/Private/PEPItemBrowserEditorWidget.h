#pragma once

#include "CoreMinimal.h"
#include "EditorUtilityWidget.h"
#include "PEPItemBrowserEditorWidget.generated.h"

/**
 * Native Slate implementation hosted by the EUW asset. Keeping the browser in the editor module
 * prevents editor-only APIs and the PEP inventory authoring UI from entering runtime modules.
 *
 * Private, because nothing outside this module needs the type — the asset finds it by its script
 * path, which a header's location does not affect — and because its base comes from Blutility,
 * a private dependency of this module that a public header would leak to every includer.
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
