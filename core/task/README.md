# CPU and GPU run fields

`CpuTaskScheduler` and `GpuTaskScheduler` are Frame-owned run helpers with
matching folder plans:

| Field | Public door | Target | Work note |
| --- | --- | --- | --- |
| CPU | `cpu/scheduler.h` | `nwb_cpu_task` | Callables, `CpuTaskScope`, CPU done handles |
| GPU | `gpu/scheduler.h` | `nwb_gpu_task` | Task graphs, built plans, GPU send tokens |

`Frame::cpuTasks()` and `Frame::gpuTasks()` show the same live copies lent
to the graphics run core and project context. GPU plan take-in builds stated
graphs from a current queue-load still shot. Then it owns home record and
send. `wait()` joins its card work, and `wait(token)` joins one sent job.
CPU and GPU done states stay split.

The GPU plan helper owns its run take-in and link life, not the picked
graphics card. `GraphicsRuntime` owns card make, stores, render passes, and
show in `core/graphics/runtime`. It links the GPU helper before store call
backs, joins work before tear down, and unlinks before card kill. A failed
unlink leaves the card link whole. Swap-chain resize keeps the same link.

Needs run one way:

```text
nwb_graphics (runtime) -> nwb_gpu_task -> nwb_graphics_backend -> nwb_cpu_task -> nwb_alloc
```

Back code cannot take GPU graph/run heads. GPU tasks cannot take the graphics
run core. CPU tasks cannot take graphics. GPU task code uses plain RHI rules
and picked heads under `core/graphics/backend_selection/` for real card and
list calls. Home Vulkan heads and SDK kinds stay inside the back. Build-time
pick adds no run fork, per-store keep, or make cost. Vulkan is the default
and only built back. Metal pick fails until its back is there.

Task-field unit tests match the plan under `tests/unit/task/cpu` and
`tests/unit/task/gpu`. Picked-back store and show rule tests live under
`tests/unit/graphics`. Test code and fixtures stay under `tests/`. New unit
cover aims at edge cases through the picked public rules, not a back-only
test back.
