# Stress rendering performance

On the recorded Snapdragon X2 Elite Extreme / Qualcomm Adreno X2-90 system, both rendering routes exceeded **60 average accepted presentations per second** with the 20-body stress workload. The final default-profile build passed explicit `>60` checks at **70.8933 FPS with hardware RT** and **62.3919 FPS with RT disabled**. Earlier software runs measured 62.5994 and 62.3973 FPS; the earlier hardware control measured 70.5082 FPS. These results qualify the recorded profiles and machine, not every frame, every scene, or another GPU. Software headroom remains modest.

## Workload and measurement

The workload is 1280Ã—900, ten transparent and ten opaque high-detail bodies at full scale, rotating in a fixed bind pose. It includes the ground, colored GI enclosure, directional and point lights, opaque/transparent shadows, AVBOIT, reflection, refraction, caustics, and surfel GI. The bodies are not twenty independently changing skeletal poses. `--characters-per-class 10 --animate` selects this workload; five per class is a different comparison scene.

The `two_rows_v1` signature checks the two rows of ten, 0.72 spacing, 0.18 stagger, depths âˆ’0.55/+0.55, camera `(0, 2.7, -7.2)`, pitch 0.25 and vertical FOV approximately 1.0471976 radians. No resolution, body-count, material-effect, or light-count reduction is part of the performance profile.

The harness excludes a five-second warmup and measures approximately thirty seconds with a steady clock. FPS is accepted native presentations divided by elapsed seconds. Each acquisition retains executable/runtime/helper hashes, requested and applied settings, route markers, raw logs, and results. Serialize runs, use fresh output directories, and do not change the executable or assets during acquisition. The recorded adapter inventory reports Vulkan 1.4.295 and Qualcomm driver 0.863.0.

## Implementation and tradeoffs

The problem was repeated high-detail geometry and optical work across many rays and views, rather than one isolated GI switch. GI remains enabled. The changes reduce repeated work while keeping unsupported and unresolved cases on explicit fallback paths.

Eligible engine-owned shared mesh programs cache decoded object-space geometry and use an internal indexed vertex stage. Accepted source revisions and resource identities govern reuse; world transforms and projection remain current during rasterization. Generic authored programs and CSG retain supported compute emulation. Native mesh shaders are not enabled globally. Current GPU mesh roots also drive software scene-BVH leaf refit and internal-bound unions, preserving posed geometry coverage.

On the software route, eligible directional lights use one light-space view and point lights use six. Indexed rasterization captures opaque D32 depth and transparent crossings; compute shades and sorts transparent records once, then resolves receivers. Crossings retain instance and primitive identity, and per-instance integration preserves thickness and overlapping optical boundaries. The budget is sixteen events per texel, twenty bytes per event. Receiver-plane correction handles discrete map self-shadowing.

Invalid views, missing point faces, invalid events, unstable receivers, overflow, unsupported features, and lights outside the map budget retain software BVH traversal. Map resolve and fallback use separate kernels so successful map receivers avoid the traversal working set. Storage admission respects checked addressing, memory budget, and the device's storage-buffer descriptor range. Per-view culling retains uncertain draws.

`ReuseOneFrame` retains depth, counts, shaded events, and GPU-fitted views together for one subsequent accepted frame. It skips upload/clear/fit/capture/shade while still shading current receivers and declaring current BVH/material fallback inputs. The following accepted frame refreshes. Only world transforms may lag; changed geometry revisions, material bytes, light data, instance/boundary ordering, layout, resources, and invalid history force capture. Runtime geometry without an accepted content revision and CSG cannot reuse. Bind-pose world rotation preserves the accepted object-space revision, making this stress scene eligible after initialization.

The shadow feature owns pending and accepted capture tickets and pins their geometry/texture identities. Refresh invalidates old in-place contents before recording; complete recording and successful shadow packet/resource-state acceptance publish the replacement. Rejection, skipped frames, resize, and graph retries cannot extend stale history. Reuse omits the untouched draw-argument import instead of promising an unowned final-state export. Metadata graph tests reproduce that former compile failure and verify accepted depth-state ownership.

One-frame capture reuse can delay moving shadows. Finite map resolution, fitted coverage and five-tap blocker estimation can alter contact detail, penumbrae and edge leakage. Temporal-one transparent sampling uses one sample only after accepted filtered history; bootstrap/reset uses three. These are explicit quality estimates, not image-equivalence claims.

Caustics keep their hardware/software producers and temporal phases. Photon-grid divisor 2 reduces grid area to one quarter; divisor 4 to one sixteenth, preserving energy normalization while changing noise/detail/convergence. All five wavelet dilations remain; large dilations avoid unused shared-memory allocation. Surfel GI retains its shared cache/resolve and backend choice. RT-disabled camera reflection/refraction use screen-space paths and existing miss handling, so arbitrary off-screen transport is unavailable. SSR remains at 96 steps in the qualified profile.

Header-first light-view lookup and shared FP32 shadow-resolve coefficients remain. The rejected retained-view-hint, surfel zero-weight guard, and deferred-final-hit caustic experiments are not production claims.

## Defaults and explicit profiles

Renderer settings remain conservative. Only the direct Stress application's base settings select the performance profile; other smoke scenes retain their defaults. Explicit smoke environment values override that base. The Python stress harness writes every quality control itself, so its default acquisition remains the reference profile regardless of the direct application's base settings.

| Setting | Renderer / harness reference | Direct Stress base / explicit performance run |
| --- | --- | --- |
| Software shadow backend | Automatic | Automatic; hardware route remains hardware |
| Directional / point map size; budget | 512 / 256; 256 MiB | Unchanged |
| Software coverage | Reference | FittedVolume |
| Blocker search | ReferenceGrid9 | CompactCross5 |
| Capture cadence | EveryFrame | ReuseOneFrame on software; hardware benchmark explicitly selects EveryFrame |
| Transparent shadow sampling | ReferenceThree | TemporalOne after accepted history |
| Caustic photon-grid divisor | 1 | 2 with logical-device RayQuery; otherwise 4 |
| SSR iteration cap | 96 | 96 |

Production receives typed settings in the shadow, caustic and reflection domains. Environment parsing stays in smoke helpers. A reuse benchmark requires both the applied setting and an actual accepted reuse marker; unsupported routes cannot silently qualify that request.

## Recorded evidence

Artifact paths below are under `__artifacts/software_rt_optimization`. Inspect each `launch.json` and `result.json` for its exact binary, assets, helper identities and profile. Individual runs and a repeat are not confidence intervals or a guarantee of sustained thermal performance.

| Run directory | Profile | Average FPS | ms/presentation | Threshold evidence |
| --- | --- | ---: | ---: | --- |
| `cadence_20260926_hw_control` | Hardware, TemporalOne, divisor 2, SSR96, EveryFrame | 70.5082 | 14.1828 | Measured above 60; no explicit threshold requested in that run |
| `cadence_20260926_sw_fixed` | True no-RT, fitted/compact/TemporalOne, divisor 4, SSR96, ReuseOneFrame | 62.5994 | 15.9746 | Measured above 60; no explicit threshold requested in that run |
| `cadence_20260926_sw_qualified` | Same explicit software quality profile | 62.3973 | 16.0263 | `--minimum-fps 60` passed: 1872 presentations / 30.0013109 seconds |
| `stress_defaults_20260926_hw_qualified` | Final default-profile build; explicit hardware settings above | 70.8933 | 14.1057 | `--minimum-fps 60` passed: 2127 presentations / 30.0028352 seconds |
| `stress_defaults_20260926_sw_qualified` | Final default-profile build; explicit software settings above | 62.3919 | 16.0277 | `--minimum-fps 60` passed: 1872 presentations / 30.0038917 seconds |

The first software cadence run published 1,785 GPU frame samples and 892 samples each for view fit, opaque capture, transparent capture and crossing shade. Both receiver map and fallback scopes retained 1,785 samples. This supports alternate-frame producer reuse without skipping current receiver work. It is not a presentation count, and the GPU publication window differs from the steady-clock FPS window. The frame GPU mean was 15.8563 ms.

Historical `pulled_20260926_*_baseline` runs used the subsequently repaired FP16 shadow sampler and are excluded from speedup attribution. The `shader_math_20260926_sw_profile` experiment included shared resolve coefficients plus the later-rejected surfel zero-weight guard; it did **not** include header-first view search. It is not a clean measure of the final retained implementation and is omitted from the comparison table.

Completed validation includes all 437 ECS graphics tests, GPU task tests, stress/CPU timing analysis CTests (`cadence_20260926_cpu_final.log`), and all eight registered no-RT scene tests (`cadence_20260926_software.log`). The scene matrix covers overlapping glass, two CSG poses, caustics, optical contributions, animated skinned glass, 20-body resize to 1001Ã—701, and GI. The separate divisor-4 optical comparison passed (`cadence_20260926_sw_optical_div4/caustic_optical_manifest.json`), using actual framebuffer readbacks with reflection, refraction and caustics individually disabled. It checks visible software contributions, not the hardware-only off-screen radiometric oracle.

Direct HW and disabled-RT Stress launches with no quality overrides passed Vulkan-validation capture checks (`stress_defaults_20260926_hw_verified.bmp` and `stress_defaults_20260926_sw.bmp`). A non-Stress caustic scene retained reference settings, and explicit reference environment values overrode the new Stress defaults. The final smoke analysis tests passed after merging both changes: 56 stress tests and 12 CPU-diagnostic tests.

`cadence_20260926_cpu_diagnostic` completed as a qualified diagnostic acquisition with `performance_qualification=false`; its FPS is not an additional target result. CPU/GPU scope durations overlap and may span workers or use different publication counts. Do not sum parent/child scopes or subtract GPU means from wall time to estimate CPU cost. Pacing-ring summaries currently include warmup observations; they do not establish timed-window p95 or that every frame is below 16.67 ms.

## Build and reproduce

Use the configured Windows ARM64 Clang/Vulkan environment. Keep each executable and its matching cooked runtime together. Choose a fresh `$perfOutput` directory and run the following acquisitions serially without GPU validation or profiling enabled:

```powershell
$perfPython = 'C:/Users/ltw94/AppData/Local/Programs/Python/Python311-arm64/python.exe'
$perfBuild = '__cmake/build/windows-clang-arm64'
$perfRuntime = "$perfBuild/Testing/skinning_culling_benchmark_runtime/opt"
$perfExe = '__exec/windows/arm64/full/opt/stress_test_smoke.exe'
$perfOutput = '__artifacts/stress_rendering_reproduction'
cmake --preset windows-clang-arm64
cmake --build $perfBuild --config opt --parallel 8 --target nwb_stress_test_smoke nwb_caustic_sphere_smoke nwb_transparent_multi_smoke nwb_transparent_csg_smoke nwb_skinned_caustic_smoke nwb_gi_test_smoke nwb_ecs_graphics_tests nwb_gpu_task_tests
ctest --test-dir $perfBuild -C opt -j 1 -R '^(nwb_ecs_graphics_tests|nwb_gpu_task_tests|nwb_stress_presentation_timing_analysis_unit|nwb_stress_cpu_timing_analysis_unit)$' --output-on-failure
$perfCommon = @('--executable', $perfExe, '--working-directory', $perfRuntime, '--no-logserver',
    '--animate', '--characters-per-class', '10', '--software-shadow-backend', 'automatic',
    '--software-shadow-budget-mib', '256', '--software-shadow-directional-resolution', '512',
    '--software-shadow-point-resolution', '256', '--software-shadow-coverage', 'fitted_volume',
    '--software-shadow-blocker-search', 'compact_cross5', '--shadow-transparent-sampling', 'temporal_one',
    '--reflection-screen-steps', '96')
& $perfPython -B tests/smoke/stress_timing_smoke.py @perfCommon --caustic-photon-grid-divisor 2 --software-shadow-capture-cadence every_frame --minimum-fps 60 --output-directory "$perfOutput/hardware"
& $perfPython -B tests/smoke/stress_timing_smoke.py @perfCommon --application-arg=--disable-hardware-ray-tracing --caustic-photon-grid-divisor 4 --software-shadow-capture-cadence reuse_one_frame --minimum-fps 60 --output-directory "$perfOutput/software"
ctest --test-dir $perfBuild -C opt -j 1 -L software_raytracing --output-on-failure
```

`--minimum-fps 60` requires **strictly greater** count/time FPS; equality fails. Below-target acquisitions retain the complete result and raw evidence but return failure. Without this option, successful acquisition only establishes valid measurement and workload/settings checks. The threshold rejects `--cpu-diagnostics` and `--reflection-diagnostics`; run those separately without a performance target. The final default-profile measurements and direct-launch checks are recorded above.

True no-RT runs must log both the pre-device disable decision and `RayQuery=0 RayTracingPipeline=0 RayTracingAccelStruct=0 AccelStructDescriptors=0 AccelStructLayout=0`. This exercises the real logical-device route on the capable adapter, not a per-effect flag; other renderer device requirements still apply. See [software ray tracing qualification](../tests/smoke/software_raytracing.md).

For reference controls explicitly select reference coverage, nine-tap blockers, EveryFrame, ReferenceThree, and photon divisor 1 while keeping bodies, resolution and motion identical. Inspect actual moving/fixed-pose captures for overlapping thickness, contact gaps, self-shadow acne, point-face seams, diagonal blockers, temporal trails and caustic detail. The averaged target result does not remove those visual tradeoffs or establish performance for independently deforming characters.
