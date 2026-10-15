#pragma once

#include "CoreMinimal.h"
#include "Item/PEPItemResultTypes.h"

#include "PEPInventoryRequest.generated.h"

/**
 * The vocabulary of a request that had to travel.
 *
 * Nothing here is predicted: a client asks, the server decides, and the answer comes back naming
 * the request it answers. The id is a correlation token for the caller's own bookkeeping — the
 * server reads nothing from it but its validity, since a client cannot be trusted to say who it is
 * asking on behalf of, only which of its own questions this reply belongs to.
 */
UENUM(BlueprintType)
enum class EPEPInventoryRequestKind : uint8
{
	Move,
	MoveAuto,
	Swap,
	SplitStack,
	MergeStacks,
	PutIntoSession,
	TakeFromSession,
	Destroy,
	MoveInSession
};

USTRUCT(BlueprintType)
struct PEPINVENTORY_API FPEPInventoryRequestResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="PEP|Inventory|Request")
	int32 RequestId = 0;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Inventory|Request")
	EPEPInventoryRequestKind Kind = EPEPInventoryRequestKind::Move;

	UPROPERTY(BlueprintReadOnly, Category="PEP|Inventory|Request")
	FPEPItemResult Result;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnPEPInventoryRequestResolved,
	const FPEPInventoryRequestResult&,
	RequestResult);
