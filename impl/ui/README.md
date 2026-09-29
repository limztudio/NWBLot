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
snapshot. Only adjacent draws with identical material and clip merge; painter order is preserved.

The saved `AssetRef<UiSkin>` and `AssetRef<Texture>` identify assets. They do not retain loaded asset
objects or resolved GPU resources. The explicit skin generation identifies the metadata revision;
the GPU layer retains the matching resolved texture revision, descriptors, and upload
storage until GPU completion.

The CPU toolkit also provides scoped stable IDs, retained declaration lifetimes (`state/`),
row/column/overlay measure and arrange (`layout/`), committed-layout input routing (`input/`),
font shaping (`text/`), and skinned windows, panels, labels, separators, buttons and checkboxes (`Builder`).
`nwb_ui_gpu` owns GPU uploads, resource retention,
and offscreen rendering in `impl/ui/gpu/`. `impl/ecs_ui/` connects the CPU UI to ECS and borrowed OS services,
`core/os/` owns clipboard/native selection services and the future IME contract, `impl/assets_ui_skin/` owns skin validation/cooking,
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
host-root lifetime before native input and frame builds. IME, OS pointer services, clipboard/selection,
editable text, lists and popups remain subsequent toolkit work.

## Windows and separators

`Builder::beginWindow(key, title, WindowState&, WindowOptions)` opens one scoped window and returns whether its content is visible. Call `endWindow()` even when the window is collapsed; a failed begin marks the context failed. Do not nest windows/panels or retain a builder's open scope beyond the owning callback. `WindowState` is host-owned and must remain alive through that matching end. Frozen snapshots never reference it.

`WindowOptions` supplies the initial bounds, minimum size, optional first-use content sizing, layout direction, and independent movement/resize/collapse capabilities. Initialize only once, then keep the same model to preserve placement and collapse state across ordinary frames. Placement is constrained so the window's title stays reachable after viewport changes; cross-run persistence belongs to the application. The migrated Testbed window retains its original initial position and content, while its UI is prepared and composed through the custom GPU layer.

The window domain separates behavior (`window_behavior.cpp`), metrics/geometry (`window_layout.cpp`), declarations (`builder_window.cpp`), and paint/chrome targets (`window_paint.cpp`). Title and resize gestures carry the exact committed reference geometry, so a pending candidate cannot shift the gesture's baseline. Coalesced updates retain a complete press/move/release sequence until the next declaration; each update is consumed once. Pointer loss cancels an active gesture, while an ordinary release keeps completed movement.

`separator(key, SeparatorOptions)` participates in layout and draws a named skin part. It supports horizontal/vertical directions, fixed/content/stretch length and skin-derived or explicit logical thickness. Required parts come from the selected atlas (`window.normal`, `window.title`, `window.collapse`, and separator names); missing required parts reject the candidate. An optional `window.resize` sprite can replace the default grip, which uses the selected atlas's white sprite. `windowMetrics()` is available only inside an open window for callers needing the actual chrome geometry.

ImGui runtime, shader assets and vendor sources have been removed. Font shaping/rasterization remains in the independently owned FreeType/HarfBuzz text service; clipboard and native selection remain borrowed OS services. Edit models, popups/lists/combos and OS IME integration are later increments.
