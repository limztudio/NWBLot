# Shared CPU task scheduling

NWB's runtime owns one `Core::Alloc::CpuTaskScheduler`, constructed before Graphics and project initialization. Systems own task scopes and data; they borrow the execution service. Asset-builder and FBX-converter processes each construct their own scheduler once at their entry point.

## Initialization and heterogeneous CPUs

`CpuTaskSchedulerConfig` selects a worker count, a default main-thread reservation, and heterogeneous scheduling. The automatic count is the number of usable logical processors minus `reservedThreadCount` (one by default). An explicit count is honored, including zero for caller-driven execution. `Frame` accepts this typed configuration in its constructor. Configure the service before submitting initialization work; worker threads persist until engine shutdown.

Topology discovery respects Windows process groups, hard affinity and default CPU sets, and Linux inherited CPU affinity. Processor identities include their group or full Linux index, so indices beyond 63 are not truncated. Windows efficiency classes and available Linux CPU-capacity data preserve all reported capacity tiers. The fastest tier is classified Performance and lower tiers Efficiency; homogeneous or unknown capacity is Any. Workers are pinned to discovered allowed processors. Failed pinning is counted and the worker falls back to unclassified OS scheduling.

Task cost and urgency are independent:

| Option | Meaning |
| --- | --- |
| `CpuTaskCost::Heavy` | Prefer performance workers for substantial CPU work. |
| `CpuTaskCost::Light` | Prefer efficiency workers for smaller work. |
| `CpuTaskCost::Any` | Allow either class without a capacity preference. |
| `CpuTaskPriority::Critical` | Prefer work needed promptly by the frame. |
| `CpuTaskPriority::Normal` | Ordinary ready work. |
| `CpuTaskPriority::Background` | Lower urgency, with periodic admission to prevent permanent starvation. |
| `CpuTaskTarget::MainThread` | Execute only on the thread that created the scheduler. |

Matching-class workers get first opportunity. When all matching-class workers are busy, other workers may take the work, preserving the shared CPU budget under uniform workloads. Cooperative joins can also cross cost preferences to guarantee progress. Cost is therefore a placement hint; main-thread execution is a hard constraint. Priority cannot preempt a running callback: choose bounded batches for expensive work. Small setup uploads use Light and larger uploads use Heavy; both keep Normal urgency. ECS systems can override `taskOptions()`, and query batches accept options explicitly.

## Dependencies and structured completion

`CpuTaskScope::submit()` returns a generation-tagged handle in the scheduler's single identity domain. Dependencies may cross system scopes. Foreign scheduler handles are rejected. Completed handles remain valid prerequisites after node reuse.

Tasks submitted from a running callback automatically become children of that callback's task. Completion means the callback, all descendants, and the task's captured objects have finished. A dependent system cannot observe partial child work simply because its producer's callback returned. A child's explicit dependency must not introduce a cycle through its parent, an ancestor, or their dependent tasks; such submission is rejected.

Callbacks must keep their referenced data alive. Captured owning objects stay alive through descendant completion; references to automatic variables inside a callback do not acquire an extended C++ lifetime. Join children before those local variables leave scope. Capture destructors must not rely on implicitly attaching new asynchronous work to the task being retired; submit such work explicitly in a separately owned scope.

Ordinary task handles and task scopes are the completion boundaries. A scope includes its admitted tasks, their descendant lifetimes, and capture retirement. Its owner must stop external producers before destroying the scope or its data. Submitting concurrently with destruction is invalid. A task cannot wait on itself, an ancestor, or a scope containing any ancestor. Expected cancellation is explicit through a scope; unexpected exceptions remain terminal at native worker boundaries or unwind to the application entry boundary when executed on the caller. Exceptions are never stored for deferred delivery.

## Parallel loops and main-thread work

`parallelFor()` partitions a range into ordinary CPU tasks and joins a local scope. It has no shared global dispatch slot and supports nested loops with one worker. Cooperative scope joins select work contributing to the joined scope, including its prerequisites, so unrelated queued work does not become part of a subsystem lifetime barrier. Parallel-loop callers can participate; ordinary main-thread scope waits pump eligible main-thread tasks while worker tasks execute on workers. Stable worker domain/index identities preserve native command-recording storage ownership.

The main thread must pump the scheduler while main-thread tasks are outstanding. Frame pumps at its update boundary; scheduler/task-scope waits on the main thread also make progress. Blocking an OS thread on an external latch does not pump this queue. Avoid blocking I/O, GPU completion waits, and unbounded callbacks in ordinary CPU tasks. An external callback can submit a continuation when its operation actually completes.

## ECS and graphics integration

ECS preparation remains a serial caller phase because existing preparation can create entities and change component storage. Each preparation callback must complete those structural changes before returning. Updates are dependency tasks: every conflicting component access preserves system registration order; read/read accesses can overlap. Structural changes during concurrent updates still require the caller to provide a safe structural boundary. UI updates explicitly target the main thread.

Graphics owns a scope for async resource setup and resource-lifetime joins. The loader owns a project scope and drains it before project callbacks are unloaded. World clearing joins only its world scope. GPU recording uses the shared CPU scheduler while retaining worker-local command storage, serial command-IR capture, and the current skinning preparation/submission ordering.

The GPU graph continues to own resource barriers, physical queues, acceptance, rollback, and device-completion tokens. CPU submission completion does not imply GPU completion. Resource destruction still requires its existing GPU join in addition to CPU scope retirement.

## Verification and delivery steps

1. CPU runtime and topology: dependency/capture/descendant lifetimes, cancellation, nested joins, main-thread targets, cost/priority policy, actual Windows processor restrictions.
2. Runtime migration: ECS, Frame, Graphics/Vulkan recording, loader/project scopes, tools and cook APIs; targeted ECS/graphics tests and native Vulkan smoke.
3. Final integration: supported local build configurations, policy checks, repeated concurrency regressions, and a measured CPU workload comparison. Record actual host coverage and any unavailable platform validation.

Each completed step is committed and pushed to `main` after its checks pass. The legacy allocator pool/job primitives remain isolated for their regression coverage; production consumers use the CPU task service.
