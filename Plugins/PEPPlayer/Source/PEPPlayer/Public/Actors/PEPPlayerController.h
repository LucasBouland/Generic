#pragma once

#include "CoreMinimal.h"
#include "Actors/GMCPlayerController.h"
#include "InputCoreTypes.h"
#include "Player/PEPPlayerInputTypes.h"
#include "Player/PEPPlayerMenuHost.h"
#include "PEPPlayerController.generated.h"

class FModifierKeysState;
class UInputAction;
class UInputMappingContext;
class UUserWidget;
class UEnhancedInputLocalPlayerSubsystem;
class IPEPPlayerHUDContract;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPEPMenuCommand, UInputAction*, Action);

/** Outcome of matching a key press against the menu shortcuts. */
enum class EPEPMenuShortcutMatch : uint8
{
	/** No bridged mapping uses the key: Slate and Enhanced Input handle it normally. */
	None,
	/**
	 * A bridged mapping uses the key but held Ctrl/Alt/Cmd do not match it. Once focused widgets decline the key,
	 * consume it without dispatch, otherwise it reaches the viewport and Enhanced Input fires the plain mapping anyway.
	 */
	Suppressed,
	/** The key press is the returned action's shortcut. */
	Matched
};

/** Local input policy: gameplay contexts / menu context, standard Enhanced Input. */
UCLASS(Blueprintable)
class PEPPLAYER_API APEPPlayerController : public AGMC_PlayerController, public IPEPPlayerMenuHost
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
	virtual bool OpenMenu() override;
	UFUNCTION(BlueprintCallable, Category="PEP|UI")
	void ToggleMenu();
	UFUNCTION(BlueprintCallable, Category="PEP|UI")
	virtual void CloseMenu() override;
	UFUNCTION(BlueprintPure, Category="PEP|UI")
	virtual bool IsMenuOpen() const override { return InputState == EPEPPlayerInputState::Menu; }
	UFUNCTION(BlueprintPure, Category="PEP|UI")
	bool CanOpenMenu() const;
	UFUNCTION(BlueprintPure, Category="PEP|UI")
	bool IsGameplayInputAllowed() const { return IsLocalController() && IsValid(GetPawn()) && !IsMenuOpen() && !bTransitionInProgress; }
	UFUNCTION(BlueprintPure, Category="PEP|UI")
	virtual EPEPPlayerInputState GetPlayerInputState() const override { return InputState; }
	UFUNCTION(BlueprintPure, Category="PEP|UI")
	UUserWidget* GetPlayerHUD() const { return PlayerHUD; }
	UFUNCTION(BlueprintPure, Category="PEP|UI")
	UUserWidget* GetPlayerMenu() const;
	virtual FOnPEPPlayerInputStateChanged& GetOnPlayerInputStateChanged() override { return OnPlayerInputStateChanged; }

	/**
	 * Matches a key press against the menu shortcuts while the menu is open, reading the live Enhanced Input mappings
	 * (active contexts, player remaps). Only Boolean press mappings qualify: no trigger, Down or Pressed, plus chords on
	 * modifier keys (Shift/Ctrl/Alt/Cmd). Ctrl, Alt or Cmd held without a chord requiring them suppresses the mapping.
	 * OutAction is set only for Matched.
	 */
	EPEPMenuShortcutMatch ResolveMenuShortcut(const FKey& Key, const FModifierKeysState& ModifierKeys, UInputAction*& OutAction) const;
	/** Focused UMG shortcut bridge: dispatches a matched action (not on repeats). True when matched; the caller consumes the key. */
	bool RouteMenuShortcut(const FKey& Key, bool bRepeat, const FModifierKeysState& ModifierKeys);

	UPROPERTY(BlueprintAssignable, Category="PEP|UI")
	FOnPEPPlayerInputStateChanged OnPlayerInputStateChanged;
	UPROPERTY(BlueprintAssignable, Category="PEP|UI")
	FOnPEPMenuCommand OnMenuCommand;

protected:
	virtual void SetupInputComponent() override;
	/**
	 * Applies the out-of-menu cursor and input mode for this player (BeginPlay, menu close). Focus changes go through
	 * SetInputMode and target only this player's Slate user. Override for project policies (mouse lock, custom focus).
	 */
	virtual void ApplyGameplayInput();

	UPROPERTY(EditDefaultsOnly, Category="PEP|UI")
	TSubclassOf<UUserWidget> PlayerHUDClass;
	UPROPERTY(EditDefaultsOnly, Category="PEP|UI")
	int32 PlayerHUDZOrder = 0;
	/** Keep the cursor visible outside the menu (strategy/RTS). Pair it with GameAndUI: GameOnly hides it while the viewport captures the mouse. */
	UPROPERTY(EditDefaultsOnly, Category="PEP|Input")
	bool bShowMouseCursorInGameplay = false;
	/** Input mode outside the menu. GameAndUI gives Slate first access to input and focuses the game viewport. */
	UPROPERTY(EditDefaultsOnly, Category="PEP|Input")
	EPEPGameplayInputMode GameplayInputMode = EPEPGameplayInputMode::GameOnly;
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
	/** Routed action matching Action and its precedence (Close, Toggle, Open, then MenuCommandActions order). */
	UInputAction* FindMenuShortcutAction(const UInputAction* Action, int32& OutRank) const;
	void HandleMenuAction(UInputAction* Action);
	void HandleMenuCommand(UInputAction* Action);
	void CancelPlayerDragDrop();
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
