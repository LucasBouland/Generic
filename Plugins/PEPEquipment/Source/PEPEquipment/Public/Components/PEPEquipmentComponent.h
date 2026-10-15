#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Containers/Ticker.h"
#include "Core/PEPTypes.h"
#include "Equipment/PEPEquipmentEventTypes.h"
#include "Equipment/PEPEquipmentNetTypes.h"
#include "Equipment/PEPEquipmentPresentationTypes.h"
#include "Equipment/PEPEquipmentTypes.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemLocation.h"
#include "Item/PEPItemResultTypes.h"

#include "PEPEquipmentComponent.generated.h"

class APEPEquippedPresentationActor;
class AController;
class APawn;
struct FStreamableHandle;
class UPEPEquipmentProfile;
class USceneComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UPEPItemDefinition;
struct FPEPItemAccessContext;
struct FPEPItemStoreChange;
class IPEPItemStore;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPEPPublicEquipmentChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPEPOwnerEquipmentChanged);
DECLARE_MULTICAST_DELEGATE(FOnPEPLocalEquipmentPresentationChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnPEPEquipmentRequestResolved,
	const FPEPEquipmentRequestResult&,
	RequestResult);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnPEPEquipmentItemChanged,
	const FPEPEquipmentChange&,
	Change);

/**
 * Bridges an authoritative item store to a small public equipment projection. Presentation actors
 * are local cosmetics reconstructed from that projection; private item state never crosses.
 *
 * Beside it, the authority and the owning client keep an owner view: every item under every slot
 * of the profile, hidden slots included, computed locally from the full item store those two
 * machines hold. It is never replicated; a simulated proxy keeps only the public projection.
 *
 * Store changes are folded into one rebuild per frame: the authority marks both views dirty on
 * each relevant store event and rebuilds them once, at the next engine tick; the owning client
 * does the same for its owner view from its own store copy. Equip and unequip requests executed
 * by this component rebuild before returning instead. FlushDeferredUpdates runs any pending
 * rebuild at once.
 */
UCLASS(ClassGroup=(PEP), BlueprintType, meta=(BlueprintSpawnableComponent))
class PEPEQUIPMENT_API UPEPEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

	/* The Fast Array receive callbacks are the only outside callers of the Handle*Replicated pair. */
	friend struct FPEPPublicEquipmentView;
	friend struct FPEPTemporaryPresentationView;

public:
	UPEPEquipmentComponent();

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Accepts any native UObject implementing IPEPItemStore. Passing null clears the store. */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|Setup")
	bool SetItemStore(UObject* InItemStoreObject);

	UFUNCTION(BlueprintPure, Category="PEP|Equipment|Setup")
	UObject* GetItemStoreObject() const
	{
		return ItemStoreObject;
	}

	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|Setup")
	void SetPresentationMesh(USkeletalMeshComponent* InPresentationMesh);

	/**
	 * C++ setup helper for the non-replicated profile.
	 * Designers should assign EquipmentProfile in class defaults. Changes are rejected after BeginPlay.
	 */
	void SetEquipmentProfile(UPEPEquipmentProfile* InProfile);

	UFUNCTION(BlueprintPure, Category="PEP|Equipment|Setup")
	UPEPEquipmentProfile* GetEquipmentProfile() const
	{
		return EquipmentProfile;
	}

	/** Registers a local pawn component that a profile may address by tag. */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|Presentation")
	bool RegisterPresentationTarget(FGameplayTag TargetTag, USceneComponent* Component);

	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|Presentation")
	void UnregisterPresentationTarget(FGameplayTag TargetTag);

	/** Updates the unequipped appearance without disturbing an active equipment override. */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|Presentation")
	bool SetModularPresentationBaseMesh(FGameplayTag TargetTag, USkeletalMesh* BaseMesh);

	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|Presentation")
	void SetLocalPresentationEnabled(bool bEnabled);

	/**
	 * Shows an item's presentation as a temporary cosmetic prop without moving the item.
	 * Only DefinitionId is replicated; private item state remains owner/server-only.
	 *
	 * The item must be in this component's store and reachable for one of
	 * TemporaryPresentationAccessTags; at most MaxTemporaryPresentations keys are live at once, and
	 * a new key beyond that is refused (replacing a live key is always allowed). Returns false when
	 * refused. The owning client checks the same rules before showing its immediate local prop, so
	 * it only diverges from the server on a race.
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|Presentation|Temporary")
	bool RequestShowTemporaryItemPresentation(
		FPEPItemId ItemId,
		FName PresentationKey,
		FName AttachSocket,
		bool bHideOriginalPresentation = false);

	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|Presentation|Temporary")
	void RequestHideTemporaryItemPresentation(FName PresentationKey);

	/** All public visual nodes: equipped roots and their visible attachments. */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment")
	TArray<FPEPEquippedItemInfo> GetPublicPresentationNodes() const;

	/** Only items that directly occupy public Equipment slots. */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment")
	TArray<FPEPEquippedItemInfo> GetPublicEquipmentRoots() const;

	UFUNCTION(BlueprintPure, Category="PEP|Equipment")
	TArray<FPEPEquippedItemInfo> GetPublicAttachments(
		FPEPItemId ParentItemId,
		bool bRecursive = false) const;

	/** Any public node, root or attachment. Replaces the former FindPublicEquippedItem alias. */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment")
	bool FindPublicPresentationNode(
		FPEPItemId ItemId,
		FPEPEquippedItemInfo& OutEntry) const;

	/**
	 * The public root occupying a slot whose profile entry declares Hand. Read from the projection,
	 * so it answers alike on every machine.
	 *
	 * Deterministic: slots are taken in profile order, so even a profile that (invalidly) gives one
	 * hand to two slots yields the same item everywhere. Formerly GetActiveHandItem.
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment")
	bool FindItemInHand(EPEPEquipmentHand Hand, FPEPEquippedItemInfo& OutEntry) const;

	/** The hand the effective profile assigns to SlotTag; None for an unknown or handless slot. */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment")
	EPEPEquipmentHand GetSlotHand(FGameplayTag SlotTag) const;

	/** The slots of the effective profile (or of the fallback definitions), in profile order. */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment|Slots")
	TArray<FGameplayTag> GetEquipmentSlotTags() const;

	/** SlotTag's entry in the effective profile. False, with OutSlot reset, for an unknown slot. */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment|Slots")
	bool FindEquipmentSlotDefinition(
		FGameplayTag SlotTag,
		FPEPEquipmentSlotDefinition& OutSlot) const;

	/**
	 * Whether this machine keeps the owner view: the authority, and the owning client (autonomous
	 * proxy, or a pawn whose net owner is local). A simulated proxy does not, and reads only the
	 * public projection.
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment|Owner")
	bool HasOwnerEquipmentView() const;

	/**
	 * Every equipped node this machine's owner view holds: the occupant of each profile slot,
	 * hidden slots included, then its visible attachments (by the store's GetPublicChildren, as for
	 * the projection). Roots in profile order, each followed by its attachments. Local and never
	 * replicated; empty where HasOwnerEquipmentView is false.
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment|Owner")
	TArray<FPEPEquippedItemInfo> GetOwnerEquipmentNodes() const;

	/** Any node of the owner view, root or attachment, hidden slot or not. */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment|Owner")
	bool FindOwnerEquipmentNode(
		FPEPItemId ItemId,
		FPEPEquippedItemInfo& OutNode) const;

	/**
	 * The root occupying SlotTag, read from the most complete view this machine has: the owner view
	 * where it is kept (so a hidden slot answers for its owner), the public projection elsewhere.
	 * What an equipment UI shows.
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment")
	bool FindEquippedRootInSlot(
		FGameplayTag SlotTag,
		FPEPEquippedItemInfo& OutNode) const;

	/**
	 * Asks for an item to be equipped in SlotTag.
	 *
	 * Authority: executed at once, the return value is the result (Executed or Failed), and the
	 * projection is rebuilt before returning. OnEquipmentRequestResolved follows at the next tick,
	 * so a caller — Blueprint included — already holds OutRequestId when it arrives. Owning client:
	 * Submitted, resolved by the server through OnEquipmentRequestResolved.
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment")
	EPEPRequestResult RequestEquipItem(
		FPEPItemId ItemId,
		FGameplayTag SlotTag,
		int32& OutRequestId);

	/** Local validation only. It never mutates or predicts replicated equipment state. */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|Preview")
	FPEPItemResult PreviewEquipItem(FPEPItemId ItemId, FGameplayTag SlotTag) const;

	/**
	 * Forgets the cached list of eligibility providers; the next eligibility check gathers it again.
	 *
	 * The cache follows the owner by itself: it is rebuilt when the owner changes, when the owner's
	 * component count changes, or when a cached provider is gone. Call this after a change those
	 * checks cannot see — one component swapped for another in the same frame, leaving the count
	 * as it was and no cached provider destroyed.
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|Eligibility")
	void InvalidateEligibilityProviders();

	/**
	 * Asks for an item occupying one of this component's slots to be moved to TargetLocation.
	 *
	 * TargetLocation is required: IPEPItemStore offers no automatic placement, so an unset
	 * location fails with Reason.UnsupportedMode instead of guessing, and a malformed one with
	 * Reason.InvalidTarget. A target that is itself one of this component's slots fails with
	 * Reason.InvalidTarget: that is an equip. The item must directly occupy one of this
	 * component's slots (Reason.StateMismatch otherwise).
	 *
	 * Authority: executed at once and resolved through OnEquipmentRequestResolved at the next
	 * tick, sharing the equip request ids. Owning client: a target it can already tell is wrong
	 * returns Failed with no request; anything else is Submitted and resolved by the server.
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment")
	EPEPRequestResult RequestUnequipItem(
		FPEPItemId ItemId,
		FPEPItemLocation TargetLocation,
		int32& OutRequestId);

	/** Authority executor behind RequestUnequipItem. Raises no request resolution. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Equipment")
	FPEPEquipmentRequestResult UnequipItem(FPEPItemId ItemId, FPEPItemLocation TargetLocation);

	/** Local validation only, symmetric with PreviewEquipItem. */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|Preview")
	FPEPItemResult PreviewUnequipItem(FPEPItemId ItemId, FPEPItemLocation TargetLocation) const;

	/**
	 * Runs now what this component otherwise defers to the next engine tick: a projection and owner
	 * view rebuild pending from store changes (authority), an owner view rebuild (owning client), a
	 * presentation reconcile waiting on loaded assets, and request resolutions raised on the
	 * authority. For server code that must read the projection in the frame it wrote the store,
	 * and for tests. Safe to call at any time.
	 */
	void FlushDeferredUpdates();

	UPROPERTY(BlueprintAssignable, Category="PEP|Equipment")
	FOnPEPPublicEquipmentChanged OnPublicEquipmentChanged;

	/**
	 * A public node entered the projection, on every machine that sees it — the authority when it
	 * rebuilds, each client when the replicated view lands. Attachments included. Within one
	 * change every Unequipped is raised before any Equipped, so an item moving between slots reads
	 * Unequipped(old slot) then Equipped(new slot). Raised before OnPublicEquipmentChanged.
	 *
	 * Raised as the projection changes, which may be before a listener's BeginPlay on a freshly
	 * replicated pawn: read GetPublicPresentationNodes() when binding to catch up.
	 *
	 * Only public slots take part: an item in a slot with bShowPublicly off raises nothing here.
	 * Its owner sees it through OnOwnerItemEquipped.
	 */
	UPROPERTY(BlueprintAssignable, Category="PEP|Equipment|Event")
	FOnPEPEquipmentItemChanged OnItemEquipped;

	/** A public node left the projection, or changed slot or parent. See OnItemEquipped. */
	UPROPERTY(BlueprintAssignable, Category="PEP|Equipment|Event")
	FOnPEPEquipmentItemChanged OnItemUnequipped;

	/**
	 * A node entered the owner view: any slot of the profile, hidden ones included, plus visible
	 * attachments. Raised only where the owner view is kept — the authority and the owning client —
	 * with the same payload and the same deterministic order as OnItemEquipped (every Unequipped
	 * first, roots before attachments, ties by ItemId). Use it for what the wearer has rather than
	 * what others see: abilities, effects, the owner's own equipment screen.
	 *
	 * The authority rebuilds both views in one pass and raises the public events first, then these.
	 * The owning client rebuilds from its own store copy, at the next tick after that copy changed.
	 * Read GetOwnerEquipmentNodes() when binding to catch up.
	 */
	UPROPERTY(BlueprintAssignable, Category="PEP|Equipment|Owner")
	FOnPEPEquipmentItemChanged OnOwnerItemEquipped;

	/** A node left the owner view, or changed slot or parent. See OnOwnerItemEquipped. */
	UPROPERTY(BlueprintAssignable, Category="PEP|Equipment|Owner")
	FOnPEPEquipmentItemChanged OnOwnerItemUnequipped;

	/** The owner view changed; raised after OnOwnerItemUnequipped / OnOwnerItemEquipped. */
	UPROPERTY(BlueprintAssignable, Category="PEP|Equipment|Owner")
	FOnPEPOwnerEquipmentChanged OnOwnerEquipmentChanged;

	/**
	 * Resolves a request on the machine that made it. A request issued on the authority resolves
	 * there, at the next tick; one submitted by the owning client resolves on that client through
	 * a client RPC. Request ids are per machine, so the two never mix.
	 */
	UPROPERTY(BlueprintAssignable, Category="PEP|Equipment|Request")
	FOnPEPEquipmentRequestResolved OnEquipmentRequestResolved;

	/** C++ extension point for capability plugins layered on the local mesh presentation. */
	FOnPEPLocalEquipmentPresentationChanged& OnLocalPresentationChanged()
	{
		return LocalPresentationChanged;
	}

	/**
	 * Generic local target for attached actors and modular mesh overrides. An attached visual's
	 * actor is OutTarget.PresentationActor (formerly FindLocalPresentationActor).
	 */
	bool FindLocalPresentationTarget(
		FPEPItemId ItemId,
		FPEPLocalPresentationTarget& OutTarget) const;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_RequestEquipItem(FPEPItemId ItemId, FGameplayTag SlotTag, int32 RequestId);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_RequestUnequipItem(
		FPEPItemId ItemId,
		FPEPItemLocation TargetLocation,
		int32 RequestId);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_ShowTemporaryItemPresentation(
		FPEPItemId ItemId,
		FName PresentationKey,
		FName AttachSocket,
		bool bHideOriginalPresentation);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_HideTemporaryItemPresentation(FName PresentationKey);

	/**
	 * Resolves equip and unequip requests alike: they share one request id sequence. Sent only for
	 * requests that came from that client, never for one issued on the server.
	 */
	UFUNCTION(Client, Reliable)
	void Client_ResolveEquipRequest(FPEPEquipmentRequestResult RequestResult);

	/**
	 * Subclass hook, asked after the slot's item tags and before any
	 * IPEPEquipmentEligibilityProvider on the owner. Same contract as a provider: a pure query,
	 * Definition null when not resident. Allows by default.
	 */
	UFUNCTION(BlueprintNativeEvent, Category="PEP|Equipment|Eligibility")
	FPEPItemResult CanEquipItemInSlot(
		const FPEPItemInstance& Item,
		const UPEPItemDefinition* Definition,
		FGameplayTag SlotTag) const;
	virtual FPEPItemResult CanEquipItemInSlot_Implementation(
		const FPEPItemInstance& Item,
		const UPEPItemDefinition* Definition,
		FGameplayTag SlotTag) const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Equipment|Slots")
	TObjectPtr<UPEPEquipmentProfile> EquipmentProfile;

	/** Native/default fallback used when no EquipmentProfile is assigned. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Equipment|Slots",
		meta=(DisplayName="Fallback Slot Definitions"))
	TArray<FPEPEquipmentSlotDefinition> SlotDefinitions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	bool bEnableLocalPresentation = true;

	/**
	 * How many temporary presentation keys may be live at once. A request for a new key beyond it
	 * is refused; replacing a live key is not. Bounds the replicated temporary view, which any
	 * owning client can otherwise grow one reliable RPC at a time. 0 disables temporary props.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Equipment|Presentation|Temporary",
		meta=(ClampMin="0", UIMin="0"))
	int32 MaxTemporaryPresentations = 4;

	/**
	 * A temporary presentation may show an item only if the store grants it at least one of these
	 * gameplay accesses (IPEPItemStore::HasGrantedGameplayAccess), so that what sits deep in a
	 * backpack is not flashed in hand.
	 *
	 * Empty by default: any item of the owner's store qualifies, still subject to the key cap and
	 * the socket check. Equipment defines no access vocabulary of its own; the picker only offers
	 * the Inventory.GameplayAccess branch PEPInventory declares (Inventory.GameplayAccess.Equip,
	 * .Use, and a project's own children). A store with another vocabulary takes any tag it
	 * answers to.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Equipment|Presentation|Temporary",
		meta=(Categories="Inventory.GameplayAccess"))
	FGameplayTagContainer TemporaryPresentationAccessTags;

	/**
	 * RequestEquipItem and PreviewEquipItem accept an item only if the store grants it at least one
	 * of these gameplay accesses (IPEPItemStore::HasGrantedGameplayAccess) — an item buried in a
	 * container that grants no Equip access cannot be equipped from there.
	 *
	 * Empty by default: no access requirement; the slot's eligibility and the store's own move and
	 * swap rules still apply. A PEPInventory project typically sets Inventory.GameplayAccess.Equip
	 * here. Like the temporary presentation tags, only a filter string ties this to
	 * Inventory.GameplayAccess.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Equipment|Slots",
		meta=(Categories="Inventory.GameplayAccess"))
	FGameplayTagContainer EquipAccessTags;

	UPROPERTY(
		VisibleInstanceOnly,
		BlueprintReadOnly,
		Transient,
		Category="PEP|Equipment",
		meta=(MustImplement="/Script/PEPItemCore.PEPItemStore"))
	TObjectPtr<UObject> ItemStoreObject;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="PEP|Equipment")
	TObjectPtr<USkeletalMeshComponent> PresentationMesh;

	/** Replicated publicly to every connection for which the owning pawn is relevant. */
	UPROPERTY(Replicated)
	FPEPPublicEquipmentView PublicEquipmentView;

private:
	/** Who a request came from: decides the identity it is made with and where it resolves. */
	enum class ERequestOrigin : uint8
	{
		/** Called on this machine: server code, an AI, a listen-server host, or a local preview. */
		Local,
		/** Arrived through a server RPC from the owning client. */
		RemoteClient
	};

	/** Fast Array callback targets, reached through the view structs (friends). */
	void HandlePublicEquipmentReplicated();
	void HandleTemporaryPresentationsReplicated();

	/**
	 * Possession can turn a simulated proxy into the owning client's pawn, or back: the owner view
	 * is rebuilt (or emptied) at the next tick, once the new role has landed.
	 */
	UFUNCTION()
	void HandleOwnerControllerChanged(
		APawn* Pawn,
		AController* OldController,
		AController* NewController);

	void InitializeItemStoreIntegration();
	void UninitializeItemStoreIntegration();
	UObject* FindItemStoreObjectOnOwner() const;
	IPEPItemStore* GetItemStore() const;
	const IPEPItemStore* GetItemStoreConst() const;
	const TArray<FPEPEquipmentSlotDefinition>& GetEffectiveSlotDefinitions() const;
	void HandleItemStoreChanged(const FPEPItemStoreChange& Change);
	FPEPItemResult ValidateEquipmentLocation(
		const FPEPItemInstance& Item,
		const FPEPItemLocation& Location) const;
	/** Slot item tags, then CanEquipItemInSlot, then every provider on the owner. First failure wins. */
	FPEPItemResult CheckEquipEligibility(
		const FPEPItemInstance& Item,
		FGameplayTag SlotTag) const;
	/**
	 * The owner actor first, then its implementing components ordered by name: stable on one
	 * machine. Components are filtered before any sorting, and one implementer is never sorted.
	 *
	 * Served from a per-component cache, rebuilt only when the owner changed, the owner's component
	 * count moved, a cached provider is gone, or InvalidateEligibilityProviders was called. This
	 * runs inside every store write into a slot and in every preview.
	 */
	void GatherEligibilityProviders(TArray<const UObject*>& OutProviders) const;
	bool IsEligibilityProviderCacheCurrent(const AActor& Owner) const;
	/** Whether the store grants ItemId at least one of EquipAccessTags; true when they are empty. */
	bool HasEquipAccess(const IPEPItemStore& ItemStore, FPEPItemId ItemId) const;
	FPEPItemResult ExecuteEquipItem(FPEPItemId ItemId, FGameplayTag SlotTag, ERequestOrigin Origin);
	/** Everything an unequip can be refused for without asking the store to move anything. */
	FPEPItemResult ValidateUnequipRequest(
		FPEPItemId ItemId,
		const FPEPItemLocation& TargetLocation) const;
	FPEPItemResult ExecuteUnequipItem(
		FPEPItemId ItemId,
		const FPEPItemLocation& TargetLocation,
		ERequestOrigin Origin);
	const FPEPEquipmentSlotDefinition* FindSlotDefinition(FGameplayTag SlotTag) const;

	/**
	 * Rebuilds the projection and the owner view from the store now, in one walk. Authority only.
	 * True when the public projection changed; the owner view announces its own changes.
	 */
	bool RefreshPublicProjection();
	/**
	 * Rebuilds the owner view alone: the owning client's path, from its store copy. Empties it
	 * where this machine keeps none. True when it changed.
	 */
	bool RefreshOwnerView();
	/**
	 * Notes that the views may be stale and folds the rebuild into the next tick, or batch: both on
	 * the authority, the owner view elsewhere.
	 */
	void MarkEquipmentViewsDirty();
	/** Around this component's own store writes: their events rebuild once, before returning. */
	void BeginProjectionBatch();
	void EndProjectionBatch();
	/** Runs whichever rebuild is pending: both views on the authority, the owner view elsewhere. */
	void RefreshDirtyEquipmentViews();
	/**
	 * Diffs the projection against what this machine last announced and raises OnItemUnequipped
	 * then OnItemEquipped. The one place both events come from, on authority and clients alike.
	 */
	void BroadcastEquipmentItemChanges();
	/** Same contract for the owner view: OnOwnerItemUnequipped then OnOwnerItemEquipped. */
	void BroadcastOwnerItemChanges();
	/** Replaces the owner view when Nodes differs from it as a set. True when it changed. */
	bool ApplyOwnerViewNodes(TArray<FPEPEquippedItemInfo>&& Nodes);
	/**
	 * Every node under this component's slots: each slot's occupant in profile order, followed by
	 * its visible attachments depth first. OutOwnerNodes gets all of them; OutPublicNodes, when
	 * given, only those under a slot shown publicly. Both are appended to, not reset.
	 */
	void CollectEquippedNodes(
		const IPEPItemStore& ItemStore,
		TArray<FPEPEquippedItemInfo>& OutOwnerNodes,
		TArray<FPEPEquippedItemInfo>* OutPublicNodes) const;
	void AppendAttachmentNodes(
		const IPEPItemStore& ItemStore,
		FPEPItemId ParentItemId,
		TArray<FPEPEquippedItemInfo>& OutNodes,
		TSet<FPEPItemId>& VisitedItemIds) const;
	const FPEPEquippedItemInfo* FindOwnerViewNode(FPEPItemId ItemId) const;
	/** Indexed by ItemId; falls back to a scan if the index is stale, so it is never wrong. */
	const FPEPEquippedItemPublic* FindPublicEntry(FPEPItemId ItemId) const;
	FPEPEquippedItemPublic* FindMutablePublicEntry(FPEPItemId ItemId);
	const FPEPEquippedItemPublic* FindPublicRootInSlot(FGameplayTag SlotTag) const;
	void RebuildPublicEntryIndex();
	const AController* GetOwningController() const;
	/**
	 * The identity a store request is made with. The owner's controller whenever there is one.
	 * Without one, a local call on the authority is a server-driven change and uses System(); a
	 * client-requested change never does, and is refused instead (false).
	 */
	bool ResolveRequestContext(ERequestOrigin Origin, FPEPItemAccessContext& OutContext) const;
	int32 AllocateRequestId();
	void ResolveEquipRequest(int32 RequestId, const FPEPItemResult& Result, ERequestOrigin Origin);

	/** Schedules FlushDeferredUpdates on the core ticker, once, after BeginPlay. */
	void ScheduleDeferredUpdates();
	bool TickDeferredUpdates(float DeltaTime);
	void CancelDeferredUpdates();

	/** Brings local visuals in line with the projection by difference. Never before BeginPlay. */
	void ReconcileLocalPresentations();
	/** For asset-load completions: one reconcile at the next tick however many land in a frame. */
	void RequestLocalPresentationReconcile();
	/** Throws every local visual away and builds again: a target or the mesh moved under them. */
	void RebuildLocalPresentations();
	bool EnsureLocalPresentation(
		const FPEPEquippedItemPublic& Entry,
		TSet<FPEPItemId>& ResolvingItems);
	bool EnsureAttachedPresentation(
		const FPEPEquippedItemPublic& Entry,
		const struct FPEPEquipmentPresentationFragment& Presentation,
		USceneComponent* AttachParent,
		FName AttachSocket);
	bool ApplyModularPresentation(
		const FPEPEquippedItemPublic& Entry,
		const FPEPEquipmentSlotDefinition& Slot,
		const struct FPEPEquipmentPresentationFragment& Presentation);

	/**
	 * Creates a skeletal mesh component over the body for this item, sharing the body's skeleton.
	 *
	 * A layer, not a replacement: the component is this item's own and goes when the item does,
	 * leaving whatever it covered untouched. Which is the whole difference from the modular path,
	 * where equipping a vest would have taken the shirt with it.
	 */
	bool EnsureLayeredPresentation(
		const FPEPEquippedItemPublic& Entry,
		const FPEPEquipmentSlotDefinition& Slot,
		const struct FPEPEquipmentPresentationFragment& Presentation,
		bool bApplyPresentationTransform = false);

	/** The mesh a layer follows: a named body part when the slot asks for one, else the body. */
	USkeletalMeshComponent* ResolveLayerLeader(const FPEPEquipmentSlotDefinition& Slot) const;
	/**
	 * Registers without rebuilding. bOutChanged reports whether anything moved, so callers that
	 * change several things rebuild once.
	 */
	bool RegisterPresentationTargetInternal(
		FGameplayTag TargetTag,
		USceneComponent* Component,
		bool& bOutChanged);
	void RequestDefinitionLoad(FPrimaryAssetId DefinitionId);
	void RequestPresentationAssetLoad(const FSoftObjectPath& AssetPath);
	/** Removes one item's local visual, whatever policy built it. */
	void DestroyLocalPresentation(FPEPItemId ItemId);
	void DestroyAllLocalPresentations();
	/** Whether a store change can alter the projection: it touched one of this component's slots. */
	bool DoesChangeTouchEquipment(const FPEPItemStoreChange& Change) const;

	/**
	 * Everything a temporary presentation request is refused for, shared by the authority, the
	 * owning client's local check and the server RPC. Cheapest checks first.
	 */
	bool ValidateTemporaryPresentationRequest(
		FPEPItemId ItemId,
		FName PresentationKey,
		FName AttachSocket,
		bool bHideOriginalPresentation,
		FPrimaryAssetId& OutDefinitionId,
		FPEPItemId& OutSuppressedItemId) const;
	/** A live key may always be replaced; a new one needs room under MaxTemporaryPresentations. */
	bool HasTemporaryPresentationRoom(FName PresentationKey) const;
	bool HasTemporaryPresentationAccess(const IPEPItemStore& ItemStore, FPEPItemId ItemId) const;
	const FPEPTemporaryPresentationEntry* FindTemporaryPresentationEntry(FName PresentationKey) const;
	bool ShowTemporaryPresentationLocal(
		FName PresentationKey,
		FPrimaryAssetId DefinitionId,
		FName AttachSocket,
		FPEPItemId SuppressedItemId);
	void HideTemporaryPresentationLocal(
		FName PresentationKey,
		bool bRestoreSuppressedPresentation = true);
	void DestroyAllTemporaryPresentations();
	/** Re-shows every applied temporary prop, e.g. on a new mesh. */
	void RebuildTemporaryPresentations();
	/**
	 * Loads what a temporary prop is missing: its definition when AssetPath is unset, else that
	 * asset. Shares the failure sets with the regular presentation, so a missing asset is asked
	 * for once, and keeps the handle only while the load is pending.
	 */
	void RequestTemporaryPresentationLoad(
		FName PresentationKey,
		FPrimaryAssetId DefinitionId,
		const FSoftObjectPath& AssetPath);
	void HandleTemporaryPresentationLoaded(
		FName PresentationKey,
		FPrimaryAssetId DefinitionId,
		FSoftObjectPath AssetPath);
	/** SuppressedPresentationItems follows TemporarySuppressedItems: one key's hide never ends another's. */
	void RefreshSuppressedPresentationItems();
	/* Authority: write the replicated temporary state and apply it locally. */
	void SetTemporaryPresentationState(
		FName PresentationKey,
		FPrimaryAssetId DefinitionId,
		FName AttachSocket,
		FPEPItemId SuppressedItemId);
	void ClearTemporaryPresentationState(FName PresentationKey);
	/** Brings the local props in line with TemporaryPresentationView, touching only what changed. */
	void ApplyTemporaryPresentations();

	/** Replicated to everyone the pawn is relevant to, so a late joiner builds the props too. */
	UPROPERTY(Replicated)
	FPEPTemporaryPresentationView TemporaryPresentationView;
	/** What each local temporary prop was built from. */
	TMap<FName, FPEPTemporaryPresentationEntry> AppliedTemporaryPresentations;

	FDelegateHandle ItemStoreChangedHandle;
	int32 NextRequestId = 1;
	TMap<FPrimaryAssetId, TSharedPtr<FStreamableHandle>> DefinitionLoadHandles;
	TMap<FSoftObjectPath, TSharedPtr<FStreamableHandle>> PresentationAssetLoadHandles;
	TMap<FName, TSharedPtr<FStreamableHandle>> TemporaryPresentationLoadHandles;
	/** Shared by the regular and the temporary presentation: what failed once is not asked again. */
	TSet<FPrimaryAssetId> FailedDefinitionLoads;
	TSet<FSoftObjectPath> FailedPresentationAssetLoads;
	FOnPEPLocalEquipmentPresentationChanged LocalPresentationChanged;
	int32 LocalPresentationGeneration = 0;
	bool bEquipmentProfileLocked = false;

	/**
	 * Whether a running instance chose its own store, as opposed to inheriting a pointer.
	 *
	 * Deliberately not a UPROPERTY, so nothing can copy it from an archetype. ItemStoreObject can
	 * arrive already filled on an instance that never chose it — naming another actor's store, at
	 * which point discovery was skipped because the pointer was merely non-null, and this component
	 * claimed its equipment slots on an inventory nobody would ever ask. Only a runtime SetItemStore
	 * proves the pointer belongs to this instance; anything else is re-resolved against its owner.
	 */
	bool bItemStoreExplicitlySet = false;

	UPROPERTY(Transient)
	TMap<FPEPItemId, TObjectPtr<APEPEquippedPresentationActor>> PresentationActors;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<APEPEquippedPresentationActor>> TemporaryPresentationActors;

	TMap<FName, FPEPItemId> TemporarySuppressedItems;
	TSet<FPEPItemId> SuppressedPresentationItems;

	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<USceneComponent>> RegisteredPresentationTargets;

	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<USkeletalMesh>> ModularBaseMeshes;

	/** One component per layered item, so removing one leaves the others where they are. */
	UPROPERTY(Transient)
	TMap<FPEPItemId, TObjectPtr<USkeletalMeshComponent>> LayeredMeshComponents;

	TMap<FGameplayTag, FPEPItemId> AppliedModularItems;
	TMap<FPEPItemId, FPEPLocalPresentationTarget> LocalPresentationTargets;
	/** The public node each local presentation was built from: what a reconcile diffs against. */
	TMap<FPEPItemId, FPEPEquippedItemInfo> PresentedNodes;

	/** ItemId -> index in PublicEquipmentView.Entries. Rebuilt whenever the array changes. */
	TMap<FPEPItemId, int32> PublicEntryIndex;

	/** The projection as of the last OnItemEquipped/OnItemUnequipped this machine raised. */
	TArray<FPEPEquippedItemInfo> AnnouncedEquipmentNodes;
	bool bBroadcastingEquipmentItemChanges = false;
	bool bEquipmentItemChangesPending = false;

	/**
	 * The owner view: authority and owning client only, never replicated. Roots in profile order,
	 * each followed by its attachments.
	 */
	TArray<FPEPEquippedItemInfo> OwnerViewNodes;
	/** ItemId -> index in OwnerViewNodes. Rebuilt whenever the array changes. */
	TMap<FPEPItemId, int32> OwnerViewIndex;
	/** The owner view as of the last OnOwnerItemEquipped/OnOwnerItemUnequipped this machine raised. */
	TArray<FPEPEquippedItemInfo> AnnouncedOwnerNodes;
	bool bBroadcastingOwnerItemChanges = false;
	bool bOwnerItemChangesPending = false;

	/**
	 * Authority: a store change may have moved the projection, and so the owner view, since they
	 * were last built.
	 */
	bool bPublicProjectionDirty = false;
	/** The owner view alone may be stale: the owning client's store copy changed, or its role. */
	bool bOwnerViewDirty = false;

	/** GatherEligibilityProviders' cache, and what it was built against. */
	mutable TArray<TWeakObjectPtr<const UObject>> CachedEligibilityProviders;
	mutable TWeakObjectPtr<const AActor> CachedEligibilityProviderOwner;
	mutable int32 CachedEligibilityComponentCount = INDEX_NONE;
	int32 ProjectionBatchDepth = 0;
	bool bLocalPresentationReconcilePending = false;
	/** Resolutions of requests issued on this machine, raised at the next tick. */
	TArray<FPEPEquipmentRequestResult> PendingLocalResolutions;
	FTSTicker::FDelegateHandle DeferredUpdatesHandle;
	bool bDeferredUpdatesScheduled = false;
};
