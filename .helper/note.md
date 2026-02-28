# NWBLot Notes

Updated: 2026-02-28

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

## Project Bootstrap Invariants

1. `CreateInitialProjectWorld` / `DestroyInitialProjectWorld` are strict engine bootstrap APIs for required core world/system setup and teardown.
2. In project runtime code, `m_world` and required core systems (e.g., renderer system) are treated as required invariants after successful startup.
3. Prefer assertion/invariant semantics in project runtime for required core objects; avoid defensive per-frame null checks for them.
4. For project callback instances created after frame init, keep required core objects as non-null members (owner + references) and tear them down in callback destruction before frame destruction.
5. If ownership and non-null access point to the same object (`UniquePtr` + duplicate reference), prefer a single owner member and keep non-null by construction/contract to reduce duplicated state.
6. For required owned pointers that must not be treated as nullable, use the generic `NotNullUniquePtr<T>` wrapper (no `operator bool`) instead of ad-hoc per-class storage wrappers.
7. Keep `IProjectEntryCallbacks` lifecycle/update callbacks minimal (`onStartup()`, `onUpdate(f32)`, `onShutdown()`), and use the callback object's stored runtime context instead of passing context every call.

## ECS Runtime Type Safety

1. Keep `World::getSystem<T>()` RTTI-free (`/GR-` compatible) by using explicit `SystemTypeId` matching instead of `dynamic_cast`.

## ECS API Shape

1. `World` is still the ownership/lifecycle source of truth for component storage.
2. Keep `World` component APIs (`add/remove/get/hasComponent`) private and expose them only through the generic `NWB::Core::ECS::Entity` facade.
3. Treat `EntityID` as a lightweight ID and `Entity` as the component operation surface.
4. Do not introduce ad-hoc or project-local ECS wrappers for this role.

