# Shared CPU task scheduling

NWB's runtime owns one `Core::CpuTaskScheduler`, constructed before GraphicsRuntime and project initialization. Systems own task scopes and data; they borrow the execution service. Asset-builder and FBX-converter processes each construct their own scheduler once at their entry point.

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

Publish the initialized scheduler/scope to producers using normal C++ synchronization. Keep the scope, scheduler, and referenced task data alive until producers have stopped and tasks have joined. A scope/scheduler `wait()` observes admitted work; it does not close admission or wait for an external producer that has not submitted yet. Dependency handles shared across producer threads also require normal caller synchronization. Expected cancellation or terminal shutdown can reject submissions with an invalid handle.

## Dependencies and structured completion

`CpuTaskScope::submit()` returns a generation-tagged handle in the scheduler's single identity domain. Dependencies may cross system scopes. Foreign scheduler handles are rejected. Completed handles remain valid prerequisites after node reuse.

Tasks submitted from a running callback automatically become children of that callback's task. Completion means the callback, all descendants, and the task's captured objects have finished. A dependent system cannot observe partial child work simply because its producer's callback returned. A child's explicit dependency must not introduce a cycle through its parent, an ancestor, or their dependent tasks; such submission is rejected.

Callbacks must keep their referenced data alive. Captured owning objects stay alive through descendant completion; references to automatic variables inside a callback do not acquire an extended C++ lifetime. Join children before those local variables leave scope. Capture destructors must not rely on implicitly attaching new asynchronous work to the task being retired; submit such work explicitly in a separately owned scope.

Ordinary task handles and task scopes are the completion boundaries. A scope includes its admitted tasks, their descendant lifetimes, and capture retirement. Its owner must stop external producers before destroying the scope or its data. Submitting concurrently with destruction is invalid. A task cannot wait on itself, an ancestor, dependent work that needs its completion, or a scope containing any ancestor. Expected cancellation is explicit through a scope; unexpected exceptions remain terminal at native worker boundaries or unwind to the application entry boundary when executed on the caller. Exceptions are never stored for deferred delivery.

Normal scope and scheduler destruction joins outstanding work through a throwing wait. `drain()` is reserved for terminal cleanup: it stops admission across the shared scheduler and skips queued callbacks before joining active work and retiring captures. Scope `drain()` also aborts the shared service, because application unwind is terminal under the engine exception policy. Use `cancel()` followed by `wait()` for ordinary scope-local cancellation. During an existing exception, destructors use terminal drain so cleanup cannot invoke another queued throwing callback. Owners that store task scopes behind a non-throwing smart-pointer destructor must explicitly join before resetting the pointer; project world shutdown already does this through `World::clear()`.

Completion accounting and the transition to retirement share one scheduler lock. The thread that performs the transition owns retirement, destroys the callable outside the lock, then publishes completion under the lock. An empty scope can return after an acquire load of its pending count; profiling still records the join. Joining callers register under the scheduler mutex before parking. Progress snapshots that registration under the same mutex. Retirement broadcasts to registered joiners when a dependent becomes ready, a scope completes, the scheduler completes, or a handle join is registered. Submission, cancellation, and drain retain their broader progress notifications; worker notifications retain their existing policy.

Task nodes, dependency storage, and worker metadata belong to the scheduler's tracked `Alloc::GlobalArena`. The task domain owns its arena identity in `core/task/cpu/arena_names.h`: `core/task/cpu/scheduler`. Nested dependency checks mark the ancestor/dependent closure once in scheduler-owned search storage, then test membership while resolving the submitted predecessors. They do not rescan the predecessor list for each reachable node or allocate a temporary arena. Storage grows with the task graph and reuses completed node slots. There is no small fixed arena limit on ordinary task bursts. Search storage grows geometrically and is reserved before publishing nodes, keeping completion cleanup allocation-free.

## Parallel loops and main-thread work

`parallelFor()` partitions a range into ordinary CPU tasks and joins a local scope. It has no shared global dispatch slot and supports nested loops with one worker. Cooperative scope joins select work contributing to the joined scope, including its prerequisites, so unrelated queued work does not become part of a subsystem lifetime barrier. Parallel-loop callers can participate; ordinary main-thread scope waits pump eligible main-thread tasks while worker tasks execute on workers. Stable worker domain/index identities preserve native command-recording storage ownership.

Each scope wait remembers a generation-tagged cursor for the unrelated prefix of each ready queue. Later claims resume after that cursor while it remains queued; a removed or recycled cursor restarts the search. Successful task or range publication invalidates the cursors because it can introduce new prerequisite paths. Nested and concurrent waits keep separate cursors, and priority/target eligibility still applies before queue selection. This avoids walking the same unrelated prefix for every joined task.

The main thread must pump the scheduler while main-thread tasks are outstanding. Frame pumps at its update boundary; scheduler/task-scope waits on the main thread also make progress. Blocking an OS thread on an external latch does not pump this queue. Avoid blocking I/O, GPU completion waits, and unbounded callbacks in ordinary CPU tasks. An external callback can submit a continuation when its operation actually completes.

## ECS and graphics integration

ECS preparation remains a serial caller phase because existing preparation can create entities and change component storage. Each preparation callback must complete those structural changes before returning. Updates are dependency tasks: every conflicting component access preserves system registration order; read/read accesses can overlap. Structural changes during concurrent updates still require the caller to provide a safe structural boundary. UI updates explicitly target the main thread.

`GraphicsRuntime` owns a scope for async resource setup and resource-lifetime joins. The loader owns a project scope and joins it before project callbacks are unloaded. Normal world clearing joins only its world scope. GPU recording uses the shared CPU scheduler while retaining worker-local command storage, serial command-IR capture, and the current skinning preparation/submission ordering.

`Core::GpuTaskScheduler`, declared in `core/task/gpu/scheduler.h`, coordinates GPU graph recording, submission, and recovery. It lives in the GPU task domain alongside the graph compiler and recorder. `GraphicsRuntime` owns the separate device, resource, and presentation lifecycle and borrows the frame-owned CPU/GPU schedulers. The GPU graph continues to own resource barriers, physical queues, acceptance, rollback, and device-completion tokens. CPU submission completion does not imply GPU completion. Resource destruction still requires its existing GPU join in addition to CPU scope retirement.

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

## CPU scheduler follow-up improvements

### Step 1: scope wait cycle rejection

Scope joins now reject direct and transitive dependents of the current execution or its structured ancestors, matching task-handle joins. Cooperative work and wakeups recheck the graph so later dependency publication cannot silently introduce this wait cycle. Wait and cycle logic live together in `core/task/cpu/scheduler_wait.cpp`. Regression coverage includes direct, transitive, newly published dependencies, and valid independent joins.

Validation: the direct-cycle regression timed out against the original implementation. All four new wait cases, the full Linux Debug CPU task suite, and all 52 source-policy checks/self-tests passed after the fix.

### Step 2: batched parallel ranges

`parallelFor()` reserves and publishes its chunk nodes under one scheduler lock, with one publication notification. Internal captures contain only the caller-context pointer, an invocation pointer, and range bounds; user callables are neither copied nor invoked under that lock. A single chunk runs directly through the ordinary execution/retirement machinery when the caller is an eligible owner or worker. External producers and off-owner main-thread work remain queued. Descendants, cancellation boundaries, and exception cleanup still use task nodes and scopes.

Seven opt-in `CpuTaskProfile.DISABLED_*` cases record raw samples and validate deterministic outputs. They are excluded from normal tests and have no timing pass threshold. On Linux x64 Optimize, AMD BC-250 (6 cores / 12 logical processors), three ABBA epochs gave 48 measured samples per design with zero placement failures. Builds and other tests were stopped during measurement. Times below are complete benchmark-case medians in microseconds, not per-element costs or application frame times:

| Workload | Before | After |
| --- | ---: | ---: |
| 128 single-chunk ranges, one worker | 76.340 | 31.815 |
| 32 tiny batched ranges, four workers | 1337.584 | 945.188 |
| Two coarse ranges, four workers | 1102.427 | 1082.951 |
| Nested ranges, one worker | 101.128 | 90.746 |
| Nested ranges, four workers | 1245.630 | 1091.935 |

The one-worker nested tail varied in the first combined run. A focused six-epoch ABBA confirmation (96 samples/design) measured median 100.751 → 91.421 µs and p95 131.455 → 121.088 µs. Unrelated-scope and cancellation-history controls stayed similar; their optimizations are later steps. Raw XML, summaries, and the baseline binary are retained under ignored `__cmake/cpu_scheduler_followup/step2/`.

Validation includes Debug and Optimize CPU suites, an inline exception/descendant-retirement case, caller/worker identity, external producer routing, cancellation, nested ranges, and all 52 source-policy checks/self-tests. Physical heterogeneous-class cases skip on this homogeneous host.

### Step 3: reuse negative scope searches

This step initially cached only exhausted searches that proved a node could not contribute to its scope. It used a unique wait identity and per-node generation stamps, never a persistent scope pointer. Publication invalidated the stamps, while retirement preserved them because it only removed graph paths. Later passes below add proven positive results and share the search identity between joins of the same scope.

The zero-worker scope benchmark (128 unrelated roots feeding a shared 64-task chain ahead of 32 joined tasks) improved from median 2865.730 → 29.494 µs and p95 2898.091 → 32.984 µs. This is a deliberately dependency-heavy microbenchmark. Three ABBA epochs produced 48 samples per design; output/lifetime checks all passed. Other workload medians remained similar or improved, while some worker-based p95 values varied between runs. Raw results and the baseline binary are under ignored `__cmake/cpu_scheduler_followup/step3/`.

Debug/Optimize CPU suites and all 52 policy checks passed. Eight wait cases passed 50 repetitions (400 cases), including a newly published prerequisite, recycled node slots, nested scope identities, and concurrent waiters traversing unscoped prerequisites.

### Step 4: index canceled generations by task slot

Each slot retains its newest canceled generation inline and a sorted vector of older canceled generations. Late dependencies select the slot directly, then check its latest generation or binary-search its older history. Histories survive node reuse. Capacity for a possible older entry is reserved before ordinary or batched publication, so canceled retirement remains allocation-free. The first cancellation of a slot needs no history allocation. Old handles retain their existing lifetime semantics; this is not history eviction.

The 512-lookup benchmark with 4096 canceled handles improved from median 525.350 → 61.685 µs and p95 561.969 → 100.058 µs over three ABBA epochs (48 samples/design). Other median controls stayed similar, with the coarse control varying by about 4%. Raw results and baseline are under ignored `__cmake/cpu_scheduler_followup/step4/`.

Debug/Optimize CPU suites and all 52 policy checks passed. Added coverage alternates successful/canceled generations through 1024 slot reuses, checks independent slots with matching generation numbers, and verifies original cancellation results after both successful and canceled parallel ranges reuse those slots.

### Step 5: optional CPU task profiling

Profiling is disabled by default. Steady-state disabled scheduling reads no profiling clocks and allocates no event or ready-timer buffers. `CpuTaskOptions::profileLabel` and scope labels are compact eight-byte handles; register their owned names once through `registerProfileLabel()` during subsystem setup. Names are copied into events only during capture. Task labels override scope/ancestor labels. A foreign scheduler label resolves to an unnamed event rather than aliasing a local name.

`setProfiling(enabled, frameIndex)` enables or disables collection and updates frame attribution. `CpuTaskSchedulerConfig::profileEventCapacity` bounds storage (default 4096 events; zero disables capture). Events are read through `readProfileEvents()`; statistics expose pending, recorded, and dropped event counts. A full buffer retains existing events and counts newly dropped samples. Changing capture state discards buffered samples and advances an epoch, preventing old ready, execution, join, or idle timers from crossing a disable/re-enable boundary. Updating only the frame index preserves the capture epoch.

The event kinds are:

- Queue delay: time from becoming ready to callback start, excluding predecessor dependency waits.
- Execution: callback wall time, including nested cooperative work, with completion-mutex acquisition excluded.
- Handle, scope, and whole-scheduler joins: caller wait intervals, including cooperative work.
- Worker idle: completed condition-variable waiting intervals that began during capture. Enabling capture while a worker is already parked omits that initial interval.

Events retain task identity, worker lane/affinity, captured frame and epoch, and an owned label. Canceled callbacks do not produce execution samples. Events are emitted when their measured interval completes, so delayed samples retain their original frame attribution. Execution and join intervals overlap with nested/cooperative work; totals across workers and timing kinds are not frame wall time.

Frame CPU perf capture now enables scheduler profiling automatically. `Perf::CollectCpuTaskProfile()` drains only the initial pending-count snapshot on the timing-sink owner thread, before perf publication. Worker threads never call the non-thread-safe timing recorder. Named execution scopes cover graphics setup/frame work, ECS worlds, and project work; other events use `cpu.task.queue_delay`, `cpu.task.execution`, `cpu.task.handle_join`, `cpu.task.scope_join`, `cpu.task.scheduler_join`, and `cpu.worker.idle`. Existing perf telemetry transports these timing scopes without a new wire schema. Failed or quit frames discard unpublished profiling data.

The disabled-path ABBA comparison against step 4 used 48 samples/design. Single-chunk median was 33.061 → 33.289 µs and tiny-batch median 921.848 → 932.381 µs; the other median controls ranged from about -5% to +4.4%, with worker p95 variation. These measurements establish the local disabled overhead rather than claiming profiling itself is free. Raw comparisons and the baseline are under ignored `__cmake/cpu_scheduler_followup/step5/`.

Final validation: the Linux Debug build passed for CPU/GPU tasks, perf/telemetry, graphics, ECS/rendering, native smoke tests, asset tools, FBX conversion, and Testbed. All ten selected CTest targets passed, including the three native Vulkan/asset suites; all 52 source-policy checks/self-tests passed. The final Debug and Optimize CPU suites each ran 68 cases: 66 passed and two physical heterogeneous-class cases skipped on this host. Eight CPU profiling cases cover disabled capture, labels/frame attribution, buffer wrap/overflow, stale epoch rejection, native idle lanes, foreign labels, zero capacity, and concurrent capture changes. Four adapter tests verify perf/telemetry publication, owner-thread delivery, disabled-sink draining, and bounded collection. Native tests retain capability-based skips for unavailable queues/extensions. Windows builds and application frame-rate measurements were not performed on this Linux host.

### Post-integration cleanup

Graphics resources now expose their shared scheduler directly to Vulkan callers; the redundant parallel-range forwarding methods are removed. The resource base needs only a scheduler forward declaration, and upload declaration files include the task graph instead of the compiler. Nested dependency checks reuse the scheduler's pre-sized search vectors and generation stamps, removing the temporary dependency arena. Profiling metadata preparation no longer reads a timestamp that execution immediately overwrites; each measured interval establishes its own start time.

The Linux Debug build, eight selected CPU/GPU/graphics/telemetry/native test targets, the Optimize CPU suite, and all 52 policy checks passed. Capability-dependent tests retained their existing skips.

### 2026-10-01: scheduler optimization passes

The completed passes add generation-tagged ready-queue cursors, stamped nested-dependency membership, shared completion accounting/retirement transitions, an acquire-load empty-scope shortcut, and registered-joiner notification gating. Capture destruction remains outside the scheduler mutex and precedes completion publication. Every successful task/range publication invalidates scope cursors; claims and retirement validate cursor generation and queue state.

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

### 2026-10-01: reuse proven scope prerequisites

A follow-up probe found that successful scope searches still walked a shared continuation chain for every ready prerequisite and again for each chain node. Scope searches now retain both outcomes in the existing per-node stamp vector. Even generations encode exhausted negative searches; the following odd value encodes a proven path to the joined scope. No second cache array is allocated. The former negative-only field and search implementation are replaced by this contribution cache in `scheduler_scope_search.cpp`.

A successful search propagates positive results backward through only its processed breadth-first prefix, following generation-valid parent/dependent edges already proved positive. Visited siblings without such a path remain unmarked, and unprocessed sibling frontiers are not scanned. A live prerequisite's proven downstream path cannot disappear through retirement: every dependent or structured parent on that path still awaits the prerequisite's completion. At that stage, publication and changes of wait identity conservatively invalidated both outcomes; generation-tagged ready cursors retained their existing rules.

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

### 2026-10-01: reject unrelated leaf tasks without a graph walk

A cold scope query now records an immediate negative result when the ready task is outside the requested scope, has no dependents, and has no live structured parent. Those are all possible outgoing contribution paths, so this avoids preparing a graph search without changing scope membership, cache invalidation, or queue ordering. Existing sibling, late-publication, recycled-generation, and parent-lifetime regressions cover these boundaries. A separate masked-tag lookup trial showed no measurable benefit and was not retained.

An isolated Optimize confirmation against `f570ff93c` used the same explicit scope-search object position, three counterbalanced epochs, and 48 samples per design/control. Joining 512 scoped callbacks behind 4,096 unrelated ready tasks improved from 117.055 to 91.327 microseconds (22%); the 1,024-task prefix improved from 71.040 to 65.097 microseconds. The no-prefix control stayed at 46.410 versus 46.419 microseconds, and the coarse-range control stayed near 1.081 milliseconds. Small controls varied: the single-chunk median was 24.928 versus 25.800 microseconds, and concurrent positive-chain joins varied by roughly 2–4%; those differences are not improvements or application frame-rate claims. The full current Optimize CPU suite passed for the isolated candidate. Source/binary hashes, raw XML, and the comparison are retained under ignored `__cmake/scheduler_continuation/2026-10-01/cpu_scope_lookup/`.

### 2026-10-01: share scope proofs and reduce unnecessary join wakeups

Each nonempty scope lazily receives one search identity under the scheduler mutex. Concurrent and repeated joins of that scope reuse contribution proofs; their queue cursors remain separate. Caller-dependent cycle validation still runs before assistance and in every scope wait predicate. Publication still invalidates all contribution stamps, and scope identities terminate on wrap instead of being reused. This adds one `u64` per scope and no per-node cache storage. The former per-wait identity counter and cache key names are replaced.

Retirement now wakes registered joiners when it makes a dependent ready, completes any scope, completes the scheduler, or finds a registered handle join. Ready work includes unscoped prerequisites needed by a scoped join. Handle-wait registration and the notification snapshot share the scheduler mutex, with the existing predicate-before-park check; a late waiter observes the published state before sleeping. Submission, cancellation, drain, worker notifications, callable destruction, and completion publication keep their existing boundaries.

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
