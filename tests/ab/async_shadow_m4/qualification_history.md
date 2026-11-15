# Async-shadow M4 qualification history

This report preserves recorded runs and measured results. It describes the listed revisions, devices, and workloads; current harness behavior and invocation are documented in [README.md](README.md).

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

The separate one-frame version-10 snapshot records 103 renderer tasks, 12 accepted packets, 27 Compute tasks in two packets, two emitted waits, and three inherited wait elisions; the additional compiled recovery packet remains unused. This is scheduling evidence, not a performance measurement. Fresh evidence is under `__cmake/verification_followup_performance_20261006/`, separate from the earlier qualification above.

## Cleanup qualification: 2026-10-07

The first attempt on pulled main plus the cleanup failed collection: its synchronous process retained only the initial accepted presentation and supplied no GPU scope rows. `NWB_RENDER_UNFOCUSED` had no renderer consumer. The fixtures now register their existing selected render pass for unfocused-render admission, and the runner requires its real startup marker. Production focus behavior, frozen yaw, timing-boundary admission, sample requirements, routes, and performance/pixel gates remain unchanged. The incomplete attempt is preserved in `m4_round1`; it is not used as performance evidence.

After both fixture arms rebuilt in `opt`, `dbg`, and `fin`, three serial Windows ARM64 / Clang Optimize repeats passed. Each arm used four seconds of warmup and thirty seconds of admitted measurement. Every repeat supplied 60 frame samples per arm and 60 positive async-shadow samples, with effective dedicated Compute queue index 1 distinct from Graphics index 0:

| Evidence round | Sync `render.frame` ms | Async `render.frame` ms | Delta | Async shadow ms | Gate |
| --- | ---: | ---: | ---: | ---: | --- |
| `m4_round2` | 13.11580 | 11.63690 | -11.275713% | 2.51205 | PASS |
| `m4_round3` | 12.94100 | 12.16390 | -6.004946% | 2.54845 | PASS |
| `m4_round4` | 13.11845 | 11.54250 | -12.013233% | 2.51520 | PASS |

Pixel max-absolute differences were 5, 4, and 6 against the unchanged limit 16; mean-absolute differences were 0.162475, 0.157948, and 0.159478 against limit 0.75. All forbidden-log arrays were empty. Manual review of both round-2 images confirmed the same coherent scene; it does not claim review of every repeat's capture.

These results qualify the current sync-versus-async workload on the recorded Windows ARM64 device at the unchanged `+3%` critical-path tolerance. They do not isolate a before/after benefit of the accumulation-shader optimization change or establish performance on another GPU. Source base `46e8cee23658438e7d4563590bc7079e84567495` plus the cleanup, complete reports with admitted byte offsets, launcher exits, and the retained incomplete attempt are documented under `__cmake/verification_hacky_cleanup_20261007/`.
