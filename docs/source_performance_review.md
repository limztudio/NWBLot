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

## Allocator propagation and memory ownership

The three arena allocator adaptors now implement their advertised move-propagation contract: assignment transfers the arena pointer with the container storage. A targeted oneTBB segment-table correction releases an unequal destination allocation before propagating the incoming allocator. Same-arena and nonpropagating vendor paths retain their existing behavior. This prevents deallocation from being charged to the wrong owner and is a prerequisite for reliable recorder payload reuse.

The original direct-assignment regression failed for all three adaptors. Nine propagation regressions now pass, covering vectors, small and heap-backed strings, ordered maps, default-provider rebinding, cache-aligned concurrent vectors, supported hash-map operations, source reuse, destruction, and allocation balance. All 132 global tests pass in dbg, opt, and fin. Cross-arena hash-map assignment still follows the vendor swap contract; no unsupported allocator traits were added. This is a correctness fix, with no timing speedup claim.

## Arena object construction unwind

Arena object factories now retain raw storage in a construction guard until construction succeeds. Scalar unique factories share the guarded raw-object entry point; array factories release storage after the language has destroyed successfully constructed array elements during unwind. The original arena, element count, and alignment are preserved without calling an unconstructed object destructor.

Ten regressions pass in dbg, opt, and fin for raw, generic, global, and persistent factories, partial aligned arrays, nested members, successful ownership transfer, and rejected allocations. All 132 global tests pass in each configuration. This closes a construction-failure leak exposed while reviewing recorder slot allocation; successful construction keeps the existing allocation count and no performance claim is made for exception paths.

## Vulkan command-buffer resource references

The tracked command buffer now owns one focused resource-reference component. Small lists retain their linear path; lists beyond 32 entries publish an exact pointer-membership index over the ordered owning references, typed references, and pending buffer-state journal. Indirect descriptor/framebuffer ownership, typed-to-owning upgrades, journal discard, and clear/reuse retain their original lifetimes and order. Index storage uses the graphics owner arena and retains capacity for recycled command buffers.

| Workload | dbg before → after | opt before → after |
| --- | ---: | ---: |
| 1,024 buffers + 256 textures, cold recording | 39.3629 → 1.1214 | 0.8796 → 0.1250 |
| Same resources, eight repeated recordings | 312.4358 → 5.0771 | 7.1945 → 0.3190 |
| Recycled command buffer, same resources | 38.9381 → 0.7932 | 0.8857 → 0.0495 |
| Four buffers + one texture, 256 repeated recordings | 0.8782 → 0.9167 | 0.0119 → 0.0146 |

Small-list overhead is about 150 ns per repeated recording in dbg and 11 ns in opt. Large clear time changes from 0.1887 to 0.1403 ms in dbg and 0.0083 to 0.0099 ms in opt. Nine resource-reference regressions pass in dbg, opt, and fin, alongside the full graphics task-graph suite. Tests cover lifetime, promotion, mixed ownership, journal reuse, and allocation-free warmed reuse; these CPU fixtures do not measure GPU execution.

## Shared skeleton attachment palettes

An attachment update groups joint queries by full parent entity identity, builds each referenced pose palette once, and stores only requested matrices. A separate index groups parents while query results and transform reads/writes retain the original ECS order. Palette contents refresh every update, and zero/single-query paths avoid grouping storage. Missing, invalid, empty, or shortened poses leave affected transforms untouched and can recover on the next update.

| Workload | dbg before → after | opt before → after |
| --- | ---: | ---: |
| 1,024 attachments, one 128-joint parent, four updates | 782.0280 → 16.2776 | 4.7760 → 0.2680 |
| 256 attachments on distinct 32-joint parents, four updates | 55.6174 → 54.1501 | 0.4007 → 0.3988 |
| One attachment on a 16-joint parent, 256 updates | 7.4193 → 7.5338 | 0.0651 → 0.0648 |

The gain comes from shared-parent reuse; distinct-parent and singleton timings are essentially unchanged. Grouping requires operation-local scratch storage for multi-query updates. All seven attachment regressions and five runtime-cleanup regressions pass in dbg, opt, and fin, including generation replacement, pose changes, bounds recovery, transform-write ordering, and singleton allocation reuse.

## Telemetry recorder slot and payload reuse

The recorder reuses event slots and payload capacity across enabled-capture clears. Producers build directly into an exclusively leased slot, so callbacks can reenter the recorder, clear events, change capture options, or unwind without exposing partially built data. Publication rechecks capture eligibility. Prebuilt payloads preserve arena ownership, and foreign-backed payloads are copied into the recorder arena before publication.

| Warmed workload, 120 record/clear frames | dbg before → after | opt before → after |
| --- | ---: | ---: |
| 128 memory-owner events per frame | 29.0277 → 12.5719 | 1.3535 → 0.7607 |
| 512 memory-owner events per frame | 117.5175 → 49.2883 | 5.4368 → 3.0462 |

Backing allocations in the measured 512-owner workload fall from 245,760 to zero in dbg and 122,880 to zero in opt; the 128-owner workload also reaches zero. Slot and payload capacity is retained at the observed capture high-water mark until capture is disabled or the recorder is destroyed. Event-vector capacity remains reusable. This trades retained memory during capture for less repeated allocation.

All 94 telemetry tests pass in dbg, opt, and fin. Twelve new regressions cover empty payloads, callback reentry, capture changes, exceptions, active-event aliases, same/foreign-arena ownership, explicit allocator replacement, and concurrent builders with clear/disable. Memory payload version remains 1; decoded owner, source, stream, and memory measurements are checked by the benchmark fixture.

## Duplicate asset gathering

Gathering now records each validated identity directly against its manifest entry instead of combining a set lookup with a linear entry scan. Byte-identical duplicates keep the already computed payload identity; a successful merge recomputes size and hash once. Every input is still read and validated, and conflicting input cannot replace the previously published volume.

The end-to-end gather workload contains 128 unique 64 KiB payloads in three input directories. It includes reads, validation, duplicate collapse, and volume publication, while fixture creation/readback are excluded. Median dbg time changed from 684.6835 to 606.8864 ms; opt changed from 523.1486 to 513.5015 ms. Filesystem work dominates this fixture, so the small opt difference should not be treated as a general throughput guarantee.

Three duplicate/merge/publication regressions and the existing independent-shader-gather integration pass in dbg, opt, and fin. The owning asset integration target and pipeline tools build in all three configurations.

## Skeleton cook parent resolution

Skeleton cooking resolves each parent through the joint-name map already populated by earlier joints and reserves the map once for the expected count. It removes a growing prefix scan without adding a second index. Root handling, earlier-only parent rules, canonical duplicate detection, failure output clearing, and serialized joint/child order are preserved.

| Complete skeleton build workload | dbg before → after | opt before → after |
| --- | ---: | ---: |
| 4,096-joint chain, four builds | 168.6239 → 5.4500 | 16.9149 → 0.5206 |
| One joint, 4,096 builds | 19.8398 → 20.5019 | 0.4921 → 0.5363 |

The singleton workload adds roughly 162 ns per build in dbg and 11 ns in opt; the optimization targets multi-joint parent resolution. Four direct regressions pass in dbg, opt, and fin, including parent-order failures/recovery, canonical duplicate identities, topology rebuilding, and serialized matrix/joint identity.
