# Dynamic Diffuse GI (DDGI) — Architecture Plan & Budget

Status: APPROVED DESIGN — user decisions D1-D6 resolved 2026-07-04 (see §5); D3 = BXDF-contract extension and D4 = +1 shadow ray at hits DIVERGE from the workflow recommendation by user choice, and §2/§3/§4 reflect them. Successor feature to the soft-shadow work (which was explicitly built as the temporal/denoise foundation a dynamic-GI feature reuses; user requirement of record: "dynamic non-baked indirect light next").

All paths relative to `a:\Workstation\NWBLot`. All measured numbers: stress scene (10 skinned chars, 2 shadowed lights), dbg + `--gpudbg` validation, iGPU, GPU-bound ~16fps — 43% cross-session / ~10% within-session variance, so every budget claim is enforced ONLY via same-session interleaved A/B (§7 discipline).

---

## 0. RECOMMENDATION (one line)

**Build DDGI scaled-to-budget as the engine's third "caustics-shaped" world-space light-transport producer** (trace → atlas → sampled in `lighting_ps`, HW RayQuery + SW-BVH twins) — not surfel GI (fixed structural overhead that cannot round-robin down to iGPU scale), not radiance cascades (no production 3D reference; the mature screen-space form is exactly where this engine is weakest).

## 1. Why DDGI wins here

- ALL of DDGI's temporal accumulation lives in the **world-space probe atlas**: the engine's lack of motion vectors / prev G-buffer (view rebuilt every frame) — its biggest GI liability — becomes a **non-issue**. Camera motion is free; zero screen-space ghosting; no disocclusion artifacts. The iGPU failure mode is world-space *lag*, never noise.
- The producer shape is structurally the **already-verified caustics feature**. Crucially, `caustic_photon_sw_cs.slang` already does closest-hit surface interaction through the bounded per-mesh descriptor arrays — SW probe-hit shading is a *variation of verified code*, not new traversal machinery.
- Probe rays inherit the per-frame skinned-mesh TLAS / SW scene-BVH rebuild at zero extra build cost.
- Output is noiseless by construction (the atlas IS the accumulator) — nothing added to the a-trous budget.

## 2. Architecture

**Grid + params.** One world-space probe grid; all dims/rays/fraction are RUNTIME values in a dedicated GI constant buffer (~10 Float4 — fits neither the 16B scene CB `NWB_SCENE_SHADING_BUFFER_FLOAT_COUNT=4` nor the 128B push ceiling `core/graphics/api.h:138`); named-macro defaults in new `impl/assets/graphics/gi/binding_slots.h`.

**Trace (per frame).** `NWB_GI_RAYS_PER_PROBE` spherical-Fibonacci rays — ONE shared per-frame R2 rotation (reuse the caustics P5b sequence; no per-ray RNG), workgroup-per-probe so SW-BVH stack traffic stays bounded — from a round-robin fraction of probes. HW = RayQuery-in-compute (`shadow_rayquery_soft_cs` pattern, NOT a full RT pipeline) over `m_tlas`; SW = scene SW-BVH + bounded descriptor arrays. ONE kernel body (`gi/gi_probe_trace.slangi`) behind a traversal `#define` seam.

**Hit shade (v1, per D4).** Lambert direct light over the ≤8 `NwbSceneLight` entries + per-instance flat albedo (CPU-built StructuredBuffer; `NWB_GI_DEFAULT_ALBEDO` fallback) + PREVIOUS-front-atlas probe fetch (infinite bounce from day one — one texture fetch), PLUS **one occlusion shadow ray toward the DOMINANT light per hit** (`NWB_GI_HIT_SHADOW_RAYS=1`, user choice D4; the other lights stay unshadowed; the occlusion ray is any-hit/early-out so it is cheaper than the primary ray, est. +30-50% trace cost — kept as a named knob so U3 can A/B it against 0 if the budget gate forces the question). NEVER the per-material BXDF dispatch at hits. Miss writes far-distance/zero; backface writes short-distance/zero (feeds v2 classification).

**Ray data.** Ray results land in a ray-data texture whose first `NWB_GI_PROBE_FIXED_RAY_COUNT=32` slots are deterministic-and-excluded-from-blending — reserved so v2 relocation/classification are additive kernels, never layout migrations.

**Blend.** Backend-agnostic gather-style kernels (thread-per-atlas-texel — NO float image atomics on this backend, caustic accumulator precedent `raytracing_system.cpp:1646-47`) EMA into ping-pong octahedral atlases:
- irradiance: 6x6 interior + 1-texel border (RGBA16F v1 → R10G10B10A2 + gamma-5 perceptual encode in U7, BEFORE tier tuning);
- distance: mean/mean² 14x14 interior + border, RG16F.
The distance atlas + Chebyshev(weight³) + self-shadow bias `(0.2N+0.8V)·0.75·minSpacing` are **NOT cut for budget** — leak rejection IS the feature. Ping-pong with a `frontIsA` flag is mandatory (in-place SRV+UAV RMW violates the house rule at `renderer_state.h:465-467`); trace reads prev-front, blend writes back, flip at block end.

**Ownership + wiring.** Atlases/ray-data/probe-data/grid-CB live on `RendererRayTracingState` beside the caustic block (`renderer_state.h:643-677`) — NOT `DeferredFrameTargets`, which is torn down on every window resize and would silently reset probe convergence; lifetime = grid-param change or device reset only. `ensureGi*` cloned from `ensureCausticResolvePipeline/BindingSet`; prep hooks into `prepareShadowVisibilityResources` after the caustic prepare; render dispatches insert in `system.cpp render()` between the caustic producer and `renderDeferredLighting`: trace → blend_irr → blend_dist → border → flip. Feature gate `hasGiWork()` clones `hasCausticWork()` (`raytracing_system.cpp:2595`); black-cleared atlases are the additive identity so `lighting_ps` stays branchless.

**Lighting entry (per D3 — BXDF CONTRACT EXTENSION, user choice over the harness fold).** The engine harness still OWNS the probe evaluation (sampling correctness is engine-side): `scene/lighting.slangi` gains `nwbBxdfIndirectIrradiance(worldPos, normal, view)` beside `nwbBxdfCausticIrradiance` (`:85`), doing the full bias+trilinear+Chebyshev evaluation ONCE per pixel, with hemiAmbient (`:175-177`) as the outside-the-volume fallback value. DELIVERY is the contract change: the evaluated diffuse irradiance is passed INTO each material's BXDF entry point as a new explicit input threaded through the cook-GENERATED `bxdf_dispatch.slangi` — materials decide how to CONSUME it (lambert adds `albedo*irradiance`; toon may quantize/tint its indirect term — the reason this option was chosen). COSTS ACCEPTED: the cook generator changes, the BXDF signature changes, and every existing `.bxdf` recompiles against it — the migration must keep existing `.bxdf` files compiling (additive parameter with a helper macro/default consumption pattern), gated on `nwb_assets_graphics_tests` 40/40 + the toon-vs-lambert testbed check. New `NWB_DEFERRED_LIGHTING_BINDING_GI_IRRADIANCE/_GI_DISTANCE/_GI_PARAMS` slots added to BOTH the layout (`createDeferredLightingResources`) AND the set (`createDeferredFrameTargets`) — built in two places, must be edited together.

**Tiering.** Three runtime tiers via the grid CB — Smoke (8x4x8=256 probes × 32-64 rays × 1/2 fraction), Default (16x8x16=2,048 × 64 × 1/4 = 32k rays/frame), Quality (96 rays × 1/2, funded by §7 reclaims). Update fraction is the MASTER pressure valve (halving it halves trace cost, trades only convergence latency); rays/probe second; grid dims third. DEFERRED with plumbing reserved from day one: relocation + classification (v2 — probe-data texture allocated-but-inert; classification = −30-50% future trace), scrolling/cascades (v3 — grid-coord math isolated in `gi/probe_grid.slangi` so toroidal wrap touches one file).

## 3. Unit plan

- **U0 — invalidateResources latent-gap fix (standalone commit).** Add the missing soft-shadow-resolve-family resets to `RendererRayTracingState::invalidateResources()` (`renderer_state.cpp` — VERIFIED: currently resets ZERO of `m_shadowResolve*`/`m_shadowGeometryDownsample*`/`m_shadowReprojectMerge*`/RGB/transparent variants/soft kernels/edge buffers declared at `renderer_state.h:461-597`). No GI code. Verify: device-reset smoke recreates the family warning-free.
- **U1 — GI resource scaffold + gate + timing scopes.** `gi/binding_slots.h` (ALL named macros); RendererRayTracingState GI block (atlases A/B, ray-data, probe-data inert, grid CB, hit-albedo buffer, flags, per-pass pipeline members cloned from `m_causticResolve*`); `ensureGiResources`; `hasGiWork()`; `render.gi_probe_trace/blend/border` timing Names; stub kernels (6-line .nwb clones — cook auto-scans); every handle reset in invalidateResources. Verify: warning-free; `NWB_GPU_TIMING_FILE` shows gi scopes; **window-resize does NOT recreate atlases** (proves placement); device-reset clean. ~0.05ms.
- **U2 — Lighting-side consumer + BXDF-contract extension (visible first — constant-atlas tint).** Both binding sites + `nwbBxdfIndirectIrradiance` (harness-evaluated) + the D3 contract change: extend the BXDF entry-point signature with the diffuse-irradiance input through the cook generator (`bxdf_dispatch.slangi` generation), migrate the existing `.bxdf` files (lambert consumes `albedo*irradiance`; toon free to quantize), hemiAmbient becomes the outside-volume fallback VALUE passed through the same input. U1's stub fills the atlas with a known constant so this unit is independently visible. Verify: uniform tint inside volume fading to hemiAmbient at edge (capture A/B); `nwb_assets_graphics_tests` 40/40 after the generator change; toon-vs-lambert testbed still shades distinctly; interleaved deferred_lighting delta (expect +0.3-0.8ms). NOTE: D3 makes this the second-riskiest unit (cook-generator + signature migration) — it grew from the harness-fold version by design.
- **U3 — SW probe trace + hit shade (the cost-risk path, forced-emulation first).** `gi_probe_trace.slangi` + `gi_probe_trace_sw_cs`; Fibonacci + R2 rotation; closest-hit cloned from `caustic_photon_sw_cs`; Lambert + albedo buffer + prev-atlas fetch + **the D4 dominant-light occlusion shadow ray** (`NWB_GI_HIT_SHADOW_RAYS=1`); round-robin cursor in push constants; fixed-ray slots reserved. Verify: ray-data readback nonzero; `render.gi_probe_trace` measured interleaved at BOTH `NWB_GI_HIT_SHADOW_RAYS` 1 AND 0 — **the go/no-go gate on the SW cost estimate AND on whether D4 fits the D1 gate, BEFORE later units build on it**. Est. ~0.65-1.8ms SW @ 32k rays with the shadow ray (+30-50% over the 0.5-1.2 unshadowed base).
- **U4 — Blend + border + ping-pong (first real bounce visible).** `gi_probe_blend_irradiance_cs` (gather, cosine weight, hysteresis EMA), `gi_probe_blend_distance_cs` (mean/mean², hitT clamped ~1.5× grid diagonal), `gi_probe_border_cs`; non-updated probes pass-through-copy; `m_giSeeded` (frame-0 hysteresis 0). Verify: bounce color visible; convergence stable; no ghosting with skinned motion; zero aliasing validation errors; no octahedral-edge banding; blend+border <0.3ms.
- **U5 — Dedicated GI smoke scene.** White open-top box + ONE saturated-red wall; directional light so the wall is lit but the adjacent floor is in direct shadow → **indirect red bleed on the shadowed floor = unfakeable pass signal** (the hemiAmbient replacement makes this true GI detection); THIN interior wall lit one side = the leak detector (Chebyshev + bias gate); skinned character walk-through; scripted light toggle for convergence timing; NO refractive casters (caustics gate off = the scene is GI-funded, §7 #1). Capture assertions scripted.
- **U6 — HW twin + dual-path parity.** Same body behind the RayQuery seam; downstream fully shared → parity by construction. Verify: HW-vs-SW capture diff <1% (caustics 0.55% precedent); both warning-free; HW trace ~0.6× SW.
- **U7 — Perception pass (BEFORE tier lock).** R10G10B10A2 + gamma-5 encode (RGBA16F fallback macro); per-texel change-detect (luma delta > threshold → hysteresis 0.97→~0.5 for that texel — snaps light toggles); `gi_probe_variability_cs` pausing the trace when converged (static scenes → ~0ms steady-state). Verify: perceived toggle snap <300ms; converged trace ~0ms; no 10-bit banding.
- **U8 — Budget validation + tier lock.** Same-session interleaved GI-on/off on stress (grid over the char extent), drift-normalized; repeat on opt config (validation inflates compute non-uniformly — ranking may reorder); lock tier defaults; record in memory. Gate: `render.gi_*` sum ≤2.5ms SW / ≤1.8ms HW at Default, else drop `NWB_GI_UPDATE_DIVISOR` one step (never rays first).
- **U9 — Pay-for-it execution (parallelizable after U1).** (a) split the transparent-shadow trace into its own timing scope (prerequisite evidence); (b) keep-wide-dilations a-trous schedule (documented at `shadow_resolve_binding_slots.h:97-99`; `1<<pass` derivation at `dispatchSoftShadowResolve`); (c) per-light transparent-caster cone-vs-AABB gate (reuse the gather pattern `raytracing_system.cpp:1106-1149`) — glass-scene visual A/B REQUIRED before it funds anything; (d) verify/document the caustic gate as the glass-free funding mechanism.

## 4. Budget table

GI additions per tier (anchor: ~3-5ms/Mray coherent SW × 2-4x incoherence × ~2x hit-shade; HW ≈ 0.6× SW; re-confirmed at U3/U8):

| Tier | Probes × rays × fraction | Rays/frame | Trace SW / HW | Blend | Lighting Δ | TOTAL SW / HW |
|---|---|---|---|---|---|---|
| Smoke | 256 × 32-64 × 1/2 | 4-8k | 0.3-0.7 / 0.2-0.4 | <0.2 | +0.3-0.5 | **~0.7-1.4 / ~0.6-1.1 ms** |
| Default (unshadowed hits) | 2,048 × 64 × 1/4 | 32,768 (~1/15-1/30 of ONE shadow light) | 0.5-1.2 / 0.3-0.7 | <0.3 | +0.3-0.8 | **~1.2-2.5 / ~0.9-1.8 ms** |
| **Default + D4 shadow ray (SHIPPING config)** | 2,048 × 64 × 1/4 | 32,768 primary + ≤32,768 occlusion | 0.65-1.8 / 0.4-1.0 | <0.3 | +0.3-0.8 | **~1.4-3.1 / ~1.0-2.1 ms** |
| Quality | 2,048-5,760 × 96 × 1/2 | — | — | — | — | ~2.5-3.5 ms SW (needs U9 reclaim) |

**D4-vs-D1 TENSION (explicit):** the chosen shipping config (Default + hit shadow ray) straddles the chosen ≤2.5ms SW gate — the upper estimate (3.1ms) EXCEEDS it. Resolution protocol at U3/U8, in order: (1) if measured over-gate, drop `NWB_GI_UPDATE_DIVISOR` 1/4→1/8 first (halves trace; convergence latency doubles, masked by U7 change-detect); (2) then rays 64→48; (3) only if both fail, flip `NWB_GI_HIT_SHADOW_RAYS`→0 and report to the user — D4 is their call to un-make, not the gate's.

Steady-state static scenes after U7: trace → ~0ms. Memory: <6MB ping-ponged (trivial). Convergence: ~1.5-3s raw at 16fps GPU-bound; perceived <300ms after U7 change-detect.

**Target end-state (Default tier, stress-scene numbers):** glass-free scenes: 24ms − 3.6-4.3ms (caustics gated off, already in-code) + ~1.2-2.5ms GI ≈ **21-22ms — GI lands with the frame NET FASTER than today.** Glass scenes: 24ms + GI − U9(c) reclaim (est. 2-5ms) ≈ 21-24ms pending measurement.

## 5. USER DECISIONS (RESOLVED 2026-07-04)

- **D1 = Default tier** — the U8 gate enforces ≤2.5ms SW / ≤1.8ms HW on the stress scene. *(As recommended.)*
- **D2 = Both paths from day one, SW-first** — U3 (SW) before U6 (HW); SW's measurement is the go/no-go gate. *(As recommended.)*
- **D3 = EXTEND THE BXDF CONTRACT** *(user choice over the recommended harness fold)* — the evaluated irradiance is threaded through the cook-generated `bxdf_dispatch.slangi` as an explicit BXDF input, so materials can RESHAPE their indirect term (toon quantization etc.), consistent with the engine's no-default-BXDF philosophy. Accepted costs: cook-generator change + BXDF signature migration + every `.bxdf` recompiles; U2 grows accordingly and gates on assets tests 40/40 + toon/lambert testbed.
- **D4 = +1 OCCLUSION SHADOW RAY to the dominant light per hit** *(user choice over the recommended unshadowed)* — more correct bounce near shadowed areas, est. +30-50% trace. Kept behind `NWB_GI_HIT_SHADOW_RAYS` (ships =1); U3 measures both settings; the D4-vs-D1 tension protocol in §4 governs if the gate is exceeded (divisor first, rays second, flag to user last).
- **D5 = Fixed hand-placed volume** per scene (edge fade → hemiAmbient); grid-coord math isolated in `gi/probe_grid.slangi` so v3 scrolling touches one file. *(Plan default.)*
- **D6 = Relocation/classification deferred to v2** with plumbing reserved (inert probe-data texture + 32 fixed-ray slots — the reservation discipline is NOT optional). *(Plan default.)*

## 6. Test scenes

NEW `gi_test` project (U5, `tests/smoke/*_project.cpp` pattern) — bounce/leak/lag all capture-assertable; grid hand-placed clear of geometry; caustics gated off. EXISTING: `soft_shadow_test` = warning-free + resize/device-reset + converged-static checks; `stress_test` = the ONLY budget-comparable scene (U8 tier lock, interleaved + drift-normalized, then opt config). Harnesses: `python tests/smoke/launcher.py <scene> --config dbg --gpudbg --run-seconds N` + `NWB_GPU_TIMING_FILE` + `testbed_window_capture_smoke.py`.

## 7. Pay-for-it plan (ranked by evidence quality)

1. **Caustic gate leverage — measured, zero-risk, ALREADY IN CODE.** `hasCausticWork()` fully gates the producer+resolve: glass-free scenes reclaim the measured ~3.6-4.3ms — more than Default-tier GI costs.
2. **Keep-wide-dilations a-trous — 0ms, defends ~1.8ms already banked.** Run dilations (4,8,16) instead of (1,2,4) at identical cost; buys back the grain the 5→3 trade cost. Needs a visual A/B only.
3. **Transparent-shadow per-light scope gate — est. 2-5ms in glass scenes, MEDIUM RISK.** Step 1: split its timing scope (no claim valid before this number exists). Step 2: conservative cone-vs-AABB skip. A wrong test silently drops colored glass shadows → glass-scene visual A/B required.
4. **caustic_resolve a-trous optimization — 2.6-3.3ms scope** (already flagged the prime GPU opt target). 1-1.5ms reclaim funds Quality tier in glass scenes. Gate on the caustic-sphere capture A/B (0.55% parity bar). Separate follow-up, not a GI unit.
5. **Variability pause (U7)** — GI paying for itself on converged static fields.

DISCIPLINE: every reclaim and GI stage measures same-session, interleaved, drift-normalized against a control scope; the opt-config re-measure may reorder #3 vs #4.

## 8. Risks (top)

- **SW incoherent closest-hit cost blowout** — the single biggest estimate risk (could miss 3-5x if shaded naively). Locked mitigations: no shadow rays at hits, one per-frame rotation, workgroup-per-probe, Lambert-only, divisor valve; **U3 measures BEFORE anything builds on it.**
- **Two-site lighting binding edit** (layout + set) — editing one fails createBindingSet at runtime; U2 edits both in one commit.
- **invalidateResources omissions are proven-easy** (the existing gap shipped silently) — U0 fixes it standalone; every U1+ handle verified per unit.
- **Atlas placement** — DeferredFrameTargets teardown-per-resize would silently reset convergence; RendererRayTracingState placement is load-bearing (U1 resize smoke asserts it).
- **Ping-pong flip order** — trace must read PREV front; flipping before the trace self-feeds and diverges; in-place RMW banned outright.
- **BXDF-contract migration (D3)** — the generator change + signature migration touches every cooked material; a botched migration fails at COOK time (good) or subtly changes shading (bad). U2 gates on assets tests 40/40 + the toon-vs-lambert testbed capture; the new input must be additive/defaultable so existing `.bxdf` files stay compiling during the migration.
- **D4 budget tension** — the shipping config's upper estimate (~3.1ms SW) exceeds the chosen ≤2.5ms gate; the §4 resolution protocol (divisor → rays → flag D4 back to the user) is the contract, enforced at U3/U8 with measurements at both `NWB_GI_HIT_SHADOW_RAYS` settings.
- **Per-instance hit-albedo plumbing is the least-templated piece** — if the caustic SW twin's instance-to-material mapping is not directly reusable, U3 grows; probe the mapping first thing in that unit.
- **Late atlas-format churn** — U7 (10-bit + gamma) is deliberately sequenced before the U8 tier lock so hysteresis/bias never retune twice.
- **Measurement validity** — no budget claim from estimates or cross-session captures; U8 protocol only.
