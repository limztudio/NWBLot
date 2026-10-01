# Automatic queue placement

Task declarations describe work, resources, dependencies, and scheduling cost. The compiler chooses a legal physical Graphics, Compute, or Transfer queue before command recording, using command capabilities, resource sharing and ownership, dependency crossings, and opportunities for overlap. Tiny tasks favor locality, and compatible tasks that explicitly merge with their predecessor are placed together.

Overlap scoring uses task-relation bitsets and per-queue cost groups instead of scanning every assignment for every candidate. Merged groups exclude the union of their members' ancestors, descendants, and members, so only work independent of the entire packet contributes overlap. Moving a task between queues updates the cost index before later scoring. Independent graphs and total-order chains retain their reachability shortcuts; graphs without relation rows allocate no cost-mask metadata.

Partial graphs use compact per-row word spans when their storage plus temporary bounds and offsets saves roughly 25% of a full matrix. Small rows and broad spans retain dense storage; worst-case storage remains quadratic. Global task-bit positions are preserved. Both forms build the symmetric relation index in place using bounded 64-by-64 tiles, and topological ranks recover strict reachability direction. Benchmarks report peak scratch usage as well as CPU time. Placement policy and declaration opt-ins for parallel command recording are preserved.

Dependency reduction stops traversing after the last direct consumer's topological rank or once every direct consumer is reached. Analysis and telemetry share a byte-mixed dependency-pair hash so dense sequential IDs do not trigger pathological hash-table growth. Stable topological sorting normally scans declaration IDs; excessive rescanning activates a hierarchical ready bitmap that preserves the exact lowest-ready-ID order. Ordinary producer-first graphs retain the scan path without bitmap allocation.

Recorded and accepted callback bindings are validated and indexed before execution. Arrays of at most eight callbacks use an allocation-free linear lookup; larger arrays use a scratch-backed index. Bindings remain immutable through the operation, while callback-owned context may change. Invocation stays in compiled task order, and lookup allocates nothing after native acceptance.

Built-in copies, uploads, clears, and resolves derive their command requirements internally. A custom recorded task declares its implementation's requirements once:

```cpp
struct ExampleComputeTask{
    static constexpr GpuTaskCommandRequirements s_CommandRequirements{ GpuQueueCapability::Compute };

    struct Payload{
        // Task-specific data.
    };

    static bool record(const Payload& payload, CommandList& commandList, const GpuTaskRecordContext& context);
};

const GpuTaskId task = graph.addTask<ExampleComputeTask>(taskDesc, Move(payload));
```

When a task implementation records different command kinds depending on its payload, provide `static GpuTaskCommandRequirements commandRequirements(const Payload&)` instead. Every required capability must be supported; each nonempty alternative mask additionally requires at least one alternative. Built-ins preserve native and hook alternatives independently. A task that records native commands must declare one of these contracts. The metadata-only `addTask` overload accepts command requirements for analysis and tooling.

Framework contracts for presentation, existing external submission timelines, and imported ownership remain hard constraints. They express real synchronization requirements and are owned by the task implementation or resource import. `GpuGraphResourceDesc::directConsumerQueue` keeps an exclusive native resource in the family of a consumer that cannot process graph ownership handoffs.

Compiler `diagnosticQueueOverrides` provide exact physical assignments for diagnostics and synthetic fixtures. They are validated against the same command, resource, and external timeline constraints. Normal task construction does not use them. Optional same-class and timing-history routing continue to require their existing explicit scheduling opt-ins.

# Buffer byte ranges

GPU tasks synchronize the byte intervals declared in `GpuTaskResourceUse::range.bufferRange`:

```cpp
const GpuTaskResourceUse use{
    .resource = bufferResource,
    .range = GpuTaskResourceRange{ .bufferRange = BufferRange(256u, 128u) },
    .requiredState = ResourceStates::UnorderedAccess,
    .access = GpuTaskResourceAccess::Write,
};
```

This declaration covers bytes `[256, 384)`. Disjoint intervals do not create resource hazards; overlapping reads/writes retain the required ordering. State transitions, packet state seeds, terminal exports, and queue-family ownership transfers preserve the affected intervals, including when a consumer spans several producers.

Omitting the range keeps the whole-buffer default. `BufferRange(offset, BufferRange::AllBytes)` covers the remaining bytes from that offset. Empty, overflowing, and out-of-bounds task ranges are rejected. Acceleration structures retain whole-allocation synchronization.

The declaration must cover every byte touched by the task's commands and internal state transitions. For explicit native transitions, pass the same range as the last argument to `CommandList::setBufferState(buffer, state, forceMemoryDependency, range)`. Whole-buffer operations, including implicit vertex/index and graphics/mesh indirect binding state transitions, still require whole-buffer declarations. Compute indirect dispatch transitions only its argument structure at the dispatched offset. Built-in uploads and copies declare and transition their exact transfer intervals; buffer clears cover the whole buffer.

A pending queue-family release must be acquired with its original byte interval. Handoff subset selection rejects clipping a pending release. Release separate intervals on the producer when consumers need independent ownership ranges.

# CPU scaling benchmarks

The `nwb_gpu_task_tests` target includes disabled benchmarks for queue placement, timing-history routing, resource-version analysis and binding validation, inferred dependency analysis, resource-state fragments and epilogue grouping, terminal dependencies, ownership statistics, initial-state handoff validation, replay preflight, submission bindings and validation, resource/pipeline/completion imports, telemetry export, and compiled packet queries. Run them with an optimized build:

```text
gpu_task_tests --gtest_also_run_disabled_tests --gtest_filter="*Benchmark.*:*.DISABLED_*Benchmark*" --gtest_output=xml:gpu_benchmarks.xml
```

The XML properties record elapsed CPU nanoseconds and, where applicable, scratch memory and allocation counts. Run several samples and compare matching graph shapes and build configurations. Some analysis fixtures report the total for multiple repetitions; use their `repetitions` or `call_count` property when comparing per-operation cost. Benchmarks have correctness assertions and no wall-clock thresholds. They measure the CPU pipeline; native GPU frame performance requires a representative rendering workload.

The partial-DAG benchmarks in `task_graph_queue_partial_dag_tests.cpp` cover a single dependency, disjoint and reversed pairs, contiguous and interleaved 32-task chains, connected branches, dense layers, two dense components, fan-in/out stars, and merged pairs, with controls through 16,384 tasks. These complement the independent-task and serial-chain placement controls.

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

Queue assignment also checks the union of physical queues capable of executing any placement group's command requirements before constructing dependency reachability. When at most one queue is capable, every legal static, same-class, or timing route uses that queue and every overlap diagnostic is zero; building the quadratic matrix cannot affect the result. The capability scan deliberately overestimates legal routes and stops after finding two queues. Edge-free graphs retain their existing path without this scan.

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

Separate ancestor and descendant extrema determine exact symmetric word spans before allocation. Closure OR loops snapshot row starts and lengths so writes do not inhibit vectorization. Compact symmetry visits only potentially occupied tiles. When a dense row has at least 64 words, the compiler also retains the computed bounds through closure and skips the redundant full-matrix bounds scan. That temporary storage costs 16 bytes/task on x64, at most 1/32 of the raw dense matrix: total assignment peak grows 2.7% at 4,096 tasks and 0.75% at 16,384 tasks in the dense controls above. Smaller dense rows release these bounds before allocating the matrix. This avoids the small near-threshold peak-memory regression of unconditional bounds reuse.

Warm repeated controls retained the small-row and shortcut scratch footprints. At 1,024 tasks, dense controls were about 3–4% slower (roughly 30–35 microseconds); startup and code-layout variation makes tiny timing changes unsuitable as performance claims. Highly interleaved components can still require the dense representation. These results measure compiler CPU time and scratch, not native GPU execution or application frame rate. Scalar-reference tests exercise compact mixed-cost masks across queue moves and topology reorderings; allocation, generation, failure-reset, and compact/dense/ordered reuse cases cover representation boundaries. Raw XML, comparison summaries, source/object/header hashes, and exact link commands are under ignored `__cmake/scheduler_compact_reachability/2026-10-01/`, especially `final_comparison/` and `final_small_controls/`.

The native continuation used real 16 KiB buffers with one or 32 declared regions, shared-source, copy-chain, eight-way tree, and distinct-upload graphs. Every output was copied to host readback, waited on its accepted completion token, and checked word-for-word against varying patterns. The AMD BC-250 exposes one physical queue; multiple logical recording slots exercise CPU recording concurrency, not multi-queue GPU overlap. Four counterbalanced processes checked 4,096 graphs across 128 configurations. Plan, barrier, packet, seed, and frontier counts matched the reference.

At 256 tasks and 32 regions with four CPU workers, batching consecutive initial-state subsets from the same immutable packet source reduced shared-source compile-order execution from 93.829 to 45.996 ms (recording: 88.648 to 40.605 ms). Shared-source parallel recording improved from 93.565 to 46.314 ms, copy-chain compile order from 90.808 to 42.585 ms, tree parallel recording from 80.586 to 33.746 ms, and distinct-upload parallel recording from 81.100 to 34.524 ms. These are host execution/recording times for the buffer workload, not GPU kernel or rendering frame-rate gains. One-region and merged-packet controls remained essentially flat (aggregate median ratios 1.002 and 1.007).

Each batch contains at most eight consecutive subsets, preserves source order, and validates every original range independently. Pending buffer releases on either the current base or source retain the sequential fold: merging valid halves together must not hide an earlier prefix failure that would split a pending ownership-release interval. Regression tests cover this failure, missing-range recovery, the eight-subset boundary, and interleaved source order. Temporary snapshots are local to the operation and use caller scratch for their container; the handoff API owns copied snapshot contents in the graphics arena. The obsolete transaction-gate test-access declarations and redundant empty-subset branch were removed.

The chosen copy-back variant keeps compile-order shared-source peak graph-owned host arena usage at 10,770,580 bytes. Shared-source parallel recording grows from 10,812,052 to 10,956,308 bytes (144,256 bytes, about 1.3%); retained storage grows by 143,312 bytes. These counters cover the graph-owned host arena, not VRAM or every backend/child allocation. A persistent snapshot-cache candidate and a storage-exchange candidate were discarded because they retained more memory without a useful performance advantage. Frontier-depth and unused serial-frontier setup experiments also failed to show a reliable gain and were not promoted.

The selected native candidate additionally passed 128 readback graphs with the Vulkan validation layer enabled and zero logged errors, fatal messages, or assertions. Raw paired samples and memory counters are in ignored `__cmake/scheduler_continuation/2026-10-01/native_seed_merge/copy_comparison/`; validation-layer output is in `batch_local_copy/validation.*`. Source/build manifests preserve the exact measured variants. The intentionally guard-disabled regression binary fails the ownership-prefix case, confirming that the regression exercises the required guard.

Final combined Debug and Optimize builds each passed 358 GPU, 81 CPU, 136 global, and 72 graphics-resource tests, with one hardware-dependent CPU skip. All 135 GPU and 47 CPU opt-in benchmark workloads passed. The final recompiled differential matched all 44,928 preserved assignment/score records; compact-path scalar and representation-boundary regressions passed in both builds. The final native binaries checked another 1,024 buffer graphs and matched reference plan/barrier/packet/seed/frontier/readback fingerprints, plus 128 graphs with the Vulkan validation layer enabled and no logged errors. Final logs, XML, source/binary/library hashes, and build commands are under ignored `__cmake/scheduler_continuation/2026-10-01/final_validation/`.
