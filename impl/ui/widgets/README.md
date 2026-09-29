# UI widgets

`Ui::Builder` declares panels and windows, scoped row/column containers, labels, buttons, checkboxes, separators, selectable rows and virtualized lists. It shapes text and arranges the content before emitting paint and hit targets in the same order. Application values remain in the host model; the GPU snapshot owns copied geometry and image references.

## Windows

Keep a `WindowState` in the host component or application model. Its bounds, initialized flag, collapsed flag, and pointer gesture baselines persist across callbacks. The builder borrows this object only until the matching `endWindow()`; the object must remain alive throughout that scope. Frozen draw snapshots do not borrow it.

`beginWindow(stableKey, title, state, options)` returns whether content is visible. A collapsed window still opens its window scope. Always call `endWindow()` after a window opens, including when `beginWindow()` returns false for collapse. `Context::failed()` distinguishes an admission failure from collapse. Containers and content controls belong inside the visible-content branch. Panels and windows cannot nest; nested row and column containers provide content composition instead.

`WindowOptions::initialBounds` applies once while `state.initialized` is false. `contentWidthFirstUse` and `contentHeightFirstUse` independently fit the initial content while respecting host and skin minimum sizes. Later callbacks preserve host geometry, including user resize. Bounds and all skin metrics use logical units. The window stays inside the available viewport when it fits; larger minimum sizes remain clipped to the viewport.

The title supplies pointer dragging. The bottom-right corner supplies resizing with minimum bounds. The collapse button participates in Tab/Shift-Tab focus and Enter/Space activation. Turning off a capability discards its pending interaction. Turning off collapse restores visible content. Keys starting with `@window.` are reserved for the window chrome.

Chrome targets capture the complete committed window bounds as their pointer gesture reference. Each press uses the geometry that received the native event, even when the host model already describes a GPU-pending candidate. Active updates retain that press reference; a completed press survives until the next callback. Numeric admission and behavior updates validate candidates before publishing changes.

`windowMetrics()` is available during an open window scope, including a collapsed window. `WindowLayout::Visible`, `Content`, `Collapse`, and `Resize` compute matching logical geometry from those metrics and the host state.

## Skin parts and separators

The semantic skin names are `window.normal`, `window.title`, `window.collapse`, and `separator`. A skin can provide `window.resize`; the default atlas uses its `white` sprite to draw the corner grip. Collapse hover and focus reuse the button states and optional `focus.overlay`. Window padding, title padding, minimum sizes, and the atlas reference density determine the control metrics.

A separator does not receive focus or activation. Its direction selects a horizontal or vertical divider, its length follows a layout size policy, and its thickness is expressed in logical units. Zero thickness selects the atlas minimum for that axis, with a one-unit fallback. A horizontal separator stretches across a column by default; a vertical separator stretches through the row height.

## Source ownership

- `window_behavior.cpp` owns initialization, dragging, resizing, viewport constraints, and candidate validation.
- `window_layout.cpp` owns skin-derived metrics and chrome/content geometry.
- `builder_window.cpp` owns window declaration and balanced content arrangement.
- `window_paint.cpp` owns chrome painting and matching hit targets.
- `builder_separator.cpp` owns divider declaration; `controls_paint.cpp` paints arranged content.

## Keyed lists and scrolling

`Builder::selectable(key, text, selected, options)` paints a selected row and returns one accepted activation. The host owns its selected value. `Builder::virtualList(key, source, state, options)` instead composes a focusable list, fixed-height visible rows and a vertical scrollbar. Keep the `ListState` and `interface IListDataSource` alive through the matching `endPanel()`, `endWindow()` or `endPopup()`. Change the source or explicitly call `state.select()`/`scrollTo()` after that scope ends. Frozen snapshots and accepted input targets retain copied geometry and lifetime values, never these borrowed objects.

The source supplies nonzero unique keys, a unique nonzero instance generation, and a nonzero revision advanced whenever order, count, text or enabled state changes. Generations and revisions must not be reused for a different dataset state. `indexOf()` resolves a key and `findEnabled()` searches inclusively in one direction without wrapping. Implement these efficiently; the toolkit does not scan the full dataset. Row text may use a temporary buffer valid until the next source call. The list immediately shapes and copies it before borrowing another row.

Selection and keyboard cursor are stable keys. Reordering preserves valid enabled keys and ensures the cursor's new index is visible; removal or disabling clears the affected key. Replacing an established source instance clears selection, cursor and offset. `ListResult::selectionChanged` reports selected-key changes, while `activated` reports a row pointer release or first Enter/Space press. Up/Down skip disabled rows; Home/End select an enabled endpoint; PageUp/PageDown use the displayed viewport's row count. `selectOnNavigate=false` moves the cursor without committing selection, for later compound controls. An empty list or a list without an enabled cursor consumes Submit as a no-op.

Wheel scrolling preserves selection and uses the accepted step/range. Scrollbar dragging uses its accepted track, thumb and maximum as the press baseline. Multiple pending gestures and control actions are consumed in sequence; coalesced movement carries the sequence of its latest update. A source revision or explicit state change fences old intentions and cancels old capture. Held keys retain release ownership, and a lost control owner cannot revive after a later token reuse. A previously focused list restores focus only when the replacement frame is accepted, with native focus-loss and popup guards.

`ScrollState` stores double offsets; `ScrollLayout` computes logical padded viewport, clipping, proportional/minimum thumb, visible `[firstRow, endRow)` and row bounds. Ancestor clipping culls rows without altering the logical scroll range. The list retains one widget state and emits only visible part targets; visible-row shaping and painting are proportional to that interval. More than 1,024 visible rows rejects a frame, and the existing 4,096-target limit still bounds the complete context. This bound is independent of dataset size. Variable-height rows, horizontal scrolling, multi-selection and generic scroll containers remain future controls.

`Builder::listStyle()` resolves `list.background`, `list.row.normal/hover/selected/disabled`, `scroll.track`, `scroll.thumb` and optional `focus.overlay` from the selected skin. The default and alternate atlases reuse existing texture tiles for new semantic regions. Missing regions use explicitly named panel/button fallbacks within that skin. `SelectablePainter` is shared by standalone rows and list rows. Geometry/state, keyed model behavior, declaration/input, row painting and shared selectable painting live in separate source files.

## Combo boxes

`Builder::comboBox(key, source, state, options)` composes a skinned field, arrow, anchored popup and fixed-height virtualized list. Declare it inside a panel/window; its internal popup is arranged and painted automatically during `endPanel()`/`endWindow()`. No separate popup callback is required. Keep the application-owned `ComboState` and `interface IListDataSource` alive and unchanged through that matching end. The source query contract is stable for the complete loan: advance its revision for any dataset change and do not mutate another borrowed dataset from a query callback. Detected epoch/source mismatches reject the candidate frame while preserving explicit application changes. A state instance may own only one combo per frame, including across panels and roots.

`ComboState::selectedKey()` is the committed key. `listState().cursorKey()` is the popup preview. Pointer activation or the first Enter/Space opens the field; Up/Down opens and moves the preview toward an enabled row. Within the popup, Arrow/Page/Home/End navigation skips disabled rows and ensures the cursor is visible without changing the committed key. Enter/Space or a row release commits that enabled key and closes. Escape, outside dismissal and native focus loss cancel preview. Outside dismissal consumes the complete pointer sequence. Empty datasets show the configured placeholder and cannot commit. Tab/Shift-Tab follows the existing popup focus trap; searchable input and Tab dismissal/advance are later policies.

`ComboResult` reports validity, committed-key changes, explicit commit, opening/closing and current accepted focus during declaration. Accepted popup intentions are consumed before the selected field label is shaped, so a row activation returns `committed` immediately in that callback. The popup then uses the arranged field bounds from the candidate being painted. `bounds()`, `placement()` and `listState().placement()` expose prepared geometry; input remains tied to the separately accepted frame. A fully clipped field suppresses and cancels its popup during scope-end painting.

Explicit `state.select()`, `open()` and `close()` renew the field input lifetime even for unchanged values. Each opening/cancellation renews preview input lifetime. Reordering preserves valid committed and preview keys; removing/disabling a key clears it. A new source instance clears selection/scroll and closes. Recreated or reparented declarations close the old popup while preserving the committed key. Accepted field/popup/list tokens fence old queued clicks, wheel, navigation, thumb drags and held keys. Focus returns only when an eligible field and its matching closed frame are accepted; native focus loss prevents restoration.

`comboStyle()` resolves `combo.normal`, `combo.hover`, `combo.open`, `combo.focused`, `combo.disabled` and `combo.arrow`. The default and replacement skins define the field states as semantic aliases of existing artwork, and popup/rows/scrollbar use their existing skin contracts. Field padding/minimum sizes use the maximum of all states. `combo_state.cpp`, `combo_behavior.cpp`, `builder_combo.cpp`, `combo_input.cpp` and `combo_paint.cpp` separate lifetime, keyed behavior, declaration, input and painting. Popup rows share the existing list painter and scrollbar geometry; dataset size does not determine allocations. Combo declaration inside a user popup, nested popups, searchable combos and variable-height rows remain separate increments.
