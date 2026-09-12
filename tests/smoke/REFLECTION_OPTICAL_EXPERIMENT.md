# Deferred optical appearance experiment (September 12, 2026)

The deferred-appearance optimization was **rejected and removed**. It passed the scoped correctness checks, but the fixed 16-trial experiment did not establish a useful GPU-time reduction. The existing reflection implementation and defaults remain in place. This is separate from the historical [September 9 performance report](REFLECTION_PERFORMANCE.md); its samples are not pooled with that report.

## Candidate and retained changes

The candidate separated committed geometric-hit reconstruction from UV, shading-normal, and appearance reconstruction in `raytrace/surface_hit.slangi`. The optical walker completed real appearance for opaque and ordinary alpha surfaces. Only explicitly authored homogeneous ClosedNested/ClosedPriority boundaries used the existing homogeneous optical-parameter dispatch without fetching appearance attributes. Nearest-hit traversal, material selectors, Fresnel/Beer/Snell transport, TIR, coincident-event checks, medium membership, and query limits were preserved. The associated refraction source-contract test followed those helper boundaries.

The two shader edits and their test edit were restored to the baseline after measurement. Both configurations were rebuilt; the final optimized smoke executables, helper/DLL files, and authored asset volume match the qualified baseline exactly. No speedup is claimed from fewer attribute loads or shader size.

The prerequisite repair remains in commit `efc82d8378977f2ea415dee3ded6a253a2285d77`. It restores timing and ray-tracing resource methods omitted by upstream source extraction, repairs missing declarations/qualifications, and fixes two task-graph regressions: disabled opaque CSG must not publish its incoming dependency as a clear task, and an inactive hardware-caustics builder must not overwrite software-caustics task IDs. The actual no-CSG builder regression test passes without a GPU or mock backend. Implementations remain in their owning domains and introduce no unnamed namespaces.

## Fixed method

- Windows 11 ARM64, `windows-clang-arm64-opt`, Qualcomm Adreno X2-90, Vulkan hardware ray queries; the material path used CS + PS compute emulation. Device Vulkan API 1.4.295, reported driver value 2151018496.
- Operator observations bracket acquisition at 2026-09-12 12:34:20-12:37:09 UTC. GetSystemPowerStatus reported AC connected and 79% battery at both observations; Balanced power scheme. Clocks were not locked and thermal stability was not measured.
- One `optical_clear` Hardware workload: 960x720, seed 0, ray admission cap 1,382,400, optical-query limit 16, screen-step setting 96, fixed delta 0.016666667 seconds. Reflection diagnostics, framebuffer capture, temporal/spatial filters, and miss feedback were off. Timing launches did not request GPU validation; explicit layer overrides are rejected by the existing runner.
- Eight balanced AB/BA blocks, 16 independent process launches; order and analysis seeds 0. Each trial discards two warm-up reports and retains at least six reports and 100 completed GPU frame samples, with coverage extension bounded by 90 seconds. Timing has 32 in-flight ranges. Hardware mode performs no depth-pyramid dispatch.
- The primary endpoint is paired `render.frame`, candidate minus baseline. A useful reduction requires the 95% bootstrap interval to clear `max(0.02 ms, 3% of baseline)` and the three control scopes to establish equivalence. Controls use `max(0.015 ms, 3% of their baseline)`. There are 10,000 bootstrap draws; hardware-kernel timing is secondary evidence.
- Source, harness, executables, dependencies, and authored assets were frozen before acquisition and checked across trials. The arms used identical executable bytes at distinct paths and separate caches; only canonical runtime pipeline-cache segments could mutate. No build, cook, framebuffer capture, or CPU-analysis job overlapped the timed matrix.
- All 16 trials are included. There were no exclusions, missing blocks, acquisition failures, selective extra trials, or conclusions drawn from partial pairs. The preliminary Hardware/Hybrid acquisition pilots are excluded from these statistics.

## Complete result

There are **1,676 completed GPU frames**: 812 baseline and 864 candidate, across 173 retained reports. Each trial retained 100-110 frames and 10-13 reports. All nine required scopes have exactly one sample per completed frame and occur in every retained report. Independent analysis reparsed the raw timing files and reproduced the summaries and bootstrap comparisons.

Arm means below are means of the eight independent trial means. The intervals describe paired candidate-minus-baseline differences, not single-frame jitter.

| Endpoint | Baseline mean ms | Candidate mean ms | Paired delta ms | 95% interval ms |
| --- | ---: | ---: | ---: | --- |
| GPU frame | 57.312150 | 51.846789 | -5.465361 | [-11.457607, +0.203158] |
| Hardware reflection dispatch | 42.186845 | 36.766379 | -5.420466 | [-11.401466, +0.225381] |

The frame practical threshold is **1.719364 ms**. Both intervals include zero. The decision is `control_uncertain`, and the primary interval would remain inconclusive even without the control gate. This result establishes neither a reliable speedup nor a slowdown.

| Control | Paired delta ms | 95% interval ms | Equivalence tolerance ms | Result |
| --- | ---: | --- | ---: | --- |
| Opaque regular | -0.044319 | [-0.082763, -0.002026] | +/-0.028464 | Equivalence not established |
| Shadow visibility | +0.003274 | [-0.052006, +0.079600] | +/-0.264854 | Equivalent |
| Deferred lighting | -0.012626 | [-0.030207, +0.007144] | +/-0.015000 | Equivalence not established |

Neither uncertain control meets the separate material-drift criterion. The experiment does not identify a thermal, frequency, driver, register-pressure, or spilling cause for the variation.

Every block is shown below. A is baseline and B is candidate; frame times are milliseconds. Slower launches remain included.

| Block | Order | Baseline frame | Candidate frame | Baseline samples | Candidate samples |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | A/B | 51.525241 | 49.937255 | 100 | 104 |
| 2 | B/A | 66.771649 | 60.698149 | 104 | 107 |
| 3 | A/B | 67.133640 | 48.484754 | 104 | 110 |
| 4 | B/A | 51.347542 | 48.866053 | 100 | 109 |
| 5 | A/B | 51.445186 | 60.510856 | 100 | 107 |
| 6 | B/A | 51.108837 | 48.810630 | 100 | 109 |
| 7 | A/B | 67.236531 | 48.518339 | 104 | 109 |
| 8 | B/A | 51.928571 | 48.948273 | 100 | 109 |

This single-adapter, single-scene measurement is not an application FPS measurement or a general optical-interface cost. Bounded secondary transmission remains an expensive path on this adapter; this experiment does not qualify that stress workload for a typical real-time frame budget.

## Correctness and final-state evidence

The repaired baseline passed 73 actual framebuffer captures with required hardware and GPU validation: 30 optical, 22 reflection/budget, 14 roughness, three refraction, and four combined caustic/refraction/reflection cases. After rebasing over an unrelated FBX utility extraction, the three executables differed only in their COFF/debug timestamps; masking those fields made the entire files byte-identical. Three additional representative launches qualified the rebased executable hashes. Original baseline captures retain their original identities rather than being relabeled.

The rejected candidate passed **115 actual framebuffer captures**: the same 73 cases plus 12 temporal, 24 feedback, and six diagnostics-off captures. The 117 BMP files include two derived refraction difference images; those are not additional captures. All 52 deterministic optical/reflection BMP and PNG pairs were identical to baseline, and stationary completed per-frame work counters matched. The other suites passed their existing image/history oracles without imposing byte identity on stochastic caustics or roughness.

Both optimized and debug focused suites passed 95 tests, and all ten selected smoke-analysis/launcher targets passed. The prerequisite validation also passed graphics-resource, telemetry, and all 133 asset-graphics integration tests in both configurations. After restoring the rejected candidate, both builds and the 95 focused tests per configuration passed again, and the final optimized binaries/assets matched the qualified baseline.

**The full ECS suite is not clean.** The candidate run passed 315 of 359 tests, with 44 failures and 61 disabled tests. The pulled baseline passed 314 of 358 with the exact same 44 failing names; the extra passing test is the added no-CSG regression. Existing source-layout/ownership contracts have not all been migrated after extraction. One confirmed pre-existing boundary issue is the deferred frame-tail builder's direct dependency on RendererFramePipeline. The matching failure set does not establish that every existing failure is harmless.

## Identities and retained artifacts

The repaired baseline source commit is `efc82d8378977f2ea415dee3ded6a253a2285d77`. SHA256 identities:

| Artifact | SHA256 |
| --- | --- |
| Both arms' reflection executable | `3b5bc42d8e93d0fc78730f9502ef0dd0bb6015736e9d125fd13f55f8b14ec623` |
| Baseline/final authored volume | `39a2bb42d9df9fcc979b741ae101c10cb1dc5fcd88e39a47e7c10cd223d88134` |
| Rejected candidate authored volume | `302eb43d0bb2bccee6d2bc4ae8e0a445c0042d2afb23a65ef989372ef6fd3d74` |
| Complete timing report | `185305930553acbeaad6291e43c46668571d3fbd5a939209d52fa2035642026a` |
| Rejected three-file patch | `69f3319008b0031cbc7a6567f747304bc2835469fb06de0c48b42bccb340c2de` |

Local diagnostic artifacts remain under `__artifacts/reflection_stage9_after_pull/`: `optical_ab_final/{plan,report,trials,correctness_provenance}.json`, raw per-trial logs/timing files, operator records, `candidate_correctness.json`, `correctness_comparison.json`, `stage9_ab_independent_review.json`, `rebase_pe_identity_review.json`, `final_restoration.json`, and the rejected patch. The artifact orchestrator uses the committed `reflection_benchmark.py` acquisition and analysis routines with the fixed method above. These ignored artifacts are experimental evidence, not runtime dependencies.

Earlier build/topology failures and the initial V3 qualification-helper failure are retained separately. The latter compared a refraction image with a caustic control bearing the same name; separating the control paths fixed the helper. Its failed attempt contributes no timing samples. The corrected three-launch qualification passed before the timed matrix began.
