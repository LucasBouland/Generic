#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PEPInventoryUISettings.generated.h"

class UPEPInventoryWindow;

/** Project-wide inventory UI palette, shared by local and remote inventory windows. */
UCLASS(Config=Game, DefaultConfig, BlueprintType, meta=(DisplayName="Inventory UI"))
class PEPINVENTORYUI_API UPEPInventoryUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * Opens a session in the PEP player menu instead of a floating window: the menu opens with it,
	 * closing the menu ends it, and widgets show the loot side from OnTransferSessionPresented.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Session")
	bool bPresentSessionInPlayerMenu = true;

	/** Optional existing project window used when a world-item session opens outside the menu. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Session")
	TSoftClassPtr<UPEPInventoryWindow> SessionWindowClass;

	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("PEP"); }
	virtual FName GetSectionName() const override { return TEXT("InventoryUI"); }

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Drop Preview")
	FLinearColor ValidDropColor = FLinearColor(0.1f, 0.8f, 0.15f, 1.f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Drop Preview")
	FLinearColor InvalidDropColor = FLinearColor(0.9f, 0.1f, 0.1f, 1.f);

	/** Multiplies the selected drop color's alpha in GetDropPreviewColor. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Drop Preview",
		meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float PreviewOpacity = 0.35f;

	/** Ordinary item/slot hover tint, independent of drop preview opacity. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Hover")
	FLinearColor HoverColor = FLinearColor(0.2f, 0.5f, 1.f, 1.f);
};