# PersistentArena follow-up audit

Audited after pulling `origin/main` to `a542184b` on 2026-09-08. The pass covers project-owned core, Vulkan, implementation, tooling, pipeline, logger, and sample code, including GlobalArena aliases such as GraphicsArena and GraphicsVector.

The decision criteria are ownership, capacity, and measured benefit. Runtime-derived sizes are acceptable; a compile-time limit is not required. A conversion must preserve supported workloads, failure cleanup, live readers, and GPU completion lifetimes. Fewer heap allocations alone do not establish a speed improvement.

## Input-layout experiment

Vulkan input layouts have three immutable tables: copied vertex attributes, deduplicated native bindings, and native attributes. Their exact counts are known after validation. The experiment replaced their three GlobalArena vectors with typed arrays in one exactly sized PersistentArena. Pool sizing used checked arithmetic and `PersistentArena::StructureAlignedSize` for each allocation; the tables were destroyed before the pool. Empty layouts did not construct a pool.

The conversion was discarded because complete input-layout creation and destruction became slower.

| Windows ARM64, clang, `fin` | GlobalArena baseline | PersistentArena trial |
| --- | ---: | ---: |
| Median of five run medians, 2,048 layouts | 359,500 ns | 430,600 ns |
| Time per layout | 175.5 ns | 210.3 ns |
| Heap backing allocations per layout, including object and scratch work | 5 | 3 |

The trial was **19.8% slower** in this workload. Each process performed eight warm-up samples and 32 measured samples. Each sample created and destroyed 2,048 layouts with three attributes across two bindings, including an instance binding. Baseline and trial executions alternated for five pairs. Their per-run medians were:

| Pair | GlobalArena ns | PersistentArena ns |
| --- | ---: | ---: |
| 1 | 359,150 | 430,650 |
| 2 | 368,750 | 430,600 |
| 3 | 360,750 | 417,000 |
| 4 | 359,500 | 433,750 |
| 5 | 358,350 | 420,900 |

Device initialization and sample sorting are outside the timed region. Validation, scratch work, owning copies, and destruction are included. Timing is a CPU creation measurement, not a rendering or GPU throughput result. Pool construction and teardown are part of this experiment; this does not contradict allocator-churn improvements when a PersistentArena is reused.

The retained test source is `tests/smoke/descriptor_buffer/round_trip/input_layout_storage_tests.cpp`. Its ordinary cases cover empty input, ownership of copied descriptions, shared and sparse binding indices, independent layout lifetimes, and out-of-range description access. The opt-in benchmark records median, p95, and heap backing allocation counts without a machine-dependent timing assertion:

```powershell
cmake --build --preset windows-clang-arm64-fin --target nwb_descriptor_buffer_tests
./__exec/windows/arm64/full/fin/descriptor_buffer_tests.exe `
    --gtest_also_run_disabled_tests `
    --gtest_filter=DescriptorBufferRoundTripTest.DISABLED_InputLayoutCreationBenchmark `
    --gtest_output=xml:input_layout_benchmark.xml
```

This command measures the retained GlobalArena implementation. A comparison requires a separately built candidate; the discarded trial is not active production code.

## Remaining owner groups

| Domain | Remaining storage and decision |
| --- | --- |
| CPU task scheduler | Worker placements, depth counters, and thread handles have an exact worker-count bound, but replace only three cold-start allocations. They do not allocate in steady-state task submission. No demonstrated performance benefit justifies adding a pool. Task nodes, dependent lists, and searches grow with submissions. |
| GPU task compiler | Queue assignments have two exact task-count arrays, but retain capacity across recompilation. A fresh pool would allocate during warmed compiles that currently allocate nothing. The larger compiled-plan and analysis groups discover dependency, barrier, transfer, and packet counts during compilation; a direct fixed pool needs staging, rebuilding, or a new workload cap. |
| GPU recording and submission | Active/candidate plans, recording leases, resource references, state ranges, and submission journals have changing cardinality and reader lifetimes. Preserve retained capacity and existing publication/retirement ownership. |
| Vulkan device queues | Registry arrays and queue shells have exact descriptor counts, but are allocated once per device. Pooling their growable submission and worker state would require a separate capacity contract. The small registry bundle has no demonstrated speed benefit. |
| Vulkan shaders and ray-tracing pipelines | Bytecode, specialization tables, group handles, and export metadata are populated at creation. Consolidating the larger groups also requires reworking nested owning strings and descriptor copies; pooling only the small flat subset adds the same per-resource setup cost seen in the input-layout trial. Keep the current ownership until a complete candidate demonstrates a benefit. |
| Vulkan buffers and staging textures | Fixed queue-family/version or mip/family metadata consists of only one or two arrays. Buffer views and texture views grow later. A private pool offers no demonstrated improvement for the fixed subset and cannot directly bound the growable caches. |
| Vulkan descriptor segments and binding layouts | Segment free ranges and live allocations change with allocation fragmentation; a theoretical byte-capacity-derived maximum would reserve excessive metadata. Binding layouts expose concrete allocator-backed metadata, and pooling their small tables would add setup overhead and API work. The existing fixed descriptor-heap and breadcrumb pools remain appropriate. |
| ECS and render systems | World, draw, cutter, asset, and message counts vary with workloads. Small existing numeric bounds are already represented by stack storage, fixed containers, or scratch work. |
| Material and skinning caches | MaterialSurfaceInfo has up to seven payloads but is filled on first use and moved within its owning cache; a nonmovable private arena needs additional indirection. Skinning instances have a larger array group, but source/entity changes rebuild it and exact output sizing needs staging or two passes. These need dedicated end-to-end experiments before allocator changes. |
| Assets, filesystems, and tooling | Parsed asset groups include nested names, payloads, and variable output topology. Existing public allocator contracts and reload/move behavior do not provide a direct private-pool substitution. Build and parse temporaries belong to their operation's scratch storage. |
| Input, telemetry, logger, and registries | Registration/event/path cardinality is open-ended, often with concurrent producers or independently retired entries. No complete fixed ownership budget exists. |

Revisit these decisions when an owner gains an explicit capacity contract, a pool can be reused without losing current capacity retention, or profiling identifies a measurable allocator bottleneck. A finite input alone does not make every GlobalArena container a useful PersistentArena conversion.

## Validation

The retained code built with `windows-clang-arm64-dbg`. The five selected CTest suites passed: 143 global tests, 43 CPU task tests, 331 GPU task tests, 381 Vulkan descriptor-buffer tests, and the texture capture smoke test. Another 38 Vulkan cases skipped because this host lacks their requested capabilities. The timing benchmark is disabled in ordinary test runs and was explicitly enabled for the release-build comparison above. The texture capture was visually inspected after the successful smoke run.

This audit adds tests and measurement documentation. It keeps no new production allocator conversion and introduces no direct `std::` use.
