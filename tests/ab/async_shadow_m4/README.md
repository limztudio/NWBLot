# Async-shadow M4 target-hardware harness

This harness makes the M4 rollout decision repeatable on the selected backend when its public topology exposes a dedicated compute-only queue. It runs the same fixed-yaw stress scene twice:

- `nwb_async_shadow_m4_sync_benchmark` explicitly disables the default async-lane request.
- `nwb_async_shadow_m4_async_benchmark` retains the default async-compute lane request.

The native test logs exact primary Graphics/Compute queue IDs and device generations, along with public Compute capability and dedicated status. The runner accepts only a requested, distinct, current-generation, dedicated Compute-capable queue without Graphics capability; missing or shared transport returns the capability skip, while invalid identities or contradictory evidence fail. The synchronous request remains disabled even on hardware that needs a separate Compute transport.

The runner rejects an async result if it silently uses the Graphics queue route. For a real dedicated lane, it collects the renderer's timestamp envelopes, verifies that graph-owned `render.async_shadow` reports work, compares the `render.frame` Graphics critical path rather than summing queue work, captures a fixed-scene pixel A/B at the configured fixture update-count hold in each mode, and scans logs for ownership or selected-backend validation failures.

From the repository root, use the one-command launcher:

```powershell
python -m launcher async-shadow-m4
```

It configures the required test targets, builds both benchmarks and their cooked runtime assets, enables GPU validation, and writes a timestamped directory under `.cozter/out/ab-results/async-shadow-m4/`. The command returns `77` when the adapter has no distinct dedicated compute-only queue. To adjust a `run.py` setting, pass it after `--`, for example:

```powershell
python -m launcher async-shadow-m4 -- --measure-seconds 30
```

Pixel capture and timing run in separate processes. `--pixel-capture-frames` defaults to 96 and sets `NWB_M4_PIXEL_CAPTURE_FREEZE_FRAME`; the fixture counter advances once per world update. At that configured count, the capture process suspends new render submission while keeping its native event loop alive. The runner waits for `StressTestSmokeProject: M4 pixel capture ready after` and `render submission suspended` before settling and capturing the window. This update-count hold does not establish an exact number of accepted native presentations or guarantee the same temporal-history phase in both arms. The captured images must still pass the unchanged pixel-parity gate. The timing process runs separately without the hold. Adjust the fixture count when investigating capture behavior:

```powershell
python -m launcher async-shadow-m4 -- --pixel-capture-frames 128
```

A separate diagnostic launch of either benchmark may set `NWB_STRESS_FRAME_GRAPH_FILE` to an output path while using `NWB_M4_PIXEL_CAPTURE_FREEZE_FRAME`. Before publishing the held-frame marker, the benchmark writes readable JSON plus the encoded public telemetry stream at `<path>.nwbs`. This capture request rejects presentation-measurement mode or a missing pixel freeze; keep it unset for ordinary paired timing runs. The snapshot includes task assignments, packet boundaries, barrier counts, and accepted submission waits for the completed frame. Submission counters distinguish emitted timeline waits, same-queue elisions, duplicate merges, and inherited elisions covered by prior accepted waits. Public analysis edges do not export compiled state-seed IDs or extra compiler dependency IDs; the snapshot explicitly marks those limits and does not qualify performance.

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

Choose a configure preset for the target platform and architecture, and use the same build directory and configuration for both the launcher build and runner paths. The build command configures a cold directory automatically and cooks the target runtime assets without starting either benchmark. On Linux, run this from an active X11/Xwayland session. Each M4 fixture registers its existing selected `AvboitTimingRenderPass`, whose `shouldRenderUnfocused()` contract admits continuous rendering when another window has focus. The runner requires the real `AvboitTimingProbe: render unfocused 1` startup marker and freezes `NWB_STRESS_TEST_SPIN_ANGLE=0.6` for a repeatable capture. This registration does not enable the separate automatic presentation-measurement/quit mode. It returns exit code `77` when the target has no dedicated compute-only queue; that is an environment skip after selecting the Graphics queue route.

After wall-clock warmup, the runner snapshots the timing file's EOF and waits for two valid, fully newline-terminated interval headers beginning at or after that byte cutoff. It discards the first report because it may span warmup, then starts the requested measurement duration and parses from the second header's byte offset. The report records this boundary as `measurement_start_byte_offset`. Only publication windows admitted after that boundary contribute to the timing gate; GPU readback latency means the boundary is not a strict wall-clock fence on source-frame execution. Missing, malformed, or truncated timing output, process exit, and boundary timeout fail rather than admit warmup data.

The default gate needs at least six timing intervals, a median graph-owned `render.async_shadow` duration of at least `0.01 ms`, no more than `3%` median `render.frame` regression, no forbidden validation/ownership logs, and pixel differences inside the reported tolerance. Tune those thresholds explicitly on the command line for a device's known noise floor. `--report-only` always preserves the report while returning success for a failed rollout gate.

Artifacts include `async.timing.txt`, `sync.timing.txt`, captured logs and BMPs, plus `m4_report.json` and `m4_report.md`. A flat or negative performance result is useful data: retain the Graphics queue route and use the report to decide whether another job merits a separate async proposal.

## Recorded qualification: 2026-10-06

Three serial repeats of the frozen final Windows ARM64 / Clang Optimize binaries passed the unchanged M4 gate on the recorded Qualcomm Adreno X2-90 GPU. Each mode used four seconds of wall-clock warmup, post-warmup timing-boundary admission, and thirty seconds of measurement on the same 1280x900 fixed-yaw workload. The async arm retained a real distinct dedicated Compute queue and the `+3%` critical-path limit.

| Repeat | Sync `render.frame` ms | Async `render.frame` ms | Delta | Async shadow ms | Gate |
| --- | ---: | ---: | ---: | ---: | --- |
| 1 | 12.15025 | 11.37095 | -6.414% | 2.5729 | PASS |
| 2 | 11.35080 | 11.48525 | +1.184% | 2.6121 | PASS |
| 3 | 11.49145 | 11.51470 | +0.202% | 2.6048 | PASS |

Each arm supplied 60 required timing samples per repeat; all 60 async shadow samples were positive. Pixel max-absolute difference was 6 in each repeat, with mean-absolute differences 0.158335, 0.152716, and 0.149893. Forbidden-log and severity scans were clean. Five preserved original `ae06f1faa` binary repeats (`baseline_round2` through `baseline_round6`) failed the critical-path gate at +3.840% to +7.550%; their queue, shadow-work, pixel, and log gates passed.

These results qualify the recorded device and workload at the unchanged tolerance. The final repeats include both faster and slightly slower async results, so they do not establish a universal async speedup or a frame-rate claim for another GPU. Shader work, routing requirements, scene quality, capture tolerances, and rollout thresholds were preserved.

A separate version-10 diagnostic snapshot of one completed final frame recorded 103 tasks, 12 accepted submissions, two emitted timeline waits, and three inherited wait elisions. Its dedicated Compute queue retained 27 tasks in two packets; the compiled plan also contained one unexecuted conditional recovery packet. This is one frame's scheduling evidence, not a fixed packet-count contract or a performance measurement.

Evidence remains under ignored `__cmake/verification_async_performance_20261006/`: `qualification_repeats.json`, `baseline_binary_manifest.json`, `final_round1/` through `final_round3/` reports and raw artifacts, and `graph_final/frame_graph.json` with its encoded telemetry stream. The reports retain the admitted timing byte offsets. This record covers the M4 workload; other configuration and workflow qualification is recorded separately.

## Follow-up optimization qualification: 2026-10-06

After the additional command-recording and resource-range-planning changes, three fresh serial repeats of the final Windows ARM64 Optimize binaries passed the unchanged gate on the same Qualcomm Adreno X2-90. Each mode used four seconds of warmup, thirty seconds of admitted measurement, the same 1280x900 fixed-yaw scene, and the real dedicated Compute route. Each arm supplied 60 required timing samples; all 60 async shadow samples were positive.

| Repeat | Sync `render.frame` ms | Async `render.frame` ms | Delta | Async shadow ms | Gate |
| --- | ---: | ---: | ---: | ---: | --- |
| 1 | 11.48585 | 11.51210 | +0.229% | 2.6191 | PASS |
| 2 | 11.45020 | 11.01015 | -3.843% | 2.3924 | PASS |
| 3 | 11.53225 | 11.49730 | -0.303% | 2.5922 | PASS |

Pixel max-absolute differences were 7, 6, and 8, with mean-absolute differences 0.166839, 0.145786, and 0.159396. The +3% critical-path limit and 16/0.75 pixel limits were preserved. All runtime/validation log gates passed, and logs confirm GPU debug validation and Khronos layer activation. A fresh preserved `fb0cac07` baseline repeat also passed at +0.818%. These repeats qualify the existing async workload; they do not isolate a GPU or whole-frame speedup from the CPU optimizations.

The separate one-frame version-10 snapshot records 103 renderer tasks, 12 accepted packets, 27 Compute tasks in two packets, two emitted waits, and three inherited wait elisions; the additional compiled recovery packet remains unused. This is scheduling evidence, not a performance measurement. Fresh evidence is under `__cmake/verification_followup_performance_20261006/`, separate from the earlier qualification above. See the [optimization audit](../../../docs/project_optimization_audit.md#october-6-2026-command-recording-and-resource-state-planning) for paired CPU/scratch measurements, three-configuration native results, rendered workflow checks, and review limits.

## Cleanup qualification: 2026-10-07

The first attempt on pulled main plus the cleanup failed collection: its synchronous process retained only the initial accepted presentation and supplied no GPU scope rows. `NWB_RENDER_UNFOCUSED` had no renderer consumer. The fixtures now register their existing selected render pass for unfocused-render admission, and the runner requires its real startup marker. Production focus behavior, frozen yaw, timing-boundary admission, sample requirements, routes, and performance/pixel gates remain unchanged. The incomplete attempt is preserved in `m4_round1`; it is not used as performance evidence.

After both fixture arms rebuilt in `opt`, `dbg`, and `fin`, three serial Windows ARM64 / Clang Optimize repeats passed. Each arm used four seconds of warmup and thirty seconds of admitted measurement. Every repeat supplied 60 frame samples per arm and 60 positive async-shadow samples, with effective dedicated Compute queue index 1 distinct from Graphics index 0:

| Evidence round | Sync `render.frame` ms | Async `render.frame` ms | Delta | Async shadow ms | Gate |
| --- | ---: | ---: | ---: | ---: | --- |
| `m4_round2` | 13.11580 | 11.63690 | -11.275713% | 2.51205 | PASS |
| `m4_round3` | 12.94100 | 12.16390 | -6.004946% | 2.54845 | PASS |
| `m4_round4` | 13.11845 | 11.54250 | -12.013233% | 2.51520 | PASS |

Pixel max-absolute differences were 5, 4, and 6 against the unchanged limit 16; mean-absolute differences were 0.162475, 0.157948, and 0.159478 against limit 0.75. All forbidden-log arrays were empty. Manual review of both round-2 images confirmed the same coherent scene; it does not claim review of every repeat's capture.

These results qualify the current sync-versus-async workload on the recorded Windows ARM64 device at the unchanged `+3%` critical-path tolerance. They do not isolate a before/after benefit of the accumulation-shader optimization change or establish performance on another GPU. Source base `46e8cee23658438e7d4563590bc7079e84567495` plus the cleanup, complete reports with admitted byte offsets, launcher exits, and the retained incomplete attempt are documented under `__cmake/verification_hacky_cleanup_20261007/`. See the [cleanup audit](../../../docs/hacky_code_cleanup_audit.md) for the source review, build/native qualification, logger measurements, and rendered workflow limits.
