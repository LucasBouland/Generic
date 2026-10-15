#pragma once
#include "CoreMinimal.h"
#include "PEPItemStateFragment.generated.h"

/**
 * How a piece of runtime state behaves when several units share one stack.
 */
enum class EPEPStateStackability : uint8
{
	/**
	 * State that belongs to one unit: durability, remaining resource, uses, contents. The name is
	 * historical: SplitStack divides the stack count but copies every other state unchanged; it does
	 * not apportion this value between the resulting stacks. Stackable definitions therefore cannot
	 * carry this state until a fragment-specific split/merge policy exists.
	 */
	Divisible,

	/**
	 * State shared by every unit in a stack, such as a provenance marker. Allowed on stackable
	 * definitions, but two stacks merge only when this state is identical.
	 */
	StackIdentity,

	/** The stack count itself: subject of a merge, so neither restricted nor compared. */
	StackCount
};

/**
 * Mutable runtime state for an item instance.
 * State should be changed only by mutation helpers after rules have produced a valid plan.
 */
USTRUCT(BlueprintType)
struct PEPITEMCORE_API FPEPItemStateFragment
{
	GENERATED_BODY()

public:
	virtual ~FPEPItemStateFragment() = default;

	virtual EPEPStateStackability GetStackability() const
	{
		return EPEPStateStackability::Divisible;
	}

	/**
	 * True when this state has nothing left, e.g. a stack at zero or a resource at zero.
	 * Depletion is a legal, transitional state: the item still exists so the caller can read the
	 * result of the operation that emptied it, and is expected to destroy it right after.
	 */
	virtual bool IsDepleted() const
	{
		return false;
	}
};
