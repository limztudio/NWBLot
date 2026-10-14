# Automatic queue placement

Task declarations describe work, resources, dependencies, and scheduling cost. The compiler chooses a legal physical Graphics, Compute, or Transfer queue before command recording, using command capabilities, resource sharing and ownership, dependency crossings, and opportunities for overlap. Tiny tasks favor locality, and compatible tasks that explicitly merge with their predecessor are placed together.

Overlap scoring uses task-relation bitsets and per-queue cost groups instead of scanning every assignment for every candidate. Merged groups exclude the union of their members' ancestors, descendants, and members, so only work independent of the entire packet contributes overlap. Moving a task between queues updates the cost index before later scoring. Conditional recovery tasks contribute zero estimated normal overlap and queue load because they execute only after normal work fails; their queue requirements and accepted-frontier joins remain intact. Independent graphs and total-order chains retain their reachability shortcuts; graphs without relation rows allocate no cost-mask metadata.

Partial graphs use compact per-row word spans when their storage plus temporary bounds and offsets saves roughly 25% of a full matrix. Small rows and broad spans retain dense storage; worst-case storage remains quadratic. Global task-bit positions are preserved. Both forms build the symmetric relation index in place using bounded 64-by-64 tiles, and topological ranks recover strict reachability direction. Separate ancestor and descendant extrema determine exact symmetric word spans before allocation. Closure OR loops snapshot row starts and lengths so writes do not inhibit vectorization, and compact symmetry visits only potentially occupied tiles. Dense rows of at least 64 words retain computed bounds through closure to avoid a second full-matrix bounds scan; smaller dense rows release that storage before matrix allocation. Placement policy and declaration opt-ins for parallel command recording remain explicit.

Dependency reduction stops traversing after the last direct consumer's topological rank or once every direct consumer is reached. Analysis and telemetry share a byte-mixed dependency-pair hash so dense sequential IDs do not trigger pathological hash-table growth. Stable topological sorting normally scans declaration IDs; excessive rescanning activates a hierarchical ready bitmap that preserves the exact lowest-ready-ID order. Ordinary producer-first graphs retain the scan path without bitmap allocation.

Queue assignment checks the union of physical queues capable of executing any placement group's command requirements before constructing dependency reachability. When at most one queue is capable, every legal static, same-class, or timing route uses that queue and every overlap diagnostic is zero; building the quadratic matrix cannot affect the result. The capability scan deliberately overestimates legal routes and stops after finding two queues. Edge-free graphs retain their existing path without this scan.

Recorded and accepted callback bindings are validated and indexed before execution. Arrays of at most eight callbacks use an allocation-free linear lookup; larger arrays use a scratch-backed index. Bindings remain immutable through the operation, while callback-owned context may change. Invocation stays in compiled task order, and lookup allocates nothing after native acceptance.

Built-in copies, uploads, clears, and resolves derive their command requirements internally. A custom recorded task declares its implementation's requirements once:

```cpp
struct ExampleComputeTask{
    static constexpr GpuTaskCommandRequirements s_CommandRequirements{ GpuQueueCapability::Compute };

    struct Payload{
        // Task-specific data.
    };

    static bool Record(const Payload& payload, CommandList& commandList, const GpuTaskRecordContext& context);
};

const GpuTaskId task = graph.addTask<ExampleComputeTask>(taskDesc, Move(payload));
```

When a task implementation records different command kinds depending on its payload, provide `static GpuTaskCommandRequirements CommandRequirements(const Payload&)` instead. Every required capability must be supported; each nonempty alternative mask additionally requires at least one alternative. Built-ins preserve native and hook alternatives independently. A task that records native commands must declare one of these contracts. The metadata-only `addTask` overload accepts command requirements for analysis and tooling.

Explicit packet merging requires the same physical queue and mergeable tasks. Crossing an existing consumer frontier additionally requires the declared immediate-successor opt-in; consumers then wait for the complete packet. Per-task resource transitions and timing scopes remain inside the packet.

Framework contracts for presentation, existing external submission timelines, and imported ownership remain hard constraints. They express real synchronization requirements and are owned by the task implementation or resource import. `GpuGraphResourceDesc::directConsumerQueue` keeps an exclusive native resource in the family of a consumer that cannot process graph ownership handoffs.

Submission retains the complete dependency token array for validation and lifecycle ordering. A prior accepted native wait whose scope covers all commands on the exact consuming physical queue may cover a later wait on the same current-device producer timeline at no greater value. Rejected submissions publish no inherited coverage; binary and external synchronization remains explicit. Submission statistics keep emitted native `timelineWaitCount`, same-queue elisions, duplicate merges, and `inheritedTimelineWaitElisionCount` separate, with exact planned-token accounting.

Compiler `diagnosticQueueOverrides` provide exact physical assignments for diagnostics and synthetic fixtures. They are validated against the same command, resource, and external timeline constraints. Normal task construction does not use them. Optional same-class and timing-history routing continue to require their existing explicit scheduling opt-ins.

# Shared read state sources

Concurrently shared resources may fan out compatible reads from one immutable packet-final state snapshot. The source must cover the complete consumer range, admit every participating queue, and preserve its full anchor range through the rest of the source packet. Overlapping writes, a different source state, or an outgoing ownership barrier prevent reuse. Each consumer retains its exact declared state and the execution dependency on that source packet. A consumer that later writes or changes to an incompatible read state on an overlapping range inside the same task joins outstanding readers at task entry instead of borrowing an earlier shared-read anchor.

Textures require native close to preserve the declared state; compatible shader/depth reads use the selected provider's common read-only layout. A buffer or acceleration-structure backing buffer may restore a known creation state on close because it has no image layout; an imported read seed then receives an explicit local transition from that actual snapshot state. A later incompatible state or write still joins overlapping readers, and terminal exports retain the latest tracked state.

Reader joins may stop at a covering writer that already ordered the older readers, or at a fully covering incompatible read epoch established by another task. Partial boundaries and task-local state changes prevent that read-epoch cutoff.

Native barrier lowering normalizes queue-supported stage/access scopes in private caller-owned arrays consumed by the recording call. It does not copy those arrays into temporary containers or change layouts, ranges, ownership, or rendering-scope handling.

# Initial-state handoffs

Initial-state handoffs batch at most eight consecutive subsets from one immutable packet source. Every original range is validated independently and source order is preserved between batches. Pending buffer releases on either the current base or source use a sequential fold: merging valid halves together must not hide an earlier prefix failure that would split a pending ownership-release interval. Temporary snapshot containers use caller scratch, while the handoff API owns copied snapshot contents in the graphics arena.

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

Omitting the range keeps the whole-buffer default. `BufferRange(offset, BufferRange::s_AllBytes)` covers the remaining bytes from that offset. Empty, overflowing, and out-of-bounds task ranges are rejected. Acceleration structures retain whole-allocation synchronization.

The declaration must cover every byte touched by the task's commands and internal state transitions. For explicit native transitions, pass the same range as the last argument to `CommandList::setBufferState(buffer, state, forceMemoryDependency, range)`. Whole-buffer operations, including implicit vertex/index and graphics/mesh indirect binding state transitions, still require whole-buffer declarations. Compute indirect dispatch transitions only its argument structure at the dispatched offset. Built-in uploads and copies declare and transition their exact transfer intervals; buffer clears cover the whole buffer.

A pending queue-family release must be acquired with its original byte interval. Handoff subset selection rejects clipping a pending release. Release separate intervals on the producer when consumers need independent ownership ranges.

# CPU scaling coverage

CPU scaling behavior is covered by ordinary enabled unit tests in `tests/unit/task/gpu/` (262 `TEST` cases, no `DISABLED_*` cases): queue placement and scoring, timing-history routing, resource-version analysis, dependency inference, resource-state fragments and epilogue grouping, terminal dependencies, ownership statistics, initial-state handoff validation, replay preflight, submission bindings and validation, resource/pipeline/completion imports, telemetry export, and compiled packet queries. Those tests carry correctness assertions and no wall-clock thresholds; they exercise the CPU pipeline, while native GPU frame performance requires a representative rendering workload. Run them with an optimized build:

```sh
python -m launcher run nwb_gpu_task_tests --config opt --working-directory . -- --gtest_output=xml:gpu_task_tests.xml
```

The launcher builds and runs the target; the test executable writes the XML report in the repository root.

The partial-DAG placement cases in `tests/unit/task/gpu/task_graph_queue_partial_dag_tests.cpp` cover multiword score preservation across queue moves and rebuilds, sparse-row scratch bounds, relation-state reset on rebuild, assignment exclusion without local assignment, and mask-allocation avoidance for ordered/unused reachability. These complement the independent-task and serial-chain placement controls.

Historical measurement evidence is preserved in the [GPU task performance report](../../../docs/gpu_task_performance_history.md) and [renderer M4 qualification report](../../../tests/ab/async_shadow_m4/qualification_history.md).
