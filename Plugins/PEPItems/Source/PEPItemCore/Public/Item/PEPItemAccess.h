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
	 * The player the request answers to. Null for a System() context, and for a context built
	 * From() a null or since-destroyed controller: neither of the latter is the server, so a store
	 * treats them as an outsider with no identity.
	 */
	TWeakObjectPtr<const AController> Requester;

	/**
	 * Server-internal mutations that answer to no player: loot generation, quest rewards, an
	 * ability spending a charge. The only way to obtain a system context, so the bypass greps.
	 */
	static FPEPItemAccessContext System()
	{
		FPEPItemAccessContext Context;
		Context.bSystem = true;
		return Context;
	}

	/**
	 * Defined out of line: assigning the weak pointer needs AController complete, and this header
	 * reaches most of the item code through the container fragment. The forward declaration stays.
	 * Never yields a system context, whatever InRequester is — including null.
	 */
	static FPEPItemAccessContext From(const AController* InRequester);

	/**
	 * True only for a context built by System(). A default-constructed context, From(nullptr) and a
	 * requester destroyed mid-request are not the server and must not inherit owner permissions.
	 */
	bool IsSystem() const
	{
		return bSystem;
	}

private:
	/** Set by System() alone; nothing else can grant it, so an absent requester never reads as one. */
	bool bSystem = false;
};
