# Shared CPU task scheduling

NWB's runtime owns one `Core::CpuTaskScheduler`, constructed before GraphicsRuntime and project initialization. Systems own task scopes and data; they borrow the execution service. Asset-builder and FBX-converter processes each construct their own scheduler once at their entry point.

The CPU execution service belongs to the `core/task` domain: include `<core/task/cpu/scheduler.h>` and link `nwb_cpu_task` for `Core::CpuTaskScheduler`, `Core::CpuTaskScope`, and the associated task types. Allocator primitives remain in `core/alloc`. Scheduler coverage lives in `tests/unit/task` under `nwb_cpu_task_tests`; processor topology coverage remains in `tests/unit/global`.

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

The ready queues are MPMC (multiple producer, multiple consumer), protected by the engine `Futex`. Task reservation, dependency publication, queue insertion, and consumer claims synchronize through the scheduler mutex. A ready task is claimed once; its callback runs after releasing the mutex. Callable construction and capture retirement also stay outside that mutex. The implementation is mutex-protected, not lock-free.

Submission thread and execution target are independent. Worker tasks run on the configured workers or an eligible cooperative caller; `MainThread` tasks submitted anywhere still execute only on the scheduler owner. External waiters do not become extra worker consumers or borrow a GPU recording slot. With zero workers, the owner must pump or wait to execute submitted work.

Publish the initialized scheduler/scope to producers using normal C++ synchronization. Keep the scope, scheduler, and referenced task data alive until producers have stopped and tasks have joined. A scope/scheduler `wait()` observes admitted work; it does not close admission or wait for an external producer that has not submitted yet. Dependency handles shared across producer threads also require normal caller synchronization. Expected cancellation or terminal shutdown can reject submissions with an invalid handle.

## Dependencies and structured completion

`CpuTaskScope::submit()` returns a generation-tagged handle in the scheduler's single identity domain. Dependencies may cross system scopes. Foreign scheduler handles are rejected. Completed handles remain valid prerequisites after node reuse.

Tasks submitted from a running callback automatically become children of that callback's task. Completion means the callback, all descendants, and the task's captured objects have finished. A dependent system cannot observe partial child work simply because its producer's callback returned. A child's explicit dependency must not introduce a cycle through its parent, an ancestor, or their dependent tasks; such submission is rejected.

Callbacks must keep their referenced data alive. Captured owning objects stay alive through descendant completion; references to automatic variables inside a callback do not acquire an extended C++ lifetime. Join children before those local variables leave scope. Capture destructors must not rely on implicitly attaching new asynchronous work to the task being retired; submit such work explicitly in a separately owned scope.

Ordinary task handles and task scopes are the completion boundaries. A scope includes its admitted tasks, their descendant lifetimes, and capture retirement. Its owner must stop external producers before destroying the scope or its data. Submitting concurrently with destruction is invalid. A task cannot wait on itself, an ancestor, dependent work that needs its completion, or a scope containing any ancestor. Expected cancellation is explicit through a scope; unexpected exceptions remain terminal at native worker boundaries or unwind to the application entry boundary when executed on the caller. Exceptions are never stored for deferred delivery.

Normal scope and scheduler destruction joins outstanding work through a throwing wait. `drain()` is reserved for terminal cleanup: it stops admission across the shared scheduler and skips queued callbacks before joining active work and retiring captures. Scope `drain()` also aborts the shared service, because application unwind is terminal under the engine exception policy. Use `cancel()` followed by `wait()` for ordinary scope-local cancellation. During an existing exception, destructors use terminal drain so cleanup cannot invoke another queued throwing callback. Owners that store task scopes behind a non-throwing smart-pointer destructor must explicitly join before resetting the pointer; project world shutdown already does this through `World::clear()`.

Task nodes, dependency storage, and worker metadata belong to the scheduler's tracked `Alloc::GlobalArena`. The task domain owns its arena identities in `core/task/cpu/arena_names.h`: `core/task/cpu/scheduler` and `core/task/cpu/dependencies`. Storage grows with the task graph and reuses completed node slots. There is no small fixed arena limit on ordinary task bursts. Search storage grows geometrically and is reserved before publishing nodes, keeping completion cleanup allocation-free.

## Parallel loops and main-thread work

`parallelFor()` partitions a range into ordinary CPU tasks and joins a local scope. It has no shared global dispatch slot and supports nested loops with one worker. Cooperative scope joins select work contributing to the joined scope, including its prerequisites, so unrelated queued work does not become part of a subsystem lifetime barrier. Parallel-loop callers can participate; ordinary main-thread scope waits pump eligible main-thread tasks while worker tasks execute on workers. Stable worker domain/index identities preserve native command-recording storage ownership.

The main thread must pump the scheduler while main-thread tasks are outstanding. Frame pumps at its update boundary; scheduler/task-scope waits on the main thread also make progress. Blocking an OS thread on an external latch does not pump this queue. Avoid blocking I/O, GPU completion waits, and unbounded callbacks in ordinary CPU tasks. An external callback can submit a continuation when its operation actually completes.

## ECS and graphics integration

ECS preparation remains a serial caller phase because existing preparation can create entities and change component storage. Each preparation callback must complete those structural changes before returning. Updates are dependency tasks: every conflicting component access preserves system registration order; read/read accesses can overlap. Structural changes during concurrent updates still require the caller to provide a safe structural boundary. UI updates explicitly target the main thread.

Graphics owns a scope for async resource setup and resource-lifetime joins. The loader owns a project scope and joins it before project callbacks are unloaded. Normal world clearing joins only its world scope. GPU recording uses the shared CPU scheduler while retaining worker-local command storage, serial command-IR capture, and the current skinning preparation/submission ordering.

`Core::GpuTaskScheduler`, declared in `core/task/gpu/scheduler.h`, coordinates GPU graph recording, submission, and recovery. It remains in the graphics domain alongside the graph compiler and recorder. `GraphicsRuntime` owns the separate device, resource, and presentation lifecycle and borrows the frame-owned CPU/GPU schedulers. The GPU graph continues to own resource barriers, physical queues, acceptance, rollback, and device-completion tokens. CPU submission completion does not imply GPU completion. Resource destruction still requires its existing GPU join in addition to CPU scope retirement.

## Verification and delivery steps

1. CPU runtime and topology: dependency/capture/descendant lifetimes, cancellation, nested joins, main-thread targets, cost/priority policy, actual Windows processor restrictions.
2. Runtime migration: ECS, Frame, Graphics/Vulkan recording, loader/project scopes, tools and cook APIs; targeted ECS/graphics tests and native Vulkan smoke.
3. Final integration: supported local build configurations, policy checks, repeated concurrency regressions, and a measured CPU workload comparison. Record actual host coverage and any unavailable platform validation.

Each completed step is committed and pushed to `main` after its checks pass. The legacy allocator pool/job implementations, scheduler facades, arena names, and 64-bit affinity-mask APIs have been removed. Large distinct and repeated dependency fan-in regressions now exercise the shared CPU task service. Topology tests cover actual allowed processor identities and restrictions.

## Local integration verification

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

## Measurement method

The local comparison uses Windows ARM64 on a Snapdragon X2 Elite Extreme X2E94100, with 18 usable logical processors (12 performance-class and 6 efficiency-class). Both designs use 17 native workers. The baseline mirrors the previous Frame configuration: 11 unpinned graphics workers and 6 unpinned project workers. The shared scheduler uses 11 pinned performance workers and 6 pinned efficiency workers.

Four counterbalanced epochs alternate the execution order of the designs. Each case has three warmups and eight measured samples per epoch, for 32 measured samples per design. The timed interval includes submission, execution, and completion joins. Initialization, serial reference calculation, output clearing, validation, and logging are excluded. Every output cell is compared with a deterministic serial reference after every run. Builds and other test runs are stopped during measurement.

The coarse case runs one graphics-sized parallel range. The simultaneous case submits independent heavy graphics and lighter project ranges. The nested case models six ECS systems, each starting an inner parallel range, including the previous pool ordering restrictions. The tiny-task case submits 1,024 short callbacks in eight waves of 128 in both designs; the legacy pool's fixed arena cannot reliably admit the whole burst at once. The batched tiny case computes the same outputs through one `parallelFor` range. These are CPU microbenchmarks on one machine, not measurements of application frame rate.

Worker wakeups are separate from task joins. A completion wakes joining callers. Queue publication and task execution compute which capacity classes have both eligible queued work and parked workers, then notify one worker in each selected class after releasing the queue mutex. Starting a task continues the worker wake chain before entering its callback, so dependency fan-out and blocking test callbacks retain progress without waking every idle worker for each tiny submission.

## Local CPU results (2026-09-08)

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
