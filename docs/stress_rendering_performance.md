# Stress rendering performance

This guide explains the rotating 20-body stress workload, its explicit quality profiles, renderer tradeoffs and reproducible performance checks. The target gate requires strictly greater than 60 average accepted presentations per second on the selected host; it does not require every frame to finish within 16.67 ms or establish performance for another scene or GPU.

## Workload and measurement

The workload is 1280x900, ten transparent and ten opaque high-detail bodies at full scale, rotating in a fixed bind pose. It includes the ground, colored GI enclosure, directional and point lights, opaque/transparent shadows, AVBOIT, reflection, refraction, caustics, and surfel GI. The bodies are not twenty independently changing skeletal poses. `--characters-per-class 10 --animate` selects this workload; five per class is a different comparison scene.

The `two_rows_v1` signature checks the two rows of ten, 0.72 spacing, 0.18 stagger, depths -0.55/+0.55, camera `(0, 2.7, -7.2)`, pitch 0.25 and vertical FOV approximately 1.0471976 radians. No resolution, body-count, material-effect, or light-count reduction is part of the performance profile.

The harness excludes a five-second warmup and measures approximately thirty seconds with a steady clock. FPS is accepted native presentations divided by elapsed seconds. Each acquisition retains executable/runtime/helper hashes, requested and applied settings, route markers, raw logs, and results. Serialize runs, use fresh output directories, and do not change the executable or assets during acquisition.

## Implementation and tradeoffs

The renderer reduces repeated high-detail geometry and optical work across rays and views while retaining GI and explicit fallback paths for unsupported or unresolved cases.

Eligible engine-owned shared mesh programs cache decoded object-space geometry and use an internal indexed vertex stage. Accepted source revisions and resource identities govern reuse; world transforms and projection remain current during rasterization. Current material selection requires a compatible indexed archive stage and either no CSG or the shared engine mesh program. Other supported geometry paths use compute emulation. Native mesh shaders are not enabled globally. Current GPU mesh roots also drive software scene-BVH leaf refit and internal-bound unions, preserving posed geometry coverage.

On the software route, eligible directional lights use one light-space view and point lights use six. Indexed rasterization captures opaque D32 depth and transparent crossings; compute shades and sorts transparent records once, then resolves receivers. Crossings retain instance and primitive identity, and per-instance integration preserves thickness and overlapping optical boundaries. The budget is sixteen events per texel, twenty bytes per event. Receiver-plane correction handles discrete map self-shadowing.

Invalid views, missing point faces, invalid events, unstable receivers, overflow, unsupported features, and lights outside the map budget retain software BVH traversal. Map resolve and fallback use separate kernels so successful map receivers avoid the traversal working set. Storage admission respects checked addressing, memory budget, and the device's storage-buffer descriptor range. Per-view culling retains uncertain draws.

`ReuseOneFrame` retains depth, counts, shaded events, and GPU-fitted views together for one subsequent accepted frame. It skips upload/clear/fit/capture/shade while still shading current receivers and declaring current BVH/material fallback inputs. The following accepted frame refreshes. Only world transforms may lag; changed geometry revisions, material bytes, light data, instance/boundary ordering, layout, resources, and invalid history force capture. Runtime geometry without an accepted content revision and CSG cannot reuse. Bind-pose world rotation preserves the accepted object-space revision, making this stress scene eligible after initialization.

The shadow feature owns pending and accepted capture tickets and pins their geometry/texture identities. Refresh invalidates old in-place contents before recording; complete recording and successful shadow packet/resource-state acceptance publish the replacement. Rejection, skipped frames, resize, and graph retries cannot extend stale history. Reuse omits the untouched draw-argument import instead of promising an unowned final-state export. Selected-backend regression coverage exercises accepted depth-state ownership through existing public contracts.

One-frame capture reuse can delay moving shadows. Finite map resolution, fitted coverage and five-tap blocker estimation can alter contact detail, penumbrae and edge leakage. Temporal-one transparent sampling uses one sample only after accepted filtered history; bootstrap/reset uses three. These are explicit quality estimates, not image-equivalence claims.

Caustics keep their hardware/software producers and temporal phases. Photon-grid divisor 2 reduces grid area to one quarter; divisor 4 to one sixteenth, preserving energy normalization while changing noise/detail/convergence. All five wavelet dilations remain; large dilations avoid unused shared-memory allocation. Surfel GI retains its shared cache/resolve and backend choice. RT-disabled camera reflection/refraction use screen-space paths and existing miss handling, so arbitrary off-screen transport is unavailable. SSR remains at 96 steps in the performance profile.

Header-first light-view lookup and shared FP32 shadow-resolve coefficients remain.

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

## Build and reproduce

Use the configured Windows ARM64 Clang/Vulkan environment. Keep each executable and its matching cooked runtime together. Choose a fresh `$perfOutput` directory and run the following acquisitions serially without GPU validation or profiling enabled:

```powershell
$perfPython = 'python'
$perfBuild = '__cmake/build/windows-clang-arm64'
$perfRuntime = "$perfBuild/Testing/skinning_culling_benchmark_runtime/opt"
$perfExe = '__exec/windows/arm64/full/opt/stress_test_smoke.exe'
$perfOutput = '__artifacts/stress_rendering_reproduction'
& $perfPython -m launcher build nwb_stress_test_smoke nwb_caustic_sphere_smoke nwb_transparent_multi_smoke nwb_transparent_csg_smoke nwb_skinned_caustic_smoke nwb_gi_test_smoke nwb_ecs_graphics_tests nwb_gpu_task_tests --configure-preset windows-clang-arm64 --build-dir $perfBuild --config opt --jobs 8 --configure always
ctest --test-dir $perfBuild -C opt -j 1 -R '^(nwb_ecs_graphics_tests|nwb_gpu_task_tests|nwb_stress_presentation_timing_analysis_unit|nwb_smoke_cpu_gpu_timing_analysis_unit)$' --output-on-failure
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

`--minimum-fps 60` requires **strictly greater** count/time FPS; equality fails. Below-target acquisitions retain the complete result and raw evidence but return failure. Without this option, successful acquisition only establishes valid measurement and workload/settings checks. The threshold rejects `--cpu-diagnostics` and `--reflection-diagnostics`; run those separately without a performance target.

True no-RT runs must record `Loader: hardware ray tracing disabled before device creation`, show software renderer dispatch, and reject hardware dispatch. This exercises the selected device policy on the capable adapter; tests do not assert native provider feature or descriptor-layout log tuples. Other renderer device requirements still apply. See [software ray tracing qualification](../tests/smoke/software_raytracing.md).

For reference controls explicitly select reference coverage, nine-tap blockers, EveryFrame, ReferenceThree, and photon divisor 1 while keeping bodies, resolution and motion identical. Inspect actual moving/fixed-pose captures for overlapping thickness, contact gaps, self-shadow acne, point-face seams, diagonal blockers, temporal trails and caustic detail. The average-FPS gate does not remove those visual tradeoffs or establish performance for independently deforming characters.

Recorded stress profile, graphics-audit and optical qualification evidence is retained in the [stress rendering measurement and qualification report](reports/stress_rendering_measurement_and_qualification_report.md).
