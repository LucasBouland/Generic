#pragma once
#include "Item/Actor/PEPItemWorldActor.h"
#include "Item/PEPItemStore.h"
#include "PEPContainerItemWorldActor.generated.h"
class UPEPInventoryComponent;

/** The root and every descendant live exclusively in Inventory. */
UCLASS(BlueprintType, Blueprintable)
class PEPINVENTORY_API APEPContainerItemWorldActor : public APEPItemWorldActor
{
	GENERATED_BODY()
public:
	APEPContainerItemWorldActor();
	virtual bool SupportsDefinition(const UPEPItemDefinition* Definition) const override;
	virtual void PostInitializeComponents() override;
	virtual FPEPItemInstance GetAuthorityItemInstance() const override;
	virtual FPEPItemInstanceTree GetAuthorityItemTree() const override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item")
	TObjectPtr<UPEPInventoryComponent> Inventory;
protected:
	virtual void ResetAuthorityStorage() override;
	virtual bool StoreAuthorityTree(const FPEPItemInstanceTree& Tree) override;
	virtual void GetAuthorityChildren(FPEPItemId ParentId, TArray<FPEPItemInstanceTreeEntry>& OutChildren) const override;
private:
	void ConfigureInventory();
	void HandleStoreChanged(const FPEPItemStoreChange& Change);
	FPEPItemResult ValidateRoot(const FPEPItemInstance& Item, const FPEPItemLocation& Location) const;
	UPROPERTY(Transient)
	FPEPItemId RootItemId;
	bool bReplacingStorage = false;
};
