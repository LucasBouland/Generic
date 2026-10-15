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
	virtual const FPEPItemInstance* FindAuthorityItemInstance() const override;
	virtual FPEPItemInstanceTree GetAuthorityItemTree() const override;

	/**
	 * Moves the whole tree into TargetInventory the way a pickup places it, and leaves this actor
	 * empty but alive: whoever asked for the pickup decides what becomes of the actor. The root
	 * leaving any other way — a looter carrying it off through a session — still destroys it, since
	 * an empty container on the ground has no reason to stay. Authority only.
	 */
	FPEPItemResult TransferTreeForPickup(
		UPEPInventoryComponent* TargetInventory,
		const FPEPItemAccessContext& Access);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item")
	TObjectPtr<UPEPInventoryComponent> Inventory;
protected:
	virtual void ResetAuthorityStorage() override;
	virtual bool StoreAuthorityTree(const FPEPItemInstanceTree& Tree) override;
	virtual void VisitAuthorityChildren(
		FPEPItemId ParentId,
		TFunctionRef<void(const FPEPItemInstance&, const FPEPItemLocation&)> Visitor) const override;
private:
	void ConfigureInventory();
	void HandleStoreChanged(const FPEPItemStoreChange& Change);

	/** Whether Change can alter what this actor shows: its root, or anything in an Attachment chain from it. */
	bool DoesChangeAffectPresentation(const FPEPItemStoreChange& Change) const;

	/** Whether an item at Location would be drawn: in an Attachment slot of the root, or of something drawn. */
	bool IsPresentedLocation(const FPEPItemLocation& Location) const;

	FPEPItemResult ValidateRoot(const FPEPItemInstance& Item, const FPEPItemLocation& Location) const;
	UPROPERTY(Transient)
	FPEPItemId RootItemId;
	bool bReplacingStorage = false;
};
