# Test layout

- `common/` holds shared test-only entry points, fixtures, and helpers. It is not a CTest suite.
- `unit/` holds deterministic subsystem, policy, and pure-Python helper tests that do not require a live runtime workflow.
- `integration/` holds tests that cross asset, tool, crash, server, or filesystem/process boundaries.
- `smoke/` holds runtime and hardware validation, including GPU-optional probes.
- `ab/` holds manually launched A/B measurement workflows. Each runnable workflow has a terminal `launch.py`; capture, timing, and result artifacts stay local under `.cozter/out/ab-results/`.

CTest target names remain stable across this layout. Source-only CTest directories do not become launcher commands; add a terminal `launch.py` only for an independently runnable workflow. The current root-launcher A/B commands are `async-shadow-m4`, `command-ir`, `frame-lagged-async-lighting`, `hardware-shadow-boundary`, `renderer-baseline`, and `transfer-queue`.

Build test prerequisites from the repository root through the launcher, then use CTest to execute the selected suite. For example, on Windows ARM64:

```powershell
python -m launcher build nwb_global_tests --configure-preset windows-clang-arm64 --config dbg
ctest --preset windows-clang-arm64-dbg -R '^nwb_global_tests$' --output-on-failure
```

`python -m launcher build all --configure-preset windows-clang-arm64 --config dbg` builds the complete configured tree without launching applications. Change the configure preset and configuration for another platform or build variant. The full presets enable tests; with an engine or project preset, add `-D NWB_BUILD_TESTS=ON`. Build-specific options such as `--build-dir`, `--jobs`, and `-D KEY=VALUE` belong before any application separator; the build command accepts no application arguments. CTest execution remains a separate command.

Keep test sources focused on one domain within their suite. When a file grows to cover several domains, split its tests into named sources such as resource imports, command validation, presentation, or telemetry codecs, and register each source in the nearest `CMakeLists.txt`. Preserve the existing executable, suite, and case names so CTest commands and GoogleTest filters continue to work.

Keep helpers beside the tests that use them. Share fixtures and declarations through small headers in a named test detail namespace; place substantial shared implementations in `.cpp` files. Keep domain-specific helpers with their tests, and avoid collecting test bodies in shared headers or numbered source fragments.
