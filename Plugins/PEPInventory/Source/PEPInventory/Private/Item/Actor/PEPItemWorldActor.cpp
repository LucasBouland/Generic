#include "Item/Actor/PEPItemWorldActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
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
	ApplyDefinitionVisual();
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

		if (!GetAuthorityItemInstance().IsValid())
		{
			InitializeFromSpawnSource();
		}
		RefreshPublicPresentation();
	}
	else
	{
		ApplyDefinitionVisual();
	}
}

void APEPItemWorldActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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

UPEPItemDefinition* APEPItemWorldActor::GetItemDefinition() const
{
	if (HasAuthority())
	{
		return const_cast<UPEPItemDefinition*>(GetAuthorityItemInstance().GetDefinitionIfLoaded());
	}

	return PublicItemDefinition.Get();
}

FPEPItemInstance APEPItemWorldActor::GetAuthorityItemInstance() const
{
	return FPEPItemInstance();
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

	if (SpawnSourceLoadHandle.IsValid())
	{
		SpawnSourceLoadHandle->CancelHandle();
		SpawnSourceLoadHandle.Reset();
	}

	SpawnPreset = InPreset;
	SpawnItemDefinition = nullptr;
	ResetAuthorityStorage();
	RefreshPublicPresentation();
	InitializeFromSpawnSource();
	ForceNetUpdate();
}

void APEPItemWorldActor::SetSpawnItemDefinition(UPEPItemDefinition* InDefinition)
{
	if (!HasAuthority())
	{
		return;
	}

	if (SpawnSourceLoadHandle.IsValid())
	{
		SpawnSourceLoadHandle->CancelHandle();
		SpawnSourceLoadHandle.Reset();
	}

	SpawnItemDefinition = InDefinition;
	SpawnPreset = nullptr;
	ResetAuthorityStorage();
	RefreshPublicPresentation();
	InitializeFromSpawnSource();
	ForceNetUpdate();
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
		ForceNetUpdate();
		return;
	}
	FPEPItemInstanceTree Tree;
	Tree.RootItem = InInstance;
	SetItemTree(Tree);
}

bool APEPItemWorldActor::SetItemTree(const FPEPItemInstanceTree& InTree)
{
	FPEPItemInstanceTreeValidationResult Validation;
	if (!HasAuthority() || !InTree.ValidateDetailed(Validation)
		|| !SupportsDefinition(InTree.RootItem.GetDefinitionIfLoaded()))
	{
		return false;
	}

	if (!StoreAuthorityTree(InTree))
	{
		return false;
	}
	RefreshPublicPresentation();
	ForceNetUpdate();
	return true;
}

void APEPItemWorldActor::InitializeFromSpawnSource()
{
	if (!HasAuthority() || GetAuthorityItemInstance().IsValid())
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
						|| GetAuthorityItemInstance().IsValid()
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
					|| GetAuthorityItemInstance().IsValid()
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
	if (!HasAuthority() || !Preset || GetAuthorityItemInstance().IsValid())
	{
		return;
	}

	FPEPItemInstanceTree Tree;
	FText Error;
	if (!Preset->CreateItemTree(this, Tree, Error) || !SetItemTree(Tree))
	{
		UE_LOG(LogPEPItems, Error,
			TEXT("APEPItemWorldActor '%s': spawn preset '%s' is invalid: %s"),
			*GetName(),
			*Preset->GetName(),
			*Error.ToString());
	}
}

void APEPItemWorldActor::CreateTreeFromDefinition(const UPEPItemDefinition* Definition)
{
	if (!HasAuthority() || !Definition || GetAuthorityItemInstance().IsValid())
	{
		return;
	}

	FPEPItemInstanceTree Tree;
	if (!UPEPItemInstanceLibrary::CreateItemInstance(this, Definition, Tree.RootItem)
		|| !Tree.IsStructurallyValid()
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
	if (!HasAuthority())
	{
		return;
	}

	const FPEPItemInstance RootItem = GetAuthorityItemInstance();
	const TSoftObjectPtr<UPEPItemDefinition> DesiredDefinition(
		const_cast<UPEPItemDefinition*>(RootItem.GetDefinitionIfLoaded()));
	const bool bDefinitionChanged = PublicItemDefinition != DesiredDefinition;

	FPEPWorldPresentationView DesiredPresentation;
	if (RootItem.IsValid())
	{
		FPEPWorldPresentationNode& RootNode = DesiredPresentation.Nodes.AddDefaulted_GetRef();
		RootNode.NodeId = 0;
		BuildVisualNode(RootItem, RootNode);

		TSet<FPEPItemId> VisitedItemIds;
		VisitedItemIds.Add(RootItem.ItemId);
		AppendPublicAttachmentNodes(
			RootItem,
			RootNode.NodeId,
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
			PublicItemDefinition = DesiredDefinition;
		}
		if (bPresentationChanged)
		{
			PublicPresentation = MoveTemp(DesiredPresentation);
		}
		ForceNetUpdate();
	}
	ApplyDefinitionVisual();
}

void APEPItemWorldActor::AppendPublicAttachmentNodes(
	const FPEPItemInstance& ParentItem,
	const int32 ParentNodeId,
	TSet<FPEPItemId>& VisitedItemIds,
	FPEPWorldPresentationView& OutPresentation) const
{
	const FPEPItemFragment_Container* Container =
		ParentItem.GetDefinitionFragment<FPEPItemFragment_Container>();
	if (!Container)
	{
		return;
	}

	const FPEPItemFragment_WorldPresentation* ParentPresentation =
		ParentItem.GetDefinitionFragment<FPEPItemFragment_WorldPresentation>();
	TArray<FPEPItemInstanceTreeEntry> ChildEntries;
	GetAuthorityChildren(ParentItem.ItemId, ChildEntries);
	for (const FPEPItemInstanceTreeEntry& Child : ChildEntries)
	{
		if (!Child.Item.IsValid()
			|| VisitedItemIds.Contains(Child.Item.ItemId)
			|| Child.Location.Type != EPEPItemLocationType::ItemContainer
			|| Child.Location.ParentItemId != ParentItem.ItemId)
		{
			continue;
		}

		const FPEPContainerSlotDefinition* Slot =
			Container->FindSlot(Child.Location.ContainerSlotName);
		if (!Slot || Slot->Purpose != EPEPContainerSlotPurpose::Attachment)
		{
			continue;
		}

		VisitedItemIds.Add(Child.Item.ItemId);
		FPEPWorldPresentationNode& ChildNode =
			OutPresentation.Nodes.AddDefaulted_GetRef();
		ChildNode.NodeId = OutPresentation.Nodes.Num() - 1;
		ChildNode.ParentNodeId = ParentNodeId;
		BuildVisualNode(Child.Item, ChildNode);
		ChildNode.AttachSocket = ParentPresentation
			? ParentPresentation->ResolveAttachmentSocket(Slot->SlotName)
			: Slot->SlotName;

		AppendPublicAttachmentNodes(
			Child.Item,
			ChildNode.NodeId,
			VisitedItemIds,
			OutPresentation);
	}
}

void APEPItemWorldActor::GetLocalPresentationNodes(
	TArray<FPEPWorldPresentationNode>& OutNodes) const
{
	OutNodes = PublicPresentation.Nodes;
	if (!OutNodes.IsEmpty())
	{
		return;
	}

	const UPEPItemDefinition* PreviewDefinition = GetAuthoringPreviewDefinition();
	if (!PreviewDefinition)
	{
		return;
	}

	FPEPWorldPresentationNode& PreviewNode = OutNodes.AddDefaulted_GetRef();
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
}

void APEPItemWorldActor::OnRep_PublicPresentation()
{
	ApplyDefinitionVisual();
}

void APEPItemWorldActor::OnRep_PublicItemDefinition()
{
}

void APEPItemWorldActor::ApplyDefinitionVisual()
{
	if (!RootStaticMeshComponent)
	{
		return;
	}

	++PresentationGeneration;
	ResetLocalPresentation();
	RequestPresentationMeshes(PresentationGeneration);
}

void APEPItemWorldActor::RequestPresentationMeshes(const uint32 Generation)
{
	if (Generation != PresentationGeneration)
	{
		return;
	}

	TArray<FPEPWorldPresentationNode> Nodes;
	GetLocalPresentationNodes(Nodes);
	TArray<FSoftObjectPath> MissingAssetPaths;
	for (const FPEPWorldPresentationNode& Node : Nodes)
	{
		const FSoftObjectPath MeshPath = Node.StaticMesh.ToSoftObjectPath();
		if (MeshPath.IsValid() && !Node.StaticMesh.Get())
		{
			MissingAssetPaths.AddUnique(MeshPath);
		}
		for (const FPEPWorldMaterialOverride& Override : Node.Cosmetics.MaterialOverrides)
		{
			const FSoftObjectPath MaterialPath = Override.Material.ToSoftObjectPath();
			if (MaterialPath.IsValid() && !Override.Material.Get())
			{
				MissingAssetPaths.AddUnique(MaterialPath);
			}
		}
		for (const FPEPWorldTextureParameter& Parameter : Node.Cosmetics.TextureParameters)
		{
			const FSoftObjectPath TexturePath = Parameter.Value.ToSoftObjectPath();
			if (TexturePath.IsValid() && !Parameter.Value.Get())
			{
				MissingAssetPaths.AddUnique(TexturePath);
			}
		}
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

	TArray<FPEPWorldPresentationNode> Nodes;
	GetLocalPresentationNodes(Nodes);
	TMap<int32, UStaticMeshComponent*> ComponentsByNodeId;
	TSet<int32> UsedNodeIds;
	for (const FPEPWorldPresentationNode& Node : Nodes)
	{
		UStaticMeshComponent* VisualComponent = nullptr;
		if (Node.ParentNodeId == INDEX_NONE)
		{
			VisualComponent = RootStaticMeshComponent;
		}
		else
		{
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

		VisualComponent->SetStaticMesh(Node.StaticMesh.Get());

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

void APEPItemWorldActor::ResetLocalPresentation()
{
	if (VisualLoadHandle.IsValid())
	{
		VisualLoadHandle->CancelHandle();
		VisualLoadHandle.Reset();
	}
	DynamicMaterialInstances.Reset();
	RootStaticMeshComponent->EmptyOverrideMaterials();
	for (TPair<int32, TObjectPtr<UStaticMeshComponent>>& Pair : AttachmentMeshComponents)
	{
		if (Pair.Value)
		{
			Pair.Value->EmptyOverrideMaterials();
			Pair.Value->SetStaticMesh(nullptr);
		}
	}
	RootStaticMeshComponent->SetStaticMesh(nullptr);

	/*
	 * The root's transform is deliberately left alone. Resetting it here moved the actor to the
	 * origin, and from OnConstruction — which runs after FinishSpawning has placed it — so a
	 * spawned item landed nowhere near where it was dropped.
	 */
}
