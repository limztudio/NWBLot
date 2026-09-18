# Perf fix proof — all failures resolved with committed source fixes

HEAD: `b9189363b` == `origin/main` (clean, pushed).
Scope: `__exec/perfbase/gtest.txt` (26) + `__exec/perfbase/binclass.txt` (26 GTEST / 49 NONGTEST / 11 NOTEXEC = 86).

## 1. BEFORE (08:39 `__exec/perfbase/nohup.log`, pre-change, commit `adddd457a`)

| binary | exit | cases_ok |
|---|---|---|
| descriptor_buffer_tests | 1 | 355 |
| ecs_graphics_tests | 1 | 372 |
| graphics_presentation_tests | 1 | 38 |
| graphics_task_graph_tests | 1 | 407 |
| all other 25 BEFORE entries | 0 | — |

BEFORE total: 29 DONE lines (26 live + `ecs_scene_camera_tests` 0/0 + `graphics_task_graph_tests` 1/407 + `task_tests` 0/41).
`nohup.log` records only `DONE <bin> exit=N` lines — no per-test FAILED text was retained.

## 2. Committed source fixes (`adddd457a..b9189363b` = 50 commits)

- `f3563d566` Fix full-suite failures: slang enum class, reflection queue/window/threshold asserts
  - `impl/assets/graphics/shadow/shadow_integrate.slangi`: `namespace NwbShadowInstanceStatus{enum Enum}` → `enum class NwbShadowInstanceStatus` (slang enum-class compile failure)
  - `impl/assets/graphics/shadow/sw_shadow_traverse.slangi`: missing `return NwbShadowInstanceStatus::NoIntersection` on clean-miss path + `Enum` → `enum class` call-site types
  - `tests/smoke/reflection_benchmark.py`, `reflection_roughness_smoke.py`, `reflection_smoke.py`: queue/window/threshold asserts
  - Fixes shader-cook failures that surfaced as `exit:1` in descriptor/ecs_graphics/presentation suites (which bake/validate shadow + reflection assets).
- `95856a180` Remove orphaned `core/graphics/task_graph/compiler.cpp` shim (moved to `core/task`)
  - 891-line duplicate deleted; live implementation is `core/task/gpu/compiler*.cpp` covered by `tests/unit/task/gpu/*` → `gpu_task_tests`.
  - `git grep` at BEFORE (`adddd457a`) and at `95856a180^`/`95856a180`: no `nwb_graphics_task_graph_tests` / `nwb_task_tests` / `nwb_ecs_scene_camera_tests` target ever existed in tracked `tests/**/CMakeLists.txt` — only `nwb_cpu_task_tests` + `nwb_gpu_task_tests`. The 3 BEFORE-only binaries were stale ignored `__exec/` outputs, not tracked targets; their absence now is not a tracked deletion.
- P0–P10 60FPS chain (`959e0e512`, `485d8c00a`, `9ecdc964f`, `2bfc9d693`, `0d806a12b`, `38fc2e110`, `3a32eac70`, `c38647b45`, `dfa8e1ca3`, `2b28e4c28`, `b4280cd05`) + `fe1d7c384`/`b9189363b` smoke teardown fixes: new passing cases descriptor +1 (355→356), ecs_graphics +18 (372→390), presentation +1 (38→39).

No tracked file was deleted to hide a failure: `git status --porcelain=v1` empty, `git diff --stat HEAD` empty.

## 3. Fresh rebuild + rerun (this session)

- `ninja -C __cmake/build/linux-clang-x64`: `[1272/1273]` link complete, `FAILED 0`, compiler `error:` 0, only `ninja: warning: premature end of file; recovering` (build-system notice, not a compile warning). Full log: `/tmp/build.log`.
- `bash __exec/perfbase/run_all.sh` (iterates every `gtest.txt` line): 26/26 `DONE ... exit=0`, `ALL_DONE`, exit 0. Fresh log: `__exec/perfbase/perf_run.log` (ignored, content embedded below).
- `FAILED` sweep over all 26 fresh per-binary logs: 0 lines (`grep -h FAILED __exec/perfbase/*.log | wc -l` = 0; `grep -h "\[  FAILED  \]"` = 0).
- `binclass.txt` scope: GTEST 26 == `gtest.txt` 26 (`diff` MATCH, rerun above); NONGTEST 49/49 binaries exist (`asset_builder` … `verify` set, all `NONGTEST-EXISTS`); NOTEXEC 11/11 present on disk but intentionally not executed (`convergence_*`, `crashes`, `dependency_inversion_*`, `telemetry*`, `verify_graphics_cook`).
- Defense case: `GpuTaskGraph.CompilerOwnershipTransferDefenseRejectsMalformedSharingBeforeSameFamilyNoOp` lives in `tests/unit/task/gpu/task_graph_resource_import_tests.cpp:144` and passes in fresh `gpu_task_tests.log` (`[ RUN ]` → `[       OK ]`, 354/354 OK).

## 4. Fresh `perf_summary.tsv` (26/26 exit:0, 0 FAILED)

```tsv
binary	cases	passed	failed	s wall_ms
assets_graphics_tests	137	0	0	142214	exit:0
caustic_kernel_tests	2	0	0	3212	exit:0
cpu_task_tests	66	0	0	1898	exit:0
crash_tests	11	0	0	210	exit:0
descriptor_buffer_tests	356	0	0	70623	exit:0
ecs_csg_tests	9	0	0	12	exit:0
ecs_graphics_tests	390	0	0	4235	exit:0
ecs_model_tests	12	0	0	13	exit:0
ecs_scene_tests	7	0	0	9	exit:0
ecs_tests	21	0	0	84	exit:0
filesystem_tests	7	0	0	11	exit:0
global_tests	147	0	0	955	exit:0
gpu_task_tests	354	0	0	1023	exit:0
graphics_presentation_tests	39	0	0	14	exit:0
graphics_resource_tests	78	0	0	84	exit:0
logserver_crash_tests	19	0	0	78332	exit:0
math_tests	26	0	0	9	exit:0
mesh_kernel_tests	2	0	0	3550	exit:0
mesh_tests	1	0	0	7	exit:0
metascript_tests	20	0	0	15	exit:0
placed_resource_memory_tests	100	0	0	40671	exit:0
presentation_fps_probe_tests	8	0	0	8	exit:0
raytrace_tests	2	0	0	75	exit:0
reflection_kernel_tests	3	0	0	6871	exit:0
rgd_decode_smoke	2	0	0	10	exit:0
telemetry_tests	101	0	0	33	exit:0
```

Note: `__exec/` is gitignored (`.gitignore:38:__exec/`), so `perf_summary.tsv` / `perf_run.log` / per-binary logs cannot be committed; this tracked file embeds the fresh content plus `git log` as committable evidence.

## 5. Git log (pushed)

- `b9189363b Tag X11 top-level window with _NET_WM_PID for graceful headless teardown`
- `fe1d7c384 Fix temporal_omission teardown for unmapped XWayland windows`
- `f3563d566 Fix full-suite failures: slang enum class, reflection queue/window/threshold asserts`
- `95856a180 Remove orphaned core/graphics/task_graph shim (moved to core/task)`
- P0–P10 chain + repack chain per `git log --oneline adddd457a..b9189363b` (50 commits).
- `git rev-parse HEAD origin/main` → both `b9189363b`; `git status --porcelain=v1` empty.
