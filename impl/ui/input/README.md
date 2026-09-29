# UI input routing

The UI input domain owns platform-neutral hit testing and interaction state. It has no ImGui dependency. Native window focus, mouse capture, IME, clipboard, and native selection belong to the OS service boundary. The ECS UI adapter borrows available services and translates supported events into this domain; the router does not implement native service protocols.

`input.h` declares value types and bounds. `router.h` exposes the single-owner router. `router.cpp` owns queues, committed target publication, action lifetimes, and cancellation; `router_pointer.cpp` and `router_keyboard.cpp` own pointer and keyboard transitions separately. `router_gesture.cpp` owns copied pointer-gesture records and consumption, and `router_context_menu.cpp` owns copied secondary-button/keyboard menu triggers. The caller supplies a `Core::Alloc::GlobalArena` that outlives the router and its consumers.

## Events and ownership

The router accepts logical-coordinate pointer movement and wheel deltas, primary/secondary-button down/up, pointer leave/capture loss, key down/up, and window focus changes. Normalized keys cover focus and activation (Tab, Enter, Space, Escape), caret and selection editing (Left/Right, Home/End, Backspace/Delete), clipboard/history shortcut letters (A/C/X/V/Z/Y), vertical result navigation (Up/Down/PageUp/PageDown), and context-menu keys (Menu/F10), with Shift/Control/Alt modifiers. The ECS edit adapter translates native keys and character delivery into text commands; composition and clipboard protocols stay with the borrowed OS services. The ECS adapter retains native key ownership separately, including keys outside this normalized set, so a held press/repeat/up sequence keeps its original custom or scene owner across focus transfer.

Call `queue(event)` and then `process()` before offering a native event to another input owner. The ECS adapter processes each event immediately, so the first click over a UI target is consumed even before that target has keyboard focus. `process()` drains admitted events once and preserves already accepted actions across later calls.

`InputRoutingResult` reports event consumption, ongoing pointer/keyboard ownership, hover, focus, logical capture, activation overflow, and gesture overflow. `hitTest(point)` returns the top enabled target at that logical position. `wouldConsumePointer(point)` also recognizes an ongoing UI primary or secondary sequence and supports adapter routing of wheel or other buttons without making them primary activation events.

Hit targets contain a widget ID, declaration generation, rectangle, clip, paint order, and enabled/focusable/activatable roles. Hit testing intersects rectangle and clip, uses half-open bounds, chooses the greatest paint order, and chooses the latest declaration for equal paint orders. Invisible and disabled targets are skipped. The builder publishes a nonactivatable panel target beneath controls so clicks over panel space or disabled controls remain UI pointer events.

A primary press over a target starts logical capture and assigns keyboard focus when the target is focusable. A release activates only the same captured declaration when it remains activatable and is still the top target at the release position. Dragging outside does not activate. A scene-owned press does not transfer into a control when the pointer later enters it. Capture removal cancels activation but retains UI ownership until that primary sequence releases.

Tab and Shift+Tab traverse enabled, visible, focusable declarations in publication order and wrap. Ordinary user and modal popups trap traversal inside the top scope. An automatic combo popup opts into Tab exit: traversal within its children does not wrap, and crossing either boundary closes the combo and advances from its field in the parent scope after acceptance. Enter and Space activate on their first key-down. Native repeats and duplicate activation key-downs are consumed without adding another action. Tab may repeat navigation. Escape dismisses the top popup according to policy or clears ordinary focus/capture; editors first handle composition cancellation. Held releases retain their original owner. `ownsKey(key)` reports a normalized key whose down was consumed, including after focus moves elsewhere; adapters use it to prevent a held supported key's repeats from changing owners before release.

## Hover and context-menu intentions

`pointerKnown()`, `pointerPosition()` and `windowFocused()` expose copied native input state. Pointer leave, capture loss and focus loss invalidate known hover geometry until another pointer event. Tooltip timing uses this state together with the accepted anchor declaration, enabled state and popup token. A tooltip adds no hit target or popup scope and therefore takes no focus, capture or input ownership. Pointer-button/capture activity, clipping and popup precedence can suppress its paint independently of prepared visibility.

An accepted enabled target opts into menu requests with `HitTarget::contextMenu`. Secondary down addresses the top eligible hit, redirecting a visible list part to its owning host when appropriate. Menu or Shift+F10 addresses the focused eligible anchor. Pointer requests copy the press position; keyboard requests copy the accepted anchor's lower edge. The first press creates one `ContextMenuAction`; repeats do not replay it, and secondary input never creates primary activation or a primary gesture.

`ContextMenuAction` retains the anchor ID, declaration and accepted layout generation, sequence, popup/control tokens, position and keyboard origin. `consumeContextMenu(id, declarationGeneration, action)` delivers one matching request. Removing, disabling, hiding, rebinding or changing the anchor's control/popup lifetime prunes old requests and permanently retires their held intention. Secondary releases and held menu-key releases keep their original owner even when the menu opens or closes before release. Outside secondary presses follow the same popup dismissal barrier as primary presses.

Once open, a context menu uses the shared copied list navigation, keyed activation and scrollbar intentions. Cursor movement alone does not dispatch a command. Disabled/empty results cannot submit, and source/state changes fence old intentions. Escape, outside dismissal and native focus loss close through popup policy. Focus/capture clear on native loss, while closing accepted popup scopes can retain keyboard ownership until their closed replacement layout is accepted; `wantsKeyboard()` alone is not a native-focus indicator.

## Pointer gestures

A hit target opts into continuous pointer gestures with `pointerGesture`. A primary press copies its committed rectangle and `gestureReference`; an empty reference uses the target rectangle. Window title/grip targets supply the full displayed window bounds as their reference. The committed layout validates and owns these values, so later CPU/GPU candidates cannot change a press baseline.

`PointerGesture` contains a lifetime-stamped `InputActionId`, logical press origin/current position, the copied target/reference rectangles, and active/completed state. `consumePointerGesture()` delivers at most one pending update for a press. Movement coalesces within that record; consumption of an active update retains the baseline and waits for new movement or release. A complete press/move/release before any callback remains available once. Multiple presses retain their order and independent displayed references.

The router prunes gestures when their declaration disappears, changes kind/lifetime, becomes disabled, or stops accepting gestures. Context consumption additionally validates the current root and declaration. Capture loss discards an unfinished gesture and clears pointer ownership; completed gestures and activations survive. Window behavior validates a candidate before publishing host geometry and starts a new press from `referenceRectangle`.

## Layout and action publication

`commitTargets(targets, count, generation)` requires a nonzero generation newer than the committed generation. It validates all target identities, declaration lifetimes, and finite nonnegative geometry before swapping the complete target list. Duplicate IDs, malformed descriptions, invalid generations, and target overflow reject the entire publication and preserve the accepted layout and interaction state. The router retains its own copies rather than pointers into transient builder storage.

A prepared CPU layout is not an input layout. The owning context pairs hit targets with the same frozen draw generation. The host publishes that generation only after its exact output has been accepted and successfully presented. GPU retries reuse frozen paint and do not rerun callbacks or drain accepted actions into a newer CPU frame. Input continues using the previous committed layout during preparation and GPU work.

An `InputActionId` contains the stable widget ID, declaration generation, committed layout generation, and a monotonically increasing sequence. `InputAction` also records pointer or keyboard origin. It retains neither callbacks nor source pointers. `consumeActivation(id)` removes at most one action whose target still matches the current declaration lifetime. Changing, removing, hiding, or disabling a target prunes its stale actions.

Cancellation has distinct meanings:

| API or event | Effect |
| --- | --- |
| `clearFocus()` | Transfers keyboard focus without discarding actions, logical capture, or held-key ownership. |
| `invalidateTarget(id)` | Removes that committed declaration and its actions/focus/capture immediately without advancing layout generation; held releases retain their owner. |
| `PointerLeave` | Clears known hover geometry while keeping logical capture and held-input ownership. |
| `PointerCaptureLost` | Cancels the unfinished pointer capture/gesture and held pointer state; completed actions/gestures and keyboard focus/ownership survive. |
| `FocusLost` | Cancels actions, hover, focus, capture, and held-input state while preserving the committed target descriptions. |
| `FocusGained` | Marks the native window focused without reopening popups, restoring focus or inventing pointer geometry. |
| `reset()` | Cancels interaction, queued events, and the committed layout for resize, device, or root invalidation; action sequences never restart. |

Stable widget IDs derive from the host root identity and lifetime generation plus explicit scope/key hashes in `../id.h`. Labels and model values do not form identity. The retained context in `../context.h` validates current declaration lifetimes and retires removed ECS roots while a GPU frame remains pending.

## Control hosts and visible parts

`ControlToken` combines state input generation, source instance and source revision. A navigable/scrollable host is one focusable declaration. Its visible row and scrollbar targets carry the same token, owner ID and declaration lifetime, plus an optional stable key. Publication validates those references and popup layers atomically. Parts redirect focus to the host and do not add Tab stops. Removing a host removes its parts immediately.

`ControlAction` copies wheel, navigation, Submit and keyed Activate intentions into a bounded 256-entry ordered queue. Wheel targets the top accepted hit and its scrollable owner; an unrelated overlap or popup blocks lower lists. Wheel step, page row count and maximum come from accepted geometry. Navigation may repeat, while Enter/Space submit once per press. The initial key-down pins its host/control/popup owner until release; losing that owner permanently retires the navigation intention while preserving release consumption. `fenceControl()` disables old host/part interaction and prunes stale actions, capture, focus and gestures when a state/source lifetime changes.

Thumb gestures copy the accepted maximum with their track and thumb rectangles. Their identity sequence retains the press baseline; `updateSequence` orders the latest coalesced movement/release against other copied actions. A replacement list can request guarded `focusOnCommit` after a source change, but native focus loss or an active higher popup prevents that focus restoration. No router target or action borrows a list source or model.

## Keyboard delegation from editors

An enabled text editor can explicitly bind `keyboardOwner`, `keyboardOwnerDeclarationGeneration` and `keyboardControl` to an accepted navigable host in the same popup and paint layer. Up/Down/PageUp/PageDown and Enter then enqueue ordered control intentions while focus remains with the editor. Home/End, Left/Right and Space retain editor behavior. The editor host still handles Enter and IME composition; a composite control must gate its copied Submit intention on a noncomposing editor submission.

Delegated actions and held keys copy both the source editor and destination host lifetimes. Removing, disabling, rebinding or replacing either one permanently retires the original held intention; restoring an old binding cannot revive it. Publication validates the complete binding atomically. Composition can remove delegation while leaving the editor enabled for native text services.

## Bounds and failure behavior

The router admits at most 4,096 targets, 256 queued events, 256 pending activations, 256 pointer gesture records, 256 control actions and 256 context-menu actions. Its two target/lookup buffers and event/action vectors reserve those bounds from the owner arena when constructed. Sorted lookup tables avoid quadratic duplicate detection during publication.

`queue()` rejects malformed or excess events before insertion. The host must handle a rejected event; the ECS adapter invalidates input rather than retaining potentially stuck state. An activation or context-menu request at its full action queue is dropped and reported through `activationOverflow`; freeing a slot later does not replay the dropped event. A full gesture queue similarly drops the new gesture and reports `gestureOverflow`. Accepted action/gesture sequences remain unique across cancellation and reset.

The router is owned by one CPU execution context. It does not synchronize concurrent event producers or layout publication; a host serializes those operations at its UI boundary. Draw snapshots own rendering data and immutable font images, while callbacks and application values remain outside GPU work.

## Popup ancestry and activation

Every nonempty `PopupScope::parent` must identify an earlier registered, lower-layer scope with an exact widget/declaration/state/open token. Registration reserves layer order independently of activation; Context keeps a bounded parent activation stack for declarations and deferred painting. Geometry updates retain identity, parent and layer. Eight scopes is the frame limit, including automatic combo and context-menu children.

The top accepted scope owns pointer routing and focus traversal. Dismissing a child affects its subtree and restores an eligible parent after accepted removal. Closing, fencing or retiring an ancestor retires descendant targets, queued intentions and capture together while preserving unrelated top-level scope chains. Removing and recreating an old target cannot revive its original held intention; its release remains consumed by the original owner. Native focus loss cancels the entire family and fences delayed acceptance.

`popupCount()`, `topPopupToken()` and `popupScope(token)` are read-only observations of accepted copied scopes. A closing scope remains observable until replacement publication or retirement. Returned target/scope pointers are borrowed only until publication, reset or declaration retirement.
