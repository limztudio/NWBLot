Stage one established explicit F0/roughness surface parameters, validated reflection settings, and shared ray-hit reconstruction. The debug build and asset cook passed, followed by the ECS graphics unit suite and both GPU-debug refraction and caustic/refraction capture smoke tests. Visible reflection work begins in the next stage.

This is the stage-one reflection smoke and measurement plan. The reflection
fixture, runner, and statistics described below are planned work, not completed
validation. The production `ReflectionSettings` contract defines
the route and quality controls for the fixture.

The fixture will be a dedicated `nwb_reflection_smoke` target with its own
`reflection_project.cpp`, registered by `tests/smoke/CMakeLists.txt` and the smoke
launcher. It will reuse `framebuffer_capture.cpp` and
`NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK=1` for actual 960x720 framebuffer readbacks.
The existing `nwb_transparent_multi_smoke_assets` cook already combines
`impl/assets` with `tests/smoke/assets` into `Testing/smoke_runtime/<config>/res`.
Launching from that runtime directory keeps executable and cooked assets matched.

Reflection fixture materials belong under `tests/smoke/assets/smoke/reflection`,
with their `.bind`, `.surface`, and `.bxdf` assets under
`tests/smoke/assets/shaders`. Reuse existing mesh assets, not material assumptions.
The current `NwbMeshSurface::param0/param1` and `NwbBxdfSurface::param0/param1`
fields are project-defined; they cannot be assumed to mean roughness/metallic for
arbitrary authored BXDFs. Fixture materials must explicitly supply the reflection
contract once it is defined. Engine reflection passes consume that contract;
project-specific material policy stays in the test asset tree.

The visual matrix will compare reflections off, screen-space only, ray tracing
only, and hybrid screen-space/ray-traced reflection at identical resolution,
camera, geometry, material values, and simulation frame. Explicit hardware
acceptance requires real ray-query support and a successful hardware dispatch;
portable smoke coverage reports unsupported required routes as skips without
changing device capability reporting. Test-local controls forward to the typed
renderer settings rather than introducing production environment probes.

The initial cases and their checks are:

- An opaque planar mirror with an asymmetric colored marker visible to the
  camera. The reflected marker must appear in its geometrically predicted region
  and disappear when reflection is disabled. Moving the source must move its
  reflection; mere brightening or flat tinting cannot pass.
- A colored marker outside the camera frustum but visible to the reflection ray.
  The offscreen marker must be absent from the direct framebuffer region, absent
  from screen-space-only hits, and present in the hardware and hybrid reflection.
  This case proves that the hardware continuation supplies information unavailable
  to screen-space tracing.
- Opaque and primary-glass reflectors in the same scene. Reflection must appear
  on both eligible surfaces, while glass transmission, foreground AVBOIT layers,
  and pixels outside the reflective silhouettes remain correct. The existing
  strict refraction/duplicate and caustic smoke tests remain regression gates.
- A roughness sweep with a finite bright marker over a dark background. Capture
  several authored roughness values and measure lobe extent, local contrast, and
  energy using tolerances appropriate to the implemented sampling model. Do not
  impose byte equality between stochastic routes or claim that blur alone proves
  physically correct rough reflection.
- Deterministic camera and object motion, including a disocclusion and a camera
  cut. Capture fixed early/middle/settled frames. Reflections must track current
  geometry and stale history must be rejected when history is implemented.
- Overlapping glass and opaque occluders, plus a foreground transparent strip.
  Validate the nearest supported reflective surface, occlusion, and foreground
  preservation. Mark unsupported multilayer transport explicitly in the gallery.

The capture runner will reuse `window_capture_smoke.py` application capture,
strict runtime/validation log rejection, normal shutdown checks, fixed simulation
delta, and sanitized fixture environment. Dedicated Python analysis tests will
exercise the marker-location, no-effect, retained-foreground, and history-rejection
oracles. Raw BMPs remain the evidence; lossless PNG copies and a self-contained
HTML comparison can reuse the existing gallery packaging. Artifact metadata must
record route, hardware support, quality/ray budgets, roughness, frame count,
simulation delta, device/build identity, and enabled validation mode.

Performance measurement is a separate bounded run using the same fixture and
quality settings. `GpuPassTimingProbe` already reads published GPU timestamp
windows, deduplicates their publish indices, and writes per-pass intervals to
`NWB_GPU_TIMING_FILE` after `CaptureOptions::GpuTimingOnly()` is enabled. Its
current cadence is a 0.25-second initial warm-up and 0.5-second reporting interval;
the benchmark runner must add a meaningful shader/history warm-up and collect
enough positive intervals. Feed real frame delta to FPS/timing cadence while
advancing scene motion with the separately fixed simulation delta. Otherwise a
fixed 1/60 simulation delta would produce a fabricated 60 FPS report.

`tests/ab/gpu_timing_parse.py` already supports interval parsing, measurement
byte offsets, per-scope medians, required sample counts, and `.namesym` decoding
for opt/fin builds. The bounded process, warm-up, measurement, and logger lifecycle
in `tests/ab/hybrid_shadow_boundary/run.py` provides the corresponding runner
pattern. Require every expected reflection scope to appear; the existing smoke
probe has a 64-scope cap and must not silently omit a new pass. Use `render.frame`
for the end-to-end GPU critical path instead of summing overlapping envelopes.

Proposed reflection timings separate screen tracing, hardware continuation,
temporal reconstruction, spatial filtering, and composition. Existing
`render.opaque_regular`, `render.shadow_visibility`, and applicable caustic scopes
serve as unaffected controls. Compare repeated, interleaved screen-only,
hardware-only, and hybrid runs with identical quality settings. Report median
frame/pass time, variation, and device-local memory use. Broad improvements in
all control scopes indicate system variance, not a reflection optimization.

There is no existing reflection-specific GPU counter stream. The staged runtime
implementation should expose completed asynchronous statistics for eligible
opaque/glass pixels, screen-trace attempts and accepted hits, hardware rays,
budget exhaustion, fallback pixels, and history acceptance/rejection. These
counters must retain the originating frame and route identity, avoid synchronous
readback stalls, and be disabled when diagnostics are not requested. Timings alone
cannot prove that hybrid tracing saved rays or that an offscreen hit came from
hardware. Hardware-only and hybrid measurements therefore require both timing
and route/counter evidence before claiming a speed or quality improvement.
