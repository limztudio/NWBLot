# UI widgets

`Ui::Builder` declares panels and windows, scoped row/column containers, labels, buttons, checkboxes, and separators. It shapes text and arranges the content before emitting paint and hit targets in the same order. Application values remain in the host model; the GPU snapshot owns copied geometry and image references.

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
