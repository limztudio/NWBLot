# Shared CPU task scheduling

NWB's runtime owns one `NWB::Core::CpuTaskScheduler`, constructed before GraphicsRuntime and project initialization. Systems own task scopes and data; they borrow the execution service. Asset-builder and FBX-converter processes each construct their own scheduler once at their entry point.

The CPU execution service belongs to the `core/task` domain: include `<core/task/cpu/scheduler.h>` and link `nwb_cpu_task` for `Core::CpuTaskScheduler`, `Core::CpuTaskScope`, and the associated task types. Allocator primitives remain in `core/alloc`. Scheduler coverage lives in `tests/unit/task/cpu` under `nwb_cpu_task_tests`; processor topology coverage remains in `tests/unit/global`.

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

## Submission from any thread

`CpuTaskScheduler::submit()` and `CpuTaskScope::submit()` accept concurrent calls from the engine main thread, scheduler callbacks, and external OS threads. Producers do not register with the scheduler or own a worker lane. Multiple producers may share one task scope.

The ready queues are MPMC (multiple producer, multiple consumer), protected by the engine `Futex`. Task reservation, dependency publication, queue insertion, and consumer claims synchronize through the scheduler mutex. A ready task is claimed once; its callback runs after releasing the mutex. User callable construction and capture retirement also stay outside that mutex. The implementation is mutex-protected, not lock-free.

Submission thread and execution target are independent. Worker tasks run on the configured workers or an eligible cooperative caller; `MainThread` tasks submitted anywhere still execute only on the scheduler owner. External waiters do not become extra worker consumers or borrow a GPU recording slot. With zero workers, the owner must pump or wait to execute submitted work.

Publish the initialized scheduler/scope to producers using normal C++ synchronization. Keep the scope, scheduler, and referenced task data alive until producers have stopped and tasks have joined. A scope/scheduler `wait()` observes admitted work; it does not close admission or wait for an external producer that has not submitted yet. Dependency handles shared across producer threads also require normal caller synchronization. Cancellation, terminal shutdown, invalid descriptors, foreign dependencies, exhausted identity capacity, and dependency cycles return an explicit `CpuTaskAdmissionError` through `Expected<CpuTaskHandle, CpuTaskAdmissionError::Enum>`. Admit the result before publishing or using its generation-tagged handle. An admitted task that inherits cancellation still returns a valid handle; joining it retires its captures without executing its callback.

## Dependencies and structured completion

`CpuTaskScope::submit()` returns an expected generation-tagged handle in the scheduler's single identity domain. Dependencies may cross system scopes. Foreign scheduler handles are rejected. Completed handles remain valid prerequisites after node reuse.

Tasks submitted from a running callback automatically become children of that callback's task. Completion means the callback, all descendants, and the task's captured objects have finished. A dependent system cannot observe partial child work simply because its producer's callback returned. A child's explicit dependency must not introduce a cycle through its parent, an ancestor, or their dependent tasks; such submission is rejected.

Callbacks must keep their referenced data alive. Captured owning objects stay alive through descendant completion; references to automatic variables inside a callback do not acquire an extended C++ lifetime. Join children before those local variables leave scope. Capture destructors must not rely on implicitly attaching new asynchronous work to the task being retired; submit such work explicitly in a separately owned scope.

Ordinary task handles and task scopes are the completion boundaries. A scope includes its admitted tasks, their descendant lifetimes, and capture retirement. Its owner must stop external producers before destroying the scope or its data. Submitting concurrently with destruction is invalid. A task cannot wait on itself, an ancestor, dependent work that needs its completion, or a scope containing any ancestor. Expected cancellation is explicit through a scope; unexpected exceptions remain terminal at native worker boundaries or unwind to the application entry boundary when executed on the caller. Exceptions are never stored for deferred delivery.

Normal scope and scheduler destruction joins outstanding work through a throwing wait. `drain()` is reserved for terminal cleanup: it stops admission across the shared scheduler and skips queued callbacks before joining active work and retiring captures. Scope `drain()` also aborts the shared service, because application unwind is terminal under the engine exception policy. Use `cancel()` followed by `wait()` for ordinary scope-local cancellation. During an existing exception, destructors use terminal drain so cleanup cannot invoke another queued throwing callback. Owners that store task scopes behind a non-throwing smart-pointer destructor must explicitly join before resetting the pointer; project world shutdown already does this through `World::clear()`.

Completion accounting and the transition to retirement share one scheduler lock. The thread that performs the transition owns retirement, destroys the callable outside the lock, then publishes completion under the lock. An empty scope can return after an acquire load of its pending count; profiling still records the join. Joining callers register under the scheduler mutex before parking. Progress snapshots that registration under the same mutex. Retirement broadcasts to registered joiners when a dependent becomes ready, a scope completes, the scheduler completes, or a handle join is registered. Submission, cancellation, and drain retain their broader progress notifications; worker notifications retain their existing policy.

Task nodes, dependency storage, and worker metadata belong to the scheduler's tracked `Alloc::GlobalArena`. The task domain owns its arena identity in `core/task/cpu/arena_names.h`: `core/task/cpu/scheduler`. Nested dependency checks mark the ancestor/dependent closure once in scheduler-owned search storage, then test membership while resolving the submitted predecessors. They do not rescan the predecessor list for each reachable node or allocate a temporary arena. Storage grows with the task graph and reuses completed node slots. There is no small fixed arena limit on ordinary task bursts. Search storage grows geometrically and is reserved before publishing nodes, keeping completion cleanup allocation-free.

## Parallel loops and main-thread work

`parallelFor()` returns `Expected<void, CpuTaskAdmissionError::Enum>`, partitions a range into ordinary CPU tasks, and joins a local scope before returning success. Empty ranges and intentionally canceled scopes succeed without executing callbacks. A rejected range publishes no chunks, returns its admission error, and leaves ordinary scope cancellation distinct from terminal scheduler drain. It has no shared global dispatch slot and supports nested loops with one worker. Cooperative scope joins select work contributing to the joined scope, including its prerequisites, so unrelated queued work does not become part of a subsystem lifetime barrier. Parallel-loop callers can participate; ordinary main-thread scope waits pump eligible main-thread tasks while worker tasks execute on workers. Stable worker domain/index identities preserve native command-recording storage ownership.

Each scope wait remembers a generation-tagged cursor for the unrelated prefix of each ready queue. Later claims resume after that cursor while it remains queued; a removed or recycled cursor restarts the search. Successful task or range publication invalidates the cursors because it can introduce new prerequisite paths. Nested and concurrent waits keep separate cursors, and priority/target eligibility still applies before queue selection. This avoids walking the same unrelated prefix for every joined task.

The main thread must pump the scheduler while main-thread tasks are outstanding. Frame pumps at its update boundary; scheduler/task-scope waits on the main thread also make progress. Blocking an OS thread on an external latch does not pump this queue. Avoid blocking I/O, GPU completion waits, and unbounded callbacks in ordinary CPU tasks. An external callback can submit a continuation when its operation actually completes.

## ECS and graphics integration

ECS preparation remains a serial caller phase because existing preparation can create entities and change component storage. Each preparation callback must complete those structural changes before returning. Updates return an expected command result and run as dependency tasks: every conflicting component access preserves system registration order; read/read accesses can overlap. A system failure skips its conflicting dependents and reaches `World::tick()` after admitted work joins. The project owner logs the error and returns its existing failed-update status to `Frame`. Structural changes during concurrent updates still require the caller to provide a safe structural boundary. UI updates explicitly target the main thread.

`GraphicsRuntime` owns a scope for async resource setup and resource-lifetime joins. The loader owns a project scope and joins it before project callbacks are unloaded. Normal world clearing joins only its world scope. GPU recording uses the shared CPU scheduler while retaining worker-local command storage, serial command-IR capture, and the current skinning preparation/submission ordering.

`Core::GpuTaskScheduler`, declared in `core/task/gpu/scheduler.h`, coordinates GPU graph recording, submission, and recovery. It lives in the GPU task domain alongside the graph compiler and recorder. `GraphicsRuntime` owns the separate device, resource, and presentation lifecycle and borrows the frame-owned CPU/GPU schedulers. The GPU graph continues to own resource barriers, physical queues, acceptance, rollback, and device-completion tokens. CPU submission completion does not imply GPU completion. Resource destruction still requires its existing GPU join in addition to CPU scope retirement.

## Parallel-range publication and scope contribution

`parallelFor()` reserves and publishes its chunk nodes under one scheduler lock, with one publication notification. Internal captures contain only the caller-context pointer, an invocation pointer, and range bounds; user callables are neither copied nor invoked under that lock. A single chunk runs directly through the ordinary execution/retirement machinery when the caller is an eligible owner or worker. External producers and off-owner main-thread work remain queued. Descendants, cancellation boundaries, and exception cleanup still use task nodes and scopes.

Each nonempty scope lazily receives one search identity under the scheduler mutex. Concurrent and repeated joins of that scope reuse contribution proofs; each waiter retains separate ready-queue cursors. Caller-dependent cycle validation runs before assistance and in every scope wait predicate. Publication invalidates all contribution stamps, and scope identities terminate on wrap instead of being reused. One `u64` per scope holds the identity; contribution outcomes share the existing per-node stamp vector.

Even contribution generations encode exhausted negative searches; the following odd value encodes a proven path to the joined scope. A successful search propagates positive results backward through only its processed breadth-first prefix, following generation-valid parent/dependent edges already proved positive. Visited siblings without such a path remain unmarked, and unprocessed sibling frontiers are not scanned. A live prerequisite's proven downstream path cannot disappear through retirement: every dependent or structured parent on that path still awaits the prerequisite's completion. The cache retains one scope's results at a time, so joins of different scopes can invalidate each other's proofs.

A cold scope query records an immediate negative result when the ready task is outside the requested scope, has no dependents, and has no live structured parent. Those are all possible outgoing contribution paths. This avoids preparing a graph search while preserving scope membership, invalidation, and queue order.

Each task slot retains its newest canceled generation inline and a sorted vector of older canceled generations. Late dependencies select the slot directly, then check its latest generation or binary-search its older history. Histories survive node reuse. Capacity for a possible older entry is reserved before ordinary or batched publication, so canceled retirement remains allocation-free. The first cancellation of a slot needs no history allocation. Old handles retain their lifetime semantics; cancellation history is not evicted.

Worker wakeups are separate from task joins. Queue publication and task execution compute which capacity classes have both eligible queued work and parked workers, then notify one worker in each selected class after releasing the queue mutex. Starting a task continues the worker wake chain before entering its callback, allowing dependency fan-out and blocking test callbacks to progress without waking every idle worker for each submission.

## Optional profiling

Profiling is disabled by default. Steady-state disabled scheduling reads no profiling clocks and allocates no event or ready-timer buffers. `CpuTaskOptions::profileLabel` and scope labels are compact eight-byte handles; register their owned names once through `registerProfileLabel()` during subsystem setup. Names are copied into events only during capture. Task labels override scope/ancestor labels. A foreign scheduler label resolves to an unnamed event rather than aliasing a local name.

`setProfiling(enabled, frameIndex)` enables or disables collection and updates frame attribution. `CpuTaskSchedulerConfig::profileEventCapacity` bounds storage (default 4096 events; zero disables capture). Events are read through `readProfileEvents()`; statistics expose pending, recorded, and dropped event counts. A full buffer retains existing events and counts newly dropped samples. Changing capture state discards buffered samples and advances an epoch, preventing old ready, execution, join, or idle timers from crossing a disable/re-enable boundary. Updating only the frame index preserves the capture epoch.

The event kinds are:

- Queue delay: time from becoming ready to callback start, excluding predecessor dependency waits.
- Execution: callback wall time, including nested cooperative work, with completion-mutex acquisition excluded.
- Handle, scope, and whole-scheduler joins: caller wait intervals, including cooperative work.
- Worker idle: completed condition-variable waiting intervals that began during capture. Enabling capture while a worker is already parked omits that initial interval.

Events retain task identity, worker lane/affinity, captured frame and epoch, and an owned label. Canceled callbacks do not produce execution samples. Events are emitted when their measured interval completes, so delayed samples retain their original frame attribution. Execution and join intervals overlap with nested/cooperative work; totals across workers and timing kinds are not frame wall time.

Frame CPU perf capture enables scheduler profiling automatically. `Perf::CollectCpuTaskProfile()` drains only the initial pending-count snapshot on the timing-sink owner thread, before perf publication. Worker threads never call the non-thread-safe timing recorder. Named execution scopes cover graphics setup/frame work, ECS worlds, and project work; other events use `cpu.task.queue_delay`, `cpu.task.execution`, `cpu.task.handle_join`, `cpu.task.scope_join`, `cpu.task.scheduler_join`, and `cpu.worker.idle`. Existing perf telemetry transports these timing scopes without a new wire schema. Failed or quit frames discard unpublished profiling data.

## Verification procedure

Use the repository launcher and test configuration documented in [the test guide](../tests/README.md). Scheduler regressions live in `tests/unit/task/cpu`; processor-topology tests live in `tests/unit/global`. Validate the configurations and actual platforms affected by a change, and record disabled tests and unavailable hardware separately from executed passes.

Cover dependency/capture/descendant lifetimes, late prerequisites, node reuse, cancellation history, nested handle/scope joins, cycle rejection, main-thread targets, cost/priority placement, and concurrent producers. Scope-search coverage includes unrelated ready prefixes, shared continuation chains, sibling frontiers, publication invalidation, repeated/concurrent same-scope waiters, and different-scope controls. Completion coverage includes capture-destructor reentry and notification races at handle, scope, and scheduler joins. Topology coverage uses actual allowed processor identities and restrictions.

Integration checks exercise ECS and Frame ownership, selected-backend recording, worker-storage leases, loader/project shutdown, tools and cook APIs, and native graphics smoke through public contracts. Preserve the distinction between CPU retirement and GPU completion. Exercise zero-worker owner-driven execution and native workers independently; physical heterogeneous-class placement requires suitable hardware.

Profiling checks cover disabled collection, label/frame attribution, bounded buffer overflow, stale epochs, native idle lanes, foreign labels, zero capacity, concurrent capture changes, owner-thread delivery, and bounded adapter draining. Native graphics tests must state which queue/extension capabilities actually executed.

## Measurement procedure and tradeoffs

Use bounded batches or `parallelFor()` for fine-grained collections. The shared scheduler accounts for dependencies, structured lifetimes, cancellation, and placement; its overhead depends on graph shape and submission granularity. The contribution cache stores one scope's proofs at a time. Worker-based tiny workloads are sensitive to wakeups, contention, and code placement. Measure representative work before changing chunk policy, notification routing, or cache capacity.

Run opt-in `CpuTaskProfile` and `CpuTaskWaitProfile` sampling cases through the `core/perf` opt-in API. Keep timing experiments separate from ordinary correctness runs; these sampling cases have no timing pass threshold. Stop builds and heavy tests during measurements, warm the workload, alternate or counterbalance candidate/reference order, and record compiler, configuration, host topology, affinity, worker count, sample count, source/binary identity, and validation checksums.

Time submission, execution, and completion joins together when comparing scheduling overhead. Exclude initialization, serial reference calculation, output clearing, validation, and logging from that timed interval. Validate every output cell or checksum against a deterministic reference after every run. Report medians and a stated percentile method, preserve raw samples, and separate physical placement checks from timed work.

Use coarse ranges, simultaneous independent ranges, nested systems, individual tiny tasks, and batched equivalents. Scope-join experiments vary unrelated ready-prefix length, dependent fan-in, shared-chain length, worker count, and external waiter count. Include whole-scheduler joins over the same graph, no-prefix work, empty/completed joins, and coarse ranges as controls. Compare ordinary cancellation lookups with retained canceled generations after repeated node reuse.

These CPU microbenchmarks describe the named host operation. They do not establish application frame rate or GPU overlap, and small noisy differences need independent confirmation. Application claims require representative application measurements. Preserve resource limits and output/lifetime validation while measuring.
