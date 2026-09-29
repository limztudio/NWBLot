# CPU UI painting

`nwb_ui` provides `NWB::Impl::Ui::PaintBuilder` and `DrawSnapshot` through `paint.h`.
The caller supplies a `Core::Alloc::GlobalArena` that outlives the builder, every frozen snapshot,
and all consumers of those snapshots. Storage that crosses an asynchronous task boundary must have
an owning lifetime covering the complete task; use the UI owner's arena for that work.

Call `begin()` with display metrics, a frame generation, a skin generation, an already loaded and
validated `UiSkin`, and its matching typed `AssetRef<UiSkin>`. The builder copies the skin's region
descriptors and atlas binding metadata for this frame. `reserve()` accepts an expected quad count;
a complete nine-slice can emit nine quads.

Positions, dimensions, and clip rectangles use logical units with a top-left origin. The initial
clip is the logical display extent. `pushClip()` intersects its rectangle with the current clip;
every push must have a matching `popClip()` before `freeze()`. A pop at the initial clip returns
`false`. Geometry is trimmed to the current clip and UVs are interpolated to match. The snapshot
also records each command's logical clip and the display's pixel scales; the GPU renderer applies
those scales when converting positions and scissors to framebuffer pixels.

This example belongs in the owning UI caller after loading `skin` and `skinRef`:

```cpp
#include <impl/ui/paint.h>

using namespace NWB;
using namespace NWB::Impl;

Core::Alloc::GlobalArena uiArena(Name("runtime/ui"));
Ui::PaintBuilder paint(uiArena);
paint.begin({ 800.0f, 600.0f, 1.5f, 1.5f }, 1u, 1u, skinRef, skin);
paint.reserve(9u);
paint.pushClip({ 20.0f, 20.0f, 300.0f, 180.0f });
if(!paint.drawRegion(Name("panel.normal"), { 20.0f, 20.0f, 300.0f, 180.0f }))
    paint.fillRect({ 20.0f, 20.0f, 300.0f, 180.0f }, { 0.1f, 0.1f, 0.1f, 1.0f });
if(!paint.popClip())
    NWB_FATAL_ASSERT_MSG(false, NWB_TEXT("The panel clip must be present"));
Ui::DrawSnapshot snapshot = paint.freeze();
// Keep uiArena alive while snapshot or any submitted consumer remains alive.
```

`drawRegion()` resolves a semantic region name and its sprite/nine-slice mode. It returns `false`
when the region is missing so the caller can choose a fallback or report the missing skin part.
An existing region that is fully clipped produces no geometry and still returns `true`. Atlas UVs
use top-left pixel coordinates divided by the saved atlas extent.

Nine-slice corner sizes are the authored border pixels divided by `referenceDensity()`. Opposing
borders shrink in proportion when the destination is smaller than their combined logical size;
the center collapses on that axis. UV boundaries keep the authored source border coordinates.
The painter does not enforce the region's content padding or minimum size; layout owns those metrics.

Input `Color` values are linear RGB with finite straight alpha in `[0, 1]`. Emitted vertex RGB is
multiplied by alpha. Skin artwork and its color-space/coverage interpretation come from the typed
texture asset; the skin shader combines texture coverage and tint consistently with this
premultiplied vertex contract.

`freeze()` moves the geometry, indices, ordered commands, display metrics, frame generation, and
skin binding into a move-only snapshot exposed through const views. Start a new frame with `begin()`
before painting again. Reusing the builder or changing the original skin cannot alter a frozen
snapshot. Only adjacent draws with identical layer, material and clip merge; painter order is preserved
within each layer, and `freeze()` orders command ranges by layer.

The saved `AssetRef<UiSkin>` and `AssetRef<Texture>` identify assets. They do not retain loaded asset
objects or resolved GPU resources. The explicit skin generation identifies the metadata revision;
the GPU layer retains the matching resolved texture revision, descriptors, and upload
storage until GPU completion.

The CPU toolkit also provides scoped stable IDs, retained declaration lifetimes (`state/`),
row/column/overlay measure and arrange (`layout/`), committed-layout input routing (`input/`),
font shaping (`text/`), owned Unicode edit state and commands (`edit/`), and skinned windows, panels, popups/modals, labels, separators, buttons, checkboxes and single-line edit boxes (`Builder`).
`nwb_ui_gpu` owns GPU uploads, resource retention,
and offscreen rendering in `impl/ui/gpu/`. `impl/ecs_ui/` connects the CPU UI to ECS and borrowed OS services,
`core/os/` owns clipboard/native selection and text-input/IME services, `impl/assets_ui_skin/` owns skin validation/cooking,
and `impl/ecs_render/` owns final composition with the scene. The GPU module consumes frozen snapshots
without invoking callbacks or accessing live ECS data.

## Declarative controls

`Context` owns retained identity and input. `Builder` borrows that context, the paint builder,
text service and loaded skin; neither CPU type knows ECS, native windows or ImGui.
`nwb_ui_widgets` contains the builder and control painting, while `nwb_ui` owns ID/state/layout/input.
The same UI arena outlives these objects, text/font pages and every frozen draw snapshot.

A host begins a uniquely numbered frame and a generational `WidgetRoot`, then declares controls:

```cpp
if(ui.beginPanel("settings", { 18.0f, 18.0f, 320.0f, 220.0f })){
    if(!ui.label("caption", "Settings"))
        context.fail();
    if(ui.checkbox("enabled", "Enabled", enabled))
        saveEnabledValue(enabled);
    Ui::WidgetOptions options;
    options.enabled = enabled;
    if(ui.button("apply", "Apply", options))
        applySettings();
    if(!ui.endPanel())
        context.fail();
}
```

Use keys representing model identity. Labels may change without changing keys. A root includes its
host lifetime generation; nested rows/columns add scopes. Duplicate keys in one scope or unbalanced
panels/scopes reject the build. Removed or hidden roots retire their retained identities/actions.
Recreating a widget with the same key gets a new declaration lifetime, so old input cannot activate it.
Checkbox values remain references to host model values; frozen paint contains their resulting appearance.

Panel and control dimensions are logical units. Container dimensions use fixed, content or weighted
stretch policies; intrinsic dimensions come from shaped text and skin metadata. Button metrics take
maximum padding/minimum sizes across normal, hover, pressed and disabled regions to avoid state-dependent
layout movement. Checkbox metrics include the square and its label gap. State artwork falls back to the
normal region; the normal panel/button/checkbox parts are required. Optional mark/focus regions add overlays.

A panel blocks pointer input through its empty area. The layout's ancestor clip intersects both painting
and hit testing. Enabled buttons and checkboxes participate in Tab/Shift+Tab navigation and initial
Enter/Space activation; repeated key-downs do not repeat activation. A disabled control cannot consume an
activation and leaves its panel as the pointer barrier. Captured primary input stays owned through release,
even when the target is removed; release outside the original visible target cancels activation.

`finishFrame()` prepares a hit-layout candidate but does not publish it. The host must freeze matching paint,
submit it, and call `commitFrame()` only for the exact generation consumed by final GPU output and accepted
by native presentation. Native presentation acceptance is a queueing boundary, not monitor scan-out proof.
Pending/rejected GPU work does not publish new hit geometry or rebuild model callbacks. Input is processed
against the last committed geometry, producing lifetime-stamped action values; actions are consumed once
when declaring the corresponding live/enabled control. These queues are bounded, as documented in `input/`.

Resize, scale changes and device invalidation reset input publication. The ECS adapter additionally validates
host-root lifetime before native input and frame builds. It connects edit-box declarations to borrowed OS
text-input and clipboard/selection services. Lists and compound controls remain subsequent work.

## Windows and separators

`Builder::beginWindow(key, title, WindowState&, WindowOptions)` opens one scoped window and returns whether its content is visible. Call `endWindow()` even when the window is collapsed; a failed begin marks the context failed. Do not nest windows/panels or retain a builder's open scope beyond the owning callback. `WindowState` is host-owned and must remain alive through that matching end. Frozen snapshots never reference it.

`WindowOptions` supplies the initial bounds, minimum size, optional first-use content sizing, layout direction, and independent movement/resize/collapse capabilities. Initialize only once, then keep the same model to preserve placement and collapse state across ordinary frames. Placement is constrained so the window's title stays reachable after viewport changes; cross-run persistence belongs to the application. The migrated Testbed window retains its original initial position and content, while its UI is prepared and composed through the custom GPU layer.

The window domain separates behavior (`window_behavior.cpp`), metrics/geometry (`window_layout.cpp`), declarations (`builder_window.cpp`), and paint/chrome targets (`window_paint.cpp`). Title and resize gestures carry the exact committed reference geometry, so a pending candidate cannot shift the gesture's baseline. Coalesced updates retain a complete press/move/release sequence until the next declaration; each update is consumed once. Pointer loss cancels an active gesture, while an ordinary release keeps completed movement.

`separator(key, SeparatorOptions)` participates in layout and draws a named skin part. It supports horizontal/vertical directions, fixed/content/stretch length and skin-derived or explicit logical thickness. Required parts come from the selected atlas (`window.normal`, `window.title`, `window.collapse`, and separator names); missing required parts reject the candidate. An optional `window.resize` sprite can replace the default grip, which uses the selected atlas's white sprite. `windowMetrics()` is available only inside an open window for callers needing the actual chrome geometry.

ImGui runtime, shader assets and vendor sources have been removed. Font shaping/rasterization remains in the independently owned FreeType/HarfBuzz text service; clipboard, native selection and IME remain borrowed OS services. Lists, combo boxes, tooltips and context menus are implemented in the increments below; numeric/multiline editors remain later work.

## Popups and modals

`Builder::beginPopup(key, PopupState&, PopupOptions)` declares a popup after other panel/window scopes have ended. It returns whether popup content is visible. Call `endPopup()` only when begin returns `true`; a closed popup opens no scope. Keep the application-owned `PopupState` alive through that matching end. The state is noncopyable/nonmovable and retains open state, prepared placement, an instance lifetime and an open generation. Call `open()` from a live trigger action and `close()` from a content action or application policy. A new opening has a new token, so pending actions from an earlier opening cannot choose content in the new one.

```cpp
// menuState.open() comes from the trigger inside an earlier, already ended panel.
Ui::PopupOptions options;
options.anchor = triggerRectangle;
options.size = { 220.0f, 160.0f };
if(ui.beginPopup("menu", menuState, options)){
    if(ui.button("first", "First choice")){
        selectedChoice = 1u;
        menuState.close();
    }
    if(!ui.endPopup())
        context.fail();
}
```

`PopupOptions` chooses Below/Above/Right/Left/Center placement in logical coordinates, a gap, desired size, modal mode, outside-click dismissal, Escape dismissal and autofocus. Anchored placement flips on its requested axis when the opposite side offers more room, clamps to the viewport, and shrinks dimensions that exceed the viewport. `placement()` exposes the newly prepared bounds, viewport and resolved side; input still uses the separately committed geometry. Oversized child content is clipped rather than automatically scrolled.

`popupStyle()` supplies the background, fallback, padding and linear backdrop color. The default background is `popup.normal`, with `panel.normal` as a fallback in the selected skin. Atlas padding and style padding combine by taking their maximum on each edge. A modal paints its viewport backdrop before its own background and children; the default is black with alpha 0.4. Label, button, checkbox, row/column and edit-box declarations reuse the existing domains inside a popup. Closing or reopening during the body fences later child actions and model loans, and suppresses the old opening's buffered Builder paint and targets. Direct low-level paint already emitted into the overlay is not rolled back. Popup scopes nest while preserving each parent layout and row/column container. Child layouts take no space from the parent, escape its clip, and restore its declaration and paint scopes at child end. Keep every child state, editor and source loan alive through the outermost popup end. Builder subtree painting waits until that outermost end; closing or reopening any ancestor suppresses the old subtree before deferred source callbacks.

The accepted top popup traps Tab/Shift+Tab within eligible children and owns pointer input. Autofocus chooses its first eligible child only when the matching layout is accepted. Explicit close/dismissal restores saved focus at accepted publication only when the saved declaration remains eligible. Declaration/root retirement removes its ownership immediately and may restore an eligible lower target then. Outside/Escape policies control dismissal, while the dismissing held sequence and release stay consumed by UI. An outside press cannot click through into an underlying control or the scene. Disabling outside dismissal keeps the popup open and still consumes that input. Native focus loss closes popups and cancels restoration into an unfocused window. Context captures the router's focus-loss generation at frame preparation: delayed popup acceptance across a loss remains closed even if native focus has since returned. Focus gain alone does not restore a closing popup; a fresh opening may autofocus.

`PopupToken` combines widget identity, declaration lifetime, state instance and open generation. `Context` retains copied scopes and stamps popup targets/actions/capture with that token. Prepared scopes publish with the exact accepted paint/presentation generation, and a reopened/replaced state fences old scope actions. Popup state and child models are borrowed only during declaration; snapshots contain owned geometry and command layers.

`PaintBuilder::beginOverlay(layer)` and `endOverlay()` support up to 16 balanced nested low-level overlay scopes. Base commands use layer zero; a positive overlay layer uses the viewport clip and restores the exact prior clip and layer when ended. Freezing orders command ranges by layer while preserving painter order within a layer and existing vertex/index/image ownership. Popup layers therefore render above later ordinary host roots. All layers still rasterize into the same independent GPU UI texture; popup layering adds no per-popup GPU target or task.

Fixed-height virtualized lists and selectable rows now use these popup/input contracts. Combo boxes now compose these contracts below. Tooltips, context menus and nested popup layouts use these same contracts. Native Windows captures and Linux target syntax checks have different qualification scope: syntax checks with Linux headers do not establish native Linux linking or compositor execution, and synthetic text events do not establish live IME behavior.

## Single-line edit boxes

`Builder::editBox(key, EditModel&, EditBoxState&, EditBoxOptions)` declares a selectable single-line editor. The application owns the persistent model and state in its UI arena. Keep both alive through the matching `endPanel()`, `endWindow()` or `endPopup()`. `EditBoxOptions` supplies fixed/content/stretch sizing, `enabled`, and `readOnly`. `EditBoxResult` reports admission, committed-text or selection changes, submit/cancel intent, focus, and native preedit caret visibility. Enter reports submit intent. Escape cancels active preedit first while keeping the editor/popup open; a subsequent editor cancel asks the host to dismiss its accepted popup or releases ordinary editor focus. Restoring an application value after cancellation belongs to the application.

Construct the model and state once, then use them inside an existing panel/window callback:

```cpp
Ui::EditBoxOptions options;
options.height = { Ui::LayoutSizePolicy::Fixed, 40.0f };
const Ui::EditBoxResult result = ui.editBox("name", nameModel, nameState, options);
if(!result.valid)
    context.fail();
if(result.submitted)
    saveName(nameModel.text());
```

`EditModel` stores UTF8 bytes and grapheme selections. Each instance has a stable lifetime generation; accepted external `setText()` calls also advance an external revision, even when the bytes are unchanged. This fences queued input after replacement or an explicit reset. `EditBoxState` stores scroll, blink state and the newly prepared placement. Its `placement` becomes available after the matching container end; the ECS adapter keeps a separate displayed copy and admits it for pointer/IME geometry only after the exact matching presentation is accepted.

The widget domain separates immutable text/cluster geometry (`edit_box_layout.cpp`), paint (`edit_box_paint.cpp`), builder declarations (`builder_edit_box.cpp`), and the `interface IEditBoxHost` contract (`edit_box_state.h`). `EditBoxView` owns copied display bytes, shaped text and retained font versions. It replaces the selected committed range with transient preedit for display, paints selection/caret/preedit underline, interpolates grapheme caret stops inside LTR ligatures, and scrolls horizontally to keep the caret visible. Ancestor, frame and content clips apply to paint and hit geometry. The default caret is one physical pixel wide; text, control dimensions and scroll remain logical units.

`Builder::editStyle()` supplies semantic edit background names, padding and text/selection/caret/preedit colors. Padding and minimum size take the maximum across normal, hover, focused and disabled regions and combine with style padding, keeping text and layout stable when state changes. The default skin provides `edit.normal`, `edit.focused`, `edit.disabled` and `focus.overlay`; absent hover artwork falls back to the normal region. A 40-pixel field accommodates the default 16-pixel font and eight-pixel vertical skin padding. Fixed smaller fields deliberately clip their content.

`impl/ecs_ui/` installs `UiEditBoxHost` and borrows Frame-owned OS services. A toolkit-only caller may install its own `IEditBoxHost`; the CPU widget itself has no native API or ECS dependency. The adapter preserves native commit/key/selection event order, applies events only while the corresponding live model is lent, and retains copied events and snapshots rather than model pointers. Focus, declaration/model generations and external revisions fence late native/cut/paste mutation. Copy publication owns immutable selected bytes and may complete after later edits, focus transfer or widget removal. Frozen paint and GPU tasks own their data and never access edit models.

The editor supports click/drag selection, Shift extension, Tab/Shift+Tab focus, grapheme arrows and deletion, Home/End, Ctrl word navigation/deletion, Ctrl+A/C/X/V, Ctrl+Z/Y and Ctrl+Shift+Z. Read-only controls remain focusable/selectable and permit copy; disabled controls do not edit or take focus. Native preedit owns its editing keys and Enter until the IME completes or cancels it. Clipboard paste normalizes line breaks and tabs to spaces and validates the resulting byte limit before mutation; cut deletes only after a successful OS write. Native primary-selection publication and middle-button paste use backend capabilities. Middle-button paste targets only the already focused control and replaces its current selection; it does not move focus or reposition the caret to the pointer.

This increment provides one line with LTR cluster geometry. Paragraph bidi, RTL editing, wrapping, numeric validation and multiline editing are separate work. Native preedit rendering and service borrowing are implemented; synthetic character/event tests do not establish live Korean IME or native Linux compositor qualification.

## Fixed-height virtualized lists

`Builder::virtualList(key, source, state, options)` borrows an `interface IListDataSource` and persistent `ListState` through the matching scope end. The source provides stable keys, revisions, efficient index lookup and enabled-row search. `ListOptions` sets sizing, fixed row height, wheel rows and navigation selection policy. The result separates selected-key changes from pointer/Enter/Space activation. Arrow/Page/Home/End navigation skips disabled rows and ensures the cursor is visible; wheel and scrollbar dragging preserve selection.

Only clipped visible rows are shaped, painted and published as parts of one focusable host. The state retains stable selected/cursor keys and double scroll offset; reorder/removal and source replacement reconcile those values without scanning the dataset. Source/state changes fence queued intentions, capture, focus and held navigation. Accepted targets carry copied tokens and press geometry, and frozen GPU work retains no source/model pointer. See [the widget contract](widgets/README.md#keyed-lists-and-scrolling) for source lifetime rules, bounds, skin roles and behavior details.

## Combo selection

`Builder::comboBox(key, source, ComboState&, ComboOptions)` uses the same keyed source as `virtualList`. Keep source/state alive and unchanged through the enclosing panel/window end. The component declares its field and internal popup/list identities, consumes input from the last accepted layout during declaration, and automatically paints the popup after arranging the containing scope. `ComboResult::committed` and `selectionChanged` are available immediately; the field text reflects that accepted selection in the same callback.

The popup previews enabled rows with Arrow/Page/Home/End, wheel and thumb scrolling. Enter/Space or a row release commits; Escape/outside/native focus loss cancels. Selected keys remain stable through reorder, and openings ensure the committed row is visible without walking the full dataset. Omission/recreation, source replacement and explicit state changes retire previous input lifetimes. Skin appearance, exact loan/lifetime rules, popup Tab behavior and current composition limits are documented in [the combo contract](widgets/README.md#combo-boxes).

## Tooltips and context menus

Declare `tooltip(key, anchorKey, text, TooltipState&, TooltipOptions)` or
`contextMenu(key, anchorKey, IListDataSource&, ContextMenuState&, ContextMenuOptions)`
after its anchor control in the same panel/window/container. Stable keys identify the
attachment and anchor independently of their labels. Both attachments also work in plain and nested user popup scopes.

A tooltip measures owned text and starts a configurable delay when the accepted
anchor is hovered. Leaving, a held pointer button, capture, disabled state, native
focus loss, changed attachment/options or a higher popup resets that delay. Its
clamped/flipped overlay has no hit target or focus scope, so an editor can keep
receiving native text while help is visible. `TooltipStyle` resolves `tooltip.normal`
with an explicit `panel.normal` fallback in the selected skin.

A context menu opens at an accepted secondary-button press, Menu key or Shift+F10.
The trigger copies anchor declaration, popup/control lifetimes and client position;
secondary presses never activate the primary control. Menu commands reuse stable-key
virtualized lists, enabled-row navigation, scrollbars and popup focus/dismissal.
`ContextMenuResult` reports opening/closing and the activated command key. Enter,
Space or a row click activates one enabled command and closes; Escape, outside
press and focus loss dismiss. Source revision changes retire old input and reset
preview/scroll; source replacement closes the menu. Disabled/hidden anchors cannot
open or retain it.

The application keeps list/combo/menu sources and their models alive and unchanged
through the enclosing `endPanel()`, `endWindow()` or `endPopup()`. Deferred painting
checks every source before final callback-free model epoch checks and releases all
loans together. A callback that changes another borrowed model rejects the candidate
and preserves the application change. Closing a plain popup suppresses its buffered
body and attached tooltip. Frozen GPU snapshots and accepted input targets retain
only owned geometry, text, resources, copied keys and lifetime tokens.

## Nested popup scope ownership

`BuilderScopeFrame` holds each independent layout, declaration list and borrowed control state. Ended child frames remain at stable arena-owned addresses through the outermost `endPopup()`. Builder checks every surviving family source before a final callback-free model and ancestor-lifetime pass, then releases all loans together. A source callback that changes an already painted parent, child or sibling rejects the candidate without rolling back the application change. Ancestor checks between callback boundaries stop later stale subtree work. Builder rejects reentrant declaration, scope reset, and configuration mutation from source/edit-host callbacks during control preparation or final emission. Rejection preserves borrowed storage until validation and loan release finish, and rejects the candidate frame.

`Context::registerPopupScope()` reserves a declaration-order layer, `activatePopupScope()` temporarily selects its exact registered parent chain, and `updatePopupScope()` finalizes arranged geometry without changing identity, parent or layer. Automatic combo/menu popups reserve during declaration and paint after their owner layout. The frame limit is eight registered popup scopes, including automatic children. `PopupScope::parent` copies the exact parent token; missing, later, malformed or changed parent lifetimes reject publication. Popup models close stale openings when a parent lifetime changes, while a deliberately fresh opening can bind to the new parent.

`Builder::input()` offers read-only accepted input observations. Router `popupCount()`, `topPopupToken()` and `popupScope(token)` expose accepted scopes; target/scope pointers must not be retained across publication, reset or declaration retirement. The top accepted popup traps focus and input. Child Escape/outside dismissal restores an eligible parent focus after accepted removal; ancestor retirement fences every descendant while retaining unrelated top-level chains and original held-release ownership.

## Numeric Builder editors

`Builder::integerEdit()` and `floatEdit()` borrow a typed numeric model and an `EditBoxState`, using `IntegerEditOptions`/`FloatEditOptions` for edit geometry, enabled/read-only policy and typed bounds. `NumericEditBoxResult` separates ordinary edit observations from committed/rejected/clamped/restored numeric outcomes. The component action sink runs before text snapshot preparation, so Submit, Blur, Cancel and Abandon apply in input order. A configured host must implement `editActions()`; unsupported hosts reject the declaration explicitly. A Builder without an edit host can still prepare the current numeric state for painting.

The model and visual state remain lent through the enclosing panel/window or outermost popup end. Separate typed loan records validate both numeric epochs and draft text/selection/composition epochs around preparation, publication and the final callback-free family pass. Frozen paint and hit targets own their data. Application changes to a live loan reject that frame candidate while preserving the application change. Numeric controls use the existing skin's edit states and text/caret/selection colors.
