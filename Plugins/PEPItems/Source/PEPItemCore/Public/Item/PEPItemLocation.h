#pragma once

#include "CoreMinimal.h"
#include "Core/PEPTypes.h"
#include "GameplayTagContainer.h"

#include "PEPItemLocation.generated.h"

/**
 * Where an item sits inside a store. The vocabulary is deliberately generic: a store knows about
 * ownership slots, never about what they mean. Domain plugins give NamedSlot tags their semantics —
 * equipment, crafting stations, vehicle mounts — by registering a validator with the store.
 */
UENUM(BlueprintType)
enum class EPEPItemLocationType : uint8
{
	None,
	/** A container slot owned by another item, identified by ParentItemId + ContainerSlotName. */
	ItemContainer,
	/** A single named slot whose meaning belongs to a domain plugin, identified by LocationTag. */
	NamedSlot
};

USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemLocation
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item|Location")
	EPEPItemLocationType Type = EPEPItemLocationType::None;

	/**
	 * NamedSlot: Equipment.Slot.Head, Crafting.Slot.Input, ...
	 * ItemContainer uses ParentItemId + ContainerSlotName instead and leaves this unset.
	 */
	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item|Location")
	FGameplayTag LocationTag;

	/** Slot owned by ParentItemId when Type == ItemContainer. */
	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item|Location")
	FName ContainerSlotName;

	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item|Location")
	FPEPItemId ParentItemId;

	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item|Location")
	FIntPoint GridPosition = FIntPoint::ZeroValue;

	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item|Location")
	bool bRotated = false;

	/**
	 * Whether the fields are exactly those Type requires — a location carrying data belonging to
	 * another kind is rejected, not ignored. Says nothing about the slot actually existing.
	 */
	bool IsStructurallyValid() const;

	static FPEPItemLocation MakeItemContainer(
		FPEPItemId InParentItemId,
		FName InContainerSlotName,
		FIntPoint Position = FIntPoint::ZeroValue,
		bool bInRotated = false);

	static FPEPItemLocation MakeNamedSlot(FGameplayTag SlotTag);
};

// Declared at namespace scope rather than as friends: a friend declaration carries no dll export,
// so modules outside PEPItemCore would fail to link against the comparison.
PEPITEMCORE_API bool operator==(const FPEPItemLocation& A, const FPEPItemLocation& B);

inline bool operator!=(const FPEPItemLocation& A, const FPEPItemLocation& B)
{
	return !(A == B);
}

PEPITEMCORE_API uint32 GetTypeHash(const FPEPItemLocation& Location);
