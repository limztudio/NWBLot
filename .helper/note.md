# NWBLot Notes

Updated: 2026-03-29

## Important Rules

1. `IDevice` is an essential object in `Graphics`. Do not add runtime null checks for local `IDevice* device` retrievals in this layer.
2. Use `getDevice()` and rely on invariant/assert behavior instead of defensive null handling that adds overhead and hides invalid states.
3. Asset-to-asset and component-to-asset bindings must use typed `Core::Assets::AssetRef<T>`, not raw `Name` or string forms.
4. Graphics pipeline caches must include framebuffer/render-target compatibility in the cache key; material/shader identity alone is not enough when pipeline creation depends on framebuffer info.
5. Basic built-in geometry should live in `.nwb` metadata payloads, not as hardcoded vertex/index arrays inside the cooker.
6. If an asset invariant is format-wide, validate it in the asset/codec layer, not only in one cooker path.
7. Vulkan command buffers must retain bound `IBindingSet` objects until GPU completion; descriptor tables and descriptor-heap ranges are GPU-visible lifetime, not temporary CPU-side state.
8. Explicit textual config tokens like shader `compiler` and `stage` should fail on unknown values; do not silently fall back to a different backend/language.
9. Vulkan/SPIR-V shader entry points need exact case-sensitive text at pipeline creation time; use `Name` only for identity/cache lookup, and preserve the exact entry-point string separately.
10. GPU debug markers / Aftermath event labels are debug text, not identity keys; keep them as exact strings instead of `Name`.
11. Strip UTF-8 BOM on shader source/include text before compilation or include scanning; otherwise the first token/include can fail on BOM-prefixed files.
12. Do not store unused case-sensitive shader entry-point metadata in hash-only archive/index formats; keep only the lookup keys and exact text actually required at the consuming API boundary.
13. Shader define names and variant signatures are exact case-sensitive shader text; do not route them through `Name` or `CompactString`, or distinct variants can collide.
14. Short canonical shader metadata tokens like `compiler`, `stage`, and `target_profile` are good `CompactString` candidates; keep `AString` for unbounded or exact-text fields instead.
15. The ECS renderer material contract is shader-driven only: prefer `MeshShader + PS` on mesh-capable hardware, otherwise use `CS + PS`; do not add a material-facing `VS + PS` fallback path back into this renderer.
16. If a mesh-capable device and material both provide the mesh path, failures creating or using that mesh path are errors to fix, not reasons to silently fall back to compute.
17. Keep mesh and compute-emulation renderer resources path-specific: mesh path bindings stay read-only (`SRV` geometry buffers + push constants), while compute-emulation owns the UAV vertex expansion buffer and its internal raster bridge resources.
18. The compute-emulation path still intentionally uses the internal `engine/graphics/mesh_emulation_vs` bridge and the device-manager render-pass integration; those are runtime requirements, not leftovers from the removed material-facing `VS + PS` path.
19. ECS renderer material assets expose only `mesh` and `ps` stages. Do not add a user-authored `cs` or `task` stage for this path; the cooker derives the internal compute-emulation stage from the mesh shader source automatically.

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



