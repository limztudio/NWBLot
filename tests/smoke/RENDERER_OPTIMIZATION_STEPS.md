# Renderer optimization implementation record

This work follows the September 12, 2026 reflection/refraction audit at source `50f258fcf`. Each completed step is committed and pushed separately. Production changes stay in their owning domains and follow `.helper/standard.md` and `.helper/exception.md`.

Correctness takes priority over an apparent timing reduction. Shader candidates require the relevant image, boundary, and lifecycle checks. Performance comparisons use frozen baseline/candidate builds and assets, matched workloads, independent balanced blocks, and actual completed GPU samples. Builds, cooking, GPU tests, and timing acquisitions run sequentially. No failed or unfavorable trial is discarded to improve the result. A rejected experiment is restored and its outcome is documented.

| Step | Work | Status |
| --- | --- | --- |
| Preparation | Matched AVBOIT timing fixture and reusable two-build benchmark | Complete |
| Measurement correction | Preserve source-frame bounds for out-of-order timing samples | Complete; CPU and native regressions pass |
| Compiler scaling | Order discovered resource fragments without rescanning unrelated states | Complete; exact ordering and native handoffs verified |
| CPU measurement | Separate real preparation/render callback costs and memory observations | Complete; six workloads qualified |
| 1 | Coalesce AVBOIT coverage atomics | Evaluated and rejected; original shader retained |
| 2 | Skip temporal dispatches that cannot update history | Complete |
| 3 | Avoid spatial halo/geometry loads for uniformly ineligible tiles | Complete; retained after GPU parity and matched timing |
| 4 | Reuse accepted optical metadata uploads | Complete; redundant upload removal proven, GPU timing inconclusive |
| 5 | Share common hardware/software ray-scene gathering within a frame | Evaluated and rejected for current workloads after scope profiling; original routes retained |
| 6 | Share pass-independent transparent preparation | Evaluated and rejected; no resolved CPU benefit or observed memory saving |
| 7 | Reuse mip-zero depth neighborhoods | Evaluated and rejected; native/image parity passed but no measured pyramid or frame gain |
| 8 | Reject primary refraction instances earlier in ordinary accumulation | Review found a derivative-safety blocker |
| 9 | Replace repeated optical-bootstrap prior-event scans with a processed mask | Draft reviewed |
| 10 | Test a separately compiled optical kernel for proven air origins | Experiment draft in progress |

The initial audit and complete fresh rough-filter measurements remain under `__artifacts/reflection_optimization_audit_20260912/`. Implementation evidence is kept under `__artifacts/reflection_optimization_steps/`. Those generated files are local diagnostics; completed outcomes and reproducible commands are recorded here.

## Benchmark preparation

The transparent smoke fixture has an opt-in `NWB_AVBOIT_SMOKE_TIMING=1` pass that reserves 32 complete in-flight timing ranges for the frame, six AVBOIT passes, and five deferred/control passes. It also requests the existing unfocused-render policy. Normal smoke behavior is unchanged when the flag is absent. The helper belongs to `tests/smoke`; production scheduling and timing policy are unchanged.

`renderer_ab_benchmark.py` accepts explicit frozen baseline/candidate executables, separate runtime directories, physical source snapshots with SHA256 manifests, and a shared logserver. The initial `transparent-multi` workload fixes 1280x900, yaw zero, fixed delta 1/60, and diagnostics/capture off. It defaults to eight balanced AB/BA blocks (16 launches), two warm-up reports, at least six retained reports and 100 completed GPU frames per trial. All 12 required scopes must satisfy the two-range-or-2% publication-skew bound. Timings use summed GPU durations divided by actual completed samples.

The runner checks executable/dependency/source/authored-resource identities before and after each trial, preserves all trials, and refuses inference from incomplete campaigns. Only exact canonical contiguous runtime pipeline-cache segments may change; arms cannot share cache files. Runtime logs establish the device, dimensions, timing policy, material/shadow routes, and normal shutdown. Paired full-frame inference uses the larger of 0.02 ms or 3% of baseline frame time, with separate control-equivalence checks. An incomplete or unfavorable result is not relabeled as a speedup.

Validation: optimized and debug fixture/ECS builds passed. The runner's 16 CPU tests passed directly and through CTest. The real initial 16-launch campaign completed with all required scope coverage; its control-drift outcome is retained for the coverage experiment. Optimized rendering passed the full refraction gallery, duplicate comparisons, basic refraction, transparency, all three CSG poses, caustic sphere, and combined caustic/refraction checks. Debug validation passed the ECS suite, benchmark analysis suite, ordinary transparency, mid-pose CSG, and refraction with GPU validation enabled. The new coverage candidate was present during these qualification runs; its retention decision is recorded separately.

Invoke the runner from the repository root, supplying independently frozen build/runtime trees:

```text
python tests/smoke/renderer_ab_benchmark.py --baseline-executable <baseline-exe> --baseline-runtime <baseline-runtime> --baseline-source-manifest <baseline-source.json> --candidate-executable <candidate-exe> --candidate-runtime <candidate-runtime> --candidate-source-manifest <candidate-source.json> --logserver-executable <logserver-exe> --output-directory <new-empty-result-directory> --require-hardware
```

Each source manifest contains `revision` and a nonempty `files` object mapping physical snapshot paths (relative to the manifest, or absolute) to SHA256 strings. Freeze the changed sources as well as the base revision. `--plan-only` validates identities and writes a plan without launching; acquisition requires another empty output directory.

## Step 1: coverage atomic experiment rejected

The candidate combined two independently thresholded coverage marks into one atomic OR only when their bits occupied the same 32-bit word. It preserved terminal-slice and cross-word behavior. A temporary test compiled the actual production shader helper with only external-operation adapters and verified literal boundary/threshold bitsets plus 32 sequences of 257 repeated fragments. The candidate passed those optimized/debug unit checks and the rendering qualification above. Exact candidate source and tests remain in the ignored experiment artifacts.

The complete matched campaign contained 16 trials in eight balanced blocks, 96 retained reports and 2,880 completed GPU frames. Every required scope passed the coverage contract. Both arms used executable SHA256 `9ad85422bb89d592e5d346c1a52193893a18454d066117dfad6b568f31908f43`. The baseline authored volume was `9e72a8699fdfada987c8dfb18ff9748377cdaea72af909d122a9aa804f61c595`; the candidate was `603f84fccb25748495b3434f4046ac3631dd0b1ce373d81993e386e120f49f1b`.

| Scope | Baseline mean ms | Candidate mean ms | Paired delta ms | 95% interval for mean delta ms |
| --- | ---: | ---: | ---: | --- |
| AVBOIT occupancy | 0.110026 | 0.111615 | +0.001588 | [-0.005338, +0.008196] |
| Shadow visibility control | 3.575547 | 5.907419 | +2.331872 | [+2.253980, +2.410297] |
| Full frame | 29.092325 | 31.847433 | +2.755108 | [+0.716356, +4.744947] |

The occupancy result does not establish a gain. Shadow visibility fails the control-equivalence check in every block; the runner classifies the comparison as `control_drift`. The full-frame difference therefore cannot be attributed confidently to coverage atomics. Fewer atomic calls at source level are insufficient evidence to retain this candidate. The original production shader is restored, and candidate-only tests are archived with the experiment rather than committed as a runtime requirement. No performance improvement is claimed.

Evidence: `__artifacts/reflection_optimization_steps/step1/benchmark/{plan,trials,report}.json`, all raw trial logs/timing reports, source/build/runtime snapshots, and operator records. Report SHA256 is `ec699d86c507fce12547b27845363c931bbad970dcf02f1291c4246441df6f67`. Acquisition ran September 12, 2026, 15:00:05-15:03:15 UTC on the Adreno X2-90, Windows ARM64 optimized, with Balanced power and 79% battery reported at both endpoints. Frequency and thermal telemetry were unavailable. No other build, asset cook, or GPU test ran concurrently. All unfavorable trials are retained.

After restoration, the optimized build, full ECS graphics target, benchmark analysis target, and ordinary transparency capture passed. The recooked authored-volume hash exactly matches the baseline above. See __artifacts/reflection_optimization_steps/step1_restored_opt_junit.xml. The rejected candidate is absent from tracked production code.
## Step 2: omit temporal recording when history cannot change

`TemporalTask::record` resolves the existing late hardware-readiness/history outcome and returns before native render-pass termination, binding, push constants, timing or dispatch when history is not reusable or its sample cap is one. Classification and hardware resolve already populated the current bank. The graph task, resource declarations, reservation and accepted/discarded callbacks remain intact, so accepted frames still publish the correct bank and sample sequence. No shader or public setting changed.

The cap-aware benchmark contract requires zero temporal ranges for cap one and normal temporal coverage for reusable history. A dedicated smoke test checks every finalized timing report, including warm-up and shutdown, and preserves unique output directories and failed runs. It freezes sources, executable dependencies and authored resources before/after both launches. Missing unrelated GPU scopes, publication skew, changed identities and cleanup failures cannot masquerade as successful omission or an unsupported-device skip.

Validation passed in optimized and debug builds: the ECS graphics suite, existing reflection/renderer benchmark analysis suites, and the new 12-test omission analysis suite. Optimized rendering passed 14 roughness captures and 12 temporal/reset captures. Another 12 debug temporal captures ran with GPU validation enabled; camera, transform, material, light and deformation resets each matched the corresponding fresh-history image with zero byte difference. The native omission smoke passed in both configurations: optimized cap one had 883 retained completed frames and zero temporal ranges across all nine finalized reports, while cap sixteen had 860 retained frames and 860 temporal ranges; debug cap one had 115 retained frames and zero temporal ranges across ten reports, while cap sixteen had 102 retained frames and 102 temporal ranges. These tests demonstrate omitted native work and preserved active-history behavior. They are not a statistical timing comparison, and the active temporal kernel's previous timing is not claimed as a saving from omitting its much cheaper no-op path.

Reproduce the registered proof and its analysis tests with either configuration:

```text
ctest --preset windows-clang-arm64-opt --output-on-failure -R "^(nwb_reflection_temporal_omission_analysis_unit|nwb_reflection_temporal_omission_smoke)$" -j1
ctest --preset windows-clang-arm64-dbg --output-on-failure -R "^(nwb_reflection_temporal_omission_analysis_unit|nwb_reflection_temporal_omission_smoke)$" -j1
```

JUnit evidence is under `__artifacts/reflection_optimization_steps/step2/`; complete omission reports and raw logs are under `__cmake/build/windows-clang-arm64/Testing/smoke/{opt,dbg}/reflection_temporal_omission/temporal_omission_*/`. The original coverage shader from Step 1 was also recooked in both configurations.

## Step 3: avoid spatial neighborhood loads for ineligible tiles

The spatial kernel first loads each in-bounds pixel's radiance, specular and depth eligibility inputs. A workgroup-wide flag decides whether any pixel can filter. Uniformly ineligible tiles copy their source values without loading halo, position or normal data. Active tiles reuse their eligibility inputs and populate disjoint center/halo cache entries before the existing bilateral filter. Every lane participates in the synchronization, including partial-image tails. Radius-zero, alpha, finite-value, roughness and F0 rules are unchanged. The extra shared flag costs four bytes and two additional group barriers; timing, rather than source operation counts, determines whether that tradeoff is useful.

The new `tests/smoke/reflection_kernel` target compiles the actual authored kernels through the production shader cooker and runs validation-enabled native dispatch/readback. Its 160 spatial scenarios compare every RGBA16 output word against a frozen copy of the pre-change production shader, with separate copy/alpha/zero-value checks and distinct output sentinels to catch missing stores. Radius 0-3, tiny and non-power-of-two images, partial groups, dense/sparse eligibility and material/geometry discontinuities all passed. The same fixture has 39 full-chain depth cases for Step 7, using native R32/D32 inputs, the production RG32/RGBA32 storage fallback, literal tiny cases and a separate scalar footprint oracle. Both tests passed in optimized and debug configurations, with no skips or validation errors. The ECS graphics suite, 14 roughness captures, 12 temporal/reset captures, and benchmark analysis suites passed. The renderer A/B analysis suite now contains 28 tests and supports the fixed reflection workloads without changing the reflection runner's cap-aware contracts.

Three predeclared campaigns ran 48 launches in 24 balanced blocks, with 288 retained reports and 35,630 completed GPU frames. All required scope counts and frozen identities passed. Both arms used executable SHA256 `e65b5afbe0376a9b0ac232ab643ca54ef8353d869021bc646179ae0ff2d444ea`. Authored-volume SHA256 changed from `9e72a8699fdfada987c8dfb18ff9748377cdaea72af909d122a9aa804f61c595` to `a5e7ff4ab129877b1397ed2e911e69fd0bf8c53f41f797dd06f3c735fa8f4d12`. The workload settings are 960x720, Hardware reflection, seed zero, budget 1,382,400, optical cap 16, feedback/diagnostics/capture off; the combined-filter case uses temporal cap 16. Each campaign has eight balanced AB/BA blocks, two warm-up reports and at least six retained reports per launch.

| Workload | Spatial baseline / candidate ms | Frame baseline / candidate ms | Paired frame delta ms [95% interval] | Interpretation |
| --- | ---: | ---: | --- | --- |
| Mirror, spatial enabled | 0.231204 / 0.060395 | 3.524358 / 3.352278 | -0.172080 [-0.184340, -0.158982] | Resolved frame-time reduction; all controls equivalent |
| Roughness 0.4, spatial only | 0.841381 / 0.807207 | 4.204476 / 4.188264 | -0.016212 [-0.035598, +0.010805] | Control uncertainty; no frame-time gain claimed |
| Roughness 0.4, temporal + spatial | 0.843199 / 0.808444 | 4.276722 / 4.240940 | -0.035782 [-0.050841, -0.019152] | Below the predeclared practical frame threshold; all controls equivalent |

Retained because the mirror/ineligible workload clears the practical full-frame improvement gate, all tested outputs are unchanged, and the rough/filtered checks show no material slowdown. This is a measured benefit on the Adreno X2-90 for the tested scenes, not a claim of a frame-time gain on every workload or GPU. The rough-only control uncertainty is preserved; no trials were removed or added to obtain a better result. The three intervals are per-comparison intervals, not a simultaneous family-wide confidence statement.

Evidence is under `__artifacts/reflection_optimization_steps/step3/`, including frozen arms, raw logs, timing reports, JUnit results and power records. Balanced power and 79% battery were reported at both endpoints; frequency/thermal telemetry was unavailable. No build, cook, GPU test or other acquisition overlapped the campaigns. Report SHA256 values are `50715f876937c979484787455a960ff7aecdb4fd277dc02be9411ba986a21328` (mirror), `6adfbc00718a810fc8262946359aaf5f28271b38440a8f6cd3fe82172560f864` (rough), and `30606a489790060af7bc801a9833195bf658b8d86752ec5a2aaaac90d1a8de40` (filtered).

Use the common frozen-arm command above with `--workload reflection-mirror-spatial`, `reflection-rough-spatial` or `reflection-rough-filtered`, and a new output directory for each complete campaign. Run the direct kernel proof with `ctest --preset windows-clang-arm64-opt --output-on-failure -R "^nwb_reflection_kernel_tests$" -j1` (or the debug preset).


## Step 4: reuse the latest accepted optical metadata upload

The raytrace owner preserves an immutable CPU payload only after an exact comparison of every header and instance byte. A buffer-bound residency control records which payload the GPU queue actually accepted. An unchanged accepted payload imports that writer completion and the retained Common state, with zero upload tasks and zero graph upload blobs. A changed payload retains its own bytes and publishes residency from the same native upload packet's acceptance callback. Rejected replacement, accepted replacement followed by later frame failure, A-to-B-to-A reversion, physical buffer replacement and device invalidation preserve the latest accepted-content meaning. Unknown native state/provenance is rejected, not inferred from a frame-success flag.

The production upload and lifecycle helpers belong to `impl/ecs_render/raytrace`. The renderer admits frame graphs serially; successful presentation joins the asynchronous refraction reader onto primary Graphics, and accepted-prefix recovery joins the queue frontier before a later replacement upload. A previous writer token alone does not order arbitrary independent reader graphs. The new native tests deliberately wait for readback and do not claim to stress concurrent readers outside the renderer's existing join contract.

Qualification exposed an existing import/consumer mismatch: a zero hardware-ray budget imported optical data despite declaring no hardware consumer. The old unconditional upload hid this; the reused import correctly failed finalization as an untouched resource with a required final state. Reflection snapshots now expose `hasHardwareWork()` and both the task declaration and frame scene-import boundary use that predicate. Refraction must also be active and have a valid hardware snapshot before requiring the shared import. The compiler contract and image oracles were preserved. The initial failed capture and crash diagnostics remain in the step artifacts.

Validation: 18 new lifecycle/graph declaration tests, an additional zero-budget behavior test, and two validation-enabled native raytrace tests passed in optimized and debug builds. The full ECS graphics suite has 381 active tests. Native tests use the actual scene resource owner, real standalone task-graph submission, a real rejected native replacement, and fresh exact-byte readback after rejection. Optimized full reflection/budget, basic refraction, duplicate/overlap and mid-CSG captures passed after the fix; temporal/reset, all 30 reflected optical cases, ordinary transparency and combined caustic optics also passed during this step. Debug full reflection/budget, refraction with GPU validation, ordinary transparency and mid-CSG passed. No native test skipped. Source encoding, CRLF, banner/separator, EOF and unnamed-namespace checks passed.

The complete frozen optical-clear campaign used 16 launches, eight balanced blocks, 163 retained reports and 1,606 completed GPU frames. Every required scope count and build/source/resource identity passed. Baseline executable SHA256 was `e65b5afbe0376a9b0ac232ab643ca54ef8353d869021bc646179ae0ff2d444ea`; candidate was `0923ed9f8dfc3d91b8f9da72d2c08bed11721775e143819e0dd875aeae25c9f3`. Both authored volumes were `a5e7ff4ab129877b1397ed2e911e69fd0bf8c53f41f797dd06f3c735fa8f4d12`.

| Scope | Baseline / candidate ms | Paired mean delta ms [95% interval] |
| --- | ---: | --- |
| Full frame | 53.298711 / 51.322085 | -1.976626 [-5.763720, +0.022087] |
| Reflection hardware | 38.191888 / 36.246358 | -1.945530 [-5.769760, +0.064468] |

The runner reports `control_uncertain`: opaque and deferred-lighting intervals do not fit their equivalence tolerances, while shadow visibility does. The second block's baseline had a large hardware-reflection excursion; the paired full-frame difference for that block was -15.054110 ms. It is retained, with no additional timing trials. The unchanged shader asset identity does not explain that excursion, and the mean reduction is not attributed to upload reuse. No GPU frame-time gain or CPU timing gain is claimed. Retention is based on the native/graph proof that an unchanged accepted scene removes the persistent payload allocation, graph payload copy and physical upload rather than merely reducing a source-level operation count. CPU timing and memory profiling are separate follow-up measurements for the preparation work.

Evidence: `__artifacts/reflection_optimization_steps/step4/`, including `opt_unit_native_junit.xml`, the preserved initial `opt_capture_junit.xml` failure, `opt_fixed_capture_junit.xml`, `dbg_junit.xml`, frozen arms, raw campaign logs and `benchmark_optical_clear/report.json`. Report SHA256 is `52f7b9d03518387336689a54f3cd45487f0a5743b2a3c92717aa7fa6da5f78d1`. Balanced power and 78% battery were reported before and after acquisition; frequency/thermal telemetry was unavailable. No build, cook or GPU test overlapped timing.

Reproduce the native proof with `ctest --preset windows-clang-arm64-opt --output-on-failure -R "^(nwb_ecs_graphics_tests|nwb_raytrace_tests)$" -j1` and the debug preset. Use the common A/B command with `--workload reflection-optical-clear` and independent frozen arms for timing.

## CPU measurement prerequisite: correct asynchronous source-frame bounds

While preparing the CPU gathering comparison, review found that reused GPU query slots can publish source frame 51 before frame 50 in one timing batch. The performance accumulator previously used arrival endpoints as its frame bounds, producing 51..50; other arrival orders could produce an apparently valid interval that omitted an earlier source frame. That could misattribute a batch at a warm-up or measurement boundary.

The performance owner now maintains the minimum and maximum source frames. Sample delivery order, duration totals, minimum/maximum durations, last-arrival duration, publication index, binary layout and allocation behavior are unchanged. No sorting or renderer policy change is involved.

A CPU regression drives the real overlap correlator with distinct durations and reversed frame completion. A native regression actually releases and reuses one of two timer-query slots, checks callback order 51 then 50, and verifies aggregate bounds 50..51 without duplicate publication. Both optimized and debug graphics-resource, telemetry and native descriptor-buffer suites passed; the existing main-thread frame timing lifecycle test also passed. The new native regression did not skip. Evidence is under `__artifacts/reflection_optimization_steps/step5/benchmark_support_fixed_opt_junit.xml`, `benchmark_support_dbg_junit.xml`, and `benchmark_support_dbg_full_ctest.log`; the reviewed proposal is under `timing_source_span/`.

This is a measurement correctness fix, with no rendering speedup claim. Reproduce with `ctest --preset windows-clang-arm64-opt --output-on-failure -R "^(nwb_graphics_resource_tests|nwb_telemetry_tests|nwb_descriptor_buffer_tests)$" -j1` and the debug preset.

## Compiler scaling prerequisite: linear resource-fragment ordering

The distinct-mesh gathering fixture exposed a CPU bottleneck in `core/task/gpu/compiler_resource_ranges.cpp`. All 12 retained ARM64 hot-thread samples landed in `AppendResourceStateFragmentsInStateOrder`, reached through the actual resource-state planner and graph compiler. The old helper scanned every discovered fragment once for every tracked state, including unrelated resources.

Both collectors already discover contiguous state groups newest to oldest. The replacement reverses the group order while copying each group forward, then preserves the uncovered initial-state suffix. The complete output sequence, pointers, indices and ranges are unchanged. Ordering work changes from O(S x D + D) to O(D), with no extra allocation, sort, resource index, persistent cache or renderer-specific branch. Resource discovery, subtraction and queue/final-state contracts remain intact.

All 340 GPU-task tests passed in optimized and debug builds, including six new exact-order cases covering partial buffers, sparse indices, unsorted requests, initial-state holes, empty output reuse, symbolic tails, texture rectangles and 64-buffer compiled external exports. The native descriptor-buffer target passed in both configurations: optimized had 381 passing cases and 39 feature/configuration skips; debug had 382 passing cases and 38 skips. Both relevant multi-packet and cross-queue external handoff tests ran and passed. Optimized duplicate-refraction, mid-CSG and combined caustic/refraction/reflection captures passed. Debug refraction with GPU validation, mid-CSG and combined optics passed. The fresh combined optimized image was also inspected.

The compiler-fixed, Step5-absent fixture completed all six functional workloads, each with 96 successful warm-up, 256 measured and 32 drain frames. Distinct meshes completed the 384-frame run in approximately 65 seconds. Before the fix, that workload timed out after 180 seconds before finishing warm-up; a separate 60-second diagnostic completed only 17 frames. This demonstrates completion within the acquisition envelope. It is not a paired speed ratio: the old arm has no complete matching measured window, and partial frame counts or GPU times are not interchangeable with a qualified trial.

Evidence is under `__artifacts/reflection_optimization_steps/compiler_fragment_order/` (`opt_unit_native_junit.xml`, `opt_unit_native_full.log`, `dbg_junit.xml`, `dbg_full.log`, `opt_captures_junit.xml`) and `cpu_gather_benchmark/` (`baseline_unique_stack_diagnostic`, frozen `compiler_baseline_v3`/`compiler_candidate_v4`, and `compiler_fixed_all_timing_pilot`). Initial invalid target invocation is retained in `opt_build.log`; the corrected target build passed. The proposal patch SHA256 is `b8b5f0c71260565c31f0978848de25773d2f0a3e4c3789176e304e55e4489dc4`.

Reproduce ordering and native handoff qualification with `ctest --preset windows-clang-arm64-opt --output-on-failure -R "^(nwb_gpu_task_tests|nwb_descriptor_buffer_tests)$" -j1` and the debug preset. Subsequent renderer comparisons must include this same compiler fix in both arms.

## CPU gathering measurement support

The opt-in `nwb_renderer_gather_benchmark` fixture adds six fixed 64-object workloads covering opaque, mixed, shared/distinct identities, mutable overrides and actual runtime mesh owners. Generic callback timing belongs to `core/graphics/runtime`; scene generation, result collection and comparison belong to `tests/smoke`. The private project asset root owns its material surface and generated identities. Preparation/render callback subtotals remain inside the existing CPU render parent, are joined before publication, and add no timer reads when capture is disabled.

The acquisition contract requires 96 successful warm-up frames, 256 exact measured frames and 32 drain frames, with independent completed GPU-source coverage. Timing and memory acquisition are separate. Explicit Vulkan layers are rejected; executable/dependency/interpreter/helper/source/resource identities are preserved around trials. Incomplete controlled shutdown retains its raw evidence without a completion footer. The runtime workload holds eight real skeletal owners at distinct poses; it does not claim animated-deformation timing.

Optimized and debug fixture builds and private 846-asset cooks passed. All 27 gather analysis/generator tests and 28 common A/B analysis tests passed. Generic frame timing and native timing-source tests passed in both configurations during the measurement prerequisite. The compiler-fixed optimized fixture completed all six functional timing acquisitions at 384 successful frames each; the separate shared memory acquisition passed its owner/counter contract. Earlier wrong-root, long-cache-path, CRLF identity and incomplete unique attempts remain preserved. These are correctness and acquisition results, not a two-arm speed or memory-saving claim.

See [RENDERER_GATHER_BENCHMARK.md](RENDERER_GATHER_BENCHMARK.md) for build, timing and memory commands and limitations. Evidence remains under `__artifacts/reflection_optimization_steps/cpu_gather_benchmark/`, with optimized/debug build and CTest logs also under `compiler_fragment_order/` and `step5/`.

## Step 5: shared ray-scene gathering screened out after profiling

The candidate built one scratch-owned, pass-neutral renderer stream for the hardware and optional software ray-scene consumers. It resolved shared visibility, coincident filtering, mesh/transform and material facts once, while each route retained its own eligibility, geometry preparation, acceleration snapshot lookup and output packing. Mutable override bytes were copied only when needed because later material-cache insertion can relocate them. The candidate added a neutral record per selected renderer and retained mesh handles until both gathers completed; a single-route frame also paid that cost.

The implemented candidate passed its correctness qualification before profiling: 17 new CPU cases, one new native runtime-mesh replacement case, optimized/debug ECS graphics and raytrace suites, and the recorded reflection, optical, temporal/reset, refraction/duplicate, CSG, caustic and runtime-mesh captures. The accepted-upload and compiler changes are separate from this candidate. Exact reviewed candidate sources and tests remain under `step5/applied_candidate_source/manifest.json`; that manifest's SHA256 is `843d347f159c82431c9eff4e9c79ff05704fddf8d1890b06b93a478ae65a86f0`.

The compiler-fixed baseline then completed six matched-control functional workload acquisitions. Normal ray-scene preflight, including the proposed shared work, lies within `graphics.prepare_resources`. The following descriptive values use the same 256 measured successful frames for each workload. They are not candidate timing results or confidence intervals.

| Workload | Entire preparation mean ms | CPU render mean ms | Entire preparation / render |
| --- | ---: | ---: | ---: |
| Opaque | 0.073089 | 6.138091 | 1.190746% |
| Hybrid | 0.157080 | 14.689948 | 1.069306% |
| Shared | 0.205215 | 19.721707 | 1.040553% |
| Unique | 0.401876 | 154.367491 | 0.260337% |
| Overrides | 0.215496 | 19.901891 | 1.082790% |
| Runtime | 0.365255 | 26.802471 | 1.362765% |

Even hypothetically removing the whole preparation callback at zero cost falls below the unchanged primary practical gate, `max(0.02 ms, 3% of graphics.render)`, in every tested workload. Step5 changes only a subset and adds scratch bookkeeping/copies. Its neutral stream and retained handles die before preparation returns; route-owned output vectors, graph payloads, uploads and later release work remain. The compatibility fallback builds a fresh single-route stream and gains no shared gather. No concrete mechanism was found for a useful reduction outside the measured preparation scope.

The candidate was therefore screened out before the planned 96-launch timing matrix. No paired A/B campaign or two-arm memory-saving result is claimed, and no performance criterion was relaxed. Scope subtraction cannot formally bound indirect cache/allocator effects, and other scenes might spend much more time in preparation. The decision is that this extra complexity is not justified by the measured opportunity in the current workloads, not proof of zero benefit or a measured regression.

All 13 candidate paths were checked against their pre-candidate state, with the four new files physically absent. The restored optimized and debug ECS suites passed all 381 active cases, and both native raytrace cases ran and passed. These suites exposed one stale presentation source-contract assertion from the new generic timing helper; it now follows `renderWithPhaseTiming` while preserving acquisition/validation/render/present order and the public uninstrumented wrapper. The original failure is retained in `step5/restored_opt_junit.xml`; fixed optimized/debug results are `restored_opt_fixed_junit.xml` and `restored_dbg_junit.xml`, with complete logs alongside them. The compiler-fixed duplicate/CSG/combined-optics and GPU-validation captures also passed before closing this step.

Evidence paths are relative to `__artifacts/reflection_optimization_steps/`. Candidate qualification is under `step5/{opt_junit.xml,dbg_junit.xml,initial_candidate_captures}`; scope observations are under `cpu_gather_benchmark/compiler_fixed_all_timing_pilot`. The unexecuted Step5 campaign helper and earlier failed/incomplete pilots remain as historical artifacts. Subsequent transparent-pass and shader optimizations receive independent correctness and performance decisions.

## Step 6: shared transparent preparation evaluated and rejected

The implemented candidate prepared one scratch-owned stream for five synchronous consumers: transparent CSG receivers, refraction capture, AVBOIT occupancy, extinction and accumulation. It resolved shared mesh, material, transform, coincident-volume and CSG lookup facts once. Prepared-only consumers borrowed existing mutable overrides during the graph declaration; pass compatibility, typed packing, partitioning and final GPU payload ownership stayed in their existing domains. Opaque-only frames bypassed record gathering. The fourteen exact candidate paths, three new files and eleven new behavior tests remain preserved under `step6/final_candidate/`; no screened-out Step5 implementation was included.

The candidate passed optimized and debug ECS graphics suites with all 392 active cases, both native raytrace cases, and the recorded reflection, temporal, optical, refraction/duplicate, CSG, combined caustic optics and skinned-owner captures. Optimized qualification passed nine CTest targets; debug qualification passed seven, including refraction with actual GPU validation. The combined caustics/refraction image was also inspected. These correctness results did not establish a performance benefit.

Both exact frozen arms completed canonical six-scene timing pilots before the complete predeclared matrix. All 96 launches in 48 balanced pairs completed, retaining 24,576 measured CPU frames and 24,574 completed GPU frames. Each launch had 96 warm-up, 256 measured and 32 drain successful frames. Both arms contained the same compiler and measurement fixes, private generated project, authored volume and instrumentation. The sole source delta was the reviewed fourteen-file candidate. Every report and frozen identity passed the existing acquisition checks; no timing trial was dropped, extended or selectively repeated.

| Workload | CPU render baseline / candidate ms | Paired delta ms [95% interval] | Existing comparison result |
| --- | ---: | --- | --- |
| Opaque | 6.026364 / 5.979774 | -0.046589 [-0.108665, +0.009755] | `gpu_control_uncertain` |
| Hybrid | 14.344387 / 14.262874 | -0.081514 [-0.493251, +0.325291] | `cpu_change_unresolved` |
| Shared | 19.464174 / 19.370083 | -0.094091 [-0.297957, +0.104736] | `cpu_change_unresolved` |
| Unique | 155.440141 / 155.220361 | -0.219780 [-2.067950, +1.789586] | `gpu_control_uncertain` |
| Overrides | 19.611055 / 19.561349 | -0.049707 [-0.312453, +0.236886] | `gpu_control_uncertain` |
| Runtime | 26.261511 / 26.400287 | +0.138777 [-0.126825, +0.398418] | `cpu_change_unresolved` |

All six CPU-render intervals cross zero and none clears the unchanged practical reduction gate, `max(0.02 ms, 3% of baseline graphics.render)`. All six render-pass subtotal intervals also cross zero. Hybrid, shared and runtime pass GPU control equivalence but remain unresolved. Opaque has uncertain GPU frame, opaque and shadow controls; unique has uncertain lighting and presentation controls; overrides has an uncertain shadow control. None is classified as material control drift. Complete CPU-frame intervals remain inside the allowed positive regression bound. These results demonstrate neither a practical improvement nor a confirmed material regression, and no pooled six-workload confidence claim is made.

The separate memory campaign completed all twelve acquisitions, one per workload and arm, with 256 measured frames each. Seven arena owners had complete matching observations; `avboit_transparent_csg` and `material_pass_render` remain unavailable. No comparable retained-used, retained-reserved, reallocation or individual-arena lifetime-peak reduction was observed. Task-graph allocation and deallocation counts increased together by approximately one per frame for hybrid, shared, unique and runtime, five for overrides, and 0.121 for opaque; all other comparable metrics were unchanged. These single-acquisition observations establish neither timing effects nor process-peak memory changes, and small fractional differences are not attributed precisely to the candidate.

The extra shared-record and borrowed-cache lifetime machinery is rejected because the tested workloads show no justified benefit. All fourteen paths were restored to exact baseline hashes, with the three new files physically absent. Restored optimized and debug builds passed all 381 active ECS graphics cases and both native raytrace cases. The compiler, timing, temporal, spatial and accepted-upload improvements remain intact.

Evidence is under `__artifacts/reflection_optimization_steps/step6/`: `baseline_v1`, `candidate_v1`, both successful timing pilots, `timing_v1`, `memory_v1`, candidate `opt_junit.xml`/`dbg_junit.xml`, and restored `restored_opt_junit.xml`/`restored_dbg_junit.xml`, with complete logs alongside them. The candidate manifest SHA256 is `ccede216ff06598c139683eb81a2e1086fd4b93bf2e36da914b3b24a289ede46`. The initial candidate pilot invocation used a nonexistent logserver path and failed before launch; its corrected invocation passed before the matrix. Balanced power and 79% battery were reported at both timing endpoints; frequency and thermal telemetry were unavailable. No build, cook or GPU test overlapped timing. Reproduction follows `RENDERER_GATHER_BENCHMARK.md` with each fixed workload and its own eight-block campaign, followed by separate memory acquisitions.

Timing set report SHA256: `8a8ffdca6b5a77aba6eb9344d71b21d6190c107eb120dc34863f68592bd5297c`.

Memory set report SHA256: `c15092de5856f67a92038fc03114d0a1c48ab368d3b04efb09c3bfc587cff627`.

## Step 7: shared mip-zero depth neighborhood evaluated and rejected

The single-shader candidate used a 100-element FP32 shared tile for each 8x8 mip-zero output group, replacing repeated source loads with a shared 3x3 neighborhood. All lanes initialized the tile and reached the barrier before image-tail returns. Clamp, sanitization and min/max reduction order were preserved; higher-mip reduction logic was unchanged. The pipeline added 400 bytes of shared storage and one mip-zero group barrier. Source load counts alone cannot establish a gain because the original texture path already caches neighboring samples.

Baseline and candidate passed the actual production-cooker/native-readback fixture in optimized and debug configurations with no skips or validation errors. Each run contained 39 depth scenarios checking every texel in every mip and 160 spatial regression scenarios, inside two GoogleTests. Depth coverage includes literal tiny cases, odd and non-power-of-two dimensions, finite D32 and finite/invalid R32 inputs, and the supported production RG32/RGBA32 output choice. The fixture does not force both output formats or distinguish signed-zero bits.

Both exact frozen Opt arms passed all 22 smooth reflection captures with required hardware availability and actual GPU-validation startup markers. Existing image, route, ray-budget and completed-statistics oracles were rerun, and every decoded RGB pixel matched between arms. This is image parity plus each suite's semantic statistics validation; cross-process asynchronous statistics totals were not compared. Baseline/candidate executable and dependency bytes were identical, and the full frozen-source difference was exactly `depth_reduce_cs.slang`. All 206 packed-file identities were checked: only its shader payload and the owning shader index changed. Every other packed payload and shader record was identical. Each arm's native Opt module exactly matched its packed bytecode, and its Opt/Dbg native modules were identical.

The predeclared `reflection-screen-depth` campaign completed all 16 launches in eight balanced pairs, retaining 12,928 completed GPU frames. All required coverage, identities and opaque/lighting/shadow control-equivalence checks passed. At 960x720 the depth scope contains ten mip-reduction ranges per frame. The per-range mean is not a pyramid time; the table uses the runner's total measured pyramid work per completed frame.

| Scope | Baseline / candidate ms | Paired delta ms [95% interval] |
| --- | ---: | --- |
| Full GPU frame | 3.598778 / 3.593588 | -0.005190 [-0.017680, +0.006145] |
| Whole depth pyramid per GPU frame | 0.141604 / 0.142346 | +0.000742 [-0.003760, +0.004563] |

The runner reports `unresolved`; the practical full-frame threshold was 0.107963 ms. Neither the pyramid nor the frame establishes an improvement, so the additional shared storage and synchronization are rejected. No regression or speedup is claimed. All trials are retained without extension or removal. Balanced power and 78% battery were reported at both endpoints; frequency and thermal telemetry were unavailable, and no build/cook/GPU test overlapped acquisition.

The original shader was restored to exact SHA256 `ad1cae197a45b33d14a8786eaf5f3dea9592645022d090458c604b0a9882cbf7`. Optimized and debug native checks passed again; the rebuilt ordinary Opt executable/dependencies and authored runtime exactly match the qualified baseline identities. The rejected shader remains only in the experiment artifacts. The shared kernel test fixture is retained because it also qualifies the accepted spatial change.

Evidence is under `__artifacts/reflection_optimization_steps/step7/`: frozen arms and native modules, baseline/candidate/restored native JUnit and full logs, both capture suites, `capture_comparison_v1/report.json`, `packed_depth_delta.json`, and `timing_screen_v1/report.json`. Reproduce native checks with `ctest --preset windows-clang-arm64-opt --output-on-failure -R "^nwb_reflection_kernel_tests$" -j1` and the debug preset; use the common eight-block runner with `--workload reflection-screen-depth` for timing. The new generic freeze helper's offline validation ran 36 cases: 35 passed and the symlink-creation case explicitly skipped because Windows did not grant that privilege. Both real frozen arms subsequently passed the complete source/absence/copy/delta checks.

Timing report SHA256: `023a8eb8d24d447cbe1e9a6a1e50652b7cc291193a3a8acfabc4df3274114c0c`.
