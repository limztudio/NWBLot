# Frame-lagged async-lighting smoke

This is an opt-in Vulkan target-hardware check for the one-frame-lagged async-lighting path. It requires a visible native window and a Vulkan adapter with a dedicated compute-only queue family; a Graphics fallback exits with code `77` (skip), not a passing async result.

Build the smoke executable and cooked assets, then run the lifecycle harness:

```bash
cmake --build <build-dir> --target nwb_frame_lagged_async_lighting_smoke

python launcher.py frame-lagged-async-lighting \
  --executable <exec-dir>/frame_lagged_async_lighting_smoke \
  --runtime-dir <build-dir>/Testing/smoke_runtime/dbg \
  --logserver-executable <exec-dir>/logserver \
  --gpu-validation
```

The application starts with lagged lighting enabled. The runner accepts only GPU-submission transitions in this order: bootstrap, active history use, normal current-frame fallback after F1, then a second bootstrap and active-history use after F1 re-enables the option. It also rejects Vulkan validation, renderer recovery, and history-capture errors.

`--self-test` exercises the parser/state checks without Vulkan and is registered with CTest.
