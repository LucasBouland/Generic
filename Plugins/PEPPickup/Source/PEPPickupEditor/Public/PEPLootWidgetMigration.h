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
    UFUNCTION(BlueprintCallable, Category="PEP|Editor")
    static bool MigrateWidget(UBlueprint* Blueprint, bool bItemWindow);
};
