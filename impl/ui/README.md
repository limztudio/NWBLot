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
also records each command's logical clip and the display's pixel scales; a future renderer applies
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
texture asset; a future shader must combine texture coverage and tint consistently with this
premultiplied vertex contract.

`freeze()` moves the geometry, indices, ordered commands, display metrics, frame generation, and
skin binding into a move-only snapshot exposed through const views. Start a new frame with `begin()`
before painting again. Reusing the builder or changing the original skin cannot alter a frozen
snapshot. Only adjacent draws with identical material and clip merge; painter order is preserved.

The saved `AssetRef<UiSkin>` and `AssetRef<Texture>` identify assets. They do not retain loaded asset
objects or resolved GPU resources. The explicit skin generation identifies the metadata revision;
the future GPU layer must retain the matching resolved texture revision, descriptors, and upload
storage until GPU completion.

This foundation has no widget state, layout engine, text/font shaping, or GPU implementation yet.
Control behavior and layout will extend `impl/ui/`; GPU uploads, resource retention, and offscreen
rendering belong in `impl/ui/gpu/`. `impl/ecs_ui/` connects the CPU UI to ECS and borrowed OS services,
`core/os/` owns native IME and clipboard behavior, `impl/assets_ui_skin/` owns skin validation/cooking,
and `impl/ecs_render/` will own final composition with the scene.
