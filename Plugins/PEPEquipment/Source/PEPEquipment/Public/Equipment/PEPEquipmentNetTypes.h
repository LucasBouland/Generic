#pragma once

#include "CoreMinimal.h"
#include "Core/PEPTypes.h"
#include "Equipment/PEPEquipmentTypes.h"
#include "GameplayTagContainer.h"
#include "Net/Serialization/FastArraySerializer.h"

#include "PEPEquipmentNetTypes.generated.h"

class UPEPEquipmentComponent;

/** Public projection only: roots and visible attachments, never full instances or private content. */
USTRUCT(BlueprintType)
struct PEPEQUIPMENT_API FPEPEquippedItemPublic : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Equipment")
	FPEPItemId ItemId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Equipment")
	FPrimaryAssetId DefinitionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Equipment", meta=(Categories="Equipment.Slot"))
	FGameplayTag SlotTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Equipment")
	EPEPEquipmentHand Hand = EPEPEquipmentHand::None;

	/** Valid only for a visible attachment. Invalid for an equipment-slot root. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Equipment")
	FPEPItemId ParentItemId;

	/** Container slot on ParentItemId. The parent's presentation resolves it to a mesh socket. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Equipment")
	FName ParentSlotName;

	bool IsAttachment() const
	{
		return ParentItemId.IsValid();
	}
};

USTRUCT(BlueprintType)
struct PEPEQUIPMENT_API FPEPPublicEquipmentView : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="PEP|Equipment")
	TArray<FPEPEquippedItemPublic> Entries;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FastArrayDeltaSerialize<FPEPEquippedItemPublic, FPEPPublicEquipmentView>(
			Entries,
			DeltaParams,
			*this);
	}

	void PostReplicatedReceive(
		const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters);

	UPEPEquipmentComponent* OwnerComponent = nullptr;
};

template<>
struct TStructOpsTypeTraits<FPEPPublicEquipmentView>
	: TStructOpsTypeTraitsBase2<FPEPPublicEquipmentView>
{
	enum
	{
		WithNetDeltaSerializer = true
	};
};

/**
 * One temporary cosmetic prop, as replicated state rather than as an event. A multicast reaches
 * only the clients relevant when it fires; a pawn that becomes relevant later must still show
 * its current props, and state converges where an RPC is simply gone. Spam collapses into the
 * last value instead of filling a reliable queue.
 */
USTRUCT()
struct PEPEQUIPMENT_API FPEPTemporaryPresentationEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	FName PresentationKey;

	UPROPERTY()
	FPrimaryAssetId DefinitionId;

	UPROPERTY()
	FName AttachSocket;

	UPROPERTY()
	FPEPItemId SuppressedItemId;

	bool HasSameSpec(const FPEPTemporaryPresentationEntry& Other) const
	{
		return PresentationKey == Other.PresentationKey
			&& DefinitionId == Other.DefinitionId
			&& AttachSocket == Other.AttachSocket
			&& SuppressedItemId == Other.SuppressedItemId;
	}
};

USTRUCT()
struct PEPEQUIPMENT_API FPEPTemporaryPresentationView : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FPEPTemporaryPresentationEntry> Entries;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FastArrayDeltaSerialize<FPEPTemporaryPresentationEntry, FPEPTemporaryPresentationView>(
			Entries,
			DeltaParams,
			*this);
	}

	void PostReplicatedReceive(
		const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters);

	UPEPEquipmentComponent* OwnerComponent = nullptr;
};

template<>
struct TStructOpsTypeTraits<FPEPTemporaryPresentationView>
	: TStructOpsTypeTraitsBase2<FPEPTemporaryPresentationView>
{
	enum
	{
		WithNetDeltaSerializer = true
	};
};
