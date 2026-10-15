#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Item/PEPItemWorldPresentationTypes.h"
#include "Item/PEPItemPresetTypes.h"
#include "Templates/Function.h"
#include "PEPItemWorldActor.generated.h"

class UStaticMeshComponent;
class UPrimitiveComponent;
class UMaterialInstanceDynamic;
class UPEPItemDefinition;
class UPEPItemPreset;
struct FStreamableHandle;

UCLASS(Abstract, BlueprintType, Blueprintable)
class PEPINVENTORY_API APEPItemWorldActor : public AActor
{
	GENERATED_BODY()

	/* A spawner hands the tree it has just validated to the actor it spawns, without a second pass. */
	friend class APEPItemWorldSpawner;

public:
	APEPItemWorldActor();

	/** Used by authoring validation before allocating or replacing runtime state. */
	virtual bool SupportsDefinition(const UPEPItemDefinition* Definition) const { return false; }

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	/** Replaces the authoring recipe with a preset and creates a fresh runtime tree on the authority. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Item")
	void SetSpawnPreset(UPEPItemPreset* InPreset);

	/** Replaces the authoring recipe with a direct item and creates a fresh runtime tree on the authority. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Item")
	void SetSpawnItemDefinition(UPEPItemDefinition* InDefinition);

	/**
	 * Sets the exact runtime item instance (preserves state, e.g. when dropping from an inventory).
	 *
	 * C++ only, like SetItemTree: an instance carries its identity, and handing one over is how an
	 * item that just left a store arrives here. Exposed to Blueprint, any graph able to read an
	 * instance could put a second copy of it on the ground. Blueprints choose a spawn source
	 * (SetSpawnPreset, SetSpawnItemDefinition), which always mints fresh ids.
	 */
	virtual void SetItemInstance(const FPEPItemInstance& InInstance);

	/**
	 * Sets a root and its private descendants, preserving a container or configured attachment tree.
	 * Validated in full before anything is stored. C++ only, for the reason given on SetItemInstance.
	 */
	virtual bool SetItemTree(const FPEPItemInstanceTree& InTree);

	/**
	 * Empties this actor and hands its tree to the caller, which from then on holds the only copy.
	 *
	 * Taking the tree out before it is placed elsewhere is what keeps an item from existing twice:
	 * the actor is already empty while the destination announces the arrival. A caller that fails
	 * to place the tree gives it back through SetItemTree. Authority only; returns false, leaving
	 * the actor untouched, when it holds nothing.
	 */
	bool TakeAuthorityItemTree(FPEPItemInstanceTree& OutTree);

	/**
	 * The item's definition. On the authority it is read straight from the private runtime
	 * instance; every other machine reads PublicItemDefinition instead, replicated for exactly
	 * this purpose. The definition asset itself carries no per-instance state (that stays in
	 * authority storage, never replicated) — only which definition this actor represents.
	 *
	 * Off the authority the asset is streamed in when PublicItemDefinition arrives, so this is null
	 * for the short while that takes, never a synchronous load.
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Item")
	const UPEPItemDefinition* GetItemDefinition() const;

	/** Authority-only gameplay data. Clients receive PublicPresentation instead. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Item|Authority")
	virtual FPEPItemInstance GetAuthorityItemInstance() const;

	/** Authority-only root and private descendants used by pickup/drop transactions. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Item|Authority")
	virtual FPEPItemInstanceTree GetAuthorityItemTree() const;

	/**
	 * The root instance in authority storage, read in place: null off the authority and when the
	 * actor holds nothing. Valid until the storage next changes. What GetAuthorityItemInstance
	 * copies, for callers that only need to look.
	 */
	virtual const FPEPItemInstance* FindAuthorityItemInstance() const { return nullptr; }

	/** Root plus visible Attachment descendants. Contains no runtime item state or item IDs. */
	UFUNCTION(BlueprintPure, Category="PEP|Item|World Presentation")
	TArray<FPEPWorldPresentationNode> GetPublicPresentationNodes() const
	{
		return PublicPresentation.Nodes;
	}

	/** Rebuilds the local visuals from the current presentation, whether or not it changed. */
	UFUNCTION(BlueprintCallable, Category="PEP|Item|Presentation")
	void ApplyDefinitionVisual();

protected:
	virtual void BeginPlay() override;

	/**
	 * Editor/spawn recipe only. Never replicated, never consulted by runtime clients.
	 * Set either SpawnPreset or SpawnItemDefinition, never both.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="PEP|Item",
		meta=(DisplayName="Spawn Preset"))
	TSoftObjectPtr<UPEPItemPreset> SpawnPreset;

	/**
	 * Optional direct item, for a spawn point that needs no preset, attachments, or state
	 * overrides. Set either SpawnPreset or SpawnItemDefinition, never both.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="PEP|Item",
		meta=(DisplayName="Spawn Item (Direct)"))
	TSoftObjectPtr<UPEPItemDefinition> SpawnItemDefinition;

	virtual void ResetAuthorityStorage() {}
	virtual bool StoreAuthorityTree(const FPEPItemInstanceTree& Tree) { return false; }

	/**
	 * Calls Visitor once per direct child of ParentId in authority storage, by reference. Nothing
	 * is copied, so the presentation walk can pass over Content children without paying for them.
	 * The references are only valid during the call.
	 */
	virtual void VisitAuthorityChildren(
		FPEPItemId ParentId,
		TFunctionRef<void(const FPEPItemInstance&, const FPEPItemLocation&)> Visitor) const {}

	/**
	 * SetItemTree for a tree whose ValidateDetailed has already passed: a preset build, or the tree
	 * a spawner validated before handing it on. Everything else still applies — authority, the
	 * storage's own acceptance, the presentation refresh.
	 */
	bool ApplyValidatedItemTree(const FPEPItemInstanceTree& InTree);

	/** Cosmetic-only network payload: root and Attachment descendants, never Content or state. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing=OnRep_PublicPresentation,
		Category="PEP|Item|World Presentation")
	FPEPWorldPresentationView PublicPresentation;

	/**
	 * Which definition this actor represents, replicated so any machine can query its static data
	 * (tags, fragments) through GetItemDefinition() without authority. The definition asset is
	 * shipped content, identical everywhere; this only says which one applies here. Per-instance
	 * state (durability, ammo, ...) still lives in authority storage and is never replicated.
	 *
	 * Soft on purpose. A hard reference would make the net driver load the asset synchronously the
	 * moment the actor arrives; instead OnRep streams it in and PublicItemDefinitionLoadHandle keeps
	 * it resident for as long as this actor names it.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing=OnRep_PublicItemDefinition,
		Category="PEP|Item|World Presentation")
	TSoftObjectPtr<UPEPItemDefinition> PublicItemDefinition;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item|Presentation")
	TObjectPtr<UStaticMeshComponent> RootStaticMeshComponent = nullptr;

	/**
	 * Put physics-driven world items to network dormancy when their root body settles.
	 * If gameplay moves a dormant item without waking its physics body, flush its dormancy first.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Networking")
	bool bUsePhysicsBasedNetworkDormancy = true;

	UFUNCTION()
	void OnRep_PublicPresentation();

	UFUNCTION()
	void OnRep_PublicItemDefinition();

	UFUNCTION()
	void HandleRootPhysicsSleep(UPrimitiveComponent* SleepingComponent, FName BoneName);

	UFUNCTION()
	void HandleRootPhysicsWake(UPrimitiveComponent* WakingComponent, FName BoneName);

	/**
	 * Extension hook for cosmetic states that need more than material parameters. Cosmetic only:
	 * a dedicated server builds no cosmetics and never calls it. Off the authority it runs once
	 * GetItemDefinition() is resident.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category="PEP|Item|World Presentation",
		meta=(DisplayName="On Presentation Node Applied"))
	void ReceivePresentationNodeApplied(
		const FPEPWorldPresentationNode& Node,
		UStaticMeshComponent* VisualComponent);

	void InitializeFromSpawnSource();
	void CreateTreeFromPreset(const UPEPItemPreset* Preset);
	void CreateTreeFromDefinition(const UPEPItemDefinition* Definition);
	const UPEPItemDefinition* GetAuthoringPreviewDefinition() const;
	bool BuildVisualNode(
		const FPEPItemInstance& Item,
		FPEPWorldPresentationNode& OutNode) const;
	void RefreshPublicPresentation();
	void AppendPublicAttachmentNodes(
		const FPEPItemInstance& ParentItem,
		int32 ParentNodeId,
		TSet<FPEPItemId>& VisitedItemIds,
		FPEPWorldPresentationView& OutPresentation) const;
	/** The replicated nodes, or an authoring preview built into PreviewStorage when there are none. */
	const TArray<FPEPWorldPresentationNode>& GetLocalPresentationNodes(
		TArray<FPEPWorldPresentationNode>& PreviewStorage) const;
	void RequestPresentationMeshes(uint32 Generation);
	void BuildPresentationComponents(uint32 Generation);

	/** Rebuilds the local visuals only when what they show has changed since the last build. */
	void UpdateLocalPresentation();

	/** Stops a pending visual load without touching what is currently displayed. */
	void CancelPresentationLoad();

	/** Streams PublicItemDefinition in off the authority and keeps it resident while it applies. */
	void RequestPublicItemDefinition();

	/** Whether visuals are worth building here at all: a dedicated server only needs collision. */
	bool ShouldBuildCosmetics() const;

	/** Combined handles avoid synchronous loads and are invalidated by PresentationGeneration. */
	TSharedPtr<FStreamableHandle> VisualLoadHandle;
	TSharedPtr<FStreamableHandle> SpawnSourceLoadHandle;
	TSharedPtr<FStreamableHandle> PublicItemDefinitionLoadHandle;
	uint32 PresentationGeneration = 0;

	/**
	 * Set when PublicPresentation or PublicItemDefinition changed since the visuals were last
	 * built. Construction, BeginPlay, OnRep and every authority refresh all used to rebuild
	 * unconditionally — three rebuilds for each spawned item, four on a client.
	 */
	bool bLocalPresentationDirty = true;

	/** The public definition the current local visuals were built against. */
	FSoftObjectPath AppliedPresentationDefinitionPath;

	/** The public definition PublicItemDefinitionLoadHandle was requested for. */
	FSoftObjectPath RequestedPublicItemDefinitionPath;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UStaticMeshComponent>> AttachmentMeshComponents;

	/** Keeps runtime material instances reachable for the lifetime of the current projection. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterialInstances;
};
