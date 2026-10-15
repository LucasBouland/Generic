#pragma once

#include "NativeGameplayTags.h"

namespace PEPInteractionTags
{
	PEPINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Pickup);
	PEPINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Open);
	PEPINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Inspect);

	/** A timed interaction was stopped on request: input released, superseded by a new one. */
	PEPINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reason_Interaction_Cancelled);

	/** A timed interaction was stopped by something other than the player: owner gone, ability ended. */
	PEPINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reason_Interaction_Interrupted);

	/** The authority could not see the target from the interaction view point. */
	PEPINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reason_Interaction_NoLineOfSight);

	/** The authority refused a client request sent too soon after the previous one. */
	PEPINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reason_Interaction_RateLimited);
}
