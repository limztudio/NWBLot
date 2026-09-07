# Source performance review

Measurements use Windows ARM64 CPU workloads, one warm-up and seven alternating before/after runs. Values are medians in milliseconds. Each comparison includes the same functional workload; setup is excluded unless described. These are CPU operation measurements, not FPS or GPU timing claims. Allocation-owner telemetry remains enabled and its memory payload remains version 1.

## Asset input selection

The selector builds one operation-local sorted index over discovered canonical paths. Exact file inputs and directory prefixes use indexed ranges, with a separate case-preserving directory index on non-Windows hosts. Discovery order, duplicate records, overlapping inputs, empty directories, root boundaries, and validation-before-compaction are preserved. The benchmark includes path resolution, filesystem queries, selection, and owning scratch teardown for 2,048 files.

| Workload | dbg before → after | opt before → after |
| --- | ---: | ---: |
| 2,048 explicit file inputs | 183.4631 → 102.1824 | 57.4520 → 42.8689 |
| One directory containing 2,048 files | 41.0431 → 0.5545 | 1.6796 → 0.0925 |

Four input-selection regressions pass in dbg, opt, and fin. The staged pipeline tools and asset integration target build in all three configurations. Host case behavior is encoded in the tests; this machine executes the Windows branch.

## Model payload validation

A caller-owned scratch name index replaces repeated full-model scans. It records cross-kind duplicates and skeleton membership once per validation. The singleton case leaves the index unconstructed. Loading, serialization, metadata parsing, and asset building pass scratch through their owning operation; the binary payload is unchanged.

| Validation workload | dbg before → after | opt before → after |
| --- | ---: | ---: |
| 4,096 mixed objects, four validations | 804.1624 → 5.6042 | 42.9802 → 0.5336 |
| One object, 4,096 validations | 1.2460 → 0.3541 | 0.0215 → 0.0225 |

Timings cover validation and its temporary index, with caller scratch supplied outside the measured loop. The 1 microsecond difference across 4,096 optimized singleton calls is too small to claim a gain. Eight direct model regressions and five existing model integration cases pass in dbg, opt, and fin, including serialized invalid input, parent-kind validation, replacement/recovery, zero singleton scratch allocations, and stable scratch usage across repeated success/failure.

## Frame-graph packet telemetry

A capture-lifetime identity index removes repeated duplicate scans in the builder, which is shared across contributors. Codec validation accumulates packet totals once per owner and physical queue, preserving queue coverage, generations, duration accumulation order, and rejection of malformed totals. Sparse IDs require storage proportional to actual records.

| Workload | dbg before → after | opt before → after |
| --- | ---: | ---: |
| Build 4,096 packets, one owner | 86.7674 → 1.8721 | 3.7362 → 0.2501 |
| Encode 4,096 packets, 64 owners | 6.8863 → 2.3266 | 0.4880 → 0.2784 |
| Decode 4,096 packets, 64 owners | 5.3891 → 0.9870 | 0.3323 → 0.1291 |

Small batches carry index/setup overhead: at 512 packets for one owner, opt builder time changed from 0.0547 to 0.0671 ms and decode from 0.0166 to 0.0260 ms, while encode improved from 0.0565 to 0.0456 ms. The dbg builder improved from 1.4392 to 0.2818 ms for that workload. All 82 telemetry tests pass in dbg, opt, and fin, including the seven added statistics regressions. Frame and logserver build in all configurations. Allocation-owner telemetry and memory payload version 1 are preserved.

## Shadow geometry preparation

The renderer rebuilds an exact buffer/member ownership index for each freeze. It preserves the first owning mesh for each source member, ordered prepared handles and combined roles, accepted normalization state, invisible live meshes, and pending build inputs. Up to 32 distinct buffers use inline storage; larger sets use caller scratch. Retired accepted handles are pruned in one stable compaction.

| Workload | dbg before → after | opt before → after |
| --- | ---: | ---: |
| 1,024 meshes / 4,096 buffers, cold freeze | 351.7025 → 8.6349 | 12.0946 → 0.7335 |
| Same set, four repeated freezes | 1409.0004 → 31.5921 | 46.9115 → 1.8429 |
| Prune 4,096 retired handles | 103.4011 → 3.8983 | 2.4623 → 0.1082 |
| One mesh, 256 repeated freezes | 2.0169 → 1.7297 | 0.0478 → 0.0646 |

The small opt workload adds about 66 ns per freeze; larger workloads show substantial reductions. The index is rebuilt rather than cached between frames, and large sets require additional scratch storage. Seven direct shadow-geometry regressions and all 170 ECS graphics tests pass in dbg, opt, and fin. The renderer/frame also builds in all three. These fixtures use real metadata-only buffer objects and exercise the production CPU helper; they do not establish GPU/FPS gains.

## Model runtime cleanup

Runtime synchronization collects inactive objects and owners into operation-owned scratch batches before changing ECS storage. This removes nested reuse of a shared entity list, which could invalidate the outer cleanup loop, and avoids scanning every object separately for each removed owner. Runtime views are refreshed after entity destruction. Full entity generations keep reused IDs isolated. Empty and steady-state cleanup constructs no temporary vector and performs no backing allocations.

Five regressions pass in dbg, opt, and fin: mixed owner sizes, removed/invalid bindings, entity-generation reuse, failed-load recovery, and allocation-free steady state. This item fixes cleanup correctness and removes repeated scans; no timing speedup is claimed.
