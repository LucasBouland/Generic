#include "Interaction/PEPInteractionTags.h"

namespace PEPInteractionTags
{
	UE_DEFINE_GAMEPLAY_TAG(Pickup, "Interaction.Pickup");
	UE_DEFINE_GAMEPLAY_TAG(Open, "Interaction.Open");
	UE_DEFINE_GAMEPLAY_TAG(Inspect, "Interaction.Inspect");
	UE_DEFINE_GAMEPLAY_TAG(Reason_Interaction_Cancelled, "Reason.Interaction.Cancelled");
	UE_DEFINE_GAMEPLAY_TAG(Reason_Interaction_Interrupted, "Reason.Interaction.Interrupted");
	UE_DEFINE_GAMEPLAY_TAG(Reason_Interaction_NoLineOfSight, "Reason.Interaction.NoLineOfSight");
	UE_DEFINE_GAMEPLAY_TAG(Reason_Interaction_RateLimited, "Reason.Interaction.RateLimited");
}
