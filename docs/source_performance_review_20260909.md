# Source performance review — 2026-09-09

Baseline: `fa4596dd` on `main`. The audit inventoried 1,075 first-party C++, shader, and Python files across `global`, `core`, `impl`, `logger`, `loader`, `pipeline`, `utilities`, and `CoolStuff`. Detailed inspection followed runtime entry points, repeated loops, allocations, dependency inference, asset processing, and existing profiling coverage. This is a broad source audit with targeted measurements, not a claim that every execution path was profiled. Earlier work in [source_performance_review.md](source_performance_review.md) was checked to avoid repeating optimizations already present.

Three changes were retained. They preserve output and validation contracts, use existing allocator ownership, retain CRLF source formatting, and introduce no exception handlers. Implementation commits: `d0e8a2d9` (camera), `b633d72d` (metadata), and `c2c6348f` (graph analysis).

## Measurement method

Measurements used this host's AMD BC-250 CPU, Clang 22.1.8, Linux x64 `opt` (`-O2`, existing AVX2/FMA settings). Seven alternating before/after process pairs ran sequentially with CPU affinity fixed to logical CPU 2 and no compilation in progress. Tables show median time per public operation; setup is excluded. Small differences near one percent are treated as noise.

Camera processes report the median of seven measured batches after a warmup batch; each graph and metadata process warms the operation before its measured batch. Success and output checks remain in the fixtures. These are CPU operation measurements, not GPU timings or frame-rate gains.

Camera and metadata baselines were saved before their production edits. The graph pair uses the same corrected benchmark fixture and link dependencies: the baseline links the original `fa4596dd` compiler-analysis object before the archive; the candidate uses the optimized archive member. This avoids source-tree swapping. The initial graph fixture incorrectly expected out-of-order scratch frees to reclaim all bytes before arena destruction; that assertion was corrected, and all reported graph pairs were rerun with the corrected fixture on both implementations.

Raw XML, logs, baseline executables, compile/link commands, and `benchmark-results.json` are local, ignored artifacts under `.cozter/out/performance-review-20260909/`.

## GPU task graph hazard analysis

`core/task/gpu/compiler_analysis.cpp` previously scanned all historical writers/readers for every use, including other resources and inactive overwritten records. It now keeps insertion-ordered live lists indexed by validated resource ID. Fully covered accesses are unlinked; partial overlaps remain. Writer/read ordering, same-task suppression, generation checks, semantic topological order, and edge publication are preserved.

The improvement removes unrelated and retired-access scans. It does not make every range-overlap workload linear: numerous simultaneously live overlapping ranges can still require pairwise work.

| Complete public analysis workload | Before | After | Ratio |
| --- | ---: | ---: | ---: |
| 4,096 resources, 8,192 tasks | 66.171 ms | 2.132 ms | 31.04× |
| One resource, 4,096 overwrites | 11.766 ms | 0.912 ms | 12.91× |
| 64 resources, 128 tasks | 41.989 µs | 25.196 µs | 1.67× |
| Four resources, 16 tasks | 4.594 µs | 4.544 µs | Essentially flat |
| 4,096 declared resources, four used, 16 tasks | 57.953 µs | 59.691 µs | 3.0% slower |

The sparse case is a deliberate tradeoff: it adds 1.737 µs, 130,560 bytes of peak scratch use, and 257,536 bytes of scratch backing in this fixture. List endpoints require 32 bytes per declared resource when physical uses exist. Smaller access nodes offset that storage for dense graphs: the large independent-resource case reduces peak scratch use from 2,002,960 to 1,871,888 bytes and backing from 3,219,456 to 2,170,880 bytes. Graphs with no physical uses allocate no endpoint table. Separate untimed owner-telemetry checks verify used and reserved bytes return to their prior values when the measurement arena is destroyed.

Three regressions check exact edge ordering through middle/tail/all retirements, partial ranges, surviving readers/writers, same-task mixed access, and graph reset. The full GPU task suite also exercises texture ranges, external dependencies, cycles, queue policy, and resource versions.

## Scene camera selection

`impl/ecs_scene/camera.cpp` now looks up a valid active entity's transform and camera directly. If that candidate is absent or invalid, fallback iteration stops at the first valid camera. It still resolves the first active-selector component, preserves sparse-view fallback order, and reads current transforms/projection fields each call. No cache or allocation was added.

| Public camera resolution workload | Before | After |
| --- | ---: | ---: |
| Single active camera | 75.50 ns | 47.81 ns |
| 64 cameras, active last | 2.914 µs | 47.54 ns |
| 1,024 cameras, active last | 45.914 µs | 47.54 ns |
| 1,024 cameras, no active selector | 45.746 µs | 50.98 ns |

The many-camera cases measure scaling, not typical Testbed frame cost. All 14 process results verified entity identity, projection aspect, a checksum, and unchanged heap-allocation counts. Three additional regressions cover generation reuse, missing and re-added components, selector precedence, live projection changes, and fallback ordering after component removal; existing tests cover invalid transforms and projections.

## Volume metadata serialization

`core/filesystem/volume_metadata.cpp` snapshots compact disk index records, sorts their complete hash identities, and copies them directly into the final metadata buffer. This removes the intermediate serialized-index vector and its copy. Every path still publishes its hash once, and the exact file format, padding, error handling, and write boundary are preserved.

| Public flush workload | Before | After |
| --- | ---: | ---: |
| One file | 10.103 µs | 10.010 µs |
| 4,096 files | 1.110 ms | 1.063 ms |

The large case reduces median time by about 4.2%; singleton timing is effectively unchanged. At 4,096 records the eliminated index staging payload is 327,680 bytes (320 KiB). These fixtures use empty file contents to isolate metadata scaling while retaining public flush bookkeeping and file I/O. One full-file regression checks all hash lanes, deterministic record ordering, payload bytes, zero-length files, metadata padding after removals, and read-only remount.

## Other reviewed paths

Existing smallest-pool ECS iteration, CPU batching, shared skeleton palettes, prepared material caches, import indexes, scratch reuse, and indexed Vulkan handoff merging were left in place. Generic binary append reservation appeared suspicious, but current looped production callers already reserve complete serialized output.

Telemetry payload reuse was not changed: the sole production stream decoder creates a fresh, capture-disabled recorder per upload, so repeated-slot reuse would not improve that workflow. A generic decoding change also needs care around input aliasing. Shader/include caching would affect freshness; Basis encoding changes require determinism checks. FBX parent-depth walking and packet-runtime initial-state overlap validation remain candidates for separate profiling. GPU shader changes need hardware timing and image comparisons; none were made in this pass.

## Validation and reproduction

Camera and graph regressions also pass against their baseline implementations. Metadata coverage compares the complete persisted file against an expected wire image. All 449 tests pass separately in `dbg`, `opt`, and `fin` across `nwb_gpu_task_tests`, `nwb_ecs_scene_tests`, `nwb_filesystem_tests`, and `nwb_telemetry_tests`. All 52 repository policy checks pass, including terminal-only exceptions, source text format, return-value handling, and architecture boundaries. The complete policy suite was rerun successfully after the final benchmark-fixture correction.

Build the four targets using the checked-in Linux presets. The bundled CMake tools are available at `__cmake/tool-venv/bin/` when they are not on `PATH`. Run the opt-in benchmarks from the repository root:

```bash
taskset -c 2 __exec/linux/x64/full/opt/gpu_task_tests --gtest_also_run_disabled_tests --gtest_filter='GpuTaskGraph.DISABLED_HazardTrackingBenchmark*' --gtest_output=xml:graph.xml
taskset -c 2 __exec/linux/x64/full/opt/ecs_scene_tests --gtest_also_run_disabled_tests --gtest_filter='SceneCameraSelectionBenchmark.*' --gtest_output=xml:camera.xml
taskset -c 2 __exec/linux/x64/full/opt/filesystem_tests --gtest_also_run_disabled_tests --gtest_filter='FilesystemVolumeTest.DISABLED_MetadataFlushBenchmark*' --gtest_output=xml:metadata.xml
```

Use the same fixtures and build settings on each revision. Divide graph `elapsed_ns` by `repetitions`, camera `median_ns` by `iterations_per_sample`, and metadata `flush_ns` by `iterations` to obtain nanoseconds per operation. There are no timing thresholds in the ordinary regression suites.
