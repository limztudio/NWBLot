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


## Shared preparation: isolated shadow measurements

A default-off smoke render pass reserves 13 existing GPU timing scopes with 32 in-flight ranges each. It owns registration and shutdown and permits unfocused rendering during measurement. The opt-in soft-shadow fixture disables and verifies caustic emission on all three lights while preserving the ordinary scene. The A/B runner supplies explicit zero/finite extents and fixed yaw, requires the natural hybrid route, checks all required scope coverage and rejects any completed caustic work, fallback, validation or capture/freeze markers. Only unaffected opaque/deferred passes are timing controls; whole frame remains primary and shadow visibility secondary.

Opt and Dbg builds passed without compiler warning/error markers. All 43 A/B parser tests passed, including 15 new shadow cases. Both normal shadow capture CTests passed (4.35 seconds Opt, 4.23 seconds Dbg). Separate frozen Opt zero-extent and finite-extent producer-coverage launches passed: two warm-up reports, six retained reports, at least 100 completed frame samples, all 13 required scopes, hardware required, no caustic emission. Actual device was Adreno X2-90 with hybrid shadows and compute-emulated materials. Source, binaries, authored resources and acquisition dependencies were checked before and after each launch. These two launches qualify measurement coverage only; they are not comparative performance evidence.

The actual packed baseline hardware-soft, software-opaque-soft and software-transparent-soft modules were extracted with archive/index/entry linkage, validated for Vulkan 1.2 and disassembled. The hardware baseline retains query work inside the sample loop and ordered half accumulation followed by the original half reciprocal normalization. This establishes emitted inputs for the next experiment, not a dynamic software-opaque coverage claim.

Evidence remains under `__artifacts/reflection_optimization_followup/shadow_measurement/`: effective six-path preparation manifest, Opt/Dbg build/full smoke/JUnit logs and images, `qualification_freeze_v1`, both `*_pilot_v1` raw acquisitions and identity records, and `emitted_qualification_v1`. Frozen image comparison will use the existing soft-shadow baseline runner; its update-count freeze phase must not be described as a count of successful GPU submissions.


## Shared preparation: isolate shadow material response

The first Step13 candidate capture failed exact RGB comparison (199,720 changed pixels), but a separate repeat of the unchanged frozen baseline also failed (210,874 changed pixels). All original images, differences, logs and identities remain under `step13/execution_v1`. This disqualified the original fixture for exact comparison; it neither establishes a shader regression nor qualifies the optimization. The candidate was restored and both configurations recooked before this common preparation. No Step13/14 timing campaign has run.

The opt-in shadow fixture now selects project-owned materials that retain the existing surface hooks and direct lighting, while replacing only the material's resolved indirect irradiance input with the existing hemispherical ambient response. Ordinary materials keep their wrapper and equations through one guarded shared Lambert helper. Surfel preparation and production still run and remain part of total GPU frame cost. An exact-once startup marker and load-bearing runtime signature verify the material policy. The capture-ready wording now accurately describes update callbacks; the existing settle-and-client-capture protocol does not prove an accepted GPU source-frame index.

Both Opt and Dbg builds/cooks passed without compiler warning/error markers. All 44 renderer A/B parser tests passed. Fresh ordinary Opt reflection, duplicate-refraction, combined optical-caustic and normal-shadow capture suites passed in 222.38 seconds; normal Dbg shadow capture passed in 5.03 seconds. All 61 actual Opt/Dbg images and accompanying reports/logs were preserved, and the combined image was visually inspected.

Separate zero- and finite-extent producer-coverage pilots using the newly frozen Opt inputs both passed the original two-warm-up/six-retained-report, minimum-100-frame policy and all 13 scopes. Both verified the Adreno X2-90 hardware hybrid route, caustic emission off and hemispherical ambient response; full frozen source/binary/authored-runtime and acquisition dependencies matched before and after. These are coverage checks, not comparative speed measurements.

The eight-path common preparation manifest is SHA256 `01ae0c23a66528c216958050725535e3b9700cd3341688b71088411e26f7ff29`. Evidence is under `__artifacts/reflection_optimization_followup/shadow_material_response/`. Fresh Step13/14 plans require unchanged-baseline exact self-parity for both zero and finite extents before candidate comparisons or timing. The earlier failed comparisons and original zero-difference gate are preserved.


## Shared preparation: full client pixels for baseline capture

The revised Step13 unchanged-baseline zero replay failed only at 36 symmetric bottom-corner pixels; the other 1,151,964 pixels were exactly RGB-identical. Both independent offline reads localized every difference to the rounded window-corner fringe. The full exact comparison remains a failure, and finite was not attempted by the fail-stop helper. The original BMPs, differences, logs, identities and `corner_failure_diagnosis_v1.json` remain under `step13/execution_v2`; no shader candidate or timing campaign was applied there.

The baseline runner now reuses the existing Windows raw-client capture policy already used by the async-shadow M4 harness. Its three helper names are generalized and all M4 call sites migrated. Before waiting for readiness, the Windows path prepares the window, requests the documented no-rounding DWM preference and flushes composition; afterward it captures the complete client rectangle without another focus/restore operation. There is no image cropping, masking or tolerance change. Unsupported-attribute skip and HRESULT failure behavior remain explicit. The [Windows API contract](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_window_corner_preference) defines the no-rounding preference; the BMP evidence alone does not establish compositor causation.

Both existing baseline and M4 self-test entry points passed. All 59 window-capture integration tests and 44 renderer A/B parser tests passed. The new Windows orchestration case requires prepare, ready, settle, raw capture and close in that order, and rejects the previous Windows capture path. Existing M4 tests retain their DWM argument, error, skip, flush and full-client-rectangle checks. This three-Python-file correction requires no renderer rebuild; fresh frozen-arm actual captures and exact self-parity remain the next qualification gate.

Manifest SHA256 is `bb5f22e01333dc34e562ef89efa5c9bc55dadacf06debfd6c22354e9bcb9f05b`; source snapshots, patch and CPU test logs are under `__artifacts/reflection_optimization_followup/raw_client_capture/`. Callback-phase readiness is still not a GPU source-frame completion proof.


## Step13: repeated zero-extent shadow traces — rejected

Both tested implementations preserve the visual result but slow ordinary finite-size lights on Adreno X2-90. Neither optimization is retained. The original three shader sources were restored and both configurations recooked. The restored optimized binary dependencies and entire authored resource volume match the qualified original baseline.

The shared predicate accepted only the two authored floating-point zero encodings, selecting angular extent for directional lights and physical radius for punctual lights. Nonzero subnormals, nonfinite extents and nonfinite light types retained the original path. Reuse remained local to one light and pixel, preserving ordered half additions and reciprocal normalization. Version 1 cached visibility across the traversal loop and guarded each traversal. Version 2 selected the traversal count before the unchanged loop and performed the remaining ordered half additions afterward. Neither introduced an approximate radius, persistent cache, new resource or quality setting.

### Correctness and compiled integration

The full-client fixture's original baseline passed exact zero-extent and finite-extent self-replays. Each candidate then completed five GPU-debug captures. All four isolated cases (zero, finite, directional-zero only, punctual-zero only) were exactly RGB-identical to the baseline across all 1,152,000 pixels. Actual logs confirm all three GPU-validation startup markers, hybrid shadows, hemispherical ambient response and disabled caustic emission for the isolated cases. Readiness remains 360 update callbacks followed by settle/client capture, not an accepted GPU source-frame count. Ordinary finite captures retain stochastic indirect/caustic response and are report-only: v1 changed 135,751 pixels, mean absolute channel difference 0.191709, maximum 65; v2 changed 125,193, mean 0.187251, maximum 65. These ordinary images were visually inspected without relabeling their differences as exact parity.

Both candidates built/cooked all 218 assets in Opt and Dbg without compiler warning/error markers. Ordinary Dbg shadow smoke passed for each (4.07 and 4.08 seconds total). Actual packed hardware-soft, software-opaque-soft and software-transparent-soft modules passed Vulkan 1.2 validation and control/dataflow review. Version 2 retains the original traversal bodies, an FP16 accumulation before every post-loop cache read, a separate ordered FP16 repeat loop and the original denominator. Capability, descriptor and floating-point mode policies are unchanged; this does not establish portable denormal behavior or driver ISA/register allocation. Software-opaque emitted validation is not a claim that this hardware host dynamically executed the fallback kernel.

Whole-volume comparison for each candidate accounts for 218 payloads and 164 shader records: three intended bytecodes plus the shader index change, while six additional logical records change only their source checksum. All other 214 payload bytes and archive key order/table indices are unchanged; physical offsets relocate with shader growth. Executable dependencies and generated material/shadow inputs are identical. The frozen source delta is exactly the three proposed shader paths.

### Complete timing experiments

Each implementation ran two eight-block comparisons with 16 launches per workload, two warm-up and six measured publications per launch. Every workload retained 2,880 completed GPU frame samples. Across both source experiments, all 64 planned launches and 11,520 measured GPU frames were retained with no failed acquisition, exclusion or retry. Builds, captures and other GPU work did not overlap acquisition. The original 3%/0.02 ms whole-frame practical threshold and unaffected control-equivalence gates remain unchanged.

| Implementation / workload | Baseline GPU frame ms | Candidate | Paired change, 95% interval (ms) | Original status |
| --- | ---: | ---: | --- | --- |
| v1 / zero extent | 32.947471 | 25.425988 | -7.521483 [-7.650592, -7.392892] | `control_uncertain` |
| v1 / finite extent | 33.458579 | 36.674120 | +3.215541 [+3.040937, +3.338169] | `control_uncertain` |
| v2 / zero extent | 32.571835 | 26.032989 | -6.538846 [-6.695480, -6.368615] | `control_uncertain` |
| v2 / finite extent | 33.608229 | 38.929694 | +5.321464 [+5.256210, +5.383702] | `resolved_gpu_time_increase` |

Version 1's finite observed increase is 9.61%, beyond its 1.003757 ms non-regression bound. Its deferred-lighting control interval [-0.025711, -0.001483] ms exceeds the +/-0.015 ms equivalence band; the original uncertain status remains. Version 2's finite increase is 15.83%, beyond its 1.008247 ms bound, with every unaffected control equivalent. Its software-transparent trace grows from 13.001196 to 18.383453 ms and aggregate shadow visibility from 17.780498 to 23.271976 ms. This disqualifies the revised implementation.

The zero-extent observed reductions are 22.83% and 20.08%, respectively. Deferred-lighting control intervals leave both original zero-extent gates uncertain; these observations are not validated isolated renderer speedup claims. Version 2's zero control interval is [-0.017277, +0.004552] ms against +/-0.015 ms. Reduced repeated trace work does not offset the finite-light regression. Moving the branch/cache did not fix that regression, and no compiler-register or thermal cause is inferred from SPIR-V or scope timings alone.

Evidence remains under `__artifacts/reflection_optimization_followup/step13/`: original/revised proposals and predeclared plans, earlier fixture failures in `execution_v1/v2`, the qualified original baseline/selfchecks and first candidate in `execution_v3`, the revised candidate and independent emitted/volume/capture reviews in `execution_v4`, all 64 raw timing trials and original reports, and restoration identity/build records. Proposal manifests are `5dc89a984d1ae40f28f114fc8af28af8747ac960b125364555429304f0450a0b` and `12d701f31e47645c8952d913285d80d10cdd7a47b6b7fb1da700b08f2c692461`. The source optimization is closed as rejected; the improved common smoke/measurement support remains for subsequent steps.


## Step14: cull non-opaque hardware shadow candidates â€” not retained

The one-file candidate reduced observed frame time in both workloads, but did not meet the original practical-gain and control-equivalence rules. Both reports remain `control_uncertain`. The original query flags were restored and both smoke runtimes recooked in Opt and Dbg. The restored optimized application dependencies and entire authored volumes match their original frozen baselines. This is a promising, unretained candidate, not a demonstrated performance regression or a validated renderer speedup.

The shared TLAS includes transparent instances. BLAS geometry is not marked opaque by default, and only nontransparent instances receive ForceOpaque. The existing all-instance hardware shadow query drains nonopaque candidates without committing them; software traversal separately integrates colored transmission. The candidate added CullNonOpaque alongside AcceptFirstHit, preserving the all-instance mask, ray segment, Proceed loop and committed-triangle result. This remains within the shared hardware shadow helper; optical tracing, material classification and software transmission were unchanged.

### Implementation and visual qualification

Both configurations built/cooked 218 assets without compiler warning/error markers. The two ordinary Dbg shadow/transparent capture CTests passed in 8.84 seconds. Actual hard and soft packed kernels validated for Vulkan 1.2. Each query flag changes from 4 to 132 (0x04 to 0x84). Slang also separates the old unsigned-four constant from its unrelated light-field index, correctly preserving index four. After reversing that explicit constant split/flag change and consistently renaming IDs, the complete instruction streams match the originals. TLAS, mask 255, ray operands, traversal and committed status are unchanged. This is SPIR-V proof, not final driver ISA or exhaustive per-ray visibility coverage.

Both frozen runtime pairs have identical executable/dependency inventories and exactly one changed source path. Their volumes retain 218 payloads and 164 shader records: only the hard/soft bytecodes and index payload change, with two changed logical records and no unrelated metadata-only changes. Each module grows by 16 bytes; the authored volume grows by 32 bytes. Packing moves 102 otherwise identical payload offsets. All other software shadow, reflection, refraction and caustic payload bytes remain unchanged.

Fresh baseline five-case captures and separate zero/finite self-replays passed. The candidate's four isolated whole-client RGB comparisons are exact across all 1,152,000 pixels: zero, finite, directional-zero and punctual-zero. Ordinary soft-shadow comparison is report-only (91,877 changed pixels, mean absolute channel difference 0.074784, maximum 64). The existing interleaved opaque/glass AVBOIT scene also passed; its ordinary GI/caustic difference is report-only (8,680 pixels, mean 0.011675, maximum 10). Both AVBOIT images and the combined optical image were visually inspected. View overlap, grazing silhouettes and final shaded pixels are bounded scene coverage, not a complete glass-before-opaque light-ray ordering oracle.

The unchanged duplicate-refraction suite passed all 33 captures and 29 duplicate/control comparisons. The combined caustics/refraction/reflection suite passed four captures and its separate-contribution checks; the exterior-reflection oracle tested 7,032 samples with zero missing, minimum predicted-gain fraction 0.971957 and byte MAE 0.228213. All 43 actual optimized candidate logs contain each of the three GPU-debug startup markers exactly once and no strict warning/assertion/error/validation tokens. The original duplicate runner did not write per-capture log copies; 33 uniquely matched original logger files were archived with case, route, frozen runtime and source-frame evidence. They were not regenerated.

The first new AVBOIT wrapper rejected an indented validation marker even though its underlying capture succeeded. Its failed result and original helper remain preserved. V2 changes only whitespace trimming before the unchanged exact marker checks; fresh baseline/candidate captures passed. This corrects a log parser, not rendering, image thresholds or validation policy. Raw-profile readiness remains 96 updates for AVBOIT and 360 for soft shadows, not an accepted-submission count. The collateral framebuffer captures separately record actual source frames.

### Timing and disposition

Both workloads completed all eight balanced blocks and 16 launches, with two warm-up and six retained publications per launch, and 2,880 measured completed GPU frames per workload. All 32 launches and 5,760 frames were retained; no trial failed, was excluded or retried. No builds, captures, debug validation or other GPU work overlapped timing. Original practical thresholds and control gates were preserved.

| Workload | Baseline frame ms | Candidate | Paired change, 95% interval (ms) | Original status |
| --- | ---: | ---: | --- | --- |
| Zero extent | 32.815069 | 32.440053 | -0.375017 [-0.584408, -0.154542] | `control_uncertain` |
| Finite extent | 33.426645 | 32.926420 | -0.500225 [-0.628360, -0.377810] | `control_uncertain` |

Observed reductions are 1.14% and 1.50%, below the respective original practical thresholds of 0.984452 and 1.002799 ms. Deferred-lighting control intervals narrowly leave their +/-0.015 ms equivalence bands: [-0.015393, +0.008224] ms for zero and [-0.016390, +0.004902] ms for finite. They are uncertain, not classified as material drift; all other original controls are equivalent. The original uncertainty is not relabeled because the excursion is small.

The targeted hardware trace falls from 0.739494 to 0.408629 ms for zero extent, paired interval [-0.339721, -0.322520] ms, and from 0.838855 to 0.439696 ms for finite extent, interval [-0.413613, -0.380594] ms. Aggregate visibility falls from 16.989103 to 16.679601 ms and 17.677234 to 17.268878 ms. These are measured scope observations supporting the candidate's mechanism; they do not override the predeclared whole-frame/control retention rules. No broader device/scene speed claim or clock/thermal attribution is made.

All evidence remains under `__artifacts/reflection_optimization_followup/step14/`: original and formatting-only effective proposal, plans, four physical arms, original and revised AVBOIT wrappers/failure, baseline/selfcheck/candidate captures, 37 collateral captures and archived logs, emitted/volume reviews, all timing trials/original reports, and restoration records. Effective manifest SHA256 is `6474cf71e07685604ddfa8eed4c13f4caa03654defbc263c4d74f12ba1ad01dd`; the source formatting correction only wraps the longer TraceRayInline argument list to follow `.helper`. Further qualification would need a separately declared experiment; this step does not repeat samples or change thresholds to obtain a pass.
