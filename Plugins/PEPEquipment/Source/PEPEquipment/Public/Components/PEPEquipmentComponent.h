#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
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
 */
UCLASS(ClassGroup=(PEP), BlueprintType, meta=(BlueprintSpawnableComponent))
class PEPEQUIPMENT_API UPEPEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

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
	 * Only DefinitionId is multicasted; private item state remains owner/server-only.
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
	TArray<FPEPEquippedItemPublic> GetPublicPresentationNodes() const;

	/** Only items that directly occupy public Equipment slots. */
	UFUNCTION(BlueprintPure, Category="PEP|Equipment")
	TArray<FPEPEquippedItemPublic> GetPublicEquipmentRoots() const;

	UFUNCTION(BlueprintPure, Category="PEP|Equipment")
	TArray<FPEPEquippedItemPublic> GetPublicAttachments(
		FPEPItemId ParentItemId,
		bool bRecursive = false) const;

	UFUNCTION(BlueprintPure, Category="PEP|Equipment")
	bool FindPublicPresentationNode(
		FPEPItemId ItemId,
		FPEPEquippedItemPublic& OutEntry) const;

	UFUNCTION(BlueprintPure, Category="PEP|Equipment")
	bool FindPublicEquippedItem(FPEPItemId ItemId, FPEPEquippedItemPublic& OutEntry) const;

	UFUNCTION(BlueprintPure, Category="PEP|Equipment")
	bool GetActiveHandItem(EPEPEquipmentHand Hand, FPEPEquippedItemPublic& OutEntry) const;

	UFUNCTION(BlueprintCallable, Category="PEP|Equipment")
	EPEPRequestResult RequestEquipItem(
		FPEPItemId ItemId,
		FGameplayTag SlotTag,
		int32& OutRequestId);

	/** Local validation only. It never mutates or predicts replicated equipment state. */
	UFUNCTION(BlueprintCallable, Category="PEP|Equipment|Preview")
	FPEPItemResult PreviewEquipItem(FPEPItemId ItemId, FGameplayTag SlotTag) const;

	/**
	 * Asks for an item occupying one of this component's slots to be moved to TargetLocation.
	 *
	 * TargetLocation is required: IPEPItemStore offers no automatic placement, so an unset
	 * location fails with Reason.UnsupportedMode instead of guessing, and a malformed one with
	 * Reason.InvalidTarget. A target that is itself one of this component's slots fails with
	 * Reason.InvalidTarget: that is an equip. The item must directly occupy one of this
	 * component's slots (Reason.StateMismatch otherwise).
	 *
	 * Authority: executed at once and resolved through OnEquipmentRequestResolved, sharing the
	 * equip request ids. Owning client: a target it can already tell is wrong returns Failed with
	 * no request; anything else is Submitted and resolved by the server.
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
	 */
	UPROPERTY(BlueprintAssignable, Category="PEP|Equipment|Event")
	FOnPEPEquipmentItemChanged OnItemEquipped;

	/** A public node left the projection, or changed slot or parent. See OnItemEquipped. */
	UPROPERTY(BlueprintAssignable, Category="PEP|Equipment|Event")
	FOnPEPEquipmentItemChanged OnItemUnequipped;

	UPROPERTY(BlueprintAssignable, Category="PEP|Equipment|Request")
	FOnPEPEquipmentRequestResolved OnEquipmentRequestResolved;

	/** Fast Array callback targets. */
	void HandlePublicEquipmentReplicated();
	void HandleTemporaryPresentationsReplicated();

	/** C++ extension point for capability plugins layered on the local mesh presentation. */
	FOnPEPLocalEquipmentPresentationChanged& OnLocalPresentationChanged()
	{
		return LocalPresentationChanged;
	}

	APEPEquippedPresentationActor* FindLocalPresentationActor(FPEPItemId ItemId) const;

	/** Generic local target for attached actors and modular mesh overrides. */
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

	/** Resolves equip and unequip requests alike: they share one request id sequence. */
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
	/** The owner actor first, then its components ordered by name: stable on one machine. */
	void GatherEligibilityProviders(TArray<const UObject*>& OutProviders) const;
	FPEPItemResult ExecuteEquipItem(FPEPItemId ItemId, FGameplayTag SlotTag);
	/** Everything an unequip can be refused for without asking the store to move anything. */
	FPEPItemResult ValidateUnequipRequest(
		FPEPItemId ItemId,
		const FPEPItemLocation& TargetLocation) const;
	FPEPItemResult ExecuteUnequipItem(FPEPItemId ItemId, const FPEPItemLocation& TargetLocation);
	const FPEPEquipmentSlotDefinition* FindSlotDefinition(FGameplayTag SlotTag) const;

	void RefreshPublicProjection();
	/**
	 * Diffs the projection against what this machine last announced and raises OnItemUnequipped
	 * then OnItemEquipped. The one place both events come from, on authority and clients alike.
	 */
	void BroadcastEquipmentItemChanges();
	void AppendPublicAttachmentEntries(
		const IPEPItemStore& ItemStore,
		FPEPItemId ParentItemId,
		TMap<FPEPItemId, FPEPEquippedItemPublic>& OutEntries,
		TSet<FPEPItemId>& VisitedItemIds) const;
	FPEPEquippedItemPublic* FindMutablePublicEntry(FPEPItemId ItemId);
	const AController* GetOwningController() const;
	FPEPItemAccessContext GetOwnerRequestContext() const;
	int32 AllocateRequestId();
	void ResolveEquipRequest(int32 RequestId, const FPEPItemResult& Result);

	void ReconcileLocalPresentations();
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
	void RequestDefinitionLoad(FPrimaryAssetId DefinitionId);
	void RequestPresentationAssetLoad(const FSoftObjectPath& AssetPath);
	/** Removes one item's local visual, whatever policy built it. */
	void DestroyLocalPresentation(FPEPItemId ItemId);
	void DestroyAllLocalPresentations();
	/** Whether a store change can alter the projection: it touched one of this component's slots. */
	bool DoesChangeTouchEquipment(const FPEPItemStoreChange& Change) const;
	bool ShowTemporaryPresentationLocal(
		FName PresentationKey,
		FPrimaryAssetId DefinitionId,
		FName AttachSocket,
		FPEPItemId SuppressedItemId);
	void HideTemporaryPresentationLocal(
		FName PresentationKey,
		bool bRestoreSuppressedPresentation = true);
	void DestroyAllTemporaryPresentations();
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
	TMap<FPEPItemId, FPEPEquippedItemPublic> PresentedNodes;

	/** The projection as of the last OnItemEquipped/OnItemUnequipped this machine raised. */
	TArray<FPEPEquippedItemPublic> AnnouncedEquipmentNodes;
	bool bBroadcastingEquipmentItemChanges = false;
	bool bEquipmentItemChangesPending = false;
};
