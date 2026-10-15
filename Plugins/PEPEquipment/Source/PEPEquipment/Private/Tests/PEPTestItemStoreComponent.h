#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Item/PEPItemStore.h"

#include "PEPTestItemStoreComponent.generated.h"

/**
 * Minimal alternate store used to prove that PEPEquipment has no inventory-specific contract.
 *
 * Exported so the plugin's other test modules (PEPEquipmentGMAS, PEPEquipmentUI) can build the
 * same fixture: they include this private header by relative path. Test-only all the same.
 */
UCLASS()
class PEPEQUIPMENT_API UPEPTestItemStoreComponent final : public UActorComponent, public IPEPItemStore
{
	GENERATED_BODY()

public:
	virtual const FPEPItemInstance* FindItem(const FPEPItemId& ItemId) const override;
	virtual bool FindItemLocation(
		const FPEPItemId& ItemId,
		FPEPItemLocation& OutLocation) const override;
	virtual bool FindItemAtLocation(
		const FPEPItemLocation& Location,
		FPEPItemId& OutItemId) const override;
	virtual void GetDirectChildren(
		const FPEPItemId& ParentItemId,
		TArray<FPEPItemId>& OutChildItemIds) const override;
	/** Children in a slot named in PublicSlotNames. */
	virtual void GetPublicChildren(
		const FPEPItemId& ParentItemId,
		TArray<FPEPItemId>& OutChildItemIds) const override;
	/** Any held item, unless named in DeniedGameplayAccess. */
	virtual bool HasGrantedGameplayAccess(
		const FPEPItemId& ItemId,
		FGameplayTag ActionTag) const override;
	virtual FPEPItemResult AddItem(
		const FPEPItemInstance& Item,
		const FPEPItemLocation& Destination,
		const FPEPItemAccessContext& Access) override;
	virtual FPEPItemResult CanMoveItem(
		const FPEPItemId& ItemId,
		const FPEPItemLocation& Destination,
		const FPEPItemAccessContext& Access) const override;
	virtual FPEPItemResult MoveItem(
		const FPEPItemId& ItemId,
		const FPEPItemLocation& Destination,
		const FPEPItemAccessContext& Access) override;
	virtual FPEPItemResult CanSwapItems(
		const FPEPItemId& FirstItemId,
		const FPEPItemId& SecondItemId,
		const FPEPItemAccessContext& Access) const override;
	virtual FPEPItemResult SwapItems(
		const FPEPItemId& FirstItemId,
		const FPEPItemId& SecondItemId,
		const FPEPItemAccessContext& Access) override;
	virtual FPEPItemResult RemoveItem(
		const FPEPItemId& ItemId,
		const FPEPItemAccessContext& Access) override;
	virtual FPEPItemResult MutateItem(
		const FPEPItemId& ItemId,
		const FPEPItemStoreMutation& Mutation,
		const FPEPItemAccessContext& Access) override;
	virtual bool RegisterLocationValidator(
		FGameplayTag RootLocationTag,
		FPEPItemLocationValidator Validator,
		bool bAllowAutoPlacement) override;
	virtual void UnregisterLocationValidator(
		FGameplayTag RootLocationTag,
		const UObject* BoundObject) override;
	virtual FOnPEPItemStoreChanged& OnItemStoreChanged() override
	{
		return OnChanged;
	}

	bool DoesLocationAllowAutoPlacement(FGameplayTag RootLocationTag) const;

	/** This store's own notion of a public slot: a name, with no container vocabulary behind it. */
	TSet<FName> PublicSlotNames;

	/** Items this store reports as out of reach for every gameplay action, wherever they sit. */
	TSet<FPEPItemId> DeniedGameplayAccess;

private:
	struct FStoredItem
	{
		FPEPItemInstance Item;
		FPEPItemLocation Location;
	};

	struct FValidatorBinding
	{
		FGameplayTag RootLocationTag;
		FPEPItemLocationValidator Validator;
		bool bAllowAutoPlacement = true;
	};

	FPEPItemResult ValidateDestination(
		const FPEPItemInstance& Item,
		const FPEPItemLocation& Destination,
		const FPEPItemId& IgnoredItemId) const;
	FStoredItem* FindStoredItem(const FPEPItemId& ItemId);
	const FStoredItem* FindStoredItem(const FPEPItemId& ItemId) const;

	TArray<FStoredItem> Items;
	TArray<FValidatorBinding> Validators;
	FOnPEPItemStoreChanged OnChanged;
};
