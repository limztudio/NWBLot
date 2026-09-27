# Project-wide optimization audit — September 27, 2026

This pass extends the earlier graphics audit to project-owned engine foundations, ECS/content, assets and pipeline tools, diagnostics, platform/runtime code, build configuration and tests. Baseline `9c0c9279b`; final qualification commit `59793fc15`. Changes follow `.helper/standard.md` and retain owning-module boundaries.

## Accepted steps

| Commit | Scope | Result |
| --- | --- | --- |
| `99506099e` | Utilities and benchmark build | Correct native-character format constants, stderr streams and a missing logging include; fix the gather benchmark's stale dereference of a renderer reference. |
| `793821947` | Stale code | Remove an empty logger translation unit, unused CMake finder, write-only parser token state, dead FBX constants and a startup-only retained Testbed handle. |
| `db8596b39` | ECS frame work | Keep cutter fill counts in the existing group table; select the attachment basis before constructing one affine matrix. |
| `6637710d1` | Asset gathering | Deduplicate sorted exact input paths before reading/checksumming. Repeated mergeable input is appended once; a regression case verifies one merge and 6 bytes instead of two merges and 10 bytes. |
| `b00e10762` | Shader cook planning | Replace pairwise canonical dependency comparisons with a scratch lookup set; skip the second inherited-define merge when dependencies did not grow. Preserve first-seen order and authored/first-inherited precedence. |
| `c62bd8e96` | Name decoding | Allocate output only after a symbol resolves, and copy unchanged spans in batches. Long plain text, unknown hashes and invalid token boundaries retain their original storage. |
| `94e15ecc2` | Telemetry log encoding | Convert directly into the retained payload buffer; retain a temporary only when source text aliases that buffer. Preserve Unicode conversion, embedded NULs and byte layout. |
| `6ce717f96` | Memory telemetry | Read immutable owner identity during scope registration, avoiding discarded tracker walks and locks. Keep full statistics collection in the publication pass. |
| `59793fc15` | Test fixture corrections | Accept a zero-duration join for an already complete task, retaining event identity/count checks. Permit timing-only reflection scenes without framebuffer diagnostics while preserving actual capture requirements. |

These are proven reductions in operations/allocation or correctness/cleanup changes. The stress measurements below qualify the combined renderer; they do not isolate a frame-rate improvement attributable to each CPU or tooling change. Rendering presets and effect budgets were not changed in this pass.

## Validation

The complete Windows ARM64 optimized build passed. The full 104-entry CTest run initially produced 100 passes, two failures and two capability-dependent skips. Both failures were corrected in `59793fc15`; the affected CPU task suite and three reflection checks then passed (4/4). The aggregate qualification is **102 passed and two skipped**, combining the full run with those targeted reruns, rather than claiming a second complete all-green invocation.

The two skips are expected capability selections on this host:

- `nwb_transparent_multi_sw_capture_smoke`: the natural hardware shadow route is selected on RayQuery-capable hardware. The forced hardware-RT-disabled tests below run independently.
- `nwb_csg_visible_capture_smoke`: Meshlets are unavailable, so the natural compute-emulation route is selected. Its compute-emulation counterpart passed.

All eight forced hardware-RT-disabled GPU-validation smoke cases passed: overlap, early/late CSG, caustics, optics, skinning, stress resize and GI. These runs use `--disable-hardware-ray-tracing --gpudbg` and verify that RayQuery, ray-tracing pipeline, acceleration-structure and acceleration-structure descriptor/layout capabilities are disabled. The full suite also exercised reflection/refraction, duplicate transparent objects, optical feedback, gallery captures, crash handling and RGD decoding.

The corrected task profiling case passed **100 consecutive repetitions**. Queue-delay and execution events still require positive durations for their deliberately delayed work; an already completed join may legitimately measure zero. Reflection temporal omission proof and controlled capture checks both passed after the fixture correction.

Fifteen new regression cases cover repeated merge inputs, dependency order/defines/checksums, name decoding and allocation counts, telemetry Unicode/alias/buffer reuse, and arena identity traversal/lifetime. Existing concurrency, ECS transform/CSG, parser, CLI, conversion and crash/logger tests remain part of qualification. All 26 changed C++ source/header files passed CRLF, exact EOF/separator and unnamed-namespace checks.

Memory telemetry intentionally has one accepted heap sample per published frame. The removed discarded setup sample could previously advance the sampled heap peak; exact allocation accounting and the full publication snapshot are retained.

## Stress qualification

Both fresh qualification runs exceeded the strict **greater than 60 FPS average** target on the Snapdragon X2 Elite Extreme X2E94100, using the Qualcomm Adreno X2-90 GPU.

| Route | Average FPS | Average frame time | Accepted presentations | Measured duration | Lowest approximately 0.5-second interval FPS |
| --- | ---: | ---: | ---: | ---: | ---: |
| Hardware RT | 69.93 | 14.30 ms | 2,098 | 30.001641 s | 66.54 |
| Hardware RT disabled | 61.98 | 16.13 ms | 1,860 | 30.0073084 s | 60.21 |

The lowest interval value is not the individual worst-frame rate. Software has limited headroom; these results qualify this scene and its accepted presets, not every camera, workload or machine. Both runs passed capture validation, runtime identity checks and performance qualification. Software capture reuse was explicitly verified. Optical correctness is covered by separate smoke cases; detailed optical counters are not collected in the timing runs.

Run the tracked stress harness with the existing accepted 20-body settings: 10 opaque plus 10 transparent, animation enabled, 1280×900, SSR steps 96, transparent shadow sampling `temporal_one`, software shadow backend `automatic`, budget 256 MiB, directional/point resolution 512/256, fitted-volume coverage and compact-cross5 blocker search. Hardware uses caustic photon divisor 2 and every-frame capture. The approved software approximation uses divisor 4, capture reuse for one frame, and `--disable-hardware-ray-tracing`. Both retain the requested transparency, reflection, refraction, caustics, shadows and surfel GI workload. Each fresh run has five seconds of warmup and at least 30 actual seconds of accepted native presentations; CPU and reflection diagnostics are disabled; normal harness GPU timing collection remains enabled.

The local runner and raw identity/timing evidence are under `__artifacts/project_audit_20260927/`: `final_hw/result.json`, `final_sw/result.json`, `final_build.log`, `final_tests.log`, `final_ctest_detail.log`, `fixture_tests.log`, `profile_repeat_tests.log` and `regression_case_evidence.json`. These generated artifacts remain local; the results and reproduction settings are recorded here in tracked documentation. Reproduction uses `tests/smoke/stress_timing_smoke.py`; the local `run_stress.py` records its exact accepted arguments.

## Coverage and stop point

- Foundations: allocator ownership/accounting/scratch reuse; global container, string/name, process/I/O contracts; CPU task chunking, wake/wait and profiling paths; GPU task work from the preceding graphics pass.
- ECS/content: world/query/scheduler/message storage, scene and pose/palette updates, model attachments, renderer bindings, mesh providers, CSG grouping/topology, UI lifetimes and Testbed startup/input/shutdown.
- Assets/tools: asynchronous asset ownership, filesystem I/O/staging/rollback, metadata parsing, cook plans/cache integrity, material/shader hooks, mesh/model/texture payloads, gathering, conversion utilities and pipeline CLIs.
- Runtime/platform: frame/input loops, logging and asynchronous ownership, telemetry/perf, crash transport/retention, loader/launcher, configuration and project-facing vendor integration.
- Tests/build: the complete configured Windows ARM64 optimized build and registered tests, plus dedicated hardware/software stress qualification.

Independent domain reviews and a final combined diff review found no further demonstrated small, safe change within the inspected paths. Public APIs are not classified as stale solely because this repository has few callers. Vendor implementations were not rewritten or upgraded. Linux/other OS paths received source review, not execution on this Windows host.

Larger candidates remain profiling work: CSG cap topology traversal needs representative workloads and ordering/failure qualification; removing X11's synchronous window query requires Linux resize/map/visibility testing; asynchronous logger copies establish ownership and need an explicit lifetime redesign before removal. No speculative cache or behavior change was added to claim that every possible optimization has been exhausted.
