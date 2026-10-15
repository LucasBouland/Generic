#include "Equipment/PEPEquippedPresentationActor.h"

#include "Algo/AllOf.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

APEPEquippedPresentationActor::APEPEquippedPresentationActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetCanBeDamaged(false);

	PresentationRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PresentationRoot"));
	SetRootComponent(PresentationRoot);

	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
	StaticMeshComponent->SetupAttachment(PresentationRoot);
	StaticMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletalMesh"));
	SkeletalMeshComponent->SetupAttachment(PresentationRoot);
	SkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void APEPEquippedPresentationActor::InitializePresentation(
	const FPEPEquippedItemInfo& InPublicState,
	const FPEPEquipmentPresentationFragment& InPresentation)
{
	PublicState = InPublicState;
	Presentation = InPresentation;
	bPresentationReady = false;
	OnPublicStateApplied(PublicState);
	RequestPresentationAssets();
}

void APEPEquippedPresentationActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AssetLoadHandle.IsValid())
	{
		AssetLoadHandle->CancelHandle();
		AssetLoadHandle.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void APEPEquippedPresentationActor::RequestPresentationAssets()
{
	TArray<FSoftObjectPath> Paths;
	if (const TSoftObjectPtr<UStaticMesh>* Mesh = Presentation.GetStaticMeshVisual();
		Mesh && !Mesh->IsNull())
	{
		Paths.AddUnique(Mesh->ToSoftObjectPath());
	}
	if (const TSoftObjectPtr<USkeletalMesh>* Mesh = Presentation.GetSkeletalMeshVisual();
		Mesh && !Mesh->IsNull())
	{
		Paths.AddUnique(Mesh->ToSoftObjectPath());
	}

	if (Paths.IsEmpty())
	{
		ApplyLoadedAssets();
		return;
	}

	// Equipment preloads attached presentation visuals before publishing their
	// socket provider. Keep this synchronous fast path so a loaded parent mesh
	// is assigned before any child is allowed to attach to one of its sockets.
	const bool bAllAssetsLoaded = Algo::AllOf(
		Paths,
		[](const FSoftObjectPath& Path)
		{
			return Path.ResolveObject() != nullptr;
		});
	if (bAllAssetsLoaded)
	{
		ApplyLoadedAssets();
		return;
	}

	AssetLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		Paths,
		FStreamableDelegate::CreateWeakLambda(this, [this]()
		{
			ApplyLoadedAssets();
		}));
}

void APEPEquippedPresentationActor::ApplyLoadedAssets()
{
	const TSoftObjectPtr<UStaticMesh>* StaticVisual = Presentation.GetStaticMeshVisual();
	const TSoftObjectPtr<USkeletalMesh>* SkeletalVisual = Presentation.GetSkeletalMeshVisual();
	UStaticMesh* StaticMesh = StaticVisual ? StaticVisual->Get() : nullptr;
	USkeletalMesh* SkeletalMesh = SkeletalVisual ? SkeletalVisual->Get() : nullptr;

	StaticMeshComponent->SetStaticMesh(StaticMesh);
	StaticMeshComponent->SetRelativeTransform(Presentation.MeshTransform);
	StaticMeshComponent->SetVisibility(StaticMesh != nullptr);

	SkeletalMeshComponent->SetSkeletalMesh(SkeletalMesh);
	SkeletalMeshComponent->SetRelativeTransform(Presentation.MeshTransform);
	SkeletalMeshComponent->SetVisibility(SkeletalMesh != nullptr);

	// The carrier owns both mesh component kinds, but only the component selected by the
	// presentation is a semantic part of the item. Keep per-item tags off the inactive carrier.
	for (const FName Tag : Presentation.MeshComponentTags)
	{
		if (StaticMesh)
		{
			StaticMeshComponent->ComponentTags.AddUnique(Tag);
		}
		if (SkeletalMesh)
		{
			SkeletalMeshComponent->ComponentTags.AddUnique(Tag);
		}
	}

	bPresentationReady = true;
	AssetLoadHandle.Reset();
	OnPresentationInitialized();
}

USceneComponent* APEPEquippedPresentationActor::GetPresentationAttachComponent() const
{
	if (SkeletalMeshComponent && SkeletalMeshComponent->GetSkeletalMeshAsset())
	{
		return SkeletalMeshComponent;
	}
	if (StaticMeshComponent && StaticMeshComponent->GetStaticMesh())
	{
		return StaticMeshComponent;
	}
	return PresentationRoot;
}
