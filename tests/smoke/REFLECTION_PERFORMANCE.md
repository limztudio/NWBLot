# Reflection performance and benchmark reproduction

The September 12 [deferred optical appearance experiment](REFLECTION_OPTICAL_EXPERIMENT.md) did not establish a reliable improvement and was reverted. Its 16 trials remain separate from the September 9 results below.

The eight-stage reflection implementation is complete. The optimized benchmark completed all **164 process trials in 40 balanced blocks**, retaining **120,748 completed GPU frame samples**. These measurements show that fewer hardware rays or screen-depth loads do not necessarily reduce total frame time.

On the tested Qualcomm Adreno X2-90, Hardware mode was faster than Hybrid in the qualified long-miss and productive-floor comparisons. The optional screen-miss feedback reduced classification work, but none of its three paired frame comparisons established a reduction beyond the predeclared practical threshold. Feedback therefore remains disabled by default. Route and filter controls remain explicit; this single-adapter experiment does not change the general defaults.

Spatial filtering is a substantial quality cost in the roughness fixture: approximately 0.78–0.80 ms in its dispatch and 0.83 ms added to the frame relative to raw Hardware. Temporal-only accumulation has a measured dispatch cost near 0.060 ms; its total-frame difference is unresolved, which does not mean it is free. Temporal reuse requires trusted, unchanged view/content/settings and is not general motion-reprojected denoising.

**Bounded secondary optical transport remains a significant performance limitation.** In the clear-volume stress scene, Hardware averages 52.42 ms for `render.frame`, with 37.22 ms inside the hardware-reflection resolve alone. That resolve costs 36.92–37.63 ms across all six Hardware trials. All optical frame comparisons are control-uncertain, so the report does not certify the entire active-minus-disabled difference as a feature-only effect. The directly measured resolve cost nevertheless identifies an expensive path. This scene is not performance-qualified for a typical real-time frame budget on this adapter.

The optical fixture reflects a 31-stripe chart through one authored closed IOR-1.5 slab. Separate optical correctness evidence reports 217,668 transmitted paths and five scene queries per path, with no unsupported, limited, ambiguous, or overflowing paths. Those receivers already fit below the ordinary 262,144-ray default, so the benchmark's larger admission cap does not explain this cost. The optical matrix contains no matched box-free reference: its resolve timing is the whole optical-scene kernel, not an isolated per-interface cost. No register-pressure, spilling, or driver-cause claim is established by these measurements. Two slower Hybrid trials remain included in the reported interval.

## Build, hardware, and validation evidence

- Date: 2026-09-09. The measurement window was bracketed by operator records at 08:50:04–09:07:26 UTC, about 17 minutes 22 seconds including acquisition startup, report analysis, and launch/shutdown overhead.
- Host: Windows 11 ARM64; `windows-clang-arm64-opt` build, Qualcomm(R) Adreno(TM) X2-90 GPU, Vulkan hardware ray queries. Materials selected CS + PS compute emulation because native mesh shaders were unavailable in this configuration.
- AC power was connected, battery state was 79%, and the Balanced scheme was active at both operator observations. GPU clocks were not locked. No other build, cook, GPU benchmark, or framebuffer-capture job was launched during the matrix.
- Rendering source base: `7b27bdeee41dd73021d1bd40d4f89b9411e91455`, plus this step's timing-only smoke-fixture changes. The benchmark changed no production renderer or shader code.
- Optimized executable SHA256: `bbe2fda18f627dd19fea308eedd504dfa5effeda09fcb2b8bddcd2be87639d3e`.
- Authored packed volume `res/eda18e57f4796c04.vol` SHA256: `f0f7c098fc0ab564648e853afeda1a19d3b77660200129897063a4221dfd100f`. Only exact canonical runtime pipeline-cache segments are treated as mutable.
- Stage seven's final debug validation passed 100 framebuffer captures and 358 ECS graphics tests. Before the timing-only fixture changes, the optimized executable passed 96 captures: 22 baseline/budget, 14 roughness, 30 optical, 24 feedback, and six diagnostics-off. The older captures remain separate validation evidence; available manifest build identities are retained.
- The final optimized executable then passed three fresh long-miss qualification captures and all six diagnostics-off feedback image checks with GPU debugging. The three diagnostics-off off/on image pairs were byte-identical. The shader assets and production renderer were unchanged by the fixture fixes; the older optimized captures are not relabeled as exact-SHA qualification of the rebuilt fixture.
- Closing checks passed all eight selected CTest targets: ECS graphics, launcher, window capture, and the five reflection analysis suites. The benchmark analysis suite contains 33 tests.

The final long-miss qualification matches the exact executable and authored-volume hashes. Its 37 completed observations record 9,397,186 attempts, 149,692,232 hierarchy loads, 15.92947 loads per attempt, and 98.61484% exhausted-step misses. A separate 96-step geometric capture and paired images verify the fixture independently of those work counters.

## Measurement contract

All variants use native 960×720, seed 0, fixed scene delta 1/60, optical query bound 16, and a hardware admission cap of 1,382,400. Ordinary smooth comparisons disable temporal/spatial filters. The roughness family uses perceptual roughness 0.4 and a 16-sample history cap. Feedback is explicitly off except in named feedback variants; the roughness family does not measure feedback overhead.

| Family | Variants | Blocks | Trials | Screen steps |
| --- | --- | ---: | ---: | ---: |
| Offscreen miss control | Disabled, Hardware, Hybrid, Hybrid + feedback | 8 | 32 | 96 |
| Qualified long misses | Same four routes | 8 | 32 | 16 |
| Productive floor | Same four routes | 8 | 32 | 96 |
| Rough estimator/filter costs | Disabled, Hardware raw, temporal only, spatial only, both | 10 | 50 | 96 |
| Clear optical volume | Disabled, Hardware, Hybrid | 6 | 18 | 96 |

Each launch discards two completed publication intervals and retains at least six, with at least 100 completed GPU frame samples. Feedback comparisons additionally discard at least 32 completed GPU frames. Slow runs extend acquisition to meet coverage; for example, optical Hardware trials needed ten measured publications. A following header must seal each live report before use. Frame time is `sum(total_ms) / sum(gpu_samples)`, never a CPU frame count or a publication-window mean.

One balanced block is the independent statistical unit. Williams ordering balances position and preceding treatment across the complete cycle. All valid blocks are retained; no extra favorable blocks were appended. Per-comparison 95% intervals use 10,000 paired bootstrap resamples with analysis seed 0. They are not a simultaneous family-wide confidence statement.

The practical frame threshold is `max(0.02 ms, 3% of baseline frame time)`. A reduction or increase is resolved only if the entire paired interval clears that threshold and every common control establishes equivalence. Controls are opaque raster, shadow visibility, and deferred lighting; their equivalence tolerance is `max(0.015 ms, 3% of baseline control time)`. Crossing an equivalence boundary is uncertainty, not automatically proven drift. Composite and presentation are observations because reflection can affect their work.

The timing fixture opts into the existing unfocused-render policy without framebuffer readbacks. It reserves 32 complete timestamp ranges for each measured single-range scope and 32 times the native mip count for depth (320 at this extent). This prevents the observed two-range pool shortage from invalidating the planned acquisition. The runner still rejects missing or inconsistent coverage using a two-frame-or-2% ratio tolerance. All 54 completed Hybrid trials actually had exactly ten depth samples per GPU frame, and all 1,490 required scope-coverage checks passed. Ordinary rendering retains its existing focus and query-allocation policies.

Initial failed acquisitions are preserved separately: one stopped rendering after foreground ownership was lost; the next detected uneven timestamp sampling. Neither contributes to this matrix. A subsequent instrumentation-only pilot collected 715 frame and 7,150 depth samples before the declared matrix began.

## Reproduce

Build with `cmake --build --preset windows-clang-arm64-opt --target nwb_reflection_smoke nwb_ecs_graphics_tests --parallel 8`. Complete relevant rendering/correctness suites from [REFLECTION.md](REFLECTION.md) before timing a changed renderer. The benchmark runner and its CTest analysis suite are checked in as [reflection_benchmark.py](reflection_benchmark.py) and `nwb_reflection_benchmark_analysis_unit`.

From the repository root, use a new empty output directory for each run. Keep the executable and authored volumes unchanged between qualification and timing. Do not build, cook, or run other GPU tests during acquisition.

```powershell
$reflectionResults = '__artifacts/reflection_benchmark_new'
$reflectionExe = '__exec/windows/arm64/full/opt/reflection_smoke.exe'
$reflectionRuntime = '__cmake/build/windows-clang-arm64/Testing/smoke_runtime/opt'
$reflectionLogserver = '__exec/windows/arm64/full/opt/logserver.exe'
$reflectionChecks = @('--executable', $reflectionExe, '--working-directory', $reflectionRuntime,
    '--logserver-executable', $reflectionLogserver, '--require-hardware', '--application-arg=--gpudbg')
python tests/smoke/reflection_smoke.py @reflectionChecks --suite feedback --feedback-cases long_miss_baseline,long_miss_feedback,long_miss_geometry --output-directory "$reflectionResults/qualification"
if($LASTEXITCODE -ne 0){ throw 'Long-miss qualification failed' }
python tests/smoke/reflection_smoke.py @reflectionChecks --suite feedback-diagnostics-off --output-directory "$reflectionResults/diagnostics_off"
if($LASTEXITCODE -ne 0){ throw 'Diagnostics-off image checks failed' }

$reflectionBenchmarkArgs = @('--executable', $reflectionExe, '--working-directory', $reflectionRuntime,
    '--logserver-executable', $reflectionLogserver, '--require-hardware',
    '--width', '960', '--height', '720', '--ray-budget', '1382400', '--sampling-seed', '0',
    '--optical-queries', '16', '--warmup-intervals', '2', '--sample-intervals', '6',
    '--minimum-frame-samples', '100', '--timeout', '90', '--order-seed', '0', '--analysis-seed', '0')
python tests/smoke/reflection_benchmark.py @reflectionBenchmarkArgs --family offscreen --include-feedback --blocks 8 --screen-steps 96 --output-directory "$reflectionResults/timing_offscreen"
if($LASTEXITCODE -ne 0){ throw 'Offscreen timing failed' }
python tests/smoke/reflection_benchmark.py @reflectionBenchmarkArgs --family feedback_long_miss --include-feedback --blocks 8 --screen-steps 16 --qualification "$reflectionResults/qualification/reflection_feedback_manifest.json" --output-directory "$reflectionResults/timing_long_miss"
if($LASTEXITCODE -ne 0){ throw 'Long-miss timing failed' }
python tests/smoke/reflection_benchmark.py @reflectionBenchmarkArgs --family floor --include-feedback --blocks 8 --screen-steps 96 --output-directory "$reflectionResults/timing_floor"
if($LASTEXITCODE -ne 0){ throw 'Floor timing failed' }
python tests/smoke/reflection_benchmark.py @reflectionBenchmarkArgs --family rough --blocks 10 --screen-steps 96 --roughness 0.4 --history-samples 16 --output-directory "$reflectionResults/timing_rough_filters"
if($LASTEXITCODE -ne 0){ throw 'Roughness timing failed' }
python tests/smoke/reflection_benchmark.py @reflectionBenchmarkArgs --family optical_clear --blocks 6 --screen-steps 96 --output-directory "$reflectionResults/timing_optical_clear"
if($LASTEXITCODE -ne 0){ throw 'Optical timing failed' }
```

The runner disables reflection diagnostics, framebuffer capture, and inherited mutation controls for timing. It rejects explicit validation-layer overrides; it does not silently disable a requested layer. It verifies route/settings/policy logs, normal shutdown, dimensions, actual GPU samples, executable identity, and authored resources. Known scope hashes are decoded using the repository's name algorithm; an explicitly supplied matching `.namesym` can also be used. Do not decode with an unrelated build's symbol file.

Every family writes `plan.json`, incremental `trials.json`, and final `report.json`; each trial preserves launch settings, runtime logs, raw timing, normalized totals, and retained publication ranges. An incomplete family produces failure evidence without a completed inference. The artifacts for this recorded experiment are under `__artifacts/reflection_stage8_opt/final`, including operator observations and `benchmark_verification.json`. Raw artifacts are generated evidence, not committed test goldens. Report hashes below identify the exact source results.

## Scope and quality limits

These are stationary fixtures on one adapter and backend, with a deliberately generous admission budget. They do not qualify default-budget image quality at every resolution. Spatial-filter costs are not a comparison at equal image quality, and temporal history is invalidated when trust is lost. Smooth primary-glass reflection is supported; rough dielectric transmission is not implemented. The bounded secondary path supports explicitly authored closed media, with finite query/media/bootstrap limits and conservative handling of ambiguous boundaries. It does not recursively trace all non-TIR secondary Fresnel reflection branches. Ordinary hit shading and environment lighting retain the implemented approximations.

The original combined-caustic and optical smoke scenes remain visual/physics checks, separate from this timing matrix. The full results below retain every comparison, including all unresolved and control-uncertain outcomes.

## Recorded results

**Complete five-family matrix: 5/5 families, 164/164 trials.**

Each trial first normalizes GPU samples as total_ms / gpu_samples. Variant values below are arithmetic means of those independent trial means, with one equal-weight trial per balanced block. They are not pooled cross-scene means. Differences and 95% confidence intervals retain the benchmark's paired-block inference.

A negative difference is less GPU time. The status requires both a practically resolved frame interval and equivalent control scopes. Unresolved or uncertain rows remain part of the evidence. Intervals are per comparison, not a simultaneous family-wide guarantee.

Executable SHA256: `bbe2fda18f627dd19fea308eedd504dfa5effeda09fcb2b8bddcd2be87639d3e`. Extent: 960 × 720. Diagnostics and capture: off. Ray budget: 1,382,400. Optical query bound: 16. Warm-up / measured publications / minimum GPU frames: 2 / 6 / 100.

## Main route and filter comparisons

| Scene | Pair | Baseline ms | Candidate ms | Δ ms | 95% CI Δ ms | Practical ms | Status | Controls |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| offscreen | hardware → hybrid | 3.4334 | 3.7846 | 0.3512 | [0.2619, 0.4366] | 0.1030 | control_uncertain | shadow_visibility uncertain |
| offscreen | hybrid → hybrid_feedback | 3.7846 | 3.7133 | -0.0713 | [-0.1411, -0.0033] | 0.1135 | unresolved | all equivalent within declared tolerance |
| feedback_long_miss | hardware → hybrid | 3.5294 | 3.9526 | 0.4232 | [0.3143, 0.5320] | 0.1059 | resolved_gpu_time_increase | all equivalent within declared tolerance |
| feedback_long_miss | hybrid → hybrid_feedback | 3.9526 | 3.7859 | -0.1667 | [-0.2470, -0.0961] | 0.1186 | unresolved | all equivalent within declared tolerance |
| floor | hardware → hybrid | 3.6639 | 3.8845 | 0.2206 | [0.1267, 0.3146] | 0.1099 | resolved_gpu_time_increase | all equivalent within declared tolerance |
| floor | hybrid → hybrid_feedback | 3.8845 | 3.9098 | 0.0254 | [-0.0579, 0.1115] | 0.1165 | unresolved | all equivalent within declared tolerance |
| rough | hardware_raw → hardware_temporal | 3.4882 | 3.5492 | 0.0610 | [-0.0240, 0.1406] | 0.1046 | unresolved | all equivalent within declared tolerance |
| rough | hardware_raw → hardware_spatial | 3.4882 | 4.3158 | 0.8276 | [0.7285, 0.9230] | 0.1046 | resolved_gpu_time_increase | all equivalent within declared tolerance |
| rough | hardware_raw → hardware_filtered | 3.4882 | 4.3272 | 0.8390 | [0.7805, 0.8954] | 0.1046 | resolved_gpu_time_increase | all equivalent within declared tolerance |
| optical_clear | hardware → hybrid | 52.4191 | 58.0185 | 5.5994 | [0.2803, 11.0051] | 1.5726 | control_uncertain | deferred_lighting uncertain; opaque_regular uncertain |

## offscreen: Offscreen miss control

8 balanced independent blocks; 32 trials. Screen step limit: 96. Perceptual roughness: 0.0.

| Variant | Trials | Frame mean ms | Min GPU frames / trial | Max GPU frames / trial |
| --- | --- | --- | --- | --- |
| disabled | 8 | 3.1998 | 854 | 950 |
| hardware | 8 | 3.4334 | 787 | 869 |
| hybrid | 8 | 3.7846 | 714 | 804 |
| hybrid_feedback | 8 | 3.7133 | 731 | 810 |

All planned pairs, including incremental cost above Disabled:

| Pair | Baseline ms | Candidate ms | Δ ms | 95% CI Δ ms | Practical ms | Status | Controls |
| --- | --- | --- | --- | --- | --- | --- | --- |
| disabled → hardware | 3.1998 | 3.4334 | 0.2337 | [0.1316, 0.3227] | 0.0960 | resolved_gpu_time_increase | all equivalent within declared tolerance |
| disabled → hybrid | 3.1998 | 3.7846 | 0.5849 | [0.4738, 0.6899] | 0.0960 | control_uncertain | shadow_visibility uncertain |
| disabled → hybrid_feedback | 3.1998 | 3.7133 | 0.5136 | [0.4142, 0.6019] | 0.0960 | control_uncertain | shadow_visibility uncertain |
| hardware → hybrid | 3.4334 | 3.7846 | 0.3512 | [0.2619, 0.4366] | 0.1030 | control_uncertain | shadow_visibility uncertain |
| hybrid → hybrid_feedback | 3.7846 | 3.7133 | -0.0713 | [-0.1411, -0.0033] | 0.1135 | unresolved | all equivalent within declared tolerance |

Kernel means are milliseconds per dispatch. Depth is per **mip** dispatch; this extent has ten mips. The auxiliary kernel-work estimate multiplies depth by ten but is not an end-to-end frame or critical-path measurement.

| Variant | Classify | HW | Depth / mip | Build args | Temporal | Spatial | Kernel-work estimate ms / frame |
| --- | --- | --- | --- | --- | --- | --- | --- |
| disabled | 0.0633 | — | — | — | — | — | 0.0633 |
| hardware | 0.2101 | 0.0973 | — | 0.0034 | — | — | 0.3108 |
| hybrid | 0.3287 | 0.0953 | 0.0140 | 0.0034 | — | — | 0.5676 |
| hybrid_feedback | 0.2987 | 0.0938 | 0.0138 | 0.0040 | — | — | 0.5346 |

Control detail (all values in milliseconds):

- **disabled → hybrid:** shadow_visibility uncertain: Δ 0.0237, CI [0.0161, 0.0313], tolerance ±0.0232 ms
- **disabled → hybrid_feedback:** shadow_visibility uncertain: Δ 0.0283, CI [0.0154, 0.0407], tolerance ±0.0232 ms
- **hardware → hybrid:** shadow_visibility uncertain: Δ 0.0236, CI [0.0139, 0.0338], tolerance ±0.0232 ms

Source report: `__artifacts/reflection_stage8_opt/final/timing_offscreen/report.json` (SHA256 `8c72d94b61953814f1fe132cd61c0223a5d9da291518d61a8379ef1b83e2f307`).

## feedback_long_miss: Qualified costly screen misses

8 balanced independent blocks; 32 trials. Screen step limit: 16. Perceptual roughness: 0.0.

| Variant | Trials | Frame mean ms | Min GPU frames / trial | Max GPU frames / trial |
| --- | --- | --- | --- | --- |
| disabled | 8 | 2.7719 | 922 | 1108 |
| hardware | 8 | 3.5294 | 766 | 852 |
| hybrid | 8 | 3.9526 | 682 | 759 |
| hybrid_feedback | 8 | 3.7859 | 727 | 793 |

All planned pairs, including incremental cost above Disabled:

| Pair | Baseline ms | Candidate ms | Δ ms | 95% CI Δ ms | Practical ms | Status | Controls |
| --- | --- | --- | --- | --- | --- | --- | --- |
| disabled → hardware | 2.7719 | 3.5294 | 0.7574 | [0.6731, 0.8505] | 0.0832 | resolved_gpu_time_increase | all equivalent within declared tolerance |
| disabled → hybrid | 2.7719 | 3.9526 | 1.1807 | [1.0649, 1.2811] | 0.0832 | resolved_gpu_time_increase | all equivalent within declared tolerance |
| disabled → hybrid_feedback | 2.7719 | 3.7859 | 1.0140 | [0.9175, 1.1175] | 0.0832 | resolved_gpu_time_increase | all equivalent within declared tolerance |
| hardware → hybrid | 3.5294 | 3.9526 | 0.4232 | [0.3143, 0.5320] | 0.1059 | resolved_gpu_time_increase | all equivalent within declared tolerance |
| hybrid → hybrid_feedback | 3.9526 | 3.7859 | -0.1667 | [-0.2470, -0.0961] | 0.1186 | unresolved | all equivalent within declared tolerance |

Kernel means are milliseconds per dispatch. Depth is per **mip** dispatch; this extent has ten mips. The auxiliary kernel-work estimate multiplies depth by ten but is not an end-to-end frame or critical-path measurement.

| Variant | Classify | HW | Depth / mip | Build args | Temporal | Spatial | Kernel-work estimate ms / frame |
| --- | --- | --- | --- | --- | --- | --- | --- |
| disabled | 0.0622 | — | — | — | — | — | 0.0622 |
| hardware | 0.2051 | 0.5727 | — | 0.0030 | — | — | 0.7808 |
| hybrid | 0.4777 | 0.5664 | 0.0139 | 0.0039 | — | — | 1.1871 |
| hybrid_feedback | 0.3401 | 0.5699 | 0.0137 | 0.0039 | — | — | 1.0511 |

Control detail (all values in milliseconds):

All reported pairs establish equivalence for the three declared controls.

Separate workload qualification: 37 completed observations, 15.929 hierarchy loads per attempt and 98.61% genuine step-limit misses. These are workload counters, not timings. Qualification manifest: `__artifacts/reflection_stage8_opt/final/qualification\reflection_feedback_manifest.json` (SHA256 `40e06a6e2af8e68a9ce36e9d65fdb097b47c2475c985b3430db6540b4cb713a5`).

Source report: `__artifacts/reflection_stage8_opt/final/timing_long_miss/report.json` (SHA256 `626609f4aaf9da6656f2059bc7d27dadf322e5624fe6e78897060b603e760f75`).

## floor: Productive screen reflection

8 balanced independent blocks; 32 trials. Screen step limit: 96. Perceptual roughness: 0.0.

| Variant | Trials | Frame mean ms | Min GPU frames / trial | Max GPU frames / trial |
| --- | --- | --- | --- | --- |
| disabled | 8 | 2.7692 | 898 | 1107 |
| hardware | 8 | 3.6639 | 743 | 827 |
| hybrid | 8 | 3.8845 | 676 | 766 |
| hybrid_feedback | 8 | 3.9098 | 673 | 772 |

All planned pairs, including incremental cost above Disabled:

| Pair | Baseline ms | Candidate ms | Δ ms | 95% CI Δ ms | Practical ms | Status | Controls |
| --- | --- | --- | --- | --- | --- | --- | --- |
| disabled → hardware | 2.7692 | 3.6639 | 0.8946 | [0.7770, 1.0069] | 0.0831 | control_uncertain | shadow_visibility uncertain |
| disabled → hybrid | 2.7692 | 3.8845 | 1.1152 | [1.0056, 1.2214] | 0.0831 | resolved_gpu_time_increase | all equivalent within declared tolerance |
| disabled → hybrid_feedback | 2.7692 | 3.9098 | 1.1406 | [0.9946, 1.2727] | 0.0831 | control_uncertain | shadow_visibility uncertain |
| hardware → hybrid | 3.6639 | 3.8845 | 0.2206 | [0.1267, 0.3146] | 0.1099 | resolved_gpu_time_increase | all equivalent within declared tolerance |
| hybrid → hybrid_feedback | 3.8845 | 3.9098 | 0.0254 | [-0.0579, 0.1115] | 0.1165 | unresolved | all equivalent within declared tolerance |

Kernel means are milliseconds per dispatch. Depth is per **mip** dispatch; this extent has ten mips. The auxiliary kernel-work estimate multiplies depth by ten but is not an end-to-end frame or critical-path measurement.

| Variant | Classify | HW | Depth / mip | Build args | Temporal | Spatial | Kernel-work estimate ms / frame |
| --- | --- | --- | --- | --- | --- | --- | --- |
| disabled | 0.0629 | — | — | — | — | — | 0.0629 |
| hardware | 0.2062 | 0.6574 | — | 0.0034 | — | — | 0.8669 |
| hybrid | 0.9600 | 0.0408 | 0.0139 | 0.0047 | — | — | 1.1441 |
| hybrid_feedback | 0.9298 | 0.0410 | 0.0140 | 0.0054 | — | — | 1.1165 |

Control detail (all values in milliseconds):

- **disabled → hardware:** shadow_visibility uncertain: Δ 0.0005, CI [-0.0177, 0.0187], tolerance ±0.0176 ms
- **disabled → hybrid_feedback:** shadow_visibility uncertain: Δ 0.0068, CI [-0.0080, 0.0225], tolerance ±0.0176 ms

Source report: `__artifacts/reflection_stage8_opt/final/timing_floor/report.json` (SHA256 `04fee4ce2ba7ebaf37c30e8105282423df86593ecc79c19811698d045f078399`).

## rough: Rough estimator and filters

10 balanced independent blocks; 50 trials. Screen step limit: 96. Perceptual roughness: 0.4.

| Variant | Trials | Frame mean ms | Min GPU frames / trial | Max GPU frames / trial |
| --- | --- | --- | --- | --- |
| disabled | 10 | 3.1143 | 828 | 973 |
| hardware_raw | 10 | 3.4882 | 772 | 874 |
| hardware_temporal | 10 | 3.5492 | 745 | 851 |
| hardware_spatial | 10 | 4.3158 | 647 | 697 |
| hardware_filtered | 10 | 4.3272 | 628 | 694 |

All planned pairs, including incremental cost above Disabled:

| Pair | Baseline ms | Candidate ms | Δ ms | 95% CI Δ ms | Practical ms | Status | Controls |
| --- | --- | --- | --- | --- | --- | --- | --- |
| disabled → hardware_raw | 3.1143 | 3.4882 | 0.3739 | [0.3284, 0.4263] | 0.0934 | resolved_gpu_time_increase | all equivalent within declared tolerance |
| disabled → hardware_temporal | 3.1143 | 3.5492 | 0.4349 | [0.3304, 0.5412] | 0.0934 | resolved_gpu_time_increase | all equivalent within declared tolerance |
| disabled → hardware_spatial | 3.1143 | 4.3158 | 1.2016 | [1.0909, 1.3010] | 0.0934 | resolved_gpu_time_increase | all equivalent within declared tolerance |
| disabled → hardware_filtered | 3.1143 | 4.3272 | 1.2129 | [1.1681, 1.2525] | 0.0934 | resolved_gpu_time_increase | all equivalent within declared tolerance |
| hardware_raw → hardware_temporal | 3.4882 | 3.5492 | 0.0610 | [-0.0240, 0.1406] | 0.1046 | unresolved | all equivalent within declared tolerance |
| hardware_raw → hardware_spatial | 3.4882 | 4.3158 | 0.8276 | [0.7285, 0.9230] | 0.1046 | resolved_gpu_time_increase | all equivalent within declared tolerance |
| hardware_raw → hardware_filtered | 3.4882 | 4.3272 | 0.8390 | [0.7805, 0.8954] | 0.1046 | resolved_gpu_time_increase | all equivalent within declared tolerance |

Kernel means are milliseconds per dispatch. Depth is per **mip** dispatch; this extent has ten mips. The auxiliary kernel-work estimate multiplies depth by ten but is not an end-to-end frame or critical-path measurement.

| Variant | Classify | HW | Depth / mip | Build args | Temporal | Spatial | Kernel-work estimate ms / frame |
| --- | --- | --- | --- | --- | --- | --- | --- |
| disabled | 0.0632 | — | — | — | — | — | 0.0632 |
| hardware_raw | 0.2253 | 0.1360 | — | 0.0033 | — | — | 0.3646 |
| hardware_temporal | 0.2260 | 0.1375 | — | 0.0036 | 0.0598 | — | 0.4269 |
| hardware_spatial | 0.2303 | 0.1427 | — | 0.0039 | — | 0.7844 | 1.1613 |
| hardware_filtered | 0.2303 | 0.1412 | — | 0.0038 | 0.0602 | 0.7993 | 1.2348 |

Control detail (all values in milliseconds):

All reported pairs establish equivalence for the three declared controls.

Source report: `__artifacts/reflection_stage8_opt/final/timing_rough_filters/report.json` (SHA256 `feca7e746eaacb78f45455fe6803e152a4988412a3b634ba511b9657844f6e1b`).

## optical_clear: Bounded secondary clear-volume transmission

6 balanced independent blocks; 18 trials. Screen step limit: 96. Perceptual roughness: 0.0.

| Variant | Trials | Frame mean ms | Min GPU frames / trial | Max GPU frames / trial |
| --- | --- | --- | --- | --- |
| disabled | 6 | 14.1847 | 206 | 216 |
| hardware | 6 | 52.4191 | 100 | 100 |
| hybrid | 6 | 58.0185 | 100 | 104 |

All planned pairs, including incremental cost above Disabled:

| Pair | Baseline ms | Candidate ms | Δ ms | 95% CI Δ ms | Practical ms | Status | Controls |
| --- | --- | --- | --- | --- | --- | --- | --- |
| disabled → hardware | 14.1847 | 52.4191 | 38.2344 | [38.0276, 38.4586] | 0.4255 | control_uncertain | deferred_lighting uncertain; opaque_regular uncertain; shadow_visibility uncertain |
| disabled → hybrid | 14.1847 | 58.0185 | 43.8338 | [38.5240, 49.1741] | 0.4255 | control_uncertain | deferred_lighting uncertain; opaque_regular uncertain; shadow_visibility uncertain |
| hardware → hybrid | 52.4191 | 58.0185 | 5.5994 | [0.2803, 11.0051] | 1.5726 | control_uncertain | deferred_lighting uncertain; opaque_regular uncertain |

Kernel means are milliseconds per dispatch. Depth is per **mip** dispatch; this extent has ten mips. The auxiliary kernel-work estimate multiplies depth by ten but is not an end-to-end frame or critical-path measurement.

| Variant | Classify | HW | Depth / mip | Build args | Temporal | Spatial | Kernel-work estimate ms / frame |
| --- | --- | --- | --- | --- | --- | --- | --- |
| disabled | 0.0697 | — | — | — | — | — | 0.0697 |
| hardware | 0.2401 | 37.2211 | — | 0.0054 | — | — | 37.4666 |
| hybrid | 0.3670 | 42.5790 | 0.0160 | 0.0034 | — | — | 43.1098 |

Control detail (all values in milliseconds):

- **disabled → hardware:** deferred_lighting uncertain: Δ 0.0189, CI [-0.0022, 0.0418], tolerance ±0.0150 ms; opaque_regular uncertain: Δ 0.0637, CI [-0.0069, 0.1231], tolerance ±0.0261 ms; shadow_visibility uncertain: Δ 0.3034, CI [0.1904, 0.3933], tolerance ±0.2580 ms
- **disabled → hybrid:** deferred_lighting uncertain: Δ 0.0025, CI [-0.0104, 0.0155], tolerance ±0.0150 ms; opaque_regular uncertain: Δ 0.0475, CI [-0.0034, 0.0994], tolerance ±0.0261 ms; shadow_visibility uncertain: Δ 0.2813, CI [0.2468, 0.3208], tolerance ±0.2580 ms
- **hardware → hybrid:** deferred_lighting uncertain: Δ -0.0164, CI [-0.0444, 0.0129], tolerance ±0.0150 ms; opaque_regular uncertain: Δ -0.0162, CI [-0.0684, 0.0250], tolerance ±0.0280 ms

Source report: `__artifacts/reflection_stage8_opt/final/timing_optical_clear/report.json` (SHA256 `d8673a9f41011d002479ac0a0f0a78ac0421aefc5db8220cce3f049d4f0beffa`).

## Interpretation limits

Disabled retains shared renderer infrastructure and classification; differences above it are incremental route costs. Frame timing includes the feature's effects on the GPU schedule. Nested kernels must not be added to their containing frame or packet envelope.

The common equivalence gates are opaque_regular, shadow_visibility, and deferred_lighting. Composite and presentation scopes are observations rather than equivalence gates because the feature may change their work. No table infers FPS from GPU timing or treats fewer rays/loads as a speedup.

The offscreen scene is a miss control; this report does not claim that its misses are immediate. The optical_clear scene measures one bounded authored clear-volume workload, not every nesting, overlap, or caustic case. Judge feedback using the productive and miss controls together; a selected win alone does not establish a safe default.

Power state, thermal stability, physical GPU identity, and absence of concurrent work remain the operator's recorded experimental conditions. The report verifies matching executable/assets/settings; it cannot infer those external conditions from timing values.
