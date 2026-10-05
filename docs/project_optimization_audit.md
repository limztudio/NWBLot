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

Build the affected native targets through the launcher:

```powershell
python -m launcher build nwb_ecs_graphics_tests nwb_graphics_resource_tests nwb_ui_layer_smoke testbed --arch arm64 --config opt --configure always --jobs 6
```

Run `CommandBufferResourceReferences.DISABLED_Benchmark*` in `graphics_resource_tests.exe` and `MaterialTypedDedupBenchmark.*` in `ecs_graphics_tests.exe` with `--gtest_also_run_disabled_tests` and `--gtest_filter=...`. Compare original and candidate binaries in alternating serial rounds; do not time them alongside a build or application smoke. Local evidence is under `__cmake/verification_optimization/`, including paired samples, model results, source inventories/reviews, build logs, CTest XML/full output, immutable runtime snapshots, and visual review reports. Other platforms received source review; native execution and CPU results in this pass apply to this Windows ARM64 host.

### Remaining opportunities

Further optimization should start with representative profiles: reuse the command initial-state indices that are already sorted, gather caustic typed data directly rather than copying intermediate blocks, reuse one asset-volume compaction buffer, and measure UI root-membership indexing against its retained-memory cost. Source review also found potential scheduler, telemetry, diagnostics, container, and X11 text-input work, but those changes need workload, concurrency, lifetime, or platform evidence before implementation. No speculative cache or ownership shortcut was introduced to exhaust the list.
