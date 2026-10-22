# Exact-full refraction tile mapping qualification â€” October 22, 2026

**Qualification complete: retain the exact full-occupancy cooked-kernel specialization. Grid candidate rejected; one-source list isolation inconclusive.** All specialized correctness and final regression gates passed. Its complete 32-trial experiment records a lower combined effect in all six dense1600 primary blocks, with paired interval below zero, while lower-resolution and empty-guard performance remain unresolved. The magnitude at 1600 is confounded by substantial unchanged-control movement; no whole-frame percentage is attributed entirely to the shaders. The earlier grid and list evidence remains preserved separately, including unfavorable outcomes and outliers. This is a bounded host/fixture qualification, with no portable speedup, CPU, FPS or lower-resolution noninferiority claim.

The reference is merged tiled renderer `f09f519cac85324f0c686100077969b27da27f5c`. Raw evidence belongs under `__artifacts/refraction_dense_mapping_qualification/`.

## Retained implementation contract

The retained implementation uses **separate cooked dense and sparse resolve kernels**, selected entirely by GPU-produced indirect arguments. A **32-byte** argument buffer stores sparse XYZ at byte 0, active count at byte 12, dense XYZ at byte 16, and reserved zero at byte 28. After the original classifier, an explicit UAV barrier precedes a one-workgroup finalizer inside the existing classification task/scope. The finalizer enables dense only when `activeCount == tilesX * tilesY` and `tilesX <= 65535`; every other occupancy uses the original sparse worklist mapping. Resolve records both indirect dispatches at offsets 0 and 16 inside its existing task/scope, with one nonempty route (both zero for empty coverage).

Dense screen and hardware CSG-disabled/enabled kernels use native `groupId.xy * 8 + lane.xy`, with no tile-list/argument reads or mapping division. Their three sparse counterparts preserve the original 65,535-capped queue geometry and reject counter tails before lookup. Graph stages and optical behavior remain unchanged. There is no CPU readback, density heuristic or adaptive threshold. The comparison is against original merged tiled renderer `f09f519ca`, with identical compiled scene/timing fixture inputs and deliberately different production shader/C++ binaries. Phase-three identities, costs, limits and completed qualification are recorded below.

## Rejected natural-grid candidate contract

When the GPU active-tile count equals `tilesX * tilesY`, every tile is admitted. The rejected natural-grid candidate used `rowWidth = min(tilesX, 65535)` consistently in producer and consumer. Each active classifier group published monotonic maxima `X = min(queueIndex + 1, rowWidth)` and `Y = queueIndex / rowWidth + 1`. Resolve reconstructed `queueIndex = groupId.y * rowWidth + groupId.x`. A fully active queue whose tile row fit the cap mapped the native `groupId.xy` directly to its tile, removing worklist lookup and runtime tile-index division. Wider capped rows kept bounded row-major reconstruction; a partial queue retained its worklist lookup. This producer geometry is not the retained specialization.

That grid variant had no coverage threshold, adaptive fallback, CPU readback, new GPU stage, racing last-lane argument rewrite, or optical change. Its indirect arguments used monotonic atomic maxima without a finalization dispatch or overwritten final-writer assumption. Counter rejection preceded queue lookup. Partial queues could launch fewer than one row of excess groups, rejected before any tile access.

For that grid experiment, both arms retained the merged clear/classify/resolve graph and bounded indirect dispatch. Its source manifests differed in exactly `impl/assets/graphics/refraction/resolve_common.slangi` and `impl/assets/graphics/refraction/classify_tiles_cs.slang`; compiled fixture and native verifier instrumentation matched in both arms, as did their executables. Authored shader resources differed after recooking those sources. The explicit freeze helper snapshots the two compiled fixture CPPs, classifier/shared constants, refraction production consumers, native probe files, and core frame code even when unchanged from the branch base.

The initial one-source list-index variant used `tile = queueIndex` for a full queue while retaining the old narrow dispatch geometry. Before any performance acquisition, code inspection led to the native-grid refinement above, removing tile division and restoring the ordinary two-dimensional full-frame tile grid. At the 257Ã—257 probe extent, full launch capacity became 66,049 groups rather than 131,070. This was a premeasurement design refinement, not a fitted response to timing data. The first variant's frozen/native/image evidence remains under its original paths; the grid variant received fresh arms and forty new images.

## Working diagnosis from prior evidence

The prior 1600Ã—900, fully dense hardware six-block comparison did not establish a scheduling regression or gain. Resolve dominated its variability: retained interval means ranged from 6.020 to 7.425 ms, while both arms' trial means ranged from 6.056 to 7.051 ms. There was no consistent first-to-last interval trend. All twelve trials loaded the same 112,965,324-byte pipeline cache; none of seven cache volume identities changed. This supplies no cold-cache explanation, and statistical uncertainty is not a renderer bug.

The older full-frame-reference comparison measured direct resolve means of 6.228/6.231 ms versus compact-worklist means of 6.796/6.974 ms in its two dense high-resolution blocks. That motivates isolating worklist ordering and lookup, but two blocks do not prove the cause or a portable effect. Current dense resolve is about 6.6 ms of a 15.8 ms GPU frame; clear plus classification is about 0.074 ms. Mapping/traversal work has greater potential ROI than tuning that small classifier scope.

A distinct earlier 50% candidate outlier affected many stages: one retained publication contained two GPU frame samples averaging 428.451 ms per frame, 214.374 ms resolve, 25.655 ms shadow visibility, and 47.194 ms lighting. That broad event is retained evidence, not a classifier defect, and is not deleted to improve the old result. The exact-full helper experiment does not claim to fix that event or establish its underlying cause.

## Predeclared grid qualification

The original `validation-plan.json` is preserved. `validation-plan-grid.json` records the refinement and its UTC declaration before any dense-mapping performance measurement. It keeps six balanced blocks at 100% hardware coverage for 960Ã—720 and 1600Ã—900: two cells and 24 primary trials. Each trial retains twelve positive sealed reports after at least two target-size warmup reports and 100 completed GPU frames. The complete affected cost is argument clear + classification + resolve + compositor; whole-frame, individual scopes, unchanged controls, raw intervals, startup/cache state, and wall eligibility remain visible. The existing one-second callback stall gate is unchanged.

Before performance, require the native shared-helper probe with full/reversed worklist and partial/empty/boundary cases, empty/sparse guards, both arms' twenty GPU-validation density images, and twenty exact RGB pair matches. Performance trials perform no framebuffer readback, GPU validation, or snapshot diagnostics. The CLI uses `--baseline-scheduling tiled` so both arms must publish clear/classify scopes. No trial is replaced to obtain a preferred result, and no rejection rule is fitted after observing it.

## One-source variant â€” preserved for causal isolation

The initial one-source variant is retained under `frozen/`, `pair-verification.json`, and `helper-v1.slangi`. Its reversed-worklist negative control produced 66,048 ordinal mismatches with the original helper, while the first direct-list-index helper passed the six-phase GPU-validation probe. That control establishes observability of the new mapping contract; it does not mean the old renderer had incorrect optical coverage. No performance run of that variant preceded the grid experiment, and none of its evidence is pooled into the grid comparison.

## Grid variant identities and correctness

`frozen-grid/baseline` and `frozen-grid/candidate` each contain twenty explicit/changed source entries. `pair-verification-grid.json` confirms exactly two shader source differences: the shared resolve helper and classifier. All compiled fixture/probe sources and all binaries are identical. Both arms were rebuilt with the same new grid-verifier shape contract before freezing. Source admission accounts for Git LF versus checkout CRLF while copying/hashing the actual bytes unchanged.

| Grid identity | Original merged tiled | Exact-full native grid |
| --- | --- | --- |
| Refraction executable SHA-256 | `22fb6f4307155692b85f7058d3d8c64ab0a8039421ed2c2eba39906cd0e9b774` | identical |
| Native probe executable SHA-256 | `ee3b3e04e190aa81c1a7796573187f32a03a7d35c9d5ed8ecbe26b457edec875` | identical |
| Source-manifest SHA-256 | `bc811e0e8288a17b7b051e69dadf0e24974938327c260a70d0909d40fdee93f9` | `45deb0bb677bf1aedd56f61966fe8e3d3ed0aabd3bbb19f4df8272cf8d5336b2` |
| Authored volume bytes | 81,519,744 | 81,521,948 |
| Authored-volume SHA-256 | `02b1dec344cc5f3e8705805f1e1e0b9b92439b1eb1d50acd63ec56ec35e0a9bd` | `c2f27b48cd312f162d6a26b128c0e6d544dac85aeb1dc63054506208e55cd9c0` |

The authored volume is `res/eda18e57f4796c04.vol`. Eight packed files differ in the freeze identity maps: this single authored volume and seven mutable pipeline-cache segments, classified using the existing exact cache-volume naming helper. `runtime-difference-classification-grid.json` lists them. Cache differences are not presented as eight changed authored shader archives; startup/cache state remains separate from authored runtime admission.

The production classifier/shared-helper native probe passed with GPU validation (`native-probe-grid.log`, 2.47 seconds CTest wall time). It uses a 2049Ã—2049 image and six successive queue occupancies, validating arguments and guards, membership/uniqueness/tails, full-resolution pixel visits, clipped edge lanes, and dense order against a reversed list. The near-full phase must retain queue lookup. The checked dispatch shapes are:

| Active tiles | Grid arguments XÃ—Y | Launched groups | Rejected tail groups |
| ---: | ---: | ---: | ---: |
| 66,049 | 257Ã—257 | 66,049 | 0 |
| 0 | 0Ã—1 | 0 | 0 |
| 6 | 6Ã—1 | 6 | 0 |
| 65,535 | 257Ã—255 | 65,535 | 0 |
| 65,536 | 257Ã—256 | 65,792 | 256 |
| 66,048 | 257Ã—257 | 66,049 | 1 |

Fresh density image qualification passed in both arms: `images-grid-baseline/` and `images-grid-candidate/` each contain twenty captures with GPU validation, independent rendered-coverage/optical checks, exact extents, and actual route diagnostics. `density-image-parity-grid.json` records twenty exact full-resolution RGB matches. These include empty and sparse correctness guards; analytical fixture markers alone cannot satisfy the image oracle. `correctness-gates-grid.json` records admission before timing. `cpu-analysis.log` separately records all eight timing/refraction analysis suites passing in 2.99 seconds.

### Predeclared empty/sparse timing guard

Before any grid timing, `validation-plan-grid-guards.json` declared two additional descriptive cells at 1600Ã—900 screen-space: 0% and 1%, with two balanced blocks each (**eight trials**). This targets the changed geometry directly: at 1%, 240 active tiles with row width 200 launch 400 groups, including 160 early-rejected tails; the old dispatch launched 240 groups. At 960Ã—720 the same percentage gives 120 tiles with row width 120 and no extra tail, so the higher extent is the more informative minimal guard. The empty case checks full classifier/argument overhead.

The acquisition order is primary24 followed by guard8, serially after correctness gates and without builds or other GPU workloads. `run_grid_guards.py` emits the exact command, with explicit `--execute` required to launch. It uses the same frozen arms, `--baseline-scheduling tiled --blocks 2 --extent 1600x900 --coverage 0 --coverage 1 --trace-route screen`, and fresh `timing-guards-grid/` output. These two-block guards retain every interval, block, and control; they introduce no posthoc cutoff or noninferiority claim and are not pooled with dense primary cells.

The emitted production shader inspection passed in `grid-mapping-cfg-proof.json`. `emitted-grid-baseline/` and `emitted-grid-candidate/` retain modules extracted from the actual packed authored volume and checked byte-for-byte against the corresponding cook-cache payloads. Inspection covers screen-space resolve, hardware resolve with CSG disabled and enabled, and the classifier. In all three resolve modules, the queue load is guarded by both counter-tail rejection and the exact-full branch; dense and rejected-tail control-flow edges cannot reach it, while valid sparse edges can. The full native group-XY tile-to-pixel block contains no tile-index integer division/modulo. The classifier uses the bounded natural row width in its atomic argument publication. This is **SPIR-V control-flow evidence, not native GPU ISA or timing evidence**; no driver lowering or runtime cost is inferred from it. `volume-differences-grid.json` independently confirms the one-authored/seven-cache classification.

The additional ordinary/lifecycle/CSG final regressions were not executed: the grid candidate was rejected by its declared performance experiment. Their prepared runner remains an artifact; it is not reported as a completed gate or a failed test.

## Complete grid primary measurements

`timing-primary-grid/report.json` confirms both complete six-block cells and all 24 trials on the Qualcomm Adreno X2-90 GPU. The admitted fully dense signatures contain 691,200 pixels/10,800 tiles at 960Ã—720 and 1,440,000 pixels/22,600 tiles at 1600Ã—900, including clipped bottom tile lanes. `summary-primary-grid/` contains the independently reconstructed summary, every paired block difference, trial/report/frame counts, raw interval counts, and control ranges. All values below are GPU milliseconds per completed frame; delta is candidate minus original merged tiled reference. The combined effect includes argument clear, classifier, resolve, and compositor. It does not omit dispatch preparation. Whole-frame includes all graph work.

| Extent, 100% hardware | Reference effect | Candidate effect | Effect delta [paired 95% interval] | Reference frame | Candidate frame | Frame delta [paired 95% interval] |
| --- | ---: | ---: | --- | ---: | ---: | --- |
| 960Ã—720 | 3.420523 | 3.452619 | +0.032096 [âˆ’0.069314, +0.097491] | 8.516047 | 8.476404 | âˆ’0.039643 [âˆ’0.233102, +0.109578] |
| 1600Ã—900 | 6.896735 | 7.229991 | **+0.333256 [+0.081998, +0.614901]** | 15.801114 | 16.234406 | **+0.433292 [+0.158272, +0.810053]** |

The six paired block differences are the independent units for the deterministic 10,000-draw percentile bootstrap (seed 0). Intervals are per metric and cell, without a family-wide confidence claim. They retain every block and do not establish a host measurement floor. A zero-crossing interval establishes neither equivalence nor noninferiority.

| Block, in acquisition order | 960 effect delta | 960 frame delta | 1600 effect delta | 1600 frame delta |
| ---: | ---: | ---: | ---: | ---: |
| 1 | +0.024746 | âˆ’0.102164 | +0.211836 | +0.165384 |
| 2 | +0.078313 | +0.125995 | +0.154668 | +0.130120 |
| 3 | +0.087620 | âˆ’0.007507 | +0.893440 | +1.293368 |
| 4 | +0.115733 | +0.056708 | +0.699748 | +0.649447 |
| 5 | +0.097725 | +0.178139 | âˆ’0.002976 | +0.154737 |
| 6 | âˆ’0.211560 | âˆ’0.489026 | +0.042820 | +0.206696 |

Combined effect increased in five of six blocks at each extent; the 1600 whole frame increased in all six. At 1600, resolve accounts for most of the observed affected-cost increase: **6.606175 â†’ 6.931459 ms**, delta **+0.325284 ms**, paired interval **[+0.074062, +0.603030] ms**. Its result does not support the proposed dense speedup. At 960, resolve is 3.275191 â†’ 3.308658 ms, delta +0.033468 ms, interval [âˆ’0.058924, +0.093056] ms.

| Extent | Argument clear reference â†’ candidate | Classifier reference â†’ candidate | Compositor reference â†’ candidate |
| --- | --- | --- | --- |
| 960Ã—720 | 0.001167 â†’ 0.001204 | 0.039313 â†’ 0.041356 | 0.104851 â†’ 0.101400 |
| 1600Ã—900 | 0.001654 â†’ 0.001643 | 0.072344 â†’ 0.078127 | 0.216562 â†’ 0.218763 |

Unchanged controls also move, so the whole-frame delta is not attributed entirely to the mapping. At 960 all four control intervals cross zero. At 1600 the lighting and tiny opaque-control intervals are above zero, while shadow and present cross zero. The control means and ranges are retained without applying a posthoc drift threshold:

| Extent | Control | Reference | Candidate | Delta | Paired 95% interval |
| --- | --- | ---: | ---: | ---: | --- |
| 960Ã—720 | Opaque | 0.007882 | 0.007667 | âˆ’0.000215 | [âˆ’0.000497, +0.000028] |
| 960Ã—720 | Shadow visibility | 1.891713 | 1.878307 | âˆ’0.013406 | [âˆ’0.032541, +0.005058] |
| 960Ã—720 | Deferred lighting | 0.098743 | 0.097735 | âˆ’0.001007 | [âˆ’0.002082, +0.000605] |
| 960Ã—720 | Deferred present | 0.042687 | 0.040720 | âˆ’0.001966 | [âˆ’0.005242, +0.000424] |
| 1600Ã—900 | Opaque | 0.007674 | 0.007869 | +0.000195 | [+0.000002, +0.000523] |
| 1600Ã—900 | Shadow visibility | 3.925596 | 3.959805 | +0.034209 | [âˆ’0.062859, +0.131276] |
| 1600Ã—900 | Deferred lighting | 0.203919 | 0.206957 | +0.003038 | [+0.000578, +0.005638] |
| 1600Ã—900 | Deferred present | 0.074537 | 0.075498 | +0.000961 | [âˆ’0.000691, +0.002720] |

The acquisition retained **288 positive intervals and 12,711 completed GPU frame samples**, with 347â€“707 frames per trial: 144 intervals/8,272 frames at 960 and 144 intervals/4,439 frames at 1600. All trials satisfied GPU admission, but acquisition is **not entirely clean**. The 1600 candidate in block index 5, position 1 (`block_05_01_candidate`) has a **1,038.068 ms** retained callback maximum, exceeding the predeclared 1,000 ms wall gate. Its wall comparison is rejected; its twelve GPU intervals and 347 GPU frames remain in every table above. The longest retained publication interval is 1.2892 seconds. No interval, outlier, or trial was removed or rerun.

Before/after identities for all seven mutable pipeline-cache files remain unchanged in all 24 primary trials. The runner separately admits fixed executable/authored-resource identities, requested extent, fixture and actual hardware route. Target-size confirmation took 0.687â€“0.895 seconds; collecting all retained reports finished 8.961â€“10.859 seconds after launch. These startup/acquisition observations are separate from the per-frame GPU metric. Neither unchanged cache files nor clean scope admission proves fixed clocks, scheduling, or a native GPU instruction-level cause.

## Complete empty/sparse guards

`timing-guards-grid/report.json` confirms all eight predeclared trials and `summary-guards-grid/` retains their full analysis. These are two-block descriptive cells; they are not added to the dense primary or used to offset its unfavorable result.

| 1600Ã—900 screen coverage | Reference effect | Candidate effect | Effect delta | Effect block deltas | Reference frame | Candidate frame | Frame delta |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: |
| 0% | 0.206474 | 0.205761 | âˆ’0.000713 | âˆ’0.003817, +0.002391 | 5.563948 | 5.433840 | âˆ’0.130109 |
| 1% | 0.232327 | 0.232091 | âˆ’0.000236 | +0.000734, âˆ’0.001206 | 6.117649 | 6.109342 | âˆ’0.008307 |

The sparse resolve itself increased 0.024090 â†’ 0.024324 ms (+0.000234 ms), with both block differences positive (+0.000305 and +0.000163 ms); the combined effect mixes small opposing scope changes. Control movement remains visible: at 0%, shadow averages 1.703398 â†’ 1.673754 ms with paired differences âˆ’0.066022 and +0.006734 ms; lighting differences are âˆ’0.006495 and +0.001876 ms. At 1%, shadow differs âˆ’0.001702 ms on average and lighting âˆ’0.000018 ms, with opposing block signs in each. No confidence or noninferiority claim is made from these two blocks.

All **96 positive intervals and 8,195 GPU frame samples** are retained. All eight guard trials satisfy the acquisition and wall gates: the maximum callback is 16.312 ms and maximum retained publication interval 0.5056 seconds. The raw signatures admit zero pixels/tiles at 0% and 14,400 pixels/240 tiles at 1%. All seven cache identities remain unchanged around each guard trial. The twenty exact paired image comparisons above establish image correctness independently of these analytical signatures.

## Decision and separate isolation

**Reject the two-source natural-grid candidate.** It passed the declared correctness and emitted-SPIR-V checks, but the intended dense1600 improvement is contradicted by the retained primary measurements. The combined effect and resolve intervals lie above zero, and all six whole-frame block differences are positive. The lower-resolution evidence does not establish a gain or noninferiority. Clean descriptive empty/sparse guards do not reverse that decision. No shipping threshold, adaptive policy, or favorable retry is introduced, and the grid artifacts are preserved unchanged.

The next causal check was the already frozen **one-source list-index variant**, retaining the original classifier and 65,535-capped argument geometry. That separates mapping/order from the natural-grid producer change without introducing another architecture. Before its performance acquisition, `validation-plan-list-isolation.json` declared **1600Ã—900, 100% hardware, six balanced blocks/twelve trials**, with the same twelve positive intervals, warmup/frame minima, controls, cache/identity admission and wall-stall rule. `run_list_isolation.py --execute` targeted a fresh `timing-isolation-list/`. Both v1 arms passed twenty GPU-validation images, with twenty exact RGB pairs in `density-image-parity-list.json`; `correctness-gates.json` admits their retained native probe and image gates. Its source manifests differ in only the shared helper, and binaries match.

### Completed one-source list isolation

`timing-isolation-list/report.json` confirms all twelve trials completed with clean acquisition; `summary-isolation-list/` independently reconstructs the full evidence. Both arms retained the original classifier and 65,535-capped dispatch geometry. Only the shared helper changed, with identical executables and compiled scene/timing fixture inputs. Its twenty candidate GPU-validation images and twenty exact RGB pairs passed before acquisition.

| 1600Ã—900, 100% hardware metric | Reference | List candidate | Delta | Paired 95% interval |
| --- | ---: | ---: | ---: | --- |
| Complete effect | 6.935974 | 6.748267 | âˆ’0.187707 | [âˆ’0.566989, +0.282257] |
| Resolve | 6.643562 | 6.447319 | âˆ’0.196243 | [âˆ’0.567724, +0.265280] |
| Whole GPU frame | 15.939396 | 15.977041 | +0.037645 | [âˆ’0.697368, +0.858738] |

The effect block differences are âˆ’0.858354, âˆ’0.271377, âˆ’0.526532, +0.015741, +0.845028, and âˆ’0.330750 ms. Whole-frame differences are âˆ’1.421005, âˆ’0.131913, âˆ’0.385880, +0.026269, +1.765911, and +0.372487 ms. These large opposing outcomes remain included. The point estimate does not establish a speedup, safety bound, or noninferiority.

Control variation prevents treating the whole-frame difference as an isolated mapping effect. Shadow visibility means are 4.022626 â†’ 3.978585 ms, with paired differences ranging âˆ’0.431157 to +0.493849 ms. Lighting means are 0.207910 â†’ 0.212405 ms, paired range âˆ’0.007384 to +0.018846 ms. Present means are 0.074844 â†’ 0.076429 ms. The affected compositor itself increases 0.217486 â†’ 0.224166 ms (+0.006680 ms, paired interval [+0.001463, +0.011710] ms). All control and affected-scope ranges are retained, without fitting a drift cutoff.

All **144 positive intervals and 4,479 completed GPU frame samples** remain included. No trial failed the wall gate: maximum callback is 37.992 ms, maximum retained publication interval 0.5198 seconds. All seven cache identities are unchanged around each trial. Clean acquisition and unchanged caches do not imply a stable GPU scheduling or clock floor. **This one-source variant is inconclusive and is not qualified for shipping.** It does not replace the rejected grid result or justify retrying it until favorable.

## Phase three â€” specialized cooked kernels

`validation-plan-specialized.json` records the next contract before its build or timing. The candidate separates dense and sparse mappings into cooked kernels. Its 32-byte GPU argument buffer contains sparse XYZ at byte 0, active count at byte 12, dense XYZ at byte 16, and reserved zero at byte 28. Classification is followed by a one-group GPU finalizer after an explicit UAV barrier, recorded in the existing classification task and measured inside that scope. The finalizer selects dense only for exact full coverage with `tilesX <= 65535`; all other cases retain the original sparse 65,535-capped mapping. Resolve records two indirect dispatches inside its existing task/scope, with exactly one route nonempty. No CPU density threshold, adaptive heuristic, extra graph stage, or optical change is introduced.

This tests a different concrete implementation: eliminating mapping alternatives from each cooked resolve kernel rather than relying on a runtime branch. Emitted SPIR-V, native argument/route tests and timings must substantiate it; reduced source branches alone establish no performance benefit. The primary metric includes the finalizer through classification and both indirect records through resolve, plus argument clear and compositor. Whole-frame work remains visible.

The declaration keeps **six balanced blocks at 100% hardware coverage for 960Ã—720 and 1600Ã—900 (24 trials)**, followed by the same **1600Ã—900 screen 0%/1% two-block guards (eight trials)**. It requires native classifier/finalizer/consumer checks of both routes, stale clearing and sparse/full/near-full/bounded cases, twenty GPU-validation images per arm and twenty exact RGB pairs before performance. Trial warmup, twelve positive retained intervals, frame minima, controls, wall rejection, raw outlier retention and no-readback acquisition remain unchanged. No earlier variant data is pooled into this comparison.

`prepare_specialized.py` provides explicit freeze, verify, command-display and run actions under fresh `frozen-specialized/`, `timing-primary-specialized/` and `timing-guards-specialized/`. Its reference reuses the actual `frozen-grid/baseline` binaries/resources without rebuilding. The derived copy must match that arm's current binary/resource bytes exactly. Only canonical mutable pipeline-cache paths may differ from the original freeze identity; the upstream identity manifest and its checksum are preserved alongside fresh derived identities. Authored-resource or executable changes cannot be excused as cache warming. The original twenty-entry source evidence is retained, including the native six-phase grid verifier that was actually compiled in that reference. Additional baseline production inputs are copied from revision `f09f519ca` and marked as Git blob representations. The specialized candidate will have a different native probe ABI; probe identity is recorded separately and is not claimed identical to the reference. The two density scene/timing fixture CPPs must match exactly. Deliberate production C++/asset and executable changes are expected, admitted only within the declared refraction pipeline/target files, and fully listed for review.

### Specialized identities and completed admission

`pair-verification-specialized.json` admits twelve production source/asset changes and seven test/documentation/reference-patch changes, with exactly identical `refraction_project.cpp` and `avboit_timing_render_pass.cpp`. Production changes comprise the finalizer shader/metadata, shared ABI/mapping and resolve variants, shader names, pipeline state and prepared graph resources, classification/finalizer recording, and dual indirect resolve recording. The original classifier remains unchanged. The separate refraction and probe executables/PDBs deliberately differ; executable equality is not claimed for this phase.

| Specialized identity | Original merged tiled reference | Specialized candidate |
| --- | --- | --- |
| Refraction executable SHA-256 | `22fb6f4307155692b85f7058d3d8c64ab0a8039421ed2c2eba39906cd0e9b774` | `a59aefc18de3d79680174174b7f9d86d28badc5df9ece14ddf35776b12838cba` |
| Native probe executable SHA-256 | `ee3b3e04e190aa81c1a7796573187f32a03a7d35c9d5ed8ecbe26b457edec875` | `86958a17bcf36319a0200a7bc32f907c9b203379100bd48d4f735973f829e710` |
| Source-manifest SHA-256 | `98bafd68eb1abf87e70472541388539b294857decaa6b91c674ca3fbd8e18183` | `3f3b3c8de00cb812e04579265e4f2569518eb9dec2e55b17868ae10d729d50f0` |
| Source entries | 38, preserving the original twenty compiled-input entries | 40 |
| Authored-volume bytes | 81,519,744 | 82,189,125 |
| Authored-volume SHA-256 | `02b1dec344cc5f3e8705805f1e1e0b9b92439b1eb1d50acd63ec56ec35e0a9bd` | `055dde99597c689dc9fb182665a047c52b47726d8779b68f36011c10da8336e3` |

The authored file is `res/eda18e57f4796c04.vol`; the other seven differing packed files are canonical mutable pipeline-cache segments. The derived baseline provenance preserves the original source/identity manifest checksums and records exact current executable/authored-resource equality. Thirty-six generated logserver logs are retained and inventoried separately; they are not executables or authored resources. None was deleted to make identity admission pass.

The candidate's native GPU-validation probe passed (`native-probe-specialized.log`, 2.42 seconds for the CTest case, 2.52 seconds total CTest wall time). `native-probe-specialized-result.json` preserves its result: six phases at 2049Ã—2049 with 66,049 tiles, **all 32 argument bytes**, production finalizer and both specialized consumer routes, **25,190,406 pixel checks**, full output/sentinels and guards. Fullâ†’emptyâ†’sparseâ†’65,535â†’65,536â†’all-but-one occupancy checks exercise counter reset, mutually exclusive dispatch records, queue membership/uniqueness, reversed dense order, sparse retention, boundary tails and edge clipping. The finalizer selects full native dispatch only for exact occupancy; the all-but-one phase must remain sparse. This probe is the specialized candidate's proof, not a claim that the reused reference contains the new verifier or ABI.

Fresh image qualification completed in both `images-specialized-baseline/` and `images-specialized-candidate/`: **twenty GPU-validation captures per arm, all twenty full-resolution RGB pairs exact**, recorded in `density-image-parity-specialized.json`. Both extents, both routes and all five densities passed the independent optical/coverage oracle and runtime admission. `correctness-gates-specialized.json` records native both-route, empty/sparse, image parity and reviewed production-diff admission before timing. `cpu-analysis-specialized.log` separately records all eight timing/refraction analysis suites passing in 2.80 seconds.

### Specialized emitted-shader evidence

`emitted-specialized-candidate/` extracts the actual authored packed modules: classifier, finalizer, and six resolve variants (dense/sparse screen and dense/sparse hardware with CSG disabled/enabled). All eight pass SPIR-V validation and match their current source-checksum cook-cache payloads byte for byte. `specialized-mapping-cfg-proof.json` passes static SSA/control-flow checks. Each of the three dense modules has **zero tile-argument/list descriptor-member reads**, computes pixels directly as `groupId.xy * 8 + lane.xy`, and has no mapping modulo/division dependency. Each sparse module retains the original `groupId.y * 65535 + groupId.x` queue mapping, with the counter-tail guard dominating the list load.

The emitted finalizer has local size 1Ã—1Ã—1, selects dense using exact occupancy plus the 65,535 native-row cap, leaves active-count word 3 intact, and writes reserved word 7 to zero. Eight bounded integer emulations cover empty, sparse, 65,535/65,536 count boundaries, near-full/full, a full 65,535-wide tile row and a 65,536-wide fallback. These last extreme-width cases are **IR emulations, not enormous native framebuffer tests**. Source review separately checks the one-workgroup recording, explicit classification-to-finalizer UAV barrier, argument-buffer allocation, pipeline lifetime, prepared graph handles, and the two resolve indirect offsets. These are SPIR-V and source proofs; they establish no driver ISA, dynamic branch count, optical equivalence beyond the image gates, or speedup.

### Complete specialized primary

`timing-primary-specialized/report.json` confirms **24/24 trials and both six-block primary cells**, with clean acquisition in each. `summary-primary-specialized/` reconstructs every retained interval, paired block, affected scope and control from the saved evidence. Units and the six-block bootstrap method are the same as above. Combined effect includes argument clear, classification **including the GPU finalizer**, resolve **including both indirect records**, and compositor; no new preparation work is omitted.

| 100% hardware extent | Reference effect | Specialized effect | Effect delta [paired 95% interval] | Reference frame | Specialized frame | Frame delta [paired 95% interval] |
| --- | ---: | ---: | --- | ---: | ---: | --- |
| 960Ã—720 | 3.703472 | 3.814285 | +0.110813 [âˆ’0.428350, +0.720223] | 9.168439 | 9.986284 | +0.817845 [âˆ’0.587572, +2.347415] |
| 1600Ã—900 | 8.185654 | 6.679076 | **âˆ’1.506578 [âˆ’2.341054, âˆ’0.681629]** | 19.038753 | 15.926619 | âˆ’3.112134 [âˆ’5.406889, âˆ’0.927380] |

At 1600, resolve is **7.840278 â†’ 6.375925 ms**, delta **âˆ’1.464353 ms**, interval **[âˆ’2.256774, âˆ’0.685505] ms**. The complete effect and resolve decrease in all six blocks. The result supports an observed affected-cost reduction on this declared dense host fixture. Its magnitude is confounded by broad reference slowdowns and is not a portable shader speedup estimate. At 960, resolve is 3.542460 â†’ 3.634202 ms, delta +0.091743 ms, interval [âˆ’0.418034, +0.665078] ms. The 960 effect and whole-frame results remain uncertain; neither gain nor noninferiority is established there.

| Block, in acquisition order | 960 effect delta | 960 frame delta | 1600 effect delta | 1600 frame delta |
| ---: | ---: | ---: | ---: | ---: |
| 1 | âˆ’0.344876 | âˆ’0.252085 | âˆ’0.461851 | âˆ’0.352901 |
| 2 | +0.211893 | +1.216218 | âˆ’0.638189 | âˆ’0.526743 |
| 3 | âˆ’0.886378 | âˆ’1.765908 | âˆ’2.724486 | âˆ’6.693147 |
| 4 | +0.763646 | +2.321983 | âˆ’0.213954 | +0.261524 |
| 5 | âˆ’0.319482 | âˆ’0.344816 | âˆ’2.829455 | âˆ’6.502413 |
| 6 | +1.240076 | +3.731680 | âˆ’2.171535 | âˆ’4.859126 |

At 960, the candidate-first blocks (2, 4 and 6 in this one-based table) have positive effect deltas, while all three candidate-second blocks have negative deltas. The corresponding unchanged controls also tend to rise in candidate-first trials. The balanced plan retains this clear order/carryover association; it does not establish whether clock state, scheduling, thermal state or another host condition caused it. It supplies a concrete reason to keep 960 uncertain instead of selecting favorable blocks. At 1600 all six effect differences remain negative across both arm orders, but the broad reference events still prevent assigning their full magnitude to the specialization.

| Extent | Argument clear reference â†’ candidate | Classification/finalizer reference â†’ candidate | Compositor reference â†’ candidate |
| --- | --- | --- | --- |
| 960Ã—720 | 0.001328 â†’ 0.001549 | 0.041684 â†’ 0.050089 | 0.118001 â†’ 0.128445 |
| 1600Ã—900 | 0.001964 â†’ 0.002157 | 0.090694 â†’ 0.077119 | 0.252717 â†’ 0.223875 |

The classification label includes the finalizer only in the specialized arm; the reference has its original classifier. These unequal implementations are intentionally measured in the full affected-cost sum. Compositor is affected and remains an observation, not an unchanged control.

| Extent | Unchanged control | Reference | Candidate | Delta | Paired 95% interval |
| --- | --- | ---: | ---: | ---: | --- |
| 960Ã—720 | Opaque | 0.008007 | 0.009488 | +0.001481 | [+0.000519, +0.002479] |
| 960Ã—720 | Shadow visibility | 2.033913 | 2.303540 | +0.269627 | [âˆ’0.071136, +0.613971] |
| 960Ã—720 | Deferred lighting | 0.110556 | 0.143186 | +0.032630 | [âˆ’0.000215, +0.068130] |
| 960Ã—720 | Deferred present | 0.051480 | 0.071475 | +0.019995 | [âˆ’0.001566, +0.045191] |
| 1600Ã—900 | Opaque | 0.011389 | 0.008015 | âˆ’0.003374 | [âˆ’0.006695, âˆ’0.000209] |
| 1600Ã—900 | Shadow visibility | 4.679456 | 4.077585 | âˆ’0.601871 | [âˆ’1.159680, âˆ’0.044471] |
| 1600Ã—900 | Deferred lighting | 0.329708 | 0.219397 | âˆ’0.110310 | [âˆ’0.198344, âˆ’0.024743] |
| 1600Ã—900 | Deferred present | 0.130159 | 0.078716 | âˆ’0.051443 | [âˆ’0.104015, âˆ’0.006933] |

The control reductions at 1600, including unrelated lighting and presentation, rule out assigning the entire âˆ’3.112 ms frame observation to refraction. Large broad events are present in reference block indices 2, 4 and 5. For example, reference block 4 report index 6 has frame/resolve/shadow/lighting means of **22.813786/9.257805/5.437486/0.930991 ms** across 22 GPU frames. Reference block 4 report 14 reaches 23.647724 ms per frame, with present 0.457452 ms. The candidate also has a broad event: its block 4 report 16 records **21.373270/8.039552/5.220852/0.858030 ms** for frame/resolve/shadow/lighting across 23 frames. These events remain included, as do all lower-resolution opposing blocks. No filtered statistic, after-the-fact control cutoff, or favorable rerun is substituted.

Within retained 1600 intervals, resolve ranges **6.525577â€“9.633400 ms** for reference and **5.984348â€“8.039552 ms** for candidate; whole-frame ranges **15.851174â€“23.647724** and **14.746997â€“21.373270 ms**, respectively. At 960, resolve ranges **3.144358â€“5.038000** and **2.943368â€“4.769347 ms**. Acquisition cleanliness does not establish stable clocks or scheduling, and neither broad-event cause nor a driver-level mechanism is established by this experiment.

All **288 positive intervals and 11,708 completed GPU frame samples** are retained: 144/7,526 at 960 and 144/4,182 at 1600. All 24 trials pass the predeclared wall gate; maximum callback is **198.596 ms**, and maximum retained publication interval is **0.6293 seconds**. There are no wall-rejected trials in this phase. This does not turn callback timing into isolated CPU cost or presentation cadence.

`primary-specialized-interval-diagnostics.json` records startup and exact cache event timestamps for all trials. Runtime logs load caches at relative **0.413â€“0.477 seconds**, before the first sealed timing publication at **2.322â€“2.430 seconds**. Target-size confirmation is **0.585â€“0.810 seconds** after launch; all measured reports finish collecting at **8.889â€“10.689 seconds**. Each arm loads and saves its own stable cache: reference **112,980,392 bytes**, candidate **113,262,768 bytes** of runtime cache payload, with seven packed segments each. Before/after hashes match in every trial; physical segment totals include the packed header and are recorded separately. No cold-cache mutation is observed, but cache stability does not establish fixed clocks or explain the broad events.

### Complete specialized guards

`timing-guards-specialized/report.json` confirms all eight predeclared trials, and `summary-guards-specialized/` preserves all blocks, intervals, scopes and controls. Both cells have clean acquisition. These two-block results are descriptive and are not pooled with the dense primary or used to claim noninferiority.

| 1600Ã—900 screen coverage | Reference effect | Candidate effect | Effect delta | Effect block deltas | Reference frame | Candidate frame | Frame delta |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: |
| 0% | 0.242983 | 0.270503 | +0.027520 | âˆ’0.003910, +0.058950 | 6.520009 | 7.012455 | +0.492446 |
| 1% | 0.320401 | 0.312443 | âˆ’0.007958 | âˆ’0.007556, âˆ’0.008360 | 7.931674 | 8.053322 | +0.121648 |

Empty behavior remains unresolved. The second empty block puts the candidate first: its effect/frame means are **0.316043/7.917226 ms**, versus **0.257093/6.955817 ms** for the following reference. Shadow visibility concurrently rises **2.236810 â†’ 2.513958 ms**, lighting **0.287640 â†’ 0.448274 ms**, and present **0.091695 â†’ 0.118837 ms**. That is broad variation rather than an isolated finalizer observation. The first empty block has a small negative effect delta and almost unchanged shadow/lighting, while its whole frame is +0.023482 ms. Both blocks and the higher first-slot candidate remain included.

At 1%, both combined-effect differences are negative but both frame differences are positive (+0.191396 and +0.051900 ms). Shadow visibility averages **2.371918 â†’ 2.422944 ms** and lighting **0.307889 â†’ 0.323346 ms**. Sparse resolve itself rises **0.031826 â†’ 0.035743 ms** (+0.003916 ms, both block differences positive), while compositor falls **0.209119 â†’ 0.196965 ms**. At 0%, resolve rises **0.001758 â†’ 0.006384 ms**, while classifier/finalizer rises **0.063710 â†’ 0.069787 ms**. These are visible costs and mixed scope changes; the guard sum is not presented as proof of an empty/sparse speedup or safety margin. The guard averages cannot resolve the uncertain 960 result.

All **96 positive intervals and 6,565 GPU frame samples** are retained, with maximum callback **23.998 ms** and maximum retained publication interval **0.5101 seconds**. All seven cache identities remain unchanged around every guard trial. Together, the specialized primary and guards retain **32 trials, 384 positive intervals and 18,273 GPU frame samples**, with no acquisition failures or wall rejections. Statistical metrics remain separated by declared cell; these counts are not a pooled performance estimate.

### Final specialized regressions and decision

`regressions-specialized-final.json` records all three final regression gates passing on `frozen-specialized/candidate`: **three ordinary, fourteen lifecycle and seven CSG frame sets**, each with GPU validation and actual hardware admission. All return codes are zero, and executable/authored-runtime identities match before and after. Individual logs/directories are `regression-{ordinary,lifecycle,csg}-specialized-final`; `regressions-specialized-final.log` records their serial execution. The old grid runner was not used.

**Retain the exact-full cooked dense/sparse architecture.** Correctness is supported by the production finalizer/32-byte native probe, both-route image gates, twenty exact RGB pairs, emitted-module/source checks, and the ordinary/lifecycle/CSG regressions. The predeclared target dense1600 cell has negative complete-effect and resolve differences in all six balanced blocks, with paired intervals below zero. This supports the bounded decision while preserving all broad events and the rejected earlier implementations.

The observed **âˆ’1.506578 ms complete effect** and **âˆ’3.112134 ms whole frame** are not claimed as isolated or portable shader gains. Unchanged controls move substantially, and no driver-level cause is proven. **960Ã—720 and empty-screen performance remain unresolved**; their intervals or descriptive guards do not establish noninferiority. The 1% guard's mixed scope/frame behavior also remains visible. The implementation uses exact GPU occupancy rather than a fitted density threshold, and no adaptive policy, outlier trimming or favorable retry was added.

The next useful work is controlled profiling of the retained dense/sparse routes, with stable host acquisition and refraction query/fallback counters, to distinguish traversal cost and scheduling/clock behavior. Another coarse effect mask would not explain the observed order/control variation. No further performance run was selected here to improve the outcome. These GPU observations do not measure isolated CPU cost or presentation cadence and support no FPS claim.
