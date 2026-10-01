# Automatic queue placement

Task declarations describe work, resources, dependencies, and scheduling cost. The compiler chooses a legal physical Graphics, Compute, or Transfer queue before command recording, using command capabilities, resource sharing and ownership, dependency crossings, and opportunities for overlap. Tiny tasks favor locality, and compatible tasks that explicitly merge with their predecessor are placed together.

Overlap scoring uses task-relation bitsets and per-queue cost groups instead of scanning every assignment for every candidate. Merged groups exclude the union of their members' ancestors, descendants, and members, so only work independent of the entire packet contributes overlap. Moving a task between queues updates the cost index before later scoring. Independent graphs and total-order chains retain their reachability shortcuts; graphs without relation rows allocate no cost-mask metadata.

Partially connected graphs still require quadratic reachability storage in the general case. The symmetric relation index is built in place using bounded 64-by-64 tile storage; topological ranks recover strict reachability direction. Benchmarks report peak scratch usage as well as CPU time. Placement policy and declaration opt-ins for parallel command recording are preserved.

Dependency reduction stops traversing after the last direct consumer's topological rank. Stable topological sorting normally scans declaration IDs; excessive rescanning activates a hierarchical ready bitmap that preserves the exact lowest-ready-ID order. Ordinary producer-first graphs retain the scan path without bitmap allocation.

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

The partial-DAG benchmarks in `task_graph_queue_partial_dag_tests.cpp` cover a single dependency, disjoint pairs, connected branches, dense layers, and merged pairs at 1,024 and 4,096 tasks. These complement the independent-task and serial-chain placement controls.

On 2026-10-01, final Linux x64 Optimize comparisons against the preserved original scheduler used three counterbalanced epochs and 66 raw samples per design/shape on AMD BC-250, with no concurrent builds or heavy tests. Graph declaration and dependency analysis were outside the timed assignment call:

| Queue-assignment workload | Original median | Final median |
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

Debug and Optimize each passed all 347 GPU tests, plus the CPU and global suites. A separate comparison matched 44,928 original/final assignment-and-score records across 192 graphs, including mixed costs, merges, queue moves, sparse queue IDs, topology permutations, and external queue pressure. Scalar-reference tests cover partial tiles, reversed declaration order, graph cycles after indexed traversal, callback duplicate/range/generation failures, and mask-free queue-load updates.

Raw comparisons, preserved binaries, and source hashes are under ignored `__cmake/scheduler_optimization/2026-10-01/`; final original comparisons are in `final_original_comparison/`, and per-pass controls are in `continued/`. Final review found no further demonstrated low-risk changes in the measured compiler and callback paths. Broader native recording/submission concurrency needs representative device workloads and a separate review of recording-state ownership.
