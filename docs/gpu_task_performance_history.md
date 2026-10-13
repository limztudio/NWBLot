# GPU task performance and validation history

This report preserves recorded GPU graph compiler CPU and native buffer-workload measurements from 2026-10-01. The measurements apply to the stated binaries, hardware, graph shapes, and capture conditions. Current architecture and benchmark procedures are described in the [GPU task guide](../core/task/gpu/README.md).

## Scheduler measurements: 2026-10-01

On 2026-10-01, the first completed optimization pass (`68e139e12`) compared Linux x64 Optimize against the preserved original scheduler using three counterbalanced epochs and 66 raw samples per design/shape on AMD BC-250, with no concurrent builds or heavy tests. Graph declaration and dependency analysis were outside the timed assignment call:

| Queue-assignment workload | Original median | `68e139e12` median |
| --- | ---: | ---: |
| 4,096 mixed tasks, one dependency | 111.097 ms | 2.642 ms |
| 4,096 mixed tasks, disjoint dependency pairs | 111.688 ms | 2.886 ms |
| 4,096 independent mixed tasks | 1.358 ms | 1.373 ms |

Peak scratch for either partial graph is 2,406,440 bytes versus the original 2,314,280 bytes, an additional 90 KiB (4%). The intermediate full-ancestor-matrix implementation needed 4,407,296 bytes; the in-place construction removes that temporary matrix. Independent, ordered, conservative, merged-chain, and single-queue controls retain their original scratch footprints. The independent timing control remained essentially flat.

Additional counterbalanced measurements isolated analysis and callback work:

| Workload | Before the respective change | After |
| --- | ---: | ---: |
| Analyze 4,096-task shortcut chain | 64.749 ms | 1.065 ms |
| Analyze 4,096 consumer-first paired tasks | 1.894 ms | 0.652 ms |
| Analyze 4,096 producer-first paired tasks (control) | 0.614 ms | 0.625 ms |
| Analyze 4,096-task reverse chain (control) | 0.746 ms | 0.745 ms |
| Resolve and dispatch 4,096 recorded callbacks | 9.159 ms | 0.103 ms |
| Resolve and dispatch 4,096 accepted callbacks | 9.132 ms | 0.105 ms |

Analysis figures are medians of six process results per design; callback figures use 24 raw samples per design/case and compare the original scalar algorithm with the production index in the same executable. Eight-callback controls stayed flat. These are CPU pipeline measurements, not native GPU throughput or application frame-rate measurements.

That pass's Debug and Optimize builds each passed all 347 GPU tests, plus the CPU and global suites. A separate comparison matched 44,928 original/final assignment-and-score records across 192 graphs, including mixed costs, merges, queue moves, sparse queue IDs, topology permutations, and external queue pressure. Scalar-reference tests cover partial tiles, reversed declaration order, graph cycles after indexed traversal, callback duplicate/range/generation failures, and mask-free queue-load updates.

Raw comparisons, preserved binaries, and source hashes are under ignored `__cmake/scheduler_optimization/2026-10-01/`; final original comparisons are in `final_original_comparison/`, and per-pass controls are in `continued/`. Subsequent dense-graph checks exposed additional cases outside those workloads. Broader native recording/submission concurrency needs representative device workloads and a separate review of recording-state ownership.

A deeper same-day check found that 256 tasks depending on all earlier tasks could repeatedly grow the dependency-pair table: the old pair hash clustered sequential indices, and the probe failed with `std::bad_alloc` under a 1 GiB process address-space cap. The shared byte hash removes this failure in both analysis and telemetry. At 128 tasks, an ABBA comparison reduced Analyze from 18.077 ms to 1.398 ms without changing scratch usage. Stopping reduction once every direct target is reached then reduced a 512-task hash-only control from about 256.7 ms to 24.1 ms, and a 1,024-task case from 2.473 s to 117.056 ms. These large cases contain 130,816 and 523,776 raw edges respectively; input processing and general reachability storage remain real costs.

The dense dependency regression covers explicit, inferred, and overlapping telemetry flags, bounded scratch, and an unreached middle target after a later target is reached. The new disabled dense benchmarks retain raw edge counts and CPU/scratch properties. Probe source, binary hashes, process-load snapshots, raw samples, and capped-failure logs live in `fresh_review/` below the ignored optimization artifact root. Measurements are CPU compiler costs, not native GPU or application frame-rate gains.

At `f570ff93c`, directed reachability construction copied each completed consumer row's nonzero word span, reused range storage, and scanned exact bounds after in-place symmetrization. A confirmation linked the original and candidate reachability objects in the same position to control code layout. Three counterbalanced epochs produced 30 samples per design: at 16,384 mixed tasks, disjoint pairs improved from 38.539 to 35.067 ms, 32-task chains from 39.152 to 34.714 ms, and 32-task branches from 38.223 to 35.067 ms. At 4,096 tasks, pairs improved 3.6%; chain and branch controls stayed within 1%. Peak scratch remained 34,791,464 bytes for the largest shapes. Existing all-pairs scalar-reference tests cover permuted producer/consumer IDs, empty leaf spans, and partial word boundaries. Raw logs, hashes, and this controlled comparison are under ignored `__cmake/scheduler_recheck/2026-10-01/closure_range/matched_layout/`.

A matched-link-position comparison used the same explicit assignment-object slot for both designs, six counterbalanced epochs, and 60 samples per design/case. For 4,096 graphics-only tasks with an unused compute queue, assignment improved from 1.869 ms to 1.135 ms; for 16,384 tasks it improved from 35.758 ms to 5.176 ms. The larger case's peak scratch fell from 34,791,464 to 868,392 bytes. Single-queue and mixed-capability controls showed no regression in that comparison; their small differences are not claimed as wins. An earlier comparison moved the assignment object between archive extraction and an explicit first-object slot and produced misleading single-queue drift, so its timing ratios are not used.

The capability regression compares all assignment and score fields against the sole-route reference, including external queue pressure, and retains nonzero overlap for independently forced groups on different physical queues (including two queues of the same class). Six disabled capability-route benchmarks preserve one/two-queue cases at 1,024, 4,096, and 16,384 tasks. Raw samples, object/source hashes, and matching link commands are under ignored `__cmake/scheduler_residual_audit/2026-10-01/capability_union_candidate/matched_layout/`. These figures measure compiler CPU time and scratch memory, not GPU execution time.

At `f570ff93c`, the combined Debug and Optimize builds each passed all 351 GPU tests, 79 CPU tests (one hardware-dependent CPU skip), and 136 global tests. All 126 opt-in GPU benchmarks and 31 CPU benchmarks passed. The final assignment comparison again matched all 44,928 original reference records exactly. A headless native Vulkan probe on AMD BC-250 passed 120 graphs across compile-order, serial-frontier, parallel-frontier, and merged-packet recording modes. That probe uses no-op tasks, zero resource barriers, one physical queue, and no validation layer; it checks recording/submission completion, not representative rendering throughput. Final logs, XML, source/build manifests, and probe results are under ignored `__cmake/scheduler_recheck/2026-10-01/final_validation/`.

A later compact-row comparison against `f570ff93c` used matching six-owner object sets and header layouts for each design, frozen libraries, a pinned CPU, and counterbalanced process order. Declaration and analysis were outside the timed assignment call. Probe tasks mixed Graphics/Compute commands with Large cost hints; the durable fixtures also vary cost classes. The final comparison matched all 73 assignment/score fingerprints across 444 shape cases, including reversed IDs, partial last tiles, merged groups, fan-in/out stars, and interleaved components.

| Queue-assignment workload | `f570ff93c` median | Compact-row median | Peak scratch before / after |
| --- | ---: | ---: | ---: |
| 16,384 contiguous pairs | 34.590 ms | 7.361 ms | 34,791,464 / 1,761,320 B |
| 16,384 contiguous 32-task chains | 34.855 ms | 7.708 ms | 34,791,464 / 1,761,320 B |
| 16,384 interleaved 32-task chains | 57.355 ms | 53.705 ms | 34,791,464 / 35,053,608 B |
| 16,384 fan-out star | 44.826 ms | 17.570 ms | 34,791,464 / 18,475,040 B |
| 16,384 fan-in star | 45.313 ms | 27.533 ms | 34,791,464 / 18,475,040 B |
| 4,096 dense layers | 7.072 ms | 6.383 ms | 2,406,440 / 2,471,976 B |
| 4,096 tasks in two dense components | 5.409 ms | 4.684 ms | 2,406,440 / 1,456,168 B |

In the dense controls above, retaining computed row bounds used 16 bytes/task on x64, at most 1/32 of the raw dense matrix. Total assignment peak grew 2.7% at 4,096 tasks and 0.75% at 16,384 tasks. Smaller dense rows released those bounds before matrix allocation.

Warm repeated controls retained the small-row and shortcut scratch footprints. At 1,024 tasks, dense controls were about 3–4% slower (roughly 30–35 microseconds); startup and code-layout variation makes tiny timing changes unsuitable as performance claims. Highly interleaved components can still require the dense representation. These results measure compiler CPU time and scratch, not native GPU execution or application frame rate. Scalar-reference tests exercise compact mixed-cost masks across queue moves and topology reorderings; allocation, generation, failure-reset, and compact/dense/ordered reuse cases cover representation boundaries. Raw XML, comparison summaries, source/object/header hashes, and exact link commands are under ignored `__cmake/scheduler_compact_reachability/2026-10-01/`, especially `final_comparison/` and `final_small_controls/`.

The native continuation used real 16 KiB buffers with one or 32 declared regions, shared-source, copy-chain, eight-way tree, and distinct-upload graphs. Every output was copied to host readback, waited on its accepted completion token, and checked word-for-word against varying patterns. The AMD BC-250 exposes one physical queue; multiple logical recording slots exercise CPU recording concurrency, not multi-queue GPU overlap. Four counterbalanced processes checked 4,096 graphs across 128 configurations. Plan, barrier, packet, seed, and frontier counts matched the reference.

At 256 tasks and 32 regions with four CPU workers, batching consecutive initial-state subsets from the same immutable packet source reduced shared-source compile-order execution from 93.829 to 45.996 ms (recording: 88.648 to 40.605 ms). Shared-source parallel recording improved from 93.565 to 46.314 ms, copy-chain compile order from 90.808 to 42.585 ms, tree parallel recording from 80.586 to 33.746 ms, and distinct-upload parallel recording from 81.100 to 34.524 ms. These are host execution/recording times for the buffer workload, not GPU kernel or rendering frame-rate gains. One-region and merged-packet controls remained essentially flat (aggregate median ratios 1.002 and 1.007).

The chosen copy-back variant keeps compile-order shared-source peak graph-owned host arena usage at 10,770,580 bytes. Shared-source parallel recording grows from 10,812,052 to 10,956,308 bytes (144,256 bytes, about 1.3%); retained storage grows by 143,312 bytes. These counters cover the graph-owned host arena, not VRAM or every backend/child allocation. A persistent snapshot-cache candidate and a storage-exchange candidate were discarded because they retained more memory without a useful performance advantage. Frontier-depth and unused serial-frontier setup experiments also failed to show a reliable gain and were not promoted.

The selected native candidate additionally passed 128 readback graphs with the Vulkan validation layer enabled and zero logged errors, fatal messages, or assertions. Raw paired samples and memory counters are in ignored `__cmake/scheduler_continuation/2026-10-01/native_seed_merge/copy_comparison/`; validation-layer output is in `batch_local_copy/validation.*`. Source/build manifests preserve the exact measured variants. The intentionally guard-disabled regression binary fails the ownership-prefix case, confirming that the regression exercises the required guard.

Final combined Debug and Optimize builds each passed 358 GPU, 81 CPU, 136 global, and 72 graphics-resource tests, with one hardware-dependent CPU skip. All 135 GPU and 47 CPU opt-in benchmark workloads passed. The final recompiled differential matched all 44,928 preserved assignment/score records; compact-path scalar and representation-boundary regressions passed in both builds. The final native binaries checked another 1,024 buffer graphs and matched reference plan/barrier/packet/seed/frontier/readback fingerprints, plus 128 graphs with the Vulkan validation layer enabled and no logged errors. Final logs, XML, source/binary/library hashes, and build commands are under ignored `__cmake/scheduler_continuation/2026-10-01/final_validation/`.

Renderer M4 measurement evidence is preserved separately in the [M4 qualification history](../tests/ab/async_shadow_m4/qualification_history.md).
