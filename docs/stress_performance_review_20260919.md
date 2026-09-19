# Stress renderer performance review — 2026-09-19

This change removes unused rendering experiments and reuses accepted object-space geometry when a runtime mesh's pose has not changed. It does not reach the 60 FPS target. Transparent shadow transmission and repeated raster geometry generation remain the first architectural targets; surfel GI is a much smaller measured cost.

## Workload and measurement

The checked-in `stress_test_project.cpp` creates **10 bodies: five transparent and five opaque**, plus the surrounding box. This differs from the requested 20-body workload. Results below apply only to the checked-in 10-body scene; they must not be presented as 20-body performance.

- Native Windows ARM64 `opt` build on Qualcomm Adreno X2-90.
- 1280 × 900; camera and body yaw fixed at 0.6 radians; simulation delta 1/60 second.
- AVBOIT, reflection, refraction, caustics, opaque shadows and surfel GI remain enabled.
- Native mesh shaders are unavailable on this device. The engine uses its compute-emulated mesh stage and pixel shaders.
- Five seconds of warmup and 30 seconds of accepted native presentation counting per process, using `tests/smoke/stress_timing_smoke.py`.
- GPU validation is excluded from performance measurements. Builds, cooks and GPU tests run serially.
- The baseline executable and authored asset volume were frozen from `126374920`; cleanup is `a941b326a`. Binary and authored resource hashes are recorded before and after every timing run.

Local logs, captures, executable identities and rejected experiment sources are under `__artifacts/stress_60fps/cleanup_20260919/`. Those artifacts are ignored by Git.

## Retained changes

The cleanup removes disconnected optics performance/geometry descriptors, a disabled wave-reduction branch, an unused hardware-transmission include, dead crossing helpers and an unproduced indexed-output branch. Active coincident-volume handling, expanded mesh output, software traversal and graph rejection guards remain.

`MeshSkinningDeformationState` owns exact accepted and pending GPU joint palettes in the skinning domain. It compares the resolved palette, edit revision, skinning mode and resource generation. All 17 source/output buffer identities participate in resource replacement detection. Accepted unchanged output skips skinning, bounds and attribute repacking. World-transform changes do not invalidate object-space geometry.

The runtime mesh descriptor publishes an accepted geometry content revision. Revision zero means unknown or pending and forces conservative work. The ray-tracing domain keeps independently accepted revisions for the hardware BLAS and software mesh BVH. Only existing submission-acceptance callbacks publish reusable acceleration contents; rejection, resource replacement and topology changes invalidate them. Scene-level transforms and acceleration updates continue normally.

## Measurements

Two baseline/candidate pairs produced:

| Native presentation measurement | Before | After |
| --- | ---: | ---: |
| Pair 1 | 13.7046 FPS | 14.5305 FPS |
| Pair 2 | 13.6138 FPS | 14.7420 FPS |
| Aggregate presentations / aggregate seconds | 13.6592 FPS | 14.6363 FPS |
| Aggregate wall time per presentation | 73.211 ms | 68.323 ms |

The observed aggregate gain is **7.15%**. These are two short controlled comparisons, not a statistical confidence interval or a thermal-soak result. The target remains unmet.

Observed first-pair GPU timings:

| Scope | Before | After |
| --- | ---: | ---: |
| Frame envelope | 71.218 ms | 68.753 ms |
| Transparent shadow trace | 38.494 ms | 38.582 ms |
| Surfel GI envelope | 1.306 ms | 1.328 ms |
| Mesh generation, 30 dispatches per observed frame | 10.884 ms | 10.775 ms |
| Skinning + bounds + packed-attribute updates | 1.736 ms/frame | No steady-state dispatches |

Nested and asynchronous scopes overlap and have different sample counts; these rows are not an additive frame budget. Mesh generation and skinning totals account for multiple dispatches per frame. GPU report selection excludes startup using the presentation warmup plus the timing ring's 32-frame delay and drops the terminal report. Native presentation FPS is the end-to-end wall-clock measure.

## Validation

- Native `opt` build and production asset cook passed.
- `nwb_ecs_graphics_tests`: 396 enabled tests passed, including eight deformation-state tests and six acceleration-update policy tests; 61 existing tests remain disabled.
- `nwb_shadow_kernel_tests`: the existing six-ray transparent-crossing GPU regression passed.
- The complete stress scene passed with `VK_LAYER_KHRONOS_validation` enabled and its debug messenger installed, without renderer errors or validation messages rejected by the harness. This run is separate from the performance results.
- Early, middle and late animated skinned-caustic captures passed with standalone logging. Visual inspection confirms changing geometry and shadows. The CTest wrappers using the network log server failed before a window appeared; equivalent standalone launches succeeded. This change does not claim to fix that logger-launch issue.
- Matching stress captures froze both implementations after 360 rendered frames. Visual inspection found no structural difference; mean absolute RGB channel difference was 0.406/255, with 95% of channel differences at most 1/255. These are image-comparison observations, not a proof of pixel identity or an assessment of pre-existing glass artifacts.
- Source checks passed for UTF-8, CRLF, exact source EOF, whitespace and absence of unnamed namespaces.

## Rejected hardware transparent-shadow experiment

A bounded inline ray-query implementation preserved complete crossing collection, per-instance thickness/absorption, separate opaque sampling and whole-ray software fallback. Native GPU fixtures verified overlapping and coincident volumes, inside-origin rays, nonuniform scale, material variation, ambiguous edges and explicit overflow behavior.

Despite passing correctness tests, its first full-scene implementation measured **2.6333 FPS**, with approximately **349 ms** in transparent tracing. Moving authored material evaluation outside the live query loop improved it to **4.2345 FPS**, still substantially worse than the software baseline. Both versions were removed from production code and tests; their source snapshot and measurements remain only in local artifacts.

The measurements do not identify the hardware cause. Register pressure, private-memory spills, poor occupancy and executing software fallback in the same large shader are hypotheses. They do establish that simply switching this workload to inline ray queries is not a valid optimization on this device.

## Next architecture work

1. **Reuse generated geometry within the transparent pass chain.** Produce compatible regular mesh output once and retain it for refraction capture, AVBOIT occupancy, extinction and accumulation. The material layer already separates generation and raster recording. Eligibility must compare actual geometry shader/variant, streams, deformation, instance/view inputs and culling, and must retain non-aliased output storage. Different pass resolutions/scissors need explicit handling. Custom geometry and CSG retain their current path until their dependencies are represented.
2. **Measure the transparent-ray workload, then test separate gather, optical resolve and fallback dispatches.** Collect candidate-count distributions and fallback reasons. Keep authored material evaluation and full software traversal out of the hardware gather shader. A compact exceptional-ray queue can isolate fallback work. Preserve complete-ray replacement on failure so partial hardware attenuation is never multiplied by a complete software result. Bound intermediate storage: densely storing the maximum hit list for every ray can replace a compute problem with a bandwidth problem.
3. **Use indexed generated vertices.** The current shader already evaluates each meshlet-local vertex once, then expands a 64-byte record per triangle corner. Index by full meshlet-local vertex identity to preserve normal, tangent and UV seams. This targets generated writes, raster fetches and repeated vertex work without changing materials.

60 FPS requires at most 16.67 ms per frame. Even eliminating the approximately 1.3 ms GI envelope would not close the gap. Re-measure after each architectural change before adjusting samples, resolution or temporal quality. None of the unimplemented directions above is a demonstrated guarantee of 60 FPS, particularly for the requested 20-body workload.
