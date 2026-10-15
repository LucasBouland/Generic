#pragma once

#include "CoreMinimal.h"
#include "Actors/GMCPlayerController.h"
#include "InputCoreTypes.h"
#include "PEPPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UUserWidget;
class UEnhancedInputLocalPlayerSubsystem;
class IPEPPlayerHUDContract;

UENUM(BlueprintType)
enum class EPEPPlayerInputState : uint8 { Gameplay, Menu };
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPEPPlayerInputStateChanged, EPEPPlayerInputState, InputState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPEPMenuCommand, UInputAction*, Action);

/** Local input policy: gameplay contexts / menu context, standard Enhanced Input. */
UCLASS(Blueprintable)
class PEPPLAYER_API APEPPlayerController : public AGMC_PlayerController
{
	GENERATED_BODY()
public:
	APEPPlayerController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetPawn(APawn* InPawn) override;
	virtual void OnRep_Pawn() override;

	UFUNCTION(BlueprintCallable, Category="PEP|UI")
	bool InitializePlayerHUD();
	UFUNCTION(BlueprintCallable, Category="PEP|UI")
	bool OpenMenu();
	UFUNCTION(BlueprintCallable, Category="PEP|UI")
	void ToggleMenu();
	UFUNCTION(BlueprintCallable, Category="PEP|UI")
	void CloseMenu();
	UFUNCTION(BlueprintPure, Category="PEP|UI")
	bool IsMenuOpen() const { return InputState == EPEPPlayerInputState::Menu; }
	UFUNCTION(BlueprintPure, Category="PEP|UI")
	bool CanOpenMenu() const;
	UFUNCTION(BlueprintPure, Category="PEP|UI")
	bool IsGameplayInputAllowed() const { return IsLocalController() && IsValid(GetPawn()) && !IsMenuOpen() && !bTransitionInProgress; }
	UFUNCTION(BlueprintPure, Category="PEP|UI")
	EPEPPlayerInputState GetPlayerInputState() const { return InputState; }
	UFUNCTION(BlueprintPure, Category="PEP|UI")
	UUserWidget* GetPlayerHUD() const { return PlayerHUD; }
	UFUNCTION(BlueprintPure, Category="PEP|UI")
	UUserWidget* GetPlayerMenu() const;

	/** Focused UMG shortcut bridge: resolves current remapped keys, no hardcoded physical keys. */
	bool RouteMenuShortcut(FKey Key, bool bRepeat);

	UPROPERTY(BlueprintAssignable, Category="PEP|UI")
	FOnPEPPlayerInputStateChanged OnPlayerInputStateChanged;
	UPROPERTY(BlueprintAssignable, Category="PEP|UI")
	FOnPEPMenuCommand OnMenuCommand;

protected:
	virtual void SetupInputComponent() override;
	UPROPERTY(EditDefaultsOnly, Category="PEP|UI")
	TSubclassOf<UUserWidget> PlayerHUDClass;
	UPROPERTY(EditDefaultsOnly, Category="PEP|UI")
	int32 PlayerHUDZOrder = 0;
	/** These contexts are owned/installed by the Pawn; only active ones are suspended. */
	UPROPERTY(EditDefaultsOnly, Category="PEP|Input")
	TArray<TObjectPtr<UInputMappingContext>> GameplayMappingContexts;
	UPROPERTY(EditDefaultsOnly, Category="PEP|Input")
	TObjectPtr<UInputMappingContext> MenuMappingContext;
	UPROPERTY(EditDefaultsOnly, Category="PEP|Input")
	int32 MenuMappingPriority = 100;
	/** Optional Boolean press action mapped in both gameplay and menu IMCs. */
	UPROPERTY(EditDefaultsOnly, Category="PEP|Input")
	TObjectPtr<UInputAction> ToggleMenuAction;
	/** Map this Boolean press action in the gameplay IMC. */
	UPROPERTY(EditDefaultsOnly, Category="PEP|Input")
	TObjectPtr<UInputAction> OpenMenuAction;
	/** Optional Boolean press action mapped in the menu IMC. */
	UPROPERTY(EditDefaultsOnly, Category="PEP|Input")
	TObjectPtr<UInputAction> CloseMenuAction;
	/** Optional Boolean press shortcuts (tabs, inventory commands), dispatched through OnMenuCommand. */
	UPROPERTY(EditDefaultsOnly, Category="PEP|Input")
	TArray<TObjectPtr<UInputAction>> MenuCommandActions;

private:
	UEnhancedInputLocalPlayerSubsystem* GetInputSubsystem() const;
	void HandleMenuAction(UInputAction* Action);
	void HandleMenuCommand(UInputAction* Action);
	void ApplyGameplayInput();
	void ReleaseMenuInputBlocks();
	void SynchronizePawn();
	void RestoreContexts(bool bRestoreGameplay);
	IPEPPlayerHUDContract* GetPlayerHUDContract() const;
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> PlayerHUD;
	UPROPERTY(Transient)
	TMap<TObjectPtr<UInputMappingContext>, int32> SuspendedContexts;
	UPROPERTY(Transient)
	EPEPPlayerInputState InputState = EPEPPlayerInputState::Gameplay;
	TWeakObjectPtr<APawn> ObservedPawn;
	TWeakObjectPtr<APawn> MenuPawn;
	bool bMenuInputBlocksApplied = false;
	bool bTransitionInProgress = false;
	bool bMenuContextAdded = false;
};
