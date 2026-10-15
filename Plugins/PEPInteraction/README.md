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

## Ability integration

The interaction backend does not activate gameplay abilities. For local input or UI, call `RequestInteractWithInteractionTag` on the pawn interaction component. A client request returns `EPEPRequestResult::Submitted` while a reliable server RPC performs final validation and execution. A server-side gameplay ability may call `ExecuteInteractWithInteractionTag` directly.
