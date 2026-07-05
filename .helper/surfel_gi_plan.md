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

### Allocation — screen-coverage spawn (integer atomics only)
`surfel_spawn_cs` runs at the existing `renderGi` hook (`system.cpp:428`), where the G-buffer is resident (opaque pass
ended). `numthreads(8,8,1)`, `dispatch(DivideUp(w,16), DivideUp(h,16), 1)` = one thread per 16x16 screen tile (<=1
spawn/tile/frame — no groupshared voting). Each thread reads the tile-center G-buffer worldPos/normal/depth (the SAME
targets deferred lighting binds), hashes the world point, sums coverage `saturate(dot(Nsurf,Ns))*saturate(1-dist/r)`
over the 3x3x3 neighbour cells; coverage < threshold allocates via `InterlockedAdd` on a uint bump counter (integer —
the "no float image atomics" rule is untouched). Every covering surfel gets `lastSeenFrame = frame` (keep-alive). On
the BOOTSTRAP frame the pool is empty, so every visible tile spawns once -> all visible surfaces covered in ONE frame
(the smoke app renders only the bootstrap frame when unfocused, so first-frame usefulness is mandatory — this delivers
it, like the DDGI first-frame-all-trace).

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

## U0 status: IMPLEMENTING (started 2026-07-05). U0 uses FLAT RGB mean-irradiance (the SH decision lands in U3).

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
