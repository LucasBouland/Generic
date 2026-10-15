#include "Equipment/PEPEquipmentPresentationFragment.h"

#include "Equipment/PEPEquippedPresentationActor.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const TSoftObjectPtr<UStaticMesh>*
FPEPEquipmentPresentationFragment::GetStaticMeshVisual() const
{
	if (const FPEPStaticMeshEquipmentVisual* Typed =
		Visual.GetPtr<FPEPStaticMeshEquipmentVisual>())
	{
		return &Typed->Mesh;
	}
	if (const FPEPActorEquipmentVisual* Actor = Visual.GetPtr<FPEPActorEquipmentVisual>())
	{
		if (const FPEPStaticMeshEquipmentVisual* Typed =
			Actor->MeshVisual.GetPtr<FPEPStaticMeshEquipmentVisual>())
		{
			return &Typed->Mesh;
		}
	}
	return nullptr;
}

const TSoftObjectPtr<USkeletalMesh>*
FPEPEquipmentPresentationFragment::GetSkeletalMeshVisual() const
{
	if (const FPEPSkeletalMeshEquipmentVisual* Typed =
		Visual.GetPtr<FPEPSkeletalMeshEquipmentVisual>())
	{
		return &Typed->Mesh;
	}
	if (const FPEPActorEquipmentVisual* Actor = Visual.GetPtr<FPEPActorEquipmentVisual>())
	{
		if (const FPEPSkeletalMeshEquipmentVisual* Typed =
			Actor->MeshVisual.GetPtr<FPEPSkeletalMeshEquipmentVisual>())
		{
			return &Typed->Mesh;
		}
	}
	return nullptr;
}

const TSoftClassPtr<APEPEquippedPresentationActor>*
FPEPEquipmentPresentationFragment::GetActorClassVisual() const
{
	if (const FPEPActorEquipmentVisual* Typed = Visual.GetPtr<FPEPActorEquipmentVisual>())
	{
		return &Typed->ActorClass;
	}
	return nullptr;
}

bool FPEPEquipmentPresentationFragment::HasVisual() const
{
	return GetStaticMeshVisual() != nullptr
		|| GetSkeletalMeshVisual() != nullptr
		|| GetActorClassVisual() != nullptr;
}

bool FPEPEquipmentPresentationFragment::IsExplicitlyInvisible() const
{
	return Visual.GetScriptStruct() == FPEPNoEquipmentVisual::StaticStruct();
}

#if WITH_EDITOR
EDataValidationResult FPEPEquipmentPresentationFragment::ValidateFragmentData(
	FDataValidationContext& Context,
	const int32 FragmentIndex) const
{
	EDataValidationResult Result =
		FPEPItemFragment::ValidateFragmentData(Context, FragmentIndex);
	const bool bHasTypedVisual = Visual.IsValid();

	if (!bHasTypedVisual)
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("DefinitionFragments[%d] EquipmentPresentation must select a typed Visual."),
			FragmentIndex)));
		Result = EDataValidationResult::Invalid;
	}
	if (const TSoftObjectPtr<UStaticMesh>* Mesh = GetStaticMeshVisual(); Mesh && Mesh->IsNull())
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("DefinitionFragments[%d] Static Mesh visual has no mesh."), FragmentIndex)));
		Result = EDataValidationResult::Invalid;
	}
	if (const TSoftClassPtr<APEPEquippedPresentationActor>* ActorClass = GetActorClassVisual();
		ActorClass && ActorClass->IsNull())
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("DefinitionFragments[%d] Actor visual has no actor class."), FragmentIndex)));
		Result = EDataValidationResult::Invalid;
	}
	if (const FPEPActorEquipmentVisual* Actor = Visual.GetPtr<FPEPActorEquipmentVisual>();
		Actor && Actor->MeshVisual.IsValid()
		&& !Actor->MeshVisual.GetPtr<FPEPStaticMeshEquipmentVisual>()
		&& !Actor->MeshVisual.GetPtr<FPEPSkeletalMeshEquipmentVisual>())
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("DefinitionFragments[%d] Actor Mesh Visual must be Static Mesh or Skeletal Mesh."),
			FragmentIndex)));
		Result = EDataValidationResult::Invalid;
	}
	if (const TSoftObjectPtr<USkeletalMesh>* Mesh = GetSkeletalMeshVisual(); Mesh && Mesh->IsNull())
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("DefinitionFragments[%d] Skeletal Mesh visual has no mesh."), FragmentIndex)));
		Result = EDataValidationResult::Invalid;
	}
	if (PoseFollowMode == EPEPEquipmentPoseFollowMode::LeaderPose
		&& !Visual.GetPtr<FPEPSkeletalMeshEquipmentVisual>())
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("DefinitionFragments[%d] Leader Pose requires a direct Skeletal Mesh visual."),
			FragmentIndex)));
		Result = EDataValidationResult::Invalid;
	}

	TSet<FName> SeenSlots;
	for (const FPEPEquipmentAttachmentSocketOverride& Override : AttachmentSocketOverrides)
	{
		if (Override.SlotName.IsNone() || Override.SocketName.IsNone())
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("DefinitionFragments[%d] contains an attachment socket override with an empty name."),
				FragmentIndex)));
			Result = EDataValidationResult::Invalid;
		}
		else if (SeenSlots.Contains(Override.SlotName))
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("DefinitionFragments[%d] overrides attachment slot '%s' more than once."),
				FragmentIndex,
				*Override.SlotName.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		SeenSlots.Add(Override.SlotName);
	}

	TSet<FName> SeenMeshTags;
	for (const FName Tag : MeshComponentTags)
	{
		if (Tag.IsNone())
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("DefinitionFragments[%d] contains an empty presentation mesh component tag."),
				FragmentIndex)));
			Result = EDataValidationResult::Invalid;
		}
		else if (SeenMeshTags.Contains(Tag))
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("DefinitionFragments[%d] declares presentation mesh component tag '%s' more than once."),
				FragmentIndex,
				*Tag.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		SeenMeshTags.Add(Tag);
	}
	return Result;
}
#endif
