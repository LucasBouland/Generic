#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/PEPInventoryRequest.h"
#include "Components/PEPInventoryTransfer.h"
#include "Components/PEPInventoryTypes.h"
#include "Item/PEPContainerAccessTypes.h"
#include "Item/PEPItemAccess.h"
#include "Net/PEPInventoryNetUtility.h"
#include "PEPInventorySessionComponent.generated.h"

class AController;
class APawn;
class UPEPInventoryComponent;
class UPEPItemDefinition;
struct FPEPItemStoreChange;

/**
 * One player's window into a store that is not theirs: a corpse, a crate, a stash.
 *
 * It lives on the PlayerController, not on the pawn, because it is a fact about the player — "this
 * player is looking into that container" — and a PlayerController exists only on the server and on
 * the one client it belongs to. That is exactly who a session concerns: the mirror replicates to
 * the looter and to nobody else, with nothing to hide from simulated proxies because nothing ever
 * reaches them, and a bot pawn carries no session at all since its controller has none.
 *
 * The server holds the real session: a reference to the remote store, a distance check, and a
 * subscription to that store's changes, mirrored one entry at a time into SessionView. The client
 * holds the mirror, the session id its requests must quote, and a scratch copy of the mirror to
 * preview placements with. The player's own store is whichever UPEPInventoryComponent this player
 * answers for — the possessed pawn's, by default — and is only ever reached through
 * GetLocalInventory, so a respawn does not strand a session on a dead pawn.
 */
UCLASS(ClassGroup=(PEP), meta=(BlueprintSpawnableComponent))
class PEPINVENTORY_API UPEPInventorySessionComponent : public UActorComponent
{
	GENERATED_BODY()

	/* The fast array reports its own deltas; nothing else may announce a session change. */
	friend struct FPEPInventoryView;

public:
	UPEPInventorySessionComponent();

	virtual void PostInitProperties() override;
	virtual void OnRegister() override;
	/** Ends an open session: a looter that leaves the world must not keep a store subscribed. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * The session component of whoever drives Actor: Actor's own when it has one, else the one on
	 * the controller of the pawn Actor is. Null for anything that is not a player, bots included.
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Session", meta=(DefaultToSelf="Actor"))
	static UPEPInventorySessionComponent* FindSessionComponent(const AActor* Actor);

	/**
	 * Names the store this player takes into and puts from. Unset, it is resolved on every use:
	 * the possessed pawn's inventory, then the controller's, then the player state's. Set it for a
	 * store kept somewhere else, or when the component does not sit on a controller at all.
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|Session")
	void SetLocalInventory(UPEPInventoryComponent* InInventory);

	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Session")
	UPEPInventoryComponent* GetLocalInventory() const;

	// ---- Session state, replicated to the owner --------------------------------------------

	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Session")
	bool HasTransferSession() const
	{
		return bHasTransferSession;
	}

	/** Identifies the current session for UI contexts; changes on every successful open. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Session")
	FGuid GetTransferSessionId() const
	{
		return bHasTransferSession ? TransferSessionId : FGuid();
	}

	/** Presentation tag the remote inventory asked for; invalid when no session is open. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Session")
	FGameplayTag GetTransferSessionPanelTag() const
	{
		return bHasTransferSession ? SessionLootPanelTag : FGameplayTag();
	}

	/** The active session's capability; None when no remote view is open. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Session")
	EPEPInventorySessionMode GetTransferSessionMode() const
	{
		return bHasTransferSession ? SessionMode : EPEPInventorySessionMode::None;
	}

	/** True while the session is the read-only inspection projection. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Session")
	bool IsTransferSessionReadOnly() const
	{
		return GetTransferSessionMode() == EPEPInventorySessionMode::Inspect;
	}

	/** Root item shown by an inspection session; invalid for transfer sessions or when closed. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Session")
	FPEPItemId GetTransferSessionRootItemId() const
	{
		return IsTransferSessionReadOnly() ? SessionRootItemId : FPEPItemId();
	}

	// ---- The mirror ---------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Session")
	void GetSessionItemIds(TArray<FPEPItemId>& OutItemIds) const;

	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Session")
	bool FindSessionEntry(FPEPItemId ItemId, FPEPInventoryEntry& OutEntry) const;

	/** Searches only the session view. Returns null if absent/unloaded; never loads assets. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Session", meta=(DisplayName="Get Session Item Definition By Id"))
	UPEPItemDefinition* GetSessionItemDefinitionById(FPEPItemId ItemId) const;

	// ---- Opening and closing, server only ---------------------------------------------------

	/**
	 * Whether OpenTransferSession would open a session on RemoteInventory right now, and why not.
	 * The single place that rule lives: interactables ask this rather than re-deriving it, and add
	 * only their own conditions (locked, still alive). Authority only, like the open it predicts.
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Session")
	FPEPItemResult CanOpenTransferSession(
		const UPEPInventoryComponent* RemoteInventory,
		float MaxDistance = 300.0f) const;

	/** Whether an attachment-only inspection view can open for this root item right now. */
	UFUNCTION(BlueprintPure, Category="PEP|Inventory|Session")
	FPEPItemResult CanOpenInspectionSession(
		const UPEPInventoryComponent* RemoteInventory,
		FPEPItemId InspectionRootItemId,
		float MaxDistance = 300.0f) const;

	/** Opens the only remote inventory that this player may currently access. Server only. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Inventory|Session")
	bool OpenTransferSession(
		UPEPInventoryComponent* RemoteInventory,
		float MaxDistance = 300.0f);

	/** Opens a read-only view of the root item and its Attachment branches. Server only. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Inventory|Session")
	bool OpenInspectionSession(
		UPEPInventoryComponent* RemoteInventory,
		FPEPItemId InspectionRootItemId,
		float MaxDistance = 300.0f);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Inventory|Session")
	void CloseTransferSession();

	/** Closes the session only if ExpectedSessionId still identifies the session shown by the caller. */
	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|Session")
	EPEPRequestResult RequestCloseTransferSession(FGuid ExpectedSessionId);

	// ---- Requests, from the owning client -------------------------------------------------

	/** Takes an item from the identified remote session and places it in the local inventory. */
	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|Session")
	EPEPRequestResult RequestTakeFromSession(
		FGuid ExpectedSessionId,
		FPEPItemId RemoteItemId,
		FPEPItemLocation LocalDestination,
		int32& OutRequestId);

	/** Optimistic display check only; the server repeats every source and destination rule. */
	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|Session|Preview")
	FPEPItemResult PreviewTakeFromSession(
		FPEPItemId RemoteItemId,
		FPEPItemLocation LocalDestination) const;

	/** Puts an item from the local inventory into the identified remote session. */
	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|Session")
	EPEPRequestResult RequestPutIntoSession(
		FGuid ExpectedSessionId,
		FPEPItemId LocalItemId,
		FPEPItemLocation RemoteDestination,
		int32& OutRequestId);

	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|Session|Preview")
	FPEPItemResult PreviewPutIntoSession(
		FPEPItemId LocalItemId,
		FPEPItemLocation RemoteDestination) const;

	/** Moves an item in the identified remote inventory, using the player's access rights. */
	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|Session")
	EPEPRequestResult RequestMoveInSession(
		FGuid ExpectedSessionId, FPEPItemId RemoteItemId,
		FPEPItemLocation RemoteDestination, int32& OutRequestId);

	/** Optimistic client preview; the authoritative store repeats all validation. */
	UFUNCTION(BlueprintCallable, Category="PEP|Inventory|Session|Preview")
	FPEPItemResult PreviewMoveInSession(
		FPEPItemId RemoteItemId, FPEPItemLocation RemoteDestination) const;

	/** Session requests only; the local inventory resolves its own. */
	UPROPERTY(BlueprintAssignable, Category="PEP|Inventory|Request")
	FOnPEPInventoryRequestResolved OnRequestResolved;

	UPROPERTY(BlueprintAssignable, Category="PEP|Inventory|Session")
	FOnPEPInventorySessionViewChanged OnSessionViewChanged;

protected:
	UFUNCTION(Server, Reliable)
	void Server_RequestTakeFromSession(
		FGuid ExpectedSessionId,
		FPEPItemId RemoteItemId,
		FPEPItemLocation LocalDestination,
		int32 RequestId);

	UFUNCTION(Server, Reliable)
	void Server_RequestPutIntoSession(
		FGuid ExpectedSessionId,
		FPEPItemId LocalItemId,
		FPEPItemLocation RemoteDestination,
		int32 RequestId);

	UFUNCTION(Server, Reliable)
	void Server_RequestMoveInSession(
		FGuid SessionId, FPEPItemId RemoteItemId, FPEPItemLocation RemoteDestination, int32 RequestId);

	UFUNCTION(Server, Reliable)
	void Server_RequestCloseTransferSession(FGuid ExpectedSessionId);

	UFUNCTION(Client, Reliable)
	void Client_ResolveInventoryRequest(FPEPInventoryRequestResult RequestResult);

private:
	bool HasAuthority() const;

	/**
	 * The identity requests are made on behalf of: the one the local store already answers to, so
	 * a request judged by two stores is judged against the same requester on both sides. With no
	 * local store, the controller this component sits on.
	 */
	FPEPItemAccessContext GetRequestContext() const;

	/** The actor whose position counts for distance: the player's body, not the controller. */
	const AActor* GetLocationActor() const;

	bool IsTransferSessionValid() const;
	/** The one distance metric, shared by opening a session and keeping it open. */
	bool IsWithinSessionDistance(const UPEPInventoryComponent& RemoteInventory, float MaxDistance) const;
	void ValidateActiveTransferSession();

	bool OpenSessionInternal(
		UPEPInventoryComponent* RemoteInventory,
		float MaxDistance,
		EPEPInventorySessionMode Mode,
		FPEPItemId InspectionRootItemId);

	/** The role the remote store grants this player. Meaningless without a session. */
	EPEPAccessRole ResolveSessionRole() const;

	void RefreshSessionView();
	void HandleRemoteStoreChanged(const FPEPItemStoreChange& Change);

	/**
	 * Mirrors one remote change into the session snapshot instead of rebuilding it.
	 *
	 * Rebuilding resets every replication id, so the fast array resends the whole snapshot for a
	 * single item taken — the cost of looting a container becomes the container, not the item.
	 * Touching only the entry that changed is what keeps the two proportional.
	 *
	 * Anything that cannot be applied falls back to the full rebuild: a snapshot that is expensive
	 * is a great deal better than one that is wrong.
	 */
	void ApplySessionViewChange(const FPEPItemStoreChange& Change);

	/*
	 * Events that end a session at once rather than at the next distance check: the store's actor
	 * leaving play (destroyed, picked up, despawned, streamed out) and the player changing body.
	 */
	UFUNCTION()
	void HandleSessionRemoteEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason);
	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	/**
	 * Whether Role may see this item at all in a mirror of Store.
	 *
	 * Walked upwards rather than down: an item is visible when the slot holding it reveals its
	 * occupants, the container owning that slot reveals its contents, and that container is itself
	 * visible by the same rule. One item costs its own depth, which is what lets both the whole
	 * snapshot and a single replicated change be answered by the same function.
	 *
	 * An item in a named slot is visible. Named slots carry no visibility vocabulary — their
	 * meaning belongs to whichever domain claimed the tag — and what a body was wearing is what a
	 * looter is meant to see; equipment already projects it publicly to everyone in range.
	 */
	static bool IsItemVisibleInExternalView(
		const UPEPInventoryComponent& Store,
		FPEPItemId ItemId,
		EPEPAccessRole Role);

	/** Whether an entry belongs to the inspected root or an Attachment branch beneath it. */
	static bool IsItemInInspectionProjection(
		const UPEPInventoryComponent& Store,
		FPEPItemId ItemId,
		FPEPItemId InspectionRootItemId);

	/* Reached only by the fast array, through the friendship declared at the top of this class. */
	void HandleReplicatedSessionViewChanged();
	/** The only way SessionView changes are announced, so the preview scratch cannot outlive its copy. */
	void NotifySessionViewChanged();

	UFUNCTION()
	void OnRep_TransferSessionState();

	int32 AllocateRequestId();
	void ResolveInventoryRequest(
		int32 RequestId,
		EPEPInventoryRequestKind Kind,
		const FPEPItemResult& Result);

	/**
	 * Client stand-in for the remote store: SessionView as an inventory, so a preview runs the
	 * store's own validation. Kept and refreshed only when the view changes — a drag previews on
	 * every mouse move, and a new component with a fresh copy of the view each time was garbage.
	 * The remote store's location validators never reach the client, so the local store's stand in
	 * for them: a preview approximation the server's own check replaces.
	 */
	const UPEPInventoryComponent* GetSessionPreviewScratch() const;

	/** Owner-only copy of the active remote inventory. It is display data, never an authority. */
	UPROPERTY(Replicated)
	FPEPInventoryView SessionView;

	UPROPERTY(ReplicatedUsing=OnRep_TransferSessionState)
	bool bHasTransferSession = false;

	UPROPERTY(ReplicatedUsing=OnRep_TransferSessionState)
	FGuid TransferSessionId;

	/** The remote store's ExternalAccess, so client previews resolve the role the server will. */
	UPROPERTY(Replicated)
	EPEPExternalAccessMode SessionExternalAccess = EPEPExternalAccessMode::SlotRules;

	UPROPERTY(ReplicatedUsing=OnRep_TransferSessionState)
	FGameplayTag SessionLootPanelTag;

	UPROPERTY(ReplicatedUsing=OnRep_TransferSessionState)
	EPEPInventorySessionMode SessionMode = EPEPInventorySessionMode::None;

	UPROPERTY(ReplicatedUsing=OnRep_TransferSessionState)
	FPEPItemId SessionRootItemId;

	/** Set through SetLocalInventory; unset, the store is resolved from the owner on every use. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UPEPInventoryComponent> ExplicitLocalInventory;

	UPROPERTY(Transient)
	mutable TObjectPtr<UPEPInventoryComponent> SessionPreviewScratch;
	mutable bool bSessionPreviewScratchDirty = true;

	/** Authority-side only. Its replicated counterparts are SessionView and the flag. */
	FPEPInventoryTransferSession TransferSession;
	int32 NextRequestId = 1;
};
