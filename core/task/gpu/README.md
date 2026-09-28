# Automatic queue placement

Task declarations describe work, resources, dependencies, and scheduling cost. The compiler chooses a legal physical Graphics, Compute, or Transfer queue before command recording, using command capabilities, resource sharing and ownership, dependency crossings, and opportunities for overlap. Tiny tasks favor locality, and compatible tasks that explicitly merge with their predecessor are placed together.

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
