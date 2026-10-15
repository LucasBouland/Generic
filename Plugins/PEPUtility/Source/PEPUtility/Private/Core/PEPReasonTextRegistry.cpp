#include "Core/PEPReasonTextRegistry.h"

#include "Core/PEPCoreTags.h"
#include "Misc/ScopeRWLock.h"

namespace
{
	struct FPEPReasonTextStorage
	{
		/** Immutable after construction, read without locking. */
		TMap<FGameplayTag, FPEPReasonText> BuiltIn;
		/** Domain and project registrations, guarded by Lock. */
		TMap<FGameplayTag, FPEPReasonText> Registered;
		FRWLock Lock;

		void AddBuiltIn(const FGameplayTag& ReasonTag, const FText& PlayerText, const FText& DeveloperText,
			const FText& SuggestedFix)
		{
			FPEPReasonText& Text = BuiltIn.Add(ReasonTag);
			Text.PlayerText = PlayerText;
			Text.DeveloperText = DeveloperText;
			Text.SuggestedFix = SuggestedFix;
		}

		FPEPReasonTextStorage()
		{
			AddBuiltIn(PEPCoreTags::Reason_None,
				NSLOCTEXT("PEPReason", "None", "No error."),
				FText::GetEmpty(),
				FText::GetEmpty());
			AddBuiltIn(PEPCoreTags::Reason_InvalidTarget,
				NSLOCTEXT("PEPReason", "InvalidTarget", "Invalid item or target."),
				FText::GetEmpty(),
				NSLOCTEXT("PEPReasonFix", "InvalidTarget", "Check item id, target location, and required data."));
			AddBuiltIn(PEPCoreTags::Reason_OutOfRange,
				NSLOCTEXT("PEPReason", "OutOfRange", "Target is out of range."),
				FText::GetEmpty(),
				FText::GetEmpty());
			// Authority and mode failures are programming errors: players get a generic message.
			AddBuiltIn(PEPCoreTags::Reason_NotAuthority,
				NSLOCTEXT("PEPReason", "NotAuthority_Player", "This action is not available right now."),
				NSLOCTEXT("PEPReason", "NotAuthority", "Action must be executed by the authority."),
				NSLOCTEXT("PEPReasonFix", "NotAuthority",
					"Submit the request through the server-authoritative request path instead of calling the authority executor directly."));
			AddBuiltIn(PEPCoreTags::Reason_BlockedByState,
				NSLOCTEXT("PEPReason", "BlockedByState", "Action is blocked by the current state."),
				FText::GetEmpty(),
				NSLOCTEXT("PEPReasonFix", "BlockedByState", "Check the current state and operation preconditions."));
			AddBuiltIn(PEPCoreTags::Reason_NoFreeSlot,
				NSLOCTEXT("PEPReason", "NoFreeSlot", "No free inventory space."),
				FText::GetEmpty(),
				NSLOCTEXT("PEPReasonFix", "NoFreeSlot", "Move or remove items, or choose a larger container."));
			AddBuiltIn(PEPCoreTags::Reason_StateMismatch,
				NSLOCTEXT("PEPReason", "StateMismatch", "These items differ in a way that keeps them apart."),
				FText::GetEmpty(),
				NSLOCTEXT("PEPReasonFix", "StateMismatch",
					"Keep the items in separate stacks, or clear the state that differentiates them."));
			AddBuiltIn(PEPCoreTags::Reason_UnsupportedMode,
				NSLOCTEXT("PEPReason", "UnsupportedMode_Player", "This action is not supported."),
				NSLOCTEXT("PEPReason", "UnsupportedMode", "Unsupported operation mode."),
				FText::GetEmpty());
		}
	};

	FPEPReasonTextStorage& GetReasonTextStorage()
	{
		static FPEPReasonTextStorage Storage;
		return Storage;
	}
}

void FPEPReasonTextRegistry::Register(const FGameplayTag& ReasonTag, const FPEPReasonText& Text)
{
	if (!ReasonTag.IsValid())
	{
		return;
	}
	FPEPReasonTextStorage& Storage = GetReasonTextStorage();
	FWriteScopeLock WriteLock(Storage.Lock);
	Storage.Registered.Add(ReasonTag, Text);
}

void FPEPReasonTextRegistry::Unregister(const FGameplayTag& ReasonTag)
{
	FPEPReasonTextStorage& Storage = GetReasonTextStorage();
	FWriteScopeLock WriteLock(Storage.Lock);
	Storage.Registered.Remove(ReasonTag);
}

bool FPEPReasonTextRegistry::Find(const FGameplayTag& ReasonTag, FPEPReasonText& OutText)
{
	if (!ReasonTag.IsValid())
	{
		return false;
	}
	FPEPReasonTextStorage& Storage = GetReasonTextStorage();
	{
		FReadScopeLock ReadLock(Storage.Lock);
		if (const FPEPReasonText* Registered = Storage.Registered.Find(ReasonTag))
		{
			OutText = *Registered;
			return true;
		}
	}
	if (const FPEPReasonText* BuiltIn = Storage.BuiltIn.Find(ReasonTag))
	{
		OutText = *BuiltIn;
		return true;
	}
	return false;
}
