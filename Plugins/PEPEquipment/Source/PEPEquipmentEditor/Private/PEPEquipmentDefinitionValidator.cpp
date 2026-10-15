#include "PEPEquipmentDefinitionValidator.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Equipment/PEPEquipmentPresentationFragment.h"
#include "Item/Fragments/PEPItemFragment_Container.h"
#include "Item/PEPItemDefinition.h"

namespace
{
	bool DoesSocketExist(
		const FPEPEquipmentPresentationFragment& Presentation,
		const FName SocketName)
	{
		if (SocketName.IsNone())
		{
			return false;
		}
		if (const TSoftObjectPtr<UStaticMesh>* SoftMesh = Presentation.GetStaticMeshVisual())
		{
			const UStaticMesh* Mesh = SoftMesh->LoadSynchronous();
			return Mesh && Mesh->FindSocket(SocketName);
		}
		if (const TSoftObjectPtr<USkeletalMesh>* SoftMesh = Presentation.GetSkeletalMeshVisual())
		{
			const USkeletalMesh* Mesh = SoftMesh->LoadSynchronous();
			return Mesh && (Mesh->FindSocket(SocketName)
				|| Mesh->GetRefSkeleton().FindBoneIndex(SocketName) != INDEX_NONE);
		}
		return false;
	}
}

bool UPEPEquipmentDefinitionValidator::CanValidateAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InObject,
	FDataValidationContext& InContext) const
{
	const UPEPItemDefinition* Definition = Cast<UPEPItemDefinition>(InObject);
	return Definition
		&& Definition->HasDefinitionFragment<FPEPEquipmentPresentationFragment>();
}

EDataValidationResult UPEPEquipmentDefinitionValidator::ValidateLoadedAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InAsset,
	FDataValidationContext& Context)
{
	const UPEPItemDefinition* Definition = CastChecked<UPEPItemDefinition>(InAsset);
	const FPEPEquipmentPresentationFragment* Presentation =
		Definition->GetDefinitionFragment<FPEPEquipmentPresentationFragment>();
	const FPEPItemFragment_Container* Container =
		Definition->GetDefinitionFragment<FPEPItemFragment_Container>();
	bool bFailed = false;

	for (const FPEPEquipmentAttachmentSocketOverride& Override :
		Presentation->AttachmentSocketOverrides)
	{
		const FPEPContainerSlotDefinition* Slot = Container
			? Container->FindSlot(Override.SlotName)
			: nullptr;
		if (!Slot || Slot->Purpose != EPEPContainerSlotPurpose::Attachment)
		{
			AssetFails(InAsset, FText::FromString(FString::Printf(
				TEXT("Socket override '%s' does not target an Attachment container slot."),
				*Override.SlotName.ToString())));
			bFailed = true;
		}
	}

	if (Container && !Presentation->IsExplicitlyInvisible())
	{
		for (const FPEPContainerSlotDefinition& Slot : Container->Slots)
		{
			if (Slot.Purpose != EPEPContainerSlotPurpose::Attachment)
			{
				continue;
			}
			const FName SocketName = Presentation->ResolveAttachmentSocket(Slot.SlotName);
			if (!DoesSocketExist(*Presentation, SocketName))
			{
				AssetFails(InAsset, FText::FromString(FString::Printf(
					TEXT("Attachment slot '%s' resolves to missing socket or bone '%s'."),
					*Slot.SlotName.ToString(),
					*SocketName.ToString())));
				bFailed = true;
			}
		}
	}

	if (!bFailed)
	{
		AssetPasses(InAsset);
	}
	return bFailed ? EDataValidationResult::Invalid : EDataValidationResult::Valid;
}
