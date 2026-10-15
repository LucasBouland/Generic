#include "Core/PEPReasonTextLibrary.h"

#include "Core/PEPCoreTags.h"
#include "Core/PEPReasonTextRegistry.h"

FText UPEPReasonTextLibrary::GetReasonText(const FGameplayTag ReasonTag)
{
	FPEPReasonText Text;
	if (FPEPReasonTextRegistry::Find(ReasonTag, Text) && !Text.PlayerText.IsEmpty())
	{
		return Text.PlayerText;
	}
	return NSLOCTEXT("PEPReason", "Generic_Player", "Action failed.");
}

FText UPEPReasonTextLibrary::GetDeveloperReasonText(const FGameplayTag ReasonTag)
{
	if (!ReasonTag.IsValid())
	{
		return NSLOCTEXT("PEPReason", "MissingReason", "Failure reason is missing.");
	}
	FPEPReasonText Text;
	if (FPEPReasonTextRegistry::Find(ReasonTag, Text))
	{
		if (!Text.DeveloperText.IsEmpty())
		{
			return Text.DeveloperText;
		}
		if (!Text.PlayerText.IsEmpty())
		{
			return Text.PlayerText;
		}
	}
	return FText::Format(
		NSLOCTEXT("PEPReason", "Unknown", "Action failed: {0}"),
		FText::FromName(ReasonTag.GetTagName()));
}

FText UPEPReasonTextLibrary::GetSuggestedFixText(const FGameplayTag ReasonTag)
{
	if (ReasonTag == PEPCoreTags::Reason_None)
	{
		return FText::GetEmpty();
	}
	FPEPReasonText Text;
	if (FPEPReasonTextRegistry::Find(ReasonTag, Text) && !Text.SuggestedFix.IsEmpty())
	{
		return Text.SuggestedFix;
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
	// Successes need no reason; describe them as Reason.None rather than as a missing failure reason.
	FGameplayTag TextTag = Result.ReasonTag;
	if (Result.bSuccess && !TextTag.IsValid())
	{
		TextTag = PEPCoreTags::Reason_None;
	}
	Message.Message = GetReasonText(TextTag);
	Message.DeveloperMessage = GetDeveloperReasonText(TextTag);
	if (!Result.bSuccess)
	{
		Message.SuggestedFix = GetSuggestedFixText(TextTag);
	}
	return Message;
}
