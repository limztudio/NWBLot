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

## Command IR capture growth and truncation

Command capture reserves inspection records before encoded bytes, then publishes both sequences without further allocation. Both buffers now grow geometrically through the existing container helper. Reset and validated rollback shrink in bulk instead of popping each byte. Stream boundaries, rejection behavior, generation changes, and allocation-failure publication order are preserved.

| Workload | dbg before → after | opt before → after |
| --- | ---: | ---: |
| Capture 4,096 records from cold capacity | 38.9551 → 1.3559 | 34.7518 → 0.1805 |
| Capture 16,384 records from cold capacity | 814.9092 → 5.3789 | 776.7808 → 0.5765 |
| Roll back half of 16,384 records | 3.9745 → 0.0794 | 0.0140 → 0.0157 |
| Reused capture of 16,384 records | 5.1581 → 5.0046 | 0.2327 → 0.2321 |

The 16,384-record cold capture uses 28 backing allocations instead of 32,768 in both configurations. Warmed reuse still allocates zero times. Debug reset falls from 8.0181 ms to 0.0003 ms; that final value is near timer granularity and is not used for a speedup-ratio claim. Geometric capacity retains spare storage rather than allocating exactly each append. Three new regressions and the complete 356-test graphics suite pass in dbg, opt, and fin, including mixed encoded records, exact prefix rollback/refill, rejection, and capacity reuse.

## Timing history lookup

History stores and immutable snapshots now use exact indexes for collections larger than 16 entries. Route keys include the full binary name identity, variant, resolution, queue class, physical queue, and device generation. Assignment keys preserve the separate semantic switch timeline. Ordered records retain diagnostic names; compact index keys use a callback-free binary identity accessor. Small collections retain linear lookup and unchanged storage.

| Workload, 4,096 histories | dbg before → after | opt before → after |
| --- | ---: | ---: |
| Initial recording | 399.6537 → 10.6616 | 18.1095 → 0.9715 |
| Four updates | 1564.5755 → 10.1868 | 72.6829 → 0.8271 |
| Four store lookup passes | 1524.4964 → 5.5621 | 71.6248 → 0.6120 |
| Four snapshot lookup passes | 1552.0006 → 5.8751 | 67.0107 → 0.6267 |
| Four snapshot copies | 1.0128 → 2.3049 | 0.0837 → 0.3864 |

Indexes trade storage and snapshot-copy time for much cheaper recording and lookup. At 4,096 histories they add about 1.44 MiB to each store and snapshot: dbg store usage changes from 4,383,160 to 5,890,520 bytes and snapshot usage from 3,014,688 to 4,522,048 bytes. Opt adds the same index capacity. The eight-history fixture keeps its original storage; its opt snapshot lookup changes from 0.0214 to 0.0318 ms across 256 passes, about 41 ns per pass. No small-list speedup is claimed.

Six timing regressions and all 356 graphics tests pass in dbg, opt, and fin, covering full identities, promotion, independent snapshot arenas, reset/device replacement, assignment timelines, and warm reuse. All 133 global tests also pass in these configurations and in the opt name-symbol build, including positive symbol-callback probes and callback-free identity access.

## Skinning graph resource declarations

A focused collector replaces three growing-vector duplicate scans. It records exact resource index/generation identities independently for deformation, post-dispatch, and finalization, merging access only when required states agree. First-occurrence ordinals preserve declaration order. Inline capacity covers every role in one plan; larger collections index actual unique resources in caller-owned scratch. Output allocation follows index growth, and ordinary rejection leaves every output empty.

| Workload | dbg before → after | opt before → after |
| --- | ---: | ---: |
| 1,024 plans with unique resources | 1797.7141 → 8.6149 | 46.5298 → 1.5819 |
| One plan, 4,096 collections | 46.3508 → 14.2233 | 2.0935 → 1.2228 |
| 1,024 shared-resource plans, eight collections | 37.1011 → 3.4282 | 0.5034 → 0.7214 |

The shared opt workload adds about 27 ns per plan; the debug improvement and unique-resource scaling are the main gains. One/shared-plan scratch peaks drop from 5,200 to 1,616 bytes in dbg and from 5,152 to 1,568 bytes in opt. For the unique workload, indexing raises peak scratch from about 5.73 to 7.78 MiB. Repeated identical plans use exactly the same scratch capacity as one plan; storage grows with actual unique resources.

Six collector regressions and all 176 ECS graphics tests pass in dbg/opt, with 179 tests in fin because of configuration-specific coverage. Renderer/frame and pipeline builds pass in all three. Coverage includes full generations, role order, shared resources, every phase's conflict/missing-resource rejection, and recovery after promoted-table failure. Fixtures exercise production CPU collection using immutable dispatch inputs; no GPU timing claim is made.

## Model metadata skeleton normalization

Model parsing builds one caller-scratch index of complete binary name identities, direct object-name presence, and unique/ambiguous skeleton asset aliases. Direct names retain precedence; mesh traversal and ambiguity diagnostics keep their original order. Unknown aliases and duplicate object names still reach the existing validator. Empty and single-skeleton cases avoid the index, and the index is destroyed before payload validation.

| Complete parse workload | dbg before → after | opt before → after |
| --- | ---: | ---: |
| 256 skeletons and aliased meshes, three parses | 20.1841 → 15.9562 | 1.0792 → 0.8912 |
| 1,024 skeletons and aliased meshes, three parses | 142.4238 → 65.3803 | 8.5128 → 3.5375 |
| 4,096 skeletons and aliased meshes, three parses | 1496.9419 → 277.2686 | 109.9718 → 19.3041 |
| 4,096 skeletons and direct-name meshes, three parses | 494.9427 → 241.1452 | 29.7104 → 18.3643 |
| One skeleton and aliased mesh, 1,024 parses | 25.0923 → 26.0879 | 1.0608 → 1.0580 |

The singleton debug difference is about one microsecond per complete parse; no singleton gain is claimed. Heap allocation counts are unchanged because metadata copying and validation remain part of the public parse operation. At 4,096 objects, retained scratch capacity changes from 11,011,072 to 13,894,656 bytes in dbg and 2,621,440 to 2,883,584 bytes in opt. Debug LIFO scratch retains 32 bytes after warm-up versus 16 before; used and reserved bytes remain stable across repeated parsing. Opt retains zero used bytes.

All six normalization regressions and the selected 26-case asset integration suite pass in dbg, opt, and fin; pipeline builds also pass. Benchmark assertions verify the normalized typed references, unchanged source metadata, and stable warmed scratch. The baseline and optimized fixtures use the same corrected warm-capacity check rather than assuming debug scratch returns every byte immediately.

## Live skinning buffer collection

Live-buffer collection retains the first owning handle for each buffer while tracking exact pointer membership. Up to 32 distinct resources use an inline pointer array, avoiding debug iterator overhead; larger collections promote to a caller-scratch index. Every call rechecks current instance validity, all 17 runtime buffer roles, and three matching-revision supplemental roles. Collection includes invisible live bindings and retains no index between calls.

| Workload | dbg before → after | opt before → after |
| --- | ---: | ---: |
| 1,024 unique instances / 20,480 buffers | 2692.7559 → 13.4981 | 51.3764 → 2.3368 |
| 1,024 instances sharing 20 buffers, eight collections | 37.7502 → 3.3405 | 0.4906 → 0.6359 |
| One instance, 1,024 collections | 12.4396 → 7.8707 | 0.3630 → 0.4009 |

The shared opt workload adds about 18 ns per instance; opt singleton overhead is about 37 ns per collection. Singleton/shared collections allocate no scratch storage. The unique workload adds about 2 MiB peak scratch and reserves about 3 MiB in dbg or 2.66 MiB in opt, released with the owning scratch arena. Inline and indexed publication retain consistency if an owning-handle allocation fails.

Six collector regressions and all 182 ECS graphics tests pass in dbg/opt, with 185 passing in fin. Renderer/frame builds pass in all three. Coverage includes buffer lifetime, first-occurrence order, missing/invalid instances, optional roles and revision changes, promotion, allocation-free duplicate replay, and large/small collection reuse after resource replacement. Benchmarks use real metadata-only buffer objects and measure CPU collection, including collection storage teardown.

## Frame-graph report owner traversal

JSON and DOT generation now walk each parsed owner table once as graph nodes advance. Decoder validation guarantees contiguous ordered owner records, so small stack cursors replace repeated full-table counts and filters without adding an index or allocation. Fresh cursors belong to each captured graph and each output pass. Record order, output formatting, and absent-versus-empty statistics remain unchanged.

| Complete report workload | dbg before → after | opt before → after |
| --- | ---: | ---: |
| 1,024 tasks/packets, one owner, three reports | 71.1961 → 41.6651 | 5.3210 → 4.7567 |
| 4,096 tasks/packets, one owner, three reports | 601.5895 → 163.7616 | 41.0792 → 19.4573 |
| 4,096 tasks/packets, 64 owners, three reports | 655.6413 → 181.5269 | 43.5201 → 21.1879 |
| One packet and one task, 128 reports | 13.6692 → 13.5457 | 1.4792 → 1.4735 |

Heap allocation counts and JSON/DOT output byte counts are unchanged in every measured case. The timed public operation includes decoding and both report formats; event construction/encoding and output assertions are excluded. Small-report timing is essentially unchanged.

All 97 telemetry tests pass in dbg, opt, and fin, including three new report regressions for sparse owners, missing coverage, exact empty records, capture/rebuild resets, malformed owner/order tables, and recovery to valid input. The logserver builds in all three configurations. Allocation-owner/source reporting and memory payload version 1 are unchanged.

## Persistent resource-state selection

Whole-resource filtering now builds one operation-owned selection of exact pointer and resource-kind identities. First input ordinals retain the original owning handles. Up to 32 distinct resources use packed inline pointer ranges; larger selections use one compact allocation with ordered entries and bounded open-address lookup. Checked growth uses the caller's scratch arena, and merged construction destroys the selection before fan-in reuses that arena. The incoming and retained snapshots share this selection, removing repeated handle/pointer deduplication and per-state full-selection scans.

| Complete workload | dbg before → after | opt before → after |
| --- | ---: | ---: |
| Raw subset, 4,096 unique buffers | 28.7697 → 1.1724 | 3.8188 → 0.2008 |
| Owning filtered candidate, 4,096 unique buffers | 230.7943 → 1.7181 | 8.1413 → 0.2016 |
| Merged candidate, 4,096 unique buffers | 463.1095 → 3.2181 | 16.2128 → 0.4168 |
| One buffer, 1,024 filtered candidates | 5.2305 → 4.1892 | 0.0794 → 0.0657 |
| 4,096 selections / 32 unique buffers, four candidates | 10.1935 → 0.7286 | 0.1408 → 0.0915 |
| 32 selected out of 4,096 buffers, eight merges | 4.1861 → 4.5640 | 0.5254 → 0.5422 |

The sparse workload adds about 47 microseconds per merge in dbg and 2 microseconds in opt; it keeps the inline path and avoids scratch allocation for selection. Singleton and repeated 32-resource selections also allocate no selection storage. At 4,096 distinct buffers, raw/filtered selection peaks at 245,760 scratch bytes and reserves 435,200 bytes on a cold arena. Raw filtering adds four cold backing allocations; owning filtered construction drops from 32 to 12 backing allocations in dbg and 24 to six in opt. Merged construction drops from 95 to 25 in dbg and 72 to nine in opt. Its peak scratch remains about 384 KiB, while cold reserved capacity grows to 1,221,680 bytes. Same-arena large/small/repeated tests verify stable used and reserved bytes after warm-up.

Eleven regressions preserve null suppression, first occurrence, collision/growth behavior, separate buffer/texture identity domains, source order, all transient/permanent categories, texture mip/slice records, queue ownership, generation validation, self-filtering, aliased merged candidates, retained handle lifetime, and allocation-free commit. The complete 367 graphics tests, 97 telemetry tests, 182 ECS graphics tests (185 in fin), and selected 26 asset integration tests pass in dbg, opt, and fin. Renderer/frame, pipeline, logserver, and descriptor-buffer smoke targets build in all three. All 48 source-policy checks and self-tests pass, and five focused native persistent-state/handoff tests pass on this host. These benchmark figures measure CPU work; no frame-rate claim is inferred. Allocation-owner telemetry and memory payload version 1 remain unchanged.

## Shadow-preparation state input gathering

The renderer now gathers mesh acceleration inputs before reserving the full shadow-preparation input vector. One checked reservation covers accepted geometry, prepared geometry, retained acceleration buffers, and four fixed roles. A linear append keeps the original valid-handle traversal order; the persistent-state selector performs the single required deduplication at its only consumer. This removes the producer's growing-prefix pointer scan and prevents interleaved scratch-vector growth.

The intermediate vector can now retain duplicate input handles until the synchronous lifecycle callback finishes. The final snapshot still retains the first selected owning handle, and all candidate-readiness distinctions are unchanged. This is an algorithmic removal of redundant quadratic work; no isolated frame-time gain is claimed. Existing renderer source contracts and the selected graphics, telemetry, ECS, and asset suites pass in dbg, opt, and fin (672 tests in dbg/opt and 675 in fin), and renderer/frame, pipeline, logserver, and descriptor-buffer targets build in all three.

## Shared terminal entry and RAII cleanup

Application entry handlers now use `core/common/terminal_entry.h`. Entry functions return its terminal result immediately; CLI parsing and application work share the boundary while the CLI context remains alive for help/error output. Pipeline validation still returns explicit failures. Existing utility CLI codes, pipeline help/error routing, loader/logserver exit codes, NameSymbols publication, and logserver's final typed exception diagnostic are preserved.

`ScopeExit` directly owns a non-throwing callable without allocation or type erasure. It replaces local catch/rollback/rethrow blocks in timing-history publication, timing subscriptions and policy transitions, live skinning buffer retention, and loader shutdown. Publication explicitly releases the guard; failure unwinding preserves destruction order and caller-owned allocation lifetimes.

Eight scope-guard regressions and four terminal-entry regressions pass in dbg, opt, fin, and the optimized name-symbol configuration. The CLI subprocess tests pass in all three full configurations, covering help, invalid options, exact terminal exit codes, and absence of output publication after rejected arguments. These changes enforce terminal-only handling; no isolated speedup is claimed for the entry or scope-guard utility.

## Terminal scheduler failures

ThreadPool and JobSystem no longer capture, store, compare, or defer worker exceptions. Unexpected native-worker failures terminate at the native thread boundary. Inline failures unwind to terminal application handling. RAII owns prepared task nodes, partial queue publication, parallel chunk completion, active-worker retirement, and dependent-job cancellation. A failed running job publishes cancellation before its capture is destroyed, preventing reentrant destruction from admitting replacement work. Queue capture destruction remains outside scheduler locks, and descriptor storage survives until all claimed callbacks retire. Obsolete exception-pointer helpers are removed; direct users include the remaining unwind-count utility explicitly.

| Public job submission workload | dbg before → after | opt before → after |
| --- | ---: | ---: |
| Fan-out: 4,096 distinct dependent jobs | 7.8728 → 7.1415 | 0.8128 → 0.6303 |
| Fan-in: join 4,096 distinct dependencies | 1.9267 → 1.8931 | 0.2284 → 0.2508 |
| Fan-out: 1,024 jobs for repeated dependencies | 1.9906 → 1.7426 | 0.1709 → 0.1623 |
| Fan-in: 4,096 references to 1,024 jobs | 1.0199 → 0.9539 | 0.1354 → 0.1328 |

Values are milliseconds, using one warm-up and seven alternating before/after pairs. Fan-out benefits from removing failed-domain admission bookkeeping. Optimized distinct fan-in adds about 22 microseconds; these figures do not establish a gain for every graph shape. The existing fan-in fixture also checks exact dependency completion and single continuation execution outside the measured submission intervals.

All 148 global tests pass in dbg, opt, fin, and optimized name-symbol builds. Native regressions distinguish deterministic caller unwind from process death on worker failure; expected false-result retries remain separate. The complete descriptor-buffer suite passes with 377 tests and 38 skips in dbg, 376 and 39 in opt, and 365 and 37 in fin. Skips reflect unavailable device/queue capabilities and configuration-specific checks. The new native worker-death, timed caller-unwind, composite unwind, and listener-unwind cases run and pass in every configuration.

## Enforced terminal-only exception policy

The parser, logger ingestion worker, symbolication entry, diagnostic telemetry callback, and diagnostic crash capture no longer catch unexpected failures as ordinary errors or continue after them. Syntax/read failures, malformed crash input, and expected I/O failures retain their explicit status paths. Three reader regressions distinguish a returned failure and an oversized read from an unexpected exception, which now propagates without becoming a parse diagnostic.

The `.helper/standard.md` rule now explicitly forbids local recovery, rollback/rethrow catches, and deferred worker exception delivery. A source policy rejects first-party production C++ catch clauses outside `core/common/terminal_entry.h`; its nine self-test cases cover typed, macro, function-try, comment, and literal handling. Vendor code and test-only exception assertions are outside that production rule.

The main validation matrix passes 870 unit/integration cases in dbg and opt and 873 in fin, plus both CLI process suites in each configuration. This includes all global, metascript, crash, logserver-crash, graphics task-graph, telemetry and ECS-graphics tests, and the selected 26 asset cases. Complete native-suite results are recorded above. The optimized name-symbol configuration passes 148 global and 20 metascript tests. All 50 source-policy checks/self-tests pass. Allocation-owner/source telemetry remains collectable, and the memory payload version remains 1.

## Asset-bunch resolved metadata ownership

Expanded asset records now own their resolved `Metascript::Value` directly. A local value owns every nested allocation during expansion, then moves into the output after successful resolution. Clearing, replacing, destroying, or unwinding the output releases those values through their original named arena. The registered expander fills the same output type directly, removing the duplicate pointer record and intermediate copy vector. Expected malformed metadata still returns its existing failure status, and partial successful output remains owned until its caller releases it.

Eight ownership regressions verify repeated replacement, move/growth, source-document independence while its arena remains alive, caller unwind, cyclic and missing local references, registered model-parser consumption, and typed null-value rejection. All 42 selected asset cases pass in dbg, opt, and fin, including existing project and model bunch integration. Pipeline and renderer/frame targets build in every configuration. Declaration lookup is unchanged in this item; separate public expansion benchmarks establish a leak-free baseline for the next optimization. No isolated timing gain is claimed for this ownership correction.

## Arena ownership during container swaps

Arena allocators now propagate their owner when containers swap storage. The existing move-assignment contract already transfers the allocation owner; robin-map implements move assignment through swap, so omitting swap propagation caused a debug assertion between different arenas and could pair storage with the wrong deallocation owner in unchecked builds. The common allocator trait now describes both transfers consistently, without changing vendor containers or adding exception handlers.

Five additional regressions exercise ordinary, cache-aligned and default-provider vectors, heap/small strings, ordered maps, and hash maps/sets across the same and different arenas. They verify stable storage, owner transfer, source reuse, growth and balanced per-arena allocation/deallocation. All 153 global tests pass in dbg, opt and fin. The complete selected matrix passes 942 cases in dbg/opt and 945 in fin, plus both CLI process suites in each; it includes the metadata-extension cross-arena move that exposed the bug. This is a correctness prerequisite for ownership-preserving optimization; no isolated timing gain is claimed.

## Parsed metadata extension ownership

Parsed metadata extensions now carry a move-only erased owner with a typed destructor and their original named asset arena. Temporary construction stays owned until insertion succeeds; erase, clear, replacement, map movement, parse rejection and unexpected unwinding all release the concrete payload. Reentrant construction returns the resident extension, and an explicitly moved-out slot can be filled again. Extensions still retire before the entry registry they may borrow. No local exception handler or compatibility owner remains.

Eleven regressions cover alignment, nested values, repeated lookup, growth, cross-arena movement, replacement, construction/insertion/caller unwind, reentrant insertion, destruction order, real graphics-parser rejection and complete public shader/include parsing. All 59 selected asset cases pass in dbg, opt and fin; pipeline, frame and the skinning smoke application build in each configuration. The full selected matrix and 50 source-policy checks also pass. This removes leaked extension payloads; separate complete-parse benchmarks retain the original blanket bucket reservation for the next measured item.

## Runtime mesh cache pruning

Pruning builds one operation-owned set of requested full mesh identities and versions, then lets providers mark current live identities in one traversal. Skinning shares its existing admission logic with descriptor construction but avoids copying thirteen owning buffer handles for membership checks. Provider traversal stops when every requested identity is found. Invisible live bindings remain included, and stale geometry is released before cache erasure in the original iteration order. Up to 32 retained runtime identities use inline storage; static-only caches avoid provider work.

| Complete pruning workload | dbg before to after (ms) | opt before to after (ms) |
| --- | ---: | ---: |
| One binding, 2,048 operations | 9.5112 → 7.0538 | 0.3928 → 0.2733 |
| 1,024 distinct live bindings and retained meshes | 1699.6517 → 2.6400 | 83.1494 → 0.1783 |
| 32 retained meshes among 4,096 bindings | 217.5250 → 8.4121 | 10.6036 → 0.3972 |
| 4,096 shared bindings / one retained mesh, eight operations | 0.3832 → 0.0329 | 0.0092 → 0.0022 |

The 1,024-mesh workload eliminates 524,800 full descriptor resolutions. Its requested-identity index peaks at 180,240 scratch bytes in dbg (180,224 in opt), reserving 361,472/360,448 bytes. Singleton, sparse 32 and shared cases allocate no scratch storage. No membership state or owning descriptor survives the operation.

Eight regressions cover exact full identities and versions, recycled entities, changed readiness, invisible bindings, provider union and early completion, all thirteen descriptor ownership roles, and release order. ECS graphics tests, frame and the skinning benchmark application pass/build in dbg, opt and fin; the selected full matrix and all 50 policy checks pass. These are CPU pruning measurements with real ECS bindings, not frame-rate estimates.

## Asset-bunch declaration lookup

Expansion filters bunch declarations once and shares one exact-text lookup across exports and nested references. Up to 16 declarations use a bounded pointer prefix; larger documents use one caller-scratch table. First exact matching declarations, case-sensitive references, canonical duplicate/cycle checks, source traversal and diagnostic ordering are unchanged. Repeated type classification no longer constructs temporary names for every declaration scan.

| Public expansion workload | dbg before to after (ms) | opt before to after (ms) |
| --- | ---: | ---: |
| Four exports/four locals, 256 expansions | 57.0341 → 35.6654 | 2.3558 → 1.4097 |
| 256 exports/256 locals, three expansions | 1056.7290 → 22.6687 | 46.8214 → 0.9917 |
| 1,024 exports/1,024 locals, three expansions | 16584.1314 → 95.7938 | 737.2510 → 4.2838 |

The large debug workload reduces backing allocations from 33,201,075 to 175,548; opt changes from 11,073,386 to 64,877. The small workload also allocates less and retains its original scratch capacity. The final scratch reservation after the large fixture's repetitions grows from 3,351,552 to 6,071,296 bytes in dbg and 2,105,344 to 2,400,256 in opt. These values include existing transient-string and cross-chunk scratch retention until arena destruction; they do not establish stable warm reuse. Resolved metadata ownership remains balanced.

Six lookup regressions cover inline/indexed thresholds, full reference text, case and duplicate policies, excluded bunch declarations, local cycles and ordered nested failures. All 59 selected asset cases pass in dbg, opt and fin, including the eight prior ownership regressions; pipeline and frame targets build in all three. The baseline already owns resolved metadata values, so these measurements isolate lookup work from the earlier leak correction.

## Shadow preparation resource membership

One declaration-owned selection resolves distinct requested buffers, tracks their BLAS/software roles and trace membership, and partitions the original ordered trace list. Large selections scan graph declarations once; small selections resolve only their requested buffers. Output reservations follow unique active roles. Full resource generations, duplicate trace order, failure cleanup and independent prepared-BLAS policy remain unchanged. Prepared mesh membership borrows immutable full name hashes for the operation, avoiding name copies while retaining exact equality.

| Complete gather, partition and mesh-query workload | dbg before to after (ms) | opt before to after (ms) |
| --- | ---: | ---: |
| One build, 1,024 operations | 2.9285 → 2.3879 | 0.2854 → 0.2926 |
| Eight builds, 256 operations | 5.5128 → 2.0614 | 0.2358 → 0.2051 |
| 16 builds, 256 operations | 15.3505 → 5.5475 | 0.5862 → 0.4818 |
| 32 builds, 64 operations | 12.3854 → 4.7212 | 0.4811 → 0.1958 |
| 64 builds, 32 operations | 21.4436 → 4.9418 | 0.8861 → 0.2185 |
| 1,024 distinct builds plus 1,024 unrelated buffers | 179.7437 → 2.8541 | 12.3227 → 0.3783 |
| 4,096 builds sharing two buffers | 92.4379 → 1.7051 | 4.0918 → 0.2615 |
| One build among 4,096 unrelated buffers, 32 operations | 3.2444 → 1.9625 | 0.4208 → 0.2178 |

The optimized singleton adds about 7ns per operation while debug improves. At 1,024 distinct builds, peak scratch grows from 114,736 to 663,792 bytes in dbg (114,688 to 663,552 in opt); cold reserved capacity becomes 971,776/909,312 bytes. For 4,096 shared-buffer builds, borrowing name identities keeps peak scratch near its old size: 262,240 to 260,368 bytes in dbg and 262,192 to 260,208 in opt. Singleton/sparse scratch remains unchanged. Storage follows distinct requested resources and mesh identities, not unrelated graph resources or repeated input buffers.

Nine functional cases preserve full identities, promotion, current graph replacement, phase ordering, failure fallback and scratch bounds. They pass in dbg, opt and fin along with the complete ECS graphics suite; frame and descriptor-buffer targets build in all three. Reported times include preparation and collection teardown. Individual phase subtotals moved with the reservation/lookup work, so only complete operation timings are compared.

## Material sampled-texture graph imports

Material and trace consumers share one import helper. A single texture uses a direct graph lookup; larger requests memoize distinct pointer identities inline through 32 textures and then use a caller-scratch index with one graph-declaration scan. Existing aliases retain their current full resource ID and bypass creation metadata as before. Missing textures still pass through complete graph import validation in request order, and rejected input preserves the accepted output prefix. No declaration view survives an import.

| Complete import workload | dbg before to after (ms) | opt before to after (ms) |
| --- | ---: | ---: |
| One texture, 2,048 operations | 1.2656 → 1.2750 | 0.1337 → 0.1355 |
| Eight textures, 256 operations | 0.4543 → 0.3885 | 0.0384 → 0.0465 |
| 32 textures, 64 operations | 0.4830 → 0.4556 | 0.0343 → 0.0393 |
| 1,024 textures plus 1,024 unrelated resources, three operations | 29.5312 → 1.6703 | 3.8602 → 0.1932 |
| 4,096 requests for one shared texture, three operations | 10.0871 → 0.4504 | 0.5406 → 0.0526 |
| 256 new imports plus 256 unrelated resources, three operations | 4.4258 → 3.0979 | 0.2867 → 0.2001 |
| One texture among 4,096 unrelated resources, 32 operations | 0.8058 → 0.8112 | 0.1126 → 0.1143 |

Singleton and sparse timings are effectively unchanged. The small opt 8/32-texture cases add about 32/78ns per operation; no universal small-list speedup is claimed. Unique 1,024 peak scratch grows from 16,400 to 175,200 bytes in dbg (16,384 to 175,104 in opt), including output storage; cold reservation is 230,400/229,376 bytes. Small and repeated-pointer workloads retain their original scratch usage. The requested index never scales with unrelated graph declarations or duplicate requests.

Ten functional regressions cover inline/promotion paths, existing aliases, complete import validation, all rejection classes, sequential prefixes, graph generations and resets, resource lifetime and scratch bounds. They and all 229 ECS graphics tests pass in dbg/opt, with 232 in fin; frame and descriptor-buffer targets build in all three. Benchmark setup and assertions are outside the timer; output allocation and helper teardown are included.

## Typed metadata bucket reservation

Public metadata parsing no longer reserves every registered entry bucket using the total input-file count. Shader/include metadata uses extension-owned registries, and bunch files can expand into many entries, so that estimate neither described typed demand nor bounded exported entries. Unused buckets now retain zero capacity; used typed vectors grow normally as actual entries are parsed. The unused registry and virtual reserve APIs are removed.

In the 512-file shader/include fixture, retained metadata falls from 3,385,120 to 1,025,824 bytes in dbg and 1,698,384 to 666,192 in opt. Peak usage falls from 3,692,248 to 1,332,952 bytes in dbg and 1,986,168 to 953,976 in opt. Every parse returns to zero metadata bytes after destruction. Even one shader/include pair drops from 22,256 to 11,888 retained bytes in dbg and 10,144 to 5,600 in opt.

Three 512-file parses measure 316.9893 to 323.0502ms in dbg and 137.0782 to 132.8714ms in opt. Thirty-two pair parses measure 13.3915 to 13.3419ms in dbg and 5.3886 to 6.3689ms in opt. These filesystem-inclusive results do not establish a uniform latency improvement; eliminating unused allocation capacity is the demonstrated gain.

A new real 65-sampler regression crosses vector-growth boundaries, preserves permuted input order and payloads, checks the allocation arena and an unused Model bucket, and verifies late duplicate rejection retains the valid prefix. It and all 60 selected asset tests pass in dbg, opt and fin; pipeline and the skinning benchmark application build in each. The benchmark baseline includes the earlier extension-ownership correction.

## Material sampled-texture collection

A collection owns exact pointer membership for its current output, with 32 inline identities and a lazy index for larger unique sets. Existing prefixes are seeded once, first owning-handle order is preserved, and duplicate prefixes remain intact. Shadow preparation reuses one pending owning vector/index per hardware or software operation. Every reference still checks the current asset cache and descriptor; complete material validation precedes publication, while later missing creation names retain the earlier named prefix. RAII clears pending ownership on success, expected rejection and terminal unwinding.

| Complete collection workload | dbg before to after (ms) | opt before to after (ms) |
| --- | ---: | ---: |
| Pass: one texture, 2,048 collections | 5.2143 → 5.1345 | 0.1931 → 0.2095 |
| Pass: eight textures, 256 collections | 2.4408 → 1.8423 | 0.0764 → 0.0784 |
| Pass: 1,024 unique textures | 15.8412 → 1.5838 | 0.3493 → 0.1222 |
| Pass: 4,096 draws sharing eight textures, two collections | 6.1227 → 3.7623 | 0.1947 → 0.1986 |
| Shadow: one texture, 2,048 collections | 5.2932 → 5.4143 | 0.2009 → 0.2172 |
| Shadow: eight textures, 256 collections | 2.9682 → 2.0610 | 0.0862 → 0.0807 |
| Shadow: 1,024 unique textures | 8.8645 → 1.3758 | 0.1915 → 0.0907 |
| Shadow: 4,096 instances sharing eight textures, two collections | 9.3775 → 5.4280 | 0.2558 → 0.2112 |
| Hardware/software: 128 unique textures, four paired collections | 2.0322 → 0.9819 | 0.0659 → 0.0609 |

Singleton opt overhead is about 8ns per operation; shadow debug singleton adds about 59ns. Unique pass peak scratch grows from 51,264 to 114,832 bytes in dbg (51,248 to 114,736 in opt), reserving 166,912 bytes. Unique shadow peak becomes 63,600/63,504 bytes, reserving 128,000/107,520. Small and shared workloads retain their original scratch usage. Repeated large-small-large operations verify stable warmed backing and preserved caller-owned storage; the pending vector releases handles after every material.

Twelve regressions cover fresh validation, first ownership/order, aliases, promotion, seeded prefixes, pass and shadow failure differences, current resource replacement, stable reuse and unwind cleanup. All 229 ECS graphics cases pass in dbg/opt and 232 in fin; frame, descriptor-buffer and skinning smoke targets build in all three. Benchmark timers cover the real gather or shadow resolve/merge operation and scratch teardown; inputs and assertions are outside.

## Compute emulation output alias validation

Material, AVBOIT, interval CSG, and receiver CSG capture now share an exact output-identity index. Each operation borrows scratch from its owning caller, uses an inline set for up to 32 identities, and builds a scratch hash set only when needed. Buffer pointers and descriptor heap slots retain separate identity domains. Receiver matching rebuilds its lookup from the currently validated public rows, so later row mutations still receive the original cross-stream intersection check. The change removes two duplicate linear-scan helpers without retaining pointers or scratch across frames.

Eight functional regressions cover duplicate pointers and slots, regular/receiver intersections, mirrored-row mutation, the inline-to-index transition, failure behavior, and repeated use. All 229 ECS tests pass in dbg and opt, and all 232 pass in fin; the frame, pipeline, descriptor test, and skinning benchmark targets also build in every configuration.

The following medians use seven alternating before/after process pairs after warmup on Windows ARM64. Each large workload captures 1,024 draws twice, except the mutated intersection workload, which performs four checks. Values are total CPU milliseconds for that workload, not frame times.

| Operation | dbg before / after | opt before / after |
| --- | ---: | ---: |
| Regular capture | 12.8552 / 1.5696 | 0.3575 / 0.1354 |
| AVBOIT capture | 9.8777 / 1.8631 | 0.3651 / 0.1426 |
| Interval capture | 10.2332 / 2.0916 | 0.4138 / 0.1728 |
| Receiver capture, 1,024 regular and 1,024 receiver draws | 42.8948 / 3.0175 | 1.0997 / 0.2858 |
| Receiver matching | 23.7322 / 0.5563 | 0.4909 / 0.0707 |
| Mutated receiver intersection checks | 47.3787 / 1.1051 | 0.9813 / 0.1182 |

Persistent plan peak storage is unchanged. The large regular index uses 63,568 scratch bytes in dbg and 63,488 in opt; AVBOIT and interval use 95,392 / 95,232 bytes, and receiver capture uses 129,120 / 129,024 bytes. Up to 32 total indexed identities require no scratch allocation. Singleton opt capture costs increase by at most about 5 ns per call. At 32 draws per stream, receiver capture indexes 64 identities and adds about 573 ns per opt capture; receiver matching adds about 188 ns per check. These small-input costs accompany substantial dbg and large-input improvements. Unchanged non-receiver matching routines are not credited with a speedup.

## Bunch transient allocation sizing

Bunch item and nested asset-reference paths reserve their complete checked length before appending. The three declaration/cycle/export lookup tables now construct their initial bucket storage directly, retaining the existing 0.5 load-factor capacity. Constructing an empty table and then reserving replaced its debug iterator proxy below the new allocation; the stranded proxy blocked subsequent LIFO reclamation even inside one chunk. These changes eliminate both string growth buffers and that temporary-table replacement.

A public regression expands and clears long exported and nested-reference paths sixteen times on the same caller arena, with 3 and 20 declarations to exercise both declaration lookup forms. It checks exact identities, reference text, export order, metadata retirement, caller sentinel bytes, and stable used/reserved memory. All 61 selected asset tests and the pipeline build pass in dbg, opt, and fin.

Seven alternating process pairs after warmup measured the unchanged public expansion workloads before the separate scratch chunk-stack fix. The 1,024-export workload (three expansions, 2,049 declarations) retains 6,071,296 -> 3,016,704 scratch backing bytes in dbg and 2,400,256 -> 2,065,408 in opt. The 256-export workload retains 1,516,544 -> 755,712 bytes in dbg and 598,016 -> 517,120 in opt. The four-export repeated workload retains 283,584 -> 12,672 bytes in dbg and remains 7,680 bytes in opt. These are retained backing measurements, not live leaks or a claim that every arbitrary scratch allocation order is reclaimable.

Timing is broadly unchanged: the large workload measures 94.3767 -> 95.2875 ms in dbg and 4.2394 -> 4.1234 ms in opt; the medium workload measures 23.2403 -> 23.5156 ms and 1.0009 -> 0.9880 ms. The benefit claimed here is lower temporary memory retention. The long-path regression isolates one chunk per alignment; cross-chunk LIFO reclamation is validated as a separate allocator item.

## Scratch chunk reclamation and reuse

ScratchArena now keeps an active chunk stack and a separate list of empty reusable chunks for each alignment. Freeing or shrinking the active top exposes the preceding live chunk. Relocation copies before unlinking an emptied former top beneath its replacement. Chunk acquisition secures replacement backing before retiring undersized cached chunks; new backing retains a nondecreasing size high-water mark. The existing single chunk link serves both disjoint lists, so chunk metadata does not grow. Out-of-order frees remain no-ops until arena destruction, and raw/typed zero-size behavior remains unchanged.

The old allocator failed the new reverse-free regression: three reverse frees left 320 bytes occupied and recorded only one deallocation. Ten regressions now cover cross-chunk and independent-alignment stacks, caller sentinels, zero requests, in-place and cached relocation, failed relocation, mixed large/small reuse, nested container unwind, and live/retired allocation-owner telemetry. All 163 global tests pass in dbg, opt, and fin; 163 global and 20 parser tests also pass in namesym opt. The memory telemetry payload remains version 1.

Seven alternating benchmark pairs after warmup measured complete scopes, including destruction, with snapshots after 1, 2, 4, 8 and 16 calls. The 4,096-element collection workload previously retained 33,072 used bytes in dbg and 33,024 in opt after each call; it now returns to the caller's 272 / 256 bytes. Peak usage falls from 197,088 -> 164,288 bytes in dbg and 196,976 -> 164,208 in opt. Cached backing remains 263,168 / 262,656 bytes and neither version allocates new heap backing after the first call. Sixteen calls measure 3.5713 -> 3.5616 ms in dbg and 0.4760 -> 0.4869 ms in opt, so no general timing improvement is claimed.

The mixed-alignment workload now returns to zero used bytes instead of retaining 2,880 bytes. Peak usage falls from 22,144 to 19,264 bytes; cached backing remains 37,888 bytes with no heap allocations after warmup. Its 24,576 allocation/free pairs measure 1.9007 -> 2.1079 ms in dbg and 0.1900 -> 0.1979 ms in opt. The corrected stack transitions add about 8 ns per pair in dbg. This is a reclamation and reuse correctness fix, with measured costs stated explicitly.

Broad validation passed 974 tests in dbg and opt and 977 in fin across allocation, parsing, crash, task-graph, telemetry, renderer and selected asset suites, plus two CLI integration suites per configuration. Native descriptor/graphics coverage passed 377 tests with 38 capability skips in dbg, 376 with 39 skips in opt, and 365 with 37 skips in fin. All 50 source-policy checks pass. Frame, pipeline, utilities, logserver, loader and skinning benchmark targets build in every configuration; the skinning asset pipeline cooked and gathered 110 assets.
