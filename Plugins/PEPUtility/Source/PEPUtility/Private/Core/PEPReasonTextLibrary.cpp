#include "Core/PEPReasonTextLibrary.h"
#include "Core/PEPCoreTags.h"


FText UPEPReasonTextLibrary::GetReasonText(
	const FGameplayTag ReasonTag)
{
	if (!ReasonTag.IsValid())
	{
		return NSLOCTEXT(
			"PEPReason",
			"MissingReason",
			"Failure reason is missing.");
	}

	if (ReasonTag == PEPCoreTags::Reason_None)
	{
		return NSLOCTEXT(
			"PEPReason",
			"None",
			"No error.");
	}

	if (ReasonTag ==
		PEPCoreTags::Reason_InvalidTarget)
	{
		return NSLOCTEXT(
			"PEPReason",
			"InvalidTarget",
			"Invalid item or target.");
	}

	if (ReasonTag ==
		PEPCoreTags::Reason_OutOfRange)
	{
		return NSLOCTEXT(
			"PEPReason",
			"OutOfRange",
			"Target is out of range.");
	}

	if (ReasonTag ==
		PEPCoreTags::Reason_NotAuthority)
	{
		return NSLOCTEXT(
			"PEPReason",
			"NotAuthority",
			"Action must be executed by the authority.");
	}

	if (ReasonTag ==
		PEPCoreTags::Reason_BlockedByState)
	{
		return NSLOCTEXT(
			"PEPReason",
			"BlockedByState",
			"Action is blocked by the current state.");
	}

	if (ReasonTag ==
		PEPCoreTags::Reason_NoFreeSlot)
	{
		return NSLOCTEXT(
			"PEPReason",
			"NoFreeSlot",
			"No free inventory space.");
	}

	if (ReasonTag ==
		PEPCoreTags::Reason_StateMismatch)
	{
		return NSLOCTEXT(
			"PEPReason",
			"StateMismatch",
			"These items differ in a way that keeps them apart.");
	}

	if (ReasonTag ==
		PEPCoreTags::Reason_UnsupportedMode)
	{
		return NSLOCTEXT(
			"PEPReason",
			"UnsupportedMode",
			"Unsupported operation mode.");
	}

	return FText::Format(
		NSLOCTEXT(
			"PEPReason",
			"Unknown",
			"Action failed: {0}"),
		FText::FromName(
			ReasonTag.GetTagName()));
}

FText UPEPReasonTextLibrary::GetSuggestedFixText(const FGameplayTag ReasonTag)
{
	if (ReasonTag == PEPCoreTags::Reason_NoFreeSlot)
	{
		return NSLOCTEXT("PEPReasonFix", "NoFreeSlot", "Move or remove items, or choose a larger container.");
	}
	if (ReasonTag == PEPCoreTags::Reason_NotAuthority)
	{
		return NSLOCTEXT("PEPReasonFix", "NotAuthority",
		                 "Submit the request through the server-authoritative request path instead of calling the authority executor directly.");
	}
	if (ReasonTag == PEPCoreTags::Reason_BlockedByState)
	{
		return NSLOCTEXT("PEPReasonFix", "BlockedByState", "Check the current state and operation preconditions.");
	}
	if (ReasonTag == PEPCoreTags::Reason_InvalidTarget)
	{
		return NSLOCTEXT("PEPReasonFix", "InvalidTarget", "Check item id, target location, and required data.");
	}
	if (ReasonTag == PEPCoreTags::Reason_StateMismatch)
	{
		return NSLOCTEXT("PEPReasonFix", "StateMismatch",
		                 "Keep the items in separate stacks, or clear the state that differentiates them.");
	}
	return NSLOCTEXT("PEPReasonFix", "Generic", "Inspect the validation report and operation inputs.");
}

FPEPFailureMessage UPEPReasonTextLibrary::MakeFailureMessageFromOperationResult(
	const FPEPOperationResult& Result,
	const FName BadField)
{
	FPEPFailureMessage Message;
	Message.bSuccess = Result.bSuccess;
	Message.ReasonTag = Result.ReasonTag;
	Message.BadField = BadField;
	Message.Message = GetReasonText(Result.ReasonTag);
	Message.SuggestedFix = GetSuggestedFixText(Result.ReasonTag);
	return Message;
}
