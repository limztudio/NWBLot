# UI GPU layer

`nwb_ui_gpu` renders a frozen `Ui::DrawSnapshot` into a single-sample, full-output-size
`RGBA16_FLOAT` texture. It borrows `GraphicsRuntime`, `AssetManager`, a shader path
resolver, and the caller's `GlobalArena`; it has no ECS or ImGui dependency.

Call `validateResources(width, height)`, then `setSkin(identity, loadedSkin,
skinGeneration)`. `setSkin` can also rebind a later skin generation without a GPU
idle wait. It loads and validates the referenced texture,
creates its concrete GPU image and descriptor, and saves the loader's authoritative
static-upload completion token. Skin identities and generation must match the
snapshot submitted by `submit(Move(snapshot))`. Asset references identify assets;
the captured resource version owns the resolved GPU image.

Each new target receives a graph-owned transparent clear from its actual unknown
native state, followed by a primary Graphics completion in `ShaderResource`, before
its bindless sampled descriptor is published. The target version saves that exact
accepted initialization token; each raster graph imports it as availability so
work routed to another physical queue also waits for initialization. Setup and
partial setup failures retain native image references through accepted GPU work.

Only one CPU generation can be pending. `submit` rejects a second pending frame
without moving its snapshot; `hasPendingFrame()` lets the caller retain that
existing generation for retry. The renderer has three target/buffer slots.
Preparation allocates or grows geometry buffers before recording and admits a
frame only when a slot is free. If all slots remain in flight, preparation succeeds
with no declared layer and keeps the pending snapshot. No frame waits for GPU idle.

`prepareTaskGraphOutputLayer(acquired)` captures the exact acquired frame.
`declareTaskGraphOutputLayer(graph)` declares graph-owned geometry uploads,
a transparent target clear, and `ui.raster` for a painted frame. The raster produces
an explicit color resource version. It has no scene dependency, so the graph can
schedule it alongside scene work. The final output consumer depends on `readyTask`,
reads `colorVersion`, and samples `sampledImage` before output transfer encoding.
An empty scene-UI snapshot declares no UI graph work or color resource; it returns
only `frameGeneration` so the accepted final consumer still retires that exact
snapshot. A rejected graph leaves the generation pending for retry. Only accepted
final consumption calls `acceptTaskGraphOutputLayer(frameGeneration, token)`.

Each graph payload owns the frozen snapshot and concrete skin, target, geometry,
pipeline, sampler, and descriptor versions it reads. Descriptor slots remain
allocated through those owners; native heap binding records submitted descriptor
use. There is no long-lived global pending-recording lease. Raster acceptance does
not consume the CPU frame. The renderer saves the exact accepted vertex-upload,
index-upload, clear, raster, and final-consumer tokens. Slot reuse requires final
consumption and completion of all accepted work. A rejected graph retains its
immutable source; rebuilding after an accepted prefix waits nonblockingly for those
prefix tokens before reusing its resources. An incomplete prefix cannot expose an
old acquired generation to a new final consumer.

Coordinates use the snapshot's logical units and saved pixel scales. Vertex RGB is
premultiplied linear Rec.709. The skin shader reads the atlas at base mip with a
linear clamp sampler; sRGB textures decode during sampling, and sampled straight
alpha is combined with premultiplied tint. The raster blends source-one over
inverse-source-alpha and keeps the layer linear. The default skin and custom skins
use the same path. The final compositor applies SDR attachment encoding or HDR10
encoding once. UI-only HDR uses the shared 203-nit paper-white helper.

`renderStandalone(acquired)` supplies UI-only presentation over black using the
same layer and an exact imported acquired backbuffer with a graph presentation
endpoint. An empty submitted snapshot still clears a valid transparent layer and
outputs defined black. A frame claimed by the scene graph skips standalone;
accepted consumption leaves no pending work. Only the exact acquired identity
already accepted can skip output without pending work. Standalone rejects a new
acquired image with no snapshot, a busy slot, or an incomplete retry prefix.
An optional registered presentation contributor prepares before recording and
declares after `ui.output`; its work is followed by a primary Graphics completion
tail used as the present endpoint. UI slot retirement continues to use the exact
accepted output-consumer token, independently of that later presentation tail.
`UiLayerSystem` selects `Scene` or
`Standalone` policy. Its current render-pass API cannot abort acquired presentation
on a rejected standalone submission, so that adapter requests device recreation
instead of continuing presentation with indeterminate ownership.

The caller must stop producers, join graph/native GPU work, and release old graph
payloads before `invalidateResources()` and before destroying the graphics device
or caller arena. Resize/device recreation uses that same joined invalidation and
validation boundary. Main-thread setup/submission methods do not provide concurrent
producer admission.

Raster and output draws support direct native recording and command IR replay.
Graph resource imports, versions, barriers, uploads, clears, and `ui.raster`/`ui.output`
task timing are available to graph capture and telemetry. Standalone presentation
also records the `render.frame` timing scope around its graph work.
Widgets, retained interaction state, text shaping/fonts, focus/navigation, and OS
IME/clipboard borrowing live in separate domains above this renderer.
