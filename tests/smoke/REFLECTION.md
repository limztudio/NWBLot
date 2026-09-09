Stage-three smooth reflection is implemented and validated for the cases below.
It combines hierarchical screen-space tracing with bounded hardware continuation
and composites reflection on opaque surfaces and primary glass with AVBOIT.
All 20 smoke captures passed on the Qualcomm Adreno X2-90 with required hardware
ray queries and `--gpudbg`; every capture completed normally with clean runtime
and GPU validation logs. The build cooked 170 assets, and all 287 ECS graphics
tests and 22 reflection analysis tests passed. The GPU-debug refraction comparison
and caustic/refraction capture also passed again after the stage-three changes.
The refraction fixture explicitly disables reflection to isolate its transmission
comparisons. The 33-capture exact-duplicate gallery passed at the stage-two gate;
that earlier run is separate evidence.

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
  completed GPU counters are still required to establish hardware ray savings.

`reflection_smoke.py` keeps the seven original captures and adds hybrid
`offscreen`. These eight form `--suite baseline`. The twelve captures in
`--suite screen` are all four routes for `onscreen` and `floor`, screen
`onscreen_moved`, and disabled/screen/hybrid `boundary`. The default `--suite all`
runs all 20. It requires the matching accepted-route diagnostic, normal
shutdown, nonempty actual framebuffer data, and strict runtime/validation logs.
Unsupported required hardware is a skip unless `--require-hardware` was selected.
The application must self-exit within each capture timeout. The outer runner
allows an additional 90 seconds for bounded child startup and process/logserver
cleanup, rather than interrupting that cleanup at the capture deadline.

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
must match their mode-specific visibility. Twenty-two independent synthetic analysis
tests reject flat tint, missing or stationary markers, sparse correctly located
patches, missing glass reflection, expanded/lost foreground, exterior corruption,
malformed frames, direct-image substitution, incorrect floor or boundary images,
incorrect direct/reflected motion scales, and inherited fixture controls.

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

Build and run on the Windows ARM64 debug preset:

```powershell
cmake --build --preset windows-clang-arm64-dbg --target nwb_reflection_smoke
ctest --test-dir __cmake/build/windows-clang-arm64 -C dbg --output-on-failure -R '^nwb_reflection_capture_analysis_unit$'
python tests/smoke/reflection_smoke.py --executable __exec/windows/arm64/full/dbg/reflection_smoke.exe --working-directory __cmake/build/windows-clang-arm64/Testing/smoke_runtime/dbg --output-directory __cmake/build/windows-clang-arm64/Testing/smoke/dbg/reflection_hybrid_gpudbg --logserver-executable __exec/windows/arm64/full/dbg/logserver.exe --require-hardware --application-arg=--gpudbg
```

The portable CTest entry is `nwb_reflection_capture_smoke`. The launcher scene is
`reflection`, with `--reflection-case
offscreen|moved|opaque_glass|onscreen|onscreen_moved|boundary|floor` and
`--reflection-mode disabled|screen|hardware|hybrid`. Optional
`--reflection-debug none|source|confidence` forwards the typed debug view for
manual inspection. The comparison runner clears inherited debug and timing
controls so ordinary captures always measure rendered radiance. All controls
remain test-local and forward to the typed renderer API.

Artifacts include the original BMPs, PNG conversions with identical RGB pixels,
`reflection_manifest.json`, and a self-contained offline `reflection.html`.
Images and an unvalidated report are saved before the final image assertions so
failed visual comparisons remain inspectable. A passing report records the
resulting metrics. The report uses actual rendered pixels; it contains no
illustrated or generated replacement images.

Remaining work and limits are explicit:

- Only smooth single-bounce reflection is validated. Roughness currently gates
  trace eligibility; rough lobe sampling and reconstruction remain future work.
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
  transport, and they do not test temporal disocclusion or history rejection.
- The strict refraction and caustic tests passed again at the stage-three gate;
  exact-duplicate tests passed at stage two. Combined reflection/duplicate and more complex
  reflected secondary transport cases remain part of the subsequent matrix.
- No reflection performance improvement has been measured. The GPU counter buffer
  counts candidates, actual hardware rays/hits, eligible opaque/glass samples,
  screen attempts/hits, and fallback samples; completion-associated CPU publication and benchmark assertions
  remain future work.

For subsequent performance runs, enable `NWB_REFLECTION_SMOKE_TIMING=1` and use
`NWB_GPU_TIMING_FILE`. The fixture forwards real frame delta to `FpsProbe` and
`GpuPassTimingProbe`, while fixed simulation delta remains separate. The current
reflection scopes are `render.reflection_depth_pyramid`,
`render.reflection_classify` (including screen tracing),
`render.reflection_build_args`, and `render.reflection_hardware`; additional
scopes will accompany filtering.
The probe has a 64-scope cap, so the benchmark must require all expected scopes
rather than silently accepting missing timings. Decode opt/fin scope hashes with
the matching `.namesym` file using `tests/ab/gpu_timing_parse.py`.

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
