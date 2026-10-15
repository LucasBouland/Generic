#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

/** Texts describing one reason tag. */
struct FPEPReasonText
{
	/** Shown to players; must not expose implementation details. Empty uses the generic player message. */
	FText PlayerText;
	/** Diagnostics for developers, logs and tools; empty falls back to PlayerText. */
	FText DeveloperText;
	/** Developer-facing remediation hint; empty uses the generic hint. */
	FText SuggestedFix;
};

/**
 * Process-wide reason tag -> text registry used by UPEPReasonTextLibrary. The PEPCore reasons are built in.
 * Domain plugins register texts for their own reason tags from StartupModule (not from static initializers) and
 * unregister them in ShutdownModule. Registering a built-in reason overrides it until unregistered.
 * Lookups are O(1); all functions are thread-safe.
 */
class PEPUTILITY_API FPEPReasonTextRegistry
{
public:
	/** Adds or replaces the texts of ReasonTag. Invalid tags are ignored. */
	static void Register(const FGameplayTag& ReasonTag, const FPEPReasonText& Text);
	/** Removes texts registered for ReasonTag; a built-in reason returns to its built-in texts. */
	static void Unregister(const FGameplayTag& ReasonTag);
	/** Copies the texts known for ReasonTag (registered first, then built-in). False when none. */
	static bool Find(const FGameplayTag& ReasonTag, FPEPReasonText& OutText);
};
