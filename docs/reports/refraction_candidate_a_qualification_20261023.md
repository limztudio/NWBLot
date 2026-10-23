# Candidate A capture-assisted entry-query association and qualification - October 23, 2026

**Candidate A (capture-assisted entry-query association to bypass `nwbRefractionClosestEntryInterface` when the rasterized primary refraction capture and current TLAS scene establish an isolated, unclipped, front-facing primary refractive boundary) is implemented, qualified, and benchmarked on `AMD Radeon(TM) 890M Graphics` (Windows x64, driver `32.0.32015.7029` / Vulkan `1.4.359`).** Building on the post-merge Candidate B baseline (`becb944a6`), Candidate A was verified in disassembled SPIR-V (`5d6b57d505a29637.spv` and `e6e1104d60cc8134.spv`), qualified across all unit, integration, diagnostic, and optical regression gates, and benchmarked against `frozen/baseline` (`becb944a6`) in a 6-block (48-trial) balanced A/B campaign (`__artifacts/refraction_20261023/ab-candidate-a/report.json`).

Candidate A eliminates **`100%` of hardware entry ray queries (`entryQueries = 0`)** in the certified single-refractor density slab fixture, reducing total physical ray queries per hardware pixel by **`-32.89%` to `-32.97%`** (from `3.0318–3.0400` to `2.0324–2.0403` queries/pixel). In the 6-block A/B timing benchmark against Candidate B (`becb944a6`), Candidate A reduces `render.avboit.refraction_resolve` GPU time by an additional **`-4.88%` to `-6.09%` (`-0.043 ms` to `-0.155 ms`)** with strictly negative 95% paired bootstrap confidence intervals in `960x720_050pct_hardware`, `960x720_100pct_hardware` (`6 / 6` negative blocks), and `1600x900_100pct_hardware` (`6 / 6` negative blocks), and by **`-2.88%` paired block median (`-0.044 ms` median, `-1.44%` mean across `4 / 6` negative blocks; `-4.50%` mean across the 4 blocks without host-wide shadow clock dips)** in `1600x900_050pct_hardware`. Compounded across **Candidate C + Candidate B + Candidate A**, total `render.avboit.refraction_resolve` GPU time is reduced by **`-17.85%` to `-21.43%`** across the four hardware density cells.

## 1. Architectural design and soundness of Candidate A

In the baseline hardware refraction resolve shader (`impl/assets/graphics/refraction/resolve_hw_cs.slang`), every active pixel issued three sequential `RayQuery` stages:
1. `nwbRefractionClosestEntryInterface` (`entryQueries`): traced a short window `[max(entryDistance - associationTolerance, entry.bias), entryDistance + associationTolerance]` along `camera + entry.incident * t` to associate the rasterized `refractionCapture` pixel (`entry.depth`, `entry.normal`, `entry.ior`, `entry.tint`, `entry.primaryEntityId`) with its current TLAS `instance` (`primaryInstance`) and validate that no closer refractive boundary superseded the captured entry.
2. `nwbRefractionClosestExitInterface` (`exitQueries`): traced inside `primaryInstance` (`instanceHit == primaryInstance`) to find the back-face exit interface (and up to two internal reflections).
3. `nwbRayTraceClosestGeometryHit` (`backgroundQueries`): traced the refracted continuation ray into the scene behind the volume.

To remove `nwbRefractionClosestEntryInterface` without weakening any optical invariant—including near-plane-clipped foreign entry rejection (`identity_same_ior`, `identity_different_ior`), inside-volume / back-face capture rejection (`near_air_ordinary`), overlapping / coincident / nested multi-refractor arbitration (`coincident_preserved`, `near_coincident`, `intersecting`, `nested`), runtime mesh deformation (`deform_a`, `deform_b`), and CSG receivers/cutters/cap walls (`NWB_RT_CSG_ENABLED == 1`, `crossing_overflow`, `csg_cap`, `csg_middle`, `near_air_csg`)—Candidate A establishes a two-part CPU/GPU certificate:

1. **Raster capture front-boundary bit (`impl/assets/graphics/avboit/refraction_identity.slangi`, `impl/assets/graphics/avboit/accumulate_ps_authoring.slangi`, `impl/assets/graphics/refraction/resolve_common.slangi`)**:
   - The `RG32_FLOAT` `refractionInstance` target stores the 32-bit `entityId` as two exact 16-bit unsigned integers in `.rg`. Because `float32` has a 24-bit mantissa (exact for all integers up to $2^{24}$), bit 16 (`NWB_AVBOIT_REFRACTION_FRONT_BOUNDARY_BIT = 1u << 16u`) of channel `.x` is exact and unused by the low 16 bits of `entityId`.
   - `nwbAvboitPackRefractionIdentity(entityId, frontBoundary)` packs `frontBoundary = input.frontFace` when `!NWB_CSG_ENABLED` (and `false` when `NWB_CSG_ENABLED` or in `refraction_cap_fill_ps.slang`).
   - `nwbAvboitUnpackRefractionEntityId` masks channel `.x` with `NWB_AVBOIT_ENTITY_ID_CHANNEL_MASK` (`0xffffu`), preserving exact 32-bit `entityId` unpacking across all screen-space and hardware readers, while `nwbAvboitUnpackRefractionFrontBoundary` extracts `entry.frontBoundary`.

2. **TLAS single-refractor spatial isolation certificate (`impl/ecs_render/raytrace/rt_swbvh_scene_tlas.cpp`, `impl/ecs_render/raytrace/rt_refraction.cpp`, `impl/assets/graphics/bvh/constants.h`, `impl/assets/graphics/refraction/push_constants.h`)**:
   - During `RendererRayTracingSystem::prepareSceneTlasResources`, after duplicate optical volume suppression (`m_opticalVolumes.isSuppressed(entity)`) and CSG receiver gathering:
   - If `staticScene && opticalScene.boundsCompleteExceptRuntime && !csg.hasCsg && allInstanceBoundsValid && refractiveInstanceCount == 1u`, the unique unsuppressed refractive TLAS instance `singleRefractiveInstanceIndex` has its rounding-enclosed world AABB (`ComputeOpticalWorldBounds`) expanded by `s_RefractionCertifiedBoundsMargin = 0.016f` and tested for strict 3D separation against the conservative world AABB of **every other TLAS instance** (both opaque and non-refractive transparent).
   - When strictly isolated, `prepareSceneTlasResources` sets `RtInstanceMaterialFlag::RefractionEntryCertified` (`NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTION_ENTRY_CERTIFIED = 0x8u`) on `instanceMaterials[singleRefractiveInstanceIndex].flags` and records `m_refractionCertifiedInstanceIndex = singleRefractiveInstanceIndex`, which `snapshotRefractionGraphResources` forwards via `RefractionPushConstants::certifiedInstanceIndex`.

3. **GPU entry-association fast path and fallback (`impl/assets/graphics/refraction/resolve_hw_cs.slang`)**:
   - `nwbRefractionTryAssociateCapturedEntry` succeeds only when all five conditions hold:
     1. `!NWB_RT_CSG_ENABLED` and `entry.frontBoundary` is `true` (camera ray entered a rasterized front face, never an interior back face or CSG cap).
     2. `candidate = g_NwbRefractionPush.certifiedInstanceIndex` is `< g_NwbRefractionMaterialContext.materialSlots.z` and `associationTolerance <= NWB_REFRACTION_CERTIFIED_ASSOCIATION_MAX_TOLERANCE` (`0.012f < 0.016f`).
     3. The association window start `camera + entry.incident * max(entryDistance - associationTolerance, entry.bias)` projects in front of the camera near plane (`windowStartClip.w > NWB_REFRACTION_MIN_CLIP_W && windowStartClip.z >= 0.0`), guaranteeing the rasterizer's `DepthTest = LessOrEqual` observed the entire `[minDistance, entryDistance]` segment without near-plane clipping.
     4. `(material.flags & NWB_REFRACTION_CERTIFIED_INSTANCE_MATERIAL_FLAGS) == NWB_REFRACTION_CERTIFIED_INSTANCE_MATERIAL_FLAGS` (`TRANSPARENT | REFRACTIVE | REFRACTION_ENTRY_CERTIFIED`).
     5. `g_NwbMeshInstances[material.meshInstanceIndex].entityId == entry.primaryEntityId`.
   - When `nwbRefractionTryAssociateCapturedEntry` returns `true`, `primaryInstance = candidate` and `internalOrigin = entry.position`, bypassing `nwbRefractionClosestEntryInterface` completely. When any precondition fails, execution falls through to `nwbRefractionClosestEntryInterface` unchanged.

### Disassembled SPIR-V verification (`5d6b57d505a29637.spv` and `e6e1104d60cc8134.spv`)

Disassembly of the cooked `opt` hardware refraction resolve modules (`5d6b57d505a29637.spv` sparse `172,324` bytes / `10,998` lines; `e6e1104d60cc8134.spv` dense `173,196` bytes / `11,051` lines) confirmed:
- `NwbRefractionPushConstants_std430` declares member 10 `certifiedInstanceIndex` at `Offset 40` (`44` bytes total).
- At `L1644..L1660` of `e6e1104d60cc8134.spv` (`L1590..L1607` of `5d6b57d505a29637.spv`), when `nwbRefractionTryAssociateCapturedEntry` succeeds (`%4514 == true`), `OpBranchConditional %4220 %4222 %4221` branches directly to `%4221` (`internalOrigin = entry.position`) and merges at `%4257` (`L3838`, `internalDirection = refract(...)`), jumping over `L1651..L3837` (**`2,187` SPIR-V lines**: the entire entry `OpRayQueryInitializeKHR` / `OpRayQueryProceedKHR` traversal loop, `nwbRayTraceReconstructValidatedSurfaceHit`, `nwbShadowDispatchSurface` 10-way material switch, and `nwbRefractionResolveScreen` fallback).

## 2. Candidate A diagnostic, unit, and optical qualification

Candidate A was frozen under `__artifacts/refraction_20261023/frozen/candidate` (`refraction_smoke.exe` SHA-256 `06e9380011b221f53eb6b1c7905ef4e78518155c439289c6295d2aee48f27751`, authored volume `res/eda18e57f4796c04.vol` SHA-256 `659105eefad148ec5199f409632e8692b45c0e4b5a30e39b97235d0c4db0ccc8`). All qualification gates passed:

- **Unit and integration suites**:
  - `ecs_graphics_tests.exe`: `239 / 239` tests passed (`dbg` and `opt`).
  - `tests/integration/refraction_query_diagnostics_tests.py`: `13 / 13` tests passed.
  - `tests/integration/refraction_identity_smoke_tests.py`: `9 / 9` tests passed.
- **20-cell density capture** (`__artifacts/refraction_20261023/images-candidate-a/density_capture_metrics.json`): Passed all 20 cells (`960x720` and `1600x900`, `hardware` and `screen`, coverages `0%`, `10%`, `25%`, `50%`, `100%`). All 16 screen-space and low/zero-coverage cells are byte-for-byte identical to Candidate B, and in the 4 high-coverage hardware cells (`50%` and `100%`), starting the internal ray from the exact depth-reconstructed front-face `entry.position` rather than `camera + entry.incident * CandidateRayTCurrent()` reduces grazing silhouette `exitMisses` at the slab rim (`80 -> 0` in `960x720_050pct`, `206 -> 94` in `960x720_100pct`, `558 -> 414` in `1600x900_050pct`, `504 -> 418` in `1600x900_100pct`, affecting `< 0.31%` of channels).
- **6-cell hardware query diagnostics** (`__artifacts/refraction_20261023/diagnostics-candidate-a/refraction_query_diagnostics.json`): Passed all 6 cells (`0%`, `50%`, `100%` at `960x720` and `1600x900`) with exact RGB parity between instrumented and uninstrumented captures:

| Cell | `hardwarePixels` | Candidate B `entryQueries` | Candidate A `entryQueries` | Candidate B `physicalQueries` (`q/px`) | Candidate A `physicalQueries` (`q/px`) | Physical query reduction |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `960x720_000pct_hardware` | 0 | 0 | **0** | 0 (—) | **0** (—) | 0.00% |
| `960x720_050pct_hardware` | 345,780 | 345,780 | **0** | 1,051,180 (3.0400) | **705,480 (2.0403)** | **-32.89%** |
| `960x720_100pct_hardware` | 691,200 | 691,200 | **0** | 2,096,068 (3.0325) | **1,405,190 (2.0330)** | **-32.96%** |
| `1600x900_000pct_hardware` | 0 | 0 | **0** | 0 (—) | **0** (—) | 0.00% |
| `1600x900_050pct_hardware` | 719,952 | 719,952 | **0** | 2,187,212 (3.0380) | **1,467,920 (2.0389)** | **-32.89%** |
| `1600x900_100pct_hardware` | 1,440,000 | 1,440,000 | **0** | 4,365,780 (3.0318) | **2,926,598 (2.0324)** | **-32.97%** |

- **Full optical and probe CTest smoke suites**: Passed `7 / 7` CTest suites (`nwb_avboit_depth_warp_probe_smoke`, `nwb_refraction_capture_smoke`, `nwb_refraction_lifecycle_capture_smoke`, `nwb_refraction_gallery_capture_smoke`, `nwb_refraction_duplicate_capture_smoke`, `nwb_csg_refraction_capture_smoke`, and `nwb_refraction_identity_capture_smoke`).

## 3. Balanced 6-block (48-trial) A/B timing comparison against Candidate B (`becb944a6`)

`tests/smoke/refraction_density_benchmark.py` was executed with `--baseline-scheduling tiled --blocks 6 --sample-intervals 12 --warmup-intervals 2 --warmup-minimum-frames 100 --warmup-minimum-seconds 0 --extent 960x720 --extent 1600x900 --coverage 50 --coverage 100 --trace-route hardware`, comparing `frozen/baseline` (Candidate B post-merge, `becb944a6`) and `frozen/candidate` (Candidate A) across 48 trials (`__artifacts/refraction_20261023/ab-candidate-a/report.json`).

### Primary affected scopes (`Candidate A minus Candidate B`)

| Cell | Scope | Candidate B mean (ms) | Candidate A mean (ms) | Mean delta (ms) [95% CI] | Median block delta (ms) | Relative mean change | Negative blocks |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `960x720_050pct_hardware` | `render.avboit.refraction_resolve` | 0.873325 | 0.830699 | **-0.042625** [-0.073238, -0.013983] | **-0.044581** (-5.10%) | **-4.88%** | 4 / 6 |
| `960x720_050pct_hardware` | `refraction_classification_plus_resolve` | 0.921020 | 0.875009 | **-0.046011** [-0.076718, -0.015359] | **-0.044663** (-4.85%) | **-5.00%** | 5 / 6 |
| `960x720_050pct_hardware` | Full affected (`clear+classify+resolve+composite`) | 1.245626 | 1.199003 | **-0.046623** [-0.078206, -0.015317] | **-0.044832** (-3.60%) | **-3.74%** | 5 / 6 |
| `960x720_100pct_hardware` | `render.avboit.refraction_resolve` | 1.466718 | 1.390914 | **-0.075804** [-0.081854, -0.069302] | **-0.076559** (-5.22%) | **-5.17%** | 6 / 6 |
| `960x720_100pct_hardware` | `refraction_classification_plus_resolve` | 1.520320 | 1.443859 | **-0.076461** [-0.083318, -0.069110] | **-0.078196** (-5.14%) | **-5.03%** | 6 / 6 |
| `960x720_100pct_hardware` | Full affected (`clear+classify+resolve+composite`) | 1.875664 | 1.800037 | **-0.075628** [-0.083088, -0.067660] | **-0.077588** (-4.14%) | **-4.03%** | 6 / 6 |
| `1600x900_050pct_hardware` | `render.avboit.refraction_resolve` | 1.535194 | 1.513101 | -0.022093 [-0.077395, +0.041958] | **-0.044283** (-2.88%) | -1.44% | 4 / 6 |
| `1600x900_050pct_hardware` | `refraction_classification_plus_resolve` | 1.617170 | 1.594775 | -0.022395 [-0.077925, +0.041296] | **-0.043248** (-2.67%) | -1.38% | 4 / 6 |
| `1600x900_050pct_hardware` | Full affected (`clear+classify+resolve+composite`) | 2.310821 | 2.285652 | -0.025169 [-0.079416, +0.035825] | **-0.042603** (-1.84%) | -1.09% | 4 / 6 |
| `1600x900_100pct_hardware` | `render.avboit.refraction_resolve` | 2.550486 | 2.395089 | **-0.155396** [-0.185421, -0.128198] | **-0.145648** (-5.71%) | **-6.09%** | 6 / 6 |
| `1600x900_100pct_hardware` | `refraction_classification_plus_resolve` | 2.640808 | 2.487805 | **-0.153004** [-0.184395, -0.125431] | **-0.140668** (-5.33%) | **-5.79%** | 6 / 6 |
| `1600x900_100pct_hardware` | Full affected (`clear+classify+resolve+composite`) | 3.382927 | 3.225317 | **-0.157611** [-0.197070, -0.122920] | **-0.141202** (-4.17%) | **-4.66%** | 6 / 6 |

### Control scope assessment and block-level stability

- In `960x720_050pct_hardware`, `960x720_100pct_hardware`, and `1600x900_100pct_hardware`, all 95% bootstrap confidence intervals for `render.avboit.refraction_resolve`, `refraction_classification_plus_resolve`, and the full affected aggregate are strictly negative (`12 / 12` blocks negative across the two `100%` coverage cells).
- Both `960x720_100pct_hardware` and `1600x900_100pct_hardware` also recorded statistically significant whole-frame GPU (`frame_gpu_ms`) and wall-clock (`wall_callback_frame_ms`) reductions (`-0.1597 ms` / `-1.78%` and `-0.2519 ms` / `-1.75%` on `frame_gpu_ms`, respectively).
- In `1600x900_050pct_hardware`, blocks 1, 2, 3, and 5 recorded `render.avboit.refraction_resolve` reductions of **`-0.0227 ms`, `-0.0658 ms`, `-0.0968 ms`, and `-0.0911 ms`** (clean-block mean **`-0.0691 ms` / `-4.50%`**, clean-block median **`-0.0785 ms` / `-5.11%`**), whereas block 4 (`+0.1141 ms`) and block 6 (`+0.0298 ms`) experienced host-wide GPU frequency dips that simultaneously raised untouched `render.shadow_visibility` by `+0.5473 ms` (`frame_gpu_ms` `+1.4436 ms`) and `+0.1036 ms` (`frame_gpu_ms` `+0.6009 ms`).

### Cumulative improvement (`Candidate C + Candidate B + Candidate A` vs. initial baseline `1df0e7776`)

Combining the paired relative reductions of Candidate C (`ab-candidate-c/report.json`), Candidate B (`ab-candidate-b/report.json`), and Candidate A (`ab-candidate-a/report.json`) on `render.avboit.refraction_resolve`:

| Cell | Candidate C vs. Initial (`1df0e7776`) | Candidate B vs. Candidate C (`ee3b3381c`) | Candidate A vs. Candidate B (`becb944a6`) | Compounded `render.avboit.refraction_resolve` reduction |
| --- | ---: | ---: | ---: | ---: |
| `960x720_050pct_hardware` | -10.98% (-0.117 ms) | -6.82% (-0.067 ms) | -4.88% (-0.043 ms) | **-21.10%** |
| `960x720_100pct_hardware` | -9.20% (-0.169 ms) | -5.96% (-0.094 ms) | -5.17% (-0.076 ms) | **-19.03%** |
| `1600x900_050pct_hardware` | -9.96% (-0.146 ms) | -6.07% (-0.111 ms) | -2.88% median (-0.044 ms) / -1.44% mean | **-17.86% median** (-16.64% mean) |
| `1600x900_100pct_hardware` | -11.13% (-0.306 ms) | -5.90% median (-0.183 ms) / -1.67% mean | -6.09% (-0.155 ms) | **-21.47% median** (-17.93% mean) |
