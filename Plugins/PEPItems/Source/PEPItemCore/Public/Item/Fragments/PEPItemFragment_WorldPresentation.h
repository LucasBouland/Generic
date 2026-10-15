#pragma once
#include "CoreMinimal.h"
#include "Item/PEPItemFragment.h"
#include "Item/PEPItemStateFragment.h"
#include "Item/PEPItemWorldPresentationTypes.h"
#include "PEPItemFragment_WorldPresentation.generated.h"
class UStaticMesh;

/** Optional exception to the convention where an Attachment slot uses a same-named mesh socket. */
USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPWorldAttachmentSocketOverride
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|World Presentation")
	FName SlotName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|World Presentation")
	FName SocketName;
};

/**
 * Persistent, authority-owned selection of cosmetic values. The WorldActor copies only the
 * sanitized fields into its public projection; the complete item instance remains private.
 * EditAnywhere so presets can author it; read-only to Blueprint, written through the owning store.
 */
USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemState_WorldPresentation : public FPEPItemStateFragment
{
	GENERATED_BODY()

	/** Optional full-mesh skin. Null keeps the definition's base world mesh. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadOnly, Category="PEP|Item|World Presentation")
	TSoftObjectPtr<UStaticMesh> MeshOverride;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadOnly, Category="PEP|Item|World Presentation")
	FPEPWorldPresentationCosmetics Cosmetics;

	virtual EPEPStateStackability GetStackability() const override
	{
		return EPEPStateStackability::StackIdentity;
	}
};

USTRUCT(BlueprintType, meta=(DisplayName="World Presentation"))
struct PEPITEMCORE_API FPEPItemFragment_WorldPresentation : public FPEPItemFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|World Presentation")
	TSoftObjectPtr<UStaticMesh> StaticMesh;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|World Presentation")
	FTransform MeshTransform = FTransform::Identity;

	/** Defaults copied into each fresh item's persistent cosmetic state. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|World Presentation")
	TSoftObjectPtr<UStaticMesh> DefaultMeshOverride;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|World Presentation")
	FPEPWorldPresentationCosmetics DefaultCosmetics;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|World Presentation")
	TArray<FPEPWorldAttachmentSocketOverride> AttachmentSocketOverrides;

	FName ResolveAttachmentSocket(FName SlotName) const
	{
		const FPEPWorldAttachmentSocketOverride* Override =
			AttachmentSocketOverrides.FindByPredicate(
				[SlotName](const FPEPWorldAttachmentSocketOverride& Candidate)
				{
					return Candidate.SlotName == SlotName;
				});
		return Override && !Override->SocketName.IsNone()
			? Override->SocketName
			: SlotName;
	}

	virtual void BuildDefaultStates(TArray<FInstancedStruct>& OutStates) const override;
	virtual void GetOwnedStateStructs(TArray<const UScriptStruct*>& OutStateStructs) const override;
#if WITH_EDITOR
	virtual EDataValidationResult ValidateFragmentData(
		FDataValidationContext& Context,
		int32 FragmentIndex) const override;
#endif
};
