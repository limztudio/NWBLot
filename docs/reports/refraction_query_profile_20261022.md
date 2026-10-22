# Hardware refraction query profile - October 22, 2026

**Diagnostic and workload-profiling qualification is complete. All six diagnostic cells preserve exact RGB output, and the independently audited 32-trial uninstrumented campaign completed its full predeclared schedule.** The four nonempty slab workloads issue approximately 3.03 to 3.04 ordinary hardware queries per admitted pixel. Entry association succeeds for every pixel, exit tracing adds a small number of total-internal-reflection continuations, and every traced background is returned from the camera lighting cache. No call to the resolver's hit-lighting function or transparent-layer continuation is exercised by this fixture. Resolve dominates the affected timing aggregate, but broad control variation remains visible; this work establishes neither an optimization speedup nor a clock-state explanation.

The implementation being profiled is the specialized dense/sparse renderer merged as `f90c01b461834e961083165eda7f256ea493f797`. The new opt-in diagnostic implementation is developed on `perf/refraction-query-profiling`. Raw evidence belongs under `__artifacts/refraction_query_profile_20261022/`. The earlier mapping qualification is recorded in [refraction_dense_mapping_qualification_20261022.md](refraction_dense_mapping_qualification_20261022.md).

## Scope and separation of evidence

Diagnostic acquisition uses the public renderer opt-in and completed `RefractionStatistics` API. The ordinary density fixture renders one closed, clear slab against the fixed unlit stripe backdrop, with the same camera and light at 0%, 50% and 100% screen coverage and extents 960x720 and 1600x900. The zero-density case retains an admitted refractor behind the opaque wall while exposing no refractive pixels. Reflection is disabled. The positive percentages describe projected rectangle coverage, with even-pixel rounding, rather than a material opacity setting.

The diagnostic kernels accumulate the 29 counters in a 128-byte group-local array using atomics, then publish each word once per 8x8 tile. All 64 lanes participate in initialization and flush barriers, including clipped image edges and compact-queue tail groups. Local and destination counter overflow set an atomic overflow flag; any nonzero flag invalidates the captured statistics. The diagnostic code is excluded from the normal cooked kernels. CSG-enabled diagnostic variants preserve physical-query bookkeeping when interface policy rejects a geometric hit, without reconstructing a material for that rejection or changing optical decisions.

The graph clears the diagnostic UAV before resolve and copies it to a reserved readback afterward. Those tasks and the diagnostic resolve use the primary Graphics queue, retaining their handles and the exact accepted physical submission token. Three readback reservations allow asynchronous completion; when none is free, rendering continues without collecting a new sample. Rejected graph work releases unaccepted reservations, and stale completions cannot replace a newer publication. No renderer readback wait is introduced.

The test fixture waits for a completed statistics source from the requested framebuffer extent after at least sixteen target graphics frames. It retains sequence, generation, source frame, observed frame, extent, actual hardware readiness and physical submission token. A later framebuffer source from the same static scene is captured after statistics publication. In this run, all six statistics sources were graphics frame 16, observed at frame 20, with image source frame 20. Counters and image describe the same fixed scene and route; they are not claimed to come from the same GPU frame.

`diagnostics/refraction_query_diagnostics.json` retains the complete metadata, counter values, image checks, extent history and exact reference paths. All six accepted statistics blocks had valid physical tokens, hardware readiness, CSG disabled and zero overflow flags. The pixel oracle independently reconstructs the projected rectangle rather than accepting the fixture's admission log as proof. It also verifies bounded primary and background loops and the relationships among cache attempts, cache misses, material shading and terminal paths.

## Completed ordinary query counts

These are completed counts from one static source frame per cell. An ordinary closest-geometry operation issues exactly one inline hardware ray query, so logical stage totals and physical queries match in these CSG-disabled cells. The separate CSG query-budget and physical-query concepts are not collapsed into a claim about CSG work here.

| Extent | Requested coverage | Hardware pixels / entry queries | Exit queries | Background queries | Total physical queries | Queries per hardware pixel |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 960x720 | 0% | 0 | 0 | 0 | 0 | n/a |
| 960x720 | 50% | 345,780 | 359,698 | 345,698 | 1,051,176 | 3.040014 |
| 960x720 | 100% | 691,200 | 713,910 | 690,990 | 2,096,100 | 3.032552 |
| 1600x900 | 0% | 0 | 0 | 0 | 0 | n/a |
| 1600x900 | 50% | 719,952 | 747,794 | 719,610 | 2,187,356 | 3.038197 |
| 1600x900 | 100% | 1,440,000 | 1,486,292 | 1,439,486 | 4,365,778 | 3.031790 |

Every entry query associated with the captured primary entity. Entry association misses, wrong entities, entry failures and invalid entry directions were all zero. Exit misses were 82, 210, 342 and 514 respectively across the four nonempty cells. Those paths use the existing entry-based screen fallback and do not start background hardware queries.

| Extent | Coverage | TIR events | TIR capped pixels | Successful volume exits / cached opaque returns |
| --- | ---: | ---: | ---: | ---: |
| 960x720 | 50% | 13,918 | 0 | 345,698 |
| 960x720 | 100% | 22,710 | 0 | 690,990 |
| 1600x900 | 50% | 27,842 | 0 | 719,610 |
| 1600x900 | 100% | 46,292 | 0 | 1,439,486 |

**TIR counts are events, not distinct pixels.** The fixed primary loop allows two continuations after its first exit query, so one pixel can contribute more than one event. The finite slab has side faces; edge rays can encounter a side face and reflect internally before a later transmission. No path exhausted the TIR cap in this run. Exit-query conservation is exact: `exitQueries = associatedEntries + tirEvents`, because initial directions and TIR caps were zero. Successful volume exits equal associated entries minus exit misses.

For each successful volume exit, exactly one background query returned an opaque hit, and every camera-cache lookup succeeded. Background misses/failures, secondary glass, transparent hits, background layer caps, shaded opaque returns, material-shading calls and exit-based background fallbacks were all zero. This rules out additional background lighting evaluation and repeated transparent background continuation as explanations for this fixture's ordinary query count. It does not establish their costs in scenes that exercise them.

The source invokes `nwbRayTraceClosestSurfaceHit`, which queries geometry and constructs a material surface, **before** checking the lighting cache. A zero `materialShadeCalls` counter counts zero calls to `nwbRefractionShadeHit`; it does not establish zero material surface evaluation, attribute fetching, geometry traversal or cache-validation work. Entry and accepted exit helpers likewise construct surfaces in the source. Inlining, dead-code elimination and instruction sinking may already remove unused outputs or move some associated work. The full source-level struct does not prove that every field or fetch executes before the cache check, and this diagnostic audit does not measure material-decode cost.

At 1600x900 and 100% coverage, entry queries account for 32.984% of physical query operations, exits for 34.044% and backgrounds for 32.972%. These are operation-count shares. Different ray bounds, traversal paths, hit reconstruction and divergence can have different costs, so the shares are not a GPU-time decomposition.

## Optical and correctness gates

All six diagnostic images matched the previously qualified uninstrumented specialized-candidate references exactly, comparing **6,393,600 RGB pixels**. The reference layout is `__artifacts/refraction_dense_mapping_qualification/images-specialized-candidate/<extent>/hardware/<density>.bmp`. Independent image checks also confirmed the zero-density neutral stripe backdrop, complete projected glass coverage, no changed pixels outside the mask and the expected neutral Fresnel stripe levels in the slab's front/back core. Side-exit bands remain part of coverage checks while being excluded from the flat-interface Fresnel oracle.

Before timing acquisition, the repaired optimized build and asset cook completed for the refraction fixture, native probe and ECS graphics tests. The six-phase classifier/mapping probe passed with GPU validation in 3.12 seconds CTest wall time. Eleven CPU CTest suites passed, including the new completed-readback state regressions, diagnostic rejection oracle, timing-order/telemetry analysis and existing refraction image/capture tooling. The state tests cover ring saturation, exact-token matching, stale generations, mapping failure, graph rejection/reset and moved reservation ownership. These are separate from image parity and from performance evidence.

The final optical regression matrix passed **3 ordinary + 14 lifecycle + 7 CSG framesets**, all with GPU validation enabled. Required automatic-route cases confirmed actual accepted hardware dispatch; their screen-space and disabled controls retained the existing independent image checks. `regressions-diagnostics-final.json` records all three successful harness invocations and confirms that the frozen executable/dependencies and authored runtime were unchanged before and after those runs. The CSG captures exercise normal optical behavior with the diagnostic-capable build; they do not independently qualify CSG physical-query counter accounting.

The source-format audit passed for all 26 changed C++/authored shader/metadata files, and the refreshed direct-reference patch passed `git apply --check`. Extraction of all twelve cooked refraction modules completed, with each module passing SPIR-V validation and matching its source-checksum cache entry exactly. The final hash-bound SPIR-V control-flow/SSA audit also passed for all twelve modules, with classifier and finalizer witnesses retained in `diagnostics-cfg-proof.json`.

Across all four hardware diagnostics-disabled variants, the audit found no reads of the diagnostic selector, no diagnostic counter accesses or atomics, and no diagnostic workgroup array or barriers. All four diagnostics-enabled variants retain the 32-word workgroup array; both AcquireRelease group barriers dominate every main-function return. Only flush loads the diagnostic descriptor. Publication is guarded by lane index below 32 and nonzero word amount, with the byte offset derived from that lane's word; reserved words 29 through 31 remain zero. Workgroup-local counter atomics and overflow-flag propagation were checked independently. Slang emits the local atomics with broader Device scope while preserving their Workgroup address space; the audit accepts that valid emitted form rather than assuming a narrower scope.

Dense variants preserve direct group-XY mapping without tile-list/argument selector reads; sparse variants retain the guarded 65,535-capped queue mapping. The existing outlined CSG geometry helper was inspected as part of control flow and has no diagnostic/workgroup accesses or kernel termination. The shared StorageBuffer heap binding remains needed by material, CSG and sparse-tile reads. These static emitted-module witnesses establish the stated instrumentation and mapping properties; they do not establish driver ISA, material-decode savings, executed instruction counts, zero CPU overhead or GPU latency. Two earlier unsupported audit attempts remain preserved; the accepted final audit handles the compiler's local-atomic scope and outlined CSG helper explicitly.

Evidence files are `build-opt-repair.log`, `native-probe.log`, `cpu-tests.log`, `diagnostics.log`, `diagnostics/refraction_query_diagnostics.json`, `regressions-diagnostics-final.json`, `source-format-check.json`, `emitted-diagnostics/report.json`, `emitted-diagnostics/source-cache-linkage.json` and `diagnostics-cfg-proof.json`.

## Frozen diagnostic implementation identity

The diagnostic-capable build was frozen separately at `2026-10-22T05:36:25.964838+00:00` under `frozen-diagnostics/`. Its source manifest records branch `perf/refraction-query-profiling`, base revision `f90c01b461834e961083165eda7f256ea493f797`, the working-tree diff and 49 explicitly copied source entries, including new diagnostic files. Its payload identity map contains twenty binary/companion/runtime entries, with mutable pipeline-cache volumes distinguished from the single authored asset volume. Build provenance points to `build-opt-repair.log`. This frozen implementation supports correctness and cooked-module inspection; the timing experiment uses the independently frozen earlier uninstrumented renderer.

| Frozen diagnostic identity | SHA-256 |
| --- | --- |
| `refraction_smoke.exe` | `8b26199a73a9d345a653ff730f1a6387adaedf4498647fdef3662233b26be382` |
| Native mapping probe executable | `35a315c702aec796e3573e48b4bf1c0ecbd8955512126e9f1c34a265f9f9ee6d` |
| Authored `res/eda18e57f4796c04.vol` | `89b2454024e170b94fdb75c467c4e5157da0764dd50a6e5b351c2abbf394466b` |
| Source manifest | `e3fa803700812378173a1a841f114c80fdb4597c8c733aa2192abd7010796be7` |

## Frozen timing campaign and host limits

Timing runs the previously qualified uninstrumented specialized candidate copied byte-for-byte to `frozen-uninstrumented/`; it does not time the new counter implementation. Its `provenance.json` names the source artifact and merged revision `f90c01b46`. The inherited source manifest records development base `f09f519ca` plus forty explicit source hashes and the original working-tree diff. That inherited base field alone is not the complete implementation identity. Binary, authored-volume, fixture-source and acquisition-script identities are retained in the predeclared plan, with mutable pipeline caches tracked separately.

| Frozen timing identity | SHA-256 |
| --- | --- |
| `refraction_smoke.exe` | `a59aefc18de3d79680174174b7f9d86d28badc5df9ece14ddf35776b12838cba` |
| Authored `res/eda18e57f4796c04.vol` | `055dde99597c689dc9fb182665a047c52b47726d8779b68f36011c10da8336e3` |
| Source manifest | `3f3b3c8de00cb812e04579265e4f2569518eb9dec2e55b17868ae10d729d50f0` |

The predeclared experiment has four hardware workloads (50% and 100% at each extent), eight equal-weight blocks and 32 trials. Blocks are treated as independent units for bootstrap analysis; serial acquisition does not establish their independence or timing stationarity. Two complete four-treatment Williams cycles put every treatment twice at each position and balance every within-block ordered predecessor pair twice. Cross-block predecessors are recorded. Each trial confirms the exact target framebuffer, discards startup/unsealed-boundary observations, warms for at least thirty seconds of target-only sealed publication intervals and 100 GPU frames, then retains twelve positive reports and at least 100 measured GPU frames. No plateau selection, selective repeats, outlier deletion or favorable extension is permitted. The declared interval lower bound is 1,152 seconds across the campaign.

The full affected scope is argument clear + classification/finalization + resolve + compositor. Whole-frame, individual affected scopes, AVBOIT and opaque/shadow/lighting/presentation controls remain visible. Coverage scaling changes the optical workload; it is not a fixed-scene optimization A/B. The analysis retains per-block and per-position results and individual paired bootstrap intervals. Raw values and control movement must be considered before interpreting coverage scaling or order effects.

Host inventory reports **Qualcomm Adreno X2-90**, driver **32.0.149.0**, running the Windows ARM64 optimized build. A persistent Windows PDH GPU Engine query samples fixture and other-process load at 1 Hz. Raw counter instance/status records, UTC/monotonic timestamps and publication observations are preserved, and physical engines are not summed into an invented utilization percentage. Initial invalid baselines and sample gaps remain visible. Publication windows only approximate GPU execution windows; the telemetry is observational.

The enumerated GPU performance-counter paths expose no clock/frequency candidate, and `nvidia-smi` is absent on this Adreno host. This campaign collected no GPU frequency values; the inventory is not an exhaustive search for Qualcomm vendor tools. GPU temperature values were also not collected, and anonymous ACPI zones are not relabeled as GPU temperature. Load does not reveal frequency, power state or thermal equilibrium. Balanced order and fixed warmup reduce a known design bias without proving that the clock or temperature was constant.

The predeclaration is `predeclared-timing-plan/plan.json`; the first actual acquisition preserves `timing-uninstrumented/plan.json`, per-trial outputs and `timing-uninstrumented/trials.json`. That plan was recorded at `2026-10-22T05:23:15.424168+00:00`.

## Operational interruption and complete restart

The first acquisition process exited with code 1 during trial 14, leaving thirteen complete trials and an incomplete fourteenth trial. It did not publish a completed campaign report or a failure report, and no fixture process remained afterward. Its last retained telemetry timestamp was approximately `2026-10-22T05:32:15+00:00`, about nine minutes after the plan timestamp. A process-lifetime limit is a possible explanation, but the termination cause has not been established.

All first-attempt raw data remain under `timing-uninstrumented/` as incomplete operational evidence. They do not satisfy the predeclared complete Williams schedule and contribute no data to the completed timing analysis. Following the correctness and audit gates, a supervised detached process ran the entire same 32-trial schedule into `timing-uninstrumented-supervised/`, using fresh trials throughout. The restart followed missing required campaign completion evidence and did not select trials by measured speed. `supervised-plan-comparison.json` confirms the original schedule, policy, arm identities and five acquisition scripts were unchanged.

## Completed uninstrumented timing results

The supervised process completed successfully at `2026-10-22T06:03:04.8991253Z`, with exit code 0 and elapsed time **1,301.393 seconds** (about 21 minutes 41 seconds). All 32 trials passed acquisition, wall and telemetry eligibility. Each retained twelve positive timing reports. The shortest target-only warmup was 30.0176 seconds, with at least 1,161 warmup GPU frames per trial; the minimum measured count was 262 GPU frames. Retained report windows spanned 6.0318 to 7.0466 seconds, and each trial had five to seven valid fixture PDH samples in its approximate retained publication window.

`independent-profile-audit.json`, completed at `2026-10-22T06:03:42.231514+00:00`, independently verifies all raw accounting, the exact 32-trial order, current frozen and acquisition-script identities, supervisor exit status and exclusion of the first partial campaign. `final-source-identity-check.json` also confirms that all 48 non-Markdown entries in the frozen diagnostic source snapshot remained unchanged. The diagnostic and uninstrumented builds retain their separate roles and identities.

The table reports equal-weight means across eight block observations per cell, treated as independent units for bootstrap analysis. Brackets contain individual 95% bootstrap intervals for those means; they are conditional on that independence assumption and are not simultaneous family-wise intervals. Full affected time is clear + classification/finalization + resolve + compositor. The last column divides each resolve observation by the independently predicted admitted pixel count.

| Extent | Coverage | Full affected ms [95% CI] | Resolve ms [95% CI] | Whole GPU frame ms [95% CI] | Resolve ms / million admitted pixels |
| --- | ---: | ---: | ---: | ---: | ---: |
| 960x720 | 50% | 2.205888 [1.893934, 2.582297] | 2.063734 [1.763472, 2.427969] | 7.451067 [6.686578, 8.340692] | 5.968345 |
| 960x720 | 100% | 3.666351 [3.370181, 4.052149] | 3.496111 [3.210272, 3.869473] | 9.619413 [8.865744, 10.593704] | 5.058030 |
| 1600x900 | 50% | 4.401774 [4.049240, 4.809485] | 4.080480 [3.767150, 4.440952] | 13.908009 [12.462135, 15.663167] | 5.667711 |
| 1600x900 | 100% | 7.542610 [6.979496, 8.408757] | 7.212396 [6.656813, 8.077237] | 18.452513 [16.755188, 21.125604] | 5.008608 |

Resolve accounts for approximately 92.7% to 95.6% of the full affected aggregate across these four cell means. This supports investigating resolve work before the small clear/classifier scopes, while the separate counters identify which paths were exercised. The timing scopes do not divide resolve latency among entry association, exit tracing, material reconstruction and background cache validation.

The paired comparison below subtracts 50% from 100% within each of the eight blocks. All eight block differences were positive for resolve, full affected time and whole-frame time at both extents. Bracketed intervals again describe individual paired mean differences.

| Extent | Full affected difference ms [95% CI] | Resolve difference ms [95% CI] | Whole-frame difference ms [95% CI] | Resolve difference ms / million admitted pixels [95% CI] |
| --- | ---: | ---: | ---: | ---: |
| 960x720 | +1.460462 [1.030444, 1.871466] | +1.432376 [1.004793, 1.833152] | +2.168346 [1.332135, 3.135056] | -0.910314 [-1.992269, -0.067236] |
| 1600x900 | +3.140836 [2.526399, 3.868116] | +3.131916 [2.520136, 3.897462] | +4.544504 [2.938275, 6.207835] | -0.659102 [-1.225274, -0.133411] |

Full coverage has lower measured resolve time per admitted pixel in this campaign. Coverage also changes slab dimensions, ray distribution and sparse/dense kernel selection; the workload comparison does not isolate a dense-mapping benefit. Physical query counts approximately double between the two coverage levels while resolve means rise by less than twofold. Fixed overhead, mapping and traversal distribution are possible contributors, but their individual latency contributions were not measured.

## Controls, order and load observations

Other rendering scopes also move with the coverage comparison. Shadow visibility increases by a mean **0.440009 ms** at 960x720 (individual paired 95% interval [0.245162, 0.700848]) and **0.895618 ms** at 1600x900 ([0.603322, 1.178153]). AVBOIT accumulation increases by 0.019029 ms and 0.040713 ms respectively. Opaque rendering and deferred-lighting paired intervals include zero at both extents. These scopes and the changing refractive geometry are part of the complete workload; their movement prevents interpreting whole-frame differences as an isolated mapping or refraction-kernel effect.

All unfavorable observations remain in the result. In the second block, the 1600x900 full-coverage observation has resolve 10.006833 ms, whole frame 27.350793 ms, shadow visibility 6.763270 ms and presentation 2.084985 ms. Its resolve and frame medians across all eight blocks are approximately 6.772552 ms and 16.851568 ms, and presentation is usually near 0.08 ms. The large observation affects several scopes. It is retained without attributing it to a renderer defect, a particular preceding treatment or a clock change.

| Cell | Spread of resolve position means, ms | Mean fixture busiest-engine load | Mean other-process busiest-engine load | Other-process load on sample's fixture-busiest engine | Same-engine other-load / resolve Pearson r |
| --- | ---: | ---: | ---: | ---: | ---: |
| 960x720, 50% | 0.836526 | 83.727% | 20.027% | 11.230% | 0.900 |
| 960x720, 100% | 0.588127 | 87.622% | 24.817% | 8.015% | 0.994 |
| 1600x900, 50% | 0.607609 | 84.182% | 24.492% | 12.280% | 0.633 |
| 1600x900, 100% | 1.701692 | 85.189% | 25.725% | 11.933% | 0.982 |

Each position mean has only two observations. For 1600x900 full coverage, the first-position resolve mean is 6.681594 ms and the fourth-position mean is 8.383286 ms, but that contrast includes the broad second-block excursion. It is a descriptive order association, not a causal carryover estimate. Within-cell shadow/resolve correlations are 0.842, 0.989, 0.708 and 0.994 in the table's cell order, consistent with broad shared-stage movement without identifying its cause.

The fixed warmup and balanced order did not establish stationary timing. After the prescribed warmup, the second block's late-half minus early-half resolve is +2.338137 ms for 1600x900 at 50% and +1.096078 ms at 100%. Other trials have both signs, and there is no uniform first-half/second-half behavior across cells. The predeclared warmup remains unchanged and no favorable plateau or subset is selected.

The fixture and other-process busiest-load columns select their engines separately. They differ in 68 of 192 retained observations, so those two columns are not a measure of activity on the same engine and must not be added into a GPU-utilization percentage. `same-engine-load-observations.json` instead selects each sample's fixture-busiest engine and measures other-process activity on that exact engine. The last two columns summarize that refinement; do not sum these samplewise means with the separately selected maxima.

Other-process activity on the same engine co-varies with slower resolve observations in all four cells, most strongly in the two full-coverage cells. The correlations use only eight trial observations per cell, with approximate host-publication alignment. They are observational associations, not proof of a contention mechanism, ownership of a particular excursion or DVFS causation. Missing clock/temperature telemetry and shared-stage variation leave the earlier timing-variation limitation unresolved. The completed data qualify workload scaling and corroborate counted work, while establishing no optimization improvement or thermal equilibrium.

## Next experiment supported by the diagnostic evidence

The bounded experiment order is **deferred background reconstruction (C), smaller entry/exit results (B), then capture-assisted entry-query removal (A)**. This ranking comes from the completed path counts and a source review retained in `next-experiment-source-review.md`. It is a set of source-level hypotheses, with no measured reconstruction saving or safe query replacement established.

First inspect the actual emitted work on the background cache-hit path. All background queries in the nonempty density cells return cached opaque lighting, yet source-level reconstruction precedes classification and cache validation. The compiler may already eliminate or sink unused surface fields, vertex fetches and hook arithmetic. An optimization claim requires identifying the remaining emitted operations on that path before asserting that a new projection avoids them.

Candidate C retains the closest-geometry query and correct per-hit classification, projecting the authoritative `refractionIor` and `renderCoverage` results from the same authored surface hook. Cache admission follows that classification; instance metadata flags are not a certificate that per-hit IOR and coverage are constant. Preserve the hook's actual instance/material context, typed and mutable values, UV, interpolated/transformed normal, world position, ray direction and texture sampling/LOD/derivative behavior, including generated CSG cap conventions. If those two outputs depend on other surface inputs, their dependencies remain necessary. A full lighting-surface reconstruction can then be deferred to a validated opaque cache miss or a transparent hit that requires actual lighting, where emitted-code inspection shows removable work.

That projection preserves secondary-glass rejection, transparent-layer folding, closest-boundary/CSG failure behavior and physical/query-budget counts. A cache miss may evaluate classification again during full reconstruction, so the duplicated work is part of the tradeoff to measure. Validate ordinary, transparent, secondary-glass, offscreen, normal-mapped, UV-varying classification, CSG and lifecycle images. Then measure separately frozen counters-disabled binaries using a balanced complete schedule and the full affected aggregate. Keep reconstruction diagnostics outside timing acquisition.

Candidate B projects smaller entry and exit results while retaining the actual entry-hit IOR eligibility, current instance/entity identity and exact geometric position. Exit optics must keep the authored surface normal, geometric-normal fallback, measured path positions and strict nearest/same-instance/backface and CSG admission. Replacing authored normals with triangle normals changes bending, TIR and Fresnel. Inspect emitted code before implementing a smaller source struct, because current inlining may already remove the unused outputs.

Candidate A offers a larger query-count opportunity but requires a new association contract. Entry tracing issues one query per hardware pixel and always succeeds in this slab fixture; the capture currently supplies entity identity, depth and optical values, without an authoritative current trace-instance/primitive/cap/facing boundary certificate. Establish current identity, exact entry position and nearest retained-boundary behavior before removing the query, retaining a fallback and coverage for inside/open/coincident/nested/foreign boundaries, deformation/material changes, CSG walls, depth precision and hardware preparation failure. Include added capture/preparation/storage costs in the aggregate. A one-third reduction in query operation count would not imply a one-third reduction in GPU time.
