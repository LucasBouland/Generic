#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class AController;

/**
 * Which set of permissions a store applies to one request.
 *
 * This is derived by the store from an FPEPItemAccessContext, never supplied by the caller: a
 * caller free to name its own role is free to name itself the owner.
 */
enum class EPEPAccessRole : uint8
{
	Owner,
	External
};

/**
 * Who is asking. Callers hand a store an identity; the store decides what that identity may do.
 *
 * Deliberately not a USTRUCT. An unreflected type cannot appear in a UFUNCTION signature, so the
 * compiler rejects any attempt to take the requester from an RPC parameter — which would hand a
 * client back the ability to claim whatever identity it liked. The requester is always resolved
 * server-side from the connection the call arrived on.
 */
struct PEPITEMCORE_API FPEPItemAccessContext
{
	/**
	 * Null for server-internal mutations that answer to no player: loot generation, quest rewards,
	 * an ability spending a charge. Build those through System() so the bypass greps.
	 *
	 * Weak, and the distinction matters: never set is a server mutation, whereas set but since
	 * destroyed is a requester that no longer exists. Only the first may keep owner permissions.
	 */
	TWeakObjectPtr<const AController> Requester;

	static FPEPItemAccessContext System()
	{
		return FPEPItemAccessContext();
	}

	/**
	 * Defined out of line: assigning the weak pointer needs AController complete, and this header
	 * reaches most of the item code through the container fragment. The forward declaration stays.
	 */
	static FPEPItemAccessContext From(const AController* InRequester);

	/** Explicitly null, not merely stale: a controller destroyed mid-request is not the server. */
	bool IsSystem() const
	{
		return Requester.IsExplicitlyNull();
	}
};
