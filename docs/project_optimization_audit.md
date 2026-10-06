# Project-wide optimization audit

The September 27, 2026 pass extends the earlier graphics audit to project-owned engine foundations, ECS/content, assets and pipeline tools, diagnostics, platform/runtime code, build configuration and tests. Baseline `9c0c9279b`; final qualification commit `59793fc15`. Changes follow `.helper/standard.md` and retain owning-module boundaries.

## Accepted steps

| Commit | Scope | Result |
| --- | --- | --- |
| `99506099e` | Utilities and benchmark build | Correct native-character format constants, stderr streams and a missing logging include; fix the gather benchmark's stale dereference of a renderer reference. |
| `793821947` | Stale code | Remove an empty logger translation unit, unused CMake finder, write-only parser token state, dead FBX constants and a startup-only retained Testbed handle. |
| `db8596b39` | ECS frame work | Keep cutter fill counts in the existing group table; select the attachment basis before constructing one affine matrix. |
| `6637710d1` | Asset gathering | Deduplicate sorted exact input paths before reading/checksumming. Repeated mergeable input is appended once; a regression case verifies one merge and 6 bytes instead of two merges and 10 bytes. |
| `b00e10762` | Shader cook planning | Replace pairwise canonical dependency comparisons with a scratch lookup set; skip the second inherited-define merge when dependencies did not grow. Preserve first-seen order and authored/first-inherited precedence. |
| `c62bd8e96` | Name decoding | Allocate output only after a symbol resolves, and copy unchanged spans in batches. Long plain text, unknown hashes and invalid token boundaries retain their original storage. |
| `94e15ecc2` | Telemetry log encoding | Convert directly into the retained payload buffer; retain a temporary only when source text aliases that buffer. Preserve Unicode conversion, embedded NULs and byte layout. |
| `6ce717f96` | Memory telemetry | Read immutable owner identity during scope registration, avoiding discarded tracker walks and locks. Keep full statistics collection in the publication pass. |
| `59793fc15` | Test fixture corrections | Accept a zero-duration join for an already complete task, retaining event identity/count checks. Permit timing-only reflection scenes without framebuffer diagnostics while preserving actual capture requirements. |

These are proven reductions in operations/allocation or correctness/cleanup changes. The stress measurements below qualify the combined renderer; they do not isolate a frame-rate improvement attributable to each CPU or tooling change. Rendering presets and effect budgets were not changed in this pass.

## Validation

The complete Windows ARM64 optimized build passed. The full 104-entry CTest run initially produced 100 passes, two failures and two capability-dependent skips. Both failures were corrected in `59793fc15`; the affected CPU task suite and three reflection checks then passed (4/4). The aggregate qualification is **102 passed and two skipped**, combining the full run with those targeted reruns, rather than claiming a second complete all-green invocation.

The two skips are expected capability selections on this host:

- `nwb_transparent_multi_sw_capture_smoke`: the natural hardware shadow route is selected on RayQuery-capable hardware. The forced hardware-RT-disabled tests below run independently.
- `nwb_csg_visible_capture_smoke`: Meshlets are unavailable, so the natural compute-emulation route is selected. Its compute-emulation counterpart passed.

All eight forced hardware-RT-disabled GPU-validation smoke cases passed: overlap, early/late CSG, caustics, optics, skinning, stress resize and GI. These runs use `--disable-hardware-ray-tracing --gpudbg` and verify that RayQuery, ray-tracing pipeline, acceleration-structure and acceleration-structure descriptor/layout capabilities are disabled. The full suite also exercised reflection/refraction, duplicate transparent objects, optical feedback, gallery captures, crash handling and RGD decoding.

The corrected task profiling case passed **100 consecutive repetitions**. Queue-delay and execution events still require positive durations for their deliberately delayed work; an already completed join may legitimately measure zero. Reflection temporal omission proof and controlled capture checks both passed after the fixture correction.

Fifteen new regression cases cover repeated merge inputs, dependency order/defines/checksums, name decoding and allocation counts, telemetry Unicode/alias/buffer reuse, and arena identity traversal/lifetime. Existing concurrency, ECS transform/CSG, parser, CLI, conversion and crash/logger tests remain part of qualification. All 26 changed C++ source/header files passed CRLF, exact EOF/separator and unnamed-namespace checks.

Memory telemetry intentionally has one accepted heap sample per published frame. The removed discarded setup sample could previously advance the sampled heap peak; exact allocation accounting and the full publication snapshot are retained.

## Stress qualification

Both fresh qualification runs exceeded the strict **greater than 60 FPS average** target on the Snapdragon X2 Elite Extreme X2E94100, using the Qualcomm Adreno X2-90 GPU.

| Route | Average FPS | Average frame time | Accepted presentations | Measured duration | Lowest approximately 0.5-second interval FPS |
| --- | ---: | ---: | ---: | ---: | ---: |
| Hardware RT | 69.93 | 14.30 ms | 2,098 | 30.001641 s | 66.54 |
| Hardware RT disabled | 61.98 | 16.13 ms | 1,860 | 30.0073084 s | 60.21 |

The lowest interval value is not the individual worst-frame rate. Software has limited headroom; these results qualify this scene and its accepted presets, not every camera, workload or machine. Both runs passed capture validation, runtime identity checks and performance qualification. Software capture reuse was explicitly verified. Optical correctness is covered by separate smoke cases; detailed optical counters are not collected in the timing runs.

Run the tracked stress harness with the existing accepted 20-body settings: 10 opaque plus 10 transparent, animation enabled, 1280×900, SSR steps 96, transparent shadow sampling `temporal_one`, software shadow backend `automatic`, budget 256 MiB, directional/point resolution 512/256, fitted-volume coverage and compact-cross5 blocker search. Hardware uses caustic photon divisor 2 and every-frame capture. The approved software approximation uses divisor 4, capture reuse for one frame, and `--disable-hardware-ray-tracing`. Both retain the requested transparency, reflection, refraction, caustics, shadows and surfel GI workload. Each fresh run has five seconds of warmup and at least 30 actual seconds of accepted native presentations; CPU and reflection diagnostics are disabled; normal harness GPU timing collection remains enabled.

The local runner and raw identity/timing evidence are under `__artifacts/project_audit_20260927/`: `final_hw/result.json`, `final_sw/result.json`, `final_build.log`, `final_tests.log`, `final_ctest_detail.log`, `fixture_tests.log`, `profile_repeat_tests.log` and `regression_case_evidence.json`. These generated artifacts remain local; the results and reproduction settings are recorded here in tracked documentation. Reproduction uses `tests/smoke/stress_timing_smoke.py`; the local `run_stress.py` records its exact accepted arguments.

## Coverage and stop point

- Foundations: allocator ownership/accounting/scratch reuse; global container, string/name, process/I/O contracts; CPU task chunking, wake/wait and profiling paths; GPU task work from the preceding graphics pass.
- ECS/content: world/query/scheduler/message storage, scene and pose/palette updates, model attachments, renderer bindings, mesh providers, CSG grouping/topology, UI lifetimes and Testbed startup/input/shutdown.
- Assets/tools: asynchronous asset ownership, filesystem I/O/staging/rollback, metadata parsing, cook plans/cache integrity, material/shader hooks, mesh/model/texture payloads, gathering, conversion utilities and pipeline CLIs.
- Runtime/platform: frame/input loops, logging and asynchronous ownership, telemetry/perf, crash transport/retention, loader/launcher, configuration and project-facing vendor integration.
- Tests/build: the complete configured Windows ARM64 optimized build and registered tests, plus dedicated hardware/software stress qualification.

Independent domain reviews and a final combined diff review found no further demonstrated small, safe change within the inspected paths. Public APIs are not classified as stale solely because this repository has few callers. Vendor implementations were not rewritten or upgraded. Linux/other OS paths received source review, not execution on this Windows host.

Larger candidates remain profiling work: CSG cap topology traversal needs representative workloads and ordering/failure qualification; removing X11's synchronous window query requires Linux resize/map/visibility testing; asynchronous logger copies establish ownership and need an explicit lifetime redesign before removal. No speculative cache or behavior change was added to claim that every possible optimization has been exhausted.

## October 5, 2026 — repeated CPU work and transient material storage

This pass starts from `bb3e1bd62ad22eec8c58c7f9a49fcfcca3b53513`. Repository-wide inventory and pattern searches were followed by focused ownership, lifetime, ordering, allocation, and call-site review across foundations, graphics, ECS/rendering, UI, platform code, assets, tooling, and launch/build paths. The inventory records 2,218 tracked first-party source, shader, script, configuration, and asset paths. This is broad triage with focused semantic review, rather than a claim that every function was read line by line. Vendor code was excluded from optimization edits.

Four changes were accepted:

- Command-buffer resource retention combines owning, typed, and pending buffer-state membership checks into one lookup on the existing indexed path. The small-list path, first-seen ordering, reference ownership, promotion threshold, and retained capacity stay intact.
- Descriptor retirement advances one completed heap-use prefix for the locked completion snapshot. Increasing admission IDs and order-preserving compaction allow later retired slots to reuse this work. Queue queries, unknown-queue handling, deferred release, and final heap-use compaction retain their contracts.
- Material typed-byte deduplication probes with a borrowed span and hash. Hits avoid constructing an owning scratch key; misses still retain owned bytes. Copying the key before upload-vector growth also preserves source spans that alias that vector. Hash collisions are resolved by length and exact byte comparison.
- UI root collection retains roots at each existing native boundary, but sorts them only immediately before painting. The ascending paint-order/entity-ID comparator and popup/modal routing remain unchanged. Input-only collection avoids an unnecessary sort without adding storage.

### CPU measurements and tradeoffs

Measurements use Windows ARM64 Opt binaries on this host, with serial alternating ABBA/BAAB process rounds and no concurrent native builds or tests. The original and measured candidate executables, every sample XML, and the unchanged benchmark body are retained. Values below are medians across 50 samples per side for resource references and 30 per side for material lookup. The project steady-clock timer measures elapsed time around these CPU operations; this is not a process CPU-cycle measurement. These results do not attribute a whole-frame FPS or process RSS improvement.

| Workload | Measured operations | Before elapsed time | After elapsed time | Elapsed time change |
| --- | --- | ---: | ---: | ---: |
| Large repeated resource recording | 1,024 buffers, 256 textures, 8 repetitions | 227.95 µs | 137.40 µs | 39.72% lower |
| Small repeated resource recording | 4 buffers, 1 texture, 256 repetitions | 13.80 µs | 14.10 µs | 2.17% higher |
| Repeated 32-byte material value | 262,144 lookup hits | 9.547 ms | 6.584 ms | 31.03% lower |
| Repeated 512-byte material value | 65,536 lookup hits | 30.360 ms | 30.033 ms | 1.08% lower |
| Repeated 4,096-byte material value | 16,384 lookup hits | 59.781 ms | 58.890 ms | 1.49% lower |
| Unique 32-byte material values | 4,096 misses | 0.223 ms | 0.253 ms | 13.61% higher |
| Unique 512-byte material values | 4,096 misses | 1.981 ms | 2.079 ms | 4.94% higher |
| Unique 4,096-byte material values | 4,096 misses | 15.315 ms | 15.087 ms | 1.49% lower |

Material measurements exclude setup, reserve, and warmup. Unique workloads contain 256 distinct values in each of 16 fresh maps. Repeated workloads seed one owned value before timing. The small resource difference is 0.30 µs across the entire 256-repetition batch. The approximately 1% material differences are small relative to sample variability; the clear benefit is repeated small values and avoiding transient allocation/copy work. All-unique small-value CPU cost remains a tradeoff, so this is not a universal speedup.

Repeated material probes go from one allocation/free pair per hit to zero. Scratch peak falls by the value size: 32, 512, or 4,096 bytes in these fixtures. Reserved scratch falls from 3,072 to 2,048 bytes for repeated 512-byte values and from 17,408 to 9,216 bytes for repeated 4,096-byte values; the 32-byte fixture retains its 2,048-byte reservation. These are local arena metrics. The original temporary keys were promptly reclaimed, so the saving is not proportional to the total hit count. Owned keys and upload storage remain; unique-value allocation and memory metrics are unchanged. Resource-reference peak and retained memory are also unchanged.

The descriptor change is qualified with an independent logical model, rather than a native timing claim: 1,382,137 exhaustive cases, 20,000 seeded randomized cases, boundary scenarios, and 96 operation-count controls preserve eligibility, freeing order, resource release, and heap-use removal. With 1,024 completed uses and 256 retired slots, retirement completion checks fall from 262,144 to 1,024. Final compaction still performs its own 1,024 checks. The UI change removes one root sort per input-only collection; no isolated UI timing result is claimed.

### Qualification and reproduction

The selected 40-target Windows ARM64 Opt build passed through the launcher. The final selected runtime/integration gate passed all 74 entries in one serial run, with zero skips. Three material regressions exercise allocation-free repeated hits, equal-hash/equal-length distinct bytes with retained ownership, and aliased upload input across growth and mutation. The disabled benchmarks are explicit performance workloads, outside ordinary unit correctness coverage.

The runtime gate admitted 185 fresh captures and 16 complete application logs, with no unexpected warning, error, assertion, crash, or Vulkan validation diagnostic. Fifteen of those application launches activated GPU validation. An additional Opt Testbed window-capture launch with GPU validation also passed, including actual validation-layer/debug-messenger activation and normal shutdown. Image review covers the 27 existing baseline fixtures, the 158 popup/nested-popup checkpoints in default and alternate skins, and the additional Testbed validation capture. Of the 27 baseline frames, 24 are RGB-identical; Testbed scene deltas outside lower window corners are at most six channel levels, and the two resize-frame differences are confined to lower corners. UI regions, geometry, clipping, skin changes, popup chronology, and all four direct/replay framebuffer pairs remain correct; the latter pairs are RGB-identical.

All eight changed C++ files pass UTF-8 without BOM, CRLF, separator, exact EOF, and unnamed-namespace checks. A constant used only by a Final-only skeleton regression is localized to that branch to remove an Opt unused-variable warning; this leaves the measured benchmark body and production helper unchanged. After this cleanup, the affected graphics target rebuilt without compiler diagnostics and its suite passed again in both Opt and Final. The 74-entry Opt run and these targeted reruns are separate qualifications; no full Final runtime gate is claimed.

Build the surviving material benchmark and runtime targets through the launcher:

```powershell
python -m launcher build nwb_ecs_graphics_tests nwb_ui_layer_smoke testbed --arch arm64 --config opt --configure always --jobs 6
```

Run `MaterialTypedDedupBenchmark.*` in `ecs_graphics_tests.exe` with `--gtest_also_run_disabled_tests` and `--gtest_filter=MaterialTypedDedupBenchmark.*`. Compare original and candidate binaries in alternating serial rounds; do not time them alongside a build or application smoke. The former private command-buffer resource-reference benchmark has been removed; its paired results above remain historical decision evidence. Local evidence is under `__cmake/verification_optimization/`, including paired samples, model results, source inventories/reviews, build logs, CTest XML/full output, immutable runtime snapshots, and visual review reports. Other platforms received source review; native execution and CPU results in this pass apply to this Windows ARM64 host.

### Remaining opportunities

Further optimization should start with representative profiles: reuse the command initial-state indices that are already sorted, gather caustic typed data directly rather than copying intermediate blocks, reuse one asset-volume compaction buffer, and measure UI root-membership indexing against its retained-memory cost. Source review also found potential scheduler, telemetry, diagnostics, container, and X11 text-input work, but those changes need workload, concurrency, lifetime, or platform evidence before implementation. No speculative cache or ownership shortcut was introduced to exhaust the list.

## October 6, 2026: command recording and resource-state planning

Two bounded changes in `core/task/gpu/compiler_resource_ranges.cpp` were accepted. First-use discovery subtracts the first earlier same-resource use directly into an empty working vector, so complete coverage needs no allocation. Terminal-state discovery omits a covered bound when subtraction already leaves no uncovered fragment. Both changes preserve traversal and fragment order, validate the first selected range before stopping, and retain the existing clear/reserve/copy steps. No persistent cache, resource field, task hook, provider interface, or retained storage was added. The existing `GpuTaskUseIndex.CompleteCoverageStopsBeforeLaterInvalidRangeButSelectedInvalidPrefixStillFails` regression distinguishes legitimate early termination from skipping a malformed selected prefix.

Two provider-private changes remove repeated command-recording work without adding storage. Capability admission reuses its immediately preceding successful scope validation instead of repeating the native lease and exact-queue proof. Initial public authorization, debug attempted-capability accounting, required-mask checks, rejection logging, and failure invalidation remain. Optimized ARM64 disassembly confirms the scope helper still contains its lease call and capability admission still calls that helper once; the second lease call is gone. The capability function decreases from 155 to 131 decoded instructions (620 to 524 instruction bytes, including cold diagnostics). This is an object-code result, not total executable size or an isolated frame-time speedup.

Resource-state subset filtering skips texture or buffer families absent from the selection. Self-alias filtering compacts directly because it cannot grow the vectors; foreign-source replacement still completes all exact reservations before clearing published state. Invalid-input admission, state order, generation, handles, ranges, ownership, and logical snapshot preservation on allocation failure remain. No backend-specific fixture, production hook, native test backend, or per-resource field was added.

CPU comparison used Windows ARM64 Opt executables in 48 serial processes ordered ABBA/BAAB, with 24 samples per side and all nine workloads passing in every process. Graph setup is outside measurement. Each new opt-in workload warms persistent analysis/compiled output once, then records 64 compiles with a fresh scratch arena per compile. Workloads cover serial whole and half-buffer overwrites, repeated same-buffer uses inside one task, and disjoint writers followed by one covering reader. Their task, terminal-export, barrier-range, and ordering assertions check compiled results. They use the existing neutral `GpuTaskGraphCompiler::compile` entry defined in `compiler_internal.h`, as existing compiler workloads do; that header is an internal compiler entry, not a public include. The cases use neutral metadata and existing public compiled views, without provider types, native resources, private planning helpers, a fake backend, or new production hooks.

The benchmark body was frozen before production edits, SHA-256 `b7ed79cfd0fdda6e8d8a034dde4e6d6dae8cdb1278587de89069c7b2973afa7a`. The preserved baseline compiler implements `fb0cac07d50e691e5ab345008cb7bd4a3508c50c`; its executable already links the two provider changes, which these metadata-only CPU workloads do not exercise. It is therefore a frozen original-compiler baseline, not a claim that the whole executable is an untouched checkout of that commit. Baseline executable SHA-256 is `ede0f6d14a841e91b28eca216b11b0c0ce9100248b64218eebd9acec179807e4`; accepted candidate executable is `e4733fbcb990c044afbe20b705cd2b85629d7c9fbfb7cec25bb195271c5db47d`.

Values below are per-compile medians derived from `cpu_paired_summary_v2.json`: each batch median divided by its fixed repetition count. Times are microseconds, rounded to three decimal places; exact decimal values are retained in `gpu_range_planning_per_compile_medians.json`. External elapsed time brackets compilation; compiler-total and resource-planning time use the existing compile statistics. These are elapsed clock durations of CPU work, not OS process CPU time, cycle counts, process RSS, or a whole-frame FPS claim.

| Workload | Planning before → after (µs) | Compiler total before → after (µs) | External elapsed before → after (µs) | External elapsed change |
| --- | ---: | ---: | ---: | ---: |
| Whole overwrite, 128 uses | 11.362 → 11.155 | 47.387 → 47.306 | 47.470 → 47.393 | -0.16% |
| Whole overwrite, 512 uses | 43.808 → 42.516 | 194.674 → 191.213 | 194.774 → 191.295 | -1.79% |
| Partial overwrite, 128 uses | 16.425 → 16.146 | 63.403 → 63.112 | 63.488 → 63.198 | -0.46% |
| Partial overwrite, 512 uses | 65.408 → 63.944 | 263.933 → 266.104 | 264.039 → 266.197 | +0.82% |
| Internal whole use, 128 uses | 5.353 → 4.466 | 10.968 → 10.055 | 11.055 → 10.135 | -8.32% |
| Internal whole use, 512 uses | 21.145 → 17.127 | 40.764 → 35.852 | 40.852 → 35.939 | -12.03% |
| Fragmented fan-in, 128 fragments | 43.129 → 41.731 | 109.806 → 108.874 | 109.879 → 108.955 | -0.84% |
| Fragmented fan-in, 512 fragments | 525.898 → 522.197 | 1142.968 → 1136.737 | 1143.101 → 1136.841 | -0.55% |
| Pending epilogue control, 1,024 tasks | 128.450 → 128.006 | Not reported | 387.569 → 380.044 | -1.94% |

The clear timing improvement is repeated whole-buffer use inside one task: planning falls 16.58% for 128 uses and 19.00% for 512 uses; external elapsed falls 8.32% and 12.03%. Other timing differences are small relative to observed variability and controls, and do not establish individual speedups. In particular, partial-overwrite 512-use external elapsed is 0.82% higher despite lower planning time. The unchanged pending-epilogue control has planning −0.35% and elapsed −1.94%, so a small downward shift alone is insufficient evidence of improvement.

Scratch values are arena accounting, in bytes and allocation calls. Each new case reports the maximum per-compile value across its 64 fresh arenas; those values are stable across the paired samples. Retained means `usedBytes` when compilation returns, before arena destruction. It describes temporary operation storage, not persistent resource or application memory.

| Workload | Scratch peak before → after (bytes) | Scratch used at return before → after (bytes) | Allocation count before → after |
| --- | ---: | ---: | ---: |
| Whole overwrite, 128 uses | 78,816 → 69,728 | 68,576 → 59,488 | 59 → 47 |
| Whole overwrite, 512 uses | 324,256 → 278,624 | 283,296 → 237,664 | 63 → 47 |
| Partial overwrite, 128 uses | 114,784 → 105,728 | 104,544 → 95,488 | 442 → 431 |
| Partial overwrite, 512 uses | 467,744 → 422,144 | 426,784 → 381,184 | 1,598 → 1,583 |
| Internal whole use, 128 uses | 27,272 → 23,616 | 22,760 → 13,672 | 177 → 38 |
| Internal whole use, 512 uses | 121,992 → 94,272 | 99,240 → 53,608 | 565 → 38 |
| Fragmented fan-in, 128 fragments | 133,480 → 126,568 | 128,968 → 119,880 | 454 → 442 |
| Fragmented fan-in, 512 fragments | 559,496 → 525,160 | 536,744 → 491,112 | 1,614 → 1,598 |

The existing 1,024-task pending-epilogue control uses one scratch arena through warmup and eight compiles, rather than fresh arenas. Its arena peak remains 4,280,320 bytes in both binaries; it exposes no used-at-return or allocation-count metric. The lifetime distinction prevents treating its peak as directly comparable to the eight new per-compile cases.

Three working/remainder vector swaps were tested in the first candidate and rejected. In the same 24-sample-per-side comparison, fragmented 512-use planning rose 10.69% and external elapsed rose 4.99%, despite a 4.67% reduction in scratch peak. Restoring all three original clear/reserve/copy loops removes that demonstrated CPU regression in the second comparison. Both vectors share the arena and propagating allocator, so swaps did not corrupt ownership, but out-of-order scratch deallocation may retain an older buffer until arena destruction. Alternating vector capacities/destruction order and extra bookkeeping are plausible costs when a fragment step saves only one small bound copy; the measurements establish the regression, not a unique cause. The first candidate source and measurements remain decision evidence.

Build and opt into the neutral compiler workloads through the launcher and normal test binary:

```powershell
python -m launcher build nwb_gpu_task_tests --arch arm64 --config opt --configure always --jobs 6
& "__exec/windows/arm64/full/opt/gpu_task_tests.exe" --gtest_also_run_disabled_tests "--gtest_filter=GpuTaskGraphRangePlanning.*:GpuTaskGraphBufferRange.DISABLED_PendingEpilogueGroupingBenchmark1024Tasks" "--gtest_output=xml:range_planning.xml"
```

Compare preserved original and candidate binaries in alternating serial rounds, with builds and native smoke runs outside the timed interval. Local evidence is under `__cmake/verification_followup_performance_20261006/`, including `cpu_paired_samples_v2.json`, `cpu_paired_summary_v2.json`, per-process XML, binary/source manifests, and both compiler-candidate audits. The disabled cases are explicit opt-in performance workloads under `tests/`; the ordinary edge suite remains separate. Selected native runtime, diagnostic, async-performance, and visual gates qualify separate behavior and should be reported alongside these metadata-only CPU measurements.


After updating the source base to `0f6dc7c3678b9a0f87d39eb1c1199be8f88b6356`, the rebuilt Opt candidate (SHA-256 `c41e452b6294047c8ac47ee71669b9e513f4d979f3e5abc213c87cff43d991e9`) repeated the same paired protocol against the preserved original-compiler baseline: all nine unchanged workloads passed in 48 processes, with 24 samples per side. `upstream_format_token_review.json` records identical non-comment raw tokens in the three upstream-edited files. In `cpu_paired_summary_latest.json`, internal 128-use planning is 5.351 to 4.468 µs per compile (16.50% lower), and external elapsed is 11.056 to 10.295 µs (6.89% lower); internal 512-use planning is 20.752 to 17.228 µs (16.98% lower), and external elapsed is 39.968 to 37.048 µs (7.30% lower). Scratch peak, used-at-return, and allocation results reproduce the table exactly, including 121,992 to 94,272 bytes peak and 565 to 38 allocations for 512 internal uses. Fragmented 512-use external elapsed changes by +0.14%, while the pending-epilogue control changes by -0.71%; these and other small timing movements remain inconclusive. This replication retains the elapsed-time and arena-accounting limits above and does not replace the historical comparison table.

### Final build, runtime, and visual qualification

The retained production sources and frozen benchmark were qualified after integrating `0f6dc7c3678b9a0f87d39eb1c1199be8f88b6356`. Launcher `build all` completed in Windows ARM64 Debug, Optimize, and Final with no compiler diagnostics. All 28 selected native CTest entries passed in each configuration: 2,707 active GoogleTest cases in Debug, 2,707 in Optimize, and 2,712 in Final, with zero failures or skips. The opt-in CPU cases remain disabled during ordinary correctness runs. `qualified_latest_manifest.json` records source/binary hashes, build commands/results, and source hygiene; the native reports are `ctest_latest_dbg.json`, `ctest_latest_opt.json`, and `ctest_latest_fin.json`.

Three fresh serial M4 repeats used the final Optimize binaries with the same 1280x900 fixed-yaw workload, four seconds of wall-clock warmup, thirty seconds of admitted measurement per mode, and the unchanged +3% critical-path limit. Each arm supplied 60 required timing samples, all async shadow samples were positive, and the dedicated Compute route remained active.

| Repeat | Sync `render.frame` ms | Async `render.frame` ms | Delta | Async shadow ms | Gate |
| --- | ---: | ---: | ---: | ---: | --- |
| 1 | 11.48585 | 11.51210 | +0.229% | 2.6191 | PASS |
| 2 | 11.45020 | 11.01015 | -3.843% | 2.3924 | PASS |
| 3 | 11.53225 | 11.49730 | -0.303% | 2.5922 | PASS |

Pixel max-absolute differences were 7, 6, and 8; mean-absolute differences were 0.166839, 0.145786, and 0.159396, within the unchanged 16/0.75 limits. A fresh preserved `fb0cac07` baseline repeat also passed at +0.818%. These runs qualify continued async behavior on this adapter; they do not isolate a GPU or frame-rate gain from the CPU changes. The separate version-10 diagnostic snapshot records 103 renderer tasks, 12 accepted packets covering 102 accepted tasks, no rejected/failed/recovery submissions, and 27 Compute tasks in two packets. Its 53 planned waits comprise 39 same-queue elisions, nine merges, three inherited elisions, and two emitted waits. The thirteenth compiled packet is unused conditional recovery. This is one completed frame's public scheduling evidence, not a timing result or a reconstruction of unexported state-seed/dependency IDs.

All four Optimize rendered workflows passed without skips: Testbed startup/shutdown, UI raster direct/replay parity, 65 TextArea checkpoints, and software CSG analytic cut/uncut shadows. They produced 76 BMPs. All four raster pairs have identical entire BMP bytes across 2,035,200 pixels. The CSG oracle checks 26 regions; maximum mean channel error is 0.660603 bytes against a 12-byte tolerance. The final audit scans 25 complete application logs from these workflows, all three M4 repeats, and the diagnostic launch; it finds no unexpected warning, error, fatal, assertion, device-loss, or VUID diagnostic. Every log confirms GPU debug validation and actual Khronos validation-layer activation. Intentional negative-unit diagnostics are outside this application-log audit. Synchronization-validation settings were requested for the separate diagnostic launch; individual setting activation is not claimed beyond the verified layer markers.

Manual inspection covers 21 workflow originals, two final M4 originals, and one preserved baseline M4 original. Reviewed geometry, shadow content, clipping, skin states, multilingual text, selection, scroll positions, and resized layouts show no rendering regression. The harness checks all captures; manual inspection does not cover every image. Long labels in the existing TextArea fixture buttons crop within their fixed 112-pixel bounds, outside these optimization changes. Testbed has no paired baseline image comparison in this pass.

Evidence remains under ignored `__cmake/verification_followup_performance_20261006/`: `qualification_repeats.json`, `final_round1/` through `final_round3/`, `graph_final/`, `visual_opt.json`, `runtime_visual_audit.json`, `independent_raster_testbed_visual_review.json`, and `backend_record_visual_review.json`, alongside the paired CPU reports and immutable binaries. Native execution and measured results apply to this Windows ARM64 host; other platforms received source review. Renderer cache reuse and broader dependency pruning still need representative profiles and lifetime/concurrency evidence before implementation.
