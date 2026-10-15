#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PEPLootWidgetMigration.generated.h"
class UBlueprint;
/** Explicit editor migration for existing item-window graphs. Never runs on asset load. */
UCLASS()
class PEPPICKUPEDITOR_API UPEPLootWidgetMigration : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /**
     * Re-points slot queries at the UI subsystem, binds the native window variables and, with
     * bItemWindow, wires RefreshSessionContents. Checks everything it needs before editing: a
     * graph it cannot migrate is left untouched and false is returned. The edits form one undoable
     * transaction. Returns whether the Blueprint compiles afterwards.
     */
    UFUNCTION(BlueprintCallable, Category="PEP|Editor")
    static bool MigrateWidget(UBlueprint* Blueprint, bool bItemWindow);
};
