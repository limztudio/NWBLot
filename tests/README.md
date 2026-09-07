# Test layout

- `common/` holds shared test-only entry points, fixtures, and helpers. It is not a CTest suite.
- `unit/` holds deterministic subsystem, policy, and pure-Python helper tests that do not require a live runtime workflow.
- `integration/` holds tests that cross asset, tool, crash, server, or filesystem/process boundaries.
- `smoke/` holds runtime and hardware validation, including GPU-optional probes.
- `ab/` holds manually launched A/B measurement workflows. Each runnable workflow has a terminal `launch.py`; capture, timing, and result artifacts stay local under `.cozter/out/ab-results/`.

CTest target names remain stable across this layout. Source-only CTest directories do not become launcher commands; add a terminal `launch.py` only for an independently runnable workflow. The current root-launcher A/B commands are `async-shadow-m4`, `command-ir`, `frame-lagged-async-lighting`, `hybrid-shadow-boundary`, and `transfer-queue`.

Keep test sources focused on one domain within their suite. When a file grows to cover several domains, split its tests into named sources such as resource imports, command validation, presentation, or telemetry codecs, and register each source in the nearest `CMakeLists.txt`. Preserve the existing executable, suite, and case names so CTest commands and GoogleTest filters continue to work.

Keep helpers beside the tests that use them. Share fixtures and declarations through small headers in a named test detail namespace; place substantial shared implementations in `.cpp` files. Keep domain-specific helpers with their tests, and avoid collecting test bodies in shared headers or numbered source fragments.
