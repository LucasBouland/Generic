#include "Core/PEPCoreTags.h"

namespace PEPCoreTags
{
	UE_DEFINE_GAMEPLAY_TAG(Input_Action_Primary, "Input.Action.Primary");
	UE_DEFINE_GAMEPLAY_TAG(Input_Action_Secondary, "Input.Action.Secondary");

	UE_DEFINE_GAMEPLAY_TAG(Reason_None, "Reason.None");
	UE_DEFINE_GAMEPLAY_TAG(Reason_InvalidTarget, "Reason.InvalidTarget");
	UE_DEFINE_GAMEPLAY_TAG(Reason_NotAuthority, "Reason.NotAuthority");
	UE_DEFINE_GAMEPLAY_TAG(Reason_OutOfRange, "Reason.OutOfRange");
	UE_DEFINE_GAMEPLAY_TAG(Reason_BlockedByState, "Reason.BlockedByState");
	UE_DEFINE_GAMEPLAY_TAG(Reason_NoFreeSlot, "Reason.NoFreeSlot");
	UE_DEFINE_GAMEPLAY_TAG(Reason_StateMismatch, "Reason.StateMismatch");
	UE_DEFINE_GAMEPLAY_TAG(Reason_UnsupportedMode, "Reason.UnsupportedMode");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Life_Down, "State.Life.Down", "Downed: crawling, awaiting revive.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Life_Dead, "State.Life.Dead", "Dead.");
}
