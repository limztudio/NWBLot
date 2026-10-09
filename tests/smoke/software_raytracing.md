# Software ray tracing qualification

Loader-based applications accept `--disable-hardware-ray-tracing`. This selects
`Core::HardwareRayTracingPolicy::Disabled` before graphics instance/device
creation. Normal startup keeps automatic hardware ray tracing selection.

The selected backend applies the disabled hardware ray tracing policy before
device creation. The setting cannot change after instance creation; use a new
process to compare automatic and disabled policies.

The scene tests require selected policy and actual software traversal evidence:

```text
Loader: hardware ray tracing disabled before device creation
RendererSystem: dispatched software shadow traversal
```

The renderer record follows actual software traversal recording. Feature-specific
registrations also require software caustic, surfel tracing or screen-space
optical evidence. These checks exercise the selected software route on a
hardware ray tracing capable adapter.

## Expected fallback behavior

| Effect | No-RT implementation |
| --- | --- |
| Opaque and transparent shadows | Indexed light-space capture and compute resolve, with GPU triangle-BVH traversal for unresolved receivers |
| Caustics | Software BVH photon producer and shared temporal/filtering passes |
| Surfel GI | Software BVH trace and the shared surfel cache/resolve |
| Camera reflection | Screen-space tracing and existing miss handling |
| Camera refraction | Screen-space resolve |
| Transparency | AVBOIT remains enabled |

Screen-space reflection/refraction cannot recover arbitrary off-screen geometry.
The policy preserves the existing fallback behavior and does not promise image
identity with hardware RT or hardware-path frame rates.

## Running the scene matrix

Build these targets in the chosen configuration so their matching runtime assets
are cooked: `nwb_transparent_multi_smoke`, `nwb_transparent_csg_smoke`,
`nwb_caustic_sphere_smoke`, `nwb_skinned_caustic_smoke`,
`nwb_stress_test_smoke`, and `nwb_gi_test_smoke`.

From the repository root, select a matching platform/architecture configure preset and build without launching a scene:

```text
python -m launcher build nwb_transparent_multi_smoke nwb_transparent_csg_smoke nwb_caustic_sphere_smoke nwb_skinned_caustic_smoke nwb_stress_test_smoke nwb_gi_test_smoke --configure-preset <configure-preset> --build-dir <build-directory> --config opt -D NWB_BUILD_TESTS=ON
ctest --test-dir <build-directory> -C opt -L software_raytracing --output-on-failure
```

The seven scene tests cover overlapping transparent meshes, two clipped CSG poses,
converged sphere caustics with refraction, animated skinned glass, the full
20-body scene before/after an odd-sized resize, and the dedicated GI scene.
They require the Loader disabled policy and software shadow dispatch, reject
hardware traversal/producer markers, and request selected GPU validation
outside the Final configuration. Caustic
cases additionally require the software photon producer. Warnings, errors,
assertions and validation diagnostics fail the capture harness.

These explicit-policy tests do not skip merely because the physical adapter
supports RayQuery. The existing natural-capability software smoke remains
available for adapters that lack RayQuery without an override. Captures are
written under `<build-directory>/Testing/smoke/<configuration>`.

An eighth test, `nwb_software_raytracing_optical_smoke`, compares four converged
sphere captures with reflection, refraction and caustics independently disabled.
A fixed striped opaque backdrop supplies screen-space depth and visible
refractive displacement in all four captures. The test requires each optical
contribution to be visible and the caustic gain to concentrate on the receiver. It explicitly excludes the hardware-only exterior
ray-miss radiometric oracle: projected screen-space hits do not establish the
same off-screen transport. Its report records that distinction.

The scene captures establish startup, traversal-route and rendered-output
coverage. The ECS graphics unit tests retain neutral empty-input, geometry
layout, identity, shadow quality and history edge cases. Native metadata-only
fixtures and their resource-dependent cases have been removed. A visually
nonempty capture alone is not an exact optical equivalence test.

## CSG indirect-light regression

Build `nwb_gi_test_smoke` through the launcher, then run:

```text
ctest --test-dir <build-directory> -C opt -L csg_gi --output-on-failure
```

The natural and disabled hardware ray tracing policies use the same static scene
and image oracle after normal backend selection. Each compares ordinary carved
mesh geometry, equivalent CSG with two overlapping box cutters, and an uncut
control. One region measures blue indirect light passing through an opening;
another measures green light bounced from a generated wall into the removed
cavity. An unobstructed panel must receive GI in every arm, and the ordinary
reference must differ from the uncut control before CSG equivalence is admitted.
Projected grids also require at least 95% coverage of the through opening and
back wall in both the ordinary reference and CSG images, with at most 5% in the
uncut image. These gates detect partial openings and internal overlap walls that
a small regional GI average can miss.

A third assembly places a closed box and an open quad in one mesh. The same
cutter removes a stripe from both. Its reference uses matching carved geometry
and retained open side quads; an uncut green stripe supplies the false-positive
control. The oracle checks passage coverage and GI, red floor light from retained
open triangles, visible retained-surface brightness, and RGB agreement that rejects
light from removed green triangles.

Captures wait for 360 successful presentations and retain the three images,
runtime logs, and regional color metrics under
`Testing/smoke/<configuration>/csg_gi_<route>`. The harness rejects runtime and
validation warnings through the existing capture workflow. Natural policy
selection qualifies whichever traversal the actual adapter selects. The same
scene also runs with frame-lagged async lighting enabled and requires accepted
bootstrap and active-history submissions before applying the image gates.
Adapters without a dedicated compute queue skip this async qualification through
the existing capability diagnostic. Lagged artifacts use the _lagged suffix.

GI uses current CSG geometry independently of retained light-space shadow maps.
Both traversal implementations subtract the cutter union from closed-component
spans and shade the selected surviving triangle or generated wall with the receiver
material. Cooked membership bits identify closed components in the final ray-triangle
order. Zero-volume components remain surfaces, with volume measured relative to a
stable component origin. Open components in the same mesh remain clipped surface
hits and do not generate walls. Light-space shadows consume the same membership
without interpreting open events as unmatched volume exits. Nonzero oriented
winding preserves reversed closed shells and nested cavity cancellation. Shared
built-in shape and bounded cutter support follow the current CSG snapshot contract;
unsupported
or over-budget geometry reports a diagnostic and uses conservative occlusion.

## Temporal CSG direct/GI sequence

`tests/smoke/csg_gi_temporal_smoke.py` records 32 framebuffer captures from one
`nwb_csg_visible_smoke` process. Live rendering first reaches graphics frame 360,
then continues for at least 30 steady-clock seconds before selecting and logging
the actual first capture frame. Compilation, asset loading, and startup
preparation are outside that timed interval. Samples use graphics frame
`firstCaptureFrame + sampleIndex * frameInterval` and retain the actual
1280 x 900 framebuffer. `--frame-interval` accepts 1 through 64 and defaults to
8, spanning 248 graphics frames after the first capture. Interval 1 captures 32
consecutive frames and waits for GPU completion before reusing the capture
observer; other intervals poll readbacks asynchronously. The driver requires one
logged warm-up start/completion, at least 30 elapsed seconds, the exact relative
source-frame sequence, and all accepted readbacks. Missing, partial,
stale-directory, or wrong-size captures fail. This settled GI acquisition
protocol is not a renderer convergence guarantee or a rule for every smoke test.

The fixture owns its diagnostic materials and nine BXDF families under
`tests/smoke/csg_visible_assets`; ordinary smoke/performance cooks use only
`tests/smoke/assets`. The executable's asset dependency runs
`tests/smoke/prepare_fixture_asset_root.py` to copy and verify the disjoint union
of those two source roots into
`<build-directory>/Testing/csg_visible_generated/<config>`. It rejects identity
collisions, unsafe root overlaps, and unexpected existing files without deleting
anything. The cooker then receives engine `impl/assets` and this single project
root, preserving the fixture's `project/...` virtual references. Passing two
project roots is unsupported because source resolution selects the first
matching virtual root without an existence fallback. The private cooked resource
directory remains
`<build-directory>/Testing/csg_visible_smoke_runtime/<config>/res`.

Build the scene and its matching assets through the launcher, then acquire one
sequence from the repository root:

```text
python -m launcher build nwb_csg_visible_smoke --configure-preset <configure-preset> --build-dir <build-directory> --config opt -D NWB_BUILD_TESTS=ON
python tests/smoke/csg_gi_temporal_smoke.py --executable <built-nwb_csg_visible_smoke> --working-directory <build-directory>/Testing/csg_visible_smoke_runtime/opt --output-directory <empty-capture-directory> --route software --view full --motion animated
```

Use a separate empty output directory and process for each comparison arm.
Defaults are full view, static pose, natural route, and interval 8; lagged
lighting and GPU validation are opt-in:

| Option | Comparison arm |
| --- | --- |
| `--view full`, `direct`, or `indirect` | Display normal lighting, direct lighting without displayed GI, or indirect-only lighting |
| `--view normal` or `position` | Display the encoded shading normal or world position for static geometry probes |
| `--view cap_state`, `interval_state`, or `event_state` | Inspect current removed-cap, interval-production, or receiver-event state through test-owned materials |
| `--view event_data` or `event_order` | Inspect same-frame receiver event validity, missing-span flags, or validated raw front/back arrival order |
| `--view span_state` | Inspect receiver span count, existing span flags, and raw receiver event count |
| `--motion static` or `animated` | Hold the initial pose or use a graphics-frame-driven, front-facing triangle-wave pose with fixed camera and light |
| `--overlapping` | Add one static plane receiver at the same x/y and z + 0.125, sharing the group, cutter, and diagnostic material |
| `--route natural` or `software` | Use normal backend capability selection or disable hardware ray tracing before device creation |
| `--frame-interval <1..64>` | Select capture spacing; default 8, or 1 for consecutive frames with serialized readback completion |
| `--lagged-lighting` | Enable frame-lagged lighting and require accepted bootstrap and active-history evidence |
| `--gpu-validation` | Request selected GPU validation for a diagnostic run |
| `--gpu-timing-output <path>` | Retain the GPU timing sidecar through `NWB_GPU_TIMING_FILE` |
| `--timeout <seconds>` | Bound acquisition; default 90 seconds |
| `--check-quality` | Apply the fixture's lighting, temporal, and spatial regression limits to full/direct/indirect views |
| `--check-geometry` | Check four analytical cap probes in every frame; requires static normal or position view |
| `--check-cap-state` | Require produced, visible caps that own completed depth at 16 probes in every frame; requires static cap_state view |
| `--check-spans` | Check two independent overlapping receiver spans and three controls in every frame; requires static span_state view and --overlapping |
| `--overwrite` | Replace only this sequence's known capture files, partial files, manifest, and log |

The direct view omits the displayed GI contribution while retaining the same GI
computation and direct lighting, so it controls for fluctuations from the direct
shadow path. Temporal mode disables camera reflections for every view. Natural
selection exercises the route available on the actual adapter; it does not force
hardware traversal. Use the matching `--logserver-executable` when needed for
runtime logging, or the driver runs without logserver.

Each directory retains `runtime.log`, 32 BMP files, and `manifest.json`. The
manifest records frame identities, image hashes, the capture interval, warm-up
start frame/elapsed seconds, actual first capture frame, and logged controls.
Regional temporal range, adjacent-frame change, standard deviation,
and aggregate horizontal-change metrics use a grid sampled every second pixel
in each axis. Separate dense measurements visit every pixel of each region for
each frame: RGB means and the fractions of adjacent horizontal and vertical RGB
channel differences exceeding 20 in the 8-bit capture. The series summary retains
the largest per-frame jump fractions and the minimum per-frame channel means,
retaining each frame's spatial changes independently of the temporal average.
Static measurements keep `box_upper_band` at center `(836, 290)` with an
80 x 16 footprint, plus the sphere upper band, capsule stripe region, and exterior
strips on the box, sphere, and capsule. Animated measurements replace only the
box upper band with `box_inner_wall`: a 48 x 16 footprint centered at `(832, 316)`,
covering pixels `[808, 856)` x `[308, 324)`. This measures the inner wall while
the ceiling/wall boundary moves; the static upper-band footprint is unchanged.

`--analysis-only` recomputes the measurements from an existing complete sequence;
pass its original view, motion, route, frame interval, and lagged options. The
driver derives controls from `runtime.log` and rejects a requested/logged mismatch
rather than relabeling the images. Acquisition and analysis both reject runtime
warning/error/assertion and validation diagnostics. Timing identities in
`opt`/`fin` may need the matching `.namesym` sidecar for readable scope names.
Without a check option, success establishes sequence, log, and readback
admission. `--check-quality` additionally rejects a static 95th-percentile
temporal channel range above 6, per-frame horizontal or vertical jump fractions
above 10%, and background means more than 2 channel levels from the fixture's
fixed RGB `(88, 98, 113)`. Full and indirect views require the box, sphere, and
capsule to retain their expected tint with a dominant-channel mean of at least
32 and margins of 4. Direct controls require a core channel mean of at least 16;
box and capsule must also differ from the background by at least 8. These are
8-bit scene-specific regression limits, not general image-quality criteria.

`--check-geometry` compares the four fixed cap pixels in each of the 32 frames,
for 128 probes per sequence. The harness reverses the fixture's display transfer
and presentation mapping before comparing with analytical ray/cutter
intersections. Normal L2 error must not exceed 0.04. Position component
tolerances include the capture's half-channel quantization interval and a 0.005
arithmetic allowance, capped at 0.06. Saturated diagnostic colors fail instead
of producing an invented inverse. The probes distinguish physical cap walls
from enclosing faces; they do not verify every cap pixel or replace direct and
indirect lighting comparisons. Failed checks remain in `manifest.json` and make
the driver fail.

The five state views are authored only under `tests/smoke/assets/`; the smoke
fixture selects their materials without adding production diagnostic switches.
They display the existing current-frame CSG textures through the normal deferred
material path:

| State view | Encoded RGB channels |
| --- | --- |
| `cap_state` | Raw removed count is nonzero; nearest cap is visible within the existing radius-one repair; that cap's projected depth exactly equals completed G-buffer depth |
| `interval_state` | Receiver span count is nonzero; a nonzero peel ID exists in the first four layers; one of those IDs has positive back-cap alpha |
| `event_state` | `min(rawCount, 33) / 33`; stored front-face count divided by 33; stored back-face count divided by 33 |
| `event_data` | Span-present marker or missing-span flags divided by 33; exactly two stored events have matching front/back receiver IDs for the first nonzero peel ID; both matching faces have finite depths with back depth greater than front depth |
| `event_order` | Span-present marker or missing-span flags divided by 33; raw event 0 face; raw event 1 face. Faces encode front as 1 and back as 0.5 only after the same-frame two-event pair passes receiver, opposite-face, finite-depth, and back-greater-than-front checks; invalid pairs encode both face channels as 0 |
| `span_state` | Receiver span count divided by 33; existing span flags divided by 33; raw receiver event count divided by 33 |

The event inspector scans at most 32 allocated layers; raw count 33 identifies
overflow rather than a 33rd stored event. The harness reverses display and
presentation mapping for all state views. Cap/interval channels become booleans
with a decoded threshold of 0.5; event-state channels become counts through
`round(channel * 33)`, with `raw_count_overflow` retained separately.
Span-state channels decode independently with `round(channel * 33)` and require
the logged count/flag encoding marker.
For event-data/order, red is 1 when a span exists; otherwise
`round(red * 33)` records the existing failed-span flag bits (ambiguity 1,
span overflow 2, event overflow 4). Event-data green/blue decode as booleans.
Event-order requires both face channels above 0.25 for a validated pair, then
uses a threshold of 0.75 for front; invalid face fields remain null. The driver
requires each view's encoding marker in the log before interpreting those fields.
Each frame's manifest includes the four geometry-probe pixels and a 5 x 4 capsule
patch at `[783, 788)` x `[604, 608)`, recording display RGB, decoded values, and
channel meanings. State views reject the lighting quality and static geometry
gates. Static `cap_state` additionally supports `--check-cap-state`: all three
decoded channels must be true at the four analytical centers and the 12 capsule
rim pixels at `[784, 787)` x `[604, 608)` in every frame. This gives 512 required
probes across 32 captures. The eight outer-rim pixels remain observational. A
checked manifest records `cap_state_required_probes`, `cap_state_gate_contract`,
and `cap_state_failures`; a false channel fails the gate.

Static `span_state` with `--overlapping --check-spans` checks the generic
multi-event path: the plane center must have two spans, zero flags, and four
receiver events; the other three centers must each have one span, zero flags,
and two events. All four probes must meet those exact decoded counts in every
frame. The harness requires the static duplicate's log marker and matches the
logged mode to the requested controls. Its checked manifest records
`span_required_probes`, `span_expectations`, and `span_failures`, alongside the
encoding and overlap contracts. Natural and software overlapping cases are
registered in CTest. Other state views remain observational;
these finite probes do not establish general topology or complete cap coverage.

CTest registers fourteen sequences as `nwb_csg_gi_<suffix>_smoke`:

| Suffix | View, motion, route, and interval |
| --- | --- |
| `temporal_full` | Full, static, natural, 8 |
| `consecutive_full` | Full, static, natural, 1 |
| `consecutive_direct` | Direct, static, natural, 1 |
| `consecutive_indirect` | Indirect, static, natural, 1 |
| `consecutive_software` | Full, static, software, 1 |
| `animated` | Full, animated, natural, 8 |
| `animated_software` | Full, animated, software, 8 |
| `temporal_lagged` | Full, static, natural, 8, with lagged lighting |
| `geometry_normal` | Normal, static, natural, 8 |
| `geometry_position` | Position, static, natural, 8 |
| `cap_state` | Cap state, static, natural, 8 |
| `cap_state_software` | Cap state, static, software, 8 |
| `overlapping_spans` | Span state, static, natural, 8, with overlapping receivers |
| `overlapping_spans_software` | Span state, static, software, 8, with overlapping receivers |

The lighting sequences enable the quality gate; normal/position sequences enable
the geometry gate; cap-state sequences enable the cap-state gate; overlapping
span-state sequences enable the span gate. All request
GPU validation, use the matching runtime directory
and logserver when available, overwrite only their own known capture artifacts,
and serialize display use through the test resource lock. Unsupported capture
environments may skip with exit 77. A passing gate verifies those finite
regression conditions on the actual run; broader optical correctness and
performance still require their own matched comparisons. Interval-1 GPU
timings describe the serialized capture workload.

CSG performance qualification also needs a matched scene containing no CSG
objects. Keep route, resolution, view, binary/assets, timing publication, and
capture conditions consistent, and separate startup/settling from measured
intervals. Compare CPU/frame time and affected GPU passes with unaffected
controls; ordinary shader equivalence alone does not measure CPU preparation,
resource retention, or binding costs. CSG-only cap snapshots and bindings must
remain conditional, and ordinary variants must exclude unused CSG arrays and
constants. A passing image gate establishes its stated visual conditions rather
than a performance result.

## Animated scene bounds

Runtime software scenes refit the scene BVH from the current GPU mesh roots
after the per-mesh build/refit work. The CPU topology and instance order remain
frozen for that frame. Static-only scenes retain their existing reuse path.

The skinned scene smoke exercises the production shader through the selected
disabled hardware ray tracing route.

Caustic photon emission-target domains remain a separate approximation based
on CPU geometry bounds; these smoke scenes do not establish coverage for every
possible skeletal deformation.
