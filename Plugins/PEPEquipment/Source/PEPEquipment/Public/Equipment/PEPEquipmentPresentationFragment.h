#pragma once

#include "CoreMinimal.h"
#include "Item/PEPItemFragment.h"
#include "StructUtils/InstancedStruct.h"

#include "PEPEquipmentPresentationFragment.generated.h"

class APEPEquippedPresentationActor;
class USkeletalMesh;
class UStaticMesh;

/** Controls whether a skeletal equipment visual evaluates independently or follows its body mesh. */
UENUM(BlueprintType)
enum class EPEPEquipmentPoseFollowMode : uint8
{
	/** Keep the normal socket-attached presentation behavior. */
	None,
	/** Share the body's bone transforms through Leader Pose. Requires the same skeleton. */
	LeaderPose
};

/** Closed family used by EquipmentPresentation.Visual. */
USTRUCT(BlueprintType)
struct PEPEQUIPMENT_API FPEPEquipmentVisual
{
	GENERATED_BODY()
	virtual ~FPEPEquipmentVisual() = default;
};

/** Hidden common base used to restrict Actor.MeshVisual to mesh-backed visual kinds. */
USTRUCT(BlueprintType, meta=(Hidden))
struct PEPEQUIPMENT_API FPEPEquipmentMeshVisual : public FPEPEquipmentVisual
{
	GENERATED_BODY()
};

USTRUCT(BlueprintType, meta=(DisplayName="No Visual"))
struct PEPEQUIPMENT_API FPEPNoEquipmentVisual : public FPEPEquipmentVisual
{
	GENERATED_BODY()
};

USTRUCT(BlueprintType, meta=(DisplayName="Static Mesh"))
struct PEPEQUIPMENT_API FPEPStaticMeshEquipmentVisual : public FPEPEquipmentMeshVisual
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	TSoftObjectPtr<UStaticMesh> Mesh;
};

USTRUCT(BlueprintType, meta=(DisplayName="Skeletal Mesh"))
struct PEPEQUIPMENT_API FPEPSkeletalMeshEquipmentVisual : public FPEPEquipmentMeshVisual
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	TSoftObjectPtr<USkeletalMesh> Mesh;
};

/**
 * A whole actor, for an item whose look is more than a mesh: a holster that opens, a lamp that
 * lights, anything wanting components and a Blueprint graph of its own.
 *
 * Constrained to APEPEquippedPresentationActor because equipment drives it — it hands over the
 * public state, asks it where attachments hang, and destroys it when the item leaves. A plain
 * AActor could not answer any of that.
 */
USTRUCT(BlueprintType, meta=(DisplayName="Actor"))
struct PEPEQUIPMENT_API FPEPActorEquipmentVisual : public FPEPEquipmentVisual
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	TSoftClassPtr<APEPEquippedPresentationActor> ActorClass;

	/** Optional mesh rendered by the actor's built-in presentation component. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Equipment|Presentation",
		meta=(BaseStruct="/Script/PEPEquipment.PEPEquipmentMeshVisual", ExcludeBaseStruct))
	FInstancedStruct MeshVisual;
};

/** Optional exception to the default convention where an attachment slot uses a same-named socket. */
USTRUCT(BlueprintType)
struct PEPEQUIPMENT_API FPEPEquipmentAttachmentSocketOverride
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	FName SlotName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	FName SocketName;
};

/** Static, public presentation data. Runtime item state and container contents never enter it. */
USTRUCT(BlueprintType)
struct PEPEQUIPMENT_API FPEPEquipmentPresentationFragment : public FPEPItemFragment
{
	GENERATED_BODY()

	/** Exactly one visual kind. No Visual explicitly represents an intentionally invisible item. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Equipment|Presentation",
		meta=(BaseStruct="/Script/PEPEquipment.PEPEquipmentVisual", ExcludeBaseStruct))
	FInstancedStruct Visual;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	FTransform MeshTransform = FTransform::Identity;

	/**
	 * Optional pose sharing for this item. Leader Pose is intended for root equipment visuals
	 * skinned to the same skeleton as the character; MeshTransform is then relative to the body mesh.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	EPEPEquipmentPoseFollowMode PoseFollowMode = EPEPEquipmentPoseFollowMode::None;

	/**
	 * Tags applied to the mesh component that renders this attached presentation.
	 *
	 * These identify the runtime component to generic systems such as melee tracing,
	 * interaction, or cosmetic effects without coupling Equipment to those systems.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	TArray<FName> MeshComponentTags;

	/** Attachment slots use their slot name as socket name unless overridden here. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Equipment|Presentation")
	TArray<FPEPEquipmentAttachmentSocketOverride> AttachmentSocketOverrides;

	const TSoftObjectPtr<UStaticMesh>* GetStaticMeshVisual() const;
	const TSoftObjectPtr<USkeletalMesh>* GetSkeletalMeshVisual() const;

	/** Null unless the visual is an Actor. The attached path spawns this instead of the default. */
	const TSoftClassPtr<APEPEquippedPresentationActor>* GetActorClassVisual() const;

	/** Whether anything at all is described, so a fragment with nothing chosen fails loudly. */
	bool HasVisual() const;

	bool IsExplicitlyInvisible() const;

	FName ResolveAttachmentSocket(const FName SlotName) const
	{
		const FPEPEquipmentAttachmentSocketOverride* Override =
			AttachmentSocketOverrides.FindByPredicate(
				[SlotName](const FPEPEquipmentAttachmentSocketOverride& Candidate)
				{
					return Candidate.SlotName == SlotName;
				});
		return Override && !Override->SocketName.IsNone() ? Override->SocketName : SlotName;
	}

#if WITH_EDITOR
	virtual EDataValidationResult ValidateFragmentData(
		FDataValidationContext& Context,
		int32 FragmentIndex) const override;
#endif
};
