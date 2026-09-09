Reflection stages one through seven are implemented and passed their final
validation gates. The renderer combines hierarchical screen-space tracing with
bounded hardware continuation and composites reflection on opaque surfaces and
primary glass with AVBOIT. It supports opaque GGX roughness, strict static-scene
temporal accumulation, separate spatial filtering, bounded secondary transmission
through authored optical volumes, and optional screen-miss feedback.

The final stage-seven run passed 100 actual-framebuffer checks on the Qualcomm
Adreno X2-90 with required hardware ray queries and `--gpudbg`: 22 baseline and
budget captures, 14 roughness captures, 30 optical captures, four combined
caustic/refraction/reflection captures, 24 feedback captures, and six captures
with reflection diagnostics disabled. All captures completed normally with clean
runtime and GPU validation logs. All 358 ECS graphics tests and the six selected
CPU test targets also passed. Stage-specific validation counts below describe
their original gates.

The optimized benchmark methodology and GPU timing evidence are documented
separately in [Reflection performance](REFLECTION_PERFORMANCE.md). Completed
traversal counters establish work performed; they do not establish a GPU speedup.
Screen-miss feedback remains disabled by default.

The refraction fixture explicitly disables reflection to isolate its transmission
comparisons. The 33-capture exact-duplicate gallery passed at the stage-two gate;
that earlier run is separate evidence.

The stage-four diagnostics extension records completed-frame statistics for every
capture and adds hybrid floor captures with hardware budgets of zero and 64 rays.
The accepted-token measurements establish bounded hardware work and lower
hardware ray counts in the floor fixture. They do not establish a GPU speedup.

The public `ReflectionSettings` contract is declared in
`impl/ecs_render/reflection/settings.h` and applied through
`RendererSystem::setReflectionSettings`. Material surface hooks explicitly supply
RGB specular F0 and perceptual roughness. These values have their own G-buffer and
primary-glass capture targets; project-defined BXDF `param0/param1` retain their
existing meanings. Fixtures use the typed `smoke_surface` mutable fields
`runtime.specular_f0` and `runtime.perceptual_roughness`.

The stage-three route contract keeps `Hardware` as the bounded hardware ray queue,
uses hierarchical screen tracing for `ScreenSpace`, and allows `Hybrid` to continue
unresolved or uncertain screen samples through hardware. `Disabled` produces
identity reflection outputs. The on-screen cases test actual SSR marker hits.
The smoke selects its mode explicitly and sets both analytic environment colors
to black, preventing environment color from passing as a reflected marker.
Its ray budget is 1,382,400, enough for both
960x720 layers; default budget quality is not established by this test.

The depth hierarchy stores minimum and maximum visible depth in `RG32_FLOAT`.
Resource setup selects `RGBA32_FLOAT` if the required texture/UAV-store support
query rejects RG32, and fails if neither format supports the required uses.
Mip zero conservatively bounds a 3x3 neighborhood of camera depth samples;
coarser levels reduce both bounds and preserve odd-sized edge coverage. A ray can
skip a cell when its depth interval is disjoint from the cell's visible-depth
interval, including when tracing back toward the camera. Finest-level candidates
still require a world-space tangent-plane intersection consistent with the exact
camera depth/source pixel. These bounds describe visible samples, not hidden
surface entry/exit depths.

At 960x720 the ten native mip levels contain 921,567 texels. The pyramid alone
therefore stores 7,372,536 bytes (about 7.03 MiB) with RG32, or about 14.06 MiB with
RGBA32, excluding driver alignment and metadata. Building mip zero reads nine
neighboring depth samples per pixel. The additional maximum-depth channel costs
more storage and reduction bandwidth than a minimum-only hierarchy, while
allowing empty intervals behind visible surfaces to be skipped. A CPU reference
of the problematic backward-camera panel ray needed 173 iterations with minimum
depth alone and 37 with interval bounds. That diagnostic explains the bounded
trace fix; it is neither a GPU timing measurement nor an overall performance or
hardware ray-savings claim. The capture passed with the existing 96-step limit
and unchanged image assertion thresholds.

The dedicated `nwb_reflection_smoke` target uses `reflection_project.cpp` and
`framebuffer_capture.cpp`. It reads back the actual 960x720 framebuffer after
16 graphics presentation frames, then exits through normal application shutdown.
Run from `Testing/smoke_runtime/<configuration>` so `cwd/res` selects the matching
packed smoke assets. The shared `nwb_transparent_multi_smoke_assets` cook includes
both `impl/assets` and `tests/smoke/assets`; rebuilding the target recooks changed
shader/material assets.

The cases are:

- `offscreen`: the camera is at `(0, 1.4, -6)`, facing `+Z` with a 60-degree
  vertical FOV. A smooth opaque mirror lies at `z=0`. Red and green sphere markers
  sit behind the camera at `z=-8`, so their mirror images have virtual depth
  `z=+8`, 14 units from the camera. The two reflected colors must appear in their
  geometrically predicted regions. Disabled and screen/environment captures must
  contain neither colored marker.
- `moved`: both offscreen markers shift right by 0.9 world units. Their images
  must move right by approximately 40.085 pixels at the fixed projection, with
  negligible vertical movement. This is a static before/after geometry test,
  not a temporal-history test.
- `opaque_glass`: adjacent opaque and clear glass spheres reflect red/green
  panels behind the camera. A grayscale striped backdrop remains visible through
  the glass, and a blue foreground AVBOIT strip crosses both objects. Glass uses
  IOR 3.8 and its corresponding dielectric F0 of approximately 0.34028. This is
  deliberately a high-index optical stress fixture, not ordinary window glass;
  coverage is zero and absorption is disabled.
- `onscreen`: the wall mirror remains at `z=0`, and two-sided upright red/green
  panels lie at `z=-3`, centered at `x=-1.7/+1.7`, `y=1.0/2.0`. Their direct
  camera images are three units away, near screen `x=127/833`; the mirror images
  have virtual depth `z=+3`, nine units away, near `x=362/598`. Separate projected
  rectangles ensure direct visibility cannot pass as reflection. Reflected rays
  approach the panel backs, so SSR has provisional confidence capped at 0.5.
  Hybrid resolves that ambiguity through hardware; this case cannot prove ray
  savings.
- `onscreen_moved`: both panels move inward by 0.25 world units. Their direct
  images must move approximately 51.96 pixels, while their reflections move
  approximately 17.32 pixels in the same respective directions. As with `moved`,
  this compares static geometry snapshots and does not test temporal history.
- `boundary`: the green panel moves to `x=3.0`, entirely outside the camera
  frustum, while its mirror image remains visible near `x=688`. Screen mode must
  lose that green reflection; hybrid must retain it. The red panel remains an
  on-screen control.
- `floor`: the mirror is a horizontal `y=0` plane, with half extents of four
  units in X/Z. Both upright panels are at `y=2, z=3`, facing the camera. Their
  direct images are near `y=318`; reflection through the floor puts their virtual
  centers at `y=-2`, near screen `y=596`, with the same `x=362/598` centers.
  Camera and reflected rays approach the marker fronts, providing a separate
  fixture for confident SSR. Image agreement establishes visible behavior;
  the completed GPU counters below separately establish hardware ray savings.

`reflection_smoke.py` keeps the seven original captures and adds hybrid
`offscreen`. These eight form `--suite baseline`. The twelve captures in
`--suite screen` are all four routes for `onscreen` and `floor`, screen
`onscreen_moved`, and disabled/screen/hybrid `boundary`. The two additional
`--suite budget` captures use the same floor geometry in hybrid mode with zero
and 64 hardware rays per frame. The default `--suite all` runs all 22.
It requires the matching accepted-route diagnostic, normal
shutdown, nonempty actual framebuffer data, and strict runtime/validation logs.
Unsupported required hardware is a skip unless `--require-hardware` was selected.
The application must self-exit within each capture timeout. The outer runner
allows an additional 90 seconds for bounded child startup and process/logserver
cleanup, rather than interrupting that cleanup at the capture deadline.

The fixture enables `ReflectionSettings::diagnosticsEnabled` and polls
`RendererSystem::tryGetLatestReflectionStatistics` without waiting. It logs each
new completed sequence once as `ReflectionSmokeStatistics: key=value ...`.
Statistics preserve their source frame, requested mode, dimensions, requested and
effective budgets, queue capacity, resource generation, and accepted submission
token including physical queue/device generation. The counters describe eligible
opaque/glass pixels, hardware candidates/rays/hits, screen attempts/accepted hits,
and fallback pixels. A screen hit is counted only when it meets the configured
confidence threshold; provisional wall-mirror color is not an accepted hit.

The runner saves the exact collected per-launch log delta beside every BMP using
the capture harness's `--log-output` option. Logs are written before validation
so timeout and validation failures retain their collected evidence. Each completed
sample must match its capture's mode, 960x720 dimensions, budget and queue
capacity, and have an accepted token/device identity. Static captures reject
mixed generations, stale or out-of-order sequence/frame/token values. Hardware
rays may never exceed either effective budget or candidate count, including on
warm-up frames, and the outcomes must satisfy:

```text
opaquePixels + glassPixels = screenHits + hardwareHits + fallbackPixels
```

At least one completed sample must come from frame three or later. Stable screen
captures must attempt screen tracing and issue no hardware rays. Offscreen
hardware/hybrid captures need actual hardware hits; active opaque/glass captures
need both receiver populations. The floor comparison requires accepted screen
hits and fewer hardware rays throughout the stable hybrid sample range than in
the matched hardware range. Eligible populations must agree within 1% (with a
16-pixel minimum tolerance), preventing reduced visible coverage from passing as
ray savings. This is a fixture-specific ray-count comparison, not a timing claim.

Both budget captures must retain visible floor markers, have more candidates than
their effective budget, and leave valid fallback outcomes. With ready hardware,
actual rays must equal the minimum of candidates and effective budget: zero or
64 for these cases. A zero budget keeps hardware capability and candidate
classification truthful; it does not simulate an unsupported adapter. Queue
storage is bounded by the configured budget, pixel population and dispatch limit,
with a minimal valid allocation for zero budget. Capture filenames include their
budget so they cannot overwrite the ordinary hybrid reference.

The image assertions require each mirror marker to cover at least 25% of its
analytically projected disk area: 659 pixels at 960x720. At least 95% of each
marker must lie in its predicted region, and its centroid and movement must match
the fixed projection. Each opaque/glass receiver must acquire colored reflection
in at least 2% of its test region. The blue foreground mask must retain at least
97% of its original pixels and add no more than 3%; exterior changes are limited
to 0.5% of the sampled exterior regions. Panel direct images must cover at least
50% of their projected rectangle area and reflections at least 25%; color
thresholds permit visible provisional SSR radiance. Their centroids must follow
the separate geometric projections, with at most 3% of each marker color outside
the expected direct/reflected regions. The boundary source and reflection masks
must match their mode-specific visibility. Thirty-six independent synthetic analysis
tests reject flat tint, missing or stationary markers, sparse correctly located
patches, missing glass reflection, expanded/lost foreground, exterior corruption,
malformed frames, direct-image substitution, incorrect floor or boundary images,
incorrect direct/reflected motion scales, and inherited fixture controls. The
statistics tests also reject malformed logs, wrong configurations, invalid tokens,
stale generations/samples, missing warm-up completion, budget overruns, invalid
outcome partitions, false accepted-screen evidence, and apparent ray savings
caused by a smaller eligible population.

The completed stage-two hardware captures contained 2,643-2,687 pixels per mirror
marker, with no marker pixels outside the predicted regions. The measured
horizontal movements were approximately 40.2 pixels for red and 40.1 for green,
against the 40.085-pixel prediction. Disabled and screen/environment controls had
zero colored marker pixels. The paired spheres gained 3,880 opaque and 3,881 glass
reflection pixels. All 12,811 foreground pixels were retained, with zero added
foreground pixels and zero changed exterior pixels. These are fixture-specific
observations from the completed capture; they are not general image-quality or
performance guarantees.

The completed stage-three run measured the following reflected marker areas:

| Case | Screen red / green pixels | Hardware red / green pixels | Hybrid red / green pixels |
| --- | ---: | ---: | ---: |
| Wall mirror (`onscreen`) | 1,410 / 1,942 | 1,470 / 2,058 | 1,470 / 2,058 |
| Floor mirror (`floor`) | 1,470 / 2,058 | 1,470 / 2,058 | 1,470 / 2,058 |

All these marker pixels were within their predicted regions. In the boundary
case, screen mode retained 1,410 red pixels and lost the offscreen green marker
entirely; hybrid retained 1,470 red and 2,058 green pixels. The on-screen sources
moved inward by exactly 52 pixels in the captures, versus a 51.96-pixel prediction;
their screen reflections moved by approximately 17.54/17.55 pixels, versus
17.32 predicted. Hybrid also retained both fully offscreen sphere reflections.
The opaque/glass comparison again measured 3,880/3,881 added reflection pixels,
retained all 12,811 foreground pixels, and found no added foreground or changed
exterior pixels.

Visual inspection shows the wall-mirror SSR rectangles have small missing edge
portions; hardware/hybrid restore their complete shapes. The floor screen capture
contains black missing-background patches just above the colored reflections,
where the camera source image cannot supply the occluded backdrop. Hybrid fills
those regions through hardware continuation. Matching marker areas does not
establish complete image equivalence or remove this screen-only visibility limit.

The completed stage-four statistics used exactly 243,336 eligible floor samples
in both routes. Hardware mode issued 243,336 rays; hybrid accepted 179,684 screen
hits and issued 63,652 hardware rays. Every stable sample had those counts,
establishing approximately 73.84% fewer actual hardware rays in this fixture.
This is a ray-count reduction at the stated settings and scene, not a measured
frame-time improvement.

Each limited-budget capture supplied 13 completed samples from frame three or
later. Both counted 63,652 hardware candidates and 179,684 accepted screen hits.
The zero-budget case issued zero hardware rays with a one-entry queue allocation.
The 64-ray case allocated 64 queue entries and issued exactly 64 rays. Those
64 rays missed in this capture, so both controls retained 63,652 fallback pixels;
zero hardware hits is valid when the budget selects rays that miss. Their visible
floor markers and outcome partitions passed the same checks.

Build and run on the Windows ARM64 debug preset:

```powershell
cmake --build --preset windows-clang-arm64-dbg --target nwb_reflection_smoke
ctest --test-dir __cmake/build/windows-clang-arm64 -C dbg --output-on-failure -R '^nwb_reflection_capture_analysis_unit$'
python tests/smoke/reflection_smoke.py --executable __exec/windows/arm64/full/dbg/reflection_smoke.exe --working-directory __cmake/build/windows-clang-arm64/Testing/smoke_runtime/dbg --output-directory __cmake/build/windows-clang-arm64/Testing/smoke/dbg/reflection_statistics_gpudbg --logserver-executable __exec/windows/arm64/full/dbg/logserver.exe --require-hardware --application-arg=--gpudbg
```

The portable CTest entry is `nwb_reflection_capture_smoke`. The launcher scene is
`reflection`, with `--reflection-case
offscreen|moved|opaque_glass|onscreen|onscreen_moved|boundary|floor` and
`--reflection-mode disabled|screen|hardware|hybrid`. Optional
`--reflection-debug none|source|confidence` forwards the typed debug view for
manual inspection. `--reflection-ray-budget` sets a nonnegative u32 hardware ray
budget through the typed settings API, preserving an explicit zero. The comparison
runner clears inherited debug, budget and timing
controls so ordinary captures always measure rendered radiance. All controls
remain test-local and forward to the typed renderer API.

Artifacts include the original BMPs, exact per-capture logs, PNG conversions with identical RGB pixels,
`reflection_manifest.json`, and a self-contained offline `reflection.html`.
Images and an unvalidated report are saved before the final image assertions so
failed visual comparisons remain inspectable. A passing report records the
resulting image metrics and completed statistics. The report uses actual rendered pixels; it contains no
illustrated or generated replacement images.

Remaining work and limits are explicit:

- The completed stage-five gate validates opaque GGX reflection and strict static
  history in addition to the original smooth single-bounce cases. Camera or
  content changes reject history; moving-camera reprojection is not implemented.
- Hardware hits use the authored BXDF with linear radiance and bounded lighting
  inputs. Additional reflection, shadow, GI, and transparent-volume transport at
  a secondary hit are not traced. A second transparent glass surface can therefore
  hide content that full optical transport would reveal.
- Primary glass reflection is composited behind foreground AVBOIT. The paired
  fixture checks this order and visible reflection on glass; it is not a numerical
  proof of full reflection/refraction energy conservation or multilayer optics.
- The stage-three cases distinguish on-screen reflection, uncertain back-facing
  hits, front-facing floor hits, and offscreen continuation. They do not prove
  screen visibility for hidden geometry or reflected secondary transparent
  transport. The separate stage-five mutation suite validates history rejection.
- The strict refraction and caustic tests passed again at the stage-four gate;
  exact-duplicate tests passed at stage two. Combined reflection/duplicate and more complex
  reflected secondary transport cases remain part of the subsequent matrix.
- No reflection speed improvement has been measured. The completion-associated
  statistics provide frame/route-specific ray and outcome evidence, with optional
  diagnostics enabled in this fixture. End-to-end timing benchmarks and the cost
  of diagnostics must be evaluated separately before making a performance claim.

For subsequent performance runs, enable `NWB_REFLECTION_SMOKE_TIMING=1` and use
`NWB_GPU_TIMING_FILE`. The fixture forwards real frame delta to `FpsProbe` and
`GpuPassTimingProbe`, while fixed simulation delta remains separate. The current
reflection scopes are `render.reflection_depth_pyramid`,
`render.reflection_classify` (including screen tracing),
`render.reflection_build_args`, and `render.reflection_hardware`; stage-five
filtering adds `render.reflection_temporal` and `render.reflection_spatial`.
The probe has a 64-scope cap, so the benchmark must require all expected scopes
rather than silently accepting missing timings. Decode opt/fin scope hashes with
the matching `.namesym` file using `tests/ab/gpu_timing_parse.py`.

Timing-file records retain the legacy `avg/min/max/samples` fields, whose sample
count is the number of folded publication windows. They also expose `total_ms`
(summed GPU duration), `gpu_samples` (actual timed samples), and `sample_avg_ms`
(`total_ms / gpu_samples`). These raw fields allow normalization by known timed
work instead of treating asynchronous publication cadence as a frame rate. A
sample average for a multi-dispatch pass is still not the full pass cost per
frame; the benchmark must account for that pass's dispatch structure.

`render.reflection_depth_pyramid` records each mip dispatch under the same scope.
The accumulator sums those dispatch GPU durations per published collection
window; the probe averages nonempty published-window sums. Asynchronous
collection can combine several source frames or publish only part of a frame's
dispatches in one window. This value is not automatically one complete pyramid
per frame, and it excludes barriers and gaps between dispatches. Do not divide
by mip count or multiply by mip count. A valid benchmark must normalize against
completed source-frame coverage and use frame critical-path timing for the
end-to-end comparison.

Collect repeated, interleaved route runs after shader/history warm-up. Compare the
`render.frame` critical path and affected reflection scopes alongside unaffected
controls such as `render.opaque_regular`, `render.shadow_visibility`, and applicable
caustic scopes; do not sum overlapping packet envelopes. CPU-visible counters
must retain their originating frame/route and come only from completed accepted
submissions. Ray savings require actual ray-count evidence, and timing changes
that also appear in unaffected controls indicate system variance.

The stage-five fixture has two separate bounded suites, leaving the original
22-capture suite as the default regression. `--suite rough` adds 14 captures:
opaque roughness 0, 0.2, 0.4 and 0.6, a second independent seed at roughness 0.4,
accepted sample caps 8/64/256, an unfiltered
smooth reference, separate spatial filtering, accumulated and raw white-furnace roughness 0/1, and
authored glass roughness 0/0.6. Clear glass keeps a smooth reflection lobe until
rough dielectric transmission is paired with it. `--suite temporal` adds 12
captures: first post-change images and fresh final-scene references for camera,
offscreen object transform, mutable material tint, directional lighting, and
runtime skeletal pose; further captures verify camera history rebuilding and
the reflected model's bind-pose footprint before skeletal mutation.

The roughness scene uses a planar black receiver, F0 0.95, and pure red/green
unlit rectangular emitters behind the camera. The reference integrates the
correlated Smith GGX BRDF over each emitter's area; it does not use the production
VNDF sampler. Image analysis reverses the fixture's pinned sRGB/Reinhard
presentation (exposure 1, shoulder 1), then checks linear integrated radiance,
color position, lobe spread, and an independent spatial grid. The furnace case
uses F0 1 and unit environment radiance. At roughness 1 its single-scatter
directional albedo is `1 - NdotV * ln(1 + 1/NdotV)`, which rejects incorrectly
renormalized below-horizon samples. Below-horizon samples are valid zero radiance;
their opaque population is counted without a hardware hit or fallback. Actual
hardware rays must still equal the bounded accepted candidate queue.

The 256-sample reference is only accepted after completed metadata proves that
the cap was reached before the captured frame. The fixture queries completed
statistics without blocking and arms its framebuffer observer at that plateau.
For mutation tests, the observer captures the exact first graphics frame after
the change and defers quitting until completed metadata covers that frame. A
matching `historyStartGraphicsFrame` and a newer epoch prove rejection of prior
history even if asynchronous publication skips the first completed sample. The
fresh comparison starts in the final scene, warms with seed 1, then resets to
seed 0 at its own capture boundary. This gives the same sample index and seed
without assuming a relation between scheduler updates and GPU frame indices.
The lighting mutation sets the authored light color to black while retaining its
positive intensity. This removes its radiance while keeping the light in the
scene, avoiding the scene gatherer's implicit default light when no authored
positive-intensity light remains. Runtime deformation must retain zero history count and never reuse history;
the deformation comparison uses roughness zero so independent raw sample indices
cannot hide stale geometry. Removing red content must remove its reflection
while retaining an unchanged green control. Spatial filtering is independently
enabled and its output must never feed temporal accumulation.

Run these additional suites using the same executable/working-directory and
logserver arguments as the existing capture command, replacing `--suite all`
with `--suite rough` or `--suite temporal`. CTest entries are
`nwb_reflection_rough_capture_smoke` and
`nwb_reflection_temporal_capture_smoke`; both share the display resource lock.
The analysis tests are available as
`python tests/integration/reflection_roughness_smoke_tests.py`.
The interactive launcher also accepts `--reflection-case rough`,
`--reflection-roughness`, `--reflection-history-samples`,
`--reflection-post-reset-samples`, `--reflection-seed`, and on/off controls
`--reflection-temporal`, `--reflection-spatial`, `--reflection-diagnostics`, and
`--reflection-final-state`. Diagnostics can be disabled for later timing runs;
source-anchored acceptance captures require it enabled.

Stage five passed 14 roughness and 12 temporal GPU-debug captures at 960x720 on
this host, alongside the original 22-capture reflection regression. Completed
manifests and actual framebuffer images are under
`Testing/smoke/dbg/reflection_roughness_gpudbg`,
`Testing/smoke/dbg/reflection_temporal_gpudbg`, and
`Testing/smoke/dbg/reflection_stage5_regression_gpudbg` in the build directory.
The ECS graphics target passed 323 tests; the baseline and roughness analysis
suites passed 37 and 32 tests respectively. The refraction GPU-debug and caustic
sphere capture regressions passed again after the final stage-five build.

The independent GGX reference error at roughness 0.4 decreased from 0.06200 at
8 accepted samples to 0.01872 at 64 and 0.00682 at 256. The rough white-furnace
mean linear albedo error was 0.000215. The separate radius-2 spatial filter
reduced the fixture's high-frequency energy from 579.647 to 10.665 at 8 samples.
These are image-quality measurements, not GPU time or frame-rate measurements.
Smooth mirror history/raw and both authored glass-roughness cases were
pixel-identical. Every first mutation image matched its fresh final-scene
reference exactly. Skeletal deformation changed 3,875 red footprint pixels
while preserving all 4,422 green control pixels.

History remains RGBA16F. Before writing an accumulated mean, the temporal shader
stochastically rounds RGB to exact half-representable FP32 values using an
independent pixel/channel/accepted-sample-index hash. This removes accumulated
directed storage-rounding bias without another buffer or dispatch. The owned
raw writer establishes nonnegative radiance, finite inputs are checked, and the
validated 256-sample maximum bounds the integer rounding implementation. The
sample index continues beyond the bounded history count. The spatial result is
stored separately and cannot bias future temporal samples.

Stage six adds a separate `--suite optical` with 30 bounded captures. The
camera and a smooth opaque mirror face forward; all transport-test glass and
the unlit striped chart are behind the camera. Camera refraction therefore
cannot substitute for reflected transport through these objects. Normal and
oblique closed slabs, tinted absorption, two and three nested media, overlapping
priority media, alpha 0/0.5/1 panes before and after glass, exact duplicate
representatives, reversed duplicate creation, mirrored nonuniform transforms,
disconnected components in one or two instances, an authored triangle torus,
one instance with overlapping shells whose winding reaches two, its matching
single-volume union, equal-priority ties resolved by full entity ID, and one or
two origin-containing volumes each have an independent reference.
The failure fixtures use an unspecified optical boundary, mixed active medium
contracts, independent unsuppressed coincident instances, five active media, a one-query slab limit and a TIR prism exhausted
after three queries. All 30 captures passed GPU-debug validation, completed
counter checks and the independent image analysis on this host.

`reflection_optical_reference.py` uses float64 plane, convex-box and authored
triangle intersections, exact unpolarized dielectric Fresnel and Snell's law.
Unit-distance transmission is half-quantized exactly as authored and integrated
over world-space segment lengths. Camera-radiance transport includes
`(eta_i / eta_t)^2` at each interface. This cancels for a complete air-to-glass-to-air
slab but remains for rays that begin inside glass. The reference therefore permits
linear radiance above one before the fixed Reinhard/sRGB presentation. Mirror
F0, material transmission, IOR and chart colors are also half-quantized. A
one-pixel neighborhood of predicted discontinuities is omitted; stable pixels
must match both stripe position and channel energy. Screenshots are never blurred,
shifted or normalized to make them match. Duplicate and disconnected-instance
comparisons additionally require the same reflected image.

The faceted torus uses a four-pixel reference grid within its existing narrow
projected region. Its 236 retained sample centers include 148 chart samples
whose rays pass through glass and 34 whose rays leave and re-enter it. The
oracle requires at least 60 transmitted and 16 re-entry chart samples, so extra
unobstructed background cannot satisfy the test. Linear RGB MAE is 0.002457 for
the torus; the largest per-case MAE in this run is 0.008575 for the inside-origin
fixture. The duplicate, mirrored-transform, disconnected-instance and priority
tie comparisons are byte-identical within the mirror region. The overlapping
shell union differs from its single-volume reference by only 0.0000167 byte
MAE there. These measurements apply to stable samples in the declared mirror
region, not every ray from the primary glass visible around its boundary.
Ambiguous seams, repeated TIR and exhausted paths elsewhere retain the defined
conservative termination behavior.

Closed homogeneous volumes explicitly use `OpticalBoundaryMode::ClosedNested`
or `ClosedPriority`. Priority media select the highest `opticalMediumPriority`
and then the smallest full entity ID. This is independent of
`opticalVolumePriority`, which selects a duplicate representative.
`IdenticalMaterial` and `SharedGroup` coincidence opt-ins retain their existing
geometry promises. The geometry of a closed optical volume and its homogeneous
IOR/absorption are author contracts; an arbitrary open mesh is not silently
declared a valid dielectric volume. IOR-one transparent panes use flat coverage
composition. Secondary non-TIR Fresnel reflected branches remain omitted; the
walker follows deterministic transmission and continues true total internal
reflection. Unsupported, ambiguous and exhausted residual paths contribute no
straight environment leak.

The typed `maxOpticalQueries` default and maximum are 16. The optical suite uses
16 for regular cases, one for the deliberately exhausted slab, and three for
the inside-prism test: one membership bootstrap, one boundary query and one
coincidence validation query allow a real TIR event before further work is
denied. Transparent crossings require both nearest-hit and coincidence queries;
opaque-only scenes select the smaller plain shader variant and need one query
per admitted path. Completed `ReflectionSmokeOptics` lines report the frozen
variant and query cap alongside actual query, bootstrap, transparent-path,
unsupported, limited, ambiguous, TIR and medium-overflow counters. They match
the existing completed sequence/generation/token metadata. Actual queries must
remain within `hardwareRays * maxOpticalQueries`; primary path counts are not
relabeled as scene-query counts. These counters establish bounded work, not GPU
speed. No complete secondary path tracer or reflected caustic-cache lookup is
claimed.

The captured opaque-only reference issued 217,668 hardware paths and exactly
217,668 scene queries using the plain kernel. The single clear slab used the
same 217,668 admitted paths and 1,088,340 queries; three nested volumes used
2,829,684. This is five and thirteen actual queries per path respectively,
including interface coincidence validation. The three-query TIR-limit fixture
reported real TIR events before exhausting its paths, and independent coincident
instances reported ambiguity. These are completed work counters rather than
timing measurements. Actual images, logs, final source hashes and metrics are
under `Testing/smoke/dbg/reflection_optical_gpudbg` in the build directory.

Run `python tests/integration/reflection_optical_smoke_tests.py` for the physical
identities and negative image/counter cases, and
`python tests/smoke/generate_reflection_optical_meshes.py --check` to verify the
closed disconnected-box and TIR-prism assets without rewriting them. The optical
capture command uses the same executable, working-directory and logserver
arguments as the earlier suites, with `--suite optical`. Its CTest name is
`nwb_reflection_optical_capture_smoke`. Interactive launch supports
`--reflection-case optical_nested3` and `--reflection-optical-queries 16`.
Actual BMPs, lossless PNGs, completed logs, an offline HTML gallery and the
reference metrics are retained in the requested output directory.

`caustic_optical_smoke.py` separately captures the existing caustic sphere scene
at presentation frame 360 with all effects enabled, reflection disabled,
caustics disabled and camera refraction disabled. The test-only
`NWB_CAUSTIC_SMOKE_REFLECTION_COMPARISON=1` setup selects a fixed bright
reflection environment and disables reflection history/spatial filtering;
the ordinary caustic fixture keeps its defaults. Its known closed sphere uses
`ClosedNested`; ground F0 stays zero. The reflection toggle must produce a
visible sphere contribution while retaining the nonreflective ground control.
The caustic toggle must remove a positive ground contribution, and camera
refraction must alter the transmitted sphere image. This is a combined visual
integration check, not a claim that secondary hit shading can reuse the
primary-camera caustic cache. The script accepts the caustic executable and the
same working-directory/output/logserver/`--require-hardware` arguments as the
other capture runners. Its CTest name is `nwb_caustic_optical_capture_smoke`.

The final four combined framebuffer captures pass the end-to-end harness:
reflection changes 203,862 sphere pixels, camera refraction changes 10,156,
and disabling caustics changes 36,229 ground pixels outside the sphere
silhouette. Neither reflection nor camera-refraction toggling changes a ground
pixel beyond the comparison threshold. The primary-glass reflection still
requires AVBOIT optical composition when camera refraction is disabled: that
variant reports the fixture's disabled camera-refraction state and uses the
screen-space composition route, while hardware camera refraction is rejected.

A geometry-based exterior oracle additionally selects 7,032 regular-grid samples
whose reflected rays escape both the authored sphere mesh and the ground. It
predicts their environment contribution from the disabled image, IOR 1.5,
Schlick Fresnel and the fixed Reinhard/sRGB transform. All samples pass, with
zero missing reflections, a minimum predicted-gain fraction of 0.97196 and RGB
byte MAE 0.22628. This catches local dark patches that whole-image contribution
counts can miss. The oracle rejects the saved defective image at seven samples.
The final images, predictor metadata and lossless gallery are retained under
`Testing/smoke/dbg/caustic_stage6_final_gpudbg`; the optical/combined CPU analysis
suite passes 44 tests.

Containment records remain immutable after candidate collection; bootstrap sorts
scalar indices and constructs the ordered active media once. A GPU diagnostic
identified the original destructive struct insertion losing a reversed entry/exit
pair. The repair preserves geometry checks and physical transport limits instead
of weakening the ambiguity policy. The 30-case optical suite passes again with
the repaired shader under `Testing/smoke/dbg/reflection_stage6_final_optical_gpudbg`.

The neutral mesh resolver distinguishes no geometry attachment from an attached
mesh whose descriptor/resources are unavailable. A model parent may carry renderer
settings without a mesh; its spawned children supply geometry. Such parents do
not invalidate optical completeness, while missing attached geometry still does.
Valid opaque deformation disables temporal trust without suppressing the plain
reflection kernel. Five behavior tests cover absent/static/runtime bindings and
existing static fallback; all 342 ECS tests pass. All 12 temporal captures pass
under `Testing/smoke/dbg/reflection_stage6_mesh_temporal_gpudbg`, including the
posed red model's reflected footprint changing from 729 to 4,604 pixels with the
4,422-pixel green control retained.

## Stage7: accepted screen-trace feedback

The optional typed `screenFeedbackEnabled` setting defaults to false. It records
smooth opaque/glass tile classes whose screen traces repeatedly miss, and reuses
only compatible accepted observations. Any positive-confidence screen return
keeps a class active, including provisional results. Dormant classes receive
periodic probes on a 16-observation cadence. A conservative previous receiver
upper bound must fit the current hardware budget before the GPU may bypass a
screen trace. Changes in scene, view, settings, resources or hardware readiness
reset the accepted feedback state. Image history remains independent.

Two banks of 8x8 tile/surface-class entries occupy 172,832 bytes at 960x720,
including their 16-byte headers, about 168.8 KiB. The 953x713 NPOT fixture has the
same rounded tile extent. Existing classification and argument-building tasks
write disjoint entry/header ranges; feedback adds no separate GPU dispatch.
The shared parameter block is 192 bytes and diagnostic counters are 96 bytes.

The leading parameter lane is an explicit `uint4` containing width, height,
trace mode and hardware-enabled state, preserving the 192-byte ABI. GPU probes
isolated incorrect leading-field reads in the production classification path:
the CPU upload and a minimal GPU reader agreed, while that path observed
incorrect mode/width values and performed no reflection work. The explicit lane
restores the contract. Offset checks cover all four fields. This evidence does
not identify a particular compiler or driver stage as the cause.

Run the separate 24-capture suite through
`reflection_smoke.py --suite feedback`, using the same executable, Testing
working directory, output directory and logserver arguments as the other
suites. `--feedback-cases offscreen_baseline,offscreen_feedback` selects a bounded
pilot. New cases are `feedback_boundary`, `feedback_mutation` and
`feedback_long_miss`; interactive launch accepts `--reflection-feedback on|off`,
`--reflection-screen-steps 8..256` and `--reflection-extent native|npot`. The
ordinary framebuffer is 960x720; the fixed NPOT preset is 953x713.

All 24 captures pass with GPU debugging enabled. The nine smooth off/on image
pairs are byte-identical, including opaque plus primary glass, provisional
results, zero/64-ray budgets, ScreenSpace routing and the NPOT floor. The rough
guard issues no bypass and passes its independent furnace energy check, with
mean linear albedo error 0.012654. Completed counters preserve all 179,684
accepted floor SSR hits per frame, and all 175,784 NPOT floor hits. The
provisional wall preserves 3,352 positive-confidence returns with zero accepted
SSR hits; its 64-ray budget prevents feedback bypass. Zero and 64-ray floor
budgets, rough receivers and ScreenSpace-only routing also report zero bypass.

The completed steady observations measure the following work. Each row averages
36 completed source-frame observations; the means include periodic probe phases
and are not GPU timing publication-window averages.

| Scene | Mean SSR attempts, off → on | Mean actual hierarchy loads, off → on |
| --- | ---: | ---: |
| Offscreen markers, 96 steps | 217,668 → 13,586 | 1,950,558 → 121,742.39 |
| Productive floor, 96 steps | 243,336 → 186,402.44 | 8,761,038 → 6,905,624.22 |
| Opaque plus primary glass | 64,250 → 13,421.42 | 1,047,400 → 352,956.06 |
| NPOT floor, 953x713 | 238,910 → 182,812.83 | 10,108,494 → 7,826,166.72 |
| Long floor, 16 steps | 253,978 → 22,896.19 | 4,045,736 → 348,848.31 |

The offscreen pair retains 217,668 hardware paths and 5,322 hits per steady
frame; the long-floor pair retains 251,266 paths and 153,594 hits. Both issue
actual periodic probes. The long-floor baseline independently qualifies as
costly unsuccessful traversal: 9,143,208 attempts perform 145,646,496 hierarchy
loads across 36 observations, averaging 15.92947 loads per attempt under the
16-step bound. Of those attempts, 98.61484% exhaust the step budget. A separate
96-step ScreenSpace capture verifies the direct/reflected panel positions near
`(392,246)/(568,246)` and `(392,620)/(568,620)`.

The first-mutation image is byte-identical to the fresh final scene. The green
reflection moves 90 pixels left, against the independent prediction of
90.06664 pixels, while the stationary red direct/reflected controls remain
unchanged. Both changed/fresh images capture Graphics source frame 67, exactly
matching their new feedback epoch's start. Both have exact completed-frame
evidence: epoch 2, probe index 0, reset true, reused false and zero bypass.
The scene mutation reports `SceneChanged`; the fresh scene's deliberate seed
reset reports `SettingsChanged`. Positive screen returns increase from 1,410
before the move to 3,352 on the first changed frame.

Completed metadata distinguishes requested/enabled/reused control from actual
bypass. The prior compatible potential-receiver bound must fit the hardware
budget; `feedbackReused` alone does not prove that the GPU guard passed.
Counters separately report potential receivers, any screen returns, bypassed
pixels, probed surface classes, actual 64-bit hierarchy loads and exhausted-step
misses. A latest-only completion query can skip an exact image frame. The
epoch-start anchor still identifies a reset, but the report explicitly marks
that exact-frame counter evidence unavailable. The changed/fresh captures in
this run did include the exact source observations.

The separate `--suite feedback-diagnostics-off` also passes all six captures
under GPU debugging: offscreen, floor and opaque/glass with feedback off/on.
All three image pairs are byte-identical. These frozen smooth cases disable
reflection diagnostics and its statistics-based capture predicate, then use the
ordinary readback after 65 prepared Graphics frames. Every completed readback
identifies Graphics source frame 64, and the harness rejects all reflection
statistics/history/feedback/optics logs. This establishes image equivalence
without claiming unavailable feedback counter observations. The same
`--feedback-cases` subset control is supported.

Both suites explicitly use optical query limit 16. Their manifests record
executable and authored packed-volume hashes, excluding only exact contiguous
canonical runtime pipeline-cache segments through the shared identity helper.
Raw BMPs, lossless PNGs, per-launch logs and self-contained galleries are under
`Testing/smoke/dbg/reflection_stage7_feedback_gpudbg` and
`Testing/smoke/dbg/reflection_stage7_diagnostics_off_gpudbg`. CTest entries are
`nwb_reflection_feedback_capture_smoke` and
`nwb_reflection_feedback_diagnostics_off_capture_smoke`.

These results establish image preservation and reduced measured traversal work
in the tested scenes. They do not establish a GPU speedup. See
[Reflection performance](REFLECTION_PERFORMANCE.md) for the separate optimized
diagnostics-off timing methodology and analysis of unrelated control scopes.
Traversal counters alone do not justify changing the default. The older
reflection, roughness, optical and combined-caustic regression coverage remains
separate: all 358 ECS unit tests
and six CPU test targets pass. The final 22 baseline, 14 roughness, 30 optical
and four combined-caustic captures also pass under GPU debugging. Together
with the 30 feedback captures, this completes 100 final GPU framebuffer checks.
The combined sphere retains its exterior reflection contribution with no missing
reflection samples in the independent geometric oracle. Final regression evidence
is under `Testing/smoke/dbg/reflection_stage7_{all,rough,optical}_gpudbg` and
`Testing/smoke/dbg/caustic_stage7_gpudbg`.
