# Refraction smoke and validation

The refraction smoke renders a clear IOR 1.5 sphere with zero AVBOIT color coverage in
front of an opaque stripe wall. A red transparent panel sits in front of the glass;
a blue transparent panel sits behind it. The fixed camera and unlit authored BXDF
make the image comparison independent of lighting histories.

Refraction classification uses the current full-resolution capture depth: an
8x8 tile is active when any in-bounds pixel has depth below one. This preserves
clear glass with zero AVBOIT opacity. Active tiles append to a GPU queue, with
compact indirect rows capped at 65,535 workgroups. The sparse kernel clips the
final row to the active count and partial tiles to the framebuffer extent.

A one-group finalizer runs after the classifier's UAV dependency within the same
graph task and classification timing scope. Its 32-byte argument buffer contains
sparse XYZ at byte 0, the active count at byte 12, dense XYZ at byte 16, and a
reserved zero word at byte 28. When every tile is active and the framebuffer tile
width fits the dispatch X limit, the finalizer enables the dense record and
zeros sparse X. Otherwise it enables the compact record and zeros dense X.
An empty capture has both X counts zero.

Both records are submitted inside the existing resolve task and timing scope.
The selected record runs its separately cooked sparse or dense kernel; the other
launches zero workgroups. The dense kernel maps workgroup IDs directly to
framebuffer tiles and has no queue lookup or runtime occupancy branch. Selection
uses exact current-frame occupancy, with no CPU readback or density threshold.
Both screen-space and hardware tracing prepare both kernels, including the
screen fallback pair for unavailable hardware preparation.

Build the scene and its cooked runtime assets through the repository launcher:

```powershell
python -m launcher build nwb_refraction_smoke --configure-preset windows-clang-arm64 --config dbg
```

For interactive inspection, use `python -m launcher smoke refraction --config dbg`.

Run `nwb_refraction_capture_smoke` through CTest after building
`nwb_refraction_smoke`. `nwb_refraction_gpudbg_capture_smoke` runs the same sequence
with selected-backend GPU validation enabled in `dbg`. Each test captures three swapchain images
after the same graphics frame count: refraction disabled, automatic tracing, and
explicit screen-space tracing. The latter uses the renderer's normal hardware
tracing preference, leaving device capability reporting intact.

The smoke requires successful resolve dispatch diagnostics, normal application
shutdown, and clean runtime/validation logs. Its visual checks require both dark
stripes becoming light and light stripes becoming dark inside the glass, stable
pixels outside its silhouette, and preservation of the foreground red panel.
Both edge directions are necessary so flat tinting or attenuation cannot pass as
refraction. Captures, amplified differences, and numeric measurements are written
under `Testing/smoke/<config>/refraction` in the selected CMake build directory.

To require a hardware ray-query dispatch on a known capable GPU, run
`refraction_capture_smoke.py` with `--require-hardware`, together with its required
`--executable`, `--working-directory`, and `--output-directory` paths. The normal
CTest accepts the engine's natural route on GPUs without ray queries and always
checks the explicitly selected screen-space route. Lack of swapchain readback
support is reported as a skip; validation failures remain failures.

Interactive fixture controls are `NWB_REFRACTION_SMOKE_ENABLED=0` for the baseline
and `NWB_REFRACTION_SMOKE_HARDWARE=0` for screen-space tracing. They call public
renderer settings and exist only in the smoke project.

The lifecycle smoke runs seven completed application framebuffer captures in one
process: refraction disabled, active centered glass, glass moved to other tiles,
glass intersecting the right framebuffer edge, glass moved offscreen, refraction
disabled again, and refraction reenabled. It uses a 967x727 framebuffer and a wider
stripe backdrop so the last seven columns of the partial 8x8 tile contain a
measurable refracted image. The checks require both stripe-displacement directions
in the broad edge region and displacement of each sufficiently represented
baseline stripe polarity inside the partial tile, restoration of the old glass region after movement,
neutral empty and disabled frames, repeatable reactivation, and preservation of
the foreground red AVBOIT panel throughout.

The moved and edge comparisons cover the projected sphere bounds. A separate
moved-region check beyond the original sphere silhouette proves that new tiles
became active. Hardware tracing must displace the partial tile's available stripe
polarity. When the runtime explicitly reports screen-space tracing, the partial
tile may instead match the clear sheet's bounded straight fallback: the screen
march rejects offscreen or inconsistent depth crossings, then preserves the
background with two Fresnel factors. The fallback check reconstructs the fixed
fixture's IOR 1.5 Schlick transmission and default Reinhard/sRGB presentation from
the disabled reference. It requires neutral, nonblack attenuation within 1.5
encoded levels at at least 95% of eligible tail pixels, with a change of at least
one level; unchanged, black, colored, or excessively darkened tails fail. The full
edge region still requires stripe displacement in both directions on every route.

Run `nwb_refraction_lifecycle_capture_smoke` after building the same target;
`nwb_refraction_lifecycle_gpudbg_capture_smoke` enables GPU validation in `dbg`.
Both run automatic and explicitly selected screen-space tracing. Captures include
the source graphics-frame identity for each phase and are retained under
`Testing/smoke/<config>/refraction_lifecycle`. The test-only fixture controls are
`NWB_REFRACTION_SMOKE_LIFECYCLE=1` and `NWB_REFRACTION_SMOKE_EXTENT=npot`.
`refraction_lifecycle_smoke.py --require-hardware` additionally requires a hardware
resolve in its automatic run.

For direct GPU scheduling coverage, build `nwb_avboit_depth_warp_probe_smoke`
and `nwb_logserver` in the same configuration, then run:

```powershell
ctest --test-dir __cmake/build/windows-clang-arm64 -C opt -R "^nwb_avboit_depth_warp_probe_smoke$" --output-on-failure
```

The refraction portion uses the cooked production classifier, its one-group
argument finalizer, and both specialized shared resolver mappings at 2049x2049,
including the partial edge tiles. Its six phases reuse the
same resources with 66,049, zero, six sparse, 65,535, 65,536, and 66,048 active
tiles; the last case omits an interior tile while keeping the final tile active.
It checks every byte of the 32-byte indirect argument layout, queue membership
and uniqueness, every tile and pixel visit, row tails, edge lanes, and sentinel
guards with GPU validation. Each phase records both indirect dispatches and
separate route counters: the inactive route has zero groups, and the empty phase
has no verifier groups. Exactly full occupancy selects the native 257x257 dense
grid; every nonempty sparse phase uses the compact grid capped at 65,535 groups
per row. The dense verifier receives a separate list of valid reversed tile IDs,
while the CPU still validates the classifier's original list. Requiring the
mapped tile to equal the dispatch index proves that the dense path bypasses
queue order. The all-but-one case must retain sparse queue mapping, including
its bounded indirect tail. The smoke
requires all six phases and 25,190,406 pixel checks before reporting success.

For uncaptured timing acquisition, set `NWB_REFRACTION_SMOKE_TIMING=1` and
`NWB_GPU_TIMING_FILE` to an output path. The fixture uses the shared AVBOIT timing
observer, including refraction classification and resolve, AVBOIT stages, frame
time, and unaffected control passes. It keeps rendering unfocused through the
existing render-pass policy. Keep timing runs separate from serialized framebuffer
lifecycle captures and compare classification plus resolve and whole-frame cost
with matched binaries, packed assets, extent, trace route, and acquisition
conditions. The normal fixture uses 960x720; `NWB_REFRACTION_SMOKE_EXTENT=npot`
selects 967x727 for either capture or timing.

The density fixture is opt-in through `NWB_REFRACTION_SMOKE_DENSITY`, which accepts
exactly `0`, `1`, `10`, `50`, or `100`. It requires an explicit
`NWB_REFRACTION_SMOKE_EXTENT=960x720`, `1600x900`, or `1920x1080`, and rejects density combined
with a gallery case or lifecycle capture. Empty or unknown density/extent values
are errors. Refraction stays enabled; `NWB_REFRACTION_SMOKE_ENABLED` must be absent
or exactly `1`. Hardware preference must be absent, `0`, or `1`, with the existing
`NWB_REFRACTION_SMOKE_HARDWARE=0` selecting screen-space tracing. The ordinary
sphere and lifecycle fixtures retain their existing defaults when density is not
selected; `native` and `npot` remain extent options for those fixtures.

Each density uses one closed, hard-edge clear glass slab with IOR 1.5, zero AVBOIT
coverage, and thickness 0.2. The eye is at `(0, 1.4, -6)`, the vertical FOV is
60 degrees, and the aspect ratio is set explicitly from the requested extent.
The visible slab starts at Z=0. Its centered front-face dimensions derive from
the perspective projection and even integer pixel counts nearest
`width * sqrt(percent / 100)` and `height * sqrt(percent / 100)`. This puts the
projected rectangle on integer pixel boundaries. The 100% slab extends one pixel
past every framebuffer edge, ensuring full coverage. The 0% slab instead starts
at Z=4 behind the fully opaque Z=2.8 backdrop, while remaining in the frustum.
It preserves one submitted refractor and a live refraction path with an empty
capture, rather than measuring the renderer's no-transparent-object bypass.

The camera, directional light, unlit opaque backdrop, and 24 light stripe panels
are the same for each paired comparison. An opaque base closes possible stripe
seams and guarantees full occlusion in the empty case. There are no additional
transparent panels in this fixture, reflection stays disabled, and simulation
always advances by 1/60 second. The finite closed slab has a valid exit and a
fixed 12-triangle mesh, keeping dense hardware traversal bounded.

`RefractionDensity:` diagnostics report requested extent/percentage, the admitted
pixel rectangle, its pixel count, the corresponding 8x8 tile count, one glass
instance, and `capture_mode visible` or `occluded`. These counts are analytical
admission from the authored projected rectangle, not GPU counter readbacks.
The tracing diagnostic distinguishes requested route from RayQuery capability;
the normal resolve diagnostic remains the evidence for actual hardware or
screen-space dispatch. Run capture and GPU validation separately from timing and
verify the framebuffer extent before accepting a density result.

| Requested density | 960x720 admitted pixels / tiles | 1600x900 admitted pixels / tiles | 1920x1080 admitted pixels / tiles |
| --- | ---: | ---: | ---: |
| 0% | 0 / 0 | 0 / 0 | 0 / 0 |
| 1% | 6,912 / 120 | 14,400 / 240 | 20,736 / 360 |
| 10% | 69,312 / 1,140 | 143,704 / 2,304 | 207,936 / 3,268 |
| 50% | 345,780 / 5,504 | 719,952 / 11,360 | 1,037,512 / 16,490 |
| 100% | 691,200 / 10,800 | 1,440,000 / 22,600 | 2,073,600 / 32,400 |

`refraction_density_capture_smoke.py` independently qualifies the rendered density
before timing. Supply the normal executable, working-directory, output-directory,
and optional logserver paths. Its defaults capture all 20 density/extent/route
combinations at 960x720 and 1600x900; `--densities`, `--extents`, and `--routes` accept comma-separated
subsets. Each extent/route always captures its 0% occluded reference even when
zero is absent from the requested subset. `--application-arg=--gpudbg` enables
validation for every captured process.

1920x1080 remains available through explicit `--extents=1920x1080` on hosts with
adequate resource-allocation limits. The renderer allocates its full 32-layer CSG
target independently of glass coverage; that target can exceed a device's single
allocation limit at this extent. Exact window sizing does not establish resource
admission. A runtime allocation warning rejects the capture, and an extent with
such a warning remains unqualified rather than falling back to a smaller image.

Capture protocol 2 sets the observer's required width/height to the requested
extent and keeps the process alive after readback. The harness inspects the
actual native client size and uses the test backend's `resize_client` operation
when startup clamps the window to the desktop work area. The density harness
opts into sizing this test window beyond the work area; it does not change the
display mode. It requires both the exact client size and the matching runtime
framebuffer-resize diagnostic before accepting readback, then closes the
application through its normal window shutdown. The final log and image must
confirm the target extent. An initial clamped size is retained as evidence and
accepted only after that confirmed transition; it never substitutes for the
requested image size. Metrics record the protocol, initial and confirmed client
extents, runtime framebuffer extents, and whether a resize was requested.

The zero reference must match the default unlit neutral stripe levels and remain
constant vertically. Positive captures report actual changed pixel bounds/counts
against independent projected-rectangle math, rather than using the fixture's
logged count as image evidence. Pixels outside a one-pixel rectangle boundary
guard must remain exactly unchanged. At least 99% of the inset rectangle must
change, and every sufficiently populated 8x8 block must have at least 90% changed
pixels, rejecting a missing local tile even when global coverage remains high.
The interior must retain both nonblack neutral stripe polarities and match the
IOR 1.5 Schlick factors through default Reinhard/sRGB presentation within two
encoded levels. Hardware uses the closed slab's entry/exit cosines; screen-space
uses its existing single-interface sheet factors. Per-block physical checks also
reject small black or tinted holes.

Only the flat front/back Fresnel comparison excludes a separate, analytically
derived inner edge band: near a finite slab's silhouette, a ray may exit its side
and undergo total internal reflection. This band derives from thickness, FOV,
view angle, and pixel extent and is recorded in the metrics. All pixels in that
band still participate in the changed-mask and local coverage checks. This is
distinct from the one-pixel outside-mask allowance. Captures, runtime logs, and
`density_capture_metrics.json` retain the measured bounds, coverage, and physical
acceptance counts. CPU oracle tests reject absent or partial glass, one missing
tile, output outside the mask, a wrong-pitch footprint, black/tinted/excessively
attenuated output, and a corrupted zero reference.

For a separate frame-graph diagnostic, set
`NWB_REFRACTION_FRAME_GRAPH_SNAPSHOT_PATH` to a nonempty output JSON path whose
parent directory already exists. This mode rejects simultaneous timing,
lifecycle, framebuffer readback, or frame freeze. It waits until the runtime has
held the requested extent for at least 360 completed graphics frames, resetting
that interval if the extent changes. On the next update it snapshots the public
registry's preceding accepted frame before submitting more scene work, writes
the JSON and companion `.nwbs` capture, and requests normal application shutdown
after the single capture. The `RefractionFrameGraphSnapshot:` marker records the
target extent and completed-frame interval; `StressFrameGraphSnapshot:` records
the captured source-frame identity and graph counts. Acquire this diagnostic
separately from image qualification and measured timing.

Use `refraction_density_benchmark.py` for matched continuous timing after the
independent image and GPU-validation gates pass. Prepare two frozen arms from
the same fixture and toolchain before launching either arm:

1. Build and cook the tiled candidate normally, then copy its executable,
   sibling loader dependencies and crash helper, logserver, name-symbol file,
   and complete runtime `res` tree into a separate candidate directory.
2. Check and apply `tests/smoke/refraction_direct_reference.patch` to the same
   renderer sources, rebuild and recook with the same configuration, and copy
   those outputs into a distinct baseline directory. The patch removes tile
   clear/classification graph tasks and their queue/argument resource uses. It
   directly dispatches the prepared dense kernel over the full framebuffer grid,
   retaining both arms' pipeline preparation, snapshots, and resource lifetimes.
   Optical shaders, AVBOIT, current-depth compositing,
   the density fixture, and target allocation remain common to both arms.
3. Save each arm's compiled source bytes and a manifest with a nonempty
   `revision` string and `files` object mapping relative paths to SHA256 hashes.
   Include the exact renderer patch and the compiled fixture files
   `tests/smoke/refraction_project.cpp` and
   `tests/smoke/avboit_timing_render_pass.cpp`. Restore the tiled sources with
   `git apply -R tests/smoke/refraction_direct_reference.patch` after freezing the
   reference. Keep compilation and cooking separate from measured acquisition.

Use `git apply --check tests/smoke/refraction_direct_reference.patch` before
applying the reference; a context failure requires updating and reviewing that
experimental patch against the current renderer. Git attributes keep the reference
patch in LF format for application on Windows. The manifest records the compiled
source state, including the reference differences. Source manifests
are checked against saved file bytes. The runner also requires identical
compiled density fixture hashes, distinct executable/runtime directories, and
stable executable/dependency, packed-volume, logserver, and optional name-symbol
identities before and after each trial. Mutable pipeline caches are recorded
separately. A frozen source directory should contain the ordinary repository
paths below its `source/` directory so fixture identity admission can resolve
those paths unambiguously.

For a scheduling-only comparison, retain a frozen unmerged tiled arm as the
baseline, freeze a separately built tiled candidate with the proposed scheduling
change, and pass `--baseline-scheduling=tiled`. Do not apply the full-frame patch
to that baseline. Both arms must publish tile clear and classification, and both
costs remain in the aggregate. The default `--baseline-scheduling=full-frame`
requires the full-frame reference to omit those scopes. Choosing the wrong mode,
missing a required tiled scope, or mixing declared strategies rejects the
comparison. The plan, launch, trial, and comparison metadata record the strategy
and reference contract alongside frozen identities.

For example, with each frozen arm containing `bin/`, `runtime/`, and
`source_manifest.json`, the following PowerShell command acquires the complete
default twenty-cell matrix in two exploratory balanced blocks:

```powershell
python tests/smoke/refraction_density_benchmark.py `
  --baseline-executable frozen/baseline/bin/refraction_smoke.exe `
  --baseline-runtime frozen/baseline/runtime `
  --baseline-logserver frozen/baseline/bin/logserver.exe `
  --baseline-source-manifest frozen/baseline/source_manifest.json `
  --candidate-executable frozen/candidate/bin/refraction_smoke.exe `
  --candidate-runtime frozen/candidate/runtime `
  --candidate-logserver frozen/candidate/bin/logserver.exe `
  --candidate-source-manifest frozen/candidate/source_manifest.json `
  --output-directory density-timing `
  --blocks 2
```

Add `--baseline-namesym` and `--candidate-namesym` with the actual generated
symbol paths when the build's timing output requires symbol resolution. Add `--plan-only` to
validate frozen inputs and inspect the cell/order plan without launching the
applications. Repeated `--extent`, `--coverage`, and `--trace-route` arguments
select subsets. Two blocks alternate reference/candidate order and are
descriptive; the default is four blocks, and an even count of at least six
enables paired bootstrap intervals. An interval for one selected metric does
not establish simultaneous confidence across the whole density matrix.

Each trial confirms the native client and final deferred-target extent, discards
startup and unsealed GPU reports, then requires at least two target-only warmup
intervals and 100 completed GPU frames. It retains at least twelve positive,
sealed report intervals. It uses fixed simulation delta and continuous
render-unfocused behavior, with framebuffer readback, frame-graph snapshots,
and GPU validation absent during measurement. Runtime warnings, an incorrect
actual trace route, or missing scope coverage reject acquisition.

Compare argument clear + classifier + resolve + compositor GPU duration per
completed GPU frame. A full-frame reference omits tile-only clear/classifier
scopes; a tiled reference and every candidate must publish both, including at 0% coverage. Keep
individual scopes and whole-frame timing alongside the aggregate; resolve alone
does not account for scheduling cost. Report opaque, shadow, lighting, and
presentation controls to expose drift. A callback stall over the declared
threshold rejects wall-time eligibility while retaining the raw evidence;
callback timing includes pacing and does not establish CPU cost or presentation
cadence. GPU acquisition success, clean wall acquisition, and a performance
conclusion are separate decisions. Retain `plan.json`, all per-trial logs and GPU
reports, summaries, and failures when reporting the result.

`refraction_query_profile_benchmark.py` profiles one frozen renderer at hardware
refraction coverage 50% and 100%, each at 960x720 and 1600x900. Its default eight
blocks comprise two complete four-treatment Williams cycles. Every treatment
appears twice at each position and follows each other treatment twice within
blocks. The plan also records the predecessor across block boundaries. This
balances workload order while preserving every trial; it does not prove that
thermal or power state has reached equilibrium.

```powershell
python tests/smoke/refraction_query_profile_benchmark.py `
  --executable frozen/bin/refraction_smoke.exe `
  --runtime frozen/runtime `
  --logserver frozen/bin/logserver.exe `
  --source-manifest frozen/source-manifest.json `
  --output-directory refraction-query-profile
```

Use `--plan-only` to validate identities and save the complete trial order before
launch. Source manifests must include both compiled density fixture files. Each
trial confirms the actual extent, discards the startup/unsealed boundary, then
discards at least 30 seconds of target-only sealed publication intervals and
100 completed GPU frames. It retains twelve or more positive sealed reports
with at least 100 GPU frames. Warmup is fixed before acquisition; the runner does
not select a favorable plateau or remove slow trials. Existing GPU timing
instrumentation remains enabled, while shader query counters, image readbacks,
frame-graph capture, and validation are acquired separately. Inherited smoke
diagnostic settings are cleared by timing environment preparation.

On Windows, a persistent PDH query records GPU Engine utilization at 1 Hz for
both the fixture PID and other processes. Raw counter instances, statuses,
monotonic/UTC observation times, and publication-header observations are saved.
Initial invalid data, missing fixture instances, and gaps remain visible; at
least two valid fixture samples must overlap the approximate retained
publication window. Per-engine process sums remain separate because concurrent
engines cannot be added into one utilization percentage. Header publication may
lag GPU execution, so load associations are observational. Before acquisition,
the host inventory records the adapter/driver, exposed GPU counter paths,
clock/frequency counter candidates, and whether `nvidia-smi` is present. The
sampler collects engine utilization only; clock measurements require separate
supported instrumentation. Anonymous ACPI thermal zones do not identify GPU
temperature. The output records these limitations explicitly and does not infer
clock state from load.

The report keeps whole-frame, resolve, scheduling/compositor aggregate, AVBOIT,
opaque, shadow, lighting, presentation, per-position, and per-block values.
Coverage scaling uses paired independent blocks with individual bootstrap
intervals. A coverage change changes the optical workload; these values guide
diagnostic attribution and do not establish an optimization speedup. Keep shader
query/fallback counts in a separate diagnostic acquisition against the same
scene and route, and measure timing with the uninstrumented frozen arm.

When the calling terminal has a shorter lifetime than a complete campaign, use
`run_refraction_profile_supervised.ps1` from PowerShell 7 in a detached hidden
process. First save a separate `--plan-only` output for the original eight-block,
32-trial protocol. Pass that plan and the same frozen arm explicitly; use a fresh
output directory for actual acquisition:

```powershell
$profileRoot = (Get-Location).Path
$profileSupervisor = (Resolve-Path tests/smoke/run_refraction_profile_supervised.ps1).Path
$profileArm = (Resolve-Path frozen).Path
$profilePlan = (Resolve-Path refraction-query-plan/plan.json).Path
$profileOutput = Join-Path $profileRoot refraction-query-profile
$profileSupervisorArguments = @(
  '-NoProfile', '-NonInteractive', '-File', ('"{0}"' -f $profileSupervisor),
  '-RepositoryRoot', ('"{0}"' -f $profileRoot),
  '-FrozenArm', ('"{0}"' -f $profileArm),
  '-ExpectedPlan', ('"{0}"' -f $profilePlan),
  '-OutputDirectory', ('"{0}"' -f $profileOutput)
) -join ' '
Start-Process -FilePath (Get-Command pwsh.exe).Source `
  -ArgumentList $profileSupervisorArguments -WindowStyle Hidden
```

The supervisor checks acquisition-script hashes and original protocol/arm paths
before launching Python, retains the native child handle, and records a running
heartbeat plus final exit code in `<output>-supervisor/process-status.json`.
The same control directory retains `stdout.log` and `stderr.log`. `-ProbeOnly`
with a separate output name verifies that the hidden supervisor survives its
originating command, without starting Python or GPU rendering. An interrupted
campaign remains incomplete; a new complete schedule uses a new output directory
and does not combine selectively replayed trials with partial data.

The public renderer diagnostic opt-in is
`RendererSystem::setRefractionDiagnosticsEnabled(true)`. It selects separately
cooked hardware kernels with counters and asynchronous completed readback.
`tryGetLatestRefractionStatistics()` returns a completed accepted copy, or failure
until one is available; it does not wait for the GPU or promise the current
frame. The latest publication may remain available while newer work is pending.
Use its sequence/generation, graphics source frame, extent, hardware readiness
and accepted physical submission token to establish freshness and route. An
accepted diagnostic copy does not establish that a later presentation succeeded.

The three-slot readback ring skips a new sample when full while rendering
continues. Statistics expose entry association, volume exit, internal reflection,
background continuation, cache and fallback work. Counters are 32-bit, and any
nonzero `overflowFlags` invalidates that sample. Ordinary logical stage queries
each issue one physical inline query; certified single-refractor front boundaries
associate directly from the capture without an entry query (`entryQueries = 0`,
`entryHits = activePixels`), while CSG may perform additional physical work, so
`physicalQueries` and `queryBudgetUnits` remain separate. Counter shares do not
establish GPU-time shares. Background hits evaluate only `refractionIor` and
`renderCoverage` before consulting the screen-space lighting cache; full shading
surface reconstruction (`nwbRefractionShadeGeometryHit`) runs only when an
uncached opaque hit or transparent layer calls the refraction lighting helper
(`materialShadeCalls`).

`refraction_query_diagnostics_smoke.py` acquires diagnostic counts and optical
checks separately from timing:

```powershell
python tests/smoke/refraction_query_diagnostics_smoke.py `
  --executable __exec/windows/arm64/full/opt/refraction_smoke.exe `
  --working-directory __cmake/build/windows-clang-arm64/Testing/smoke_runtime/opt `
  --logserver-executable __exec/windows/arm64/full/opt/logserver.exe `
  --reference-directory uninstrumented-density `
  --output-directory refraction-query-diagnostics `
  --application-arg=--gpudbg
```

The default matrix captures ordinary hardware density 0%, 50% and 100% at
960x720 and 1600x900. `--extents` and `--densities` accept comma-separated
subsets; every selected extent still captures its zero-density reference.
`--reference-directory` enables exact RGB parity against uninstrumented captures
arranged as `<extent>/hardware/NNN.bmp`. Without it, independent projected-mask,
stripe and Fresnel checks still apply, while exact parity is unqualified. The
oracle also requires valid hardware-ready completed statistics, no counter
overflow, bounded loop counts and ordinary query/terminal-path conservation.
These ordinary captures do not qualify CSG physical-query counts.

The smoke-only selector `NWB_REFRACTION_SMOKE_DIAGNOSTICS=1` requires a hardware
density framebuffer capture with timing, lifecycle and frame freeze disabled.
The fixture waits at least sixteen graphics frames at the requested target
extent and requires a statistics source after that boundary. Its later image
source follows statistics publication. Both describe the same static scene;
they are not claimed to be the same GPU frame. The JSON retains both identities,
raw counters, token/readiness metadata, extent evidence and optical checks.

The complex-case gallery is an additional visual inspection fixture. It covers
`single`, `separate`, `stacked`, `intersecting`, `nested`, `coincident`,
`coincident_tinted`, `torus`, `same_mesh`, and `prism`, plus twelve exact-duplicate
references and controls. Select a case interactively
with `--refraction-case <case>` on the smoke launcher. Add `--refraction-geometry`
to show the same arrangement with colored translucent materials, normal-based
shading, a neutral backdrop, and refraction disabled. This makes overlapping and
nested geometry easier to inspect.

```powershell
python -m launcher smoke refraction --config dbg --refraction-case torus
```

Generate all 128 framebuffer captures across 32 cases and four variants, plus a portable offline gallery:

```powershell
python tests/smoke/refraction_gallery_smoke.py `
  --executable __exec/windows/arm64/full/dbg/refraction_smoke.exe `
  --working-directory __cmake/build/windows-clang-arm64/Testing/smoke_runtime/dbg `
  --logserver-executable __exec/windows/arm64/full/dbg/logserver.exe `
  --output-directory __artifacts/refraction-gallery `
  --require-hardware
```

Use the matching executable and runtime paths for another architecture or build
configuration. `--cases nested,torus,prism` limits the scenes;
`--variants geometry,automatic,screen,disabled` selects the captures. Both options
accept comma-separated values. Each child run captures after sixteen graphics frames by
default and has a sixty-second capture timeout. `--application-arg=--gpudbg` enables
GPU validation for `dbg` or `opt`; omit it for `fin`, whose loader rejects that option. The runner clears inherited case, geometry, capture, and freeze
settings so a previous interactive launch does not alter its cases.

Open `gallery.html` from the output directory to switch cases, compare variants
side by side, zoom the scene, or inspect original pixels. The self-contained HTML
embeds losslessly encoded PNGs with exactly the framebuffer's RGB values. Original
BMP readbacks and individual PNG copies remain beside it, and `gallery_manifest.json`
records the cases, capture settings, application arguments, and descriptive
differences from the disabled baseline. The geometry preview is excluded from
those differences because its materials and backdrop intentionally differ.

The gallery checks clean logs, successful route dispatch, orderly shutdown,
nontrivial frames, and visible differences when the disabled baseline is selected.
It does not assert physical correctness for complex media. The current renderer
retains one primary refractor per pixel and has bounded internal reflection and
transparent continuation. Once the first volume has a valid exit, additional
glass uses a background approximation from that exit, preserving the primary
exit direction, measured thickness, absorption, and Fresnel without tracing extra
rays. It does not trace the remaining chain of volumes. Invalid first exits,
intersections, and nested media still use a conservative fallback from the entry.
Each case includes its relevant limitation. `--require-hardware` confirms that a
hardware resolve was dispatched; it does not imply every pixel stayed on that
path. The original single-sphere smoke remains the stricter regression test for
stripe displacement and foreground transparency preservation.

The default `RendererComponent::opticalVolumeCoincidence` policy is `Independent`,
which preserves authored surfaces. `IdenticalMaterial` opts into grouping exact
static duplicates with the same mesh, material, effective surface inputs, and
transform. `SharedGroup` additionally requires a nonempty `opticalVolumeGroup` and
permits differing mutable surface inputs. Both merge modes assert that geometry and surface hooks are independent of dense
instance identity; `SharedGroup` also asserts that the
differing inputs preserve the same boundary. The material asset, mesh, and exact
transform must still match. Higher `opticalVolumePriority` wins, with the lowest
full entity identity breaking equal-priority ties. Runtime/deformed meshes and CSG
are excluded.
Grouping also prevents duplicate tint when camera refraction is disabled; the
nonrefractive geometry preview continues to show every authored entity.

The strict duplicate regression uses radius-1.1 cool and warm single references,
with both zero coverage and coverage 0.3. It compares opted-in identical duplicates,
explicitly grouped conflicting tints, reversed creation order, and swapped
priorities with the selected single reference. A translated near-overlap and
independent coincident surfaces must retain visible separate contributions. The
existing radius-1.15 `single` gallery case is not used as their reference.

```powershell
python tests/smoke/refraction_duplicate_smoke.py `
  --executable __exec/windows/arm64/full/dbg/refraction_smoke.exe `
  --working-directory __cmake/build/windows-clang-arm64/Testing/smoke_runtime/dbg `
  --logserver-executable __exec/windows/arm64/full/dbg/logserver.exe `
  --output-directory __artifacts/refraction-duplicates `
  --require-hardware --application-arg=--gpudbg
```

The default run captures thirty-three framesets: fourteen cases on each of the
automatic and screen-space routes, plus five refraction-disabled controls.
`--variants automatic` or `--variants screen` limits the optical route. Every
capture uses sixteen graphics presentation frames, completed GPU readback, clean
logs, and normal shutdown. Duplicate
matches permit at most two levels of channel rounding and a mean absolute RGB
error of 0.02/255 across the full frame. Distinct controls must change at least
sixty-four pixels by more than two channel levels; separate cool/warm reference
checks prevent a priority test from passing with indistinguishable materials.
`duplicate_comparisons.json` records successful comparisons, while `gallery.html`
and raw BMP/PNG captures are written before image comparison so a failed oracle
can be inspected. The corresponding CTest is
`nwb_refraction_duplicate_capture_smoke`; CPU-only oracle tests are registered as
`nwb_refraction_duplicate_analysis_unit`.

The caustic-sphere fixture also explicitly enables camera refraction while keeping
its original glass material, light, receiver, and photon caustics. Launch it with:

```powershell
python -m launcher smoke caustic-sphere --config dbg
```

`NWB_REFRACTION_SMOKE_ENABLED=0` disables only camera refraction for a comparison;
the glass continues casting its refractive shadow and photon caustic. Set
`NWB_REFRACTION_SMOKE_HARDWARE=0` to compare screen-space camera refraction while
leaving the caustic producer on the device's natural route. Both preferences
default to enabled when the environment variables are absent.

`nwb_software_raytracing_optical_smoke` reads back the application framebuffer after
360 graphics presentation frames, once GPU readback has completed, with a fixed
1/60-second simulation delta. It requires both
a refraction resolve and a caustic producer dispatch, clean runtime logs, a
nonempty image, and orderly shutdown. Its output is
`Testing/smoke/<config>/caustic_sphere_capture.bmp`. The existing renderer-baseline
freeze remains available for interactive/reference captures; an active framebuffer
observer takes precedence so a stale freeze setting cannot stop its readback.
