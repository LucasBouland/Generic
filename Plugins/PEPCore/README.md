# PEPCore

`PEPCore` is the dependency-light foundation shared by PEP domain plugins. Keep its public API small and stable: cross-domain contracts, foundational value types, canonical reason tags, PEP logging and PEP-wide debug settings.

## In scope

- `EPEPRequestResult` and `FPEPOperationResult` for generic request and infrastructure outcomes. Operations with domain-specific context use their owning domain's result type.
- `FPEPItemId` as a stable value contract shared by item, inventory, equipment and pickup APIs. It contains identity only; ID allocation is private to `PEPItemCore`'s item-instance creation path.
- Canonical cross-domain reason tags in `PEPCoreTags`, `LogPEP`, and the global PEP debug setting.
- Small contracts that genuinely have no owning gameplay/UI domain and are consumed by multiple PEP plugins.

## Out of scope and owners

- Item ID allocation, persistence, count helpers and Blueprint equality/generation functions: `PEPItems`, module `PEPItemCore`.
- Player controller, menu input state and player lifecycle: `PEPPlayer`.
- Player HUD and player-specific presentation: `PEPPlayerUI`, built on `PEPCoreUI` and connected to `PEPPlayer` through `IPEPPlayerHUDContract`.
- GMC/GMAS Pawn adapters: `PEPPawn`.
- General struct, reason-text and input-value helpers: `PEPUtility`.
- Shared widgets, UI navigation/style and CommonUI foundations: `PEPCoreUI`.
- Item, inventory, equipment, interaction, AI, camera and ballistic behavior: the plugin that owns that domain.

Do not add a module dependency to PEPCore merely to make a type convenient to include. Domain and UI modules should depend on the owner of the API they use. PEPCore itself does not depend on GMC, GMAS, Enhanced Input, UMG or Slate.

## API and migration notes

`FPEPItemId` remains in `/Script/PEPCore` to keep its shared reflected identity type stable. Runtime IDs are allocated only while creating an item through the authority-checked PEPItemCore API; the allocator itself is not a public Blueprint operation. Other moved reflected types and classes have CoreRedirects in their new plugins; keep those redirects while assets may still reference their old `/Script/PEPCore` paths.

`PEPCore` does not own item, inventory, equipment, weapon, interaction, action or UI result types. It owns `FPEPOperationResult` only when a result needs generic success/failure and a canonical reason tag. Domain results carry the same `bSuccess`/`ReasonTag` pair plus their own context and convert down to it — for example `FPEPItemResult::ToOperationResult()` in PEPItemCore, which drops only the item id. PEPCore never depends on those domain types.
