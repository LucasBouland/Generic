#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "PEPInteractionTargetComponent.generated.h"

/** Optional per-actor data used to prioritize an interactable during local selection. */
UCLASS(ClassGroup=(PEP), meta=(BlueprintSpawnableComponent))
class PEPINTERACTION_API UPEPInteractionTargetComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPEPInteractionTargetComponent();

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Normalized priority added to target selection scoring. 0 is neutral; 1 is highest priority. */
	UPROPERTY(
		Replicated,
		EditAnywhere,
		BlueprintReadWrite,
		Category="PEP|Interaction|Selection",
		meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float SelectionPriority = 0.0f;
};
