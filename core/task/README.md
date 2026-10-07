# CPU and GPU execution domains

`CpuTaskScheduler` and `GpuTaskScheduler` are frame-owned execution services with parallel module layouts:

| Domain | Public entry | Target | Work description |
| --- | --- | --- | --- |
| CPU | `cpu/scheduler.h` | `nwb_cpu_task` | Callables, `CpuTaskScope`, CPU completion handles |
| GPU | `gpu/scheduler.h` | `nwb_gpu_task` | Task graphs, compiled plans, GPU submission tokens |

`Frame::cpuTasks()` and `Frame::gpuTasks()` expose the same instances borrowed by the graphics runtime and project context. GPU scheduler admission compiles declared graphs from a current queue-pressure snapshot, then owns native recording and submission; `wait()` joins its device work and `wait(token)` joins a submitted operation. CPU and GPU completion remain distinct.

The GPU scheduler owns its execution admission and binding lifetime, not the selected graphics device. `GraphicsRuntime` owns device creation, resources, render passes, and presentation in `core/graphics/runtime`. It binds the GPU scheduler before resource callbacks, joins work before teardown, and detaches before device destruction. A failed detach leaves the device binding intact. Swap-chain resizing keeps the same binding.

Dependencies run in one direction:

```text
nwb_graphics (runtime) -> nwb_gpu_task -> nwb_graphics_backend -> nwb_cpu_task -> nwb_alloc
```

Backend code cannot include GPU graph/runtime headers, GPU tasks cannot include the graphics runtime, and CPU tasks cannot include graphics. GPU task code uses neutral RHI contracts and selected headers under `core/graphics/backend_selection/` for concrete device and command-list operations; native Vulkan headers and SDK types stay inside the provider. Compile-time selection adds no runtime dispatch, per-resource storage, or allocation overhead. Vulkan is the default and only implemented provider; selecting Metal is rejected until its provider exists.

Task-domain unit tests mirror the layout under `tests/unit/task/cpu` and `tests/unit/task/gpu`; selected-backend resource and presentation contract tests live under `tests/unit/graphics`. Test code and fixtures stay under `tests/`; new unit coverage targets edge cases through the selected public contracts rather than a provider-specific test backend.
