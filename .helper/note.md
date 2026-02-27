# NWBLot Notes

Updated: 2026-02-27

## Important Rules

1. `IDevice` is an essential object in `Graphics`. Do not add runtime null checks for local `IDevice* device` retrievals in this layer.
2. Use `getDevice()` and rely on invariant/assert behavior instead of defensive null handling that adds overhead and hides invalid states.

## Scheduler Architecture

1. Keep responsibilities split:
   - `Graphics` layer uses `Alloc::JobSystem` for orchestration-level async jobs and dependencies.
   - Vulkan backend keeps `Alloc::ThreadPool` for fine-grained `parallelFor` style workloads.
2. Do not unify Vulkan internals onto `JobSystem` without measured evidence (oversubscription/contention/scheduling conflict).
3. Favor adapter-based unification later (if needed) over direct hard migration.

## JobSystem Performance Status

1. Applied:
   - Per-job completion signaling (removed global completion wakeup behavior).
   - Batched enqueue path for ready dependent jobs.
   - Replace `std::function` task storage on hot path.
   - Work-first dependent execution (inline one continuation, enqueue remainder).
   - Split `ThreadPool` synchronization domains (`parallelFor` control vs task queue).
   - Reduce per-completion temporary container overhead.
