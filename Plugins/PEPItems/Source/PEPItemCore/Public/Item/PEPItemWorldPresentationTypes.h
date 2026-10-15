#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPtr.h"

#include "PEPItemWorldPresentationTypes.generated.h"

class UMaterialInterface;
class UStaticMesh;
class UTexture;

/** Replaces one material slot for a public visual variant such as a skin. */
USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPWorldMaterialOverride
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item|World Presentation",
		meta=(ClampMin="0"))
	int32 MaterialIndex = 0;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item|World Presentation")
	TSoftObjectPtr<UMaterialInterface> Material;

	bool operator==(const FPEPWorldMaterialOverride& Other) const
	{
		return MaterialIndex == Other.MaterialIndex && Material == Other.Material;
	}
};

/** Runtime scalar applied to one material slot, or every slot when MaterialIndex is INDEX_NONE. */
USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPWorldScalarParameter
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item|World Presentation")
	FName ParameterName;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item|World Presentation")
	float Value = 0.0f;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item|World Presentation",
		meta=(ClampMin="-1"))
	int32 MaterialIndex = INDEX_NONE;

	bool operator==(const FPEPWorldScalarParameter& Other) const
	{
		return ParameterName == Other.ParameterName
			&& Value == Other.Value
			&& MaterialIndex == Other.MaterialIndex;
	}
};

/** Runtime vector/color applied to one material slot, or every slot when MaterialIndex is INDEX_NONE. */
USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPWorldVectorParameter
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item|World Presentation")
	FName ParameterName;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item|World Presentation")
	FLinearColor Value = FLinearColor::White;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item|World Presentation",
		meta=(ClampMin="-1"))
	int32 MaterialIndex = INDEX_NONE;

	bool operator==(const FPEPWorldVectorParameter& Other) const
	{
		return ParameterName == Other.ParameterName
			&& Value == Other.Value
			&& MaterialIndex == Other.MaterialIndex;
	}
};

/** Runtime texture applied to one material slot, or every slot when MaterialIndex is INDEX_NONE. */
USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPWorldTextureParameter
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item|World Presentation")
	FName ParameterName;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item|World Presentation")
	TSoftObjectPtr<UTexture> Value;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item|World Presentation",
		meta=(ClampMin="-1"))
	int32 MaterialIndex = INDEX_NONE;

	bool operator==(const FPEPWorldTextureParameter& Other) const
	{
		return ParameterName == Other.ParameterName
			&& Value == Other.Value
			&& MaterialIndex == Other.MaterialIndex;
	}
};

/**
 * Explicitly cosmetic runtime data. New visual concerns belong here instead of exposing item state.
 * VisualStateTags are hooks for presentation actors; they must never be used as gameplay authority.
 */
USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPWorldPresentationCosmetics
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item|World Presentation")
	FName VariantName;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item|World Presentation",
		meta=(Categories="Item.Presentation"))
	FGameplayTagContainer VisualStateTags;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item|World Presentation")
	TArray<FPEPWorldMaterialOverride> MaterialOverrides;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item|World Presentation")
	TArray<FPEPWorldScalarParameter> ScalarParameters;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item|World Presentation")
	TArray<FPEPWorldVectorParameter> VectorParameters;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item|World Presentation")
	TArray<FPEPWorldTextureParameter> TextureParameters;

	bool operator==(const FPEPWorldPresentationCosmetics& Other) const
	{
		return VariantName == Other.VariantName
			&& VisualStateTags == Other.VisualStateTags
			&& MaterialOverrides == Other.MaterialOverrides
			&& ScalarParameters == Other.ScalarParameters
			&& VectorParameters == Other.VectorParameters
			&& TextureParameters == Other.TextureParameters;
	}
};

/** One public visual node. It deliberately contains no runtime item identity or gameplay state. */
USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPWorldPresentationNode
{
	GENERATED_BODY()

	/** Stable only inside this projection. Zero is the root node. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item|World Presentation")
	int32 NodeId = INDEX_NONE;

	/** INDEX_NONE for the root; otherwise identifies another node in this projection. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item|World Presentation")
	int32 ParentNodeId = INDEX_NONE;

	/** Exact cosmetic asset selected by the authority. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item|World Presentation")
	TSoftObjectPtr<UStaticMesh> StaticMesh;

	/** Cosmetic offset relative to the actor or parent attachment socket. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item|World Presentation")
	FTransform MeshTransform = FTransform::Identity;

	/** Socket already resolved by the server from the parent's Attachment slot. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item|World Presentation")
	FName AttachSocket;

	/** Sanitized cosmetic state; no gameplay state is included. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item|World Presentation")
	FPEPWorldPresentationCosmetics Cosmetics;

	bool operator==(const FPEPWorldPresentationNode& Other) const
	{
		return NodeId == Other.NodeId
			&& ParentNodeId == Other.ParentNodeId
			&& StaticMesh == Other.StaticMesh
			&& MeshTransform.Equals(Other.MeshTransform)
			&& AttachSocket == Other.AttachSocket
			&& Cosmetics == Other.Cosmetics;
	}
};

/** Public cosmetic graph for a dropped item: root plus visible Attachment descendants only. */
USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPWorldPresentationView
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Item|World Presentation")
	TArray<FPEPWorldPresentationNode> Nodes;

	bool operator==(const FPEPWorldPresentationView& Other) const
	{
		return Nodes == Other.Nodes;
	}
};
