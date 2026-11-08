# Software ray tracing qualification

Loader apps take `--disable-hardware-ray-tracing`. This picks `Core::HardwareRayTracingPolicy::Disabled` before card box/card make. Plain start keeps auto card ray-trace pick.

The picked back sets the off card ray-trace rule before card make. The set cannot move after box make. Use a new run to weigh auto and off rules.

The scene tests need the picked rule and true soft-walk proof:

```text
Loader: hardware ray tracing disabled before device creation
RendererSystem: dispatched software shadow traversal
```

The draw log tracks true soft-walk takes. Part notes also need soft burn, surfel walk, or screen look proof. These checks run the picked soft path on a card that can ray-trace.

## Expected fallback behavior

| Effect | No-RT implementation |
| --- | --- |
| Opaque and transparent shadows | Indexed light-space capture and compute resolve, with GPU triangle-BVH traversal for unresolved receivers |
| Caustics | Software BVH photon producer and shared temporal/filtering passes |
| Surfel GI | Software BVH trace and the shared surfel cache/resolve |
| Camera reflection | Screen-space tracing and existing miss handling |
| Camera refraction | Screen-space resolve |
| Transparency | AVBOIT remains enabled |

Screen bounce/bend cannot bring back free off-shot shapes. The rule keeps the old fall-back way. It does not vow same shots as card RT or card-path frame rates.

## Running the scene matrix

Make these builds in the picked form so their match run files
are cooked: `nwb_transparent_multi_smoke`, `nwb_transparent_csg_smoke`,
`nwb_caustic_sphere_smoke`, `nwb_skinned_caustic_smoke`,
`nwb_stress_test_smoke`, and `nwb_gi_test_smoke`.

From the repo root, pick a match board/chip set preform. Make with no scene start:

```text
python -m launcher build nwb_transparent_multi_smoke nwb_transparent_csg_smoke nwb_caustic_sphere_smoke nwb_skinned_caustic_smoke nwb_stress_test_smoke nwb_gi_test_smoke --configure-preset <configure-preset> --build-dir <build-directory> --config opt -D NWB_BUILD_TESTS=ON
ctest --test-dir <build-directory> -C opt -L software_raytracing --output-on-failure
```

The seven scene tests span lap clear webs, two cut CSG poses,
joined ball burns with bend, moved skinned glass, the full
20-thing scene past an odd-size grow, and the set GI scene.
They need the Loader off rule and soft dark send. They turn down
card walk/make marks. They ask set card proof
out of the End set. Caustic
cases also require the software photon producer. Warnings, errors,
assertions and validation diagnostics fail the capture harness.

These set-policy tests do not skip merely because the physical adapter
supports RayQuery. The existing natural-capability software smoke remains
available for adapters that lack RayQuery without an override. Captures are
written under `<build-directory>/Testing/smoke/<configuration>`.

An eighth test, `nwb_software_raytracing_optical_smoke`, compares four converged
sphere captures with reflection, refraction and caustics independently disabled.
A fixed striped opaque backdrop supplies screen-space depth and visible
refractive displacement in all four captures. The test requires each optical
contribution to be visible and the caustic gain to concentrate on the receiver. It explicitly excludes the hardware-only exterior
ray-miss radiometric oracle: projected screen-space hits do not prove the
same off-screen transport. Its report records that distinction.

The scene captures prove startup, traversal-route and rendered-output
coverage. The ECS graphics unit tests retain neutral empty-input, geometry
layout, identity, shadow quality and history edge cases. Native metadata-only
fixtures and their resource-dependent cases have been removed. A visually
nonempty capture alone is not an exact optical equivalence test.

## CSG indirect-light regression

Build `nwb_gi_test_smoke` through the launcher, then run:

```text
ctest --test-dir <build-directory> -C opt -L csg_gi --output-on-failure
```

The GI set owns its test mats, BXDF sets, and scene list under
`tests/smoke/gi_assets`. Building `nwb_gi_test_smoke` also runs
`nwb_gi_smoke_assets`, which cooks `tests/smoke/gi_assets` alongside
`tests/smoke/assets` and `impl/assets` into
`<build-directory>/Testing/gi_smoke_runtime/<config>/res`. These shots and the cover probe run from that folder. The formal surfel-GI base scene keeps the shared `skinning_culling_benchmark_runtime` cook.

The free and off card ray-trace rules use the same still scene and shot true-model after plain back pick. Each weighs plain cut mesh shape, same CSG with two lap box cutters, and a no-cut kept shot. One zone weighs blue bounced light through a gap. One more weighs green light bounced from a made wall to the cut hole. A free pane must take GI in each arm. The plain base must move from the no-cut kept shot first. Then CSG same can pass.
Shown grids also need 95% or more lap of the through gap and back wall in both the plain base and CSG shots. They need 5% or less in the no-cut shot. These gates spot half gaps and in lap walls that a small zone GI mean can miss.

A third build puts a shut box and an open quad in one mesh. The same cutter cuts a bar from both. Its base uses match cut shape and kept open side quads. A no-cut green bar gives the false-good kept shot. The true-model checks pass cover and GI. It checks red floor light from kept open tris. It checks seen kept-face glow. It checks RGB match that turns down light from cut green tris.

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
`nwb_csg_visible_smoke` process. Live draw first hits card frame 360. Then it runs 30 or more true-clock secs. Then it picks and logs the true first grab frame. Compilation, asset loading, and startup
preparation are outside that timed interval. Samples use graphics frame
`firstCaptureFrame + sampleIndex * frameInterval` and retain the actual
1280 x 900 framebuffer. `--frame-interval` accepts 1 through 64 and defaults to
8, spanning 248 graphics frames after the first capture. Interval 1 captures 32
consecutive frames and waits for GPU completion before reusing the capture
observer; other intervals poll readbacks asynchronously. The driver requires one
It logs warm start/done, 30 or more passed secs, the true frame line, and all took read-backs. Lost, half, old-folder, or wrong-size shots fail. This set GI take rule does not vow draw meet-up. It is not a rule for each smoke test.

The set owns its test mats, nine BXDF sets, and scene list under `tests/smoke/csg_visible_assets`. Plain smoke/speed cooks use `tests/smoke/shared_assets` with `tests/smoke/assets`. The exe asset need gives `impl/assets`, `tests/smoke/assets`, and `tests/smoke/csg_visible_assets` straight to the asset pipe. It cooks only files found from `tests/smoke/csg_visible_assets/scenes/main.nwb` to `<build-directory>/Testing/csg_visible_smoke_runtime/<config>/res`.

Build the scene and its matching assets through the launcher, then acquire one
sequence from the repository root:

```text
python -m launcher build nwb_csg_visible_smoke --configure-preset <configure-preset> --build-dir <build-directory> --config opt -D NWB_BUILD_TESTS=ON
python tests/smoke/csg_gi_temporal_smoke.py --executable <built-nwb_csg_visible_smoke> --working-directory <build-directory>/Testing/csg_visible_smoke_runtime/opt --output-directory <empty-capture-directory> --route software --view full --motion animated
```

Use one more blank out folder and run per weigh arm. Base form is full look, still pose, free path, and gap 8. Late light and GPU checks are a choice:

| Option | Comparison arm |
| --- | --- |
| `--view full`, `direct`, or `indirect` | Show plain light, straight light with no shown GI, or bounced-only light |
| `--view normal` or `position` | Show the coded face tilt or world spot for still shape probes |
| `--view cap_state`, `interval_state`, or `event_state` | See now cut-cap, gap-make, or catch-event state through test-owned mats |
| `--view event_data` or `event_order` | See same-frame catch-event good, lost-span flags, or proved raw front/back come order |
| `--view span_state` | See catch span count, live span flags, and raw catch-event count |
| `--motion static` or `animated` | Hold the first pose, or use an art-frame-run front tri-wave pose with set cam and light |
| `--overlapping` | Add one still pane catch at same x/y and z + 0.125. It shares the set, cutter, and test mat |
| `--route natural` or `software` | Use plain back skill pick, or turn off card ray-trace before card make |
| `--frame-interval <1..64>` | Pick shot gaps. Base is 8, or 1 for back-to-back frames with lined-up read-back done |
| `--lagged-lighting` | Turn on late light. It needs took start and live-past proof |
| `--gpu-validation` | Request selected GPU validation for a diagnostic run |
| `--gpu-timing-output <path>` | Retain the GPU timing sidecar through `NWB_GPU_TIMING_FILE` |
| `--timeout <seconds>` | Bound acquisition; default 90 seconds |
| `--check-quality` | Use the set light, time, and space back-slide caps on full/direct/indirect looks |
| `--check-geometry` | Check four math cap probes in each frame. It needs still plain or spot look |
| `--check-cap-state` | Need made seen caps that hold done depth at 16 probes in each frame. It needs still cap_state look |
| `--check-spans` | Check two free lap catch spans and three kept shots in each frame. It needs still span_state look and --overlapping |
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
admission. `--check-quality` also rejects a static 95th-percentile
temporal channel range above 6, per-frame horizontal or vertical jump fractions
above 10%, and background means more than 2 channel levels from the fixture's
fixed RGB `(88, 98, 113)`. Full and indirect views require the box, sphere, and
capsule to retain their expected tint with a dominant-channel mean of at least
32 and margins of 4. Direct controls require a core channel mean of at least 16;
box and capsule must also differ from the background by at least 8. These are
8-bit scene-specific regression limits, not general image-quality criteria.

`--check-geometry` weighs the four set cap pixels in each of the 32 frames. That is 128 probes per run. The cover flips the set show move and show map first. Then it weighs math ray/cutter cuts. Plain L2 gap must stay at 0.04 or less. Spot part pass holds the shot half-lane round gap and a 0.005 math spare, capped at 0.06. Full test colors fail. They do not make a fake back change. The probes tell true cap walls from hold faces. They do not check each cap pixel. They do not swap straight and bounced light weighs. Bad checks stay in `manifest.json` and fail the run.

The five state looks live only under `tests/smoke/assets/`. The smoke set picks their mats with no ship test knobs. They show the old now-frame CSG shots through the plain held mat path:

| State view | Encoded RGB channels |
| --- | --- |
| `cap_state` | Raw removed count is nonzero; nearest cap is visible within the existing radius-one repair; that cap's projected depth exactly equals completed G-buffer depth |
| `interval_state` | Receiver span count is nonzero; a nonzero peel ID exists in the first four layers; one of those IDs has positive back-cap alpha |
| `event_state` | `min(rawCount, 33) / 33`; stored front-face count divided by 33; stored back-face count divided by 33 |
| `event_data` | Span-present marker or missing-span flags divided by 33; exactly two stored events have matching front/back receiver IDs for the first nonzero peel ID; both matching faces have finite depths with back depth greater than front depth |
| `event_order` | Span-present marker or missing-span flags divided by 33; raw event 0 face; raw event 1 face. Faces encode front as 1 and back as 0.5 only after the same-frame two-event pair passes receiver, opposite-face, finite-depth, and back-greater-than-front checks; invalid pairs encode both face channels as 0 |
| `span_state` | Receiver span count divided by 33; existing span flags divided by 33; raw receiver event count divided by 33 |

The event look scans at most 32 made coats. Raw count 33 means flood, not a 33rd kept event. The cover flips show and show map for all state looks. Cap/gap lanes turn true/false at a read mark of 0.5. Event-state lanes turn counts through `round(channel * 33)`. `raw_count_overflow` stays split.
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
channel meanings. State looks turn down the light good and still shape gates. Still `cap_state` also takes `--check-cap-state`. All three read lanes must be true at the four math hubs and the 12 pill edge pixels at `[784, 787)` x `[604, 608)` in each frame. That is 512 must probes in 32 shots. The eight far-edge pixels stay watch-only. A checked list writes `cap_state_required_probes`, `cap_state_gate_contract`,
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
these finite probes do not prove general topology or complete cap coverage.

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
on CPU geometry bounds; these smoke scenes do not prove coverage for every
possible skeletal deformation.
