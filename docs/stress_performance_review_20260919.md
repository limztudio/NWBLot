# Stress renderer performance review — 2026-09-19

**Qualification correction, 2026-09-20:** the optical-path audit of `c9e594cfe` and its predecessors found that this stress scene rejected every admitted optical reflection path before issuing a hardware query. The current-pose bounds step at the end of this report removes that global rejection, with updated timing and remaining optical limitations reported separately. Earlier references to an "all-features" scene mean that the settings and passes were enabled, not that optical reflection transport succeeded. The recorded presentation rates and A/B improvements remain measurements of that effective workload; they do not qualify complete optical reflection or the original twenty-body target. Primary refraction and transparent shadows use separate traversal policies and are not shown to fail by these reflection counters.

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

Local logs, frozen executable/resources, resource identities and arithmetic are under `__artifacts/stress_60fps/continuation_208998137/`. `baseline_run{1,2}`, `capacity24_run{1,2}`, `comparison24.json`, `native_all24.log`, `capture_comparison.json` and `validation24` contain the evidence. The subsequent bounded-row-tile experiment is recorded below; it is not part of this capacity change.

## Rejected bounded shadow scratch experiment, 2026-09-20

A capacity-24 row-tile experiment limited crossing storage, the overflow list and indirect arguments to a combined 64 MiB budget. At 1280 x 900, this selected 208 + 208 + 34 half-resolution rows and reduced the actual allocation from 140,544,016 to 64,962,576 bytes (53.78%). Global receiver, jitter and output coordinates were preserved while scratch used local tile coordinates. The seven allocation-policy tests and complete 412-test enabled ECS suite passed for this experiment.

The baseline/candidate/candidate/baseline presentation comparison measured 38.1073/38.2020 FPS for the original implementation and 37.8365/37.8444 FPS for tiling. Aggregate performance changed from **38.1546 to 37.8405 FPS**, a **0.82% regression**, or **0.218 ms additional wall time per presentation**. Transparent trace increased from approximately 2.217 to 2.417/2.422 ms. A subsequent experiment removing the barrier between disjoint gathered/overflow output writes measured 37.6356 FPS and did not establish an improvement.

The memory saving is real, but this change was **not retained** because the current target is frame rate. Production and test sources were restored to `4d4eb56c7` before the next experiment. The final tiled production-entrypoint test fixture was written but was not built or executed; no claim of complete tile correctness qualification is made. Source snapshots, patch, allocation tests, measurements and the frozen baseline remain in ignored local artifacts under `__artifacts/stress_60fps/tiles_4d4eb56c7/`. The current production scratch allocation therefore remains unchanged.

## Specialized shadow resolve stages, 2026-09-20

Opaque scalar visibility and transparent RGB visibility previously each used one shader for wavelet filtering and full-resolution upsampling, selected by a runtime push constant. Both compiled shaders consequently carried the wavelet shared-memory declarations into the upsample pipeline. The accepted implementation cooks explicit wavelet and upsample variants of each existing asset and selects the corresponding pipeline at dispatch. Filter equations, precision, tap counts, ray counts, temporal behavior, reconstruction, transparent multiplication and the 84-byte push-constant ABI are unchanged.

The resolve feature now owns its shared layout, scalar/RGB stage shaders, pipelines and failure flags in `raytrace/soft_shadow_resolve_state.h`. A channel is ready only when both stages exist; resource invalidation resets the complete feature owner. Variant keys are explicit and each stage has its own shader handle, avoiding the loader's existing-handle fast path mixing variants.

Production SPIR-V inspection confirms that both upsample variants contain **zero workgroup variables and zero control barriers**. Scalar/RGB wavelet variants retain four shared arrays and two barriers. Wavelet binaries are 23,684/24,244 bytes, and upsample binaries are 15,320/16,080 bytes. This establishes removal of unused shader resources; it does not directly measure native register allocation or occupancy.

The frozen baseline is `4d4eb56c7`, using the binaries/resources preserved before the rejected tile experiment. A fresh baseline/candidate/candidate/baseline comparison used the same all-features 1280 x 900, five-opaque/five-transparent scene, fixed yaw/simulation, five-second warmup and thirty-second native presentation interval:

| Measurement | Combined-stage shader | Specialized stages |
| --- | ---: | ---: |
| Run 1 | 38.1156 FPS | 39.7573 FPS |
| Run 2 | 38.0903 FPS | 39.4633 FPS |
| Aggregate presentations / aggregate seconds | 38.1030 FPS | 39.6103 FPS |
| Aggregate wall time per presentation | 26.245 ms | 25.246 ms |
| Opaque resolve, first run mean per GPU range | 1.769 ms | 1.438 ms |
| Transparent resolve, first run mean per GPU range | 2.422 ms | 1.713 ms |

The observed end-to-end improvement is **3.96%**, saving **0.999 ms per presentation**. Both repeated resolve measurements support the intended reduction. The full trace and material features remain enabled. Nested/asynchronous scopes have different sample counts and must not be summed into the wall-frame budget. These short runs do not establish sustained thermal performance or a confidence interval. **60 FPS remains unmet**, and the original twenty-body target is not qualified by these ten-body measurements.

All twelve native shadow GPU tests passed: the six existing upsample cases, two new scalar/RGB wavelet tests, three hardware transmission groups covering 24 analytic scenes, and the retained software traversal regression. Wavelet coverage includes uniform preservation, invalid/mixed guidance, actual edge smoothing, temporal moments, partial workgroups, shared-memory steps 1/4 and direct-texture step 5. Production cooking and native builds passed. All 405 enabled ECS graphics tests passed (61 existing disabled). The separate full-scene Vulkan validation run passed with the validation layer and debug messenger enabled; its timing is excluded from the performance comparison. An initial validation run caught a layout-handle ownership error in the new helper. Passing the original owning handle into pipeline descriptions preserves its arena deleter, and the corrected build completed shutdown without the leak. Source formatting and named-namespace checks passed.

Matched stress captures use yaw 0.6 and freeze after 360 rendered frames. Visual inspection found no structural change; the mean absolute RGB channel difference is 0.0812/255 and the 95th percentile is 1/255. The existing glass surface artifacts remain visible in both captures. This comparison does not establish pixel identity or correctness for every scene. `qualified_stress.bmp` and `capture_comparison.json` preserve the final capture and numerical comparison.

Local evidence is under `__artifacts/stress_60fps/resolve_stages_4d4eb56c7/`: `baseline_run{2,3}`, `qualified_run{1,2}`, `comparison_final.json`, `native_all.log`, `ecs_tests_final.log`, `validation_final` and `compiled_stage_audit.json` contain the final measurements, checks and cooked-module evidence. The final presentation comparison was repeated after the owning-handle correction; the earlier `specialized_run{1,2}` measurements remain exploratory evidence.

## Indexed generated mesh output, 2026-09-20

The accepted shared mesh compute path now writes each complete 64-byte meshlet-local vertex once and emits a 32-bit index per triangle corner. Previously it evaluated each local vertex once but copied the complete record for every corner. Generated vertex identity is `1 + meshlet.localVertexOffset + localVertexIndex`, preserving attribute seams and distinct meshlet identities. Index zero is a fully initialized culled sentinel; rejected triangles index that sentinel without changing culling math or triangle order. Only the engine shared geometry program's default, non-CSG compute variant selects indexed output. Custom shaders, CSG variants and native mesh shading retain their existing contracts.

Compact vertices and indices occupy one existing generated-geometry allocation, keeping descriptor identity, ownership, per-mesh aliasing and cross-pass sharing intact. Allocation size is the larger of legacy expanded storage and aligned compact-vertex-plus-index storage. Persistent allocation size therefore does not decrease; the optimization targets generated writes, shared memory and indexed raster reuse. Allocation arithmetic checks the complete raw index byte range before publishing its 32-bit offset. An 80-byte compute-only push wrapper preserves the existing 64-byte mesh prefix and adds the index offset at byte 64. Raster and AVBOIT push layouts remain unchanged.

The material pipeline explicitly selects indexed draws, while graph consumers request combined vertex/index state on the unified allocation. Retained draw snapshots and opaque, AVBOIT and CSG reuse guards include the output mode and index-region offset. Allocation policy belongs to the mesh domain; generated-resource consumption and draw selection belong to the material domain, with each rendering feature retaining its own graph declarations. No graphics backend workaround, RT triangle-buffer reuse, material approximation, sample reduction or resolution change is involved.

Production SPIR-V inspection confirms that the indexed shader stages only 96 clip positions in shared memory instead of 96 complete vertex records: **1.5 KiB instead of 6 KiB**. The four control barriers remain. Its compiled module is 49,084 bytes versus 61,196 bytes for the retained CSG variant; this comparison establishes shader resources, not native occupancy or register allocation. For a fully visible 96-vertex, 126-triangle meshlet, generated output is 7,656 bytes instead of 24,192 bytes, plus one 64-byte sentinel per draw. That example is a **68.35% reduction in generated bytes**, not a measurement of total DRAM traffic.

The frozen baseline is `6c03537fbc841a8065a13b0ad3abc0aa4f4efcac`. Baseline/candidate/candidate/baseline runs used the same all-features 1280 x 900 scene, fixed yaw 0.6 and simulation step 1/60, five-second warmup and thirty-second native presentation interval. The workload still contains **five opaque and five transparent bodies**, with five additional room instances; it is not the originally requested twenty-body stress scene.

| Measurement | Expanded output | Indexed output |
| --- | ---: | ---: |
| Run 1 | 39.5261 FPS | 42.4935 FPS |
| Run 2 | 39.7695 FPS | 42.5154 FPS |
| Aggregate presentations / aggregate seconds | 39.6478 FPS | 42.5044 FPS |
| Aggregate wall time per presentation | 25.222 ms | 23.527 ms |
| Mesh generation, first run work over fifteen dispatches | 4.277 ms | 3.914 ms |
| Opaque regular pass, first run mean per GPU range | 2.760 ms | 2.335 ms |
| AVBOIT occupancy, first run mean per GPU range | 0.386 ms | 0.147 ms |
| AVBOIT extinction, first run mean per GPU range | 0.469 ms | 0.195 ms |
| AVBOIT accumulation, first run mean per GPU range | 0.473 ms | 0.248 ms |

The observed end-to-end gain is **7.20%**, saving **1.695 ms per presentation**. Shadow visibility, transparent trace, resolves and GI remain approximately stable. The individual `render.raster` timing ranges are below the useful measurement floor; the table uses their enclosing material passes instead. Nested/asynchronous scopes and differing sample counts must not be added into the wall-frame budget. These short runs do not establish sustained thermal performance or a confidence interval. **60 FPS remains unmet**, and the original twenty-body workload remains unqualified.

Production asset cooking and all selected native builds passed. All **412 enabled ECS graphics tests** passed, with 61 existing disabled tests. Two native mesh tests cover **84 GPU cases**, expanding the actual generated indices on readback and comparing every byte of each 64-byte vertex against the frozen expanded-output oracle. Coverage includes resolved streams, seams, culling, mixed visibility, overdispatch, empty input, sentinel identity, exact indices and unwritten-region guards. Four allocation-policy tests cover padding, legacy capacity, invalid sizes and the final addressable index boundary.

All **nine selected native graph tests** pass with validation, covering real indexed binding of one allocation as vertex and index data, AVBOIT and CSG handoffs, and shared-output alias ordering through pairs, triples and quadruples. During fixture qualification, validation exposed an uninitialized test color-image layout. The fixture now establishes the imported layout before graph execution. Failure diagnostics also print validation messages captured at warning log level. These are test-fixture corrections, not production backend changes.

The separate complete stress scene passed with `VK_LAYER_KHRONOS_validation` and a debug messenger enabled; its timing is excluded from the performance comparison. CSG late-scene and frame-lagged asynchronous-lighting captures also passed their scene assertions with GPU validation. Matched baseline/indexed stress captures use yaw 0.6 and freeze after 360 rendered frames. Visual inspection found no structural change; mean absolute RGB channel difference is **0.0935/255** and the 95th percentile is **1/255**. Existing glass surface artifacts remain in both captures, so this comparison does not establish pixel identity or fix those artifacts.

Local evidence is under `__artifacts/stress_60fps/indexed_mesh_6c03537fb/`: `baseline_run{1,2}`, `indexed_run{1,2}`, `comparison_final.json`, `compiled_mesh_audit.json`, `native_mesh.log`, `ecs_tests.log`, `native_graph_qualified.log` and `validation` contain the accepted measurements and checks. Captures are `baseline_stress.bmp`, `indexed_stress.bmp`, `csg_late.bmp` and `async_lighting.bmp`; `capture_comparison.json` records the numerical stress-image comparison. Earlier graph logs preserve the fixture failures; only `native_graph_qualified.log` is the final clean graph qualification.

## Rejected guided-fit coefficient factoring, 2026-09-20

A bounded experiment factored the original FP32 guided upsample regression into signed per-tap coefficients, computed once before the light-layer loop. It retained the same guidance, tap masks/order, negative extrapolation, precision, fallback, final clamp and transparent multiplication. The coefficient for a valid tap was `w_i / W * (1 - meanG / (varianceG + epsilonSquared) * (g_i - meanG))`. This is equivalent over real numbers but reassociates FP32 arithmetic. The coefficient array was explicitly initialized to satisfy Slang's definite-assignment analysis; the initial uninitialized version did not compile and was never measured.

The frozen baseline is `76139ecf8`. The same all-features 1280 x 900, ten-body scene used five-second warmup and thirty-second native presentation intervals, in baseline/candidate/candidate/baseline order:

| Measurement | Original regression | Factored coefficients |
| --- | ---: | ---: |
| Run 1 | 42.5243 FPS | 42.4105 FPS |
| Run 2 | 42.4842 FPS | 42.4692 FPS |
| Aggregate presentations / aggregate seconds | 42.5043 FPS | 42.4398 FPS |
| Aggregate wall time per presentation | 23.5271 ms | 23.5628 ms |
| Opaque resolve, first run mean per GPU range | 1.4406 ms | 1.4634 ms |
| Transparent resolve, first run mean per GPU range | 1.7172 ms | 1.7361 ms |

The candidate did not establish an improvement: aggregate FPS changed by **-0.15%**, adding **0.0357 ms per presentation**. Both candidate runs showed modestly higher resolve times while unaffected controls were approximately stable. These short measurements do not establish statistical significance, but provide no reason to retain the rewrite. The original production shader was restored byte-for-byte from the frozen baseline. The rejected source, build logs, all four runs, GPU summaries and `comparison.json` remain under `__artifacts/stress_60fps/guided_fit_76139ecf8/`.

The numerical regression coverage is retained. A new native test runs **33 dispatches** across eleven profiles and scalar overwrite, RGB overwrite and RGB folding. It checks all 17 x 13 x 8 output texels, including distinct active layers 2 through 4 and untouched sentinels in the other layers. Its CPU oracle retains the original weighted moments/covariance equations over half-quantized inputs; analytic assertions also require signed extrapolation, both final clamp endpoints and the expected unclamped affine channel. Profiles cover near/far receivers, low/zero guidance variance, large offsets, constant signals, mixed invalid/normal-rejected taps, a single guided tap, boundaries, background and partial workgroups. Helper declarations and implementation are split within the native shadow-test domain. All **thirteen native shadow tests** pass on both the rejected candidate and the restored production shader, recorded in `native_factored.log` and `native_restored.log`.

## Refraction timing and remaining targets, 2026-09-20

The refraction recorder now measures its actual dispatch under `render.avboit.refraction_resolve`, matching the existing graph task identity. The renderer reserves the timestamp pair and the AVBOIT smoke probe includes the scope in its existing in-flight reservation policy. The packet-owned recording scope continues to handle submission acceptance and lifetime; no extra task tickets or scheduler callbacks are introduced. Both hardware and screen fallback dispatches are measured, while the separate clear/no-work path produces no resolve sample. The shader assets are unchanged, and their packed volume hash matches the frozen `76139ecf8` baseline.

Two all-features stress runs publish **1,170 refraction samples each**, after warmup and delayed-query exclusion. The dispatch averages **1.3393 ms** and **1.3501 ms**. Native presentation rates are 42.4297 and 42.4140 FPS, aggregating to **42.4218 FPS / 23.5728 ms**. This is instrumentation qualification, not an accepted rendering speedup. The earlier frozen baseline measured 42.5043 FPS; these short runs do not separate the small difference from timing overhead or system variance.

| Mean per reported GPU range | Instrumented run 1 | Instrumented run 2 |
| --- | ---: | ---: |
| Shadow visibility envelope | 6.3633 ms | 6.3633 ms |
| Reflection hardware | 3.1587 ms | 3.1190 ms |
| Reflection classification / SSR | 1.1304 ms | 1.1258 ms |
| Refraction resolve | 1.3393 ms | 1.3501 ms |
| Surfel resolve | 0.9415 ms | 0.9602 ms |
| Surfel ray tracing | 0.1350 ms | 0.1430 ms |

Scopes include nested and asynchronously scheduled work with different sample counts, so they must not be summed into a wall-frame budget. The measured surfel ray-tracing scope is small relative to reflection and reconstruction in this ten-body scene. This does not establish the same balance for the requested twenty-body scene or for a different GI workload.

The instrumented production builds and asset cook passed, along with all **412 enabled ECS graphics tests** (61 existing disabled). The separate complete stress run passed with the Khronos validation layer and hardware transparent-shadow route enabled, with no validation errors. Its FPS is excluded from the table and performance aggregation. The retained thirteen native shadow tests also pass after restoring the original shader. Evidence is under `__artifacts/stress_60fps/guided_fit_76139ecf8/`: `instrumented_run{1,2}`, `instrumented_summary.json`, `instrumented_validation`, `build_instrumented.log`, `ecs_instrumented.log` and `native_restored.log`.

## Optical reflection eligibility and correctness audit, 2026-09-20

The proposed exterior-reflection specialization is not implemented. Before changing routing, an opt-in diagnostic proves whether an entire normalized reflection segment, including its origin, lies outside the frozen transparent bounds. The proof accepts only strict coordinate separation after conservative FP32 uncertainty padding; overlapping intervals, missing or invalid metadata, invalid rays and boundary contact remain unproven. It reads no material data and issues no ray query. Native GPU coverage exercises 77 cases, including an independent FP64 slab oracle, mandatory successful proofs, tangency, hidden initial-medium intersections, normalization, invalid metadata and overdispatch guards.

The new `exteriorEligibleRays` diagnostic reuses the reserved last word of the existing 96-byte counter ABI. It runs only when reflection diagnostics are enabled and does not change admission, optical traversal, queues or normal benchmark configuration. The stress harness exposes `--reflection-diagnostics`; it clears inherited diagnostic controls unless explicitly requested. Readback samples are deduplicated by accepted sequence and generation. The harness checks the producer's numeric and ordering contracts, saves sums and weighted ratios with generation/frame ranges, and reports ordinary performance runs as optical support `not_measured`. Query activity alone is never reported as complete optical correctness.

A dedicated run at frozen baseline `ef31f0b88` plus diagnostics produced 1,495 accepted samples. All 1,195 samples with graphics frame at least 300 reported the same values:

| Per accepted sample | Count |
| --- | ---: |
| Validated admitted hardware-path invocations | 50,067 |
| Exterior-eligible rays | 0 |
| Actual hardware queries | 0 |
| Bootstrap events | 0 |
| Transparent paths traversed | 0 |
| Unsupported optical paths | 50,067 |

That is **100% unsupported and zero hardware traversal** over 59,830,065 retained admitted invocations. `hardwareRays` is incremented before the optical trace call, so it must not be interpreted as actual ray-query count. The approximately 3.1 ms hardware reflection timing in the earlier table measures the selected dispatch, reconstruction and rejection work; it does not establish the cost of successful optical transport. Rejected traces return zero radiance here, without an environment fallback.

The deterministic rejection chain is `rt_swbvh.cpp` excluding runtime/skinned meshes from optical bounds, `optical_scene.cpp` clearing the scene bounds-valid flag for any transparent instance without valid bounds, and `optical_bootstrap.slangi` rejecting that flag before its first query. Existing CPU skinning bounds describe the bind pose and are deliberately not finite current-pose bounds. Simply treating those bounds as valid would be incorrect for deformation.

There is also a separate authored geometry issue. A reproducible edge audit of `tests/smoke/assets/characters/body.nwb` finds 26,812 positions, 28,887 attribute vertex references, 51,652 triangles, 167 connected components and **1,632 open boundary edges**. Attribute seams are joined through their position references, and exact FP32 position welding changes none of these counts. The source SHA-256 is `89969c3cdc866facf63bc7f97c45f565ff18ec4b8df4bd7f3934f74d0bbcfaf8`. Five components have no boundary edges; this is not a self-intersection or valid optical-volume certificate. The complete body cannot be declared a closed glass volume from these results. Its renderer currently retains `Unspecified` optical boundary mode despite a non-unit material IOR, so publishing runtime bounds alone would not establish a supported optical contract.

Primary AVBOIT refraction directly traces surface hits and same-instance exits through its own bounded resolve policy; it does not call this optical bootstrap. Hardware transparent shadows likewise have a separate transmission path. The reflection audit therefore does not establish that refraction, caustics, shadows or GI are disabled or failing.

## Reflection spatial HDR correction, 2026-09-20

Running the full native reflection suite exposed preexisting FP16 accumulation differences against the frozen FP32 shader reference. FP16 weighted radiance sums can also overflow even when the final normalized HDR color fits in RGBA16F. Commit `54bc6e910` restores FP32 weighted multiplication, numerator, denominator and normalization, retaining the half-precision LDS cache, tile eligibility and radius specializations.

All original exact comparisons against the unchanged frozen shader now pass. Thirty-five added uniform HDR cases cover dynamic and specialized radii with input `(32768, 16384, 8192)`. They require finite output, exact agreement with the frozen reference, unchanged diagnostic alpha and at most one adjacent FP16 value of analytic constant-preservation error. Exact symbolic cancellation is not assumed across target division and half-storage rounding. The initial stricter constant assertion failed by one half value in radius-three cases; that new assertion was corrected without weakening any existing comparison.

All four native reflection tests, all 412 enabled ECS graphics tests and all 22 Python measurement tests pass; 61 preexisting ECS tests remain disabled. The accumulation change is a correctness fix, not a claimed speedup.

## Final timing and diagnostic qualification, 2026-09-20

Two frozen-baseline runs measured 42.5482 and 42.6975 FPS, aggregating to **42.6228 FPS / 23.4616 ms**. Two final-build runs measured 43.2919 and 43.4462 FPS, aggregating to **43.3690 FPS / 23.0579 ms**. These use the same ten-body 1280 x 900 scene, five-second warmup and thirty-second native-presentation interval; diagnostics and GPU validation were disabled for those four runs. Binary/resource/helper identities remained unchanged within each acquisition. The final build includes both the HDR correction and the dormant diagnostic branch, so this comparison does not isolate their effects. The hardware reflection dispatch declined to approximately 2.63–2.66 ms, but the optical path still rejects before traversal. No full-feature speedup or 60 FPS qualification is claimed.

A separate complete stress run passed with the Khronos validation layer and debug messenger enabled, with no reported validation errors. Its 1,516 accepted diagnostic samples report `all_rejected`, unsupported ratio 1.0, exterior-eligible ratio 0.0 and queries per admitted ray 0.0. Its timing is excluded from the performance comparison. The harness's presentation PASS is explicitly separate from the optical status; it does not certify optical correctness. Evidence is in `final_run{1,2}`, `baseline_run{1,2}`, `comparison_final.json` and `validation_final`, alongside the final native/CPU/Python test logs.

## Architecture plan recorded before the current-pose bounds step

1. **Publish conservative current-pose optical bounds.** Skinning already computes per-meshlet position extrema. Emit finite-valid AABB partials there and reduce them into a skinning-owned bounds resource tied to accepted deformation identity. Expose it through the runtime mesh contract. Raytracing must transform those bounds with the exact TLAS snapshot and build its own GPU-resolved optical metadata, preserving the existing immutable upload and accepted-submission lifetimes. Unchanged accepted poses can reuse local bounds; changed transforms still require a new world union. Avoid CPU readback and do not substitute bind-pose bounds for arbitrary deformation.
2. **Establish an explicit supported optical model for the stress geometry.** Solid glass requires appropriately closed authored geometry and a validated volume contract. Open surfaces need an explicit thin-surface model. This is an authoring/optical-policy decision, not a bounds optimization; the current body remains unchanged pending that choice.
3. **Establish a new baseline with actual optical transport.** Verify queries, termination diagnostics and visuals on the supported scene before evaluating the full-feature 60 FPS goal. Also qualify the requested twenty-body workload; current performance measurements still contain five opaque and five transparent bodies plus five room instances.
4. **Revisit exterior specialization only with measured eligibility.** The present zero-eligible evidence does not justify queue allocation, partitioning or a second resolve dispatch. Once bounds and optical policy are supported, preserve the admitted set and original transport behavior while measuring whether a split can pay for itself.
5. **Continue bounded GI/refraction experiments.** Deferred SH loads for surfel candidates with exactly zero existing weight and targeted profiling of the approximately 1.35 ms refraction resolve remain candidates. Their benefit must be measured, with controls and unchanged quality; neither is a promised route to 60 FPS.

The 60 FPS budget is 16.67 ms. Current approximately 42 FPS measurements still exceed that budget and underrepresent the intended optical workload. The corrected optical scene must be qualified before an honest remaining frame-budget estimate can be made.

Local evidence is under `__artifacts/stress_60fps/reflection_exterior_ef31f0b88/`: the frozen `baseline`, `eligibility_enabled`, `eligibility_summary.json`, `body_topology_audit.json`, standalone `audit_body_topology.py`, `dynamic_optical_bounds_plan.md`, `native_final.log`, `ecs_final.log` and `python_final.log`. The earlier `eligibility` run did not forward the diagnostic environment switch and contains no eligibility evidence; it must not be used as such. Native failure logs are retained alongside the final passing run.

## Completed current-pose optical bounds, 2026-09-20

The hardware optical path now receives conservative bounds from the current accepted GPU deformation. Skinning emits a 32-byte AABB beside each existing meshlet sphere/cone and reduces those partials to one local bounds record. Empty or nonfinite contributions invalidate the complete record. Outward padding covers float rounding and subnormal flushing before arbitrary world transforms. The new reduction runs only when deformation output changes; unchanged accepted poses keep their existing bounds and skip the skinning work.

The runtime mesh contract exposes that record only with a nonzero accepted geometry revision. Its two new source/output identities participate in deformation invalidation and live resource retention, bringing the tracked buffer count to 19. The terminal skinning finalizer publishes the generation after deformation, repacking, partial bounds and reduction have been accepted. New bounds resources preserve their final ShaderResource state rather than restoring Common on command-list close.

Mesh resources own the renderer descriptor and include bounds identity in runtime cache/snapshot matching. The ray-tracing owner retains the exact emitted instance order, accepted bounds resources and frozen TLAS transforms. One primary-Graphics task transforms and unions the bounds, copying immutable CPU metadata into a separate GPU-resolved output. It preserves entities, boundary policy, priority and unrelated flags. A missing or invalid contributor leaves the scene union incomplete. This avoids CPU readback and preserves the immutable upload cache. Previous asynchronous optical readers join the presentation/recovery frontier before a subsequent overwrite. Static-only scenes retain the existing upload path; software shaders do not consume this metadata, so that route retains its original conservative metadata without an unused finalizer pipeline or allocation.

### Measurement and optical qualification

The baseline executable and resource volume were frozen from `c9e594cfe` before building this change. Each process used the existing ten-body scene at 1280 x 900, five seconds of warmup and thirty seconds of accepted native presentation counting. Validation and reflection diagnostics were disabled in performance runs. Executable and packed-volume identities were checked by the harness. This is a changed effective optical workload, so the comparison does not establish a same-quality optimization or regression.

| Measurement | Frozen predecessor | Current bounds integration |
| --- | ---: | ---: |
| Presentation run 1 | 43.2657 FPS | 33.2451 FPS |
| Presentation run 2 | Not repeated in this step | 33.0958 FPS |
| Aggregate current presentation rate | — | **33.1704 FPS / 30.1473 ms** |
| Hardware reflection dispatch, mean GPU range | 2.6817 ms | 9.3504–9.4878 ms |
| Optical bounds finalize, mean GPU range | Absent | **0.00718–0.00724 ms** |
| Surfel GI envelope, mean GPU range | 1.3610 ms | 1.3532–1.3934 ms |
| Shadow visibility envelope, mean GPU range | 6.3601 ms | 6.3582–6.3907 ms |
| AVBOIT refraction resolve, mean GPU range | 1.3474 ms | 1.4703–1.5280 ms |

GPU ranges overlap and have different sample populations; the rows are not an additive frame budget. Scope selection excludes warmup, the timing ring delay and the terminal report. No steady-state skinning/reduction dispatch occurs in this fixed-pose stress run; animated poses do execute it.

A separate stress run passed with the Khronos validation layer and debug messenger enabled. Across 1,056 accepted reflection diagnostic samples, it recorded 52,870,752 admitted rays, 91,791,744 hardware queries, 13,934,976 bootstrap events, 2,424,576 transparent paths and 16,359,552 unsupported paths. The unsupported share dropped from the predecessor's 100% with zero queries to **30.9425%**, with **1.73615 queries per admitted ray**. These samples include warmup. Query activity and absence of validation errors do not certify optical correctness. The 14,784 exterior-eligible rays are only **0.02796%** of admitted rays, so a queue split based on the existing whole-scene bounds proof still lacks a useful eligible population.

The scene's open body mesh and its Unspecified refractive boundary policy remain unchanged. Bounds now permit tracing; they do not provide the missing solid-volume or thin-surface semantics. Remaining unsupported paths are consistent with that policy limitation, but the aggregate counter does not distinguish every rejection reason. No 60 FPS or complete optical-transport qualification is claimed. The requested twenty-body workload also remains unqualified.

### Validation and next work

- Native `opt` builds and production shader/asset cooking pass. First-build missing-include and handle-reset errors were corrected before test execution.
- All **417 enabled ECS graphics tests** pass; 61 existing tests remain disabled. New gather coverage checks ordering, policy preservation, immutable metadata, missing bindings and invalid contributors. Extended deformation tests check accepted/pending/rejected publication and resource replacement.
- Both new native GPU tests pass **35 scenarios** using production meshlet, local-bounds and optical-union shaders. They cover current-pose positions outside bind-pose bounds, 129 meshlets, 65 runtime contributors, invalid/empty/subnormal/near-overflow inputs, mirrored and nonuniform transforms, shear, cancellation, an independent FP64 eight-corner enclosure oracle, bounded outward error, and metadata/guard preservation.
- All **three native optical upload/owner tests** and **five native skinning graph/layout tests** pass. Existing state fixtures now cover the 80-byte selector and four-stage/seven-buffer handoff, including acceptance only after submission.
- Early and late animated skinned-caustic captures pass the strict capture harness with GPU validation requested. Visual inspection shows different deformed poses and corresponding shadow footprints. This confirms active animation and provides a visual smoke check; it does not certify the open body's glass semantics.

The new bounds task is a negligible part of the measured frame. The next substantial work is to establish the intended optical model for the open character geometry, then profile and reduce the now-active reflection traversal cost. GI's roughly 1.35 ms envelope alone cannot explain the approximately 30.15 ms presentation interval. Any proposed reflection specialization must preserve required transport and demonstrate eligibility/cost savings on a supported workload before a full-scene 60 FPS claim.

Evidence is under `__artifacts/stress_60fps/runtime_bounds_c9e594cfe/`: frozen `baseline`, `baseline_run1`, `candidate_run{1,2}`, per-run `gpu_summary.json`, `validation`, `cpu.xml`, `gpu_bounds.xml`, `raytrace.xml`, `gpu_graph.xml`, and `skinned_{early,late}.bmp` with capture logs. Build failure logs are retained alongside the passing build logs. These local binary/image/log artifacts are ignored by Git.

## Reflection specialization for Unspecified boundaries, 2026-09-20

The optical gather now records whether every emitted transparent instance uses the Unspecified boundary policy. The frozen scene snapshot carries this qualifier to reflection pipeline selection. Such a scene cannot enter the closed-medium branch, so its specialized shader omits closed-medium stacks and bootstrap event sorting arrays. It retains the original ray queries, material reconstruction, alpha handling, unsupported-policy termination and query/event limits. Any transparent ClosedNested, ClosedPriority or unknown policy selects the general shader. Opaque instance policies do not restrict eligibility. Missing snapshots remain conservative.

The original bootstrap has substantial private array storage even for the Unspecified case. Removing it is a compiler specialization, not an exterior-ray shortcut or reduction in admitted rays. Register/spill behavior has not been measured directly, so the source-level storage difference is an explanation to investigate, not a hardware occupancy measurement.

The executable and resources from `6c9207d03` were frozen before implementation. Matching ten-body runs use 1280 x 900, yaw 0.6, five seconds of warmup and thirty seconds of accepted native presentation counting, without validation or reflection diagnostics:

| Measurement | Original general shader | Unspecified specialization |
| --- | ---: | ---: |
| Accepted presentations per second | 33.1881 | 47.8182 |
| Wall interval per presentation | 30.1313 ms | 20.9126 ms |
| Hardware reflection, mean GPU range | 9.4376 ms | 0.6740 ms |
| Shadow visibility envelope, mean GPU range | 6.3938 ms | 6.3759 ms |
| Mesh generation over fifteen dispatches | 3.9151 ms | 3.9137 ms |
| Opaque regular pass, mean GPU range | 2.3445 ms | 2.3359 ms |

This single paired acquisition improves presentation rate by approximately 44%; it does not establish sustained thermal performance. Nested/asynchronous GPU scopes are not additive. A separate full stress run passes GPU validation. Its accepted diagnostic ratios remain unchanged: 1.73615355 queries per admitted ray, 0.30942537 unsupported paths, and 0.0002796253 exterior-eligible rays. The open body's unsupported optical semantics are preserved, not solved by specialization.

All 420 enabled ECS graphics tests pass (61 existing disabled). Five native reflection tests pass, including 41 real-TLAS cases comparing all twelve result words between general and specialized production walkers, with separate analytic expectations. Cases cover alpha ordering/coverage, bootstrap/event/query limits, invalid metadata and IOR, mirrored instances, seams, ambiguity and preserved partial radiance. Fixture compile errors and an invalid zero-mask TLAS test instance were corrected before qualification; empty-query cases now use a valid instance beyond the ray range. No backend change was needed.

Matched 1280 x 900 captures freeze after 120 submitted frames. The mean absolute channel difference between original and specialized images is 0.08436/255, maximum 11/255. Repeating the unchanged original gives 0.08213/255, maximum 12/255. Visual inspection finds no structural change; this is agreement within the observed temporal variation, not pixel identity. Existing glass artifacts remain. This ten-body comparison does not satisfy the user's confirmed twenty-body, at-least-60-FPS target.

Evidence: `__artifacts/stress_60fps/reflection_active_6c9207d03/`, including `baseline_run1`, `specialized_run1`, `specialized_validation`, `reflection_kernel_final.{log,xml}`, `ecs_combined.{log,xml}`, the three `*frame120.bmp` captures and both image-comparison JSON files. The production shader and snapshot changes were frozen for the timing runs before subsequent geometry/caustic work.

## Caustic stage specialization and twenty-body baseline, 2026-09-20

Caustic resolve now owns separate prepare, wavelet and upsample pipelines in its feature state. Preparation retains the original dynamic program; wavelet and upsample use constant stage variants. All seven dispatches, all five wavelet dilations, resource ownership, 56-byte push layout and filtering equations remain. Specializing upsample prevents its threads from reserving the wavelet shared-memory tile.

Three native tests pass 234 paired cases. Existing wavelet comparisons retain their original frozen reference. Newly added prepare/upsample coverage uses the exact immediately preceding `6c9207d03` source, with provenance and SHA-256. An initial comparison against the older wavelet-only oracle exposed its missing, already-shipped half-precision preparation conversion; that old file is unchanged. The new stage reference is a separate historical source, not a copy of this candidate. GPU comparisons remain exact. Independent preparation flux/area checks and upsample activity/constant checks also pass, including the original zero-weight corner when the forward differences collapse and no half-grid tap coincides. Initial fixture and comparison failures remain in the artifact logs.

The matched ten-body comparison measures 47.8182 FPS before this step and 48.0225 FPS afterward. The caustic resolve range decreases from 1.6985 to 1.6037 ms, while mesh generation, hardware reflection and shadow ranges remain approximately stable. The 0.43% end-to-end difference is small and based on one run per arm; the measured scope saving is about 0.095 ms. This is a bounded improvement, not the required frame-budget reduction.

The stress fixture now defaults to ten opaque plus ten transparent bodies. `--characters-per-class 5` explicitly retains the historical ten-body layout, phases and camera for comparisons. The twenty-body profile uses two staggered rows at unit scale, ten columns spaced 0.72 apart, row offsets -0.18/+0.18, Z=-0.55/+0.55, camera (0,2.7,-7.2), pitch 0.25, and the same FOV, 1280 x 900 output, mesh, materials and lights. A required observed count/layout/camera record prevents accidental qualification of a different profile. All 28 harness tests pass; the native capture confirms both rows are in view and passes with GPU validation requested.

The first retained twenty-body baseline measures **37.9992 FPS / 26.3164 ms** after five seconds warmup and thirty seconds measurement. Mesh preparation across twenty-five dispatches totals 7.6935 ms per observed frame; the opaque pass averages 4.4125 ms, shadow envelope 7.1048 ms and GI envelope 1.5209 ms. These overlapping ranges must not be summed. **60 FPS is still unmet.** The next major candidate caches decoded object-space vertices by accepted source revision and applies instance transforms during rasterization, retaining the original culling and custom/CSG fallback.

A smaller shared-mesh barrier/LDS experiment was rejected and its five production files restored exactly. On the ten-body profile it increased mesh preparation from 3.9137 to 4.4613 ms and lowered presentation rate to 46.6622 FPS despite the concurrent caustic saving. Its extra primitive-to-position reference loads did not pay for the synchronization savings on this device. The native custom-clip regression is retained, independently covering authored builders whose clip positions differ from source positions.

Evidence: `__artifacts/stress_60fps/object_cache_baseline/` contains the frozen retained executable/resources, `run10`, `run20` and `scene20_frame120.bmp` with the validation capture log. `__artifacts/stress_60fps/reflection_active_6c9207d03/` retains `caustic_kernel_prestep.{log,xml}`, `mesh_kernel_restored.log`, `python_workload.log`, the rejected `combined10_run1`/`combined20_run1` measurements and `rejected_mesh_barriers/` source. The rejected twenty-body run is not the retained baseline.

## Accepted object-space geometry cache, 2026-09-20

The fixed engine shared-mesh program now decodes its object-space attributes into a retained 48-byte vertex buffer. Per-draw compute still performs the original meshlet and triangle culling and emits indices; the ordinary vertex stage applies the current instance and view transforms. Local vertex references remain distinct, preserving normal, UV, tangent and color seams. Authored mesh programs and CSG retain their original compute-emulation route. The cooker emits the auxiliary stages only for the exact engine program, with independent dependency checksums and no inherited material defines.

The mesh domain owns allocation, descriptor retirement and accepted content revisions. The material graph owns one declaration-local producer registry spanning G-buffer, refraction capture and all three AVBOIT raster phases. Decoders depend on uploaded instance/source data, export VertexBuffer state and publish reusable content only after submission acceptance. Source replacement, decoder replacement and unknown runtime revisions invalidate reuse. Twelve CPU tests exercise size bounds and accepted publication, including replacement of every source stream and late writes to an older revision.

The timing harness now supports `--animate`: wall-clock spinning with no frozen angle or fixed simulation delta. Its default remains the fixed yaw 0.6 comparison. Thirty Python tests cover the harness. Both modes retain the twenty-body workload, 1280 x 900 output, five-second warmup and thirty-second measurement, with validation and reflection diagnostics disabled during timing.

| Measurement | Prior retained build | Object cache |
| --- | ---: | ---: |
| Fixed-view accepted presentations/s | 37.9992 | 38.2073 |
| Fixed-view wall interval | 26.3164 ms | 26.1730 ms |
| Rotating accepted presentations/s | 33.9224 | 36.3519 |
| Rotating wall interval | 29.4791 ms | 27.5089 ms |
| Fixed-view mesh dispatch total/frame | 7.6935 ms | 5.4377 ms |
| Rotating mesh dispatch total/frame | 8.6083 ms | 6.2629 ms |

A repeat of the unchanged fixed baseline measures 37.9319 FPS, confirming that the fixed-view overall gain is small. Its shadow envelope remains 7.1167 ms, compared with 9.1438 ms in the cache candidate; opaque and transparent tracing ranges also increase. The rotating shadow envelope changes from 9.1588 to 9.7111 ms. Caustic and reflection control ranges remain approximately stable. The cause of this redistribution is not established: extra vertex-stage work, GPU overlap and actual VS-stage floating-point lowering need separate evidence. The cache improves the rotating acquisition by about 7.16%, but **60 FPS remains unmet** and the fixed-view result must not be described as a large frame-time win.

All 434 enabled ECS graphics tests and all 138 enabled asset integration tests pass, with 61 and 21 pre-existing disabled tests respectively. Four native mesh tests cover 107 cases, including seventeen new cache cases with exact decoded attributes, transformed vertex words, culling indices and guard checks. The transform comparison executes the production helper in a compute wrapper; actual full VS raster equivalence is a separate follow-up. The full twenty-body capture passes with GPU validation requested. Its image difference from the prior frame-120 capture averages 0.09663/255, maximum 27/255; repeating the unchanged baseline gives 0.08136/255 and maximum 25/255. This establishes close rendered agreement amid temporal variation, not bitwise image identity or corrected glass semantics.

Evidence is under `__artifacts/stress_60fps/object_cache_candidate/` and `object_cache_baseline/`: frozen binaries/resources, fixed and rotating runs, baseline repeat, native/CPU/asset logs and XML, frame-120 captures and image-comparison JSON. The next substantial candidate is a persistent index buffer with a dedicated indexed raster route, removing view-dependent triangle-index generation for the fixed engine program.

## Combined opaque and transparent shadow upsample, 2026-09-20

The hardware-transparent route now retains the opaque wavelet output in HalfB, filters transparent RGB into HalfA and resolves both in one full-resolution dispatch. A float4 guided fit shares the receiver guidance across opaque scalar and RGB values, then multiplies them. The original scalar and RGB paths remain available. Fusion requires exactly one wavelet pass per channel and all required pipelines. Transparent-stage failure resolves the retained opaque buffer through the original scalar upsample; only successful transparent completion advances history. The graph retains its existing acceptance endpoint and validates that all prepared stages share its packet, with the opaque upsample task omitted only on the fused route.

Fourteen native shadow tests pass, including eighteen combined-resolve scenarios. They cover active and unused light layers, guards, background, odd/tiny extents, invalid/opposed guidance, signed extrapolation, clamping and temporal-moment inputs. Both arms and the opaque intermediate are checked against an independent CPU weighted-fit reference. Exact comparison initially exposed an actual conversion difference: at one pixel the RGBA16F image store produced half word 13055 (0.2186279296875), while arithmetic half conversion produced 13056 (0.21875). A temporary GPU diagnostic confirmed the same fitted value had different store/arithmetic rounding; the diagnostic was removed. Active RGB parity is therefore bounded by the opaque half step propagated through RGB plus the final output half step. Background, unused layers, guards and alpha remain exact. No device-specific rounding bias was added.

Relative to the frozen object-cache candidate, the fixed twenty-body acquisition improves from **38.2073 to 39.5027 FPS**, saving **0.8583 ms per presentation**. The shadow envelope decreases from 9.1438 to 8.2872 ms; mesh preparation is unchanged at about 5.4375 ms per frame. Rotating performance improves from **36.3519 to 37.2742 FPS**, saving **0.6807 ms per presentation**. These are short paired acquisitions, not a sustained thermal qualification; overlapping GPU scopes must not be summed. **The twenty-body 60 FPS target remains unmet.**

All 438 enabled ECS graphics tests pass, including four behavioral packet-validator tests that compile metadata graphs and reject missing stages, foreign task IDs and forced packet splits. Their initial fixture omitted required task marker labels; that declaration failure was fixed without changing production behavior. The full twenty-body validation capture passes at frame 120. Its difference from the cache-only image averages 0.08767/255, maximum 27/255, close to the repeated-baseline temporal variation. Early and late animated skinned-caustic captures also pass with GPU validation requested and show changing poses and shadow footprints. Existing open-body optical artifacts remain.

Evidence: `__artifacts/stress_60fps/combined_upsample_candidate/` contains frozen binaries/resources, `run20`, `run20_moving`, GPU summaries, `ecs.{log,xml}`, frame-120 and skinned captures. `object_cache_baseline/` retains the original exact-comparison failure, rounding diagnostics and passing `shadow_bounded.{log,xml}`; `combined_upsample_host_prepare/` retains the staged host design and diagnostic archives.

## Persistent indexed raster route, 2026-09-20

The fixed engine mesh program now uses an explicit VertexIndexed route on devices without native mesh shaders. Its accepted cache retains the 48-byte object-space vertices and a trailing u32 index region. A single decoder writes both; ordinary instance/view changes require neither decoding nor triangle-index generation. Raster applies the current transforms and uses hardware clipping and culling. Primitive order, winding and local attribute seams remain intact. Custom authored geometry and CSG retain compute emulation, with legacy buffers and its vertex pipeline allocated only when needed.

The mesh domain owns checked allocation/layout and acceptance. The material graph transports indexed draws separately through opaque rendering, refraction capture and all AVBOIT phases, declares combined VertexBuffer | IndexBuffer state, and creates no synthetic Ready task on steady cached frames. Mixed indexed/custom draw sets retain the ordinary raster callback when the AVBOIT shared-output replacement cannot represent them. Runtime source revisions refresh both vertices and indices, because the existing revision contract does not independently certify unchanged topology. The obsolete view-dependent object-cull shader, archive field and compute-cache reuse key are removed. The benchmark route signature now retains a deterministic set of observed routes per material, allowing legitimate indexed/CSG mixtures while still detecting differences between trials.

The frozen preceding build is `37426cca0`. Both acquisitions retain twenty bodies (ten opaque and ten transparent), all effects, 1280 x 900, five seconds warmup and thirty seconds of accepted presentation counting. Validation and reflection diagnostics are disabled during timing.

| Measurement | Combined-upsample baseline | Persistent indexed raster |
| --- | ---: | ---: |
| Fixed-view presentations/s | 39.5027 | 58.7279 |
| Fixed-view wall interval | 25.3147 ms | 17.0277 ms |
| Rotating presentations/s | 37.2742 | 55.4495 |
| Rotating wall interval | 26.8282 ms | 18.0344 ms |
| Fixed-view mesh dispatch total/frame | 5.4375 ms | No steady-state dispatches |
| Rotating mesh dispatch total/frame | 6.2010 ms | No steady-state dispatches |
| Fixed-view shadow envelope | 8.2872 ms | 6.2171 ms |
| Rotating shadow envelope | 9.0690 ms | 7.1052 ms |
| Rotating caustic resolve | 1.6309 ms | 1.6231 ms |

Presentation rate improves about 49% in both acquisitions. Removing compute expansion also reduces the shadow ranges that regressed in the intermediate cache design, while the caustic control remains stable. These are short paired measurements, not sustained thermal qualification. Nested/asynchronous GPU timings overlap and must not be summed. **The twenty-body 60 FPS target is still unmet**, especially in motion; the next work targets avoidable transparent shadow traversal.

All 440 enabled ECS graphics tests and 138 asset integration tests pass, with 61 and 21 existing disabled tests. Five native mesh suites pass 123 cases: the original ninety, twenty-one cache/guard cases and twelve actual raster comparisons. The new comparison renders the original expanded compute output through emulation_vs and the persistent cache through object_vs with a real indexed draw. It requires exact coverage and checks FP32 varyings within 2e-5 times max(1, abs(reference)); tests include near-plane crossing, tiny/zero/negative clip w, scissor, attribute seams, mirrored/nonuniform transforms and one/two-sided state. The initial fixture AlignUp type mismatch was corrected before execution. Sixty-one benchmark Python tests and thirty stress harness tests pass.

The full twenty-body frame-120 capture passes GPU validation and logs the indexed route. The image difference from the preceding capture averages 0.08764/255, maximum 29/255, consistent with the earlier temporal comparison scale rather than pixel identity. Early and late skinned-caustic captures pass validation and visibly change pose and shadow footprint. The transparent CSG smoke simultaneously exercises indexed ordinary meshes and compute CSG, passes its strict cut/remaining-region checks and GPU validation. Existing open-body glass artifacts remain; activity, parity and performance checks do not certify those optical semantics.

Evidence: `__artifacts/stress_60fps/vertex_indexed_candidate/` retains frozen binaries/packed assets and their identity, `run20`, `run20_moving`, GPU summaries, native/CPU/asset logs and XML, Python logs, frame-120 and skinned/CSG captures, and image comparison JSON. `combined_upsample_candidate/` is the unchanged preceding baseline.

After pulling the independent helper-rules cleanup `b64479971`, the integrated build repeats all 440 renderer, 138 asset and five native mesh tests successfully. Its rotating acquisition measures 55.5322 FPS, consistent with the candidate above. Frozen integrated binaries/assets and that repeat live in `__artifacts/stress_60fps/vertex_indexed_integrated/`; the authored packed volume hash is unchanged.

## Direct large-dilation caustic specialization, 2026-09-20

Caustic resolve now compiles a direct-texture wavelet variant for dilations 8 and 16. It excludes the small-dilation shared tile and its branch; the direct filtering body is unchanged. The feature-owned resolve state retains the extra pipeline, selected by the existing pass dispatcher. All five dilations, seven dispatches, the 56-byte push ABI, resource transitions, temporal accumulation and upsample remain unchanged.

The native disassembly confirms that the preceding wavelet shader declares four Workgroup variables and two control barriers, while the new direct variant declares neither. Four native caustic suites pass 312 paired cases, including 78 additional exact RGBA16 comparisons for both large dilations, odd/partial extents, invalid geometry and neighboring energy propagation. All 440 renderer CPU tests pass. Production asset cooking and the full twenty-body frame-120 GPU-validation capture pass. The image difference from indexed raster averages 0.08568/255 with maximum 27/255; visual inspection shows no structural difference amid temporal variation. Existing glass artifacts remain.

The rotating acquisition compares against the integrated indexed build `1e04f418a`: **55.5322 to 57.6700 FPS**, or **18.0076 to 17.3400 ms** per accepted presentation. Caustic resolve falls from **1.6256 to 1.0718 ms**; shadow visibility remains 7.1295 versus 7.1282 ms and refraction resolve 1.1929 versus 1.1966 ms. The fixed candidate measures **60.2347 FPS / 16.6017 ms**, compared with the earlier indexed capture's 58.7279 FPS / 17.0277 ms; that fixed baseline predates the independent helper cleanup, while the rotating comparison uses the integrated baseline. Its caustic range falls from 1.5970 to 1.0608 ms. These short acquisitions retain twenty bodies, 1280 x 900, all effect settings, five seconds warmup and thirty seconds measurement. **The rotating sixty-FPS target remains unmet.**

A separate transparent-shadow endpoint-bounds experiment was rejected before this change. It passed all fourteen native shadow tests (including 33 real-TLAS transmission cases) and full-scene GPU validation, but moving performance was 55.2448 versus 55.5322 FPS and the target transparent-shadow range was unchanged at 3.2050 versus 3.2074 ms. Its five live files were restored exactly to the preceding revision; the rejected candidate is not in this commit. The existing endpoint proof is too conservative to provide a measured benefit here.

Evidence: `__artifacts/stress_60fps/caustic_direct_candidate/` contains frozen executable/assets, fixed/moving runs, GPU summaries, native and CPU logs/XML, both shader disassemblies, the frame-120 validation capture and comparison JSON. `shadow_bounds_candidate/` and `shadow_bounds_prepare/` preserve the rejected experiment and its unchanged reference/proof hashes.

## Rejected shadow and reflection scheduling experiments, 2026-09-20

A stronger transparent-shadow exterior proof used three line/AABB separating planes and moving-away coordinate checks, with operand-magnitude error bounds and ordinary traversal for invalid or uncertain data. Fifteen native shadow suites passed, including 4,896 pure GPU cases checked against independent FP64 slab intersection and 34 real-TLAS cases. The moving twenty-body result was 57.1352 FPS / 17.5023 ms, compared with the retained 57.6700 FPS / 17.3400 ms. Transparent tracing was 3.1942 versus 3.1620 ms. All ten experiment files were restored or removed. Source inspection found no descriptor/finalizer dependency defect; the trace receives the GPU-finalized union of the transparent instances. The experiment's finalizer executed 1,624 times, but current union validity, extents and rejection counts were not read back, so lack of useful eligibility versus already-cheap hardware misses remains unproven.

A surface-parallel reflection classifier doubled the group to two 64-lane surfaces and shared immutable feedback. Its 78 native cases passed exact output/counter/feedback comparison and canonical queue-block checks, but classification rose from 1.4016 to 1.8088 ms and presentation rate fell to 56.0306 FPS. A smaller follow-up retained the original 64-lane group and two constant-surface calls, sharing only feedback acquisition. All six native reflection tests passed, but moving presentation rate was 57.3946 FPS / 17.4232 ms and classification 1.4085 ms. Neither variant is retained. The original production shader is restored byte for byte; the 78-case native regression fixture and frozen classifier remain to protect later changes.

The reflection run also exposed an independent helper-cleanup change to the historical spatial shader oracle. Commit `e3f8c9a39` restores that oracle to its original frozen float-accumulation implementation and records its provenance. All six native reflection tests then pass. Production spatial filtering was unchanged by that repair.

Evidence: `__artifacts/stress_60fps/shadow_separation_candidate/`, `shadow_separation_prepare/`, `reflection_classify_candidate/`, `reflection_feedback_candidate/` and their staged preparation directories preserve the rejected candidates, native results and frozen twenty-body acquisitions. The current retained performance baseline remains `caustic_direct_candidate/`; the moving sixty-FPS target remains unmet.

## Refraction admission before material reconstruction, 2026-09-20

The shared ray domain now exposes the closest geometric hit separately from attribute/material reconstruction. Refraction admits the same entry front face and same-instance exit back face before reconstruction. Its IOR association, nearest-hit traversal, alpha behavior, internal-reflection/background limits and screen fallback remain unchanged. Other consumers retain the common closest-surface API. No ray, descriptor, resource or setting is added. Refraction policy stays in its own interface-hit include, outside the shared ray helper.

All seven native reflection/refraction suites and fourteen native shadow suites pass. The new real-TLAS fixture covers 35 cases / 70 dispatches, checks all retained geometry/surface words exactly against a frozen original header, and observes material-hook calls for accepted and rejected faces/instances, nearest zero-alpha surfaces, range/miss cases, transforms, invalid normals and IOR. Initial fixture-only build issues (explicit half conversion, ray-query capability metadata and a missing aggregate initializer) were fixed before qualification. The test hook's atomic side effect proves admission control flow but does not prove what the uninstrumented legacy compiler already eliminates.

The fixed twenty-body acquisition changes from 60.2347 to **60.3312 FPS**, or 16.6017 to **16.5752 ms**. The moving acquisition changes from 57.6700 to **57.7557 FPS**, or 17.3400 to **17.3143 ms**. Refraction resolve falls from 1.2074 to 1.1909 ms fixed, and 1.1966 to 1.1647 ms moving. This is a small local saving, with whole-frame changes close to measurement variability; it does not establish sixty FPS in motion. All scene settings, twenty bodies and 1280 x 900 remain unchanged. Production cooking and the frame-120 GPU-validation capture pass; image comparison averages 0.08163/255, maximum 28/255, with no structural visual difference. Existing open-body glass artifacts remain.

A separate algebraic shadow-upsampling experiment precomputed signed regression coefficients once per pixel for all light channels. It passed all fourteen native shadow suites at their existing tolerances after a definite-initialization fix, but moving performance was 57.4138 FPS and transparent resolve 1.7835 ms versus 1.7752 ms. Its sole production shader was restored exactly; it is not part of the retained refraction change.

Evidence: `__artifacts/stress_60fps/refraction_hit_candidate/` contains frozen binaries/assets, corrected compiled fixture sources, native logs/XML, fixed/moving timings, GPU summaries and the validated frame-120 image/comparison. `shadow_fit_coefficients_candidate/` retains the rejected shader, native results and acquisition.
