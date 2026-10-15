#include "Item/Actor/PEPItemWorldActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Item/Fragments/PEPItemFragment_WorldPresentation.h"
#include "Item/PEPItemDefinition.h"
#include "Item/PEPItemInstanceLibrary.h"
#include "Item/PEPItemLogChannels.h"
#include "Item/PEPItemPreset.h"
#include "Item/PEPItemQueryTypes.h"
#include "Item/PEPItemSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace PEPItemWorldActorPrivate
{
	/** A pending handle is cancelled and a completed one released; either way its callback never runs. */
	void StopStreamableHandle(TSharedPtr<FStreamableHandle>& Handle)
	{
		if (!Handle.IsValid())
		{
			return;
		}

		if (Handle->HasLoadCompleted())
		{
			Handle->ReleaseHandle();
		}
		else
		{
			Handle->CancelHandle();
		}
		Handle.Reset();
	}
}

APEPItemWorldActor::APEPItemWorldActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	RootStaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(
		TEXT("RootStaticMeshComponent"));
	SetRootComponent(RootStaticMeshComponent);

	RootStaticMeshComponent->SetMobility(EComponentMobility::Movable);
	RootStaticMeshComponent->SetCollisionProfileName(TEXT("WorldDynamic"));
	RootStaticMeshComponent->SetSimulatePhysics(true);
}

void APEPItemWorldActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	/*
	 * The authoring preview only. In a game world the visuals follow the authority tree or the
	 * replicated presentation, each of which rebuilds when it changes; building here as well was a
	 * wasted pass for every item spawned at runtime.
	 */
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		ApplyDefinitionVisual();
	}
}

void APEPItemWorldActor::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		if (bUsePhysicsBasedNetworkDormancy && RootStaticMeshComponent)
		{
			RootStaticMeshComponent->OnComponentSleep.AddUniqueDynamic(
				this, &ThisClass::HandleRootPhysicsSleep);
			RootStaticMeshComponent->OnComponentWake.AddUniqueDynamic(
				this, &ThisClass::HandleRootPhysicsWake);
		}

		if (!FindAuthorityItemInstance())
		{
			InitializeFromSpawnSource();
		}

		/* A spawner hands its tree to the actor it spawns and destroys itself on the way. */
		if (IsActorBeingDestroyed())
		{
			return;
		}
		RefreshPublicPresentation();
	}
	else
	{
		RequestPublicItemDefinition();
		UpdateLocalPresentation();
	}
}

void APEPItemWorldActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	/* Nothing streamed in for this actor may call back into it once it has left play. */
	++PresentationGeneration;
	PEPItemWorldActorPrivate::StopStreamableHandle(VisualLoadHandle);
	PEPItemWorldActorPrivate::StopStreamableHandle(SpawnSourceLoadHandle);
	PEPItemWorldActorPrivate::StopStreamableHandle(PublicItemDefinitionLoadHandle);
	RequestedPublicItemDefinitionPath.Reset();

	if (RootStaticMeshComponent)
	{
		RootStaticMeshComponent->OnComponentSleep.RemoveDynamic(
			this, &ThisClass::HandleRootPhysicsSleep);
		RootStaticMeshComponent->OnComponentWake.RemoveDynamic(
			this, &ThisClass::HandleRootPhysicsWake);
	}

	Super::EndPlay(EndPlayReason);
}

void APEPItemWorldActor::HandleRootPhysicsSleep(
	UPrimitiveComponent* SleepingComponent,
	const FName BoneName)
{
	(void)BoneName;
	if (!HasAuthority()
		|| !bUsePhysicsBasedNetworkDormancy
		|| !RootStaticMeshComponent
		|| SleepingComponent != RootStaticMeshComponent
		|| !RootStaticMeshComponent->IsSimulatingPhysics()
		|| NetDormancy == DORM_DormantAll)
	{
		return;
	}

	/* Send the final resting transform, then stop considering this stable actor for replication. */
	ForceNetUpdate();
	SetNetDormancy(DORM_DormantAll);
}

void APEPItemWorldActor::HandleRootPhysicsWake(
	UPrimitiveComponent* WakingComponent,
	const FName BoneName)
{
	(void)BoneName;
	if (!HasAuthority()
		|| !bUsePhysicsBasedNetworkDormancy
		|| !RootStaticMeshComponent
		|| WakingComponent != RootStaticMeshComponent
		|| NetDormancy == DORM_Awake)
	{
		return;
	}

	SetNetDormancy(DORM_Awake);
	ForceNetUpdate();
}

void APEPItemWorldActor::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, PublicPresentation);
	DOREPLIFETIME(ThisClass, PublicItemDefinition);
}

const UPEPItemDefinition* APEPItemWorldActor::GetItemDefinition() const
{
	if (HasAuthority())
	{
		const FPEPItemInstance* Item = FindAuthorityItemInstance();
		return Item ? Item->GetDefinitionIfLoaded() : nullptr;
	}

	/* Resident once RequestPublicItemDefinition's load lands; never loaded synchronously here. */
	return PublicItemDefinition.Get();
}

FPEPItemInstance APEPItemWorldActor::GetAuthorityItemInstance() const
{
	const FPEPItemInstance* Item = FindAuthorityItemInstance();
	return Item ? *Item : FPEPItemInstance();
}

FPEPItemInstanceTree APEPItemWorldActor::GetAuthorityItemTree() const
{
	FPEPItemInstanceTree Tree;
	Tree.RootItem = GetAuthorityItemInstance();
	return Tree;
}

void APEPItemWorldActor::SetSpawnPreset(UPEPItemPreset* InPreset)
{
	if (!HasAuthority())
	{
		return;
	}

	PEPItemWorldActorPrivate::StopStreamableHandle(SpawnSourceLoadHandle);

	SpawnPreset = InPreset;
	SpawnItemDefinition = nullptr;
	ResetAuthorityStorage();
	InitializeFromSpawnSource();

	/*
	 * Once, after the new source had its chance: a source that built synchronously has already
	 * refreshed and this finds nothing to do, one still loading publishes the empty actor. Before,
	 * the empty state was published first and the new item second — two rebuilds per call.
	 */
	if (!IsActorBeingDestroyed())
	{
		RefreshPublicPresentation();
	}
}

void APEPItemWorldActor::SetSpawnItemDefinition(UPEPItemDefinition* InDefinition)
{
	if (!HasAuthority())
	{
		return;
	}

	PEPItemWorldActorPrivate::StopStreamableHandle(SpawnSourceLoadHandle);

	SpawnItemDefinition = InDefinition;
	SpawnPreset = nullptr;
	ResetAuthorityStorage();
	InitializeFromSpawnSource();

	if (!IsActorBeingDestroyed())
	{
		RefreshPublicPresentation();
	}
}

void APEPItemWorldActor::SetItemInstance(const FPEPItemInstance& InInstance)
{
	if (!HasAuthority())
	{
		return;
	}

	if (!InInstance.IsValid())
	{
		ResetAuthorityStorage();
		RefreshPublicPresentation();
		return;
	}

	FPEPItemInstanceTree Tree;
	Tree.RootItem = InInstance;
	SetItemTree(Tree);
}

bool APEPItemWorldActor::SetItemTree(const FPEPItemInstanceTree& InTree)
{
	if (!HasAuthority())
	{
		return false;
	}

	FPEPItemInstanceTreeValidationResult Validation;
	if (!InTree.ValidateDetailed(Validation))
	{
		return false;
	}

	return ApplyValidatedItemTree(InTree);
}

bool APEPItemWorldActor::ApplyValidatedItemTree(const FPEPItemInstanceTree& InTree)
{
	if (!HasAuthority()
		|| !SupportsDefinition(InTree.RootItem.GetDefinitionIfLoaded())
		|| !StoreAuthorityTree(InTree))
	{
		return false;
	}

	/*
	 * A spawner stores a tree by handing it to the actor it spawns and destroying itself. There is
	 * nothing left here to present or to push to the network.
	 */
	if (IsActorBeingDestroyed())
	{
		return true;
	}

	/* Pushes the net update itself when, and only when, something replicated changed. */
	RefreshPublicPresentation();
	return true;
}

bool APEPItemWorldActor::TakeAuthorityItemTree(FPEPItemInstanceTree& OutTree)
{
	OutTree.Reset();
	if (!HasAuthority() || !FindAuthorityItemInstance())
	{
		return false;
	}

	OutTree = GetAuthorityItemTree();
	if (!OutTree.RootItem.IsValid())
	{
		OutTree.Reset();
		return false;
	}

	ResetAuthorityStorage();
	RefreshPublicPresentation();
	return true;
}

void APEPItemWorldActor::InitializeFromSpawnSource()
{
	if (!HasAuthority() || FindAuthorityItemInstance())
	{
		return;
	}

	if (!SpawnItemDefinition.IsNull())
	{
		if (UPEPItemDefinition* LoadedDefinition = SpawnItemDefinition.Get())
		{
			CreateTreeFromDefinition(LoadedDefinition);
			return;
		}

		const FSoftObjectPath RequestedDefinitionPath = SpawnItemDefinition.ToSoftObjectPath();
		SpawnSourceLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
			RequestedDefinitionPath,
			FStreamableDelegate::CreateWeakLambda(
				this,
				[this, RequestedDefinitionPath]()
				{
					if (!HasAuthority()
						|| IsActorBeingDestroyed()
						|| FindAuthorityItemInstance()
						|| SpawnItemDefinition.ToSoftObjectPath() != RequestedDefinitionPath)
					{
						return;
					}

					SpawnSourceLoadHandle.Reset();
					if (UPEPItemDefinition* LoadedDefinition = SpawnItemDefinition.Get())
					{
						CreateTreeFromDefinition(LoadedDefinition);
					}
					else
					{
						UE_LOG(LogPEPItems, Error,
							TEXT("APEPItemWorldActor '%s': failed to load spawn item definition '%s'."),
							*GetName(),
							*RequestedDefinitionPath.ToString());
					}
				}));
		return;
	}

	if (SpawnPreset.IsNull())
	{
		return;
	}

	if (const UPEPItemPreset* LoadedPreset = SpawnPreset.Get())
	{
		CreateTreeFromPreset(LoadedPreset);
		return;
	}

	const FSoftObjectPath RequestedPresetPath = SpawnPreset.ToSoftObjectPath();
	SpawnSourceLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		RequestedPresetPath,
		FStreamableDelegate::CreateWeakLambda(
			this,
			[this, RequestedPresetPath]()
			{
				if (!HasAuthority()
					|| IsActorBeingDestroyed()
					|| FindAuthorityItemInstance()
					|| SpawnPreset.ToSoftObjectPath() != RequestedPresetPath)
				{
					return;
				}

				SpawnSourceLoadHandle.Reset();
				if (const UPEPItemPreset* LoadedPreset = SpawnPreset.Get())
				{
					CreateTreeFromPreset(LoadedPreset);
				}
				else
				{
					UE_LOG(LogPEPItems, Error,
						TEXT("APEPItemWorldActor '%s': failed to load spawn preset '%s'."),
						*GetName(),
						*RequestedPresetPath.ToString());
				}
			}));
}

void APEPItemWorldActor::CreateTreeFromPreset(const UPEPItemPreset* Preset)
{
	if (!HasAuthority() || !Preset || FindAuthorityItemInstance())
	{
		return;
	}

	FPEPItemInstanceTree Tree;
	FText Error;
	if (!Preset->CreateItemTree(this, Tree, Error))
	{
		UE_LOG(LogPEPItems, Error,
			TEXT("APEPItemWorldActor '%s': spawn preset '%s' is invalid: %s"),
			*GetName(),
			*Preset->GetName(),
			*Error.ToString());
		return;
	}

	/* CreateItemTree ran ValidateDetailed on exactly this tree; a second pass would find nothing new. */
	if (!ApplyValidatedItemTree(Tree))
	{
		UE_LOG(LogPEPItems, Error,
			TEXT("APEPItemWorldActor '%s': spawn preset '%s' builds an item this actor cannot hold."),
			*GetName(),
			*Preset->GetName());
	}
}

void APEPItemWorldActor::CreateTreeFromDefinition(const UPEPItemDefinition* Definition)
{
	if (!HasAuthority() || !Definition || FindAuthorityItemInstance())
	{
		return;
	}

	/* SetItemTree validates the tree in full, which includes its structure. */
	FPEPItemInstanceTree Tree;
	if (!UPEPItemInstanceLibrary::CreateItemInstance(this, Definition, Tree.RootItem)
		|| !SetItemTree(Tree))
	{
		UE_LOG(LogPEPItems, Error,
			TEXT("APEPItemWorldActor '%s': spawn item definition '%s' failed to build a valid item."),
			*GetName(),
			*Definition->GetName());
	}
}

const UPEPItemDefinition* APEPItemWorldActor::GetAuthoringPreviewDefinition() const
{
	const UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		return nullptr;
	}

	if (const UPEPItemDefinition* Definition = SpawnItemDefinition.Get())
	{
		return Definition;
	}
#if WITH_EDITOR
	if (!SpawnItemDefinition.IsNull())
	{
		if (const UPEPItemDefinition* Definition = SpawnItemDefinition.LoadSynchronous())
		{
			return Definition;
		}
	}
#endif

	const UPEPItemPreset* Preset = SpawnPreset.Get();
#if WITH_EDITOR
	if (!Preset && !SpawnPreset.IsNull())
	{
		Preset = SpawnPreset.LoadSynchronous();
	}
#endif
	return Preset ? Preset->RootDefinition.Get() : nullptr;
}

#if WITH_EDITOR
EDataValidationResult APEPItemWorldActor::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (!SpawnPreset.IsNull() && !SpawnItemDefinition.IsNull())
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("'%s' has both Spawn Preset and Spawn Item (Direct) set; only one may be used."),
			*GetName())));
		Result = EDataValidationResult::Invalid;
	}

	const UPEPItemDefinition* Definition = SpawnItemDefinition.LoadSynchronous();
	if (const UPEPItemPreset* Preset = SpawnPreset.LoadSynchronous())
	{
		Definition = Preset->RootDefinition;
	}
	if (Definition && !SupportsDefinition(Definition))
	{
		Context.AddError(FText::FromString(TEXT("The spawn definition is incompatible with this actor storage. Use a simple actor, a container actor, or the generic world spawner.")));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

bool APEPItemWorldActor::BuildVisualNode(
	const FPEPItemInstance& Item,
	FPEPWorldPresentationNode& OutNode) const
{
	const UPEPItemDefinition* Definition = Item.GetDefinitionIfLoaded();
	if (!Definition)
	{
		return false;
	}

	const FPEPItemFragment_WorldPresentation* Fragment =
		Definition->GetDefinitionFragment<FPEPItemFragment_WorldPresentation>();
	FPEPItemWorldPresentationData Presentation;
	if (!PEPItemSettings().GetEffectiveWorldPresentation(
		Fragment,
		Presentation)
		|| !Presentation.bValid)
	{
		return false;
	}

	OutNode.StaticMesh = Presentation.StaticMesh;
	OutNode.MeshTransform = Presentation.MeshTransform;
	if (Fragment)
	{
		OutNode.Cosmetics = Fragment->DefaultCosmetics;
		if (!Fragment->DefaultMeshOverride.IsNull())
		{
			OutNode.StaticMesh = Fragment->DefaultMeshOverride;
		}
	}
	if (const FPEPItemState_WorldPresentation* State =
		Item.GetState<FPEPItemState_WorldPresentation>())
	{
		OutNode.Cosmetics = State->Cosmetics;
		if (!State->MeshOverride.IsNull())
		{
			OutNode.StaticMesh = State->MeshOverride;
		}
	}
	return true;
}

void APEPItemWorldActor::RefreshPublicPresentation()
{
	if (!HasAuthority() || IsActorBeingDestroyed())
	{
		return;
	}

	/* Read in place: the walk below needs neither the root nor any child copied. */
	const FPEPItemInstance* RootItem = FindAuthorityItemInstance();
	const UPEPItemDefinition* RootDefinition = RootItem ? RootItem->GetDefinitionIfLoaded() : nullptr;
	const FSoftObjectPath DesiredDefinitionPath = RootDefinition
		? FSoftObjectPath(RootDefinition)
		: FSoftObjectPath();
	const bool bDefinitionChanged = PublicItemDefinition.ToSoftObjectPath() != DesiredDefinitionPath;

	FPEPWorldPresentationView DesiredPresentation;
	if (RootItem)
	{
		FPEPWorldPresentationNode& RootNode = DesiredPresentation.Nodes.AddDefaulted_GetRef();
		RootNode.NodeId = 0;
		BuildVisualNode(*RootItem, RootNode);

		TSet<FPEPItemId> VisitedItemIds;
		VisitedItemIds.Add(RootItem->ItemId);
		AppendPublicAttachmentNodes(
			*RootItem,
			0,
			VisitedItemIds,
			DesiredPresentation);
	}

	const bool bPresentationChanged = !(PublicPresentation == DesiredPresentation);
	if (bDefinitionChanged || bPresentationChanged)
	{
		/* Dormant actors must be flushed before changing replicated presentation data. */
		if (NetDormancy != DORM_Awake)
		{
			FlushNetDormancy();
		}

		if (bDefinitionChanged)
		{
			PublicItemDefinition = TSoftObjectPtr<UPEPItemDefinition>(DesiredDefinitionPath);
		}
		if (bPresentationChanged)
		{
			PublicPresentation = MoveTemp(DesiredPresentation);
		}
		bLocalPresentationDirty = true;
		ForceNetUpdate();
	}

	/* Nothing to rebuild when nothing changed, which is most calls: a content move, a re-check. */
	UpdateLocalPresentation();
}

void APEPItemWorldActor::AppendPublicAttachmentNodes(
	const FPEPItemInstance& ParentItem,
	const int32 ParentNodeId,
	TSet<FPEPItemId>& VisitedItemIds,
	FPEPWorldPresentationView& OutPresentation) const
{
	const FPEPItemFragment_Container* Container =
		ParentItem.GetDefinitionFragment<FPEPItemFragment_Container>();
	if (!Container
		|| !Container->Slots.ContainsByPredicate(
			[](const FPEPContainerSlotDefinition& Slot)
			{
				return Slot.Purpose == EPEPContainerSlotPurpose::Attachment;
			}))
	{
		/* No slot of this item is ever shown, so its children need not even be visited. */
		return;
	}

	const FPEPItemFragment_WorldPresentation* ParentPresentation =
		ParentItem.GetDefinitionFragment<FPEPItemFragment_WorldPresentation>();
	const FPEPItemId ParentItemId = ParentItem.ItemId;

	/* Filtered by slot before anything is built: Content children cost a slot lookup and no copy. */
	VisitAuthorityChildren(
		ParentItemId,
		[this, Container, ParentPresentation, ParentItemId, ParentNodeId, &VisitedItemIds, &OutPresentation](
			const FPEPItemInstance& Child,
			const FPEPItemLocation& Location)
		{
			if (!Child.IsValid()
				|| Location.Type != EPEPItemLocationType::ItemContainer
				|| Location.ParentItemId != ParentItemId
				|| VisitedItemIds.Contains(Child.ItemId))
			{
				return;
			}

			const FPEPContainerSlotDefinition* Slot =
				Container->FindSlot(Location.ContainerSlotName);
			if (!Slot || Slot->Purpose != EPEPContainerSlotPurpose::Attachment)
			{
				return;
			}

			VisitedItemIds.Add(Child.ItemId);
			const int32 ChildNodeId = OutPresentation.Nodes.Num();
			FPEPWorldPresentationNode& ChildNode = OutPresentation.Nodes.AddDefaulted_GetRef();
			ChildNode.NodeId = ChildNodeId;
			ChildNode.ParentNodeId = ParentNodeId;
			BuildVisualNode(Child, ChildNode);
			ChildNode.AttachSocket = ParentPresentation
				? ParentPresentation->ResolveAttachmentSocket(Slot->SlotName)
				: Slot->SlotName;

			/* ChildNode is not used past this point: the recursion may grow the array under it. */
			AppendPublicAttachmentNodes(
				Child,
				ChildNodeId,
				VisitedItemIds,
				OutPresentation);
		});
}

const TArray<FPEPWorldPresentationNode>& APEPItemWorldActor::GetLocalPresentationNodes(
	TArray<FPEPWorldPresentationNode>& PreviewStorage) const
{
	if (!PublicPresentation.Nodes.IsEmpty())
	{
		return PublicPresentation.Nodes;
	}

	PreviewStorage.Reset();
	const UPEPItemDefinition* PreviewDefinition = GetAuthoringPreviewDefinition();
	if (!PreviewDefinition)
	{
		return PreviewStorage;
	}

	FPEPWorldPresentationNode& PreviewNode = PreviewStorage.AddDefaulted_GetRef();
	PreviewNode.NodeId = 0;
	FPEPItemWorldPresentationData Presentation;
	if (PEPItemSettings().GetEffectiveWorldPresentation(
		PreviewDefinition->GetDefinitionFragment<FPEPItemFragment_WorldPresentation>(),
		Presentation)
		&& Presentation.bValid)
	{
		PreviewNode.StaticMesh = Presentation.StaticMesh;
		PreviewNode.MeshTransform = Presentation.MeshTransform;
	}
	if (const FPEPItemFragment_WorldPresentation* Fragment =
		PreviewDefinition->GetDefinitionFragment<FPEPItemFragment_WorldPresentation>())
	{
		PreviewNode.Cosmetics = Fragment->DefaultCosmetics;
		if (!Fragment->DefaultMeshOverride.IsNull())
		{
			PreviewNode.StaticMesh = Fragment->DefaultMeshOverride;
		}
	}
	return PreviewStorage;
}

void APEPItemWorldActor::OnRep_PublicPresentation()
{
	bLocalPresentationDirty = true;
	UpdateLocalPresentation();
}

void APEPItemWorldActor::OnRep_PublicItemDefinition()
{
	RequestPublicItemDefinition();

	/*
	 * Both properties normally arrive in the same bunch, and OnRep_PublicPresentation has then
	 * already built against this definition. Only a definition that changed on its own needs a
	 * pass, for the hooks that read it.
	 */
	if (PublicItemDefinition.ToSoftObjectPath() != AppliedPresentationDefinitionPath)
	{
		bLocalPresentationDirty = true;
		UpdateLocalPresentation();
	}
}

void APEPItemWorldActor::RequestPublicItemDefinition()
{
	if (HasAuthority())
	{
		return;
	}

	const FSoftObjectPath DefinitionPath = PublicItemDefinition.ToSoftObjectPath();
	if (DefinitionPath == RequestedPublicItemDefinitionPath
		&& (PublicItemDefinitionLoadHandle.IsValid() || !DefinitionPath.IsValid()))
	{
		return;
	}

	PEPItemWorldActorPrivate::StopStreamableHandle(PublicItemDefinitionLoadHandle);
	RequestedPublicItemDefinitionPath = DefinitionPath;
	if (!DefinitionPath.IsValid())
	{
		return;
	}

	/*
	 * Requested even when the asset is already resident: the handle is what keeps it resident for
	 * as long as this actor names it, which the soft reference alone does not.
	 */
	PublicItemDefinitionLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		DefinitionPath,
		FStreamableDelegate::CreateWeakLambda(
			this,
			[this, DefinitionPath]()
			{
				if (RequestedPublicItemDefinitionPath == DefinitionPath && !PublicItemDefinition.Get())
				{
					UE_LOG(LogPEPItems, Warning,
						TEXT("APEPItemWorldActor '%s': replicated item definition '%s' could not be loaded."),
						*GetName(),
						*DefinitionPath.ToString());
				}
			}));
}

bool APEPItemWorldActor::ShouldBuildCosmetics() const
{
	return GetNetMode() != NM_DedicatedServer;
}

void APEPItemWorldActor::ApplyDefinitionVisual()
{
	if (!RootStaticMeshComponent)
	{
		return;
	}

	bLocalPresentationDirty = false;
	AppliedPresentationDefinitionPath = PublicItemDefinition.ToSoftObjectPath();
	++PresentationGeneration;
	CancelPresentationLoad();
	RequestPresentationMeshes(PresentationGeneration);
}

void APEPItemWorldActor::UpdateLocalPresentation()
{
	if (bLocalPresentationDirty)
	{
		ApplyDefinitionVisual();
	}
}

void APEPItemWorldActor::CancelPresentationLoad()
{
	PEPItemWorldActorPrivate::StopStreamableHandle(VisualLoadHandle);
}

void APEPItemWorldActor::RequestPresentationMeshes(const uint32 Generation)
{
	if (Generation != PresentationGeneration)
	{
		return;
	}

	TArray<FPEPWorldPresentationNode> PreviewNodes;
	const TArray<FPEPWorldPresentationNode>& Nodes = GetLocalPresentationNodes(PreviewNodes);
	const bool bCosmetics = ShouldBuildCosmetics();

	TArray<FSoftObjectPath> MissingAssetPaths;
	const auto AddIfMissing =
		[&MissingAssetPaths](const FSoftObjectPath& Path, const bool bResident)
		{
			if (Path.IsValid() && !bResident)
			{
				MissingAssetPaths.AddUnique(Path);
			}
		};

	for (const FPEPWorldPresentationNode& Node : Nodes)
	{
		/* Collision is all a dedicated server needs, and only the root carries any. */
		if (!bCosmetics && Node.ParentNodeId != INDEX_NONE)
		{
			continue;
		}

		AddIfMissing(Node.StaticMesh.ToSoftObjectPath(), Node.StaticMesh.Get() != nullptr);
		if (!bCosmetics)
		{
			continue;
		}

		for (const FPEPWorldMaterialOverride& Override : Node.Cosmetics.MaterialOverrides)
		{
			AddIfMissing(Override.Material.ToSoftObjectPath(), Override.Material.Get() != nullptr);
		}
		for (const FPEPWorldTextureParameter& Parameter : Node.Cosmetics.TextureParameters)
		{
			AddIfMissing(Parameter.Value.ToSoftObjectPath(), Parameter.Value.Get() != nullptr);
		}
	}

	/*
	 * Off the authority the node hooks may read GetItemDefinition(), so the build waits for it in
	 * the same batch rather than running once without it and again when it lands.
	 */
	if (bCosmetics && !HasAuthority())
	{
		AddIfMissing(PublicItemDefinition.ToSoftObjectPath(), PublicItemDefinition.Get() != nullptr);
	}

	if (MissingAssetPaths.IsEmpty())
	{
		BuildPresentationComponents(Generation);
		return;
	}

	VisualLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		MissingAssetPaths,
		FStreamableDelegate::CreateWeakLambda(
			this,
			[this, Generation]()
			{
				if (Generation != PresentationGeneration)
				{
					return;
				}
				VisualLoadHandle.Reset();
				BuildPresentationComponents(Generation);
			}));
	if (!VisualLoadHandle.IsValid())
	{
		BuildPresentationComponents(Generation);
	}
}

void APEPItemWorldActor::BuildPresentationComponents(const uint32 Generation)
{
	if (Generation != PresentationGeneration || !RootStaticMeshComponent)
	{
		return;
	}

	/*
	 * Copied, unlike in RequestPresentationMeshes: the Blueprint hook below runs inside this loop,
	 * and anything it does to the presentation must not move the array being walked.
	 */
	TArray<FPEPWorldPresentationNode> PreviewNodes;
	const TArray<FPEPWorldPresentationNode> Nodes = GetLocalPresentationNodes(PreviewNodes);
	const bool bCosmetics = ShouldBuildCosmetics();

	/* The previous instances stay referenced by the components still using them until replaced. */
	DynamicMaterialInstances.Reset();

	TMap<int32, UStaticMeshComponent*> ComponentsByNodeId;
	TSet<int32> UsedNodeIds;
	bool bRootNodeApplied = false;
	for (const FPEPWorldPresentationNode& Node : Nodes)
	{
		UStaticMeshComponent* VisualComponent = nullptr;
		if (Node.ParentNodeId == INDEX_NONE)
		{
			VisualComponent = RootStaticMeshComponent;
			bRootNodeApplied = true;
		}
		else
		{
			/* Attachments carry no collision; a dedicated server has no use for them. */
			if (!bCosmetics)
			{
				continue;
			}

			UStaticMeshComponent* const* ParentVisualComponent =
				ComponentsByNodeId.Find(Node.ParentNodeId);
			if (!ParentVisualComponent || !*ParentVisualComponent)
			{
				continue;
			}

			if (TObjectPtr<UStaticMeshComponent>* Existing = AttachmentMeshComponents.Find(Node.NodeId);
				Existing && IsValid(*Existing))
			{
				VisualComponent = *Existing;
			}
			else
			{
				VisualComponent = NewObject<UStaticMeshComponent>(
					this,
					MakeUniqueObjectName(
						this,
						UStaticMeshComponent::StaticClass(),
						TEXT("WorldAttachmentMesh")));
				VisualComponent->SetMobility(EComponentMobility::Movable);
				VisualComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				VisualComponent->SetGenerateOverlapEvents(false);
				VisualComponent->SetCanEverAffectNavigation(false);
				VisualComponent->RegisterComponent();
				AttachmentMeshComponents.Add(Node.NodeId, VisualComponent);
			}
			if (VisualComponent->GetAttachParent() != *ParentVisualComponent
				|| VisualComponent->GetAttachSocketName() != Node.AttachSocket)
			{
				VisualComponent->AttachToComponent(
					*ParentVisualComponent,
					FAttachmentTransformRules::SnapToTargetNotIncludingScale,
					Node.AttachSocket);
			}
		}
		UsedNodeIds.Add(Node.NodeId);

		ComponentsByNodeId.Add(Node.NodeId, VisualComponent);

		/*
		 * Not on the root. It is the actor's root component, so a relative transform on it is the
		 * actor's own placement — writing the node's offset there teleported every dropped item to
		 * wherever the mesh transform pointed, usually the world origin. An offset from oneself is
		 * meaningless anyway: where a free-standing item stands is its actor transform. Attachments
		 * own components of their own, and that is where a mesh offset means something.
		 */
		if (VisualComponent != RootStaticMeshComponent)
		{
			VisualComponent->SetRelativeTransform(Node.MeshTransform);
		}

		/*
		 * Compared first, and never cleared in between: swapping the mesh rebuilds render and
		 * physics state, and the root used to be emptied and refilled with the same mesh on every
		 * refresh — a simulating body recreated for nothing.
		 */
		UStaticMesh* const DesiredMesh = Node.StaticMesh.Get();
		if (VisualComponent->GetStaticMesh() != DesiredMesh)
		{
			VisualComponent->SetStaticMesh(DesiredMesh);
		}

		if (!bCosmetics)
		{
			continue;
		}

		VisualComponent->EmptyOverrideMaterials();
		for (const FPEPWorldMaterialOverride& Override : Node.Cosmetics.MaterialOverrides)
		{
			if (UMaterialInterface* Material = Override.Material.Get();
				Material && Override.MaterialIndex >= 0)
			{
				VisualComponent->SetMaterial(Override.MaterialIndex, Material);
			}
		}

		TSet<int32> DynamicMaterialIndices;
		const auto AddParameterTarget =
			[VisualComponent, &DynamicMaterialIndices](const int32 MaterialIndex)
			{
				if (MaterialIndex == INDEX_NONE)
				{
					for (int32 Index = 0; Index < VisualComponent->GetNumMaterials(); ++Index)
					{
						DynamicMaterialIndices.Add(Index);
					}
				}
				else if (MaterialIndex >= 0 && MaterialIndex < VisualComponent->GetNumMaterials())
				{
					DynamicMaterialIndices.Add(MaterialIndex);
				}
			};
		for (const FPEPWorldScalarParameter& Parameter : Node.Cosmetics.ScalarParameters)
		{
			AddParameterTarget(Parameter.MaterialIndex);
		}
		for (const FPEPWorldVectorParameter& Parameter : Node.Cosmetics.VectorParameters)
		{
			AddParameterTarget(Parameter.MaterialIndex);
		}
		for (const FPEPWorldTextureParameter& Parameter : Node.Cosmetics.TextureParameters)
		{
			AddParameterTarget(Parameter.MaterialIndex);
		}

		for (const int32 MaterialIndex : DynamicMaterialIndices)
		{
			UMaterialInstanceDynamic* DynamicMaterial =
				VisualComponent->CreateDynamicMaterialInstance(MaterialIndex);
			if (!DynamicMaterial)
			{
				continue;
			}
			DynamicMaterialInstances.Add(DynamicMaterial);
			for (const FPEPWorldScalarParameter& Parameter : Node.Cosmetics.ScalarParameters)
			{
				if (!Parameter.ParameterName.IsNone()
					&& (Parameter.MaterialIndex == INDEX_NONE
						|| Parameter.MaterialIndex == MaterialIndex))
				{
					DynamicMaterial->SetScalarParameterValue(Parameter.ParameterName, Parameter.Value);
				}
			}
			for (const FPEPWorldVectorParameter& Parameter : Node.Cosmetics.VectorParameters)
			{
				if (!Parameter.ParameterName.IsNone()
					&& (Parameter.MaterialIndex == INDEX_NONE
						|| Parameter.MaterialIndex == MaterialIndex))
				{
					DynamicMaterial->SetVectorParameterValue(Parameter.ParameterName, Parameter.Value);
				}
			}
			for (const FPEPWorldTextureParameter& Parameter : Node.Cosmetics.TextureParameters)
			{
				if ((Parameter.MaterialIndex == INDEX_NONE
						|| Parameter.MaterialIndex == MaterialIndex)
					&& !Parameter.ParameterName.IsNone())
				{
					DynamicMaterial->SetTextureParameterValue(
						Parameter.ParameterName,
						Parameter.Value.Get());
				}
			}
		}

		ReceivePresentationNodeApplied(Node, VisualComponent);

		/* The hook may have asked for a rebuild of its own; that one supersedes this pass. */
		if (Generation != PresentationGeneration)
		{
			return;
		}
	}

	/* No root node: nothing to show, and so nothing to collide with either. */
	if (!bRootNodeApplied && RootStaticMeshComponent->GetStaticMesh())
	{
		RootStaticMeshComponent->EmptyOverrideMaterials();
		RootStaticMeshComponent->SetStaticMesh(nullptr);
	}

	TArray<int32> UnusedNodeIds;
	for (const TPair<int32, TObjectPtr<UStaticMeshComponent>>& Pair : AttachmentMeshComponents)
	{
		if (!UsedNodeIds.Contains(Pair.Key))
		{
			UnusedNodeIds.Add(Pair.Key);
		}
	}
	for (const int32 NodeId : UnusedNodeIds)
	{
		if (TObjectPtr<UStaticMeshComponent>* Component = AttachmentMeshComponents.Find(NodeId))
		{
			if (IsValid(*Component))
			{
				(*Component)->DestroyComponent();
			}
			AttachmentMeshComponents.Remove(NodeId);
		}
	}
}
