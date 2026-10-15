#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Item/PEPContainerAccessTypes.h"
#include "Item/PEPItemFragment.h"
#include "PEPItemFragment_Container.generated.h"

USTRUCT(BlueprintType)
struct PEPINVENTORY_API FPEPItemState_ContainerRuntime : public FPEPItemStateFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="PEP|Item|Container")
	bool bLocked = false;
};

UENUM(BlueprintType)
enum class EPEPContainerSlotMode : uint8
{
	Grid,
	Single
};

/** Structural meaning of a child slot. Content stays private; attachments may be presented. */
UENUM(BlueprintType)
enum class EPEPContainerSlotPurpose : uint8
{
	Content,
	Attachment
};

USTRUCT(BlueprintType)
struct PEPINVENTORY_API FPEPContainerSlotDefinition
{
	GENERATED_BODY()

public:
	FPEPContainerSlotDefinition();
	~FPEPContainerSlotDefinition();
	FPEPContainerSlotDefinition(const FPEPContainerSlotDefinition& Other);
	FPEPContainerSlotDefinition(FPEPContainerSlotDefinition&& Other);
	FPEPContainerSlotDefinition& operator=(const FPEPContainerSlotDefinition& Other);
	FPEPContainerSlotDefinition& operator=(FPEPContainerSlotDefinition&& Other);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PEP|Item")
	FName SlotName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item")
	EPEPContainerSlotMode Mode = EPEPContainerSlotMode::Grid;

	/** Content is the privacy-safe default. Attachment slots must use Single mode. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item")
	EPEPContainerSlotPurpose Purpose = EPEPContainerSlotPurpose::Content;

	UPROPERTY(
		EditDefaultsOnly,
		BlueprintReadOnly,
		Category="PEP|Item",
		meta=(
			EditCondition="Mode == EPEPContainerSlotMode::Grid",
			EditConditionHides,
			ClampMin="1"))
	FIntPoint GridSize = FIntPoint(1, 1);

	UPROPERTY(
		EditDefaultsOnly,
		BlueprintReadOnly,
		Category="PEP|Item",
		meta=(
			EditCondition="Mode == EPEPContainerSlotMode::Grid",
			EditConditionHides))
	bool bAllowRotation = true;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item", meta=(Categories="Item"))
	FGameplayTagContainer RequiredItemTags;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item", meta=(Categories="Item"))
	FGameplayTagContainer BlockedItemTags;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Access")
	EPEPContainerSlotVisibility OwnerVisibility = EPEPContainerSlotVisibility::Lootable;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Access")
	EPEPContainerSlotVisibility OtherVisibility = EPEPContainerSlotVisibility::Lootable;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Access")
	bool bOwnerCanTake = true;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Access")
	bool bOwnerCanPut = true;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Access")
	bool bOwnerCanMoveInside = true;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Access")
	bool bOtherCanTake = true;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Access")
	bool bOtherCanPut = true;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Access")
	bool bOtherCanMoveInside = true;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Access")
	bool bIncludeChildrenInExternalView = true;
	bool IsValid() const;

	/** The owner-versus-other choice, made once. Callers pass a role and read plain booleans. */
	FPEPSlotAccessPolicy GetPolicyFor(EPEPAccessRole Role) const;
};

/**
 * A reusable bundle of slot policy — access rules, visibility, item tags — with no name or size
 * of its own. Authored once in UPEPItemSettings::SlotPresets and applied to as many slots as
 * share the same policy, so a jacket with six pockets that all behave the same way doesn't need
 * those ~10 fields retyped six times.
 *
 * Deliberately excludes SlotName, Mode, GridSize and Purpose: those are what makes one slot a
 * different *place* from another, not a different *policy*, and a preset should never overwrite
 * them.
 */
USTRUCT(BlueprintType)
struct PEPINVENTORY_API FPEPContainerSlotPreset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="PEP|Item")
	FName PresetName;

	UPROPERTY(EditAnywhere, Category="PEP|Item")
	bool bAllowRotation = true;

	UPROPERTY(EditAnywhere, Category="PEP|Item", meta=(Categories="Item"))
	FGameplayTagContainer RequiredItemTags;

	UPROPERTY(EditAnywhere, Category="PEP|Item", meta=(Categories="Item"))
	FGameplayTagContainer BlockedItemTags;

	UPROPERTY(EditAnywhere, Category="PEP|Item|Access")
	EPEPContainerSlotVisibility OwnerVisibility = EPEPContainerSlotVisibility::Lootable;

	UPROPERTY(EditAnywhere, Category="PEP|Item|Access")
	EPEPContainerSlotVisibility OtherVisibility = EPEPContainerSlotVisibility::Lootable;

	UPROPERTY(EditAnywhere, Category="PEP|Item|Access")
	bool bOwnerCanTake = true;

	UPROPERTY(EditAnywhere, Category="PEP|Item|Access")
	bool bOwnerCanPut = true;

	UPROPERTY(EditAnywhere, Category="PEP|Item|Access")
	bool bOwnerCanMoveInside = true;

	UPROPERTY(EditAnywhere, Category="PEP|Item|Access")
	bool bOtherCanTake = true;

	UPROPERTY(EditAnywhere, Category="PEP|Item|Access")
	bool bOtherCanPut = true;

	UPROPERTY(EditAnywhere, Category="PEP|Item|Access")
	bool bOtherCanMoveInside = true;

	UPROPERTY(EditAnywhere, Category="PEP|Item|Access")
	bool bIncludeChildrenInExternalView = true;

	/** Copies every field above onto Slot. Never touches SlotName, Mode, GridSize or Purpose. */
	void ApplyTo(FPEPContainerSlotDefinition& Slot) const;
};

USTRUCT(BlueprintType, meta=(DisplayName="Container"))
struct PEPINVENTORY_API FPEPItemFragment_Container : public FPEPItemFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item")
	TArray<FPEPContainerSlotDefinition> Slots;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Access")
	EPEPContainerLootVisibility ExternalLootVisibility = EPEPContainerLootVisibility::ShowContents;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Container", meta=(Categories="Item"))
	FGameplayTagContainer RequiredContainedItemTags;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Container", meta=(Categories="Item"))
	FGameplayTagContainer BlockedContainedItemTags;
	/** Actions that may reach items through this container. Every containing ancestor must grant it. */
	UPROPERTY(
		EditDefaultsOnly,
		BlueprintReadOnly,
		Category="PEP|Item|Container|Gameplay Access",
		meta=(Categories="Inventory.GameplayAccess"))
	FGameplayTagContainer GrantedGameplayAccess;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Container")
	bool bStartsLocked = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Container", meta=(Categories="Item"))
	FGameplayTagContainer RequiredUnlockKeyTags;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PEP|Item|Container")
	bool bCanRelock = false;
	virtual void BuildDefaultStates(TArray<FInstancedStruct>& OutStates) const override;
	virtual void GetOwnedStateStructs(TArray<const UScriptStruct*>& OutStateStructs) const override;
	const FPEPContainerSlotDefinition* FindSlot(FName SlotName) const;
#if WITH_EDITOR
	virtual EDataValidationResult ValidateFragmentData(
		FDataValidationContext& Context,
		int32 FragmentIndex) const override;
#endif
};
