# Test layout

- `common/` holds shared test-only entry points, fixtures, and helpers. It is not a CTest suite.
- `unit/` holds deterministic subsystem, policy, and pure-Python helper tests that do not require a live runtime workflow.
- `integration/` holds tests that cross asset, tool, crash, server, or filesystem/process boundaries.
- `smoke/` holds runtime and hardware validation, including GPU-optional probes.
- `ab/` holds manually launched A/B measurement workflows. Each runnable workflow has a terminal `launch.py`; capture, timing, and result artifacts stay local under `.cozter/out/ab-results/`.

CTest target names remain stable across this layout. Source-only CTest directories do not become launcher commands; add a terminal `launch.py` only for an independently runnable workflow. The current root-launcher A/B commands are `async-shadow-m4`, `bindless-parity`, and `frame-lagged-async-lighting`.
