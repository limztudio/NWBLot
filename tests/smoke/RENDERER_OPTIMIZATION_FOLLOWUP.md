# Renderer optimization follow-up

This pass starts at `296e8f485` after the completed ten-step evaluation in `RENDERER_OPTIMIZATION_STEPS.md`. The user authorized the additional source-review recommendations and retains the instruction to commit and push each completed step to `main`. Production code follows `.helper/standard.md` and `.helper/exception.md`; no unnamed namespaces are permitted.

| Step | Candidate | Owner | Status |
| --- | --- | --- | --- |
| 11 | Index tracked resource-state history by resource | `core/task/gpu` | Retained for memory/scanning improvements; isolated timing gate did not pass |
| 12 | Index previous uses of each resource within a task | `core/task/gpu` | Retained for planning/allocation improvements; isolated timing gate inconclusive |
| 13 | Reuse identical zero-extent shadow visibility samples | Shadow shaders | Proposal preparation |
| 14 | Cull non-opaque candidates in opaque hardware-shadow queries | Shadow shaders | Pending |
| 15 | Cache valid normalized normals for dense reflection filtering | Reflection filtering | Pending |
| 16 | Compile reflection spatial variants for supported radii | Reflection filtering/resources | Pending; separate from Step 15 |
| 17 | Reuse existing caustic tile geometry for center/spacing reads | Caustic resolve | Pending |

## Evaluation contract

Root owns all tracked-source application, builds, asset cooking, native/GPU tests, timing acquisitions, and git operations. Independent proposal preparation and source review may run concurrently; builds/cooks and GPU acquisitions run sequentially. Candidate snapshots and raw diagnostics are retained under `__artifacts/reflection_optimization_followup/`.

Each candidate receives an independent correctness and retention decision. Preserve declared range/ordering/ownership contracts and image-quality settings. Source-level operation removal does not establish a frame-time improvement. Failed, incomplete and unfavorable measurements remain evidence; do not drop trials or change thresholds after observing results. Restore candidates when their benefit does not justify retaining them, documenting that decision in the corresponding step commit.

CPU work begins with an unchanged optimized build, compiler subphase observations where available, and a separate bounded ARM64 hot-thread diagnostic. Diagnostic sampling is never part of an A/B timing trial. For each retained CPU candidate, use physically frozen baseline/candidate arms and the existing CPU runner with eight balanced blocks per selected workload, fixed 96/256/32 successful-frame windows, unchanged 3%/0.02 ms practical gate, complete CPU/GPU coverage, GPU controls and whole-frame non-regression checks. The preselected primary workloads are `unique` and `shared`; other fixture workloads provide functional coverage and may receive separately declared measurements if a newly identified mechanism requires them. There is no pooled workload speedup claim.

Shader candidates first require actual compiled-kernel or native-output checks, boundary cases and relevant smoke captures. Inspect emitted instructions when compiler elimination could make a source optimization redundant. Declare each applicable GPU measurement workload and acceptance criteria before acquisition. Keep ray budgets, source radii, filter radii, optical policies and quality settings matched between arms. Report whole-pass work per completed GPU frame rather than dividing multi-dispatch work into misleading per-dispatch savings.

The completed measurements and their limits are recorded below. A favorable observed duration is not automatically a validated isolated speedup.


## Step 11: indexed tracked-state history retained

The compiler now owns a compilation-local resource index beside its existing tracked-state vector. Per-resource first/last indices and per-state previous/next indices visit exactly the same filtered global order as the original scans. The original state array, range subtraction, fragment ordering, state publication, queue/export ordering and borrowed-pointer lifetime remain authoritative. All three production append sites publish through the owner. The compiler checks and sums actual expanded task-use counts, reserves the state vector before constructing the index, and reserves index links from that capacity. No anonymous namespace, persistent cache, native resource or shader change is introduced.

A separate unchanged ARM64 diagnostic first found the latest-state collector in 8 of 24 hot-thread PC samples and the separate first-use collector in 3. All 24 thread resumes succeeded. This is only a bounded hotspot observation, not a time attribution or statistical CPU percentage. The first-use scan remains unchanged in this step.

### Correctness and provenance

Opt and Dbg builds passed. Both configurations passed all 344 GPU-task and 381 ECS graphics cases. The native descriptor/graph target passed 381 cases with 39 hardware-dependent skips in Opt, and 382 with 38 skips in Dbg. Four new history cases exercise interleaved resources and forced vector growth, invalid/stale IDs, invalidated backing-vector identity, and selected-versus-unrelated invalid ranges. Existing fragment/order/export and native synchronization assertions were preserved.

Both baseline and candidate completed all six fixed gather workloads (opaque, hybrid, shared, unique, overrides, runtime), each with 384 successful frames and complete required CPU/GPU coverage. Opt reflection, duplicate-refraction and combined caustics/refraction/reflection capture suites passed. The combined image was inspected. These scene checks do not claim exact stochastic-caustic parity.

The physically frozen arms differ in precisely the nine proposal paths (including two actual baseline absences), source-absence metadata and the renderer executable. Dependencies, generated project inputs and authored resource volumes match. The baseline functional qualification uses the same executable/resources as the exact baseline freeze. The final source additionally value-initializes ten unit-test resource views through a narrow local helper before assigning their IDs/types. An initial marker-label-only cleanup exposed further omitted-member warnings; the complete value initialization removes the warnings without changing production code or assertions. Both 344-case unit suites passed again after that cleanup. The measured production executable was not rebuilt for a test-only cleanup.

### Original predeclared timing result: not a validated isolated gain

Each workload retained all eight balanced blocks, 16 launches, fixed 96/256/32 successful-frame windows, original practical thresholds, GPU controls and whole-CPU-frame checks. There were no acquisition failures or excluded trials.

| Workload | Baseline CPU render ms/frame | Candidate | Paired change, 95% interval (ms) | Original runner status |
| --- | ---: | ---: | --- | --- |
| Unique materials | 157.588633 | 98.501184 | -59.087448 [-60.872702, -57.624676] | `gpu_control_drift` |
| Shared material | 19.482777 | 19.611217 | +0.128440 [-0.011742, +0.259180] | `cpu_change_unresolved` |

The unique workload's observed CPU-render reduction is 37.49%, but several unchanged GPU controls also fall substantially: GPU frame 46.807749 -> 26.915119 ms, shadow visibility 13.937095 -> 8.003686 ms, and deferred composite 0.521039 -> 0.283930 ms. The control gate therefore failed and remains failed. This result cannot be reported as an isolated 37.49% compiler or renderer speedup. Shared-material controls all satisfy equivalence, and its whole CPU frame remains within the predeclared non-regression bound; there is no resolved shared-material gain.

The composite timestamps surround one dispatch in a recorded command buffer, so its change is not a direct measurement of CPU graph-compilation time. GPU elapsed spans can include execution/synchronization conditions; no frequency or thermal telemetry establishes a DVFS explanation here. Balanced power was reported at both timing endpoints, on AC at 78% battery. No build, cook, GPU test, framebuffer capture or CPU sampling overlapped acquisition.

### Separate memory and CPU-consumption observations

One separate memory-mode launch per arm/workload retained all 256 arena samples. These are descriptive observations, not statistical timing trials. The task-graph arena's recorded historical individual-arena lifetime peak decreased:

| Workload | Baseline bytes | Candidate bytes | Decrease |
| --- | ---: | ---: | ---: |
| Unique materials | 55,343,480 | 29,788,512 | 25,554,968 |
| Shared material | 1,636,160 | 1,006,384 | 629,776 |

Both arms report zero retained used/reserved task-graph bytes at the sampled frame boundary. These peaks are not a concurrent whole-process peak or a per-frame peak distribution. Prepare/render arena peaks are unchanged. The new index adds bounded scratch arrays, while upfront state reservation avoids intermediate vector growth. All original memory evidence remains available.

A further explicitly descriptive diagnostic ran one unchanged frozen unique-material launch per arm. Two Win32 `GetProcessTimes` observations retained a duplicate of the actual renderer process handle; there was no additional during-frame sampling. Both actual acquisitions and handle cleanup passed. Total process CPU consumption was 60.015625 seconds for baseline and 37.781250 seconds for candidate; process wall lifetimes were 61.039985 and 38.366736 seconds. These totals cover all renderer threads, startup, all 384 successful frames and shutdown. They exclude GPU execution and CPU work charged elsewhere. They are not render-only timings, invariant instruction counts, paired statistical inference or proof of sole attribution.

### Retention decision and limits

Retain the indexed history for its bounded ownership/order-preserving algorithm and materially lower observed scratch footprint, supported by the lower consumed-CPU observation and complete correctness checks. This is an engineering retention on memory/mechanism evidence, a different basis from passing the original GPU-controlled CPU timing contract. It does **not** change that contract, relabel the failed gate, discard an unfavorable result or establish an isolated speedup. The shared workload has no resolved speed improvement. Subsequent compiler work should use direct existing compiler-phase observations to separate synchronous planning work from whole-render timing conditions.

Evidence is retained under `__artifacts/reflection_optimization_followup/step11/`: exact `baseline_v1`/`candidate_v1` freezes and comparison, Opt/Dbg full test logs/JUnit, actual capture galleries, all six candidate functional results, both complete `timing_v1` campaigns and concise summaries, both memory pilots, `memory_summary.json`, `process_cpu_v1`, and power observations. The unchanged six-workload baseline and 24-sample diagnostic remain under the sibling `profiling/` tree. No raw artifact or failed outcome was removed.


## Shared preparation: optional compiler statistics capture

A test-owned, opt-in tail render pass copies the renderer's existing immutable compiler statistics into a bounded, preallocated 1,024-row buffer. It adds no production timer or GPU query. The runner supplies a distinct per-trial output only with `--compiler-statistics`; inherited diagnostic paths are stripped. The probe stops before renderer/world destruction and writes once during shutdown. Ordinary timing remains diagnostic-off.

The offline validator preserves raw records, generation identities, durations and counts, and uses the existing gather validator to join exactly the 256 measured successful source frames. Invalid early snapshots remain explicit unmeasured evidence; missing/invalid measured rows, duplicate plans, order violations or overflow fail qualification. Structural counts are not a compiled-plan fingerprint, and nested phase times cannot be summed.

Opt and Dbg benchmark builds passed without compiler warnings. All 22 new diagnostic tests, 27 existing gather-analysis tests and 28 existing renderer A/B-analysis tests passed; the three registered CTest suites also passed. One actual optimized shared-material diagnostic completed all 384 successful frames and joined all 256 measured frames, source IDs 96 through 351, with no invalid snapshots. A separate default-off 384-frame shared-material functional launch passed, with no diagnostic environment key or output file. These are qualification launches, not an A/B performance comparison. Exact source/build/runtime identities, commands, logs, JSONL and the joined report are retained under `__artifacts/reflection_optimization_followup/compiler_statistics_diagnostic/`.


## Step 12: indexed uses within each task

A compilation-local `TaskResourceUseIndex` builds an ascending declaration-order chain for each resource in the current task. Rebuild clears only previously touched resources. Capacity is reserved once from the graph resource count and the largest actual expanded task-use array; no graph-wide table is cleared per task and no state survives compilation. Unknown-state uses remain indexed because the original prefix scan included their coverage. Resource IDs/generations and immutable task identity, array and count are validated.

The first-use collector now visits only earlier uses of the same resource. Bounds normalization, range resolution/subtraction order, early coverage termination, whole-resource acceleration-structure behavior and Step11's separate state history remain intact. A resource's first occurrence still passes both bounds conversions but avoids temporary interval vectors. Two test helper sites fully value-initialize task views before assigning their inputs, addressing omitted-member warning risks without changing assertions.

### Correctness and exact-arm evidence

Opt and Dbg builds passed without compiler warnings. Both configurations passed all 354 GPU-task and 381 ECS graphics tests. Native descriptor/graph tests passed 381 cases with 39 feature skips in Opt, and 382 with 38 in Dbg. Ten new cases cover ascending/Unknown-state chains, reset/rebuild/invalid views, normalized first uses, partial/disjoint/symbolic-tail range ordering, original early-stop behavior, expanded resource-set initial barriers and repeated acceleration-structure state/export behavior.

The frozen baseline is `338e504c0`, with the optional compiler diagnostic present in both arms and off in ordinary acquisition. The exact eight-path candidate uses `effective_manifest_v1.json` SHA256 `aa737f3dc8d6205fe0478563da84700a73b36117f32ace1a2a4c489d1aa1e11a`: original proposal plus the two test-only initialization cleanups. Source comparison verifies all eight deltas, three genuine baseline absences, unchanged dependencies, identical generated inputs/authored resource volumes and only the benchmark executable changing among its binary dependencies.

Both exact frozen arms passed all six ordinary gather workloads, each completing 384 successful frames with full required CPU/GPU coverage. Separate memory-mode unique/shared launches also passed. The qualification verifier replays original raw-sample and runtime-log validation, checks actual route/extent/vsync/count signatures, and rejects a diagnostic-enabled ordinary launch. Freshly relinked Opt reflection (22 images), duplicate refraction (33 images) and combined caustics/refraction/reflection (four images including feature-disabled controls) capture suites all passed, 59 captures in 220.39 seconds. Executable/dependency, authored-runtime and frozen-source identities match before/after. These normal captures did not enable GPU debug validation; the native suites provide their separately recorded validation scope.

### Timing result and its limit

Each workload retained all eight balanced blocks and 16 launches, original 96/256/32 successful-frame windows, thresholds, whole-frame checks and GPU controls. No acquisition failed or trial was excluded; diagnostic observation, builds, native tests and captures did not overlap these acquisitions.

| Workload | Baseline CPU render ms/frame | Candidate | Paired change, 95% interval (ms) | Original overall status |
| --- | ---: | ---: | --- | --- |
| Unique materials | 97.919814 | 82.386397 | -15.533417 [-16.640214, -14.566326] | `gpu_control_uncertain` |
| Shared material | 19.299177 | 19.336646 | +0.037469 [-0.129235, +0.209908] | `cpu_change_unresolved` |

The observed unique-material CPU render reduction is 15.86%. Its whole CPU frame decreases from 98.727708 to 83.184360 ms, with interval [-16.661914, -14.573123] ms. However, the deferred-composite GPU control has interval [-0.018858, -0.000947] ms, extending beyond its +/-0.015 ms equivalence tolerance. It is uncertain, not classified as material drift. All other unique controls, including GPU frame (26.973124 -> 26.926183 ms), satisfy equivalence. The original overall gate therefore remains inconclusive; this is not a validated isolated 15.86% renderer speedup.

Shared-material controls all satisfy equivalence. Whole CPU frame is 20.632676 -> 20.612080 ms, interval [-0.191420, +0.167660] ms, within the original non-regression bound. No shared-material speed improvement is resolved. Balanced power, AC and 78% battery were reported at both endpoints; no clock/thermal attribution is inferred.

### Separate diagnostic and allocation evidence

One diagnostic-enabled launch per arm/workload joined all 256 measured successful frames with zero invalid snapshots. These are descriptive phase observations, not paired statistical inference. The unique scene expands to 97,736 declared resource uses per frame. Existing synchronous resource-state-planning wall time is 21.093380 -> 4.721684 ms; existing core compile total is 26.860288 -> 10.451135 ms. Shared planning is 0.200100 -> 0.163856 ms and compile total 0.815123 -> 0.749672 ms. The timing region contains no native GPU calls/waits but remains wall time, including possible scheduling/memory effects. Nested phase buckets must not be summed. Aggregate compile counts match all 256 frames in each workload; this is not a native-command or plan fingerprint.

Separate memory launches show the tradeoff:

| Workload | Task-graph arena lifetime peak, baseline -> candidate bytes | Mean recorded allocations/frame, baseline -> candidate |
| --- | --- | --- |
| Unique materials | 29,788,512 -> 29,810,752 (+22,240) | 293,991.054688 -> 196,358.039063 |
| Shared material | 1,006,384 -> 1,007,944 (+1,560) | 9,494.054688 -> 6,601.000000 |

All 256 task-graph arena samples are available; retained used/reserved bytes at sampled frame boundaries remain zero. Index storage modestly raises the historical individual-arena peak while the first-occurrence path avoids repeated temporary allocations. These are not concurrent whole-process peaks or per-frame peak distributions. Other arena availability and observations remain in the raw reports; unavailable owners are not zero.

### Retention decision

Retain the bounded task-use index for its substantial reduction in recorded temporary allocations and synchronous planning work, supported by the observed unique CPU-render reduction and shared whole-frame non-regression. This is an engineering retention with a small explicit scratch-memory cost, not a relabeling of the uncertain original timing gate. The final 59-capture visual regression passed before committing the step.

Exact freezes, manifest/proposal/test-cleanup versions, build/full test/JUnit logs, all six functional results per arm, source/log replay qualification, all 32 timing trials and both original reports/concise summaries, four memory observations, four compiler diagnostic joins, `descriptive_summary_v1.json`, and power evidence are retained under `__artifacts/reflection_optimization_followup/step12/`.
