# PEPInventoryUI

`UPEPInventoryUISubsystem` is automatically created per local player. It routes UI placement
checks and requests through that player's `UPEPInventoryComponent`; it does not replicate,
open remote inventories, or move widgets. UMG provides the drag operation lifecycle.

## Registration and lifetime

From a widget, use its **Get Owning Player** with the Local Player Subsystem getter and select
**PEPInventoryUISubsystem**. Do not use player index 0: each split-screen player has a separate
subsystem. Do not Construct Object or create a subsystem in a dedicated-server gameplay path.

Call **Register Inventory Component** once the local player's inventory is ready. Supply the
actual component from the pawn, player controller, or player state used by your game. The owner
must resolve to this subsystem's local player. An unavailable or foreign component is rejected;
the existing registration is preserved. Registering the same component is idempotent.

Register again after component replacement/respawn. Call **Unregister Inventory Component** when
the local inventory goes away. A controller notification preserves the registration when the
inventory still belongs to that controller. A different/null controller and subsystem
deinitialization detach the subscriptions. **Get Inventory Component** returns null for a destroyed/foreign owner. The
subsystem keeps a weak reference so it does not retain an old pawn inventory.

Subscribe once to **On Inventory Binding Changed**, **On Inventory Changed**, **On Session View
Changed**, and **On Request Resolved** on the subsystem. These relay the component's events. On a
binding change, rebuild the relevant local views. **Get Inventory Component** lets the panel read
the existing inventory/session query APIs. A session-view event can indicate either content or
session identity changes: compare the context identity before retaining a remote window.

## Container and drag contexts

Add **DestinationContext : PEPInventoryUIContext** to your container. At initialization, capture
**Get Player Inventory Context** for a local container, or **Get Transfer Session Context** for a
remote container. These getters return a None context if their store is unavailable. Use
**Is Context Current** to gate interaction.

Add **SourceContext : PEPInventoryUIContext** to `BP_PEP_DragDropOperation`, separate from the
existing `PEPItemDragData`. At drag start, copy the source container's DestinationContext.
Keep that captured context unchanged during the drag. Do not query a fresh source context in
OnDrop; that would silently reroute an old drag after a session change.

Contexts include an inventory binding identity and, for remote views, the server-created session
identity replicated owner-only by `GetTransferSessionId`. Closing/reopening a session (including
reopening the same remote inventory) or re-registering an inventory invalidates old contexts.
Normal item updates do not change identities. Replicated session data and its identity may arrive
in separate callbacks; treat intermediate previews as unavailable and refresh from session events.
`RequestPlacement` and the session windows pass the captured session ID to Take, Put, Move, and
Close requests; the server rejects an ID that no longer matches the active session.

## OnDragOver and OnDrop

The container still calculates CandidateCell/Destination with its own ParentItemId, SlotName,
grid geometry, CellSize, and the drag's grab offset/preview rotation.

**OnDragOver** calls **Preview Placement** on the subsystem:

- Source Context = operation.SourceContext
- Destination Context = container.DestinationContext
- Item Id = operation.DragData.Item.ItemId
- Destination = calculated PEPItemLocation

Break the returned **PEPItemResult** and use **Success** to choose green/red. Context mismatches
return `Reason_StateMismatch`. Structurally invalid inputs return `Reason_InvalidTarget`.

**OnDrop** recalculates Destination and calls **Request Placement** with the same inputs. This
repeats the local preview before sending a request. OutRequestId is zero if rejected before
submission. The returned enum distinguishes Failed, Submitted, and Executed. Server authority
still performs its existing checks; a green preview is not a guarantee of acceptance.

Do not move/remove widgets based solely on Submitted. Update local containers from inventory
changes and remote views from session-view changes. Subscribe to request results before calling
Request Placement: on authority, OnRequestResolved can fire inside that call, before it returns
OutRequestId. Preview rejection does not produce a request-result event; use PreviewPlacement's
ReasonTag to explain it if needed.

## Supported routes

| Source | Destination | Component preview/request |
|---|---|---|
| PlayerInventory | PlayerInventory | MoveItem |
| TransferSession | PlayerInventory | TakeFromSession |
| PlayerInventory | TransferSession | PutIntoSession |
| TransferSession | TransferSession | MoveInSession |

Multiple remote inventories are not supported by this subsystem. Remote sessions are opened
server-side through the existing interaction flow, on the `UPEPInventorySessionComponent` carried by
the player's PlayerController; the subsystem binds to that component when the controller changes and
exposes it through **Get Session Component**. Session requests resolve on that component's
`OnRequestResolved`, relayed here like the inventory's own. The binding ID is a local UI check. The session
ID is also carried by remote-operation RPCs as a stale-context guard; it does not replace
server-side permission, ownership, or distance checks.

## Widget ownership

`PEPInventoryUI` owns the inventory-facing drag workflow: it captures inventory/session contexts,
previews placement, submits placement requests, and coordinates drag lifecycle. Container widgets
calculate their own grid geometry and render the preview. The item widget/payload can be reused,
but it does not decide whether an inventory destination accepts the item.

`UPEPInventoryWindow` and `UPEPInventorySessionLayer` belong here because they carry inventory
context and manage the presentation of a transfer session. A future reusable floating-panel shell
may be supplied by `PEPCoreUI`; its inventory contents, store identity, and session lifetime stay
in this module. A future equipment slot drop target belongs to the equipment UI module, which
validates and requests equipment operations through its own domain API.

## Verification

Automation test: `PEP.Inventory.UI.RoutingAndContexts`. Exercises preview non-mutation, local
movement, taking/putting remote items, unavailable/foreign registrations, invalid placements,
stale contexts after reopen/unregister, controller changes, and subscription cleanup.

`PEP.Inventory.UI.ReplicatedCallbackTarget` verifies the receiving path using the component's
actual InventoryView after creation from a template. PostInitProperties and OnRegister restore
InventoryView/SessionView local callback targets; constructor-only initialization can be
replaced by native struct copies of component defaults. The test checks that PostReplicatedAdd
and PostReplicatedChange reach the subsystem listener, alongside the same-controller delegate
preservation covered by RoutingAndContexts. These tests drive callbacks without a live network;
retest the actual pickup in client/listen-server PIE after restarting the editor.

## Active drag commands across windows

The subsystem coordinates one active inventory drag per local player. It does not receive keys.
Each inventory window receives OnKeyDown (or OnPreviewKeyDown when interception is required)
and calls RotateActiveDrag. Use the owning player's subsystem for every window.

1. Add PEPInventoryUIDragOperation to BP_PEP_DragDropOperation's implemented interfaces.
2. Implement its RotateDraggedItem function: check rotation permission, update preview
   orientation/offset, refresh the floating visual and hover target, and return true when handled.
   Return false if the operation declines the input. The subsystem does not change item data.
3. After creating/initializing the operation in OnDragDetected, call BeginDrag(Operation).
   A different active operation is rejected; calling again with the same operation is idempotent.
4. In the focused window, route R to RotateActiveDrag; return Handled when it returns true.
   Ignore key repeat for a single rotation per press.
5. Drop and DragCancelled automatically call EndDrag. Explicit EndDrag(Operation) is available;
   it only clears a matching operation, so late cleanup from an old window cannot clear a new drag.

The subsystem holds a weak operation reference and detaches its lifecycle delegates at cleanup.
Inventory replacement/unregistration and subsystem deinitialization also clear the registration.
Clearing this registration does not cancel Slate's drag or remove its preview: window closure and
hover-target visual cleanup remain responsibilities of the UI. This does not extend remote-session
or placement routes. GetActiveDragOperation is available for UI coordination.

Automation test PEP.Inventory.UI.DragLifecycle covers routing, declined rotation, duplicate/rejected
registration, Drop/cancel cleanup, stale EndDrag calls, and inventory/subsystem cleanup.
## Project UI colors

Project Settings > PEP > Inventory UI configures ValidDropColor, InvalidDropColor,
PreviewOpacity, and HoverColor. These Config=Game defaults are project-wide, persisted in
DefaultGame.ini when saved in the settings editor, and do not depend on a map or replicate.

In a container, feed PreviewPlacement's Success to the subsystem's GetDropPreviewColor,
then apply the returned LinearColor with Image.SetColorAndOpacity. The returned alpha is
the selected color's alpha multiplied by PreviewOpacity; do not apply that opacity again.
A material preview can use the returned RGB and alpha through its color/opacity parameters.
GetUISettings exposes the current settings object to Blueprint, including the ordinary
HoverColor, whose alpha is independent of PreviewOpacity. Settings are read at each query;
existing visuals update the next time their tint is applied. Widget graphs must call these
nodes to use the palette; adding settings alone does not change existing image/material tints.
