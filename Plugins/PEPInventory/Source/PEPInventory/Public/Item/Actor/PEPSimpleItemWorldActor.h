#pragma once
#include "Item/Actor/PEPItemWorldActor.h"
#include "PEPSimpleItemWorldActor.generated.h"

/** One runtime instance, without a persistent tree or inventory component. */
UCLASS(BlueprintType, Blueprintable)
class PEPINVENTORY_API APEPSimpleItemWorldActor : public APEPItemWorldActor
{
	GENERATED_BODY()
public:
	virtual bool SupportsDefinition(const UPEPItemDefinition* Definition) const override;
	virtual const FPEPItemInstance* FindAuthorityItemInstance() const override;
protected:
	virtual void ResetAuthorityStorage() override;
	virtual bool StoreAuthorityTree(const FPEPItemInstanceTree& Tree) override;
	UPROPERTY(Transient)
	FPEPItemInstance AuthorityItemInstance;
};
