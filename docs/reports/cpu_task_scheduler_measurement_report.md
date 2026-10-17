# CPU task scheduler measurement and qualification report

This report preserves substantive measurements and run-specific qualification evidence recorded in `docs/cpu_task_scheduler.md` before its conversion to a current-design guide. The extracts describe their original source states and hosts; their pass counts, build-mode spellings, revisions, artifact locations, and platform limits are historical evidence, not a claim of fresh execution or current test inventory. Raw output remains in the artifact locations named by each acquisition. Incidental implementation chronology is retained in Git history rather than added here.

Current contracts and operating procedures are in [CPU task scheduler guide](../cpu_task_scheduler.md). Original source SHA256: `a671b8700ab6452fd4486dd0bd742aaf5d22c01499627c831b1f1522cdf03ad5` (UTF-8 text with normalized LF).

## Scope and integration qualification

The final CPU scheduler source was integrated with the GPU buffer-range synchronization changes through `6f0c546f` before the full build matrix:

- Windows ARM64 Debug, Optimize, and Final: the 11 selected engine, loader, ECS/rendering, tool, and test targets built; all seven primary CTest suites passed in each configuration.
- Native Vulkan scheduler/render coverage: all 18 selected cases executed and passed in each configuration, including recording overlap, worker-storage leases, caller/worker terminal failure cleanup, and main-thread render-pass ordering with descendants.
- Scheduler stress: the final 35 scheduler and cleanup tests passed 50 repetitions, for 1,750 passing cases. Coverage includes 1,024 tasks queued before execution, 8,192 nested callbacks, cancellation, dependency wait-cycle rejection, and terminal-entry exception propagation.
- Optimize asset-graphics integration: 133 tests passed; 21 pre-existing disabled tests were not run.
- Optimize placed-resource-memory integration: 98 tests passed and nine skipped unsupported device capabilities, including separate transfer queues, compatible virtual-buffer upload memory, and RGB32_FLOAT support.
- All 50 source-policy checks and self-tests passed, including the local exception-handler and source-format rules.
- Fresh Optimize Testbed and the representative skinned-CSG smoke target built and cooked their assets. The bounded Testbed window-capture test passed in 4.08 seconds, verified startup and shutdown logs, and rendered the scene through the mesh compute-emulation path.

The subsequent GPU active-buffer-range change, `aec212c4`, integrated without CPU source changes. A focused Optimize rebuild of ECS rendering, ECS graphics tests, placed-resource memory tests, native descriptor tests, and Testbed passed. ECS graphics and placed-resource memory suites passed again, as did all 18 native scheduler/render cases. Explicit native buffer-range coverage passed four cases and skipped one requiring a separate transfer queue; the new UAV-prefix, indirect-dispatch, and material active-frame-prefix cases passed. A fresh Testbed window capture passed in 3.93 seconds. All 50 source-policy checks and self-tests also passed against this integrated source.

Linux and Windows x64 were not built or run on this ARM64 host. Local test and benchmark artifacts are retained in the ignored build/artifact directories; the device-capability skips above are not counted as executed tests.

After legacy code removal, the 11 selected Optimize engine, renderer, loader, tool, test, and Testbed targets rebuilt successfully. All 58 selected Optimize checks passed: seven primary suites, 50 source-policy checks and self-tests, and the fresh Testbed window capture (4.11 seconds). The global suite also rebuilt and passed in Debug and Final. At that step the global suite contained 169 tests, including migrated large fan-in, callable-construction, lvalue-invocation, and native-worker terminal-failure coverage; tests specific to the retired pool/job implementation were removed.

The explicit MPMC verification adds two tests, bringing the global suite to 171 passing tests in Debug, Optimize, and Final. One combines the owner, four external producers, and four worker producers sharing scopes and direct submission, with 4,612 task completions checked exactly once. Worker producers consume a submitted task and its descendants before finishing their submission burst; cross-producer dependencies verify descendant completion visibility. The other checks 512 externally submitted main-thread callbacks and producer waits while only the owner pumps. All 100 Optimize repetitions passed (200 cases and 512,400 task callbacks), followed by all 50 source-policy checks and self-tests. The audit required no queue algorithm change: existing publication and claims already use the same mutex. After integrating GPU cleanup `b84e0445`, the affected Optimize graphics targets rebuilt, the task-graph suite passed, and native buffer-range checks passed four cases with one unavailable separate-transfer-queue capability skip.

## 2026-09-08 Windows ARM64 comparison

The local comparison uses Windows ARM64 on a Snapdragon X2 Elite Extreme X2E94100, with 18 usable logical processors (12 performance-class and 6 efficiency-class). Both designs use 17 native workers. The baseline mirrors the previous Frame configuration: 11 unpinned graphics workers and 6 unpinned project workers. The shared scheduler uses 11 pinned performance workers and 6 pinned efficiency workers.

Four counterbalanced epochs alternate the execution order of the designs. Each case has three warmups and eight measured samples per epoch, for 32 measured samples per design. The timed interval includes submission, execution, and completion joins. Initialization, serial reference calculation, output clearing, validation, and logging are excluded. Every output cell is compared with a deterministic serial reference after every run. Builds and other test runs are stopped during measurement.

The coarse case runs one graphics-sized parallel range. The simultaneous case submits independent heavy graphics and lighter project ranges. The nested case models six ECS systems, each starting an inner parallel range, including the previous pool ordering restrictions. The tiny-task case submits 1,024 short callbacks in eight waves of 128 in both designs; the legacy pool's fixed arena cannot reliably admit the whole burst at once. The batched tiny case computes the same outputs through one `parallelFor` range. These are CPU microbenchmarks on one machine, not measurements of application frame rate.

The final five-case run validated all 440 warmup and measured results against their serial references. All 24 untimed placement checks matched Heavy to Performance and Light to Efficiency, with zero pinning failures. The observed peak was 390 outstanding tasks. Median and nearest-rank p95 times are in milliseconds:

| Workload | Split median | Shared median | Split p95 | Shared p95 |
| --- | ---: | ---: | ---: | ---: |
| Coarse graphics range | 3.88875 | 3.82070 | 4.7897 | 4.5569 |
| Simultaneous systems | 4.11575 | 3.13275 | 4.8842 | 4.4755 |
| Nested systems | 11.13395 | 5.14780 | 11.8959 | 6.1818 |
| 1,024 tiny individual tasks | 2.10645 | 5.60850 | 2.2942 | 6.3072 |
| Same tiny work through `parallelFor` | 0.02915 | 0.26095 | 0.0682 | 0.3213 |

The simultaneous and nested cases reduce median elapsed time by about 24% and 54%. Coarse work is similar on this host. Individual tiny submissions still cost more than the legacy pool, and the legacy specialized range remains faster for the tiny batched case. The shared scheduler adds dependency, structured-lifetime, cancellation, and heterogeneous placement accounting; it does not make every scheduling pattern faster. Use bounded batches or `parallelFor` for fine-grained collections. Batching the tiny shared workload reduced elapsed time from 5.61 ms to 0.261 ms, about 21.5 times faster than its individual submissions.

The initial shared implementation took 30.75 ms for the tiny queue case because task transitions woke every worker. Per-class notifications and ready-work/parked-worker gating reduced that to 5.61 ms. An independent confirmation reproduced the final coarse-range result; the earlier 2.9 ms coarse measurement from another notification variant is not used as a claim for the final implementation. Local raw runs, checksums, placement records, build metadata, and historical variants are retained under the ignored `global_test_artifacts/cpu_scheduler_profile/` directory.

## Scope-cycle rejection regression qualification

Validation: the direct-cycle regression timed out against the original implementation. All four new wait cases, the full Linux Debug CPU task suite, and all 52 source-policy checks/self-tests passed after the fix.

## Batched-range Linux comparison

Seven opt-in `CpuTaskProfile` sampling cases (recorded through the `core/perf` profiler opt-in API, excluded from the normal `nwb_cpu_task_tests` run) record raw samples and validate deterministic outputs. They have no timing pass threshold. On Linux x64 Optimize, AMD BC-250 (6 cores / 12 logical processors), three ABBA epochs gave 48 measured samples per design with zero placement failures. Builds and other tests were stopped during measurement. Times below are complete benchmark-case medians in microseconds, not per-element costs or application frame times:

| Workload | Before | After |
| --- | ---: | ---: |
| 128 single-chunk ranges, one worker | 76.340 | 31.815 |
| 32 tiny batched ranges, four workers | 1337.584 | 945.188 |
| Two coarse ranges, four workers | 1102.427 | 1082.951 |
| Nested ranges, one worker | 101.128 | 90.746 |
| Nested ranges, four workers | 1245.630 | 1091.935 |

The one-worker nested tail varied in the first combined run. A focused six-epoch ABBA confirmation (96 samples/design) measured median 100.751 → 91.421 µs and p95 131.455 → 121.088 µs. Unrelated-scope and cancellation-history controls stayed similar; their optimizations are later steps. Raw XML, summaries, and the baseline binary are retained under ignored `__cmake/cpu_scheduler_followup/step2/`.

Validation includes Debug and Optimize CPU suites, an inline exception/descendant-retirement case, caller/worker identity, external producer routing, cancellation, nested ranges, and all 52 source-policy checks/self-tests. Physical heterogeneous-class cases skip on this homogeneous host.

## Negative-scope-search comparison

The zero-worker scope benchmark (128 unrelated roots feeding a shared 64-task chain ahead of 32 joined tasks) improved from median 2865.730 → 29.494 µs and p95 2898.091 → 32.984 µs. This is a deliberately dependency-heavy microbenchmark. Three ABBA epochs produced 48 samples per design; output/lifetime checks all passed. Other workload medians remained similar or improved, while some worker-based p95 values varied between runs. Raw results and the baseline binary are under ignored `__cmake/cpu_scheduler_followup/step3/`.

Debug/Optimize CPU suites and all 52 policy checks passed. Eight wait cases passed 50 repetitions (400 cases), including a newly published prerequisite, recycled node slots, nested scope identities, and concurrent waiters traversing unscoped prerequisites.

## Canceled-generation lookup comparison

The 512-lookup benchmark with 4096 canceled handles improved from median 525.350 → 61.685 µs and p95 561.969 → 100.058 µs over three ABBA epochs (48 samples/design). Other median controls stayed similar, with the coarse control varying by about 4%. Raw results and baseline are under ignored `__cmake/cpu_scheduler_followup/step4/`.

Debug/Optimize CPU suites and all 52 policy checks passed. Added coverage alternates successful/canceled generations through 1024 slot reuses, checks independent slots with matching generation numbers, and verifies original cancellation results after both successful and canceled parallel ranges reuse those slots.

## Optional-profiling overhead and qualification

The disabled-path ABBA comparison against step 4 used 48 samples/design. Single-chunk median was 33.061 → 33.289 µs and tiny-batch median 921.848 → 932.381 µs; the other median controls ranged from about -5% to +4.4%, with worker p95 variation. These measurements establish the local disabled overhead rather than claiming profiling itself is free. Raw comparisons and the baseline are under ignored `__cmake/cpu_scheduler_followup/step5/`.

Final validation: the Linux Debug build passed for CPU/GPU tasks, perf/telemetry, graphics, ECS/rendering, native smoke tests, asset tools, FBX conversion, and Testbed. All ten selected CTest targets passed, including the three native Vulkan/asset suites; all 52 source-policy checks/self-tests passed. The final Debug and Optimize CPU suites each ran 68 cases: 66 passed and two physical heterogeneous-class cases skipped on this host. Eight CPU profiling cases cover disabled capture, labels/frame attribution, buffer wrap/overflow, stale epoch rejection, native idle lanes, foreign labels, zero capacity, and concurrent capture changes. Four adapter tests verify perf/telemetry publication, owner-thread delivery, disabled-sink draining, and bounded collection. Native tests retain capability-based skips for unavailable queues/extensions. Windows builds and application frame-rate measurements were not performed on this Linux host.

The Linux Debug build, eight selected CPU/GPU/graphics/telemetry/native test targets, the Optimize CPU suite, and all 52 policy checks passed. Capability-dependent tests retained their existing skips.

## 2026-10-01 queue, dependency, retirement and wakeup comparison

The original baseline was preserved before edits. Its task-domain sources match revisions `45f142ae8` through `ea1b01af4`; intervening upstream changes were in the UI domain. Final Linux x64 Optimize comparisons on AMD BC-250 used three counterbalanced epochs, 96 samples per design/workload, and no concurrent builds or heavy tests. Times are whole-operation medians:

| Workload | Original | Final |
| --- | ---: | ---: |
| Join 512 tasks, no unrelated ready prefix | 66.903 µs | 47.495 µs |
| Join 512 tasks behind 4,096 unrelated ready tasks | 14.086 ms | 0.112 ms |
| Join 512 tasks behind 16,384 unrelated ready tasks | 185.502 ms | 0.890 ms |
| Submit 1,024 nested prerequisites, no dependent chain | 24.963 µs | 23.266 µs |
| Submit 1,024 nested prerequisites with 4,096 dependent nodes | 1.952 ms | 0.077 ms |

Separate comparisons isolated the later passes against the first optimized implementation, with 48 samples per design/case:

| Workload | First optimized pass | Final |
| --- | ---: | ---: |
| 128 single-chunk ranges | 34.618 µs | 24.199 µs |
| 1,024 individual tiny tasks, zero workers | 201.509 µs | 147.912 µs |
| 1,024 dependent tasks, zero workers | 232.464 µs | 160.398 µs |
| 16,384 empty-scope joins | 324.483 µs | 51.785 µs |
| 1,024 individual tiny tasks, four workers | 2.239 ms | 2.145 ms |
| Two coarse ranges, four workers | 1.083 ms | 1.082 ms |

The four-worker tiny case was slower in an intermediate lock-consolidation build; the final notification-gating comparison recovered that loss. Multithreaded tiny workloads remain variable, so the small differences are not general throughput guarantees. These measurements establish host scheduler costs, not application frame-rate gains.

Debug and Optimize each passed the CPU, GPU, and global suites: 74 CPU cases passed with one heterogeneous-hardware skip, all 347 GPU cases passed, and all 136 global cases passed. Twenty-one focused CPU cases passed 100 repetitions, including 9,600 cancellation-race joins. Regressions cover removed/running anchors, publication changes, duplicate fan-in edges, late cycles, stale generations, capture-destructor reentry, empty/canceled join profiling, and notification races for handle/scope/scheduler joins.

Opt-in `CpuTaskProfile` cases cover prefix/fan-in scaling, individual and dependent tasks at zero/one/four workers, empty/completed joins, and the existing range controls. Original and intermediate binaries, raw XML, method files, hashes, and final results are under ignored `__cmake/scheduler_optimization/2026-10-01/`; the direct final comparison is `final_original_comparison/final_summary.json`. The historical source-policy scanners referenced above were removed before this revision. This work followed the current `.helper/` standards instead.

That pass left per-scope notification routing, claim/setup fusion, and automatic nested chunk tuning unproven; those changes require additional representative workload evidence.

## 2026-10-01 positive contribution-cache comparison

An isolated Linux x64 Optimize comparison used 512 ready prerequisites feeding a shared chain and two scoped continuations. Native-worker cases occupy all workers with gate callbacks that enter cooperative scope joins; ordinary whole-scheduler joins over the same graph serve as controls. Three counterbalanced epochs produced 72 validated samples per design and shape:

| Scoped join workload | Before | Tagged contribution cache |
| --- | ---: | ---: |
| 128 continuation nodes, zero workers | 0.978 ms | 0.082 ms |
| 512 continuation nodes, zero workers | 4.941 ms | 0.132 ms |
| 2,048 continuation nodes, zero workers | 43.098 ms | 0.335 ms |
| 2,048 continuation nodes, one worker | 46.529 ms | 1.101 ms |
| 512 continuation nodes, two workers | 9.756 ms | 1.958 ms |
| 2,048 continuation nodes, two workers | 51.685 ms | 7.445 ms |

Zero-worker whole-scheduler controls remained near 0.25 ms for the largest graph. Concurrent wait identities still invalidated the shared cache at that stage, limiting the two-worker improvement. Short one-worker controls varied substantially, so their small differences are not treated as wins. These synthetic graph measurements do not establish application frame-rate gains. Source/binary hashes, raw samples, and the isolated comparison are retained under ignored `__cmake/scheduler_residual_audit/2026-10-01/`.

The isolated candidate passed the existing Debug and Optimize CPU suites (74 passed and one hardware-dependent skip each), plus 100 sibling/publication regression checks per configuration. Persisted regressions additionally cover shortest-path sibling frontiers, recycled positive slots, and structured-parent completion. Opt-in `CpuTaskProfile` cases preserve the shared-chain workloads at zero/one/two workers and same-graph whole-scheduler controls.

At `f570ff93c`, the combined source passed Debug and Optimize with 79 CPU tests and one hardware-dependent skip per configuration. Scope-wait and cancellation cases also passed 50 repetitions per configuration (2,000 checks total), and all 31 opt-in CPU benchmarks passed. A final counterbalanced complete-binary comparison collected 32 samples per design/control: the unrelated 4,096-task ready-prefix control changed from 110.066 to 114.572 microseconds (about 4% slower); the other five controls stayed within 2%. The small control changes are not claimed as improvements. Combined build logs, XML, repeated-run logs, and control samples are under ignored `__cmake/scheduler_recheck/2026-10-01/final_validation/`.

## 2026-10-01 unrelated-leaf shortcut comparison

An isolated Optimize confirmation against `f570ff93c` used the same explicit scope-search object position, three counterbalanced epochs, and 48 samples per design/control. Joining 512 scoped callbacks behind 4,096 unrelated ready tasks improved from 117.055 to 91.327 microseconds (22%); the 1,024-task prefix improved from 71.040 to 65.097 microseconds. The no-prefix control stayed at 46.410 versus 46.419 microseconds, and the coarse-range control stayed near 1.081 milliseconds. Small controls varied: the single-chunk median was 24.928 versus 25.800 microseconds, and concurrent positive-chain joins varied by roughly 2–4%; those differences are not improvements or application frame-rate claims. The full current Optimize CPU suite passed for the isolated candidate. Source/binary hashes, raw XML, and the comparison are retained under ignored `__cmake/scheduler_continuation/2026-10-01/cpu_scope_lookup/`.

## 2026-10-01 shared-scope proof and notification comparison

Independent variants and the combined implementation passed the Optimize suite with 81 cases and one hardware-dependent skip each. Each changed variant additionally passed 20 repetitions of 24 progress, scope-wait, cancellation, MPMC, and retirement cases. New regressions cover an unscoped main-thread prerequisite becoming ready while its scoped consumer is waiting, plus concurrent same-scope joins with late structured descendants and scope reuse. Sixteen opt-in `CpuTaskWaitProfile` cases preserve the notification and shared-scope workloads with checksum checks and XML samples.

A balanced six-order comparison collected 48 samples per design and workload against the preceding leaf-shortcut implementation. Median host times in milliseconds:

| Workload | Before | Combined |
| --- | ---: | ---: |
| 4,096 ready callbacks, no workers, four external scope waiters | 8.662 | 0.581 |
| 4,096 ready callbacks, no workers, sixteen external scope waiters | 235.844 | 0.871 |
| 4,096 ready callbacks, four workers, four external scope waiters | 25.647 | 3.727 |
| 4,096 ready callbacks, four workers, sixteen external scope waiters | 124.700 | 4.015 |
| Shared 512-root/2,048-chain graph, two workers joining one scope | 9.230 | 2.187 |
| Shared 512-root/2,048-chain graph, four workers joining one scope | 9.748 | 4.214 |

Different-scope controls measured 9.451 → 8.841 ms with two workers and 9.659 → 8.662 ms with four. Coarse ranges stayed near 1.084 ms, repeated empty joins near 51.8 microseconds, and tiny batched ranges near 0.83 ms. One-worker timings remained bimodal and are not improvement claims.

The zero-worker/no-waiter control changed from 504.668 to 537.621 microseconds in the combined binaries, a real observed 6.5% difference. An isolated relink shifted benchmark callbacks by 32 bytes while preserving scheduler hot-function addresses and bytes: the notification-only implementation changed from 507.478 to 532.776 microseconds, compared with 538.125 for the combined implementation. Scheduler hot-function addresses and bytes stayed identical, and the relocated work/invoke callbacks matched the combined addresses and bytes exactly. That reproduces most of the difference through code placement without changing scheduler behavior. No diagnostic padding is retained in production. These synthetic host measurements do not establish application frame-rate gains.

The contribution cache still retains one scope's results at a time. Additional cache storage for different scopes, more selective notification routing, claim/setup fusion, and nested chunk-policy changes were not added without a separately qualified implementation. Frozen source/object/binary hashes, suite logs, raw samples, and the address-control diagnostic are retained under ignored `__cmake/scheduler_continuation/2026-10-01/cpu_waiter_variants/`.

Final combined Debug and Optimize builds each passed 81 CPU tests with one hardware-dependent affinity skip. The 24 progress, scope-wait, cancellation, MPMC, and retirement cases passed 20 repetitions in each configuration (960 checks), and all 47 opt-in CPU benchmarks passed with checksum and sample properties. These final runs validate the combined code; the controlled performance comparisons above remain the timing evidence. Logs, XML, and source/binary hashes are under ignored `__cmake/scheduler_continuation/2026-10-01/final_validation/`.
