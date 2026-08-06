# Frame-lagged async-lighting smoke

This is an opt-in Vulkan target-hardware check for the one-frame-lagged async-lighting path. It requires a visible native window and a Vulkan adapter with a dedicated compute-only queue family; the Graphics queue route exits with code `77` (skip), not a passing async result.

From the repository root, use the build-aware launcher:

```text
python launcher.py frame-lagged-async-lighting
```

The launcher configures the required test targets, builds the smoke executable and cooked runtime assets, and starts GPU validation by default. Use `--no-gpu-validation` when necessary. To pass a lifecycle-runner option, place it after `--`, for example `python launcher.py frame-lagged-async-lighting -- --transition-timeout 30`.

The application starts with lagged lighting enabled. The runner accepts only GPU-submission transitions in this order: bootstrap, active history use, normal current-frame path after F1, then a second bootstrap and active-history use after F1 re-enables the option. It also rejects Vulkan validation, renderer recovery, and history-capture errors.

`--self-test` exercises the launcher command composition without Vulkan; the lifecycle runner's parser/state checks are separately registered with CTest.
