# Async-shadow M4 target-hardware harness

This harness makes the M4 rollout decision repeatable on the selected backend when its public topology exposes a dedicated compute-only queue. It runs the same fixed-yaw stress scene twice:

- `nwb_async_shadow_m4_sync_benchmark` explicitly disables the default async-lane request.
- `nwb_async_shadow_m4_async_benchmark` retains the default async-compute lane request.

The native test logs exact primary Graphics/Compute queue IDs and device generations, along with public Compute capability and dedicated status. The runner accepts only a requested, distinct, current-generation, dedicated Compute-capable queue without Graphics capability; missing or shared transport returns the capability skip, while invalid identities or contradictory evidence fail. The synchronous request remains disabled even on hardware that needs a separate Compute transport.

The runner rejects an async result if it silently uses the Graphics queue route. For a real dedicated lane, it collects the renderer's timestamp envelopes, verifies that graph-owned `render.async_shadow` reports work, compares the `render.frame` Graphics critical path rather than summing queue work, captures a fixed-scene pixel A/B after the same number of rendered frames in each mode, and scans logs for ownership or selected-backend validation failures.

From the repository root, use the one-command launcher:

```powershell
python -m launcher async-shadow-m4
```

It configures the required test targets, builds both benchmarks and their cooked runtime assets, enables GPU validation, and writes a timestamped directory under `.cozter/out/ab-results/async-shadow-m4/`. The command returns `77` when the adapter has no distinct dedicated compute-only queue. To adjust a `run.py` setting, pass it after `--`, for example:

```powershell
python -m launcher async-shadow-m4 -- --measure-seconds 30
```

Pixel capture and timing run in separate processes. After 96 rendered frames, the capture process suspends new render
submission while keeping its native event loop alive, so the sync and async images compare the same temporal-history
phase even when the async path renders more frames per second. The runner waits for the explicit submission-suspended
marker before it settles and captures the window. The timing process then runs normally, without that hold. Adjust the
capture point only when investigating a specific temporal phase:

```powershell
python -m launcher async-shadow-m4 -- --pixel-capture-frames 128
```

On Windows, the M4 capture path restores, raises, and foregrounds the benchmark window before waiting for that
held-frame marker. It then requires `DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE=33,
DWMWCP_DONOTROUND=1)` and `DwmFlush` to succeed before the existing raw client-area screen capture. This is scoped to
M4; it does not mask pixels or change generic capture behavior. Windows 11 build 22000 or later is required: the
documented unsupported-attribute result `E_INVALIDARG` (`HRESULT 0x80070057`) is an explicit skip (exit 77). Every
other non-`S_OK` DWM result, including any `DwmFlush` failure, is an explicit M4 test failure rather than a
best-effort fallback, because the resulting composed desktop capture would not be a valid raw parity artifact.

Build both benchmark targets and their cooked runtime assets in the chosen configuration. The runner decodes its known required scopes (`render.frame`, `render.async_shadow`, and `render.async_final`) in normal opt/final builds without a `.namesym` sidecar. Optional `--sync-namesym` and `--async-namesym` sidecars decode additional scope names.

```bash
python -m launcher build \
  nwb_async_shadow_m4_sync_benchmark \
  nwb_async_shadow_m4_async_benchmark nwb_logserver \
  --configure-preset <configure-preset> --build-dir <build-dir> --config dbg \
  -D NWB_BUILD_TESTS=ON

python tests/ab/async_shadow_m4/run.py \
  --sync-executable <exec-dir>/async_shadow_m4_sync_benchmark \
  --async-executable <exec-dir>/async_shadow_m4_async_benchmark \
  --runtime-dir <build-dir>/Testing/skinning_culling_benchmark_runtime/dbg \
  --logserver-executable <exec-dir>/logserver \
  --output-dir <artifact-dir> \
  --gpu-validation
```

Choose a configure preset for the target platform and architecture, and use the same build directory and configuration for both the launcher build and runner paths. The build command configures a cold directory automatically and cooks the target runtime assets without starting either benchmark. On Linux, run this from an active X11/Xwayland session. The runner sets `NWB_RENDER_UNFOCUSED=1` and freezes `NWB_STRESS_TEST_SPIN_ANGLE=0.6` for a repeatable capture. It returns exit code `77` when the target has no dedicated compute-only queue; that is an environment skip after selecting the Graphics queue route.

After wall-clock warmup, the runner snapshots the timing file's EOF and waits for two valid, fully newline-terminated interval headers beginning at or after that byte cutoff. It discards the first report because it may span warmup, then starts the requested measurement duration and parses from the second header's byte offset. The report records this boundary as `measurement_start_byte_offset`. Only publication windows admitted after that boundary contribute to the timing gate; GPU readback latency means the boundary is not a strict wall-clock fence on source-frame execution. Missing, malformed, or truncated timing output, process exit, and boundary timeout fail rather than admit warmup data.

The default gate needs at least six timing intervals, a median graph-owned `render.async_shadow` duration of at least `0.01 ms`, no more than `3%` median `render.frame` regression, no forbidden validation/ownership logs, and pixel differences inside the reported tolerance. Tune those thresholds explicitly on the command line for a device's known noise floor. `--report-only` always preserves the report while returning success for a failed rollout gate.

Artifacts include `async.timing.txt`, `sync.timing.txt`, captured logs and BMPs, plus `m4_report.json` and `m4_report.md`. A flat or negative performance result is useful data: retain the Graphics queue route and use the report to decide whether another job merits a separate async proposal.
