# Test layout

- `common/` holds shared test-only entry points, fixtures, and helpers. It is not a CTest suite.
- `unit/` holds deterministic subsystem, policy, and harness tests that do not require a live runtime workflow.
- `integration/` holds tests that cross asset, tool, crash, server, or filesystem/process boundaries.
- `smoke/` holds runtime and hardware validation, including GPU-optional probes.
- `ab/` holds manually launched A/B measurement workflows; selected workflows also expose parser self-tests to CTest.

CTest target names remain stable across this layout. Source-only CTest directories do not become launcher commands; add a terminal `launch.py` only for an independently runnable workflow.
