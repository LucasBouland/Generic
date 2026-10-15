#pragma once

#include "CoreMinimal.h"
#include "Equipment/PEPEquipmentNetTypes.h"
#include "Equipment/PEPEquipmentPresentationFragment.h"
#include "GameFramework/Actor.h"

#include "PEPEquippedPresentationActor.generated.h"

struct FStreamableHandle;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/**
 * Local cosmetic actor spawned from the replicated public projection. It never replicates and
 * never owns gameplay state; destroying/recreating it cannot affect the inventory.
 */
UCLASS(BlueprintType, Blueprintable, NotPlaceable)
class PEPEQUIPMENT_API APEPEquippedPresentationActor : public AActor
{
	GENERATED_BODY()

public:
	APEPEquippedPresentationActor();

	void InitializePresentation(
		const FPEPEquippedItemPublic& InPublicState,
		const FPEPEquipmentPresentationFragment& InPresentation);

	void ApplyPublicState(const FPEPEquippedItemPublic& InPublicState);

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

	bool MatchesPresentationNode(const FPEPEquippedItemPublic& State) const;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintImplementableEvent, Category="PEP|Equipment|Presentation")
	void OnPresentationInitialized();

	UFUNCTION(BlueprintImplementableEvent, Category="PEP|Equipment|Presentation")
	void OnPublicStateApplied(const FPEPEquippedItemPublic& State);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	TObjectPtr<USceneComponent> PresentationRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	TObjectPtr<UStaticMeshComponent> StaticMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent;

	UPROPERTY(Transient, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	FPEPEquippedItemPublic PublicState;

	UPROPERTY(Transient, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	FPEPEquipmentPresentationFragment Presentation;

private:
	void RequestPresentationAssets();
	void ApplyLoadedAssets();

	TSharedPtr<FStreamableHandle> AssetLoadHandle;
	bool bPresentationReady = false;
};
