# PEPInteraction

Runtime interaction plugin for actor-based targets.

## Selection flow

- Add `UPEPInteractionComponent` to the player pawn and call `Init` with the interactor actor.
- Interactable actors implement `IPEPInteractionInteractable`, which supplies available options, validation, and execution through Gameplay Tags.
- Configure `SelectionShape` on `UPEPInteractionComponent` as `Line`, `SphereSweep`, or `Cylinder`.
- `Range` sets the selection length and server interaction distance. `SelectionRadius` sets the radius for sphere and cylinder selection.
- Setting `SelectionRadius` to 0 falls back to line selection.
- Set `TraceStartSocketName` to a bone or socket on a scene component owned by the interactor to start selection and server line-of-sight checks there. `TraceStartOffset` is applied in that socket's local space; if no socket name is set, selection uses the camera plus the offset and server validation keeps using the actor eye view plus the offset.
- Cylinder selection follows either the camera forward axis or the interactor forward axis.
- Add `UPEPInteractionTargetComponent` to an interactable actor to set its normalized `SelectionPriority` from 0 to 1.
- The selected target maximizes the normalized weighted score for priority, aim alignment, and distance. `SelectionHysteresis` prevents rapid switching between near-equal candidates.

The sphere sweep uses a capsule overlap along the view direction. Cylinder selection uses a box overlap to gather candidates, then applies a radial cylinder filter. Both shapes check line of sight for each candidate. The server validates range, line of sight, options, and `CanInteract` before execution.

`OnInteractionSelectionChanged` notifies listeners when the target or its options change. `OnSelectedOptionChanged` notifies listeners when cycling among the current target's Gameplay Tag options.

## Timed interactions

An option is instant unless its target says otherwise: `IPEPInteractionInteractable::GetInteractionDuration` returns 0 by default. A positive duration turns the option into a hold.

- `RequestInteractWithInteractionTag` stays the single local entry point. The authority handles every request through `BeginInteractWithInteractionTag`: an instant option executes at once, a timed option starts the component's active interaction and returns `Submitted`.
- The authority re-validates the active interaction on every tick (target, range with `ServerRangeTolerance`, line of sight, offered options, `CanInteract`) and executes it through `ExecuteInteraction` once its duration has elapsed. A failed check cancels it with the validation reason (`Reason.OutOfRange`, `Reason.InvalidTarget`, `Reason.UnsupportedMode`, or the reason returned by `CanInteract` such as `Reason.BlockedByState`).
- One timed interaction runs at a time. Starting another cancels the previous one with `Reason.Interaction.Cancelled`; requesting the one already in progress keeps it running.
- The authority ticks the active interaction before any local selection work, so it progresses on a dedicated server for remote pawns.
- Targets are told on the authority through `OnInteractionStarted` and `OnInteractionCancelled`, both no-ops by default. Exactly one of `ExecuteInteraction` or `OnInteractionCancelled` follows a start.

### Cancellation

Cancellation is always explicit or the result of server re-validation. Changing the local selection — looking away, cycling options — never cancels an interaction in progress.

- `RequestCancelInteraction` is the local entry point for input release. A client always sends the request, even before it has seen the interaction start, so a quick tap still stops a hold the server has just begun.
- `CancelInteraction(ReasonTag)` is the authority executor. Without a reason it uses `Reason.Interaction.Cancelled`.
- `Reason.Interaction.Interrupted` marks a stop nobody asked for: the interaction component ending play, or the interact ability ending for another reason.

### State and events

The active interaction (`FPEPActiveInteraction`: id, target, option, start time, duration) replicates to the owning client only. Its start time comes from `AGameStateBase::GetServerWorldTimeSeconds`, so `GetInteractionProgress` works on the authority and, as an estimate, on the owning client. `IsInteractionInProgress`, `GetActiveInteractionTarget`, `GetActiveInteractionTag` and `GetActiveInteractionDuration` complete the queries.

`OnInteractionStarted`, `OnInteractionCompleted` and `OnInteractionCancelled` fire on the authority and on the owning client:

- Started fires on the owning client from the replicated state, so a listener can already read progress.
- Completed and Cancelled reach the owning client through reliable client RPCs, because the cleared state cannot say how an interaction ended and an instant execution never appears in it. Interaction ids keep a late state update from reviving an interaction whose end was already reported.
- Every request the authority handles ends in exactly one Completed — carrying the execution result, success or failure, including a request rejected by validation — or, for a timed interaction that started, one Cancelled.

## Extensibility

`UPEPInteractionComponent` is Blueprintable and its selection and validation steps are virtual for C++ subclasses: `ValidateInteraction`, `ValidateLineOfSight`, `HasLineOfSightToTarget`, `GetTraceStartLocation`, `GetSelectionAxis`, `IsWithinSelectionVolume`, `CalculateCandidateScore`, `ShouldPerformLocalSelection`, `ResolveInteractionDuration` (to scale target durations) and `TickActiveInteraction`.

## Ability integration

The core `PEPInteraction` module does not depend on GMC or GMAS. For local input or UI, call `RequestInteractWithInteractionTag` on the pawn interaction component, and `RequestCancelInteraction` on release for timed options. A client request returns `EPEPRequestResult::Submitted` while a reliable server RPC performs final validation and execution.

Server-side code that already runs on the authority may call `BeginInteractWithInteractionTag` (honours durations) or `ExecuteInteractWithInteractionTag` (executes at once, ignoring durations), and `CancelInteraction`.

### GMAS ability module

The `PEPInteractionGMAS` runtime module provides `UPEPInteractAbility`, a GMAS ability that drives the component on the ability owner:

1. The ability is ancillary (`bActivateOnMovementTick = false`), so it never runs in a replayed movement tick.
2. On the owning client (or listen-server host), it sends the selected target and selected option to the server with `UGMCAbilityTask_SetTargetDataInstancedStruct`, carrying an `FPEPInteractAbilityTargetData`. An empty selection is still sent, so both sides end the ability together.
3. When the data arrives on the authority, the ability calls `BeginInteractWithInteractionTag`: an instant option executes immediately, a timed option starts. This is the only point where the ability changes game state, and it is guarded by an authority check outside any GMAS replay.
4. With `bCancelOnInputRelease` (default) and an input action, `UGMCAbilityTask_WaitForInputKeyRelease` cancels a timed interaction on release with `Reason.Interaction.Cancelled`.
5. The ability ends on each machine when the component reports the interaction completed or cancelled there, on input release, or on an invalid request. With `bCancelInteractionOnAbilityEnd` (default), an ability ended for another reason cancels its timed interaction with `Reason.Interaction.Interrupted`.

Tasks are created identically on client and server so that GMAS task ids stay paired. Override `ResolveInteractionComponent` when the component is not on the ability owner, and `MakeLocalTargetData` to request something other than the current selection.
