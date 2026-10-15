#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Equipment/PEPEquipmentAbilityMapLedger.h"
#include "Equipment/PEPEquipmentEligibilityProvider.h"
#include "Equipment/PEPEquipmentEventTypes.h"

#include "PEPEquipmentAbilityBridgeComponent.generated.h"

struct FStreamableHandle;
struct FPEPItemFragment_EquipmentAbilities;
class AController;
class APawn;
class UGMC_AbilitySystemComponent;
class UPEPEquipmentComponent;

/**
 * Grants what equipped items describe in FPEPItemFragment_EquipmentAbilities through the pawn's
 * GMC ability system, and refuses equipment whose active-tag requirements the wearer does not meet.
 *
 * Place it on the pawn next to a UPEPEquipmentComponent and a UGMC_AbilitySystemComponent; both
 * are found on the owner. It follows the public equipment projection through OnItemEquipped and
 * OnItemUnequipped, on every machine:
 *
 *  - Ability map entries are local GMAS data, not replicated, so they are written on the server
 *    and on every client alike — always with bGrantedByDefault cleared, so a client never grants.
 *  - Granting an InputTag (GrantedAbilityTags is GMC-bound, server-authoritative output) and
 *    applying EquippedEffects happen on the authority only. Effects use the ServerAuth queue:
 *    equipment changes arrive from server RPCs, store writes and replication, outside the GMC
 *    movement cycle, so a Predicted or PredictedQueued effect would be wrong, and nothing here
 *    runs in a prediction or replay path, where an irreversible grant must never happen.
 *
 * Equipment requests must likewise never be issued from a GMC prediction path (an ability's
 * predicted tick): they are server RPCs and would be re-sent on every replay.
 *
 * Only the authority and the owning client keep ability map entries: a simulated proxy (another
 * player's pawn on this machine) never activates an ability from input, so it skips the work and
 * holds nothing. A pawn whose role changes (possessed after spawn, unpossessed) is brought in
 * line when its controller changes.
 *
 * Grants follow the PUBLIC projection: an item in a slot whose profile entry has bShowPublicly off
 * raises no equipment event and so grants nothing, whatever its fragment says.
 */
UCLASS(ClassGroup=(PEP), BlueprintType, meta=(BlueprintSpawnableComponent))
class PEPEQUIPMENTGMAS_API UPEPEquipmentAbilityBridgeComponent
	: public UActorComponent
	, public IPEPEquipmentEligibilityProvider
{
	GENERATED_BODY()

public:
	UPEPEquipmentAbilityBridgeComponent();

	virtual FPEPItemResult CanEquipItem_Implementation(
		const UPEPEquipmentComponent* Equipment,
		const FPEPItemInstance& Item,
		const UPEPItemDefinition* Definition,
		FGameplayTag SlotTag) const override;

	UFUNCTION(BlueprintPure, Category="PEP|Equipment|Abilities")
	UPEPEquipmentComponent* GetEquipmentComponent() const;

	UFUNCTION(BlueprintPure, Category="PEP|Equipment|Abilities")
	UGMC_AbilitySystemComponent* GetAbilitySystemComponent() const;

	/** Whether equipment items currently contribute anything through this bridge. */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment|Abilities")
	bool IsItemApplied(FPEPItemId ItemId) const;

	/** Server only: the GMAS effect ids applied for ItemId, empty when none. */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment|Abilities")
	TArray<int32> GetAppliedEffectIds(FPEPItemId ItemId) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void HandleItemEquipped(const FPEPEquipmentChange& Change);

	UFUNCTION()
	void HandleItemUnequipped(const FPEPEquipmentChange& Change);

	/** Possession can turn a simulated proxy into the owning client's pawn, or back. */
	UFUNCTION()
	void HandleControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);

private:
	struct FAppliedEquipment
	{
		FPEPEquippedItemInfo Node;
		/** ServerAuth effect ids, authority only. */
		TArray<int32> EffectIds;
	};

	UGMC_AbilitySystemComponent* ResolveAbilitySystem() const;
	UPEPEquipmentComponent* ResolveEquipment() const;
	bool HasOwnerAuthority() const;
	/**
	 * Whether this machine needs ability map entries at all: the authority (which grants) and the
	 * owning client (which activates from input) do; a simulated proxy does not.
	 */
	bool NeedsAbilityMap() const;
	bool IsReadyToApply() const;

	/**
	 * Waits for the ability system's BeginPlay, which fills its AbilityMap from its own assets and
	 * would overwrite anything written earlier, then applies what is already equipped.
	 */
	void TryInitialSync();
	/**
	 * Brings applied state in line with the projection: used at start, after a load and after a
	 * controller change. On a simulated proxy it gives back whatever earlier writes left.
	 */
	void ReconcileWithProjection();

	void ApplyChange(const FPEPEquipmentChange& Change);
	void RevokeItem(FPEPItemId ItemId);
	void RevokeAll();
	/** Writes the ledger's answer for InputTag to the ASC and, on authority, settles the grant. */
	void SyncInputTag(FGameplayTag InputTag);

	const FPEPItemFragment_EquipmentAbilities* FindLoadedFragment(FPrimaryAssetId DefinitionId) const;
	void RequestDefinitionLoad(FPrimaryAssetId DefinitionId);
	void CancelDefinitionLoads();

	TWeakObjectPtr<UPEPEquipmentComponent> CachedEquipment;
	TWeakObjectPtr<UGMC_AbilitySystemComponent> CachedAbilitySystem;

	FPEPEquipmentAbilityMapLedger AbilityMapLedger;
	TMap<FPEPItemId, FAppliedEquipment> AppliedItems;
	TMap<FPrimaryAssetId, TSharedPtr<FStreamableHandle>> DefinitionLoadHandles;
	TSet<FPrimaryAssetId> FailedDefinitionLoads;

	bool bInitialSyncDone = false;
	int32 InitialSyncAttempts = 0;
};
