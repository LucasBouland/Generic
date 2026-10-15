#pragma once

#include "CoreMinimal.h"
#include "Core/PEPTypes.h"
#include "Item/PEPItemResultTypes.h"
#include "Engine/AssetManagerTypes.h"
#include "Item/PEPItemAccess.h"
#include "Item/PEPItemInstance.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PEPItemContainerRules.generated.h"

class UPEPItemDefinition;

namespace PEPItemContainerRules
{
	/** @param KeyItem  Optional; consulted only when the container declares unlock key tags. */
	PEPINVENTORY_API FPEPItemResult CanUnlockContainer(
		const FPEPItemInstance& ContainerItem,
		const FPEPItemInstance* KeyItem);

	PEPINVENTORY_API FPEPItemResult CanLockContainer(
		const FPEPItemInstance& ContainerItem);

	/*
	 * Seeing and taking are different questions, and the container vocabulary has always answered
	 * both. Only the taking half was ever consulted: a store mirroring another one for a looter
	 * copied every entry it held, so a pocket a designer marked Hidden was replicated to the very
	 * player it was meant to be hidden from.
	 *
	 * These two decide the seeing half. They read a definition and a role and nothing else — no
	 * store, no authority — which is what lets a mirror, a UI and an appraising AI reach the same
	 * verdict instead of each inventing one.
	 *
	 * Not exposed to Blueprint: EPEPAccessRole is deliberately unreflected, because a caller free
	 * to name its own role is free to name itself the owner.
	 */

	/**
	 * Whether Role may see what a slot holds.
	 *
	 * Distinct from bCanTake and bCanPut, which say what may be done with what is already seen: a
	 * display case is visible and gives nothing up, a sealed pouch is neither.
	 */
	PEPINVENTORY_API bool DoesSlotRevealOccupants(
		const FPEPContainerSlotDefinition& Slot,
		EPEPAccessRole Role);

	/**
	 * Whether Role may see into this container at all, before any slot of it is considered.
	 *
	 * The container-wide gate, so ShowContainerOnly needs no per-slot bookkeeping: a rucksack can
	 * appear on a corpse with nothing of what it holds. The owner is never gated here — this reads
	 * ExternalLootVisibility, which says what it applies to.
	 */
	PEPINVENTORY_API bool DoesContainerRevealContents(
		const FPEPItemInstance& ContainerItem,
		EPEPAccessRole Role);
}

UCLASS()
class PEPINVENTORY_API UPEPItemContainerRulesLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="PEP|Item|Container|Rules")
	static FPEPItemResult CanUnlockContainer(
		const FPEPItemInstance& ContainerItem,
		const FPEPItemInstance& KeyItem,
		bool bUseKeyItem);

	UFUNCTION(BlueprintPure, Category="PEP|Item|Container|Rules")
	static FPEPItemResult CanLockContainer(
		const FPEPItemInstance& ContainerItem);

	/** Slots declared on Definition's container fragment, or empty if it has none. For editor tooling: a slot dropdown does not need to know the fragment exists. */
	UFUNCTION(BlueprintPure, Category="PEP|Item|Container|Rules")
	static TArray<FPEPContainerSlotDefinition> GetContainerSlots(const UPEPItemDefinition* Definition);

	/**
	 * Whether CandidateItem may be placed in ContainerDefinition's slot named SlotName, by tag
	 * rules alone (the slot's own rules plus its owning container's). Grid fit and rotation depend
	 * on siblings already placed, so they are not decided here; a preset build or a live container
	 * still makes the final call.
	 */
	UFUNCTION(BlueprintPure, Category="PEP|Item|Container|Rules")
	static bool CanPlaceItemInSlot(
		const UPEPItemDefinition* ContainerDefinition,
		FName SlotName,
		const UPEPItemDefinition* CandidateItem,
		FText& OutError);
};
