# Surfel GI Plan (pivot from the world-grid DDGI)

Author date: 2026-07-05. Basis: a 5-agent design workflow (2 surveys of the reusable plumbing + G-buffer, 2 candidate
architectures, 1 verified synthesis). Supersedes the world-grid probe volume in `.helper/ddgi_plan.md` for the GI
sample layer; the world-grid DDGI it replaces is COMPLETE + verified (colored red/blue bounce) and stays as the
reference until surfels reach parity.

## Why surfels (the honest perf win)
The world grid places 16x8x16 = 2048 probes over `[-8,8]^3` — most in EMPTY AIR for a 4x4x2 box. Surfels place samples
ONLY on visible surfaces, bounding the count to visible surface area (~4-5k for the box) not cubic volume. **The win is
NOT fewer rays** (surfels trace comparable-or-slightly-more). The win is **deleting the grid's dominant passes**: the
irradiance blend (2048*6*6 texel-ops), the distance blend (2048*14*14) + its Chebyshev distance atlas, and the two
border passes — which GPU timing already flagged as the bulk of GI cost. Surfels fold the EMA into the trace's
groupshared reduce, so there is NO separate blend/atlas pass in U0-U2, and the octahedral atlas (U3) touches only live
surfel tiles. Bonus correctness: single-buffered surfel storage eliminates the DDGI ping-pong + front-flip-rebind
machinery that the review tied to confirmed bugs.

## Architecture (recommended: GIBS-style screen-spawned, world-hashed surfels)

### Allocation — one-surfel-per-hash-bucket spawn (integer atomics only)
`surfel_spawn_cs` runs at the existing `renderGi` hook (`system.cpp:428`), where the G-buffer is resident (opaque pass
ended). `numthreads(8,8,1)`, `dispatch(DivideUp(w,16), DivideUp(h,16), 1)` = one thread per 16x16 screen tile. Each thread
reads the tile-center G-buffer worldPos/normal (the SAME targets deferred lighting binds) and hashes the world point to a
cell. ONE SURFEL PER HASH BUCKET: the hash is (re)built from the live pool BEFORE the spawn (hash-build now runs first), so
a non-empty cell head means a surfel already covers the cell -> skip. An empty cell is claimed with an atomic
`InterlockedCompareExchange(cellHead, INVALID -> PENDING)` so exactly one tile allocates when many map to one near-camera
cell (bootstrap); the winner bump-allocates via `InterlockedAdd` on a uint counter and publishes the real slot (losers /
pool-full bail without consuming a slot). Integer atomics only. This keeps every cell list length 1 -> the gather's
fixed-order walk is deterministic (the flicker was hundreds of tile-surfels per cell walked in non-deterministic prepend
order + truncated at MAX_WALK). NOTE: the ORIGINAL coverage-sum spawn (`saturate(dot(Nsurf,Ns))*saturate(1-dist/r)` over
3x3x3, coverage < threshold -> spawn, radius = cellSize/2) was the flicker's root cause -- it packed one surfel per 16px
TILE, overstuffing near-camera cells -- and was REPLACED by this occupancy+claim. On the BOOTSTRAP frame the pool is empty
so every visible cell spawns once -> all visible surfaces covered in ONE frame (mandatory: the unfocused smoke app renders
only the bootstrap frame). Keep-alive (`lastSeenFrame`) + recycle land in U1.

### Storage — RendererRayTracingState, resize-safe, GPU-only
- `NwbSurfel` = 64B std430 (4 float4 lanes): `position+radius / normal+nextInCell / meanIrradiance+sampleCount /
  lastSeenFrame+alive+pad`.
- `g_SurfelPool` RWStructuredBuffer<NwbSurfel>[16384] (1 MB).
- `g_SurfelCellHead` RWStructuredBuffer<uint>[2^18 = 262144] (1 MB) — linked-list head per hash cell.
- `g_SurfelCounter` RWStructuredBuffer<uint> — bump top + free top.
- `NwbSurfelConstants` CB (~5 float4: cameraPos+cellSize / hashCells+poolCap+frameIndex+divisor /
  coverageThresh+defaultRadius+normalBias+hysteresis / maxAge+raysPerSurfel+spawnTile+screenW / screenH+pad).
- Lifecycle mirrors `m_gi*` EXACTLY: lazy `ensureSurfelResources`, one-shot black clear, `m_surfel{Enabled,Seeded,
  FrameIndex,NeedsClear,UpdateCursor}`, reset in `invalidateResources`.

### Acceleration — linked-list world hash (no prefix-sum scan)
`surfel_hash.slangi`: `cell = floor(pos/cellSize)`, `hash = fold(cell) & (cells-1)`, `cellSize = 2*maxRadius` so a
3x3x3 neighbour query is exhaustive. `surfel_hash_build_cs`: clear `cellHead` to 0xFFFFFFFF, then one thread/slot:
skip `!alive`, else `surfel.nextInCell = InterlockedExchange(cellHead[hash], surfelId)` (one uint atomic/surfel). Walks
cap per-cell (64) so collisions add near-zero-weight candidates, never a hang. This ENTIRELY replaces the grid model:
`probe_grid.slangi`'s `NwbGiGridConstants / ProbeWorldPos / LinearIndex / ProbeCount / ProbeActiveThisFrame` are
DELETED; only `nwbGiFibonacciDirection` + `nwbGiFrameRotation` survive.

### Ray budget — one workgroup per surfel, groupshared EMA (no float atomics)
`surfel_trace_cs` `#include`s the `gi_probe_trace_sw_cs.slang` closest-hit VERBATIM behind a
`NWB_SURFEL_TRACE_CLOSEST(...)` seam. `dispatch(poolCap)` workgroups, `numthreads(64,1,1)` = 64 rays/surfel.
`groupThreadID.x = ray i`; dir = `nwbGiFibonacciDirection(i,64,nwbGiFrameRotation(frameIndex))` mapped into the
surfel-normal TBN; origin = `surfel.position + surfel.normal*normalBias`. Each ray: `nwbGiSwTraceClosest ->
nwbGiSwShadeHit` (Lambert over <=8 `nwbSceneResolveLight` + optional dominant-light shadow ray + **per-instance
baseColorR/G/B tint** — all verbatim, incl. the hit-colour we wired this session). The 64 results reduce in GROUPSHARED
(one workgroup owns one surfel -> zero cross-workgroup contention -> NO float atomics) -> hemispherical irradiance ->
EMA into `surfel.meanIrradiance` in place (hysteresis 0 while `sampleCount==0`, so per-surfel seeding sidesteps the
DDGI global-`m_giSeeded` dark-bias).

### Application — the consumer contract is untouched
`nwbBxdfIndirectIrradiance` KEEPS its signature + `nwbGiHemiAmbient` fallback + normal bias VERBATIM (every material +
the AVBOIT `NWB_SCENE_GI_SAMPLING_DISABLED` path unchanged). Only the BODY (`lighting.slangi:239-313`, the 8-probe
trilinear + Chebyshev fetch) becomes a `surfel_gather.slangi` call: bias +normal, hash the biased worldPos, walk the
3x3x3 neighbour lists (capped), weight each surfel by `saturate(dot(Np,Ns)) * smoothstep(radius) * confidence(
sampleCount)`, accumulate `meanIrradiance` (U0) or the octahedral tile in the pixel-normal direction (U3), normalize;
`totalWeight < eps -> hemiAmbient`. Pool SRV + cellHead SRV + surfel CB become Pixel-visible at deferred-lighting slots
9/10/11 (renamed SURFEL_POOL/HASH/PARAMS, same indices), wired at BOTH coupled sites (`deferred_lighting.cpp`
`createDeferredLightingResources` layout AND `deferred_targets.cpp` binding set).

## Reuse map
VERBATIM: `nwbGiSwTraceClosest` + `nwbGiSwInstanceClosest` + `nwbGiSwShadeHit` (incl. `AssignInstanceBaseColor` hit
tint) + the SW BVH bindings (`sw_binding_slots.h` slots 0-10) + `nwbGiFibonacciDirection` + `nwbGiFrameRotation` +
`octahedral.slangi` (U3) + the `nwbBxdfIndirectIrradiance` signature/hemiAmbient/normal-bias + the `m_gi*` lifecycle
pattern.
NEW: the surfel pool/hash/counter storage, `surfel_spawn_cs` / `surfel_hash_build_cs` / `surfel_trace_cs` /
`surfel_gather.slangi` / `surfel_border_cs` (U3), and the screen-space gather body.
DELETED: the grid CB + probe atlases + ray-data + the blend/border/distance passes + the vestigial hit-albedo buffer
(slot 15 + the constant-0.5 upload) + the ping-pong front-flip rebind.

## Unit plan (each independently buildable + verifiable; first bounce in U0)
- **U0 — vertical slice (first bounce on the SW gi-test bootstrap frame).** spawn -> hash-build -> trace -> per-pixel
  gather, flat RGB mean-irradiance, single-buffered. Touches the surfel shaders (new) + `lighting.slangi` +
  `deferred/binding_slots.h` + `renderer_state.{h,cpp}` + `raytracing_system.{cpp,h}` (delete grid blend/border/atlas +
  hit-albedo; add ensure/prepare/renderSurfelGi) + `deferred_lighting.cpp` + `deferred_targets.cpp`. VERIFY: bootstrap
  capture shows RED bleed near +X, BLUE near -X, red->blue gradient; validation-clean + warning-free; HW/GI-off byte-
  identical.
- **U1 — recycling + free list.** Age-free surfels older than maxAge (push id to a free-list stack); spawn pops the
  free list before bump-allocating. Log the live count. VERIFY: pan the camera (ArrowYawInputHandler) — count stays
  bounded, bleed persists.
- **U2 — round-robin trace budget + per-surfel seeding.** Trace 1/Nth of surfels/frame in steady state, ALL on
  bootstrap. VERIFY: GPU timing shows ~4x ray drop vs U0 steady; temporally stable; no recycle dark-flash.
- **U3 — octahedral directional irradiance (the mandated octahedral reuse).** Per-surfel RGBA16F octahedral atlas
  (octahedral.slangi VERBATIM, surfelIndex for probeIndex; 8px tile, 64/row -> 512x2048 < 16384). Trace bins rays into
  the tile; NEW `surfel_border_cs` (mirror-wrap, with the corner/edge-mirror fix). DROP the distance atlas + Chebyshev
  (surfels sit ON surfaces; leak-rejection is normal+distance similarity in the gather). VERIFY: a floor patch facing
  the red wall reads redder than one facing away (directional, not flat).
- **U4 — surfel-to-surfel infinite bounce (the ONLY ping-pong).** Replace the `nwbGiSwShadeHit` bounce-tail no-op with
  a hash-grid gather of nearby surfels' PREV-frame irradiance at the ray hit point. Add a small ping-pong copy of the
  irradiance payload only (records stay single-buffered). VERIFY: multi-bounce brightening in corners; energy stable.
- **U5 — HW RayQuery twin (dual-path).** NEW `surfel_trace_hw_cs` shares the body via the seam, swapping the SW trace
  for inline-RayQuery over the TLAS (model on `shadow_rayquery.slangi`; read baseColor from the same InstanceID-indexed
  record). Wire on the HW branch beside `renderHwCaustics`. VERIFY: HW-vs-SW A/B diff small (caustic 0.55% precedent).
- **U6 — perf + polish.** `dispatchIndirect` the trace off the live counter; depth-scaled spawn radius + multi-res
  cells; optional half-res surfel-apply + bilateral upsample. VERIFY: NWB_GPU_TIMING_FILE shows the surfel block below
  the old grid block; fps up.

## Decisions RESOLVED (user, 2026-07-05)
1. **U3 storage = 2-band SPHERICAL HARMONICS** (user chose against the octahedral recommendation). Per-surfel
   directional irradiance is stored as SH-2 packed into the surfel record — NO per-surfel atlas, NO border pass
   (`surfel_border_cs` is DROPPED from U3). Needs new SH encode (accumulate `radiance * shBasis(dir)` in the trace's
   groupshared reduce) + decode (`dot(sh, shBasis(pixelNormal))` clamped >=0) code. Smaller + cheaper than the atlas;
   softer directionality (2 bands = 4 coeffs/channel = 12 floats RGB). The `NwbSurfel` record grows past 64B to hold
   the SH (RGB SH-2 = 12 floats = 48B) — re-size to ~96-112B (6-7 float4 lanes); `octahedral.slangi` is NOT reused for
   surfels (only the SW trace/shade + Fibonacci dirs + the consumer contract are).
2. **HW twin (U5) = IN SCOPE**, after U0-U4 land. Makes the first real GI HW path (the old DDGI HW trace was a stub).
3. Bootstrap: pool 16384 + hash 262144 + 16x16 spawn tile (~4-5k surfels, ~290k bootstrap rays); raise the tile if a
   target GPU chokes. PS-gather through U5. Linked-list hash. Screen-only spawn through U5. (Plan defaults kept.)

## CORNER FLICKER — TRUE ROOT CAUSE + FIX (2026-07-05): OVERSTUFFED HASH CELLS, not a data race. (supersedes the two sections below)
The triple-corner flicker was FINALLY root-caused. The "frames-in-flight pool DATA RACE" + "resolve pass fixed it" claims
below are WRONG: that resolve verification was a FALSE POSITIVE -- a concurrent nwbSceneApplyLighting bug meant the render
was showing hemiAmbient the whole time, so the "0/8 stable" measurement never actually touched the surfel field.
DECISIVE isolation (flicker_probe.py, 6 runs each): (a) resolve writes a CONSTANT -> 0/6 stable (texture+lighting path is
fine); (b) gather over a pool of HARDCODED-CONSTANT meanIrradiance -> STILL 6/6 flicker. A weighted average of a constant
is that constant regardless of weights, so the flicker can only be a structural churn in WHICH surfels get walked. (c)
MAX_WALK 64 -> 4096 (complete, order-independent walk) -> flicker 11 -> 0.5 (0/6). CONFIRMED it is the walked SET, not a race.
ROOT CAUSE: the old spawn allocated one surfel per 16px SCREEN TILE with radius 0.35 < cellSize 0.70, so a near-camera cell
collected far more than MAX_WALK(64) surfels; the coverage query ALSO truncated at 64, so a stuffed cell never reached the
coverage threshold and spawned FOREVER (hundreds/cell). The gather then walked only the first 64 in the hash-build's
InterlockedExchange PREPEND order, which is NON-DETERMINISTIC (thread race) -- so at the triple corner (one cell spans
red+blue+floor surfels) the walked-64's surface MIX churned frame to frame = the flicker. NOT a barrier race: every
within/cross-frame barrier checked out (SRV->Common restore at close; Common maps to ALL_COMMANDS stage, constants.cpp:256).
FIX = ONE SURFEL PER HASH BUCKET (canonical sparse surfel cache). Reordered renderSurfelGi so HASH-BUILD runs BEFORE SPAWN;
the spawn reads the fresh cell head (occupancy: non-empty == already covered -> skip) and claims an EMPTY bucket via atomic
CompareExchange(INVALID -> PENDING) so exactly one screen tile allocates it (bootstrap packs many tiles into one near-camera
cell); the winner publishes the real slot, losers / pool-full bail without wasting a counter slot. NO new buffer (cellHead
SRV->UAV in the spawn layout+set). Retuned cellSize 0.70->0.6 (surfel spacing) + radius 0.35->0.9 (>cell, for smooth 3x3x3
neighbour overlap; the old radius=cellSize/2 was tied to the dropped coverage sum); MAX_WALK 64->16 (lists are length 1 now,
pure collision headroom); dropped NWB_SURFEL_COVERAGE_THRESHOLD + CB coverageRadiusBiasHyst.x (now reserved). Files:
surfel_spawn_cs.slang (occupancy+claim), surfel_hash_build_cs (runs first), surfel_binding_slots.h, surfel_constants.slangi,
raytracing_system.cpp (spawn cellHead UAV, pass reorder, cellSize, CB pack).
VERIFIED SIMULTANEOUSLY (the whole point -- bounce needs the flicker gone AT THE SAME TIME): flicker 0/8 (max std 0.48 =
residual Monte-Carlo ray-rotation variance, converges); bounce vivid AND observable -- blue wall (70,75,124) / red wall
(111,73,84) match the "vivid" target, corner red-glow (161,109,116), directional floor bleed (warm 212,205,203 near red /
cool 208,205,210 near blue); warning+validation clean (120-line streamed logserver log, zero [WARNING]/[ERROR]/VUID/
validation/SYNC-hazard, all 4 surfel pipelines created). The RESOLVE PASS stays -- it is a genuine architectural
improvement (gather once/pixel in compute, pool compute-only) and the right foundation; it just was NOT the flicker fix.

## (WRONG -- superseded above) CORNER FLICKER — "FIXED via a RESOLVE PASS / frames-in-flight pool DATA RACE" (2026-07-05).
FIX SHIPPED (option A, the caustic pattern): a new COMPUTE pass `surfel_resolve_cs` gathers the surfel field ONCE PER
PIXEL (nwbSurfelGather, reused verbatim) into a screen-space `surfelIrradiance` RGBA16F texture (rgb = indirect
irradiance, a = 1 where a surfel covered the pixel). The deferred lighting PIXEL shader now Loads THAT texture (point,
1:1, alpha<0.5 -> hemiAmbient), never the read-write surfel pool. So the pool is touched only by COMPUTE (spawn/hash/
trace/resolve), like the caustic accumulator, and the frames-in-flight pixel-read-vs-next-frame-compute-write race is
GONE. VERIFIED with flicker_probe.py: 0/8 runs flicker, corner temporal std = 0.00 (was ~12); red bounce preserved
(corner reddish, warning-free + validation-clean, C++/cook build clean 101 files). Files: surfel_resolve_cs.slang(+.nwb)
+ surfel_binding_slots.h (resolve slots) + gi/names.h (s_SurfelResolveShaderName) + timing_names.h (s_SurfelResolve);
the resolve pipeline/set on RendererRayTracingState (ensureSurfelResolvePipeline/BindingSet, dispatched as step 5 of
renderSurfelGi); the surfelIrradiance texture on DeferredFrameTargets (created + bound at target creation, cleared to 0
each frame in clearDeferredTargets); lighting_framework.slangi samples it (nwbBxdfResolvedIndirect in nwbBxdfLoadSurface),
lighting.slangi's nwbBxdfIndirectIrradiance is now the hemiAmbient FALLBACK only (AVBOIT/disabled path); the deferred
lighting set binds ONE texture SRV at slot 9 (pool/hash/params dropped from the LIGHTING layout; they live only in the
resolve set); rebuildDeferredLightingGiBindings + surfelLightingBindingSetPool DELETED (no lazy rebuild -- the texture is
a normal target). NOTE: the strong corner "glow" the user first saw was largely the flicker artifact; the stable bounce
is subtler (the box-interior camera directly lights the floor). Probe tool + measurements in scratchpad.

## (WRONG -- superseded) CORNER FLICKER — "ROOT CAUSE: a frames-in-flight DATA RACE on the surfel pool" (2026-07-05).
The user reported a persistent flicker at the back blue/red/floor triple-corner. Two earlier diagnoses were WRONG
(over-spawn/duplication; then a non-converging fixed-alpha EMA -> the running-mean accumulator below). Built a
multi-frame flicker probe (scratchpad/flicker_probe.py: launches the app, foregrounds it, BitBlt-grabs N frames,
computes per-pixel temporal std). It isolated the flicker to a ~16x8px spot at the exact triple corner (peak std ~12/255,
a RED-channel drop 175->~100 on sporadic frames; ~1/4 launches show it, rest stable -> non-deterministic per launch).
EXHAUSTIVE black-box tests (each 8 runs via the probe), ALL still flicker 8/8 -> RULED OUT: over-spawn (spawn gated to
bootstrap-only), walk truncation (NWB_SURFEL_MAX_WALK 64->512), the accumulator (running mean), ray-count variance (64->256
rays, NO change), the shade shadow ray (off -> WORSE), the ray rotation (frozen -> still flickers), the RMW blend (direct
write -> still flickers), the whole-struct vs partial write. The DECISIVE test: a HARDCODED CONSTANT meanIrradiance (every
surfel writes the identical value, zero computation dependence) STILL flickers 8/8 -> the gather reads pool values that are
NOT what the trace wrote -> a TORN READ / DATA RACE on the pool, not the GI algorithm. Disabling the gather -> perfectly
stable (std 0.00), so it is unambiguously the surfel path. The contributor-count diag showed the gathered SET is stable ->
it is the meanIrradiance VALUES being corrupted.
ROOT CAUSE: the deferred-lighting PIXEL shader reads the persistent RW surfel pool DIRECTLY, while the NEXT frame's
COMPUTE passes (spawn/hash-build/trace) RMW the same single-buffered pool. The engine runs up to m_maxFramesInFlight
frames concurrently (backend_context.cpp:2109), so frame N's pixel read overlaps frame N+1's compute write with no
sufficient cross-frame barrier -> torn reads. This is SPECIFIC to the surfel pool: every other RW resource (caustic
accumulator, shadow BVH) is touched only by compute and the CONSUMER reads a SEPARATE resolved texture -- the caustic
path RESOLVES the accumulator into a screen-space irradiance texture that lighting reads, decoupling reader from writer.
FIX OPTIONS (pick one): (A) mirror the caustic pattern -- a compute "surfel resolve" pass gathers once per pixel into a
screen-space irradiance texture; deferred lighting reads THAT texture (decouples the pixel consumer from the RW pool;
also faster -- gather once/pixel in compute, not in the pixel shader). RECOMMENDED. (B) a correct explicit cross-frame
barrier serialising the pool's pixel-read vs next-frame compute-write (cheaper if the state-tracker gap is real, but
uncertain). (C) double-buffer the pool -- rejected: the round-robin traces only 1/N surfels/frame, so a ping-pong would
need to copy all untraced surfels forward each frame. Probe tool + all measurements in scratchpad.

## U0 status: COMPLETE + VERIFIED + FLICKER-FIXED (for real) 2026-07-05. U0 uses FLAT RGB mean-irradiance (SH lands in U3).
## [The running-mean accumulator below is a real correctness improvement (variance->0) but was NOT the flicker cause -- the
## flicker was overstuffed hash cells; see the "TRUE ROOT CAUSE" section at the top. Both changes stay.]
FLICKER FIX (post-U0, diagnosed by a 6-agent adversarial workflow): the user saw a temporal flicker at the back
blue/red/floor triple-corner. Root cause was NOT surfel duplication (a 5-agent diagnosis REFUTED that: static camera ->
spawn is a one-shot bootstrap, coverage>=1.0 at each tile centre, so the surfel set is FIXED after bootstrap). The real
cause: surfel_trace_cs blended each 64-ray estimate with a FIXED-alpha EMA (hysteresis 0.9), but nwbGiFrameRotation
rotates the ray set every frame -> successive estimates are DECORRELATED Monte-Carlo samples -> a fixed-alpha EMA never
converges, holding a PERMANENT residual std = sigma*sqrt(alpha/(2-alpha)) ~= 23% of single-trace noise, largest where
the bleed is brightest (the corner) = the flicker. FIX = replace the fixed EMA with a BOUNDED RUNNING MEAN: historyWeight
= n/(n+1), n=min(sampleCount, NWB_SURFEL_MAX_ACCUM=64) -> a true incremental average (variance -> 0) that becomes a
bounded EMA past the cap. n==0 seeds directly (subsumes the old sampleCount==0 special case). Repurposed the CB
coverageRadiusBiasHyst.w lane (was hysteresis) to carry the cap; deleted s_SurfelDefaultHysteresis + the CPU seeded?0:0.9
branch (the shader's n==0 handles bootstrap). Files: surfel_trace_cs.slang, surfel_binding_slots.h (NWB_SURFEL_MAX_ACCUM),
raytracing_system.cpp (CB .w + comments), surfel_constants.slangi + surfel_record.slangi (comments). VERIFIED: a numeric
sim of both accumulators over decorrelated samples shows fixed-EMA residual 2.5-7.2% of signal vs running-mean 0.56-2.0%
(4-5x reduction, -> 0 uncapped); the render still shows the correct red bleed, warning-free + validation-clean. NOTE: the
capture harness CANNOT reproduce the flicker (it catches a deterministic per-launch frame; A/B corner diff is dominated by
window-alignment jitter, not GI noise) -- the definitive flicker check is interactive. arithmetic-only change (no
binding/barrier/CB-layout change), bootstrap frame byte-unchanged.

## U0 status: COMPLETE + VERIFIED 2026-07-05. U0 uses FLAT RGB mean-irradiance (SH lands in U3).
VERIFIED: nwb_gi_test_sw_smoke (forced SW emulation, --gpudbg) renders a correct colored bounce -- the saturated-red
wall bleeds a distinct red glow onto the floor at its base (the unfakeable color-bleed GI signal), blue wall opposite.
Warning-free + validation-clean: the capture harness with --reject-log-message "Validation"/"VUID"/"[WARNING]"/"surfel"
all pass (exit 0); the C++ + shader cook build clean (100 cooked files, no grid shaders). The full grid GI subsystem is
retired: deleted gi_probe_{trace_sw,trace_hw,blend_irradiance,blend_distance,border}_cs(+.nwb) + octahedral.slangi +
gi/binding_slots.h; probe_grid.slangi -> sampling.slangi (Fibonacci + R2 rotation only); sw_binding_slots.h trimmed to
slots 0-10; names.h/timing_names.h surfel-only; all m_gi* CPU state -> m_surfel*; NwbGiGridConstantsGpu/push mirrors gone.
CPU shape: 3 DISTINCT binding layouts (cellHead is an SRV at spawn but a UAV at hash-build, so they cannot share one) --
spawn: constants+pool(UAV)+cellHead(SRV)+counter(UAV)+gbuffer worldPos/normal; hash-build: constants+pool(UAV)+cellHead(UAV);
trace: SW BVH 0-10 + constants + pool(UAV). renderSurfelGi order: spawn -> clear cellHead -> hash-build -> trace -> pool +
cellHead UAV->SRV for the gather (setEnableUavBarriersForBuffer on all three). prepareSurfelResources one-shot clears (pool 0
/ cellHead 0xFFFFFFFF / counter 0 on (re)creation) + uploads the CB each frame (divisor 1 while !seeded so ALL surfels trace
on the bootstrap frame). Deferred set binds pool SRV / cellHead SRV / params CB at slots 9/10/11; NULL-SAFE at target
creation -- createBindingSet skips null-handle items with NO warning (resource_bindings.cpp:1907) -- rebuilt once when the
pool appears (surfelLightingBindingSetPool guard, replacing the grid's giLightingBindingSetFrontIsA). U0 caveats (fixed in
later units): the bounce is surfel-LOCAL near the wall (no temporal/spatial spread); the trace dispatches the FULL poolCapacity
each frame (U6 -> dispatchIndirect off the live count); the pool grows monotonically (U1 recycling). NEXT: U1 recycle+free
list+keep-alive age, U2 round-robin budget, U3 2-band SH, U4 surfel->surfel infinite bounce, U5 HW twin, U6 perf.

## (historical) SHADER LAYER DONE 2026-07-05 (CPU + wiring next). U0 uses FLAT RGB mean-irradiance (SH lands in U3).
SHADER LAYER COMPLETE (all CRLF): impl/assets/graphics/gi/gi_sw_trace.slangi (extracted BVH bindings + nwbGiSwTrace
Closest/InstanceClosest/ShadeHit from the grid trace -- reused verbatim); impl/assets/graphics/gi/surfel/{surfel_record,
surfel_constants,surfel_hash,surfel_gather}.slangi + surfel_binding_slots.h + {surfel_spawn_cs,surfel_hash_build_cs,
surfel_trace_cs}.slang(+.nwb); gi/names.h has s_Surfel{Spawn,HashBuild,Trace}ShaderName. Contracts the CPU must honour:
  - Pass order per frame at the renderGi hook: spawn -> (barrier) -> CLEAR cellHead to 0xFFFFFFFF -> hash_build ->
    (barrier) -> trace -> (barrier pool UAV->SRV for the lighting gather).
  - Dispatches: spawn = dispatch(DivideUp(screenW/tile,8), DivideUp(screenH/tile,8),1) [numthreads 8x8]; hash_build =
    dispatch(DivideUp(poolCap,64),1,1) [numthreads 64]; trace = dispatch(activeSurfelCount,1,1) [numthreads 64], where
    activeSurfelCount = DivideUp(poolCap, divisor) (divisor 1 bootstrap -> poolCap groups). NOTE U0 dispatches the full
    poolCap; U6 makes it dispatchIndirect off the live count.
  - Buffers (RWStructuredBuffer, on RendererRayTracingState): pool = poolCap * sizeof(NwbSurfel=64B); cellHead = cellCount
    * 4B; counter = 2 * 4B (bump top, free top). One-shot init: pool zeroed, cellHead = 0xFFFFFFFF, counter = 0. CB =
    sizeof(NwbSurfelConstantsGpu) = 5*float4. Reset counter.bumpTop = 0 EACH frame in prepare (U0 re-spawns from empty
    each frame it renders; the smoke app renders 1 bootstrap frame so this is fine; U1 adds recycling instead).
  - CB (NwbSurfelConstantsGpu mirror, matched to surfel_constants.slangi lane-for-lane): cameraPos+cellSize(=2*radius);
    cellCount,poolCap,frameIndex,divisor(=m_surfelSeeded?4:1); coverageThresh,radius,normalBias(~0.05),hysteresis(0.9);
    maxAge,rays(64),spawnTile(16),screenW; screenH,pad. frameIndex increments each rendered frame; m_surfelSeeded set
    true after the first renderSurfelGi (so bootstrap traces ALL surfels).
  - Consumer wiring: lighting_framework.slangi rename NWB_SCENE_GI_IRRADIANCE/DISTANCE/GI_PARAMS macros (SET+BINDING) ->
    NWB_SCENE_GI_SURFEL_POOL/HASH/PARAMS (same slot indices 9/10/11); lighting.slangi (inside the #ifndef
    NWB_SCENE_GI_SAMPLING_DISABLED block) replace the 3 GI atlas/CB decls with `#include "../gi/surfel/surfel_gather.slangi"`
    and make nwbBxdfIndirectIrradiance body: `float3 gi; if(nwbSurfelGather(worldPosition, normalVector, gi)) return
    half3(gi); return nwbGiHemiAmbient(normalVector);` (keep the outer signature + the normal-bias comment). deferred_
    lighting.cpp createDeferredLightingResources layout (slots 9/10/11 -> StructuredBuffer_SRV pool, StructuredBuffer_SRV
    cellHead, ConstantBuffer surfel CB) + rebuildDeferredLightingGiBindings collapses to build-once-on-handle-change;
    deferred_targets.cpp binding set (443-458) binds the same three. giLightingBindingSetFrontIsA -> a handle-generation
    guard (no ping-pong).
  - DELETE (retired, cleanly): ensureGiHitAlbedoBuffer + slot-15 upload + m_giHitAlbedo; ALL ensureGi{TracePipeline is
    KEPT-as-surfel-trace? no}, ensureGiBlend*/Border* + their dispatches + pipelines/binding-sets/shaders (gi_probe_blend_
    *_cs, gi_probe_border_cs + .nwb); the atlases + ray-data + grid CB + ping-pong flip; the grid state fields + their
    invalidateResources resets; probe_grid.slangi's grid helpers (KEEP only nwbGiFibonacciDirection + nwbGiFrameRotation);
    gi_probe_trace_sw_cs.slang (its reusable core moved to gi_sw_trace.slangi; its main was grid-specific). KEEP: gi_sw_
    trace.slangi, the SW BVH build (buildSceneSwBvh + AssignInstanceBaseColor), the m_giEnabled gate site (-> m_surfelEnabled).

DONE so far (foundation, under impl/assets/graphics/gi/surfel/, CRLF): surfel_record.slangi (NwbSurfel 64B),
surfel_constants.slangi (NwbSurfelConstants 5xfloat4), surfel_hash.slangi (cell coord + pow2 fold + NWB_SURFEL_MAX_WALK
cap), surfel_binding_slots.h (slots 11-17 tail + counter layout + defaults: pool 16384, hash 2^18, tile 16, rays 64,
divisor 4, radius 0.35). REMAINING for U0:
  1. Refactor gi_probe_trace_sw_cs.slang -> extract the BVH bindings + nwbGiSwTraceClosest/InstanceClosest/ShadeHit +
     helpers into a shared gi_sw_trace.slangi (behind a NWB_SURFEL_TRACE_CLOSEST-style seam); the retired grid trace
     main() + the surfel trace both include it. (KEEP the trace; RETIRE only the grid main + grid bindings 11-15.)
  2. New compute shaders + .nwb: surfel_spawn_cs (screen-tile coverage spawn, InterlockedAdd bump alloc, G-buffer in),
     surfel_hash_build_cs (clear cellHead 0xFFFFFFFF + InterlockedExchange link), surfel_trace_cs (workgroup-per-surfel,
     64 rays via nwbGiFibonacciDirection into the surfel TBN, groupshared reduce -> EMA meanIrradiance, hysteresis 0
     while sampleCount==0). + surfel_gather.slangi (3x3x3 capped walk, weight by normal+distance).
  3. gi/names.h: add s_Surfel{Spawn,HashBuild,Trace}ShaderName.
  4. renderer_state.h/.cpp: replace the m_gi grid/atlas/ray-data/hit-albedo/blend/border fields with m_surfel{Pool,
     CellHead,Counter,Constants} + spawn/hashbuild/trace pipelines+sets + m_surfel{Enabled,Seeded,FrameIndex,NeedsClear,
     UpdateCursor}; update invalidateResources.
  5. raytracing_system.cpp: DELETE ensureGiHitAlbedoBuffer + slot-15 0.5 upload + ALL blend/border/distance/atlas
     ensure*/dispatch + the grid CB + the ping-pong flip; ADD ensureSurfelResources + prepareSurfelResources (cellHead
     clear + CB upload, bootstrap divisor 1) + renderSurfelGi (spawn -> hashbuild -> trace with barriers); set
     m_surfelEnabled at the old m_giEnabled site (line ~1373).
  6. scene/lighting.slangi: replace the 3 GI atlas/CB decls with surfel pool SRV + cellHead SRV + surfel CB; the
     nwbBxdfIndirectIrradiance BODY -> surfel_gather (KEEP signature + hemiAmbient + normal bias).
  7. deferred/binding_slots.h + lighting_framework.slangi: rename GI_IRRADIANCE/DISTANCE/GI_PARAMS slots 9/10/11 to
     SURFEL_POOL/HASH/PARAMS (same indices). deferred_lighting.cpp createDeferredLightingResources layout +
     rebuildDeferredLightingGiBindings (collapse to build-once-on-handle-change) + deferred_targets.cpp binding set.
  8. BUILD nwb_gi_test_sw_smoke + capture: expect the red/blue bootstrap bounce (parity with the retired grid), then a
     whole-workspace grep sweep for stale gi_probe_blend/gi_probe_border/m_giIrradianceAtlas/nwbGiProbe* references.

## Cleanup mandate (user 2026-07-05: "remove all retired core later")
The surfel pivot RETIRES the world-grid GI. Remove ALL of it CLEANLY (no dead code) as the surfel units land — not
left orphaned: the probe atlases (irradiance A/B + distance A/B) + ray-data + grid CB + hit-albedo buffer; the blend
(irradiance + distance) + border passes and their pipelines/binding-sets/`ensure*`/shader files (gi_probe_blend_*_cs,
gi_probe_border_cs + .nwb); the grid state fields on RendererRayTracingState + their invalidateResources resets; the
ping-pong front-flip + rebuildDeferredLightingGiBindings flip; and probe_grid.slangi's grid helpers
(NwbGiGridConstants/ProbeWorldPos/LinearIndex/ProbeCount/ProbeActiveThisFrame). KEEP + REUSE: gi_probe_trace_sw_cs.slang
(the closest-hit trace + shade + baseColor tint), nwbGiFibonacciDirection/nwbGiFrameRotation, the sw BVH bindings, and
the nwbBxdfIndirectIrradiance consumer contract. Whole-workspace grep for stale `gi_probe_blend`/`gi_probe_border`/
`m_giIrradianceAtlas`/`nwbGiProbe*` references after each unit (mirror the BXDF-redesign stale-code sweep).
