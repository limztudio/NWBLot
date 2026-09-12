# Renderer optimization implementation record

This work follows the September 12, 2026 reflection/refraction audit at source `50f258fcf`. Each completed step is committed and pushed separately. Production changes stay in their owning domains and follow `.helper/standard.md` and `.helper/exception.md`.

Correctness takes priority over an apparent timing reduction. Shader candidates require the relevant image, boundary, and lifecycle checks. Performance comparisons use frozen baseline/candidate builds and assets, matched workloads, independent balanced blocks, and actual completed GPU samples. Builds, cooking, GPU tests, and timing acquisitions run sequentially. No failed or unfavorable trial is discarded to improve the result. A rejected experiment is restored and its outcome is documented.

| Step | Work | Status |
| --- | --- | --- |
| Preparation | Matched AVBOIT timing fixture and reusable two-build benchmark | Complete |
| 1 | Coalesce AVBOIT coverage atomics | Evaluated and rejected; original shader retained |
| 2 | Skip temporal dispatches that cannot update history | Complete |
| 3 | Avoid spatial halo/geometry loads for uniformly ineligible tiles | Complete; retained after GPU parity and matched timing |
| 4 | Reuse accepted optical metadata uploads | Implementation draft in progress |
| 5 | Share common hardware/software ray-scene gathering within a frame | Pending |
| 6 | Share pass-independent transparent preparation | Pending |
| 7 | Reuse mip-zero depth neighborhoods | Shader draft prepared; production-kernel readback tests in progress |
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
