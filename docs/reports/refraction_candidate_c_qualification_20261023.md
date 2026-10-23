# Hardware refraction re-measurement and Candidate C qualification - October 23, 2026

**Host re-measurement and Candidate C (deferred background shading reconstruction) qualification are complete on `AMD Radeon(TM) 890M Graphics` (Windows x64, driver `32.0.32015.7029` / Vulkan `1.4.359`).** Following the merge of `main` (`c3ea0238d`) into `perf/refraction-query-profiling`, the baseline renderer (`1df0e7776`) was re-qualified and profiled across the full 32-trial uninstrumented Williams schedule, emitted SPIR-V for `impl/assets/graphics/refraction/resolve_hw_cs.slang` was audited across entry, exit, and background hit paths, and Candidate C was implemented, qualified across all optical and diagnostic gates, and benchmarked in a 6-block (48-trial) balanced A/B campaign (`__artifacts/refraction_20261023/`).

Candidate C reduces `render.avboit.refraction_resolve` GPU time in **all 24 of 24 paired blocks** across the four hardware density cells (`-9.20%` to `-11.13%`, or `-0.117 ms` to `-0.306 ms`), and reduces the full affected refraction aggregate (`clear + classify + resolve + composite`) in **all 24 of 24 paired blocks** (`-6.64%` to `-8.69%`), while preserving exact 20-cell BMP byte parity and identical physical query counts.

## 1. High-DPI window capture and standby-monitor hotplug stabilization

On this Windows 11 host (150% primary display scaling with a secondary standby display that periodically triggers `WM_DISPLAYCHANGE` / `WM_DPICHANGED`), two platform harness issues were resolved before freezing the baseline arm:

1. **Per-Monitor DPI Awareness in Win32 Capture Backend** (`tests/smoke/window_capture_win32.py`, `tests/integration/window_capture_resize_tests.py`):
   - Enabled `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2` (`SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))`) in `WindowsCapture.__init__` so `GetClientRect`, `AdjustWindowRectExForDpi`, and `SetWindowPos` operate in physical pixels matching the Vulkan swapchain extent rather than 150% DWM-virtualized coordinates.
2. **Physical Client-Extent Preservation and Unfocused Minimization Guard** (`core/frame/win32.cpp`):
   - Updated `FrameDetail::WinProc` (`WM_DPICHANGED` and `WM_DISPLAYCHANGE`) to recompute the outer window rectangle from the authoritative physical client extent (`frameData.width() x frameData.height()`) via `AdjustWindowRectExForDpi` and place the window within the primary monitor work area (`MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY)`), preventing OS suggested-rect scaling from shrinking `1600x900` windows to `1063x591`.
   - Added `WM_SYSCOMMAND` (`SC_MINIMIZE`) suppression and `Frame::mainLoop` `IsIconic` restoration (`ShowWindow(hwnd, SW_RESTORE)`) when `frame->graphics().shouldRenderUnfocused()` is enabled, preventing standby-monitor hotplug disconnects from suspending Vulkan swapchain acquisition during unattended multi-trial campaigns.

## 2. Baseline qualification and 32-trial uninstrumented profile on AMD Radeon 890M

The merged baseline was frozen under `__artifacts/refraction_20261023/frozen/baseline` (`refraction_smoke.exe` SHA-256 `4c189872748844c5439675d16fd1c4c57efcf221dc3d8931a158894c8452ff5c`, authored volume `res/eda18e57f4796c04.vol` SHA-256 `d1b2cf474ab451fd7fc7d65dc5e74f97654813537cb82cb1a159f72765bb5067`).

- **20-cell uninstrumented density capture** (`__artifacts/refraction_20261023/images-baseline/density_capture_metrics.json`): Passed all 20 cells (`960x720` and `1600x900`, `hardware` and `screen`, coverages `0%`, `10%`, `25%`, `50%`, `100%`).
- **6-cell hardware query diagnostics** (`__artifacts/refraction_20261023/diagnostics-baseline/refraction_query_diagnostics.json`): Passed all 6 cells (`0%`, `50%`, `100%` at `960x720` and `1600x900`) with exact RGB parity across all `6,393,600` compared pixels and identical completed query counts (`1,051,176` at `960x720` 50%, `2,096,100` at `960x720` 100%, `2,187,356` at `1600x900` 50%, `4,365,778` at `1600x900` 100%; `3.032` to `3.040` queries per hardware pixel).
- **32-trial uninstrumented hardware query timing profile** (`__artifacts/refraction_20261023/profile-baseline/report.json`): Completed all 8 Williams blocks (32 trials, 30s warmup per trial) with `acquisition_passed: true`, `telemetry_eligible: true`, and `wall_eligible: true`:

| Extent | Coverage | Full affected ms [95% CI] | Resolve ms [95% CI] | Whole GPU frame ms [95% CI] | Resolve ms / million admitted pixels |
| --- | ---: | ---: | ---: | ---: | ---: |
| 960x720 | 50% | 1.178738 [1.044577, 1.430720] | 0.814338 [0.713640, 1.006897] | 6.967229 [6.238922, 8.210896] | 2.355075 |
| 960x720 | 100% | 1.920454 [1.720944, 2.144925] | 1.512953 [1.343832, 1.705804] | 8.552651 [7.638398, 9.575469] | 2.188502 |
| 1600x900 | 50% | 2.553512 [2.256418, 2.838820] | 1.786416 [1.553121, 2.007418] | 13.028095 [11.653151, 14.374666] | 2.481299 |
| 1600x900 | 100% | 4.199316 [3.776478, 4.663413] | 3.357379 [3.003604, 3.748421] | 15.893300 [14.385305, 17.530067] | 2.331513 |

## 3. Emitted SPIR-V audit of `resolve_hw_cs.slang` and Candidate C design

Disassembly of the cooked baseline dense hardware resolve module (`__build_obj/c/windows/x64/full/smoke_catalogs/shared/opt/aac63649b1eba4cd.spv`, `10,744` lines) established why Slang already eliminates unused surface reconstruction work on entry and exit hits (Candidate B), and why it could not do so on background hits (Candidate C):

1. **Entry interface hit (`L1490..L2215`)**: Because the caller only reads `entryHit.surface.refractionIor`, Slang's dead-code elimination (DCE) after inlining `nwbRefractionClosestEntryInterface` completely removes `nwbRayTraceTriangleGeometricNormal` (index buffer loads, vertex position buffer loads, edge cross product, and `worldToObject` transform) and strips all `baseColor`, `normal`, and `renderCoverage` computations (including `OpImageSampleExplicitLod`) from all 10 cases of `nwbShadowDispatchSurface` (`359` lines in the switch).
2. **Exit interface hit (`L4077..L5073`)**: Because the caller only reads `exitHit.surface.normal` and `exitHit.geometricNormal`, Slang's DCE strips all `baseColor`, `refractionIor`, `renderCoverage`, and texture sampling (`OpImageSampleExplicitLod`) from all 10 cases of `nwbShadowDispatchSurface` (`395` lines in the switch).
3. **Baseline background hit (`L6052..L8743`, `2,685` lines before `nwbRefractionCachedHit` at `L8911`)**: Because a single `const NwbRayTraceSurfaceHit background = nwbRayTraceClosestSurfaceHit(...)` value was consumed by pre-cache classification (`background.surface.refractionIor`, `background.surface.renderCoverage`), `nwbRefractionCachedHit(background, ...)`, and post-cache-miss / transparent-layer shading (`nwbRefractionShadeHit(background, outgoingDirection)`), Slang was forced to emit `nwbRayTraceTriangleGeometricNormal` (`L6148..L6384`, including index/position buffer fetches and `Cross` at `L6278`) and full `baseColor` + `normal` surface evaluation across all 10 material cases (`L6628..L8743`, including `OpImageSampleExplicitLod` at `L6910`) **before** `nwbRefractionCachedHit` was evaluated—even though 100% of background hits in the opaque backdrop fixture return from the screen lighting cache.

### Candidate C implementation (`impl/assets/graphics/refraction/resolve_hw_cs.slang`)

In `nwbRefractionResolveHardware`:
- The background loop traces `const NwbRayTraceGeometryHit backgroundGeometry = nwbRayTraceClosestGeometryHit(origin, outgoingDirection, entry.bias, NWB_REFRACTION_MAX_DISTANCE);` and reconstructs `const NwbRayTraceSurfaceHit background = nwbRayTraceReconstructSurfaceHit(g_NwbRefractionMaterialContext.sceneSlots.z, origin, outgoingDirection, backgroundGeometry);` solely for query-work accounting, classification (`refractionIor`, `renderCoverage`), and `nwbRefractionCachedHit`.
- When `coverage >= half(NWB_REFRACTION_OPAQUE_COVERAGE_MIN)` and `nwbRefractionCachedHit(background, entry, layer == 0u, radiance)` succeeds, the shader increments `NWB_REFRACTION_COUNTER_cachedOpaqueReturns` and returns immediately.
- Only when an opaque hit misses the screen cache or when `coverage < half(NWB_REFRACTION_OPAQUE_COVERAGE_MIN)` (transparent layer) does execution reach the single unified `nwbRefractionShadeGeometryHit(origin, outgoingDirection, backgroundGeometry)` call site, which reconstructs the shading surface and evaluates `nwbRefractionShadeHit`.

In the resulting cooked dense SPIR-V module (`__build_obj/c/windows/x64/full/smoke_catalogs/shared/opt/5c7dc3fcb6d19530.spv`, validated with `spirv-val` across all 8 hardware variants):
- The pre-cache background path (`L6062..L7825`) shrinks by **`1,096` lines of SPIR-V** (`1,763` lines vs `2,859` lines in baseline).
- **Zero** `Cross` (`nwbRayTraceTriangleGeometricNormal`) and **zero** `OpImageSampleExplicitLod` instructions remain before `nwbRefractionCachedHit` (`L7825`); both `nwbRayTraceTriangleGeometricNormal` (`L8292`) and `OpImageSampleExplicitLod` (`L8924`) are sunk into the single post-cache-check `nwbRefractionShadeGeometryHit` block (`L8107..L11050`).

## 4. Candidate C optical and diagnostic qualification

Candidate C was frozen under `__artifacts/refraction_20261023/frozen/candidate` (authored volume `res/eda18e57f4796c04.vol` SHA-256 `b6df5fb58d6f1847f69a8d8e61e262e11420c4fd7be5915b481b448cf2667a87`). All qualification gates passed:

- **Unit and native probe suites**: `11/11` CTest suites (`nwb_ecs_graphics_tests`, 9 refraction analysis unit suites, and `nwb_avboit_depth_warp_probe_smoke`) plus `13/13` `window_capture_resize_tests`.
- **20-cell density capture** (`__artifacts/refraction_20261023/images-candidate/density_capture_metrics.json`): Passed all 20 cells, and all 20 captured `.bmp` images are **byte-for-byte identical** to `images-baseline`.
- **6-cell hardware query diagnostics** (`__artifacts/refraction_20261023/diagnostics-candidate/refraction_query_diagnostics.json`): Passed all 6 cells with exact RGB parity across all `6,393,600` pixels against `images-baseline` and identical counter values across all 29 diagnostic fields.
- **Full optical regression smoke suites**: Passed `6/6` CTest suites (`nwb_refraction_capture_smoke`, `nwb_refraction_lifecycle_capture_smoke`, `nwb_refraction_gallery_capture_smoke`, `nwb_refraction_duplicate_capture_smoke`, `nwb_csg_refraction_capture_smoke`, and `nwb_refraction_identity_capture_smoke`) with GPU validation enabled.

## 5. Balanced 6-block (48-trial) A/B timing comparison

`tests/smoke/refraction_density_benchmark.py` was executed with `--baseline-scheduling tiled --blocks 6 --extent 960x720 --extent 1600x900 --coverage 50 --coverage 100 --trace-route hardware`, comparing `frozen/baseline` and `frozen/candidate` across 48 trials (`__artifacts/refraction_20261023/ab-candidate-c/report.json`).

### Primary affected scopes (`candidate minus baseline`)

| Cell | Scope | Baseline mean (ms) | Candidate C mean (ms) | Mean delta (ms) [95% CI] | Relative change | Negative blocks |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `960x720_050pct_hardware` | `render.avboit.refraction_resolve` | 1.068585 | 0.951234 | **-0.117350** [-0.145597, -0.088761] | **-10.98%** | 6 / 6 |
| `960x720_050pct_hardware` | Full affected (`clear+classify+resolve+composite`) | 1.485605 | 1.365314 | **-0.120291** [-0.151941, -0.091494] | **-8.10%** | 6 / 6 |
| `960x720_050pct_hardware` | `render.frame` | 8.336116 | 8.158045 | **-0.178071** [-0.316862, -0.068782] | **-2.14%** | 6 / 6 |
| `960x720_100pct_hardware` | `render.avboit.refraction_resolve` | 1.839411 | 1.670265 | **-0.169146** [-0.191268, -0.149566] | **-9.20%** | 6 / 6 |
| `960x720_100pct_hardware` | Full affected (`clear+classify+resolve+composite`) | 2.302628 | 2.134750 | **-0.167878** [-0.192837, -0.144544] | **-7.29%** | 6 / 6 |
| `960x720_100pct_hardware` | `render.frame` | 9.946327 | 9.848410 | -0.097917 [-0.271775, +0.073921] | -0.98% | 3 / 6 |
| `1600x900_050pct_hardware` | `render.avboit.refraction_resolve` | 1.463020 | 1.317337 | **-0.145683** [-0.159584, -0.130421] | **-9.96%** | 6 / 6 |
| `1600x900_050pct_hardware` | Full affected (`clear+classify+resolve+composite`) | 2.181915 | 2.037067 | **-0.144848** [-0.158407, -0.128278] | **-6.64%** | 6 / 6 |
| `1600x900_050pct_hardware` | `render.frame` | 10.899067 | 10.756459 | **-0.142608** [-0.190130, -0.075975] | **-1.31%** | 5 / 6 |
| `1600x900_100pct_hardware` | `render.avboit.refraction_resolve` | 2.749422 | 2.443525 | **-0.305897** [-0.313381, -0.294876] | **-11.13%** | 6 / 6 |
| `1600x900_100pct_hardware` | Full affected (`clear+classify+resolve+composite`) | 3.542691 | 3.234966 | **-0.307726** [-0.318450, -0.293154] | **-8.69%** | 6 / 6 |
| `1600x900_100pct_hardware` | `render.frame` | 13.492876 | 13.070048 | **-0.422828** [-0.481142, -0.364729] | **-3.13%** | 6 / 6 |

### Control scope assessment

- In `960x720_050pct_hardware` and `1600x900_050pct_hardware`, all four control scopes (`render.opaque_regular`, `render.shadow_visibility`, `render.deferred_lighting`, `render.deferred_present`) have 95% paired bootstrap intervals spanning zero with mean deltas within `±0.012 ms`, isolating the `render.avboit.refraction_resolve` and full-affected reductions cleanly.
- In `960x720_100pct_hardware`, block 5 of `candidate` (`block_05_01_candidate`) experienced a transient standby-display hotplug event (`maximum_retained_interval_seconds = 2.326 s`), which rejected `wall_callback_frame_ms` for that single trial and raised `render.shadow_visibility` in block 5 by `+0.387 ms`. Even in that unfavorable block, `render.avboit.refraction_resolve` was `-0.143 ms` faster on `candidate` than `baseline`.
- In `1600x900_100pct_hardware`, all six paired blocks were wall-eligible and clean; `render.avboit.refraction_resolve` decreased by `-0.305897 ms` (`-11.13%`, narrow block range `-0.279 ms` to `-0.315 ms`) and the full affected aggregate decreased by `-0.307726 ms` (`-8.69%`), while `render.shadow_visibility` also shifted by `-0.095739 ms` (`-2.18%`), accounting for the difference between the `-0.308 ms` affected-scope reduction and the `-0.423 ms` whole-frame reduction.
