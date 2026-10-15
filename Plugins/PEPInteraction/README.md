# PEPInteraction

Runtime interaction plugin for actor-based targets.

## Selection flow

- Add `UPEPInteractionComponent` to the player pawn and call `Init` with the interactor actor on every machine.
- Interactable actors implement `IPEPInteractionInteractable`, which supplies available options, validation, and execution through Gameplay Tags.
- Configure `SelectionShape` on `UPEPInteractionComponent` as `Line`, `SphereSweep`, or `Cylinder`.
- `Range` sets the selection length and server interaction distance. `SelectionRadius` sets the radius for sphere and cylinder selection.
- Setting `SelectionRadius` to 0 falls back to line selection.
- Selection and server validation share one view point, `GetInteractionViewPoint`: the bone or socket `TraceStartSocketName` on a scene component owned by the interactor when set, else the interactor's eyes view point (`GetActorEyesViewPoint`). `TraceStartOffset` is applied in that frame. The camera is never the start point, because it only exists locally and sits behind the pawn in third person: a target selected from the camera could be refused by the server. The camera only gives the aim direction.
- Cylinder selection follows either the camera forward axis or the interactor forward axis.
- Add `UPEPInteractionTargetComponent` to an interactable actor to set its normalized `SelectionPriority` from 0 to 1.
- The selected target maximizes the normalized weighted score for priority, aim alignment, and distance among the candidates in line of sight. `SelectionHysteresis` prevents rapid switching between near-equal candidates.

The sphere sweep uses a capsule overlap along the view direction. Cylinder selection uses a box overlap to gather candidates, then applies a radial cylinder filter. Candidates are ranked by score and line of sight is traced from the best down, stopping at the first visible one (plus, at most, the previous target for hysteresis). Selection buffers are reused from one tick to the next.

`OnInteractionSelectionChanged` notifies listeners when the target or its options change. `OnSelectedOptionChanged` notifies listeners when cycling among the current target's Gameplay Tag options.

### Who selects, and ticking

Selection runs only where `ShouldPerformLocalSelection` is true: a pawn interactor that `IsLocallyControlled`, or a non-pawn interactor owned by a local player controller. A server never traces for a remote player's pawn, and a simulated proxy never selects nor broadcasts selection events.

The component ticks only when it has work: local selection, or a timed interaction on the authority. The need is re-evaluated on `Init`, `BeginPlay`, controller changes of a pawn interactor (`APawn::ReceiveControllerChangedDelegate`) and timed interaction start and end. Call `RefreshTickEnabled` after changing anything else `ShouldPerformLocalSelection` depends on, such as the owner of a non-pawn interactor.

## Server validation

The authority validates every request before execution, in this order:

1. Reach (`ValidateInteractionReach`): authority, a valid interactable target in the same world, and range. Range is measured from the shared view point to the closest point of the target's bounds (its location when it has no primitive), within `Range + ServerRangeTolerance`.
2. The option is among the target's `GetInteractOptionTags` (`Reason.UnsupportedMode` otherwise).
3. Line of sight from the shared view point (`Reason.Interaction.NoLineOfSight` otherwise).
4. The target's `CanInteract`, whose reason is reported as is.

`ValidateInteraction` runs the whole sequence; both functions are virtual.

## Timed interactions

An option is instant unless its target says otherwise: `IPEPInteractionInteractable::GetInteractionDuration` returns 0 by default. A positive duration turns the option into a hold.

- `RequestInteractWithInteractionTag` stays the single local entry point. The authority handles every request through `BeginInteractWithInteractionTag`: an instant option executes at once, a timed option starts the component's active interaction and returns `Submitted`.
- While it runs, the authority re-validates it:
  - every tick, reach (target, range): a failure cancels at once with its reason (`Reason.OutOfRange`, `Reason.InvalidTarget`);
  - every `ActiveInteractionValidationInterval` (0.1 s by default), the full validation (options, line of sight, `CanInteract`). Its failures cancel only once they have lasted `ActiveInteractionValidationGracePeriod` (0.25 s by default), so a brief occlusion does not end a hold. The cancel carries the validation reason (`Reason.UnsupportedMode`, `Reason.Interaction.NoLineOfSight`, or the reason returned by `CanInteract` such as `Reason.BlockedByState`).
- Once the duration has elapsed, the authority validates once more and executes through `ExecuteInteraction` without validating again. A failure at that point delays completion within the grace period; it never executes a failing interaction.
- One timed interaction runs at a time. Starting another cancels the previous one with `Reason.Interaction.Cancelled`; requesting the one already in progress keeps it running.
- The authority ticks the active interaction before any local selection work, so it progresses on a dedicated server for remote pawns.
- Targets are told on the authority through `OnInteractionStarted` and `OnInteractionCancelled`, both no-ops by default. Exactly one of `ExecuteInteraction` or `OnInteractionCancelled` follows a start.

### Cancellation

Cancellation is always explicit or the result of server re-validation. Changing the local selection — looking away, cycling options — never cancels an interaction in progress.

- `RequestCancelInteraction` is the local entry point for input release. A client always sends the request, even before it has seen the interaction start, so a quick tap still stops a hold the server has just begun.
- `CancelInteraction(ReasonTag)` is the authority executor. Without a reason it uses `Reason.Interaction.Cancelled`.
- `Reason.Interaction.Interrupted` marks a stop nobody asked for: the interaction component ending play, or the interact ability ending for another reason.

### State and events

The active interaction (`FPEPActiveInteraction`: id, target, option, start time, duration) replicates to the owning client only. Its start time comes from `AGameStateBase::GetServerWorldTimeSeconds`, so `GetInteractionProgress` works on the authority and, as an estimate, on the owning client. `IsInteractionInProgress`, `GetActiveInteractionId`, `GetActiveInteractionTarget`, `GetActiveInteractionTag` and `GetActiveInteractionDuration` complete the queries.

`OnInteractionStarted`, `OnInteractionCompleted` and `OnInteractionCancelled` fire on the authority and on the owning client:

- Started fires on the owning client from the replicated state, so a listener can already read progress.
- Completed and Cancelled reach the owning client through reliable client RPCs, because the cleared state cannot say how an interaction ended and an instant execution never appears in it. Interaction ids keep a late state update from reviving an interaction whose end was already reported.
- Every request the authority handles ends in exactly one Completed — carrying the execution result, success or failure, including a request rejected by validation — or, for a timed interaction that started, one Cancelled. A repeat of the interaction in progress is absorbed into it instead.

### Interaction ids

Every handled request has an `InteractionId`, carried by `FPEPInteractionResult::InteractionId` on Completed and by the Cancelled event, so a listener matches the outcome of its own request exactly.

- The machine making a request reserves its id with `ReserveInteractionId`: positive on the authority, negative on the owning client. The two sequences never meet, so ids are unique per component.
- `RequestInteractWithInteractionTag` reserves one itself; `GetLastRequestedInteractionId` returns it. Authority callers can pass a reserved id to `BeginInteractWithInteractionTag` and `ExecuteInteractWithInteractionTag` (advanced pin), or 0 to let the component allocate one; the returned result carries the id actually used.
- An id the component cannot accept (already used, or never reserved) is replaced by a fresh one. A repeated request joining the interaction in progress returns that interaction's id.

### Network limits

`MinClientRequestInterval` (0.1 s by default) is the minimum real time between two interaction requests the authority accepts from the owning client. An earlier request completes as failed with `Reason.Interaction.RateLimited`. Repeating the interaction in progress is never limited.

## Blueprint

- `IPEPInteractionInteractable::ExecuteInteraction` can be implemented in Blueprint but not called from it: only the component calls it, after validation. `GetInteractOptionTags`, `CanInteract` and `GetInteractionDuration` stay callable queries and must have no side effects.
- `FPEPInteractionResult` is read-only in Blueprint. A Blueprint interactable builds its results with `UPEPInteractionLibrary::MakeInteractionSuccess` and `MakeInteractionFailure`; the component fills in a missing target or option, and always sets the id.
- `BeginInteractWithInteractionTag`, `ExecuteInteractWithInteractionTag` and `CancelInteraction` are authority only. `Init`, the `Request…` entry points and the queries work on every machine.

## Extensibility

`UPEPInteractionComponent` is Blueprintable and its selection and validation steps are virtual for C++ subclasses: `ValidateInteraction`, `ValidateInteractionReach`, `ValidateLineOfSight`, `HasLineOfSightToTarget`, `GetInteractionViewPoint` (the shared view point; `GetTraceStartLocation` returns its location), `GetSelectionAxis`, `IsWithinSelectionVolume` and `CalculateCandidateScore` (both receive the candidate's bounds), `ShouldPerformLocalSelection`, `ResolveInteractionDuration` (to scale target durations) and `TickActiveInteraction`.

Logs use the `LogPEPInteraction` category.

## Ability integration

The core `PEPInteraction` module does not depend on GMC or GMAS. For local input or UI, call `RequestInteractWithInteractionTag` on the pawn interaction component, and `RequestCancelInteraction` on release for timed options. A client request returns `EPEPRequestResult::Submitted` while a reliable server RPC performs final validation and execution.

Server-side code that already runs on the authority may call `BeginInteractWithInteractionTag` (honours durations) or `ExecuteInteractWithInteractionTag` (executes at once, ignoring durations), and `CancelInteraction`.

### GMAS ability module

The `PEPInteractionGMAS` runtime module provides `UPEPInteractAbility`, a GMAS ability that drives the component on the ability owner:

1. The ability is ancillary (`bActivateOnMovementTick = false`), so it never runs in a replayed movement tick.
2. On the owning client (or listen-server host), it sends the selected target, the selected option and an interaction id it reserved on the component to the server with `UGMCAbilityTask_SetTargetDataInstancedStruct`, carrying an `FPEPInteractAbilityTargetData`. An empty selection is still sent, so both sides end the ability together.
3. When the data arrives on the authority, the ability calls `BeginInteractWithInteractionTag` with that id: an instant option executes immediately, a timed option starts. This is the only point where the ability changes game state, and it is guarded by an authority check outside any GMAS replay.
4. With `bCancelOnInputRelease` (default) and an input action, `UGMCAbilityTask_WaitForInputKeyRelease` cancels a timed interaction on release with `Reason.Interaction.Cancelled`.
5. The ability ends on each machine when the component reports the completion or cancellation of its own interaction id there, on input release, or on an invalid request. Outcomes of other requests on the same component are ignored. With `bCancelInteractionOnAbilityEnd` (default), an ability ended for another reason cancels its timed interaction with `Reason.Interaction.Interrupted`.

If the authority handles the request under another id (it joined an interaction already in progress, for instance), the authority's ability follows that id, and the client's ability ends when GMAS ends it from the server.

Tasks are created identically on client and server so that GMAS task ids stay paired. Override `ResolveInteractionComponent` when the component is not on the ability owner, and `MakeLocalTargetData` to request something other than the current selection.
