#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Components/PEPInventoryComponent.h"
#include "Components/PEPInventorySessionComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Item/Fragments/PEPItemFragment_InventoryShape.h"
#include "Item/Fragments/PEPItemFragment_Stackable.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemInstance.h"
#include "Item/PEPItemTags.h"
#include "StructUtils/InstancedStruct.h"

/**
 * The tests' one way into a view's callback targets, which nothing else may set: the replication
 * tests drive the entry callbacks by hand on a detached view, and it has to name the store they
 * report to. Global, because that is where FPEPInventoryView's friend declaration looks for it.
 */
struct FPEPInventoryViewTestAccess
{
	static void BindToInventory(FPEPInventoryView& View, UPEPInventoryComponent* Inventory)
	{
		View.OwnerComponent = Inventory;
		View.SessionOwner = nullptr;
		View.bIsSessionView = false;
	}
};

namespace PEPInventoryTest
{
	/**
	 * The session component standing in for the player who owns Inventory, created on its owner
	 * the first time it is asked for. In a game it sits on the PlayerController; a test owner is
	 * whatever actor carries the store, so it is placed there and told which store is local.
	 */
	inline UPEPInventorySessionComponent* SessionFor(UPEPInventoryComponent* Inventory)
	{
		AActor* Owner = Inventory ? Inventory->GetOwner() : nullptr;
		if (!Owner)
		{
			return nullptr;
		}
		if (UPEPInventorySessionComponent* Existing = Owner->FindComponentByClass<UPEPInventorySessionComponent>())
		{
			return Existing;
		}
		UPEPInventorySessionComponent* Session = NewObject<UPEPInventorySessionComponent>(Owner);
		Session->SetLocalInventory(Inventory);
		Session->RegisterComponent();
		return Session;
	}

	/**
	 * A throwaway world with one authoritative actor carrying an inventory.
	 *
	 * A world is needed because mutations check GetOwner()->HasAuthority(); an actor spawned in a
	 * standalone game world is authoritative, which is what the server-side paths expect.
	 * Definitions are transient and registered through PEPItemAutomationTestSupport so that
	 * FPEPItemInstance can resolve them without an asset manager.
	 */
	struct FFixture
	{
		UWorld* World = nullptr;
		AActor* Owner = nullptr;
		UPEPInventoryComponent* Inventory = nullptr;
		/** This player's window into other stores, on the same actor as the store it takes into. */
		UPEPInventorySessionComponent* Session = nullptr;
		TArray<TObjectPtr<UPEPItemDefinition>> Definitions;

		FFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());

			Owner = World->SpawnActor<AActor>();
			Inventory = NewObject<UPEPInventoryComponent>(Owner);
			Inventory->RegisterComponent();
			Session = SessionFor(Inventory);

			/*
			 * Without RootGrid, an inventory only holds a root item through a named slot, and
			 * named slots have no meaning until a domain plugin claims them. Equipment does that
			 * in the game; here a permissive validator stands in for it so a tree can be seeded.
			 */
			Inventory->RegisterLocationValidator(
				RootSlotTag(),
				FPEPItemLocationValidator::CreateLambda(
					[](const FPEPItemInstance& Item, const FPEPItemLocation&)
					{
						return FPEPItemResult::Success(Item.ItemId);
					}),
				/*bAllowAutoPlacement*/ true);
		}

		static FGameplayTag RootSlotTag()
		{
			return PEPItemTags::Item_Slot_Content_Main;
		}

		/** Places Item in the stand-in equipment slot, which is the only way into an empty store. */
		FPEPItemResult AddToRootSlot(const FPEPItemInstance& Item)
		{
			return Inventory->AddItem(
				Item,
				FPEPItemLocation::MakeNamedSlot(RootSlotTag()),
				FPEPItemAccessContext::System());
		}

		~FFixture()
		{
			for (const TObjectPtr<UPEPItemDefinition>& Definition : Definitions)
			{
				if (Definition)
				{
					PEPItemAutomationTestSupport::UnregisterTransientDefinitionForTests(
						Definition->GetPrimaryAssetId());
				}
			}

			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		/** A definition carrying whatever fragments the test needs, already resolvable. */
		UPEPItemDefinition* MakeDefinition(
			const FName Name,
			const TArray<FInstancedStruct>& Fragments)
		{
			UPEPItemDefinition* Definition =
				NewObject<UPEPItemDefinition>(GetTransientPackage(), Name);
			Definition->DefinitionFragments = Fragments;

			PEPItemAutomationTestSupport::RegisterTransientDefinitionForTests(Definition);
			Definitions.Add(Definition);
			return Definition;
		}

		/** A definition occupying Size cells, with no other capability. */
		UPEPItemDefinition* MakeShapedDefinition(const FName Name, const FIntPoint Size)
		{
			FPEPItemFragment_InventoryShape Shape;
			Shape.Size = Size;
			Shape.bCanRotate = true;

			return MakeDefinition(Name, { FInstancedStruct::Make(Shape) });
		}

		/** A definition owning one grid slot of GridSize cells, so it can hold other items. */
		UPEPItemDefinition* MakeContainerDefinition(
			const FName Name,
			const FName SlotName,
			const FIntPoint GridSize)
		{
			FPEPContainerSlotDefinition Slot;
			Slot.SlotName = SlotName;
			Slot.Mode = EPEPContainerSlotMode::Grid;
			Slot.GridSize = GridSize;
			Slot.bOwnerCanPut = true;
			Slot.bOwnerCanTake = true;
			Slot.bOwnerCanMoveInside = true;
			// Pinned rather than inherited: tests that check outsider access expect a slot an
			// outsider may only take from, whatever the project default becomes.
			Slot.bOtherCanTake = true;
			Slot.bOtherCanPut = false;
			Slot.bOtherCanMoveInside = false;

			FPEPItemFragment_Container Container;
			Container.Slots.Add(Slot);

			FPEPItemFragment_InventoryShape Shape;
			Shape.Size = FIntPoint(1, 1);

			return MakeDefinition(
				Name,
				{ FInstancedStruct::Make(Container), FInstancedStruct::Make(Shape) });
		}

		/** A stackable definition; stacking forbids per-unit state, so it carries none. */
		UPEPItemDefinition* MakeStackableDefinition(const FName Name, const int32 MaxStackSize)
		{
			FPEPItemFragment_Stackable Stackable;
			Stackable.bUseProjectDefaultMaxStackSize = false;
			Stackable.MaxStackSize = MaxStackSize;

			FPEPItemFragment_InventoryShape Shape;
			Shape.Size = FIntPoint(1, 1);

			return MakeDefinition(
				Name,
				{ FInstancedStruct::Make(Stackable), FInstancedStruct::Make(Shape) });
		}

		static FPEPItemInstance MakeItem(const UPEPItemDefinition* Definition)
		{
			return PEPItemAutomationTestSupport::MakeItemForTests(Definition);
		}
	};
}

#endif
