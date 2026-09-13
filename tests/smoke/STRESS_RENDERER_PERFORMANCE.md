# Stress renderer performance

This work starts from `8326717e5` on the Qualcomm Adreno X2-90, Windows ARM64, Opt, at 1280 x 900. The user requested progress toward 60 FPS and a commit and push to `main` after each completed step. Source changes follow `.helper/standard.md`; unnamed namespaces are prohibited.

## Baseline correction

The initial screenshot's 10.7 FPS label was incorrect. `render.frame` timing queries can run out of reserved slots while rendering continues. In the corresponding 48 focused reporting intervals, the application recorded 402 updates over 25.247864 seconds (15.9221 FPS), but only 268 GPU timing samples. Those samples average 60.361 ms for the GPU frame. Neither the sample publication rate nor the reciprocal of a GPU scope is a presentation counter.

Raw evidence and the corrected measurement record are under `__artifacts/stress_screenshot_20260913_v4/`. Step 18 establishes a successful-presentation counter independent of update callbacks and timing-query coverage. An accepted presentation request is not a monitor scanout measurement.

## Work order

| Step | Change | Owner | Status |
| --- | --- | --- | --- |
| 18 | Successful-presentation counting and an explicit bounded stress timing mode | Graphics presentation/runtime and smoke diagnostics | Complete |
| 19 | Reuse compatible generated geometry between AVBOIT phases | Material geometry contract and AVBOIT graph declaration | Proposal |
| 20 | Evaluate each meshlet-local vertex once in compute mesh emulation | Mesh shader runtime | Proposal |
| 21 | Reduce live transparent-shadow crossing storage while preserving optical integration | Shadow traversal/integration | Proposal |
| 22 | Evaluate remaining budget and, if needed, an explicit performance profile | Renderer settings and stress fixture | Pending measured results |

The initial sampled costs prioritize shadows (20.455 ms, including 15.654 ms transparent tracing), AVBOIT occupancy/extinction/accumulation (14.866 ms combined), and opaque geometry (5.439 ms). Reflection classification plus hardware tracing costs 3.875 ms, caustic photons plus resolve 2.459 ms, and surfel GI 1.293 ms. These are scope measurements, not additive estimates of independent savings; parent/child and overlapping scopes must remain distinct.

## Evaluation

Root applies candidates, builds, cooks assets, runs GPU work, and performs git operations sequentially. Other agents prepare and review proposals in ignored artifact directories. A completed evaluation may reject a candidate; an unproven optimization is not retained merely because its source performs fewer operations.

Preserve actual baseline and candidate executables and runtime volumes before each A/B. Validate geometry, overlapping transparent objects, CSG, refraction, reflection, and caustic behavior with applicable native tests and actual renderer smoke captures. Keep failure evidence. Shader changes require a fresh cook and matching volume identity; editing a source include alone is not an executable change.

For quality-preserving candidates, keep resolution, geometry, ray budgets, filter settings, and optical behavior identical. Predeclare each timing campaign before acquisition. Use eight balanced paired blocks with the same stress orientation and no concurrent builds, cooks, captures, or other GPU tests. Use successful presentations and monotonic wall time for application throughput, and sample-weighted totals for GPU scope duration. Require a resolved whole-frame improvement of at least 3% for a performance claim, with unchanged control scopes checked for drift. If results are inconclusive, report them as such rather than selecting favorable trials. Source-level storage or allocation benefits are reported separately from frame-time claims.

The default timing window is five seconds of warm-up followed by 30 seconds of measurement. Diagnostic and visual acquisitions are separate from timing. Quality-changing configurations, if introduced, remain explicit and receive separate image and performance results; they cannot be presented as equivalent-output optimizations.

The final target remains 16.67 ms per frame. No step is assumed to reach 60 FPS before measurement.

## Step 18: reliable presentation measurement

`GraphicsRuntime::getSuccessfulPresentationCount()` counts native `VK_SUCCESS` and `VK_SUBOPTIMAL_KHR` acceptance. The backend exposes that outcome separately from its continuation/recovery result: out-of-date presentation is not counted, while an accepted presentation remains counted if later synchronization fails. The runtime-lifetime counter survives resize and device recreation and does not depend on GPU timing capacity.

Ordinary stress FPS now observes that count against the steady clock. `NWB_STRESS_SMOKE_TIMING=1` additionally uses the existing AVBOIT timing render pass, reserves its 32 in-flight ranges, permits unfocused rendering, measures after five wall-clock seconds of warm-up for at least 30 seconds, and exits normally. Animation delta does not provide elapsed FPS time. Long stalls remain in the denominator; repeated idle callbacks add no frames. Capture-freeze controls are incompatible with this continuous mode and are rejected.

Opt and Dbg builds passed. Each configuration passed 381 active ECS graphics tests, 39 presentation tests, and five new presentation-probe tests (425 total; the existing 61 disabled ECS tests remain disabled). Actual stress runs with Vulkan validation enabled exited normally in both configurations: Opt counted 473 accepted presentations over 30.0170199 seconds; Dbg counted 197 over 30.125061 seconds. Counts equal the difference between the reported cumulative anchors. These validation-enabled runs establish measurement behavior and are not performance comparisons. Evidence is preserved under `__artifacts/stress_60fps/step18/validation_v1/`.

All changed authored C++ files passed CRLF, final-separator/EOF, and named-namespace checks; `git diff --check` passed. The build also reported three pre-existing unused-constant warnings in `impl/assets_texture/loader.cpp`, outside this change.

The common `stress_timing_smoke.py` runner now preserves each launch, binary/resource/helper identities, actual device route, raw logs, sampled GPU timings, and a parsed result in a new output directory. It checks contiguous count anchors, elapsed-time sums, normal shutdown, the actual extent, and requested Vulkan validation. Its 12 parser/lifecycle tests passed, CMake registered both tests, and an actual Opt validation run passed with 485 accepted presentations over 30.008545 seconds (16.1621 FPS; diagnostic only). Evidence is under `__artifacts/stress_60fps/step18/timing_wrapper_validation_v1/`.

Run `ctest --test-dir __cmake/build/windows-clang-arm64 -C opt -R '^nwb_stress_presentation_timing_smoke$' --output-on-failure` for the native timing smoke. For preserved A/B arms, invoke the Python runner with explicit `--executable`, `--working-directory`, `--output-directory`, and `--no-logserver` (or the existing logserver path). The default fixed yaw is 0.6 radians and the fixed simulation delta is 1/60 second; FPS always uses the independent steady clock.
