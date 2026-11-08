# Test plan

- `common/` holds shared test-only doors, fixtures, and help code. It is not
  a CTest set.
- `unit/` holds fixed part tests and plain-Python help tests with no live
  run flow.
- `integration/` holds tests that cross asset, tool, crash, server, or
  file/box edges.
- `smoke/` holds run-time and hardware checks, with GPU-opt probes.
- `ab/` holds hand-run A/B measure flows. Each run-ready flow has an end
  `launch.py`. Shots, times, and result files stay local under
  `.cozter/out/ab-results/`.

CTest target names stay firm in this plan. Source-only CTest folders do not
turn into launcher calls. Add an end `launch.py` only for a free-run flow.
The current root-launcher A/B calls are `async-shadow-m4`,
`frame-lagged-async-lighting`, `hardware-shadow-boundary`, and
`renderer-baseline`.

Build test needs from the repo root through the launcher. Then use CTest to
run the picked set. For example, on Windows ARM64:

```powershell
python -m launcher build nwb_global_tests --configure-preset windows-clang-arm64 --config dbg
ctest --preset windows-clang-arm64-dbg -R '^nwb_global_tests$' --output-on-failure
```

`python -m launcher build all --configure-preset windows-clang-arm64 --config dbg`
builds the full set tree with no app starts. Change the setup form and build
form for another box or build kind. The full forms turn tests on. With an
engine or project form, add `-D NWB_BUILD_TESTS=ON`. Build-only flags like
`--build-dir`, `--jobs`, and `-D KEY=VALUE` go before any app split. The
build call takes no app flags. CTest run stays a split call.

Listed shots that ask for GPU checks turn them on in `dbg` and `opt`, and
turn that ask off in `fin`. The `fin` loader turns down `--gpudbg` before
graphics start. Shared shot covers default checks on for hand use. Pick
`--no-gpu-validation` with a `fin` exe. The plain `window_capture_smoke.py`
defaults it off. Ask for `--gpu-validation` with a `dbg` or `opt` exe when
checks are needed. Checks cannot turn on by shooting an already-run
`--window-handle`.

Unit tests cover real edges and past bugs: bad or no-good input, exact ends,
over-flow, blank or weak data, same-spot use, old versions, hold, drop,
re-do, side-by-side runs, order, and fail rescue. Drop plain defaults,
get/set pairs, flag pass-through, name maps, good round trips, and
source-text wire checks. In mixed cases, keep the edge and the setup or good
check needed to prove it. This span also fits Python read tests and cover
self-checks listed as unit sets. Real CLI and run flows stay in
integration/smoke cover. Plain speed exes stay set speed work under
`tests/performance/` and `tests/smoke/`.

Keep test sources on one field in their set. When a file grows to cover many
fields, cut its tests into named sources like pack imports, call checks,
show, or stream codecs, and list each source in the near `CMakeLists.txt`.
Keep exe and set names firm where you can, and name cases for their tested
edge. Drop idle fixtures and CMake lists with their tests.

Keep all unit-test sources, fixtures, and test-only help under `tests/`,
with help next to the tests that use it. Share fixtures and notes through
small heads in a named test part space. Put big shared code in `.cpp` files.
Keep field help with its tests. Do not pile test bodies in shared heads or
counted source bits. Graphics tests use set public rules after normal backend
pick. Never add Vulkan-, Metal-, or other back-only tests, home SDK claims,
closed back fixtures, direct back replays, or production test hooks. Test
covers may set box loaders and read home check notes needed to fail run
warns.
