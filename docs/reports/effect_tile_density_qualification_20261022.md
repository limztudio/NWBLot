# Effect tile density qualification — October 22, 2026

**Status: qualification complete for the declared correctness gates and bounded empty-fixture performance claim.** This report records the completed twenty-cell unmerged density matrix, four-cell six-block scheduling comparison, native boundary/frame-loop probes, density images, packet diagnostics, and final regressions for `perf/effect-tile-density-qualification`, based on `c1659141aa916abdf68bed40fe3a615e00b036da`.

**Decision:** retain the five scheduling-hint lines that combine refraction's clear/classify/resolve chain, supported by actual packet/wait reduction and consistent six-block empty-fixture GPU results. Keep the current tiled strategy without an adaptive coverage policy. **1600×900 fully dense hardware performance remains inconclusive:** its wide paired intervals do not establish a gain, equivalence, or noninferiority. The results do not establish arbitrary-scene, portable, CPU, or FPS improvements.

All raw evidence referenced below is retained under `__artifacts/effect_tile_density_qualification/`. This report is separate from [the initial scheduling qualification](effect_tile_scheduling_qualification_20261022.md), which compared the broader scheduling implementation with its earlier branch base. This investigation compares tiled refraction with a surgical full-frame reference while retaining the same AVBOIT integration, optical shading, and depth-gated compositor.

## Question and reference contract

The question is whether the cost of argument clearing and tile classification is repaid at different amounts of captured glass, including a full framebuffer of glass. The comparison must include the entire affected path rather than the resolve scope alone.

The reference uses the current renderer with only three changes: omit the tile-argument clear and classifier tasks, dispatch the resolve directly over the full framebuffer, and map each resolve workgroup directly to its original 8×8 pixel block. Optical entry checks, tracing, current-depth validity, composition, AVBOIT integration, material routing, fixture content, and simulation policy remain the same. No production strategy switch or test hook is introduced by that temporary build procedure.

The exact reference patch is retained as `tests/smoke/refraction_direct_reference.patch`. The build helper preserves the original source bytes, records pre-patch and reference SHA-256 values in `reference-manifest*.json`, and restores the tiled candidate byte-for-byte. Each frozen arm retains a source manifest, working-tree diff, executable/dependency hashes, and its packed runtime resources. The benchmark independently verifies those frozen identities and requires identical compiled fixture instrumentation in `refraction_project.cpp` and `avboit_timing_render_pass.cpp`.

The completed twenty-cell matrix uses `frozen/baseline-v4` (surgical full-frame reference) and `frozen/candidate-v4` (unmerged tiled renderer), with the shared fixed Win32 frame loop and identical density fixture instrumentation. Each arm records thirty-one source entries, the base revision above, and its complete working-tree diff. The exact three-file reference transformation is recorded in `reference-manifest-v4.json`, with original bytes in `reference-backup-v4/` and the emitted reference patch under `tests/smoke/`.

| V4 identity | Full-frame reference | Unmerged tiled candidate |
| --- | --- | --- |
| `refraction_smoke.exe` bytes | 8,980,992 | 8,984,576 |
| Executable SHA-256 | `75f93d30d206d8ec939facc7f0eb45844324a4964b8b35955039ee61ec80455a` | `222f660c049a0d1872b94ea9b5a8c5d0b8add6f1b244cddff5c2e443206c4043` |
| Source-manifest SHA-256 | `9837480f283de4d32cc5ba81a4a9912aef81d7d50a77b613f2c5bc7dbc538438` | `023d90715f4c46dc0d11c9cd78d34b010c2a6d1673e4e967da4515dc8a529011` |
| Authored `res/eda18e57f4796c04.vol` bytes | 81,516,016 | 81,519,384 |
| Authored-volume SHA-256 | `5406d795c3698dc1e5d614a3487ee12c4a5ee08003113ff357bb389b8b01085c` | `789ba8da582a9319d76725d621077d831cd6929c7a20c8eed9f1919a4e5f4620` |

The authored volume differs because the reference changes the resolve workgroup mapping shader; all shared fixture inputs remain identical. The frozen manifests and `timing-density-v4/plan.json` retain the remaining binary/dependency and source identities. `build-reference-v4-opt.log` and `build-candidate-v4-restored-opt.log` record successful builds without compiler warnings. Earlier pilot/v3 arms remain separately identified and are not pooled with v4.

## Host and supported matrix

The qualification host is Windows ARM64 with Clang/LLVM 23.1.3 and a Qualcomm Adreno X2-90 GPU. Earlier host inspection reported graphics driver `32.0.149.0`. Actual hardware versus screen-space resolve diagnostics are required independently of the reported ray-query capability.

The intended host matrix has two resolutions, five analytical captured-pixel percentages, and both tracing routes:

| Dimension | Requested cases |
| --- | --- |
| Framebuffer extent | 960×720 and 1600×900 |
| Captured-glass coverage | 0%, 1%, 10%, 50%, and 100% |
| Refraction route | Explicit screen-space and confirmed hardware ray query |

This is twenty cells. An exploratory run with two balanced blocks has four trials per cell, or eighty trials. The reusable runner defaults to four blocks; selected cells can be repeated with four or six blocks if the exploratory data justify a further decision. Six blocks satisfy the shared inference helper's minimum of five independent paired differences. Multiple-cell intervals do not constitute a simultaneous family-wide confidence claim.

### Why 1920×1080 is excluded on this host

The initial window request was clamped to a 1904×1011 client by the 1920×1050 work area. The density harness subsequently requested an exact client size through its explicit `allow_outside_work_area` option. On Win32 this uses `SWP_NOSENDCHANGING`; Microsoft documents that this flag suppresses `WM_WINDOWPOSCHANGING`. The harness still requires the actual client dimensions and the renderer's resize/target-ready evidence. It does not change display configuration. [Microsoft SetWindowPos documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos)

The exact 1920×1080 resize succeeded, but GPU validation then rejected a preexisting CSG target allocation: **1,074,003,968 bytes**, exceeding the adapter's **1,073,741,824-byte** maximum memory allocation. The runtime warning is retained in `capture-candidate-v2/1920x1080/screen/000.log`. It is not muted, reclassified as a pass, or attributed to the tile queue.

Consequently, 1920×1080 remains an explicit option for capable hosts, while the default qualification matrix uses 960×720 and 1600×900. **1080p is unqualified on this adapter.** The later allocation failure does not erase the successful exact-size window transition, and that transition does not establish a valid 1080p render.

## Density fixture and acquisition admission

The fixture uses one closed glass slab, an opaque striped backdrop, and identical scene/material inputs for the two renderer arms. Positive percentages select a centered rectangle with dimensions rounded to even pixels. The 100% case covers the full framebuffer. At 0%, the glass instance remains within the frustum behind the opaque backdrop, producing zero admitted capture pixels while keeping the transparent/refraction path live. Removing the glass instance would change CPU admission and would not qualify the empty-queue case.

Startup logs record the requested extent and percentage, analytical pixel bounds, admitted pixel count, active and total 8×8 tile counts, one glass instance, and visible or occluded capture mode. These are **analytical pixel-center coverage values, not GPU readback measurements**. The capture harness supplies separate image evidence for visible coverage and optical behavior.

The timing runner rejects a mismatched fixture, extent, coverage/bounds count, trace preference, actual resolve route, material execution route, or runtime warning. The full-frame reference must not publish tile-clear/classification timing work. The candidate must publish those scopes, including the occluded 0% case. Missing cost scopes are not silently treated as zero on the candidate.

When an initial client is clamped, timing admission waits for the exact requested client dimensions, the matching framebuffer resize marker, and the final deferred-target extent. Only an explicit matching initial clamp explains an earlier different renderer extent. Arbitrary sizes or a later extent change fail admission.

After target confirmation, the runner discards every existing timing header and one additional sealing boundary, protecting against a concurrently partial header. It then requires at least two new warmup reports and 100 completed GPU frames from subsequent target-size reports before retaining measurements. This prevents startup-size samples from satisfying target-size warmup. Startup/window/resize time and mutable pipeline-cache identities are recorded separately.

## Native queue and indirect-dispatch boundary probe

The completed native GPU-validation gate exercises the production classifier and the shared resolve pixel-mapping helper using a 2049×2049 depth image. Its 257×257 tile grid contains **66,049 tiles**, crossing the engine's 65,535-workgroup X boundary. It then dispatches a test-owned verifier indirectly from the arguments produced by the actual classifier.

The Vulkan dispatch specification defines total workgroups as the product of the dimensions and requires each direct or indirect dimension to remain within the corresponding device limit. The engine's bounded two-dimensional mapping therefore needs both queue-index rejection and pixel-edge rejection; dispatch capacity alone is insufficient. [Vulkan dispatch specification](https://docs.vulkan.org/spec/latest/chapters/dispatch.html)

The five successive phases reuse storage and require active counts of **66,049, 0, 6, 65,535, and 65,536**. The checks cover:

- Complete argument fields and untouched argument guards.
- Valid, unique active-prefix queue entries and exact expected tile membership.
- Every inactive queue word and queue guard remaining at its sentinel.
- Per-tile visit counts and every full-resolution pixel visit.
- Launched-group, rejected-tail-group, mapping-error, and clipped-edge-lane counters.
- Untouched visit-buffer guards and active-to-empty reuse.

Across the five phases, the CPU compares **20,992,005 full-resolution pixel readback values**, in addition to queue, argument, tile, counter, and guard checks. Phase 0 admits all 66,049 tiles and launches 131,070 groups; the extra 65,021 groups must produce no tile/pixel visits. The test is under `tests/`, uses ordinary selected-backend contracts, and introduces no production probe hook.

`test-large-queue-split-opt.log` records `nwb_avboit_depth_warp_probe_smoke` passing with GPU validation after all five phases; total CTest duration was 2.81 seconds. The probe is correctness evidence for the large-queue mapping, rather than a performance workload or an image-quality comparison.

### CPU oracle anomaly and retained evidence

The first combined CPU prefix/tail loop reported an overwritten tail word even though expected and observed both printed `0xa573d91b`; neighboring guard values were unchanged. The retained native object branched directly to mismatch diagnostics for a tail index without the sentinel comparison present in the saved frontend IR.

Separating the active-prefix and inactive-tail loops restored the native comparison and retained every assertion listed above. Production shaders, GPU initialization, dispatch arguments, resource barriers, and readback layout were unchanged by that test-oracle correction. The corrected native GPU gate passed.

The isolated CPU investigation also reproduced the missing native comparison by compiling the saved frontend bitcode directly through the same ARM64 backend flags **without literal transformation**. Encoding the literals was therefore not necessary to reproduce the observed control flow. The compiler wrapper has no persistent object cache in this path. These observations narrow the investigation but do not prove a reduced compiler defect or establish the underlying cause. No compiler or production workaround is part of this change.

Evidence is retained in `probe-oracle-investigation.txt`, `probe-original-frontend.ll`, `probe-disassembly.txt`, `probe-split-disassembly.txt`, and `probe-cpu-investigation/` with isolated commands, bitcode, object files, transformed IR, and disassemblies.

## Timing protocol and cost accounting

The benchmark uses frozen executable/runtime directories, fixed simulation delta `1/60`, continuous rendering, no framebuffer readbacks, and no GPU validation during measured trials. It attempts foreground ownership and retains the fixture's existing render-unfocused policy. Strict warning/error validation and normal shutdown remain required.

Each exploratory trial retains twelve positive sealed GPU report intervals after warmup, exceeding the rejection threshold of ten positive intervals. The order alternates baseline → candidate and candidate → baseline; the first order also alternates between cells. Each invocation writes a fresh output directory; subsets can run independently without overwriting earlier failures or raw evidence.

The primary effect aggregate is the sum of **argument clear + classification + resolve + compositor GPU duration**, divided by completed GPU frame samples. The full-frame reference has no tile-only clear or classifier work. Entire GPU frame time includes all frame work. Individual resolve timing is retained, but a resolve-only saving cannot establish an optimization when scheduling overhead consumes it.

Opaque rendering, shadow visibility, deferred lighting, and presentation are unaffected controls. The compositor is an affected observation. Report control movement alongside target movement; a current-host measurement floor has not been independently established. Tiny differences are not treated as established effects.

The runner separately records every retained report duration, the maximum duration, and callback wall-time alignment. A retained callback over one second rejects wall-time eligibility. The raw GPU and wall reports remain intact, and `acquisition_clean` is separate from successful GPU acquisition. A rejected callback interval is not trimmed or silently replaced. Callback delta includes pacing and stalls; it is neither isolated CPU execution time nor measured presentation cadence. These counters do not establish an FPS claim.

### Native inactive-window loop qualification

The Win32 message pump previously chose its blocking `GetMessage` branch whenever the window was inactive, before the graphics runtime's `shouldRenderUnfocused()` preference could admit an update. The fixed predicate also considers that existing render-pass preference. A normal inactive window still waits for messages; an inactive window whose render pass requests updates can reach the project update callback.

The native lifecycle regression uses a hidden 64×64 frame, the selected graphics backend, a test-owned valid generic descriptor-heap layout, and an ordinary render pass that requests unfocused rendering. Its first callback requests normal quit before any graphics frame is submitted. A one-second watchdog posts quit if the callback is blocked. A companion case without the render-pass preference must wait for that watchdog. Test-owned warning/error diagnostics remain visible if platform or graphics startup cannot be admitted.

The initial test attempt omitted the required descriptor-heap ABI, so both new loop cases skipped during graphics startup. That run does not qualify the predicate. After supplying the generic layout, the fixed native suite passed all four cases with zero skips. The exact original inactive-only predicate was then restored temporarily and only the opted-in case was run as a negative control: it failed once, with **zero callbacks instead of one** and **the watchdog fired instead of remaining stopped**. The test took 1,340 ms (1.386 seconds process wall time), exited with code 1, and had no skips.

The fixed `core/frame/win32.cpp` bytes were restored in `finally` and verified as SHA-256 `a44cfe9c3d75abd4cac7ebcda53e8fe9f171bb3d9089fd42b890b7626f14c508`. The frame test, refraction smoke, and AVBOIT depth-warp probe were rebuilt and relinked from those bytes. The subsequent native frame suite again passed **4/4 with zero skips**, in 1.28 seconds CTest wall time. `frame-lifecycle-negative-control.json` records commands, elapsed times, exact source hashes, restoration, and build/test outcomes; the corresponding `test-frame-lifecycle-old-predicate-opt.log` and `test-frame-lifecycle-fixed-restored-opt.log` retain assertions and complete results.

This establishes the inactive-window update regression and its fix. The earlier process observation is consistent with the message-pump gate, making it a working explanation for stalled updates, but there is no captured native stack or dump connecting a particular earlier stall to that gate. Final matched renderer arms must both contain the same fixed frame loop before timing admission.

`tests-final-cpu-opt.log` separately records **8/8 CPU analysis suites passing**, with 2.63 seconds total CTest wall time: window resize, refraction capture, lifecycle, density benchmark, density capture, resource identity, gallery, and duplicate-instance analysis. These analysis tests complement the four native frame tests; they do not replace GPU or image admission for the final matrix.

## Completed pilot: 960×720, 0%, screen-space

The pilot used the initial frozen full-frame and tiled arms, one retained occluded glass instance, and two balanced blocks. `timing-pilot-zero-screen/report.json` records successful GPU acquisition with twelve positive reports in every trial. Its wall-time qualification is rejected because candidate block 0 contains a **4.3228-second** maximum callback delta. The cause of that stall is unknown.

| Block/order | Arm | Positive reports | Completed GPU frames | Retained report duration | Wall timing eligible |
| --- | --- | ---: | ---: | ---: | --- |
| 0, first | Full-frame reference | 12 | 1,810 | 6.0220 s | Yes |
| 0, second | Tiled candidate | 12 | 1,401 | 20.4685 s | No — callback stall |
| 1, first | Tiled candidate | 12 | 1,785 | 6.0226 s | Yes |
| 1, second | Full-frame reference | 12 | 1,844 | 6.0187 s | Yes |

| GPU metric | Reference arm mean | Tiled arm mean | Candidate minus reference |
| --- | ---: | ---: | ---: |
| Argument clear + classification + resolve + compositor per completed frame | 0.103992 ms | 0.106829 ms | +0.002837 ms |
| Entire GPU frame | 3.185257 ms | 3.333855 ms | +0.148598 ms |

The combined-effect differences were +0.004899 ms and +0.000776 ms in the separate blocks. The pilot demonstrates that a near-empty resolve does not eliminate classification/clear overhead. It does not establish a confidence-qualified regression, a density crossover, a shipping policy, or CPU/FPS behavior. It must not be pooled with later fixture/build versions without their identity and protocol equivalence being established.

### Incomplete v3 matrix and retained stalls

The first full-matrix attempt with the pre-fix v3 arms stopped as **inconclusive acquisition after twelve completed trials**. Those trials covered 960×720 at 0% with both routes and 1% screen-space, two blocks each. Every completed trial retained twelve positive GPU report intervals. The 0% screen-space block 1 candidate and reference trials contained maximum callback deltas of **77.3204 seconds** and **79.6230 seconds**, respectively; their wall timing was rejected and the raw reports were retained.

The next trial, candidate 960×720 at 1% hardware, reached its acquisition deadline with only four sealed reports, one retained positive report of twelve required, and an additional unsealed header. `timing-density-v3/failure.json` records that incomplete outcome, while `trials.json` retains the twelve earlier trials. This is not a completed density matrix and supplies no shipping decision.

During that incomplete trial, the read-only process observer found the timing sidecar unchanged at 70,863 bytes and five headers, total process CPU unchanged at 1.90625 seconds, the main-window UI thread in `Wait/UserRequest`, and all 24 sampled threads waiting. The harness later closed the process normally. These observations indicate waiting rather than a CPU busy loop; they do not identify a wait object or stack. The one-shot dump observer was armed after that process exited, and **no dump was captured**. Neither observer attached a debugger, explicitly suspended the process, nor sent a window message. The observer itself consumed 2.96875 cumulative CPU seconds across approximately 4 minutes 47 seconds including PowerShell startup.

`refraction-stall-observation.txt` and `refraction-process-watch.jsonl` preserve this diagnostic evidence. The independently proven inactive-window gate explains how an opted-in smoke render pass could stop receiving callbacks, but attributing the pilot's 4.3228-second callback or either v3 long interval to that exact cause remains unproven. The pre-fix pilot and incomplete v3 run remain retained evidence and are separate from the completed v4 rerun and later scheduling comparison.

## Completed unmerged v4 matrix

`timing-density-v4/report.json` records all **20/20 cells and 80/80 trials** completing successfully with clean acquisition. Every trial retained twelve positive GPU intervals: **960 positive intervals** and **84,120 completed GPU frame samples** in total. Individual trials contain 358–1,860 completed GPU frames. No wall trial was rejected; the largest retained callback delta was 577.163 ms, below the declared one-second stall gate. The longest retained GPU publication interval was 0.9037 seconds. These counts establish the declared acquisition rather than stable clocks, isolated CPU cost, or presentation cadence.

Each table entry is the mean of two independent paired blocks. Timings are GPU milliseconds per completed frame; delta is candidate minus the surgical full-frame reference. Effect includes argument clear + classifier + resolve + compositor. Coverage is admitted analytical pixels / 8×8 tiles. All twenty cells have two blocks and zero rejected wall trials.

| Extent | Requested coverage | Route | Pixels / tiles | Reference effect ms | Candidate effect ms | Effect delta ms | Reference frame ms | Candidate frame ms | Frame delta ms | Blocks | Wall rejected trials |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 960x720 | 0% | screen | 0 / 0 | 0.103824 | 0.104703 | +0.000878 | 3.156776 | 3.290328 | +0.133552 | 2 | 0 |
| 960x720 | 0% | hardware | 0 / 0 | 0.115364 | 0.131153 | +0.015789 | 3.240209 | 3.750583 | +0.510374 | 2 | 0 |
| 960x720 | 1% | screen | 6,912 / 120 | 0.139518 | 0.281325 | +0.141807 | 5.470864 | 4.488210 | -0.982654 | 2 | 0 |
| 960x720 | 1% | hardware | 6,912 / 120 | 0.161212 | 0.155398 | -0.005814 | 3.625727 | 3.776969 | +0.151242 | 2 | 0 |
| 960x720 | 10% | screen | 69,312 / 1,140 | 0.143820 | 0.158577 | +0.014756 | 3.868302 | 4.033495 | +0.165193 | 2 | 0 |
| 960x720 | 10% | hardware | 69,312 / 1,140 | 0.484435 | 0.466712 | -0.017723 | 4.058990 | 4.225995 | +0.167006 | 2 | 0 |
| 960x720 | 50% | screen | 345,780 / 5,504 | 0.266749 | 0.312723 | +0.045975 | 4.571642 | 4.825310 | +0.253668 | 2 | 0 |
| 960x720 | 50% | hardware | 345,780 / 5,504 | 1.825743 | 1.769753 | -0.055990 | 6.257368 | 6.366913 | +0.109545 | 2 | 0 |
| 960x720 | 100% | screen | 691,200 / 10,800 | 0.420541 | 0.499252 | +0.078711 | 5.450660 | 5.676006 | +0.225345 | 2 | 0 |
| 960x720 | 100% | hardware | 691,200 / 10,800 | 3.289873 | 3.355690 | +0.065817 | 8.691077 | 8.500472 | -0.190605 | 2 | 0 |
| 1600x900 | 0% | screen | 0 / 0 | 0.217428 | 0.215945 | -0.001483 | 5.799572 | 5.837548 | +0.037976 | 2 | 0 |
| 1600x900 | 0% | hardware | 0 / 0 | 0.220794 | 0.212839 | -0.007955 | 5.474981 | 5.720243 | +0.245262 | 2 | 0 |
| 1600x900 | 1% | screen | 14,400 / 240 | 0.238702 | 0.242127 | +0.003425 | 6.538147 | 6.253317 | -0.284830 | 2 | 0 |
| 1600x900 | 1% | hardware | 14,400 / 240 | 0.316957 | 0.314405 | -0.002553 | 6.088919 | 6.207007 | +0.118088 | 2 | 0 |
| 1600x900 | 10% | screen | 143,704 / 2,304 | 0.290819 | 0.321643 | +0.030824 | 6.355304 | 6.785176 | +0.429872 | 2 | 0 |
| 1600x900 | 10% | hardware | 143,704 / 2,304 | 0.970431 | 0.893967 | -0.076464 | 7.226960 | 7.224749 | -0.002211 | 2 | 0 |
| 1600x900 | 50% | screen | 719,952 / 11,360 | 0.559325 | 0.637644 | +0.078320 | 8.104782 | 8.309210 | +0.204427 | 2 | 0 |
| 1600x900 | 50% | hardware | 719,952 / 11,360 | 3.620599 | 4.342268 | +0.721669 | 11.242513 | 13.076818 | +1.834305 | 2 | 0 |
| 1600x900 | 100% | screen | 1,440,000 / 22,600 | 0.881134 | 1.029886 | +0.148752 | 9.809434 | 9.955997 | +0.146563 | 2 | 0 |
| 1600x900 | 100% | hardware | 1,440,000 / 22,600 | 6.448499 | 7.183653 | +0.735154 | 15.748194 | 16.405445 | +0.657251 | 2 | 0 |

The empty screen-space aggregate has small observed absolute differences (+0.000878 ms at 960×720, −0.001483 ms at 1600×900); these samples do not establish equivalence. The denser screen-space means are higher for the tiled arm. Hardware results vary with coverage and extent; the 1600×900 50% and 100% cases show +0.721669 ms and +0.735154 ms respectively. Those are observations to investigate, not a supported density threshold: there are only two blocks and some control changes are large.

`timing-density-v4-summary/` contains the derived twenty-row table and `summary.json`, every paired metric in `per-block.csv`, unchanged-control ranges in `control-ranges.csv`, trial admission/counts in `trials.csv`, named-scope coverage in `scope-counts.csv`, and every retained report/scope in `intervals.csv`. The summarizer reparses raw timing files with the shared parser and verifies stored sample counts/totals. Its `--require-complete` gate passed only after the top-level final report existed.

Across the twenty completed cells, unchanged controls show the following range of candidate-minus-reference changes in cell means. This is a descriptive range over cells, not a confidence interval or a tolerance gate:

| Unchanged control | Lowest observed relative change | Highest observed relative change |
| --- | ---: | ---: |
| Opaque rendering | −11.71% | +28.31% |
| Shadow visibility | −31.10% | +12.62% |
| Deferred lighting | −62.40% | +51.33% |
| Deferred presentation | −51.81% | +16.17% |

The raw per-arm and paired ranges remain available for each cell. No numeric control threshold or correction factor was chosen after observing these values. Whole-frame deltas therefore remain descriptive and cannot be assigned solely to the refraction scheduling change. No data from the pre-fix pilot, incomplete v3 attempt, or later scheduling comparison is pooled into this matrix.

### Handling control drift in the v4 acquisition

The completed v4 matrix uses matched arms containing the fixed native frame loop. Clean acquisition alone does not establish stable GPU timing. In the 960×720, 1% screen-space cell, the two-block combined-effect mean moves from **0.139518 to 0.281325 ms** (+0.141807 ms), while the entire GPU frame moves from **5.470864 to 4.488210 ms** (−0.982654 ms). These directions cannot support attributing the whole-frame change to tile scheduling.

The affected compositor measurement has paired differences of **+0.282366 ms** and **−0.021450 ms**. The first candidate block measures 0.372781 ms there, compared with 0.071424 ms in its second block. Meanwhile unchanged shadow visibility moves from 1.277254 to 0.879975 ms and deferred lighting from 0.288119 to 0.108321 ms. The effect aggregate must retain the compositor and its outlying block; dropping that cost or selecting the favorable block would change the declared comparison. The controls must remain beside the affected metrics rather than being converted into an after-the-fact correction.

These observations limit causal or policy fitting from the exploratory matrix. The complete evidence retains per-block differences and control ranges, preserves every valid interval, and distinguishes acquisition cleanliness from stability and attribution. No density crossover, adaptive dispatch policy, CPU cost, presentation cadence, or FPS conclusion follows from this example. The raw cell report remains `timing-density-v4/960x720_001pct_screen/report.json`.

The artifact-only `summarize_density.py` reads the retained files and uses the existing benchmark comparison and interval parsers. It emits the planned twenty-row table, exact analytical runtime coverage, all paired metric differences, control arm/delta ranges, every trial's positive report and GPU-frame count, wall rejections, and every retained raw interval's named GPU-scope sample counts. Its output remains marked incomplete until the top-level `report.json` confirms every planned cell and trial; successful GPU acquisition does not automatically mark image or packet qualification complete.

## Native image and snapshot qualification

The complete full-frame reference and merged tiled candidate each passed the twenty-cell density capture harness with GPU validation enabled. `capture-reference-final/density_capture_metrics.json` and `capture-merged-final/density_capture_metrics.json` both record `passed: true`, exact requested framebuffer/client extents, actual trace routes, independent image-coverage checks, and all twenty admitted analytical signatures. The harness independently derives the expected projected rectangle and compares optical changes against the 0% image; the logged analytical marker alone cannot satisfy the image oracle. Captures, runtime logs, and launch identities remain beside each result. The unsupported 1920×1080 allocation attempt remains separate failed evidence.

`density-image-parity.json` compares the full-resolution RGB payload for every matched pair and records **20/20 exact RGB matches** at the correct extents. This proves parity for the declared stationary fixture captures and both tracing routes; it does not establish arbitrary-scene or temporal image equivalence. Image readbacks and GPU validation were separate from every timed trial.

The shared test-only normal-completion validator accepts either a fully accepted graph or exactly one unsubmitted canonical `render.frame_recovery` task in its independent packet. It accounts for every compiled normal task and accepted packet with consistent generation and exact task totals. It rejects additional discards, unaccepted normal work, executed recovery, native-submit failure, duplicate rows, and incorrect generation/counts. All **10 crafted CPU cases passed** in `test-snapshot-validation-opt.log`, including the zero-discard normal path. This handles the renderer's unused recovery task without accepting an incomplete normal frame. `test-native-queue-frame-final-opt.log` also records the native frame and large-queue GPU-validation CTests passing (2/2); the frame suite's four native cases have zero skips.

### Packet placement before and after the scheduling hints

Native snapshots at 0% and 100% were captured for the unmerged and merged tiled renderers. `snapshots-merged-final/placement-comparison.json` compares them with the retained JSON and encoded `.nwbs` streams under `snapshots-unmerged-final/` and `snapshots-merged-final/`. Both densities show the same changes:

| Public diagnostic metric | Unmerged tiled | Merged tiled |
| --- | ---: | ---: |
| Compiled tasks | 89 | 89 |
| Accepted normal tasks | 88 | 88 |
| Compiled packets | 14 | 12 |
| Accepted packets | 13 | 11 |
| Packet dependencies | 27 | 22 |
| Planned waits | 29 | 24 |
| Submitted timeline waits | 5 | 3 |
| Transition barriers | 245 | 245 |
| Public analysis edges | 752 | 752, identical set |
| Native submit failures / recovery submissions | 0 / 0 | 0 / 0 |

The single unaccepted task/packet in each snapshot is the validated unused recovery path. In the unmerged renderer, argument clear is compute packet 8, classification is compute packet 9, and refraction resolve plus compositor are graphics packet 10. In the merged renderer, all four tasks occupy accepted **graphics packet 8**. The source change applies existing `mergeWithPrevious` and `allowMergeAcrossConsumerFrontier` hints to classification and resolve; it leaves the clear as the chain start and retains all task identities, dependencies, resource/state declarations, and per-task timing scopes.

This demonstrates actual accepted packet placement and fewer waits, rather than assuming that the hints took effect. Moving clear/classification onto the graphics packet also changes their overlap opportunity: the combined packet must respect resolve's incoming dependencies. A packet-count reduction alone therefore does not establish a performance gain. The public edge set and transition count are unchanged; compiler state-seed IDs and extra internal dependency IDs are not exposed by this diagnostic, so the snapshot does not claim to compare those hidden structures. Diagnostic captures do not measure steady frame time.

## Completed matched scheduling experiment

The four endpoint cells were predeclared under `scheduling-prototype/`: 0% screen-space and 100% hardware, each at 960×720 and 1600×900, with six balanced blocks per cell (**48 trials**). The original plan remains in `validation-plan-before-matched-arm.json`. At **2026-10-22 02:35:26 UTC**, before these measurements, `validation-plan.json` recorded the fresh matched arms needed to include the corrected test-owned snapshot helper in both executables. Cell selections, blocks, sampling, metrics, and acceptance limits were unchanged.

This is a direct comparison of **unmerged tiled versus merged tiled**, using `--baseline-scheduling tiled`. It has a different reference contract from the twenty-cell matrix. Both measured arms must contain clear, classify, and resolve timing scopes; missing baseline tile work is an error rather than an assumed zero. Results must remain separate from full-frame-reference and pre-fix data.

`frozen/baseline-scheduled` and `frozen/candidate-scheduled` each contain thirty-five source-manifest entries. Exactly one entry differs: `source/impl/ecs_render/graph/frame_graph_reflection_resolve.cpp`, with five added scheduling-hint lines. The other thirty-four source entries are identical, including compiled fixture/helper and fixed core frame code. The frozen artifact identity maps differ only in `bin/refraction_smoke.exe` and its `.pdb`; all authored runtime resources are identical. The authored volume SHA-256 is `789ba8da582a9319d76725d621077d831cd6929c7a20c8eed9f1919a4e5f4620` in both arms.

| Scheduled-arm identity | Unmerged tiled reference | Merged tiled candidate |
| --- | --- | --- |
| Executable SHA-256 | `0e287f232864661508601157ac96fbc39d69bd4a918095f8db32e1c3016a70db` | `ac42e3f0dd3d85e87d873fdfedd117edbb2e6d19c3a01dfb9f7bcbafc1106d5d` |
| Source-manifest SHA-256 | `482e54e7248d12553e7020a84c11c18383f78bbbbcd2466ea7c2fef1dbac6bf9` | `3dff2b01794cde496e77e7342a0a5f940600d9b7fad84b0d617e511196871dd1` |
| Changed graph source SHA-256 | `41d2847924f64d0e10b333db99284f9cb45c75d67967958f61cb04a26ff8f108` | `f73096f6371d5cc2f16825eda994e46462035016397ef026bf4909666c19428d` |

The two declared runs are under `timing-scheduled/six-block-empty-screen` and `timing-scheduled/six-block-dense-hardware`. Their primary metric remains argument clear + classifier + resolve + compositor, with whole-frame timing, individual scopes, controls, and callback eligibility retained. Six paired blocks permit the shared per-cell paired interval helper; four cells and several metrics do not create a simultaneous confidence guarantee. Selected endpoints cannot establish a general density crossover or an adaptive shipping policy.

### Completed empty screen-space endpoints

`timing-scheduled/six-block-empty-screen/report.json` records both cells, all **24/24 trials**, **288 positive intervals**, and **34,040 completed GPU frames** with clean acquisition and zero wall rejections. The largest callback delta is 49.936 ms. `timing-scheduled-summary/six-block-empty-screen/` retains the table, all six paired differences, controls, trial/report counts, and raw interval-derived counts.

The interval below is the shared helper's deterministic 95% percentile bootstrap interval for the mean of six independent paired block differences (10,000 draws, seed 0). It is per metric and cell, without a simultaneous family-wide guarantee. Negative delta favors the merged candidate.

| Empty screen-space endpoint | Unmerged effect ms | Merged effect ms | Effect delta ms [95% interval] | Unmerged frame ms | Merged frame ms | Frame delta ms [95% interval] |
| --- | ---: | ---: | --- | ---: | ---: | --- |
| 960×720, 0% | 0.105192 | 0.099937 | −0.005255 [−0.005738, −0.004751] | 3.304658 | 3.173040 | −0.131618 [−0.167162, −0.099641] |
| 1600×900, 0% | 0.213938 | 0.206941 | −0.006997 [−0.008864, −0.005222] | 5.822438 | 5.620380 | −0.202058 [−0.281884, −0.132691] |

All six effect and whole-frame differences are negative at each empty endpoint. Combined effect work is lower by 5.00% and 3.27% in these samples. Individual compositor means move from 0.073332 to 0.069965 ms at 960×720 and from 0.155768 to 0.149149 ms at 1600×900. The complete effect aggregate retains those affected observations; resolve-only measurements are not substituted for it.

Unchanged controls still move and bound attribution. Their paired mean differences and intervals are:

| Extent | Control | Unmerged ms | Merged ms | Delta ms [95% interval] |
| --- | --- | ---: | ---: | --- |
| 960×720 | Opaque | 0.007761 | 0.007807 | +0.000046 [−0.000249, +0.000436] |
| 960×720 | Shadow visibility | 0.819708 | 0.822921 | +0.003212 [−0.000407, +0.006671] |
| 960×720 | Deferred lighting | 0.096973 | 0.095681 | −0.001292 [−0.002472, −0.000010] |
| 960×720 | Deferred presentation | 0.039340 | 0.039089 | −0.000251 [−0.000632, +0.000149] |
| 1600×900 | Opaque | 0.007720 | 0.007760 | +0.000040 [−0.000171, +0.000227] |
| 1600×900 | Shadow visibility | 1.736536 | 1.709564 | −0.026972 [−0.047350, −0.008448] |
| 1600×900 | Deferred lighting | 0.212694 | 0.207375 | −0.005319 [−0.008626, −0.001880] |
| 1600×900 | Deferred presentation | 0.072389 | 0.071852 | −0.000537 [−0.001333, +0.000311] |

The repeated target differences support lower affected GPU work in these two declared empty fixtures. Whole-frame reductions are larger than the measured effect reduction, while some controls also decrease; they cannot all be assigned to the clear/classifier/resolve timestamps. Neither these intervals nor packet counts measure isolated CPU submission cost or presentation cadence. Adoption also depends on the predeclared dense-hardware check and the final scope of the claim.

### Completed dense hardware endpoints

`timing-scheduled/six-block-dense-hardware/report.json` records both full-coverage hardware cells, all **24/24 trials**, **288 positive intervals**, and **12,823 completed GPU frames**, again with clean acquisition and zero wall rejections. The largest callback delta is 35.073 ms. The two selected subsets together retain all **48 declared trials, 576 positive intervals, and 46,863 completed GPU frames**. No trial or block was replaced or trimmed.

| Dense hardware endpoint | Unmerged effect ms | Merged effect ms | Effect delta ms [95% interval] | Unmerged frame ms | Merged frame ms | Frame delta ms [95% interval] |
| --- | ---: | ---: | --- | ---: | ---: | --- |
| 960×720, 100% | 3.351947 | 3.348548 | −0.003399 [−0.019773, +0.010826] | 8.519546 | 8.377154 | −0.142392 [−0.227901, −0.061889] |
| 1600×900, 100% | 6.881291 | 6.893530 | +0.012238 [−0.190526, +0.233848] | 15.894668 | 15.800771 | −0.093897 [−0.296927, +0.172432] |

The six effect differences at 960×720 range from −0.039116 to +0.022975 ms; those at 1600×900 range from −0.265206 to +0.420246 ms. Both dense effect intervals include zero, and the high-resolution interval allows changes larger than its +0.012238 ms point estimate. This does not establish a dense improvement, equivalence, or a noninferiority bound. The 960×720 whole-frame differences are all negative; the 1600×900 whole-frame interval includes zero. These remain the declared GPU observations rather than CPU or FPS estimates.

| Extent | Dense control | Unmerged ms | Merged ms | Delta ms [95% interval] |
| --- | --- | ---: | ---: | --- |
| 960×720 | Opaque | 0.007649 | 0.008051 | +0.000401 [−0.000002, +0.000816] |
| 960×720 | Shadow visibility | 1.872759 | 1.871807 | −0.000952 [−0.009975, +0.006767] |
| 960×720 | Deferred lighting | 0.097013 | 0.096569 | −0.000445 [−0.002474, +0.001470] |
| 960×720 | Deferred presentation | 0.040494 | 0.040858 | +0.000364 [−0.000407, +0.001250] |
| 1600×900 | Opaque | 0.007674 | 0.007669 | −0.000005 [−0.000027, +0.000019] |
| 1600×900 | Shadow visibility | 3.901083 | 3.932056 | +0.030973 [−0.084127, +0.137771] |
| 1600×900 | Deferred lighting | 0.207975 | 0.204627 | −0.003348 [−0.009011, +0.001218] |
| 1600×900 | Deferred presentation | 0.072507 | 0.073799 | +0.001292 [+0.000394, +0.002505] |

The 1600×900 presentation-control mean increases by 0.001292 ms, with an interval above zero; it remains reported rather than hidden behind the whole-frame mean. Other dense control intervals include zero. All per-block observations, individual affected scopes, and sample counts remain under `timing-scheduled-summary/six-block-dense-hardware/` and the original raw trial directories. The two selected comparisons isolate the scheduling-hint source difference, but their endpoints do not establish a density crossover or a portable claim over arbitrary scenes.

## Final implementation decision and regression reconciliation

Retain the five successor scheduling-hint lines. They produce the observed clear/classify/resolve/compositor graphics packet, reduce accepted packets from thirteen to eleven and timeline waits from five to three, and preserve the declared task/resource contract. The paired empty-fixture effect intervals are below zero at both selected extents, with all six block differences favoring the merged candidate; the matching whole-frame intervals are also below zero. This supports a bounded claim about those measured empty screen-space fixtures.

Keep the current tiled dispatch strategy at every coverage. The exploratory full-frame comparison does not support fitting a density threshold, and this change introduces no adaptive coverage policy. The dense 960×720 effect interval crosses zero even though its whole-frame interval is negative. At 1600×900, the dense effect and whole-frame intervals both cross zero; the effect has two positive and four negative block differences. Its small +0.012238 ms mean is not proof of safety. The upper effect bound (+0.233848 ms), control movement, and block variation remain explicit limitations. **High-resolution fully dense hardware performance is unresolved by this experiment; no dense gain or noninferiority claim is made.**

The final tracked `tests/smoke/refraction_direct_reference.patch` applies to the merged implementation and reproduces exactly the three measured v4 full-frame source outputs. `final-direct-reference-identities.json` records input and output hashes: `resolve_common.slangi` → `06c73e371fea0444fa1f56e1c916fcfa8788d3925e9a154c4867778a1eeb4fc9`, `rt_refraction.cpp` → `ed5b257dc8f7b68f5763d324796543f4616f1103d6f2a6696662aec214275485`, and `frame_graph_reflection_resolve.cpp` → `e3b8b993046f780bca49a7afcb2a6e48a52b616dbd6992a26e39ae46afc3c119`. The patch admission check and diff whitespace check pass. This preserves reproducibility after retaining the hints without changing the optical reference contract or pooling its measurements with the tiled-to-tiled comparison.

Final GPU-validation regressions passed for all three ordinary route frame sets, fourteen lifecycle captures, and seven CSG capture frame sets. `regressions-final.log` records ordinary, lifecycle, and CSG gates passing; details, images, and logs are retained in `regression-ordinary-final/`, `regression-lifecycle-final/`, and `regression-csg-final/` and their adjacent `.log` files. The lifecycle gate covers movement, partial tiles, stale-output retirement, disabling, reactivation, and AVBOIT foreground preservation. The CSG gate checks retained entry/exit geometry in hardware and screen-space routes with distinct thickness/approximation controls.

`tests-final-all-opt.log` records **10/10 final CTest suites passing** in 3.99 seconds: native frame lifecycle, the full telemetry suite, and all eight CPU analysis suites. The four native frame cases have zero skips; the ten crafted snapshot-completion cases also pass. The earlier large-queue selected-backend GPU-validation probe and twenty-pair exact density image comparison remain part of the evidence above. No warning was muted to qualify an unsupported run, no long interval was removed, and no additional trial was selected to obtain a preferable result. Completion here means the declared tests, planned acquisitions, source reconciliation, and bounded decision are finished; the stated dense-hardware uncertainty remains a performance limit of the result.
