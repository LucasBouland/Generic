#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "UObject/WeakObjectPtr.h"

class AActor;
class UPEPInventoryComponent;

/**
 * The server's half of an open loot session: which remote store this one may reach, how far it may
 * be, and what has to be undone to close it.
 *
 * Gathered here because these four were kept as loose members and had to be set and cleared in
 * step; a session that kept its timer but dropped its delegate, or the reverse, is not a state
 * anything should be able to reach by forgetting a line.
 *
 * The client's half — the snapshot and the flag saying a session exists — stays on the component,
 * where it is replicated. This struct is authority-side only and never crosses the wire.
 */
struct PEPINVENTORY_API FPEPInventoryTransferSession
{
	/*
	 * Plain C++, with no reflection at all — which is the honest shape. None of this belongs on the
	 * wire or in an editor, and a delegate handle could not be a UPROPERTY in any case.
	 */

	/** Weak: the corpse being looted can be destroyed while someone is looking into it. */
	TWeakObjectPtr<UPEPInventoryComponent> RemoteInventory;

	/** Actor whose OnEndPlay ends the session; kept to unbind from it even once it is unreachable. */
	TWeakObjectPtr<AActor> RemoteOwner;

	float MaxDistance = 0.0f;

	/** Subscription on the remote store, so its changes reach the mirror. */
	FDelegateHandle RemoteChangeHandle;

	/**
	 * Re-checks the distance, the one condition with no event behind it: nothing announces a pawn
	 * crossing a radius. A session is not something the looter can be trusted to end.
	 */
	FTimerHandle ValidationTimer;

	/** Named for what a caller wants to know, rather than making each one test the same fields. */
	bool IsOpen() const
	{
		return RemoteInventory.IsValid() && MaxDistance > 0.0f;
	}

	UPEPInventoryComponent* GetRemote() const
	{
		return RemoteInventory.Get();
	}

	void Reset()
	{
		RemoteInventory.Reset();
		RemoteOwner.Reset();
		MaxDistance = 0.0f;
		RemoteChangeHandle.Reset();
		ValidationTimer.Invalidate();
	}
};
