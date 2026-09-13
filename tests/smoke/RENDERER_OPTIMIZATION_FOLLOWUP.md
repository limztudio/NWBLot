# Renderer optimization follow-up

This pass starts at `296e8f485` after the completed ten-step evaluation in `RENDERER_OPTIMIZATION_STEPS.md`. The user authorized the additional source-review recommendations and retains the instruction to commit and push each completed step to `main`. Production code follows `.helper/standard.md` and `.helper/exception.md`; no unnamed namespaces are permitted.

| Step | Candidate | Owner | Status |
| --- | --- | --- | --- |
| 11 | Index tracked resource-state history by resource | `core/task/gpu` | Retained for memory/scanning improvements; isolated timing gate did not pass |
| 12 | Index previous uses of each resource within a task | `core/task/gpu` | Pending; separate from Step 11 |
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
