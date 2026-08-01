# Async-shadow M4 target-hardware harness

This harness makes the M4 rollout decision repeatable on a Vulkan target that exposes a dedicated compute-only queue family. It runs the same fixed-yaw stress scene twice:

- `nwb_async_shadow_m4_sync_benchmark` explicitly disables the default async-lane request.
- `nwb_async_shadow_m4_async_benchmark` retains the default `AsyncCompute` request.

The runner rejects an async result if it silently resolves to the Graphics fallback. For a real dedicated lane, it collects the renderer's timestamp envelopes, checks measured `render.async_shadow_effects_overlap`, compares the `render.frame` Graphics critical path rather than summing queue work, captures a fixed-scene pixel A/B after the same number of rendered frames in each mode, and scans logs for ownership or Vulkan-validation failures.

From the repository root, use the one-command launcher:

```powershell
python launcher.py async-shadow-m4
```

It configures the required test targets, builds both benchmarks and their cooked runtime assets, enables GPU validation, and writes a timestamped directory under `__artifacts`. The command returns `77` when the adapter has no distinct compute-only family. To adjust a `run.py` setting, pass it after `--`, for example:

```powershell
python launcher.py async-shadow-m4 -- --measure-seconds 30
```

Pixel capture and timing run in separate processes. After 96 rendered frames, the capture process suspends new render
submission while keeping its native event loop alive, so the sync and async images compare the same temporal-history
phase even when the async path renders more frames per second. The runner waits for the explicit submission-suspended
marker before it settles and captures the window. The timing process then runs normally, without that hold. Adjust the
capture point only when investigating a specific temporal phase:

```powershell
python launcher.py async-shadow-m4 -- --pixel-capture-frames 128
```

Build both benchmark targets and their cooked runtime assets. A debug or namesym build is simplest because timing scope names are readable. For an opt/final build, pass each generated `.namesym` sidecar to the runner.

```bash
cmake --build <build-dir> --target \
  nwb_async_shadow_m4_sync_benchmark \
  nwb_async_shadow_m4_async_benchmark

python tests/ab/async_shadow_m4/run.py \
  --sync-executable <exec-dir>/async_shadow_m4_sync_benchmark \
  --async-executable <exec-dir>/async_shadow_m4_async_benchmark \
  --runtime-dir <build-dir>/Testing/skinning_culling_benchmark_runtime/dbg \
  --logserver-executable <exec-dir>/logserver \
  --output-dir <artifact-dir> \
  --gpu-validation
```

On Linux, run this from an active X11/Xwayland session. The runner sets `NWB_RENDER_UNFOCUSED=1` and freezes `NWB_STRESS_TEST_SPIN_ANGLE=0.6` for a repeatable capture. It returns exit code `77` when the target has no dedicated compute family; that is an environment skip, not a fallback pass.

The default gate needs at least six timing intervals, a median overlap of at least `0.01 ms`, positive overlap in at least half the intervals, no more than `3%` median `render.frame` regression, no forbidden validation/ownership logs, and pixel differences inside the reported tolerance. Tune those thresholds explicitly on the command line for a device's known noise floor. `--report-only` always preserves the report while returning success for a failed rollout gate.

Artifacts include `async.timing.txt`, `sync.timing.txt`, captured logs and BMPs, plus `m4_report.json` and `m4_report.md`. A flat or negative performance result is useful data: retain the Graphics fallback and use the report to decide whether another job merits a separate async proposal.
