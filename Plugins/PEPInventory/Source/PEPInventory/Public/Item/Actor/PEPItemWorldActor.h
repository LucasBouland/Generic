#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Item/PEPItemWorldPresentationTypes.h"
#include "Item/PEPItemPresetTypes.h"
#include "PEPItemWorldActor.generated.h"

class UStaticMeshComponent;
class UPrimitiveComponent;
class UMaterialInstanceDynamic;
class UPEPItemDefinition;
class UPEPItemPreset;

UCLASS(Abstract, BlueprintType, Blueprintable)
class PEPINVENTORY_API APEPItemWorldActor : public AActor
{
	GENERATED_BODY()

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

	/** Sets the exact runtime item instance (preserves state, e.g. when dropping from an inventory). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Item")
	virtual void SetItemInstance(const FPEPItemInstance& InInstance);

	/** Sets a root and its private descendants, preserving a container or configured attachment tree. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Item")
	virtual bool SetItemTree(const FPEPItemInstanceTree& InTree);

	/**
	 * The item's definition. On the authority it is read straight from the private runtime
	 * instance; every other machine reads PublicItemDefinition instead, replicated for exactly
	 * this purpose. The definition asset itself carries no per-instance state (that stays in
	 * authority storage, never replicated) — only which definition this actor represents.
	 */
	UFUNCTION(BlueprintCallable, Category="PEP|Item")
	UPEPItemDefinition* GetItemDefinition() const;

	/** Authority-only gameplay data. Clients receive PublicPresentation instead. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Item|Authority")
	virtual FPEPItemInstance GetAuthorityItemInstance() const;

	/** Authority-only root and private descendants used by pickup/drop transactions. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="PEP|Item|Authority")
	virtual FPEPItemInstanceTree GetAuthorityItemTree() const;

	/** Root plus visible Attachment descendants. Contains no runtime item state or item IDs. */
	UFUNCTION(BlueprintPure, Category="PEP|Item|World Presentation")
	TArray<FPEPWorldPresentationNode> GetPublicPresentationNodes() const
	{
		return PublicPresentation.Nodes;
	}

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
	virtual void GetAuthorityChildren(FPEPItemId ParentId, TArray<FPEPItemInstanceTreeEntry>& OutChildren) const {}

	/** Cosmetic-only network payload: root and Attachment descendants, never Content or state. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing=OnRep_PublicPresentation,
		Category="PEP|Item|World Presentation")
	FPEPWorldPresentationView PublicPresentation;

	/**
	 * Which definition this actor represents, replicated so any machine can query its static data
	 * (tags, fragments) through GetItemDefinition() without authority. The definition asset is
	 * shipped content, identical everywhere; this only says which one applies here. Per-instance
	 * state (durability, ammo, ...) still lives in authority storage and is never replicated.
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

	/** Extension hook for cosmetic states that need more than material parameters. */
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
	void GetLocalPresentationNodes(TArray<FPEPWorldPresentationNode>& OutNodes) const;
	void RequestPresentationMeshes(uint32 Generation);
	void BuildPresentationComponents(uint32 Generation);
	void ResetLocalPresentation();

	/** Combined handles avoid synchronous loads and are invalidated by PresentationGeneration. */
	TSharedPtr<struct FStreamableHandle> VisualLoadHandle;
	TSharedPtr<struct FStreamableHandle> SpawnSourceLoadHandle;
	uint32 PresentationGeneration = 0;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UStaticMeshComponent>> AttachmentMeshComponents;

	/** Keeps runtime material instances reachable for the lifetime of the current projection. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterialInstances;
};
