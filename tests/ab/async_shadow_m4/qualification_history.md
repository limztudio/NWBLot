# Async-shadow M4 past runs

This file keeps past runs and their measured numbers. It covers the listed
code, devices, and scenes. Current use is in [README.md](README.md).

## Runs on 2026-10-06

Three back-to-back runs of the fixed Windows ARM64 Clang Optimize builds
passed the M4 gate on the Qualcomm Adreno X2-90 GPU. Each mode used 4 seconds
of warm-up and 30 seconds of measure on the same 1280x900 fixed-yaw scene.
The async arm kept a real distinct dedicated Compute queue and the `+3%` path
limit.

| Repeat | Sync `render.frame` ms | Async `render.frame` ms | Delta | Async shadow ms | Gate |
| --- | ---: | ---: | ---: | ---: | --- |
| 1 | 12.15025 | 11.37095 | -6.414% | 2.5729 | PASS |
| 2 | 11.35080 | 11.48525 | +1.184% | 2.6121 | PASS |
| 3 | 11.49145 | 11.51470 | +0.202% | 2.6048 | PASS |

Each arm gave 60 timing samples per run. All 60 async shadow samples were
good. Pixel max gaps were 6 in each run. Mean gaps were 0.158335, 0.152716,
and 0.149893. Log scans were clean. Five kept `ae06f1faa` binary runs
(`baseline_round2` to `baseline_round6`) failed the path gate at +3.840% to
+7.550%. Their queue, shadow-work, pixel, and log gates passed.

These runs qualify the listed device and scene at the same limit. The final
runs show both faster and slightly slower async times. So they do not prove a
general async speed gain or a frame-rate claim for other GPUs. Shader work,
route needs, scene quality, capture limits, and rollout limits were kept.

One version-10 debug shot of one done final frame showed 103 tasks, 12
accepted submits, two real waits, and three passed-on waits. Its dedicated
Compute queue held 27 tasks in two packets. The built plan also held one idle
fix-up packet. This is one frame of plan proof, not a fixed packet rule or a
speed measure.

Proof stays under ignored `__cmake/verification_async_performance_20261006/`:
`qualification_repeats.json`, `baseline_binary_manifest.json`, `final_round1/`
to `final_round3/` reports and raw files, and `graph_final/frame_graph.json`
with its coded stream. The reports keep the used timing byte spots. This file
covers the M4 scene. Other setup checks are stored elsewhere.

## Follow-up runs on 2026-10-06

After more command-record and range-plan changes, three fresh back-to-back
runs of the final Windows ARM64 Optimize builds passed the same gate on the
same Qualcomm Adreno X2-90. Each mode used 4 seconds of warm-up and 30
seconds of measure on the same 1280x900 fixed-yaw scene and the real
dedicated Compute route. Each arm gave 60 timing samples. All 60 async shadow
samples were good.

| Repeat | Sync `render.frame` ms | Async `render.frame` ms | Delta | Async shadow ms | Gate |
| --- | ---: | ---: | ---: | ---: | --- |
| 1 | 11.48585 | 11.51210 | +0.229% | 2.6191 | PASS |
| 2 | 11.45020 | 11.01015 | -3.843% | 2.3924 | PASS |
| 3 | 11.53225 | 11.49730 | -0.303% | 2.5922 | PASS |

Pixel max gaps were 7, 6, and 8. Mean gaps were 0.166839, 0.145786, and
0.159396. The +3% path limit and 16/0.75 pixel limits were kept. All log
gates passed. Logs show GPU debug checks and Khronos layer use. A fresh kept
`fb0cac07` baseline run also passed at +0.818%. These runs qualify the async
workload. They do not split out a GPU or full-frame speed gain from the CPU
changes.

The separate one-frame version-10 shot shows 103 renderer tasks, 12 accepted
packets, 27 Compute tasks in two packets, two real waits, and three passed-on
waits. The extra built fix-up packet stays idle. This is plan proof, not a
speed measure. Fresh proof is under
`__cmake/verification_followup_performance_20261006/`, apart from the first
runs above.

## Cleanup runs on 2026-10-07

The first try on pulled main plus the cleanup failed to collect: its sync run
kept only the first shown frame and gave no GPU scope rows.
`NWB_RENDER_UNFOCUSED` had no renderer user. The fixtures now list their real
render pass for no-focus runs, and the runner needs its real start marker.
Focus rules, fixed yaw, timing gates, sample needs, routes, and speed/pixel
gates stay the same. The short try stays in `m4_round1`. It is not used as
speed proof.

After both fixture arms rebuilt in `opt`, `dbg`, and `fin`, three back-to-back
Windows ARM64 Clang Optimize runs passed. Each arm used 4 seconds of warm-up
and 30 seconds of measure. Each run gave 60 frame samples per arm and 60 good
async-shadow samples, with real dedicated Compute queue index 1 apart from
Graphics index 0:

| Proof round | Sync `render.frame` ms | Async `render.frame` ms | Delta | Async shadow ms | Gate |
| --- | ---: | ---: | ---: | ---: | --- |
| `m4_round2` | 13.11580 | 11.63690 | -11.275713% | 2.51205 | PASS |
| `m4_round3` | 12.94100 | 12.16390 | -6.004946% | 2.54845 | PASS |
| `m4_round4` | 13.11845 | 11.54250 | -12.013233% | 2.51520 | PASS |

Pixel max gaps were 5, 4, and 6 against limit 16. Mean gaps were 0.162475,
0.157948, and 0.159478 against limit 0.75. All bad-log lists were empty. A
manual look at both round-2 images showed the same full scene. It does not
claim a look at each run's capture.

These runs qualify the current sync-vs-async scene on the listed Windows ARM64
device at the same `+3%` path limit. They do not split out a gain from the
sum-shader change or prove speed on other GPUs. Source base
`46e8cee23658438e7d4563590bc7079e84567495` plus the cleanup, full reports
with used byte spots, launcher exits, and the kept short try are stored under
`__cmake/verification_hacky_cleanup_20261007/`.
