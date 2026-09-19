# Stress renderer performance review — 2026-09-19

The first step removes unused rendering experiments and reuses accepted object-space geometry when a runtime mesh's pose has not changed. The second step reuses generated geometry across compatible transparent passes. Neither reaches the 60 FPS target. Transparent shadow transmission remains the dominant measured cost; surfel GI is much smaller.

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

## Completed second step: generated geometry reuse

`AvboitGeneratedGeometryReuse` belongs to the AVBOIT domain and lasts for one frame-graph declaration. It captures complete regular, compute-only groups using the known `shared_ms/default/mesh_compute` program. It compares selected instance bytes, mesh-view bytes, all source buffer and descriptor identities, output identities, draw ordering/counts, and the originating culling inputs. An incompatible group clears the retained state. CSG, custom geometry, native mesh-shader draws, and aliased output buffers or descriptor slots retain their existing generation path.

Refraction capture publishes a completion task only after declaring all actual generation/raster pairs. When capture is absent, occupancy can be the first producer. Subsequent matching occupancy, extinction and accumulation passes import the same output as vertex-buffer reads and depend on that producer while retaining their own material uploads, raster pipelines, viewport/scissor and timing. Reuse creates no dummy generator task. Failed fresh output import clears reuse and preserves local generation; failed consumer import rejects graph declaration.

Reusable producers explicitly disable compute scissor culling because the transparent passes use different raster resolutions. Clip-plane and meshlet-frustum culling remain enabled. Every raster consumer retains its real viewport/scissor. This is a conservative generation policy, not a reduction in visual features or sample quality.

The baseline executable/resources were frozen from `863840266`. Two baseline runs and two final candidate runs used the same 1280 × 900, fixed-yaw/fixed-simulation, all-features 10-body scene and native presentation harness described above:

| Native presentation measurement | Baseline | Final candidate |
| --- | ---: | ---: |
| Run 1 | 14.8330 FPS | 15.9836 FPS |
| Run 2 | 14.6806 FPS | 15.9149 FPS |
| Aggregate presentations / aggregate seconds | 14.7567 FPS | 15.9492 FPS |
| Aggregate wall time per presentation | 67.766 ms | 62.699 ms |

The observed gain for this step is **8.08%**, saving **5.067 ms** per presentation. The candidate runs were repeated after the final fallback/style changes; earlier exploratory runs measured 15.9479 and 15.8710 FPS. These short runs do not establish a confidence interval or sustained thermal performance.

The first baseline and final candidate GPU samples show mesh generation falling from **30 to 15 dispatches per observed frame**, and from **10.737 to 4.416 ms per observed frame**. The frame envelope changed from 67.342 to 62.302 ms. Transparent-shadow trace remained expensive at 37.195/38.455 ms, while the surfel-GI envelope measured 1.437/1.264 ms. These scopes overlap and have different asynchronous sample counts; they are not additive.

Artifacts, final binary/resource identities, logs and captures are under `__artifacts/stress_60fps/geometry_reuse_863840266/`. `baseline_run{1,2}` and `qualified_run{1,2}` are the reported measurements. `comparison.json` records aggregate arithmetic; `gpu_summary.json` in each run records the selected scope samples.

Second-step validation:

- Native `opt` builds passed for the ECS tests, GPU graph tests, stress, transparent CSG and refraction smoke executables.
- All **407 enabled ECS graphics tests** and **356 enabled GPU graph tests** passed. This includes 11 new reuse-key tests and two graph tests for capture-present/capture-absent reuse, cross-packet handoff and later aliased writes. The suites retain 61 and 27 existing disabled tests respectively.
- The complete stress presentation harness passed separately with Vulkan validation enabled and a debug messenger installed. Its timing is excluded from the performance table.
- Matching baseline/candidate stress captures froze after 360 rendered frames. Mean absolute RGB channel difference was **0.0713/255**, with 95% of channel differences at most 1/255. Visual inspection found no structural difference. This does not claim pixel identity or fix the pre-existing glass speckles.
- The transparent CSG fixture with `NWB_TRANSPARENT_CSG_DISABLE_CUTTER=1` passed with both optical features disabled, exercising occupancy as the eligible first producer. Re-enabling the cutter passed the existing transparent-region and clipped-void/remaining-geometry image checks.
- The refraction fixture's `stacked` case passed framebuffer readback and log checks with two transformed instances of the same mesh, covering the shared-output fallback. All three native fallback captures used standalone logging.
- All 31 changed C++ source/header files passed UTF-8, CRLF, exact EOF, separator and named-namespace checks; `git diff --check` passed.

## Shadow block reconstruction correction

The shared shadow upsample had a concrete source of screen-space blocks. It rounded each full-resolution location to a half-resolution texel and assigned equal spatial weight to its 3 x 3 neighborhood. With constant receiver guidance, adjacent full-resolution pixels sharing that rounded center therefore received identical values even when the half-resolution input was an affine ramp. Its coordinate mapping also assumed block-center samples, while both trace paths and the geometry cache sample full-resolution pixel index `factor * halfPixel`.

The correction remains in the shadow resolve domain and applies to both scalar opaque visibility and RGB transparent transmission. It uses `fullPixel / factor` coordinates and separable quadratic B-spline spatial weights over the existing 3 x 3 support. All guidance/color moments and the invalid-normal fallback use consistent weighted normalization. Weights vanish at neighborhood transitions; clamped texture coordinates provide boundary extension. The kernel preserves constants and interior affine signals. Zero-weight taps are rejected before loading geometry.

Ray counts, half-resolution tracing, temporal history, wavelet passes, resources, thickness integration and transparent traversal remain unchanged. This is a reconstruction correctness fix. It does not establish that the user's historical hardware-ray silhouette artifact has the same cause, and it does not enable hardware transparent-shadow tracing.

Native resolve tests compile the production scalar/RGB Slang shaders and read back actual GPU output. The initial five tests cover ramp/edge interpolation, scalar overwrite, transparent multiplication into opaque visibility, invalid/incompatible guidance, and odd/tiny extents. Three of those tests failed against the unchanged shader and all five passed with this correction. An additional test covers a nonconstant distance field and an offset receiver against an analytic regularized linear fit, exercising nonzero guidance mean, variance and color covariance. The existing transparent-crossing test is retained.

The existing transparent-multi scene was captured before and after at yaw 0.6, frozen after six rendered frames. Both captures pass the existing transparent-region checks. Visual inspection confirms overlapping colored and opaque ground shadows. These whole-scene checks do not quantify the original reported block artifact; the isolated GPU ramp/edge failures establish the reconstruction defect specifically.

Two frozen-baseline runs from `81bacc004` and two corrected runs used the same all-features 1280 x 900, 10-body stress workload:

| Native presentation measurement | Baseline | Corrected resolve |
| --- | ---: | ---: |
| Run 1 | 15.9293 FPS | 15.9100 FPS |
| Run 2 | 16.0183 FPS | 15.8561 FPS |
| Aggregate presentations / aggregate seconds | 15.9738 FPS | 15.8831 FPS |
| Aggregate wall time per presentation | 62.602 ms | 62.960 ms |
| Opaque resolve, mean per GPU range | 1.637 ms | 1.874 ms |
| Transparent resolve, mean per GPU range | 2.249 ms | 2.508 ms |

The observed end-to-end change is -0.57%, or +0.358 ms per presentation. The resolve scopes show a small additional cost, so this change must not be presented as a performance gain or a free fix. These short runs do not establish statistical significance or sustained thermal performance; the GPU scopes overlap and must not be added to wall frame time. Transparent traversal still costs approximately 38 ms per reported trace range.

A recomputed-weight variant passed the same GPU tests and reduced the measured resolve scopes by about 0.04 ms each, but its one full-scene run measured 15.6383 FPS with a slower trace scope. It did not establish an end-to-end benefit and was not retained. The retained implementation computes and stores weights once per output pixel for reuse across light layers.

All seven native shadow GPU tests passed, including the six resolve tests and the retained traversal regression. The production asset cook, ECS graphics suite, overlap capture and separate full stress run with Vulkan validation passed. Final source, stress executable and authored-volume identities match the qualified implementation after recooking. Local evidence is under `__artifacts/stress_60fps/shadow_upsample_81bacc004/`: `baseline_kernel.log` records the original failures; `baseline_run{1,2}` and `qualified_run{1,2}` contain the reported measurements and resource identities; `comparison.json` records the aggregation; and `validation` contains the separate validation run. The rejected simplification is retained only in ignored artifacts.

## Hardware transparent-shadow replacement

The accepted hardware replacement separates intersection collection from authored material evaluation into actual compute dispatches. Unlike the rejected inline experiment above, the gather shader contains neither material dispatch nor a private hit array nor software traversal. It uses the existing triangle BLAS/TLAS, selects the transparent instance mask, and writes full-precision distance, instance ID, primitive ID and barycentrics into scratch planes. Hardware devices no longer prepare the second mesh/scene software BVH for transparent shadows. Devices without hardware ray queries retain their software route.

Thickness remains a renderer calculation: intersections are ordered by instance, distance and primitive, clustered only within the same instance, and integrated into each object's path length. Separate coincident instances therefore retain separate optical contributions. Same-instance duplicate faces do not add extra interfaces. Authored closed-volume modes use boundary orientation to handle rays starting inside or ending inside a volume; unspecified materials retain their previous singleton/odd-crossing policy. Project-owned material surface hooks still supply absorption and refraction parameters.

The fast gather stores twelve crossings per ray. A thirteenth crossing queues the pixel for a hardware continuation pass. That pass re-enumerates the original segment and selects successive events in a strict order; it does not advance a shared ray minimum past coincident objects, truncate the remaining volume list, or allocate a software BVH. Repeated traversal is intentionally isolated to overflow rays. Scratch is reused across light slots and samples: at 1280 x 900, the half-resolution crossing allocation is 70,272,000 bytes, with a 1,152,000-byte overflow list and 16-byte argument buffer. This is a memory tradeoff, not a free optimization.

The new feature state and recording code live in the raytracing domain. The frame graph imports its scratch, hardware scene/material/optical data and geometry reads, and retains accepted cross-frame scratch state. Reflection, refraction and shadows reuse one optical-scene import. Shadow sample counts, resolution, temporal filtering and the corrected upsample are unchanged. Gather, evaluate and continuation have separately reserved GPU timing scopes.

Three new native GPU tests cover 22 analytic scenes using real BLAS/TLAS resources and no software BVH: distinct absorption tints, separated/overlapping/coincident objects, reversed instance and primitive order, four volumes, nonuniform and mirrored scale, opaque-mask rejection, misses, shared triangle diagonals, duplicate faces, a tangent/near-outside/thin edge of A over B, inside origins, finite light segments, legacy unspecified behavior, seventeen/eighteen boundaries and eight overlapping instances. Both the gathered result and full hardware continuation are checked against analytic RGB expectations; overflow status, queue entry and dispatch-group count are read back. All ten shadow-kernel tests pass, including the retained traversal and six reconstruction tests.

The overlap and stress scenes were captured with matched yaw and rendered-frame counts. Their mean absolute RGB channel differences from the frozen software baseline are 0.0258/255 and 0.0985/255 respectively; the 95th percentile is 0/255 and 1/255. The overlap result uses the final capture after cleanup; the stress image comparison preceded behavior-preserving cleanup. Visual inspection found no structural difference in these scenes. These comparisons retain pre-existing glass surface artifacts and do not establish pixel identity or universal silhouette behavior on every driver.

The full stress scene also passed separately with `VK_LAYER_KHRONOS_validation` enabled and a debug messenger installed. Local evidence is under `__artifacts/stress_60fps/hardware_shadow_0d0e6b668/`; the frozen baseline is `0d0e6b668`. The performance comparison continues to use the current 10-body scene (five opaque, five transparent), not the originally requested 20-body workload.

Two frozen-baseline runs and two final hardware runs used the same all-features 1280 x 900 scene, fixed yaw/simulation, five-second warmup and thirty-second native presentation interval:

| Native presentation measurement | Software baseline | Hardware replacement |
| --- | ---: | ---: |
| Run 1 | 16.0135 FPS | 30.3519 FPS |
| Run 2 | 15.6980 FPS | 30.4437 FPS |
| Aggregate presentations / aggregate seconds | 15.8558 FPS | 30.3978 FPS |
| Aggregate wall time per presentation | 63.068 ms | 32.897 ms |

The observed improvement is **91.71%**, saving **30.171 ms** per presentation. The initial hardware run measured 30.3051 FPS; the final runs include dead-hybrid-path cleanup and the additional timing reservations. These short runs do not establish sustained thermal performance or a confidence interval. This step does not reach the 16.67 ms / 60 FPS target.

Transparent trace ranges fell from 38.013/39.198 ms to 8.517/8.452 ms. In the first final run, six dispatches per frame yield approximately 1.446 ms of gathering, 0.645 ms of optical evaluation and 6.408 ms of hardware continuation. The stage sum is consistent with the trace envelope, but other nested/asynchronous scopes have different sample counts and must not be added to it. The GI envelope remains about 1.5 ms; it is not the dominant cost in this measurement. The remaining continuation cost is a concrete next target.

The disconnected hybrid preparation task, software-tail geometry declarations, material-context restore snapshots, readiness flags and backend-order comparator were removed. The no-RT software build/trace route and its accepted upload/acceleration lifecycle remain covered. The final regression run passes 405 enabled ECS graphics tests (61 existing disabled), 356 GPU graph tests (27 existing disabled), four material/graph integration contracts, all ten native shadow tests, and two native immutable-buffer upload tests. Two obsolete hybrid-only unit tests were removed with their deleted production behavior. The CPU smoke/benchmark checks pass 60 renderer A/B tests, 45 optical-smoke tests and both boundary harness self-tests.

Current hardware smoke expectations require the new hardware transparent dispatch marker and reject software traversal. Both the final transparent-multi capture and the frame-lagged async-lighting capture pass those route checks; the latter also runs with GPU validation and rejects validation errors. Frozen-build benchmark parsers preserve historical hybrid route identity explicitly, while current route checks are strict. The compiled gather module contains no private hit array or material resources, and the compiled evaluator contains no ray-query instructions or capability. Source checks cover UTF-8, CRLF, exact source EOF, whitespace and named namespaces.

Two implementation limits remain explicit. The engine currently creates one triangle geometry per BLAS, which is the material/primitive indexing contract. Forward intersections cannot infer occupancy for a finite segment wholly inside a volume with no boundary crossing; the previous software path also lacked that information. At exact silhouettes, paired opposite-facing reports are treated as a tangent, but a lone exit remains ambiguous with an inside-origin exit. The tested tangent cases pass on Adreno X2-90; this is not a claim that every hardware intersection convention resolves the ambiguity.

## Gather capacity tuning, 2026-09-20

The first hardware replacement spent approximately three quarters of transparent trace time completing rays that exceeded its twelve-crossing gather buffer. Raising the capacity to **24** lets more rays use the separate, bounded material-evaluation pass. The complete hardware continuation remains in place for a twenty-fifth candidate; no crossings are discarded. Canonical event ordering, instance clustering, material math, ray counts, sampling and denoising are unchanged. A shared compile-time guard now restricts capacity to 1 through 31, matching the evaluator's 32-bit remaining-event mask.

This increases the full-frame crossing allocation at 1280 x 900 from **70,272,000 to 139,392,000 bytes**, an additional **69,120,000 bytes**. Overflow-list storage remains 1,152,000 bytes plus sixteen argument bytes. Rays with at most twelve events still touch the same number of record planes and process the same actual event count; allocated capacity is not an unconditional doubling of memory traffic.

The frozen baseline is `208998137`. The test order was baseline, candidate, candidate, baseline, with the same all-features 1280 x 900, 10-body stress workload, fixed yaw and simulation, five-second warmup and thirty-second native presentation measurement:

| Measurement | Capacity 12 | Capacity 24 |
| --- | ---: | ---: |
| Run 1 | 30.4050 FPS | 37.9811 FPS |
| Run 2 | 30.3117 FPS | 37.9822 FPS |
| Aggregate presentations / aggregate seconds | 30.3583 FPS | 37.9816 FPS |
| Aggregate wall time per presentation | 32.940 ms | 26.329 ms |
| Transparent trace, first run mean per range | 8.438 ms | 2.232 ms |
| Continuation, first run work over six dispatches | 6.362 ms | 0.0186 ms |

The observed gain is **25.11%**, saving **6.611 ms per presentation**. Gather and evaluation together account for approximately 2.20 ms of the candidate's trace work; continuation is now small in this scene. Nested/asynchronous GPU scopes have different sample counts and must not be added into a frame budget. These short tests are not a sustained thermal qualification. **60 FPS remains unmet**, and these results continue to cover five opaque plus five transparent bodies rather than the requested 20-body scene.

The native fixture now derives its dense geometry from the shared capacity and covers **24 analytic scenes**. New cases exercise exactly 24 gathered crossings and the twenty-fifth crossing entering continuation. Dense 30/29-boundary paths and 28 crossings through distinct overlapping instances retain real hardware-overflow coverage. Tests still compare both gathered/selected and complete continuation output to analytic RGB, and read back the overflow sentinel, route, queue index and dispatch-group count. All ten native shadow tests pass, including the six reconstruction tests and retained software traversal regression. Production shader cooking and native builds passed; the separate complete-scene Vulkan validation run passed with the validation layer and debug messenger enabled. The validation timing is excluded from the comparison.

Matched stress captures freeze at yaw 0.6 after 360 rendered frames. Visual inspection found no structural change; mean absolute RGB channel difference is 0.0995/255 and the 95th percentile is 1/255. These are observations from the captured scene, not a claim of pixel identity or a fix for pre-existing glass surface artifacts.

Local logs, frozen executable/resources, resource identities and arithmetic are under `__artifacts/stress_60fps/continuation_208998137/`. `baseline_run{1,2}`, `capacity24_run{1,2}`, `comparison24.json`, `native_all24.log`, `capture_comparison.json` and `validation24` contain the evidence. The next memory improvement is to reuse the same capacity-24 storage across bounded row tiles; this requires independent validation of global pixel/jitter coordinates, partial tiles and overflow, and is not part of this capacity change.

## Next architecture work

1. **Bound hardware shadow scratch memory.** Reuse capacity-24 storage across row tiles with a fixed byte budget. Keep global receiver/jitter/output coordinates and complete overflow integration intact. Measure dispatch overhead and total frame time separately from the already-demonstrated capacity gain.
2. **Use indexed generated vertices.** The current shader already evaluates each meshlet-local vertex once, then expands a 64-byte record per triangle corner. Index by full meshlet-local vertex identity to preserve normal, tangent and UV seams. This targets generated writes, raster fetches and repeated vertex work without changing materials.

60 FPS requires at most 16.67 ms per frame. Even eliminating the approximately 1.3 ms GI envelope would not close the gap. Re-measure after each architectural change before adjusting samples, resolution or temporal quality. None of the unimplemented directions above is a demonstrated guarantee of 60 FPS, particularly for the requested 20-body workload.
