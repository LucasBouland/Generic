#pragma once

#include "CoreMinimal.h"
#include "Equipment/PEPEquipmentPresentationFragment.h"
#include "Equipment/PEPEquipmentTypes.h"
#include "GameFramework/Actor.h"

#include "PEPEquippedPresentationActor.generated.h"

struct FStreamableHandle;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/**
 * Local cosmetic actor spawned from the replicated public projection. It never replicates and
 * never owns gameplay state; destroying/recreating it cannot affect the inventory.
 *
 * One actor presents one node for its whole life: when the node changes (slot, parent or
 * definition), equipment destroys the actor and spawns another rather than updating it in place.
 */
UCLASS(BlueprintType, Blueprintable, NotPlaceable)
class PEPEQUIPMENT_API APEPEquippedPresentationActor : public AActor
{
	GENERATED_BODY()

public:
	APEPEquippedPresentationActor();

	/**
	 * Called once, right after spawning and attaching: raises OnPublicStateApplied, then loads the
	 * visual and raises OnPresentationInitialized once it is assigned.
	 */
	void InitializePresentation(
		const FPEPEquippedItemInfo& InPublicState,
		const FPEPEquipmentPresentationFragment& InPresentation);

	FPEPItemId GetPresentedItemId() const
	{
		return PublicState.ItemId;
	}

	/** True once the configured visual assets have been assigned to their components. */
	bool IsPresentationReady() const
	{
		return bPresentationReady;
	}

	/** Parent for locally spawned attachment actors. Prefers the mesh that is actually visible. */
	USceneComponent* GetPresentationAttachComponent() const;

	FName ResolveAttachmentSocket(FName SlotName) const
	{
		return Presentation.ResolveAttachmentSocket(SlotName);
	}

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintImplementableEvent, Category="PEP|Equipment|Presentation")
	void OnPresentationInitialized();

	/**
	 * Raised once per actor, from InitializePresentation, with the public node it presents, before
	 * the visual is loaded. A temporary prop receives a state whose only valid field is
	 * DefinitionId (its ItemId is invalid). The node never changes afterwards: a changed node gets
	 * a new actor.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category="PEP|Equipment|Presentation")
	void OnPublicStateApplied(const FPEPEquippedItemInfo& State);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	TObjectPtr<USceneComponent> PresentationRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	TObjectPtr<UStaticMeshComponent> StaticMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent;

	UPROPERTY(Transient, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	FPEPEquippedItemInfo PublicState;

	UPROPERTY(Transient, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	FPEPEquipmentPresentationFragment Presentation;

private:
	void RequestPresentationAssets();
	void ApplyLoadedAssets();

	TSharedPtr<FStreamableHandle> AssetLoadHandle;
	bool bPresentationReady = false;
};
