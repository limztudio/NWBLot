# Renderer optimization implementation record

This work follows the September 12, 2026 reflection/refraction audit at source `50f258fcf`. Each completed step is committed and pushed separately. Production changes stay in their owning domains and follow `.helper/standard.md` and `.helper/exception.md`.

Correctness takes priority over an apparent timing reduction. Shader candidates require the relevant image, boundary, and lifecycle checks. Performance comparisons use frozen baseline/candidate builds and assets, matched workloads, independent balanced blocks, and actual completed GPU samples. Builds, cooking, GPU tests, and timing acquisitions run sequentially. No failed or unfavorable trial is discarded to improve the result. A rejected experiment is restored and its outcome is documented.

| Step | Work | Status |
| --- | --- | --- |
| Preparation | Matched AVBOIT timing fixture and reusable two-build benchmark | Complete |
| 1 | Coalesce AVBOIT coverage atomics | In progress |
| 2 | Skip temporal dispatches that cannot update history | Draft prepared |
| 3 | Avoid spatial halo/geometry loads for uniformly ineligible tiles | Draft prepared |
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
