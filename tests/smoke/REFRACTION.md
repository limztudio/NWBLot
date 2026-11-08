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

A one-group end pass runs after the sort UAV need in the same graph job. Its 32-byte flag box holds thin XYZ at byte 0. It holds the live count at byte 12. It holds thick XYZ at byte 16. It holds a kept zero word at byte 28. When each tile is live and the tile width fits the send X cap, the end pass turns on the thick note. It zeros thin X. If not, it turns on the thin note. It zeros thick X. A blank grab has both X counts at zero.

Both notes go out in the old fix-up job and time span. The picked note runs its own cooked thin or thick code. The other starts zero work groups. The thick code maps group IDs straight to frame tiles. It has no list look-up and no live-count fork. Pick uses exact now-frame live tiles. It uses no CPU read-back and no full mark. Both screen and card trace ready both code sets. This holds the screen fall-back pair for no-card make.

Make the scene and its cooked run files through the repo launcher:

```powershell
python -m launcher build nwb_refraction_smoke --configure-preset windows-clang-arm64 --config dbg
```

For interactive inspection, use `python -m launcher smoke refraction --config dbg`.

Run `nwb_refraction_capture_smoke` through CTest after you build `nwb_refraction_smoke`. `nwb_refraction_gpudbg_capture_smoke` runs the same steps. It turns on picked-back GPU checks in `dbg`. Each test grabs three swap shots after the same art frame count. The three are bend off, auto trace, and set screen trace. The last keeps the normal card trace want. Card skill notes stay whole.

The smoke needs good fix-up send notes. It needs a plain app end. It needs clean run/check logs. Its look checks need dark bars turned light in the glass. They need light bars turned dark in the glass. They need firm pixels past its shape. They need the front red pane kept. Both edge ways are a must. Flat tint or fade thus cannot pass as bend. Shots, grown gaps, and count checks land under `Testing/smoke/<config>/refraction` in the picked CMake build folder.

To force a card ray-ask send on a known good card, run `refraction_capture_smoke.py` with `--require-hardware`. Give its set `--executable`, `--working-directory`, and `--output-directory` paths. Plain CTest takes the card free route on cards with no ray asks. It still always checks the set screen path. No swap-shot back-up gives a skip. Check faults stay faults.

Interactive fixture controls are `NWB_REFRACTION_SMOKE_ENABLED=0` for the baseline
and `NWB_REFRACTION_SMOKE_HARDWARE=0` for screen-space tracing. They call public
renderer settings and exist only in the smoke project.

The life smoke runs seven done app frame-mark shots in one run. The steps are bend off, live mid glass, glass moved to more tiles, glass cut by the right mark edge, glass moved off-shot, bend off once more, and bend back on. It uses a 967x727 mark and a wider bar back-wall. The last seven rows of the half 8x8 tile thus hold a weighed bent shot. The checks need both bar-move ways in the wide edge zone. They need a move of each well-shown base bar end in the half tile. They need the old glass zone fixed after move. They need plain blank and off frames. They need repeat on. They need the front red AVBOIT pane kept all through.

The moved and edge weighs span the shown ball box. One more moved-zone check past the first ball shape proves new tiles turned live. Card trace must move the half tile seen bar end. When the run plainly tells screen trace, the half tile can instead match the clear sheet capped straight fall-back. The screen walk turns down off-shot or mixed depth cuts. Then it keeps the back-wall with two Fresnel rates. The fall-back weigh remakes the set IOR 1.5 Schlick pass and base Reinhard/sRGB show from the off base. It needs plain non-black fade in 1.5 coded steps at 95% or more of fit tail pixels. It needs a move of at least one step. Same, black, tinted, or too-dark tails fail. The full edge zone still needs bar move both ways on each path.

Run `nwb_refraction_lifecycle_capture_smoke` after you build the same exe. `nwb_refraction_lifecycle_gpudbg_capture_smoke` turns on GPU checks in `dbg`. Both run auto and set screen trace. Shots hold the true art-frame ID for each step. They stay under `Testing/smoke/<config>/refraction_lifecycle`. The test-only knobs are `NWB_REFRACTION_SMOKE_LIFECYCLE=1` and `NWB_REFRACTION_SMOKE_EXTENT=npot`. `refraction_lifecycle_smoke.py --require-hardware` also forces a card fix-up in its auto run.

For direct GPU scheduling coverage, build `nwb_avboit_depth_warp_probe_smoke`
and `nwb_logserver` in the same configuration, then run:

```powershell
ctest --test-dir __cmake/build/windows-clang-arm64 -C opt -R "^nwb_avboit_depth_warp_probe_smoke$" --output-on-failure
```

The bend part uses the cooked real sort, its one-group flag end pass, and both set shared fix-up maps at 2049x2049. This holds half edge tiles. Its six steps reuse the same stores. Live tile counts are 66,049, zero, six thin, 65,535, 65,536, and 66,048. The last step drops a mid tile but keeps the end tile live. It checks each byte of the 32-byte send flag plan. It checks list hold and once-only. It checks each tile and pixel hit, row tails, edge lanes, and mark guards with GPU checks. Each step logs both send counts and split path counts. The idle path has zero groups. The blank step has no checker groups. Full live tiles pick the home 257x257 thick grid. Each live thin step uses the small grid capped at 65,535 groups per row. The thick checker gets its own list of true flipped tile IDs. The CPU still checks the sort first list. The mapped tile must match the send index. This proves the thick path skips list order. The all-but-one step must keep thin list map. This holds its capped send tail. The smoke needs all six steps and 25,190,406 pixel checks first. Then it reports good.

For shot-free time take, set `NWB_REFRACTION_SMOKE_TIMING=1` and `NWB_GPU_TIMING_FILE` to an out path. The set uses the shared AVBOIT time look-out. This holds bend sort and fix-up, AVBOIT steps, frame time, and plain kept passes. It keeps draw off-focus through the old draw-pass rule. Keep time runs apart from set frame-shot life grabs. Match exes, packed files, size, trace path, and take rules when you weigh sort plus fix-up and full-frame cost. The plain set uses 960x720. `NWB_REFRACTION_SMOKE_EXTENT=npot` picks 967x727 for shot or time runs.

The density fixture is opt-in through `NWB_REFRACTION_SMOKE_DENSITY`, which accepts
exactly `0`, `1`, `10`, `50`, or `100`. It requires an set
`NWB_REFRACTION_SMOKE_EXTENT=960x720`, `1600x900`, or `1920x1080`, and rejects density combined
with a gallery case or lifecycle capture. Empty or unknown density/extent values
are errors. Refraction stays enabled; `NWB_REFRACTION_SMOKE_ENABLED` must be absent
or exactly `1`. Hardware preference must be absent, `0`, or `1`, with the existing
`NWB_REFRACTION_SMOKE_HARDWARE=0` selecting screen-space tracing. The ordinary
sphere and lifecycle fixtures retain their existing defaults when density is not
selected; `native` and `npot` remain extent options for those fixtures.

Each density uses one closed, hard-edge clear glass slab with IOR 1.5, zero AVBOIT
cover, and thick 0.2. The eye sits at `(0, 1.4, -6)`. The straight FOV is
60 deg. The shown ratio comes straight from the asked span.
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
before time runs. Give the plain exe, work-folder, out-folder, and set log-server paths. Its base form grabs all 20 thick/size/path sets at 960x720 and 1600x900. `--densities`, `--extents`, and `--routes` take cut lists. Each size/path still grabs its 0% hid base shot, even when zero is out of the asked set. `--application-arg=--gpudbg` turns on checks for each shot run.

1920x1080 stays open through set `--extents=1920x1080` on boxes with good store-ask caps. The draw grabs its full 32-coat CSG mark apart from glass cover. That mark can pass one card ask cap at this size. Exact pane size does not prove store take-in. A run store warn turns down the shot. Such a size stays unfit. It does not fall back to a small shot.

Grab rule 2 sets the look-out asked width/height to the asked size. It keeps the run live after read-back. The cover checks the true home pane size. It uses the test back `resize_client` call when start clips the pane to the desk work zone. The thick cover opts to size this test pane past the work zone. It does not swap show mode. It needs both the exact pane size and the match run frame-mark resize note first. Then it takes read-back. Then it ends the app through its plain pane end. The end log and shot must prove the mark size. A first clipped size stays as proof. It counts only after that proved move. It never stands in for the asked shot size. Scores log the rule, first and proved pane sizes, run frame-mark sizes, and if a resize was asked.

The zero base shot must match the base dim plain bar steps. It must stay flat top to base. Good shots tell true moved pixel edges/counts. They weigh free shown-box math. They do not use the set logged count as shot proof. Pixels past a one-pixel box edge guard must stay same. At least 99% of the inset box must move. Each well-filled 8x8 cell must hold at least 90% moved pixels. A lost home tile thus fails, even when world cover stays high. The in-box must keep both non-black plain bar ends. It must match the IOR 1.5 Schlick rates through base Reinhard/sRGB show in two coded steps. Card runs use the shut slab in/out tilts. Screen runs use their old one-face sheet rates. Per-cell true checks also turn down small black or tinted pits.

Only the flat front/back Fresnel weigh drops one more set in-box edge strip. Near a thick slab shape edge, a ray can leave its side and fully bounce in. This strip comes from thick, FOV, look tilt, and pixel size. Scores log it. All strip pixels still join the moved-mask and home cover checks. This is apart from the one-pixel past-mask pass. Shots, run logs, and `density_capture_metrics.json` keep the weighed edges, cover, and true take counts. CPU true-model tests turn down lost or half glass, one lost tile, out past the mask, a bad-step foot shape, black/tinted/too-dim out, and a bad zero base.

For a split frame-plan note, set `NWB_REFRACTION_FRAME_GRAPH_SNAPSHOT_PATH` to a non-blank out JSON path whose
parent folder already exists. This mode rejects simultaneous timing,
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

Give `--baseline-namesym` and `--candidate-namesym` with the true made mark paths when the build time out needs mark fix. Give `--plan-only` to check held ins and see the cell/order plan with no app start. Repeat `--extent`, `--coverage`, and `--trace-route` flags to pick cuts. Two blocks swap base/try order and only tell. The base form is four blocks. A same count of at least six turns on paired boot spans. A span for one picked score does not prove same-time trust for the full thick grid.

Each try proves the home pane and end held-mark size. It drops start and unsealed GPU notes. Then it needs at least two mark-only warm spans and 100 done GPU frames. It keeps at least twelve good sealed note spans. It uses a set act step and non-stop draw-off-focus way. Frame-shot read-back, frame-plan shots, and GPU checks stay off in measure. Run warns, a wrong true trace path, or lost span cover turn down the take.

Weigh flag clear + sort + fix-up + mix card time per done GPU frame. A full-frame base drops tile-only clear/sort spans. A tiled base and each try must show both, plus at 0% cover. Keep split spans and full-frame times next to the sum. Fix-up alone does not hold plan cost. Tell solid, dark, light, and show kept scores to show drift. A call-back stall past the set cap turns down wall-time use but keeps raw proof. Call-back time holds pace and does not prove CPU cost or show beat. Card take good, clean wall take, and a speed call stay split calls. Keep `plan.json`, each try logs and card notes, sums, and faults when you tell the result.

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

Use `--plan-only` to check IDs and save the full try order before start. Source lists must hold both built thick set files. Each try proves the true size. It drops the startup/unsealed boundary, then
discards at least 30 seconds of target-only sealed publication intervals and
100 completed GPU frames. It retains twelve or more positive sealed reports
with at least 100 GPU frames. Warmup is fixed before acquisition; the runner does
not select a favorable plateau or remove slow trials. Existing GPU timing
instrumentation remains enabled, while shader query counters, image readbacks,
frame-graph capture, and validation are acquired separately. Inherited smoke
diagnostic settings are cleared by timing environment preparation.

On Windows, a persistent PDH query records GPU Engine load at 1 Hz for
both the fixture PID and other processes. Raw counter instances, statuses,
monotonic/UTC observation times, and publication-header observations are saved.
Initial invalid data, missing fixture instances, and gaps remain visible; at
least two valid fixture samples must overlap the approximate retained
publication window. Per-engine process sums remain separate because concurrent
engines cannot be added into one load percentage. Header publication may
lag GPU execution, so load associations are observational. Before acquisition,
the host inventory records the adapter/driver, exposed GPU counter paths,
clock/frequency counter candidates, and whether `nvidia-smi` is present. The
sampler collects engine load only; clock measurements require separate
supported instrumentation. Anonymous ACPI thermal zones do not identify GPU
temperature. The output records these limitations explicitly and does not infer
clock state from load.

The report keeps whole-frame, resolve, scheduling/compositor aggregate, AVBOIT,
opaque, shadow, lighting, presentation, per-position, and per-block values.
Coverage scaling uses paired independent blocks with individual bootstrap
intervals. A coverage change changes the optical workload; these values guide
diagnostic attribution and do not prove an optimization speedup. Keep shader
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

The boss checks take-script hashes and first rule/arm paths first. Then it starts Python. It keeps the home child grip. It writes a live beat plus end exit code in `<output>-supervisor/process-status.json`. The same kept folder holds `stdout.log` and `stderr.log`. `-ProbeOnly` with a split out name proves the hid boss lives past its start call. It starts no Python and no card draw. A cut drive stays half done. A new full plan uses a new out folder. It does not mix hand-picked repeat tries with half facts.

The open draw note opt-in is `RendererSystem::setRefractionDiagnosticsEnabled(true)`. It picks split cooked card code with counts and free done read-back. `tryGetLatestRefractionStatistics()` gives a done took copy, or fault till one lives. It does not wait for the card and does not vow the now frame. The last post may stay up while new work waits. Use its line/set, art true frame, size, card ready, and took true send token to prove new and path. A took note copy does not prove a next show won.

The three-slot read-back ring drops a new try when full. Draw goes on. Facts show in link, bulk out, in bounce, back follow, kept and fall-back work. Counts are 32-bit. Any non-zero `overflowFlags` voids that try. Plain true step asks each send one true in-line ask. Marked one-bender front edges link straight from the grab with no in ask (`entryQueries = 0`, `entryHits = activePixels`). CSG can do more true work. So `physicalQueries` and `queryBudgetUnits` stay split. Count shares do not prove card-time shares. Back hits on solid copies ask the screen light kept store first. They use hit range and copy flags first. They read this before point, mid-weight, or `worldToObject` hit facts. Face remake and `nwbRefractionShadeSurfaceHit` (`materialShadeCalls`) run only when a hit is non-solid or misses the screen kept store.

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
transform. `SharedGroup` also requires a nonempty `opticalVolumeGroup` and
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

The base run grabs 33 frame sets. It grabs 14 cases on auto and 14 on screen, plus five bend-off kept shots. `--variants automatic` or `--variants screen` cuts the light path. Each grab uses 16 art show frames, done card read-back, clean logs, and a plain end. Same-copy match lets at most two steps of lane round and a mean full-RGB gap of 0.02/255 for the full frame. Split kept shots must move at least 64 pixels by more than two lane steps. Split cool/warm base checks stop a first test from passing with same-look mats. `duplicate_comparisons.json` logs good weighs. `gallery.html` and raw BMP/PNG shots land first, so a bad true-model can be seen. The match CTest is `nwb_refraction_duplicate_capture_smoke`. CPU-only true-model tests live as `nwb_refraction_duplicate_analysis_unit`.

The burn-ball set also turns on camera bend outright. It keeps its first glass mat, light, catch plate, and photon burns. Start it with:

```powershell
python -m launcher smoke caustic-sphere --config dbg
```

`NWB_REFRACTION_SMOKE_ENABLED=0` turns off only camera bend for a weigh. The glass still casts its bend dark and photon burn. Set `NWB_REFRACTION_SMOKE_HARDWARE=0` to weigh screen camera bend. The burn maker stays on the card free path. Both wants base to on when the box flags miss.

`nwb_software_raytracing_optical_smoke` reads back the app frame mark after 360 art show frames. Card read-back must be done first. Act step stays at 1/60 sec. It needs both a bend fix-up and a burn maker send. It needs clean run logs, a non-blank shot, and a neat end. Its out file is `Testing/smoke/<config>/caustic_sphere_capture.bmp`. The old draw-base hold stays open for hand/base shots. A live frame-mark look-out wins, so an old hold set cannot stop its read-back.
