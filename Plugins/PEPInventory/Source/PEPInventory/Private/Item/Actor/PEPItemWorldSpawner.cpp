#include "Item/Actor/PEPItemWorldSpawner.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Inventory/PEPInventorySettings.h"

void APEPItemWorldSpawner::BeginPlay()
{
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	RootStaticMeshComponent->SetSimulatePhysics(false);
	Super::BeginPlay();
}
bool APEPItemWorldSpawner::StoreAuthorityTree(const FPEPItemInstanceTree& Tree)
{
	const TSubclassOf<APEPItemWorldActor> ActorClass =
		PEPInventorySettings().ResolveWorldItemActorClassForDefinition(Tree.RootItem.GetDefinitionIfLoaded());
	if (!GetWorld() || !GetWorld()->IsGameWorld() || !ActorClass
		|| ActorClass->HasAnyClassFlags(CLASS_Abstract)
		|| ActorClass->IsChildOf(StaticClass())) return false;
	const FTransform Transform = GetActorTransform();
	APEPItemWorldActor* Item = GetWorld()->SpawnActorDeferred<APEPItemWorldActor>(
		ActorClass, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Item) return false;
	/* Only ever reached from SetItemTree or a preset build, both of which validated this tree. */
	if (!Item->ApplyValidatedItemTree(Tree))
	{
		Item->Destroy();
		return false;
	}
	Item->FinishSpawning(Transform);
	/* The caller sees this and stops: nothing is presented or replicated from a spawner on its way out. */
	Destroy();
	return true;
}
