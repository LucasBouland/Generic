#pragma once
#include "Item/Actor/PEPItemWorldActor.h"
#include "PEPItemWorldSpawner.generated.h"

/** Authoring-only carrier: chooses the concrete runtime actor from the root definition. */
UCLASS(BlueprintType, Blueprintable)
class PEPINVENTORY_API APEPItemWorldSpawner : public APEPItemWorldActor
{
	GENERATED_BODY()
public:
	virtual bool SupportsDefinition(const UPEPItemDefinition* Definition) const override { return Definition != nullptr; }
protected:
	virtual void BeginPlay() override;
	virtual bool StoreAuthorityTree(const FPEPItemInstanceTree& Tree) override;
};
