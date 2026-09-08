# CPU and GPU execution domains

`CpuTaskScheduler` and `GpuTaskScheduler` are frame-owned execution services with parallel module layouts:

| Domain | Public entry | Target | Work description |
| --- | --- | --- | --- |
| CPU | `cpu/scheduler.h` | `nwb_cpu_task` | Callables, `CpuTaskScope`, CPU completion handles |
| GPU | `gpu/scheduler.h` | `nwb_gpu_task` | Task graphs, compiled plans, GPU submission tokens |

`Frame::cpuTasks()` and `Frame::gpuTasks()` expose the same instances borrowed by the graphics runtime and project context. GPU `submit()` coordinates recording and submission; `wait()` joins its device work and `wait(token)` joins a submitted operation. CPU and GPU completion remain distinct.

The GPU scheduler owns its execution admission and binding lifetime, not the Vulkan device. `GraphicsRuntime` owns device creation, resources, render passes, and presentation in `core/graphics/runtime`. It binds the GPU scheduler before resource callbacks, joins work before teardown, and detaches before device destruction. A failed detach leaves the device binding intact. Swap-chain resizing keeps the same binding.

Dependencies run in one direction:

```text
nwb_graphics (runtime) -> nwb_gpu_task -> nwb_graphics_backend -> nwb_cpu_task -> nwb_alloc
```

Backend code cannot include GPU graph/runtime headers, GPU tasks cannot include the graphics runtime, and CPU tasks cannot include graphics. The task-domain policy checks enforce these boundaries. Tests mirror the task layout under `tests/unit/task/cpu` and `tests/unit/task/gpu`; native graphics tests remain under `tests/unit/graphics`.
