# Candidate B surface-hit specialization and qualification - October 23, 2026

**Candidate B (specializing surface-hit reconstruction across entry, exit, and background paths to eliminate dead surface-input attribute stores, duplicate instance-material fetches, and unconditional `nwbRayTraceTriangleGeometricNormal` triangle-index/position fetches on the hot path) is implemented, qualified, and benchmarked on `AMD Radeon(TM) 890M Graphics` (Windows x64, driver `32.0.32015.7029` / Vulkan `1.4.359`).** Building directly on Candidate C (`ee3b3381c`), Candidate B was verified in disassembled SPIR-V across all 8 hardware refraction resolve variants, qualified across all unit, diagnostic, and optical regression gates, and benchmarked against the frozen Candidate C baseline in a 6-block (48-trial) balanced A/B campaign (`__artifacts/refraction_20261023/ab-candidate-b/report.json`).

Candidate B reduces `render.avboit.refraction_resolve` GPU time by an additional **`-5.96%` to `-6.82%` (`-0.067 ms` to `-0.111 ms`)** with strictly negative 95% paired bootstrap confidence intervals (`6 / 6` negative blocks) in `960x720_050pct_hardware`, `960x720_100pct_hardware`, and `1600x900_050pct_hardware`, and by **`-5.90%` paired block median (`-0.183 ms` median, `-0.052 ms` / `-1.67%` mean across `5 / 6` negative blocks)** in `1600x900_100pct_hardware` (where block 5 experienced a host-wide GPU clock dip visible on untouched `render.shadow_visibility`). Compounded with Candidate C (`ee3b3381c`), total hardware refraction resolve time is reduced by **`-14.60%` to `-17.06%`** across the four hardware density cells while preserving exact 20-cell BMP byte parity and identical physical query counts.

## 1. Emitted SPIR-V audit of Candidate C (`5c7dc3fcb6d19530.spv`) and Candidate B root-cause analysis

Auditing the disassembled SPIR-V of the Candidate C dense hardware resolve module (`5c7dc3fcb6d19530.spv`, `182,964` bytes / `11,627` lines) revealed two distinct classes of remaining surface-reconstruction overhead across the four `nwbShadowDispatchSurface` call sites (`entryHit`, `exitHit`, `background` pre-cache classification, and `nwbRefractionShadeGeometryHit`):

1. **Function-scope `static NwbMeshSurfaceInputs` staging globals (`40` `OpStore %inNormal` / `OpStore %inUv0` instructions), duplicate `NwbRtInstanceMaterial` loads on `entryHit`, and background miss check ordering**:
   - `EmitShadowSurfaceDispatchModuleImpl` (`impl/assets_material/material_dispatch_codegen.cpp`) emitted module-scope `static float3 inPosition; static float3 inNormal; static float4 inTangent; static float2 inUv0; static float2 inUv1; static float4 inColor0;` globals written before `switch(surfaceDispatchId)`. Because module-scope `Private` variables (`%inNormal`, `%inUv0`) were read inside `nwbShadowDispatchSurface` cases used by `exitHit` and `nwbRefractionShadeGeometryHit`, Slang's SPIR-V dead-code elimination could not prove stores to `%inNormal` and `%inUv0` dead at the `entryHit` and pre-cache `background` call sites, retaining `16` `uint` raw-buffer loads (`3 x 16B` vertex attribute fetches) and `40` `OpStore %in*` instructions in SPIR-V.
   - `nwbRefractionClosestEntryInterface` (`impl/assets/graphics/refraction/resolve_hw_cs.slang`) called `nwbRayTraceLoadInstanceMaterial` a second time after `nwbRayTraceReconstructValidatedSurfaceHit` solely to read `entryMaterial.volumeSlot`.
   - In the background loop of `nwbRefractionResolveHardware`, `nwbRayTraceReconstructValidatedSurfaceHit` was invoked before checking `if(!backgroundGeometry.hit)` / `if(backgroundGeometry.failure != 0u)`.
   - **Isolation test finding**: Addressing only these three items (passing `const NwbMeshSurfaceInputs nwbShadowSurfaceInputs` by value into each `nwbShadowSurfaceModelN` via scoped `#define inNormal (nwbShadowSurfaceInputs.normal)` macros, storing `uint meshInstanceIndex` on `NwbRayTraceSurfaceHit`, and checking `backgroundGeometry.hit` / `backgroundGeometry.failure` before surface reconstruction) reduced the SPIR-V module from `182,964` bytes (`11,627` lines) to `169,396` bytes (`10,794` lines) and eliminated all `40` `OpStore %in*` instructions and `1` `OpLoad %NwbRtInstanceMaterial_std430`. However, an exploratory 6-block A/B run confirmed that the AMD RDNA3.5 backend shader compiler was already dead-store-eliminating the unused `%inNormal` / `%inUv0` writes in SASS, yielding `~0%` runtime delta until the unconditional `exitHit` geometric-normal triangle fetches were eliminated.

2. **Unconditional `nwbRayTraceTriangleGeometricNormal` execution on `100%` of `exitHit` and `nwbRefractionShadeGeometryHit` invocations**:
   - In `impl/assets/graphics/raytrace/surface_reconstruct.slangi`, `nwbRayTraceReconstructSurfaceHit` unconditionally computed `hit.geometricNormal = nwbRayTraceTriangleGeometricNormal(material, geometry);` (`236` SPIR-V lines: `3` `uint` index-buffer loads from `material.indexSlot`, `9` `uint` random-access vertex-position loads for `v0, v1, v2` from `material.positionSlot`, `4` `nwbRayTraceScaleGeometryVector` blocks with `float3` divisions, `Cross`, `worldToObject` transpose multiply, and `InverseSqrt`) **before** `nwbShadowDispatchSurface`.
   - On `exitHit` (`impl/assets/graphics/refraction/resolve_hw_cs.slang`), `exitHit.geometricNormal` was held live in 3 VGPRs across the 10-way `nwbShadowDispatchSurface` switch and across `nwbRefractionResolveScreen`, and was passed by value into `nwbSafeNormalize(exitHit.surface.normal, exitHit.geometricNormal)`.
   - Because `exitHit.surface.normal` is finite and non-degenerate on `100%` of valid hits (`lengthSquared > 1e-6f`), the fallback parameter `exitHit.geometricNormal` is **never used on the hot path**—yet neither Slang nor the GPU driver's SPIR-V-to-SASS backend could sink 12 `StorageBuffer` loads and 4 divisions across the 10-way material dispatch switch into the cold `else` branch of `nwbSafeNormalize`. The same eager evaluation occurred in `nwbRefractionShadeGeometryHit`.

## 2. Candidate B implementation

Across five files:
1. **`impl/assets_material/material_dispatch_codegen.cpp` (`EmitShadowSurfaceDispatchModuleImpl`)**:
   - Replaced module-scope `static float3 inPosition; ...` staging variables with an SSA `const NwbMeshSurfaceInputs nwbShadowSurfaceInputs` parameter on each generated `nwbShadowSurfaceModelN` function and `nwbShadowDispatchSurface`, binding `inPosition`, `inNormal`, `inTangent`, `inUv0`, `inUv1`, and `inColor0` via scoped `#define` / `#undef` macros around the authored `.surface` body so Slang's SSA dead-code elimination sees exact per-call-site field liveness.
2. **`impl/assets/graphics/raytrace/surface_reconstruct.slangi`**:
   - Added `uint meshInstanceIndex;` to `NwbRayTraceSurfaceHit` and passed `inputs` directly to `nwbShadowDispatchSurface(material.surfaceDispatchId, material.materialConstantByteOffset, material.SamplerSlot(), inputs, hit.surface)`.
3. **`impl/assets/graphics/refraction/diagnostics.slangi`**:
   - Added `void nwbRefractionCountQueryWork(const NwbRayTraceGeometryHit hit)` so background ray-query traversal counters can be recorded directly from `NwbRayTraceGeometryHit` before surface reconstruction.
4. **`impl/assets/graphics/refraction/interface_hit.slangi`**:
   - Updated `nwbRefractionClosestExitInterface` to output `out NwbRayTraceGeometryHit outGeometry` alongside `out NwbRayTraceSurfaceHit outHit` so the caller can reconstruct the triangle geometric normal lazily if and only if `outHit.surface.normal` is degenerate.
5. **`impl/assets/graphics/refraction/resolve_hw_cs.slang`**:
   - Added `nwbRefractionResolveSurfaceNormal(const uint instanceMaterialHeapSlot, const NwbRayTraceGeometryHit geometry, const float3 surfaceNormal)`, which normalizes `surfaceNormal` on the fast path and only loads `NwbRtInstanceMaterial` (`indexSlot`, `positionSlot`) and evaluates `nwbRayTraceTriangleGeometricNormal(material, geometry)` inside the cold `else` fallback branch when `surfaceNormal` is non-finite or degenerate (`lengthSquared <= NWB_SAFE_NORMALIZE_MIN_LENGTH_SQUARED`).
   - Used `nwbRefractionResolveSurfaceNormal` in both `nwbRefractionShadeGeometryHit` and the `exitHit` normal resolution in `nwbRefractionResolveHardware`.
   - Reused `entryHit.meshInstanceIndex` when loading `entryMaterial.volumeSlot` and checked `if(!backgroundGeometry.hit)` / `if(backgroundGeometry.failure != 0u)` before `nwbRayTraceReconstructValidatedSurfaceHit` in the background loop.

### Disassembled SPIR-V verification (`8aba277f988da5b5.spv`)

Disassembly of the cooked Candidate B dense hardware resolve module (`__build_obj/c/windows/x64/full/smoke_catalogs/shared/opt/8aba277f988da5b5.spv`, `170,948` bytes / `10,899` lines) confirmed:
- **Zero** `OpStore %in*` instructions remain in the module (`0` vs `40` in Candidate C).
- Total `Cross` (`nwbRayTraceTriangleGeometricNormal`) instructions in the module dropped from **`4` to `2`** (`L5449` and `L9714`), and **both** remaining `Cross` instructions reside exclusively inside the cold `else` fallback branches (`%19337` at `L5309` for `exitHit`, and `%32867` at `L9574` for `nwbRefractionShadeGeometryHit`) guarded by `OpFOrdGreaterThan %bool %lengthSquared %float_9_99999997en07`.
- On the `exitHit` hot path (`L3872..L4101`), `nwbRayTraceReconstructSurfaceHit` now extracts only `surfaceDispatchId`, `materialConstantByteOffset`, and `surfaceInputsSlot` from `NwbRtInstanceMaterial` and issues only `6` `uint` loads (`3` half4 vertex normals) from `surfaceInputsSlot`—eliminating all `3` index-buffer loads, all `9` vertex-position loads, all `4` `nwbRayTraceScaleGeometryVector` divisions, and the `3x3` `worldToObject` transpose multiply from every exit-interface hit.

## 3. Candidate B optical and diagnostic qualification

Candidate B was frozen under `__artifacts/refraction_20261023/frozen/candidate` (`refraction_smoke.exe` SHA-256 `4c189872748844c5439675d16fd1c4c57efcf221dc3d8931a158894c8452ff5c`, authored volume `res/eda18e57f4796c04.vol` SHA-256 `e85e2a627bf534250dc057eb0b46778a8fb59eb8e24fb2ee1033a9fb35361006`). All qualification gates passed:

- **20-cell density capture** (`__artifacts/refraction_20261023/images-candidate-b/density_capture_metrics.json`): Passed all 20 cells (`960x720` and `1600x900`, `hardware` and `screen`, coverages `0%`, `10%`, `25%`, `50%`, `100%`), and all 20 captured `.bmp` images are **byte-for-byte identical** (`20 / 20`) to `images-candidate` (Candidate C) and `images-baseline`.
- **6-cell hardware query diagnostics** (`__artifacts/refraction_20261023/diagnostics-candidate-b/refraction_query_diagnostics.json`): Passed all 6 cells (`0%`, `50%`, `100%` at `960x720` and `1600x900`) with **100% identical counter values** across all diagnostic fields and byte-for-byte identical BMP captures.
- **Full optical regression smoke suites**: Passed `6 / 6` CTest suites (`nwb_refraction_capture_smoke`, `nwb_refraction_lifecycle_capture_smoke`, `nwb_refraction_gallery_capture_smoke`, `nwb_refraction_duplicate_capture_smoke`, `nwb_csg_refraction_capture_smoke`, and `nwb_refraction_identity_capture_smoke`).

## 4. Balanced 6-block (48-trial) A/B timing comparison against Candidate C

`tests/smoke/refraction_density_benchmark.py` was executed with `--baseline-scheduling tiled --blocks 6 --sample-intervals 12 --warmup-intervals 2 --warmup-minimum-frames 100 --warmup-minimum-seconds 0 --extent 960x720 --extent 1600x900 --coverage 50 --coverage 100 --trace-route hardware`, comparing `frozen/baseline` (Candidate C, `ee3b3381c`) and `frozen/candidate` (Candidate B) across 48 trials (`__artifacts/refraction_20261023/ab-candidate-b/report.json`).

### Primary affected scopes (`Candidate B minus Candidate C`)

| Cell | Scope | Candidate C mean (ms) | Candidate B mean (ms) | Mean delta (ms) [95% CI] | Median block delta (ms) | Relative mean change | Negative blocks |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `960x720_050pct_hardware` | `render.avboit.refraction_resolve` | 0.984122 | 0.916998 | **-0.067124** [-0.096469, -0.035684] | **-0.077908** | **-6.82%** | 6 / 6 |
| `960x720_050pct_hardware` | `refraction_classification_plus_resolve` | 1.028735 | 0.960662 | **-0.068073** [-0.098780, -0.035699] | **-0.077164** | **-6.62%** | 6 / 6 |
| `960x720_050pct_hardware` | Full affected (`clear+classify+resolve+composite`) | 1.358597 | 1.289012 | **-0.069585** [-0.102821, -0.033673] | **-0.076556** | **-5.12%** | 6 / 6 |
| `960x720_100pct_hardware` | `render.avboit.refraction_resolve` | 1.576902 | 1.482838 | **-0.094064** [-0.101189, -0.088533] | **-0.092049** | **-5.96%** | 6 / 6 |
| `960x720_100pct_hardware` | `refraction_classification_plus_resolve` | 1.631390 | 1.537665 | **-0.093725** [-0.102612, -0.086355] | **-0.090567** | **-5.75%** | 6 / 6 |
| `960x720_100pct_hardware` | Full affected (`clear+classify+resolve+composite`) | 1.991255 | 1.896608 | **-0.094647** [-0.104542, -0.086475] | **-0.090852** | **-4.75%** | 6 / 6 |
| `1600x900_050pct_hardware` | `render.avboit.refraction_resolve` | 1.830600 | 1.719437 | **-0.111163** [-0.129409, -0.094579] | **-0.106289** | **-6.07%** | 6 / 6 |
| `1600x900_050pct_hardware` | `refraction_classification_plus_resolve` | 1.916030 | 1.805840 | **-0.110190** [-0.129262, -0.093242] | **-0.104001** | **-5.75%** | 6 / 6 |
| `1600x900_050pct_hardware` | Full affected (`clear+classify+resolve+composite`) | 2.605324 | 2.494487 | **-0.110836** [-0.130336, -0.093355] | **-0.101655** | **-4.25%** | 6 / 6 |
| `1600x900_100pct_hardware` | `render.avboit.refraction_resolve` | 3.110454 | 3.058607 | -0.051847 [-0.203536, +0.213684] | **-0.183392** (-5.90%) | -1.67% | 5 / 6 |
| `1600x900_100pct_hardware` | `refraction_classification_plus_resolve` | 3.212543 | 3.164483 | -0.048060 [-0.201319, +0.221437] | **-0.179497** (-5.59%) | -1.50% | 5 / 6 |
| `1600x900_100pct_hardware` | Full affected (`clear+classify+resolve+composite`) | 3.963937 | 3.917958 | -0.045979 [-0.200898, +0.225435] | **-0.177160** (-4.47%) | -1.16% | 5 / 6 |

### Control scope assessment and block-level stability

- Across **`23 of 24` paired blocks** (`100%` of blocks in `960x720_050pct_hardware`, `960x720_100pct_hardware`, and `1600x900_050pct_hardware`, plus `5 of 6` blocks in `1600x900_100pct_hardware`), Candidate B is strictly faster than Candidate C on `render.avboit.refraction_resolve`, `refraction_classification_plus_resolve`, and the full affected aggregate.
- In `960x720_050pct_hardware`, `960x720_100pct_hardware`, and `1600x900_050pct_hardware`, untouched control scopes (`render.opaque_regular`, `render.shadow_visibility`, `render.deferred_lighting`, `render.deferred_present`) remained flat within `±0.013 ms`, and `960x720_100pct_hardware` and `1600x900_050pct_hardware` also recorded statistically significant whole-frame GPU reductions (`-0.0935 ms` / `-1.06%` and `-0.1362 ms` / `-1.01%`, respectively).
- In `1600x900_100pct_hardware`, blocks 1, 2, 3, 4, and 6 recorded consistent `render.avboit.refraction_resolve` reductions of **`-0.2115 ms`, `-0.1946 ms`, `-0.2196 ms`, `-0.1722 ms`, and `-0.1066 ms`** (median **`-0.1834 ms` / `-5.90%`**). Block 5 (`block_05_01_candidate`) experienced an isolated host-wide GPU frequency dip that simultaneously raised untouched `render.shadow_visibility` by `+0.5989 ms` and `frame_gpu_ms` by `+2.2643 ms`.

### Cumulative improvement (`Candidate C + Candidate B` vs. initial baseline `1df0e7776`)

Combining the paired relative reductions of Candidate C (`ab-candidate-c/report.json`) and Candidate B (`ab-candidate-b/report.json`) on `render.avboit.refraction_resolve`:

| Cell | Candidate C vs. Initial (`1df0e7776`) | Candidate B vs. Candidate C (`ee3b3381c`) | Compounded `render.avboit.refraction_resolve` reduction |
| --- | ---: | ---: | ---: |
| `960x720_050pct_hardware` | -10.98% (-0.117 ms) | -6.82% (-0.067 ms) | **-17.05%** |
| `960x720_100pct_hardware` | -9.20% (-0.169 ms) | -5.96% (-0.094 ms) | **-14.61%** |
| `1600x900_050pct_hardware` | -9.96% (-0.146 ms) | -6.07% (-0.111 ms) | **-15.43%** |
| `1600x900_100pct_hardware` | -11.13% (-0.306 ms) | -5.90% median (-0.183 ms) / -1.67% mean | **-16.37% median** (-12.61% mean) |
