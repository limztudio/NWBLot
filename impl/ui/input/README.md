# UI input routing

The UI input domain owns platform-neutral hit testing and interaction state. It has no ImGui dependency. Native window focus, mouse capture, IME, clipboard, and native selection belong to the OS service boundary. The ECS UI adapter borrows available services and translates supported events into this domain; the router does not implement native service protocols.

`input.h` declares value types and bounds. `router.h` exposes the single-owner router. `router.cpp` owns queues, committed target publication, action lifetimes, and cancellation; `router_pointer.cpp` and `router_keyboard.cpp` own pointer and keyboard transitions separately. `router_gesture.cpp` owns copied pointer-gesture records and consumption. The caller supplies a `Core::Alloc::GlobalArena` that outlives the router and its consumers.

## Events and ownership

The router accepts logical-coordinate pointer movement, primary-button down/up, pointer leave/capture loss, key down/up, and window focus loss. Its current normalized key set is Tab, Enter, Space, and Escape. Native key codes, character events, modifiers beyond Shift, text editing, shortcuts, and composition are adapter or future control concerns. The ECS adapter retains native key ownership separately, including keys outside this normalized set, so a held press/repeat/up sequence keeps its original custom or scene owner across focus transfer.

Call `queue(event)` and then `process()` before offering a native event to another input owner. The ECS adapter processes each event immediately, so the first click over a UI target is consumed even before that target has keyboard focus. `process()` drains admitted events once and preserves already accepted actions across later calls.

`InputRoutingResult` reports event consumption, ongoing pointer/keyboard ownership, hover, focus, logical capture, activation overflow, and gesture overflow. `hitTest(point)` returns the top enabled target at that logical position. `wouldConsumePointer(point)` also recognizes an ongoing UI primary sequence and supports adapter routing of wheel or nonprimary buttons without making them UI activation events.

Hit targets contain a widget ID, declaration generation, rectangle, clip, paint order, and enabled/focusable/activatable roles. Hit testing intersects rectangle and clip, uses half-open bounds, chooses the greatest paint order, and chooses the latest declaration for equal paint orders. Invisible and disabled targets are skipped. The builder publishes a nonactivatable panel target beneath controls so clicks over panel space or disabled controls remain UI pointer events.

A primary press over a target starts logical capture and assigns keyboard focus when the target is focusable. A release activates only the same captured declaration when it remains activatable and is still the top target at the release position. Dragging outside does not activate. A scene-owned press does not transfer into a control when the pointer later enters it. Capture removal cancels activation but retains UI ownership until that primary sequence releases.

Tab and Shift+Tab traverse enabled, visible, focusable declarations in publication order and wrap. Enter and Space activate on their first key-down. Native repeats and duplicate activation key-downs are consumed without adding another action. Tab may repeat navigation. Escape clears focus and logical capture while preserving ownership of already held input until release. `ownsKey(key)` reports a normalized key whose down was consumed, including after focus moves elsewhere; adapters use it to prevent a held supported key's repeats from changing owners before release.

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
| `PointerLeave` | Clears hover while keeping logical capture and held-input ownership. |
| `PointerCaptureLost` | Cancels the unfinished pointer capture/gesture and held pointer state; completed actions/gestures and keyboard focus/ownership survive. |
| `FocusLost` | Cancels actions, hover, focus, capture, and held-input state while preserving the committed target descriptions. |
| `reset()` | Cancels interaction, queued events, and the committed layout for resize, device, or root invalidation; action sequences never restart. |

Stable widget IDs derive from the host root identity and lifetime generation plus explicit scope/key hashes in `../id.h`. Labels and model values do not form identity. The retained context in `../context.h` validates current declaration lifetimes and retires removed ECS roots while a GPU frame remains pending.

## Bounds and failure behavior

The router admits at most 4,096 targets, 256 queued events, and 256 pending activations, and 256 pointer gesture records. Its two target/lookup buffers and event/action vectors reserve those bounds from the owner arena when constructed. Sorted lookup tables avoid quadratic duplicate detection during publication.

`queue()` rejects malformed or excess events before insertion. The host must handle a rejected event; the ECS adapter invalidates input rather than retaining potentially stuck state. An activation at a full action queue is dropped and reported through `activationOverflow`; freeing a slot later does not replay the dropped event. A full gesture queue similarly drops the new gesture and reports `gestureOverflow`. Accepted action/gesture sequences remain unique across cancellation and reset.

The router is owned by one CPU execution context. It does not synchronize concurrent event producers or layout publication; a host serializes those operations at its UI boundary. Draw snapshots own rendering data and immutable font images, while callbacks and application values remain outside GPU work.
