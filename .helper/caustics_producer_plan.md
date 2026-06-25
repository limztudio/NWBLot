# Caustics Producer — Architecture Plan & Recommendation

Status: APPROVED DESIGN — supersedes the deferred "producer" decision in `.helper/caustics_plan.md:41-46` (Phase 0 IOR/refraction material model stays as built).

All paths relative to `a:\Workstation\NWBLot`.

---

## 0. RECOMMENDATION (one line)

**Build Approach 1 — light-side photon-splat caustics.** Approach 2 (eye-side gather) is rejected for v1 because it cannot, by construction, produce the bright, focused caustic the user explicitly requires.

---

## 1. The decision and WHY (parity vs focusing quality vs risk, weighed)

The choice is governed by four inputs, in priority order as stated by the user.

### 1a. The user's BRIGHT, FOCUSED-caustic goal is the deciding factor — and it eliminates Approach 2 by construction

The user asked for "a genuinely BRIGHT, FOCUSED caustic (not soft brightening)" and instructed that **if one approach cannot truly focus, weight that heavily.** This is not a tuning gap in Approach 2 — it is structural:

- Caustic peak brightness IS the Jacobian (area-compression ratio) of the light→glass→receiver map. Focusing means that map is singular: many distinct light-side rays land in a vanishing receiver area, so density (brightness) spikes on a thin codimension-1 fold (the crescent/cusp).
- An eye-side per-pixel gather sits at one receiver point and integrates incoming directions over a positive-measure set with a finite sample budget, then must denoise. That pipeline is a **low-pass filter applied to the true caustic** — it recovers total energy and footprint but the peak is provably attenuated (peak ≈ energy / pixel-area, never the energy / near-zero-area spike). Approach 2's own design document concedes this three independent ways and recommends the feature be renamed "refractive receiver brightening."
- The only eye-side methods that DO resolve the cusp (specular manifold sampling / MNEE with the explicit Jacobian) abandon the cheap, shadow-pass-shaped, byte-identical framing that was Approach 2's entire reason to exist — they cost like Approach 1 without its robustness at the singularity.
- Approach 1 gets focus for free and physically: photons land at their true refracted destination, so where the lens converges them, many photons pile into one small receiver cell. The per-cell count IS the density estimate of the singular measure. Bright crescent emerges from real convergence, not a heuristic boost.

Conclusion: focusing is a hard requirement; only Approach 1 can satisfy it. Per the user's explicit weighting, this dominates.

### 1b. The HW/SW byte-identical mandate — survivable on BOTH approaches, with the same relaxation

The hard mandate is that the HW and SW trace paths stay byte-identical. Neither approach can keep the traversal bit-exact — that divergence already exists and is accepted for shadows (HW `RayTCurrent()`/`HitAttributes` vs SW explicit-stack Möller-Trumbore; "only the `t` source differs," `shadow_sw_traversal_cs.slang:302-304`). What both approaches CAN and MUST keep byte-identical is the **physics math**, isolated in a shared `.slangi` that both backends `#include` — exactly the existing shadow factoring (shared math in `mesh/surface.slangi`, backend-specific geometry fetch).

Crucially, the parity mandate does NOT favor Approach 2: a multi-bounce refractive trace amplifies first-hit normal discrepancies between the two intersectors regardless of which approach traces it, and Approach 2's second-interface walk is just as normal-sensitive as Approach 1's bounce loop. Both resolve it the same way — bend on the GEOMETRIC face normal (not the interpolated shading normal) so the two intersectors agree — and both adopt the same realistic bar: **converged-image A/B within Monte-Carlo tolerance, not per-frame bit-equality** (Approach 1's stochastic jitter makes per-frame bit-equality meaningless anyway; this is the relaxation `.helper/caustics_plan.md:43` already anticipated). So parity is a wash between the two — it does not rescue Approach 2.

### 1c. The standing mandate ("never hacky; full-redesign is fine; do it right") points at Approach 1

Approach 2's honest verdict is that within its cost/parity envelope, sharp caustics are unreachable — so shipping it as "caustics" would be the hacky outcome the standing mandate forbids (a feature that looks like a glow and is quietly mislabeled). The non-hacky path is the technique that actually does the physics: light-side photon transport with real convergence. "Don't dread full-redesign" directly licenses the bigger Approach-1 build (new light-side dispatch, new additive buffer, R32_UINT splat + resolve) rather than a cheaper gather that structurally can't meet the goal.

### 1d. Engine fit + risk — Approach 1 is a larger but better-fit build; its risks are bounded and known

- Approach 1 reuses ~80% of the shadow plumbing: per-frame HW TLAS/BLAS + SW GPU-LBVH (static and skinned), per-instance material table indexed by `InstanceID()`==SW-leaf, the importance/slot allocator, and the existing Fresnel/Beer-Lambert helpers. The `refractive` classification is already plumbed end-to-end (`MaterialSurfaceInfo.refractive` → `RtInstanceMaterialFlag::Refractive` bit1 → `NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE`, `instance_material.slangi:36`) and is the producer's intended reader.
- The backend confirms every capability Approach 1 needs: recursion is supported up to the device cap (`raytracing.cpp:1099,1267`) though Approach 1 deliberately stays at recursion 1 via a raygen-driven iterative bounce loop (keeps HW structurally identical to the SW iterative kernel — the cleanest way to hold parity).
- The one decisive backend constraint: there are NO float image/buffer atomics anywhere in `core/graphics/vulkan` (every atomic is integer — `InterlockedAdd`/`Min`/`Max` on `uint` in BVH refit and CSG; `VK_EXT_shader_atomic_float` is not enabled). This forces the splat target design (R32_UINT fixed-point accumulators + a resolve pass) — it is a known, contained decision, not an open risk.
- Approach 1's real risks (emission budget wasted on empty space, splat aliasing, atomic hot-spotting on focused pixels) are all bounded and have concrete mitigations (tight occluder-AABB emission, jitter + temporal + footprint + edge-aware blur, fixed-point clamp). They are engineering risks with known answers — not a structural ceiling like Approach 2's.

### 1e. Summary of the trade

| Axis | Approach 1 (photon splat) | Approach 2 (eye gather) |
|---|---|---|
| Focus (the hard requirement) | YES — physical convergence | NO — soft brightening only, by construction |
| Byte-identical parity | Shared physics header; relaxed converged-image bar | Same shared-header trick; same relaxed bar — no advantage |
| "Do it right" mandate | Real light transport | Would ship mislabeled glow → hacky |
| Engine fit | Larger build, ~80% plumbing reused | Smaller build, same plumbing reused |
| Risk profile | Bounded engineering risks, known fixes | Structural ceiling (cannot focus) = unfixable within envelope |

**Net: Approach 1 wins on the single highest-weighted axis (focus), ties on parity, wins on the standing mandate, and trades a larger build for a feature that actually meets the goal. Approach 2's only edge — lower cost — buys a feature that fails the requirement, so it is no edge at all.**

---

## 2. Chosen end-to-end architecture (Approach 1, photon-splat)

### 2.0 The governing structural decision

Photon generation is decoupled from the screen. The producer does NOT use "one thread per screen pixel" (that is an eye-side parameterization). Photons are generated in LIGHT space, aimed at the refractive occluders, with no relationship to screen pixels. Everything below follows from this.

### 2.1 Photon generation (light-side, importance-aimed)

- **Caustic-light budget:** a new CPU pass `ResolveCausticLights` (mirrors `ResolveSceneLights`, `renderer_scene_private.h:66-106`) ranks lights by radiant power × "scene contains a refractive instance in range." Directional + spot get 1-4 caustic slots; point lights are EXCLUDED in v1 (omnidirectional emission = far too many photons). The chosen caustic-slot index is written into `NwbSceneLight.params.w` (currently reserved; keeps the GPU struct byte-stable, no version bump). Negative = no caustic slot.
- **Aimed emission domain (the make-or-break):** gather the world AABBs of only the `refractive` instances (reuse `instanceAabbMin/Max` from `buildSceneSwBvh`, `raytracing_system.cpp:551,694`) into a per-caustic-light emission-target structured buffer. The photon emission domain is the projection of those AABBs onto a plane perpendicular to the light's principal direction (directional/spot); the dispatch is N×N photons (tunable, e.g. 256×256). A loose domain (whole light solid angle) at the same budget = sparse sparkle; a tight occluder-projected domain = focus. This is the single most important generation decision.
- **Flux normalization:** each photon carries `flux = totalEmittedPower / photonCount`, so total emitted power is independent of photon count (critical so doubling photons leaves the converged image unchanged — the energy-conservation invariant).
- **Jitter:** per-frame rotated low-discrepancy offset (Halton/R2 + frame index) within each emission cell, so the splat dithers across frames and the temporal accumulator averages out the grid → smooth crescents instead of a lattice of dots.

### 2.2 Refractive photon tracing (Snell bend at entry + exit, continue to receiver)

Each photon: emitter → first refractive surface (Snell refract, entry) → through medium (Beer-Lambert over the true interior path) → exit surface (Snell refract, exit) → free ray → nearest OPAQUE receiver hit = the splat. Generalizes to a bounded loop "until opaque receiver hit, or MAX_BOUNCES, or flux < epsilon." Each refractive crossing flips eta (air→glass `eta=1/ior`, glass→air `eta=ior`) and applies the Fresnel-transmitted fraction. TIR (refract returns zero) drops the photon in v1 (documented; reflection is a later option).

- **Snell primitive:** a new shared header `caustic/refract.slangi` with `nwbCausticRefract(incident, normal, eta)` — a faithful port of `SIMDVectorDetail::RefractV` (`global/math/vector.h:2811`), including its TIR-returns-zero branch (matches `Vector3Refract`, `:3043-3047`). Slang built-ins only. Do NOT rely on the `refract()` intrinsic — write the explicit form so HW and SW lower identically.
- **Fresnel + Beer-Lambert:** reuse `nwbShadowFresnelTransmittedFraction` and `nwbShadowAbsorptionCoefficient` (`mesh/surface.slangi:83-94`) VERBATIM — already Slang-built-ins-only and byte-identical across backends.
- **HW path:** new pipeline `CausticPhotonRayGen` + `CausticClosestHit` + `CausticMiss`. The bounce loop lives in **raygen, iterative, recursion stays at 1** — each segment is a fresh `TraceRay`. This keeps the HW algorithm structurally identical to the SW iterative kernel (the only way to hold parity), makes the bounce budget an explicit constant, and avoids multiplying payload/stack pressure. Closest-hit is thin: it fills a payload `{ hitPosition, hitNormal, transmission, ior, flags, t }` (~48 B; grow `setMaxPayloadSize` from the shadow pipeline's 16; keep `setMaxRecursionDepth(1)`). Raygen does all physics + the splat (UAV writes from raygen are fine).
- **SW path:** a 1D compute dispatch over `photonCount` (NOT 2D over screen). Adapt the shadow SW traversal (`shadow_sw_traversal_cs.slang:239-366`) from any-occluder-accumulate to **closest-hit (track min-t)**; wrap it in the same bounce loop, re-seeding origin/dir/eta/flux after each refractive crossing. Positions are already bound (`g_NwbSwShadowMeshPositions`, `sw_binding_slots.h:32`), so the geometric face normal is computed directly from `cross(v1-v0, v2-v0)`.
- **HW position-buffer gap (closed here):** the HW shadow set binds only `MESH_INDICES`(8)+`MESH_ATTRIBUTES`(9) — NOT positions (BLAS-owned, `occlusion.slangi:68-70`). The world hit point is reconstructed as `WorldRayOrigin()+RayTCurrent()*WorldRayDirection()` (no positions needed), but the GEOMETRIC face normal needs the three object-space vertex positions. So the **caustic HW binding layout adds a `MESH_POSITIONS[N]` SRV array** (the SW set already has it). This closes the gap on the caustic pipeline's OWN layout — never on the shadow layout.

### 2.3 Splat / accumulation onto the receiver

- **Target space — screen-space additive buffer (chosen v1):** project the receiver hit point through the camera VP to a pixel and deposit flux there. Trivial integration (same pixel parameterization as G-buffer + shadow visibility), bounded memory, no UV-atlas authoring. Texture-space (UV atlas) and world-space photon-map alternatives are rejected for v1 (heavy authoring / final-gather complexity the engine has no infra for).
- **Deposit mechanism — R32_UINT packed-atomic + resolve (forced):** no float atomics exist, so the splat target is fixed-point `R32_UINT` accumulators (3 channels, or one RGB-packed). Each photon does `InterlockedAdd(g_CausticAccumR[pixel], uint(flux.r * FIXED_POINT_SCALE))` per channel into a 2×2/3×3 tent-weighted footprint (footprint = the splat-side AA, complementary to emission jitter). A resolve compute pass converts fixed-point→float, area-normalizes (flux / receiver-area-per-pixel → irradiance), edge-aware-blurs, and writes the final RGBA16F caustic irradiance buffer. Mirrors the CSG interval-combine R32-packed pattern.
- **Wrong-surface rejection:** before splatting, compare the photon's camera depth at the projected pixel against the G-buffer depth there (reuse the depth SRV the shadow pass binds); reject beyond tolerance → kills screen-space light leak.
- **Why density becomes brightness (focus, physically):** resolve divides accumulated flux by the receiver area subtended by the pixel → irradiance. Where the lens focuses many photons into a small area, accumulated flux is high AND area is small → irradiance spikes → bright crescent. Automatic and physical; a single `causticIntensity` artist multiplier scales the final look.
- **Denoise stack (mandatory at a sane budget):** emission jitter + temporal accumulation (EMA history blend, camera-reprojected, motion-rejected when a refractive instance/light moved) is the primary smoother; splat footprint spreads each photon; an edge-aware (depth+normal-guided) à-trous/Gaussian blur in the resolve is the cheapest big quality win. No single layer suffices — the stack is required.

### 2.4 Additive integration into deferred lighting

Shadow visibility is multiplicative `[0,1]` (can only darken). A caustic is additive `[0,inf)`. The engine has NO existing additive irradiance buffer — this is the new structural piece.

- **Deposit point — inside `nwbSceneApplyLighting`, PRE-tonemap (chosen):** in `scene/lighting.slangi:124-133`, after the per-light loop accumulates `litColor` in linear HDR and BEFORE the Reinhard tonemap `litColor/(litColor+0.65)`:
  ```
  litColor += causticIrradiance * baseColor;   // NEW: additive, scene-referred, pre-tonemap
  return litColor / (litColor + float3(0.65));
  ```
  Pre-tonemap is physically right: the caustic is real incident irradiance and must roll off through the same tonemapper as direct light (a focused caustic SHOULD be able to saturate toward white). Multiplying by `baseColor` reflects the caustic off the receiver albedo; the Beer-Lambert glass tint rides inside `causticIrradiance`. The cheaper composite-pass add (`composite_ps.slang:36`) is rejected — it makes caustics a flat overlay that can't interact with exposure.
- **Per-material opt-in:** because BXDFs are per-material and call `nwbSceneApplyLighting` themselves (no engine default BXDF; `lighting_ps.slang:30` dispatches per material), the engine provides accessor `float3 nwbBxdfCausticIrradiance(int2 pixel)` and the receiver material's BXDF adds it — mirroring how shadows enter via `nwbBxdfLightTransmittance`.
- **Binding plumbing (clone the shadow-visibility lifecycle, inverted):** allocate the RGBA16F caustic irradiance buffer + R32_UINT accumulators + history next to `shadowVisibility` (`createShadowVisibilityTarget`, `raytracing_system.cpp:802`); CLEAR the accumulators to BLACK each frame (additive identity, vs the shadow buffer's white) next to `clearShadowVisibility` (`:861`). Add `NWB_DEFERRED_LIGHTING_BINDING_CAUSTIC_IRRADIANCE`=8 to `deferred/binding_slots.h`, bind the SRV in `deferred_targets.cpp` (~`:419`), add the layout entry in `deferred_lighting.cpp:62`, declare `g_NwbCausticIrradiance` in `scene/lighting.slangi` (next to `g_NwbShadowVisibility:30`).
- **Pass orchestration:** dispatch the producer in `system.cpp` right after the shadow pass (`:334-347`) and BEFORE `renderDeferredLighting` (`:349`): emit photons → resolve → lighting reads the resolve output. Runs only if there is >=1 caustic light AND >=1 refractive instance (else the black-cleared buffer = additive no-op, exactly like the all-lit white shadow default).

---

## 3. HW/SW byte-identical parity story

The mandate is satisfied the same way the shadow trace already satisfies it: shared physics math + backend-specific geometry fetch, with the divergence confined to the unavoidable intersector difference.

- **Shared (byte-identical) layer:** all per-bounce physics — `nwbCausticRefract` (the ported Snell + TIR), Fresnel (`nwbShadowFresnelTransmittedFraction`), Beer-Lambert (`nwbShadowAbsorptionCoefficient`), flux update, eta flip, splat math, fixed-point conversion — lives in `caustic/refract.slangi` (+ reused `mesh/surface.slangi`). Both backends `#include` the identical text. These helpers are already proven byte-identical for shadows.
- **The single shared hook:** each backend provides ONLY `nwbCausticTraceClosest(origin, dir, tMin, tMax) -> NwbCausticHit { hit, pos, normal, ior, transmission, t }`. HW fills it via `TraceRay`+closest-hit; SW fills it via the explicit-stack BVH descent. The shared bounce loop calls this hook and does ALL arithmetic.
- **Unavoidable divergence (already accepted):** HW uses fixed-function triangle intersection + `RayTCurrent()`; SW uses Möller-Trumbore + `triangleHit.t`. This is the same divergence the shadow trace accepts. A normalized ray direction guarantees the two `t` values are the same world distance.
- **Amplification risk + its fix:** a SECOND refraction amplifies any first-hit normal discrepancy between the intersectors (refraction is normal-sensitive). Fix: bend on the GEOMETRIC face normal (`cross(v1-v0,v2-v0)`) on BOTH backends, not the interpolated shading normal — the geometric normal is identical across intersectors. Cost: faceted bending on smooth meshes (acceptable; documented). This is why the HW position SRV array (2.2) is load-bearing.
- **Order-independence:** the shared sampler/loop uses a fixed iteration count, no atomics in the math, no wave intrinsics — so HW and SW don't drift in the low bits. (Atomics appear ONLY in the integer splat, which is order-independent by construction.)
- **Realistic parity bar:** converged-image A/B within Monte-Carlo tolerance, NOT per-frame bit-equality (stochastic jitter makes per-frame bit-equality meaningless). The gate is: accumulate N frames on each backend, compare means. This is the explicit Approach-1 relaxation anticipated in `.helper/caustics_plan.md:43`.
- **Non-uniform-scale normal divergence (now load-bearing):** the documented latent issue in `nwbSwShadowTransformNormal` / the HW normal transform didn't matter for transmittance-only shadows, but becomes REAL for refraction (a non-uniformly-scaled glass refracts wrong). Closed in P6 via the proper inverse-transpose normal transform on both backends.

---

## 4. Key risks + mitigations

1. **Photon budget wasted on empty space → no visible caustic (make-or-break).** Cause: loose emission domain. Mitigation: tight occluder-AABB-projected emission domain (2.1).
2. **Splat aliasing / sparse-splat sparkle.** Fundamental focus⟷noise tension at finite budget. Mitigation: the full stack — emission jitter + temporal accumulation + splat footprint + edge-aware blur. The splat-kernel/blur radius is the primary documented quality/perf knob; v1 is "smooth-but-slightly-soft OR sharp-but-slightly-noisy," never both perfectly.
3. **R32_UINT atomic overflow on an extremely focused caustic → wraparound → black/garbage pixel.** Mitigation: conservative `FIXED_POINT_SCALE` + clamp/saturating guard on the accumulated value.
4. **Atomic hot-spotting** — a focused caustic means many photons hitting the same few bright pixels (intrinsic to focusing). Mitigation: integer atomics are individually cheap; resolve+blur amortizes; per-light budget cap.
5. **Screen-space light leak** (caustic on a surface the eye can't see at that pixel). Mitigation: G-buffer depth-compare reject at splat time (2.3).
6. **Wrong refraction from a smoothed shading normal → photons fan out → washed-out blob, not a crescent.** Mitigation: bend on the geometric face normal (drives the HW position-array requirement; SW gets it free).
7. **Non-uniform-instance-scale normal divergence becomes real for refraction.** Mitigation: proper inverse-transpose normal transform on both backends (P6).
8. **TIR draining too much light (dark holes) or runaway bounces.** Mitigation: bounded MAX_BOUNCES + v1 drops TIR (documented); revisit reflection later.
9. **Energy non-conservation** (caustic too bright/dim, unstable with photon count). Mitigation: the `power/photonCount` invariant + a test that doubling photon count leaves the converged image unchanged (P3 gate).
10. **HW/SW parity drift through the second refraction** (top parity risk). Mitigation: geometric normal + deterministic order-independent accumulation + converged-image A/B gate (P4).

---

## 5. Unit-by-unit build breakdown (each leaves the engine green + validation-clean; HW/SW stay in lockstep)

- **P1 — Caustic-light classification + emission-target gathering (CPU only).**
  New `ResolveCausticLights`: rank directional/spot lights, write the caustic slot into `NwbSceneLight.params.w`; gather refractive-instance world AABBs (reuse `instanceAabbMin/Max`) into a per-caustic-light emission-target structured buffer. No consumer yet.
  **Gate:** unit-log the chosen caustic lights + their emission-target AABBs for a one-glass-object + one-directional-light scene — confirm exactly the glass AABB is targeted; a non-refractive scene yields zero caustic lights. No visual change. Engine builds clean; `nwb_assets_graphics_tests` green.

- **P2 — Shared refraction header + the additive caustic buffer plumbing (no producer yet).**
  Add `caustic/refract.slangi` (`nwbCausticRefract`, ported from `RefractV`, with a CPU-mirror unit test). Allocate the RGBA16F caustic irradiance buffer + R32_UINT accumulators + history next to `shadowVisibility`; clear-to-black each frame; add the deferred binding slot (=8), SRV bind, layout entry, `g_NwbCausticIrradiance` + `nwbBxdfCausticIrradiance` accessor; wire the additive pre-tonemap term into `nwbSceneApplyLighting` + the receiver material's BXDF.
  **Gate:** force the buffer to a tiny constant grey → the receiver brightens additively and uniformly (proves the additive path + binding + tonemap interaction); cleared to black → pixel-identical to today (proves no regression). `nwbCausticRefract` unit test matches `Vector3Refract` on a table of (incident, normal, eta) incl. a TIR case (zero on TIR).

- **P3 — SW photon producer (iterative closest-hit kernel) + R32_UINT splat + resolve.**
  New SW compute kernel (1D over photons): emit from P1's targets → iterative bounce loop (refract via P2 header, Fresnel/Beer-Lambert via `mesh/surface.slangi`) → nearest-opaque receiver hit (adapt `nwbSwShadow*` traversal to closest-hit/min-t) → project to pixel, depth-reject, 2×2 footprint `InterlockedAdd` into the accumulators. New resolve compute: fixed-point→float, area-normalize, write the irradiance buffer. Dispatch in the `system.cpp` SW-fallback branch before lighting.
  **Gate (the money shot):** forced-emulation run with a glass sphere + ground plane + directional light shows a BRIGHT FOCUSED caustic crescent on the ground (not uniform brightening); doubling the photon budget leaves converged brightness unchanged (energy conservation); a non-refractive scene shows zero caustic. Validation-clean.

- **P4 — HW RT photon producer (raygen iterative loop), byte-parallel to P3.**
  New pipeline `CausticPhotonRayGen`+`CausticClosestHit`+`CausticMiss`; caustic binding layout adds the `MESH_POSITIONS[N]` SRV array (closes the HW position gap) so the geometric normal is available; payload grown to ~48 B; `setMaxRecursionDepth(1)` kept (iterative, not recursive); shared P2 physics header → physics byte-identical to P3; same R32_UINT accumulators + same resolve pass. Dispatch in the `system.cpp` HW branch before lighting.
  **Gate:** HW run reproduces P3's crescent; converged-image A/B (HW vs forced-emulation SW via `tests/smoke/testbed_window_capture_smoke.py`) matches within Monte-Carlo tolerance (accumulate N frames each, compare means). Validation-clean both paths.

- **P5 — Emission jitter + temporal accumulation + edge-aware resolve blur (quality).**
  Add per-frame R2/Halton jitter to P1/P3/P4 emission; EMA history blend with camera reprojection + motion-reject; edge-aware (depth+normal-guided) à-trous/Gaussian blur in the resolve.
  **Gate:** the crescent is smooth (no grid banding, no sparkle) at the v1 photon budget; a static camera converges within ~30 frames; a moving glass object doesn't ghost (motion-reject works); the focused core stays sharp (blur is edge-aware, not a flat smear). Side-by-side vs P3/P4 raw shows noise gone with focus retained.

- **P6 — Correctness hardening + full-sweep refactor (standing mandate).**
  Bend on the geometric face normal on both backends (SW from positions, HW from the new position array); fix the inverse-transpose normal transform for non-uniform instance scale on both backends; bound MAX_BOUNCES + flux-epsilon early-out + atomic-overflow clamp; finalize caustic-light budget caps; audit virtual paths (`engine/caustic/...` not bare `caustic/...`, per the CSG-shape-path backlog).
  **Gate:** a non-uniformly-scaled glass refracts correctly (caustic shape matches the uniformly-scaled reference rescaled); a high-IOR/grazing scene shows TIR regions going dark without crashing or runaway cost; the max-caustic-lights × max-refractive-instances stress scene stays within frame budget with no atomic-overflow artifacts; full `nwb_assets_graphics_tests` green; both backends validation-clean; one final forced-emulation A/B capture archived.

---

## 6. Key files index (all relative to `a:\Workstation\NWBLot`)

- **New shaders:** `impl/assets/graphics/caustic/{refract.slangi, caustic_photon_raygen.slang, caustic_photon_chit.slang, caustic_photon_miss.slang, caustic_photon_sw_cs.slang, caustic_resolve_cs.slang, binding_slots.h, sw_binding_slots.h}`
- **Reused physics:** `impl/assets/graphics/mesh/surface.slangi` (`nwbShadowFresnelTransmittedFraction:83`, `nwbShadowAbsorptionCoefficient:92`)
- **Lighting integration:** `impl/assets/graphics/scene/lighting.slangi` (`nwbSceneApplyLighting:110-133` pre-tonemap add; add `g_NwbCausticIrradiance` + `nwbBxdfCausticIrradiance`), `impl/assets/graphics/deferred/binding_slots.h` (slot 8), `impl/assets/graphics/scene/buffer.slangi:48-57` (`NwbSceneLight.params.w` = caustic slot)
- **C++ producer + plumbing:** `impl/ecs_render/raytracing_system.cpp` (new pipeline/SW kernel/targets next to `createShadowVisibilityTarget:802` / `clearShadowVisibility:861` / `ensureShadowPipeline:1019` / `buildSceneTlas:352`), `impl/ecs_render/system.cpp` (dispatch before `renderDeferredLighting:349`), `impl/ecs_render/deferred_targets.cpp` (caustic SRV bind ~`:419`), `impl/ecs_render/deferred_lighting.cpp:62` (layout), `impl/ecs_render/renderer_scene_private.h` (new `ResolveCausticLights` next to `ResolveSceneLights:66`)
- **Math reference to port:** `global/math/vector.h` (`RefractV:2811`, `Vector3Refract:3043-3047`)
- **Backend (no change; capabilities confirmed):** `core/graphics/vulkan/raytracing.cpp:1099,1267` (recursion stays 1 — iterative loop); integer-atomics-only confirmed (no `VK_EXT_shader_atomic_float`) → forces the R32_UINT-packed splat
- **Parity harness:** `tests/smoke/testbed_window_capture_smoke.py`
- **Predecessor doc:** `.helper/caustics_plan.md` (Phase 0 done; producer decisions `:41-46`)

### Decisions this plan fixes (from `caustics_plan.md:41-46`)
Approach 1 photon-splat; budget = directional/spot 1-4 slots, point excluded v1; dispersion skipped v1; additive deposit in `scene/lighting.slangi` PRE-tonemap (not composite); R32_UINT-packed + resolve (forced — no float atomics); HW position SRV-array gap closed on the caustic layout (P4); non-uniform-scale normal divergence closed (P6, now load-bearing); iterative bounce loop, recursion stays 1 on HW (parity with SW); relaxed parity bar = converged-image A/B.
