# Effect tile scheduling qualification — October 22, 2026

This report records the qualification of `perf/effect-tile-scheduling` against its actual branch base, `0a930be89e5910e439563fdbf260948493886165`. The final implementation skips empty AVBOIT integration regions, resolves refraction through a compacted queue of 8×8 screen tiles, and uses current capture depth to guard the sparsely written refraction result in the compositor. Material pass routing and reflection candidate scheduling retain their existing behavior.

Correctness checks passed for the qualified fixtures, including both refraction routes, moving and disappearing glass, partial tiles, target recreation, AVBOIT foreground preservation, and generated CSG caps. Timing is descriptive: AVBOIT integration was substantially lower in both paired blocks on both routes. Classification plus refraction resolve was approximately unchanged on average for screen-space tracing and lower in both hardware blocks. The compositor is also affected by this change and must be included when examining the refraction work. These acquisitions do not establish an FPS improvement or a portable performance guarantee.

## Implementation and validity contract

The refraction classifier reads the existing full-resolution capture depth. It ORs `depth < 1.0` across each 8×8 group, appends active tile indices, and publishes indirect dispatch arguments. It does not evaluate a new material pass or sample alpha at reduced resolution. Clear glass with zero AVBOIT coverage remains eligible. The resolve still evaluates every pixel of an admitted tile at full resolution, with its existing entry checks and hardware or screen-space tracing behavior.

Each target generation owns a four-byte queue entry per possible tile and a 16-byte argument/counter buffer. The graph orders argument clear, classification, indirect resolve, and composition, with declared shader and indirect argument reads. Dispatches containing more than 65,535 tiles use a second group dimension and reject excess tail invocations. The odd-size fixture exercises incomplete groups at the framebuffer edge; these captures do not exercise the more-than-65,535-tile case.

The final classifier does not clear the full-resolution RGBA16 result. Instead, the compositor samples transmitted radiance only where the **current frame's** capture depth is below one. Every such pixel belongs to a queued tile and is written by the resolve, including an explicit zero on rejected entries. Pixels outside that mask retain ordinary AVBOIT composition and do not consume old or uninitialized refraction radiance. The compositor declares capture depth as a graph input. The inactive refraction branch retains its existing texture clear.

AVBOIT integration scans the already produced packed extinction words for the first active depth word in each low-resolution XY cell. The required transmittance clear leaves empty cells and leading slices at neutral one. A finite overflow slice remains an independent activity signal even when packed additions have wrapped a word back to zero. No additional raster pass, producer atomic, persistent mask, or sampling approximation is introduced for this optimization.

## Host, builds, and comparison identity

Qualification used Windows ARM64, Clang/LLVM 23.1.3, the `opt` configuration, and a Qualcomm Adreno X2-90 GPU. The smoke build and asset pipeline completed, including the newly authored classifier and the final compositor shader. The final rebuild recooked and gathered 321 assets, and all three native targets built successfully: `nwb_refraction_smoke`, `nwb_transparent_multi_smoke`, and `nwb_avboit_depth_warp_probe_smoke`.

Build logs are retained under `__artifacts/effect_tile_scheduling/`, including `build-opt.log`, `build-baseline-opt.log`, `build-validity-opt.log`, and `build-final-opt.log`. Frozen comparison inputs are retained in:

- `frozen/baseline/`: the `0a930be89e5910e439563fdbf260948493886165` baseline with instrumented fixture sources, executable/DLL files, and the matching packed runtime assets.
- `frozen/candidate-validity/`: the final depth-validity implementation with the matching fixture, executable/DLL files, and packed runtime assets.
- `frozen/candidate/`: the earlier classifier variant that also cleared the full-resolution result, retained to explain the refinement decision.

Each frozen directory contains `source-manifest.json` and `identities.json`. The source manifests' revision labels record `0a930be89e5910e439563fdbf260948493886165` plus the arm name, which was the checkout HEAD at freezing. Both comparison executables include the unrelated main-branch work already present at that base and the same fixture instrumentation. The candidate additionally includes the scheduling implementation. Every frozen baseline manifest entry under `impl/` was compared with `git show 1fd9dd723:<path>` after CRLF normalization and matched, confirming those rendering files had not changed between the earlier checkout and the actual feature-branch base. Source, executable, DLL, authored volume, and runtime-cache hashes provide the exact measured identities, including modifications relative to the recorded HEAD. The timing plan records both arms' executable and volume hashes before acquisition; per-trial files retain cache identities and runtime route evidence.

The final build recooked the classifier after normalizing its source to CRLF with the required two EOF newline terminators. Comparison of the parsed packed-volume index and every byte against frozen `candidate-validity` found only the classifier's `sourceChecksum` metadata changed: eight bytes in the 81,513,853-byte authored volume. Every executed SPIR-V payload and `bytecodeChecksum` remained identical. The final raw volume hash therefore differs from the measured frozen volume despite identical executed shader bytecode.

## Correctness qualification

`tests-validity-opt.log` records all nine selected CTest cases passing after the final validity change:

| Coverage | CTest case |
| --- | --- |
| Existing capture analysis | `nwb_refraction_capture_analysis_unit` |
| Lifecycle oracle corruption cases | `nwb_refraction_lifecycle_analysis_unit` |
| Full entity identity handling | `nwb_refraction_identity_analysis_unit` |
| Gallery analysis | `nwb_refraction_gallery_analysis_unit` |
| Duplicate-instance analysis | `nwb_refraction_duplicate_analysis_unit` |
| Direct depth-warp and integration GPU probes | `nwb_avboit_depth_warp_probe_smoke` |
| Clear-glass displacement and foreground AVBOIT | `nwb_refraction_capture_smoke` |
| Movement, empty output, partial tiles, and reactivation | `nwb_refraction_lifecycle_capture_smoke` |
| Generated CSG refraction caps | `nwb_csg_refraction_capture_smoke` |

The direct GPU probe validates 4,112 depth-warp cases against complete LUT/control outputs and sentinel tails. Its additional integration probe reads all 1,785 R16_FLOAT voxels across five successive phases: 7×3 cells, 17 physical slices, and reused storage. Cases include empty cells, leading zero words, partial active words, saturation, finite overflow with a zero packed word, and a populated-to-empty transition. The first phase imports the fresh volume with unknown layout and declares write access before its unconditional clear; later phases restore and import the known copy-destination state. Submission completion precedes CPU readback comparison. The enclosing smoke enables GPU validation.

An independent numerical comparison in `avboit_integrate_numerical.json` passed 45,400 cases against the prior integration loop: 25,400 boundary cases and 20,000 randomized cases with seed `0x3489ef62`. It compares every resulting R16_FLOAT slice value and includes overflow at or beyond the active bound, one active slice, leading empty words, partial active words, and physical slice tails. This numerical comparison complements the direct GPU probe; it does not replace it.

Fresh final captures with `--gpudbg` passed both the ordinary refraction and lifecycle harnesses. The runtime logs confirm validation enabled and actual hardware tracing for the automatic route; the explicitly selected route reports screen-space tracing. The ordinary fixture at 960×720 retained all 8,832 reference foreground pixels and changed none of the 27,370 sampled exterior pixels on either route. `refraction-final-baseline-images.json` records exact RGB equality between the frozen baseline and final candidate for disabled, automatic, and screen-space captures: zero differing channels and zero maximum channel difference in all three images.

The 967×727 lifecycle fixture takes seven completed framebuffer captures in one process. Its hardware source frames were 15, 32, 49, 67, 85, 102, and 120; screen-space frames were 15, 33, 51, 69, 86, 104, and 122. Both routes restored the departed glass region, empty frame, disabled frame, and reactivated frame with zero changed pixels under the harness's comparison. Both retained all 9,264 reference foreground pixels in every phase.

The last seven columns form an actual partial tile crossed by the edge sphere. All 1,015 eligible dark stripe pixels displaced to light on the hardware route. The screen-space route used its existing straight sheet fallback at this offscreen boundary; all 1,015 eligible pixels satisfied the bounded Fresnel/presentation oracle. That fallback is accepted only when the runtime reports screen-space tracing. It requires neutral, nonblack attenuation within 1.5 encoded levels of the fixed fixture's predicted result, and a change of at least one encoded level. Corruption tests reject missing or unchanged tails, black replacement, unrelated tint, and excessive attenuation. The wider edge region still requires displacement in both stripe directions on both routes.

A separate `--gpudbg` resize run confirmed a live change from 960×720 to a 967×727 client and framebuffer, then captured the resized image. This exercises replacement of target-owned queue buffers and descriptors. Evidence is retained in `resize-gpudbg.log`, `resize-gpudbg.before-resize.bmp`, and `resize-gpudbg.bmp`.

`tests-transparent-opt.log` additionally records the natural hardware transparent-multi capture passing. Its natural software-only companion correctly skipped because this adapter supports RayQuery and selected the hardware route. That skip is not software-only device qualification. Explicit screen-space refraction was exercised separately by the refraction harnesses above.

Final capture evidence is retained under `final-refraction-gpudbg/` and `final-lifecycle-gpudbg/`, together with `final-refraction-gpudbg.log` and `final-lifecycle-gpudbg.log`. The lifecycle directory includes `lifecycle_metrics.json` and each phase's image and runtime log.

## Screen-space timing method

Timing used the continuous 960×720 refraction fixture with refraction enabled, screen-space tracing explicitly selected, and a fixed simulation delta of 0.016666667 seconds. The scene contains a zero-coverage clear sphere, an opaque stripe backdrop, and foreground/background AVBOIT panels. Both arms used the same fixture instrumentation and material route, reported as `VertexIndexed + PS from persistent object-space geometry`.

Acquisition used frozen executable and runtime directories, no framebuffer readbacks, and no GPU validation. The harness recorded window/startup timing separately, attempted to focus the window, and enabled the fixture's existing render-unfocused policy. It discarded two initial sealed timing reports and retained 12 reports with positive GPU frame measurements per trial. All trials exceeded the rejection threshold of ten positive reports. The balanced order was baseline → candidate, followed by candidate → baseline.

Per-scope times are GPU measurements. Combined classification, resolve, and compositor work sums their GPU duration and divides by completed GPU frames, so different scope sample counts do not distort that sum. The argument-buffer clear is not independently timed in this aggregate; full GPU frame time includes it and all other frame work. The compositor is an affected observation, not an unchanged control. Opaque rendering, shadow visibility, deferred lighting, and presentation are controls. Callback delta includes pacing and long stalls; it is not isolated CPU execution time or measured presentation cadence.

The four final trials retained the following data:

| Block and order | Arm | Positive reports | Completed GPU frames | Retained report duration | GPU frame mean |
| --- | --- | ---: | ---: | ---: | ---: |
| 0, first | Baseline | 12 | 1,912 | 6.0194 s | 3.0205 ms |
| 0, second | Final candidate | 12 | 1,706 | 49.8934 s | 3.1951 ms |
| 1, first | Final candidate | 12 | 1,737 | 6.0202 s | 3.3699 ms |
| 1, second | Baseline | 12 | 1,459 | 6.0176 s | 4.0250 ms |

The block 0 candidate includes a recorded 44.2576-second maximum callback delta. This interval remains in the raw evidence and wall-time summary. It is not silently trimmed or described as continuous ordinary frame pacing. The other retained report durations were approximately six seconds. The stall, whole-frame movement, and control variation prevent an FPS or CPU-cost conclusion from these trials.

## Screen-space timing observations and refinement decision

The following values are the arithmetic means across the two trials per arm. Block differences are candidate minus baseline within each balanced block. They are descriptive observations, without confidence intervals.

| GPU work | Baseline mean | Final candidate mean | Difference, block 0 | Difference, block 1 |
| --- | ---: | ---: | ---: | ---: |
| Classification + resolve + compositor per completed frame | 0.186775 ms | 0.157119 ms | -0.008002 ms | -0.051309 ms |
| Classification + resolve per completed frame | 0.076930 ms | 0.077095 ms | +0.009791 ms | -0.009462 ms |
| Refraction resolve scope | 0.076930 ms | 0.047123 ms | -0.019177 ms | -0.040438 ms |
| Deferred compositor scope | 0.109844 ms | 0.080024 ms | -0.017794 ms | -0.041847 ms |
| AVBOIT integration scope | 0.080003 ms | 0.013102 ms | -0.052765 ms | -0.081037 ms |
| Entire GPU frame | 3.522731 ms | 3.282510 ms | +0.174644 ms | -0.655087 ms |
| Opaque control | 0.006585 ms | 0.006773 ms | +0.000007 ms | +0.000369 ms |
| Shadow visibility control | 0.606031 ms | 0.539407 ms | +0.007919 ms | -0.141166 ms |
| Deferred lighting control | 0.110124 ms | 0.080497 ms | -0.002277 ms | -0.056976 ms |
| Presentation control | 0.050064 ms | 0.045292 ms | -0.001213 ms | -0.008330 ms |

AVBOIT integration decreased in both blocks, by approximately 0.053 ms and 0.081 ms. This is consistent with the avoided empty-cell and leading-slice work. The resolve scope also decreased in both blocks, but classification consumes that saving when considered alone: classification plus resolve averages 0.077095 ms against 0.076930 ms. The mean difference of 0.000165 ms does not establish a regression or a gain. Including the affected compositor gives lower observed work in both blocks, although the changed controls and two-block sample limit prevent attributing the whole-frame difference to this implementation. A current-host timing floor was not independently established, so tiny per-scope differences are not treated as established effects.

The initial variant retained in `timing-screen-03/report.json` cleared the entire RGBA16 result inside classification. Its classification-plus-resolve aggregate increased from 0.063002 ms to 0.078952 ms, with positive paired differences of 0.021775 ms and 0.010125 ms. Resolve-only timing would have concealed that overhead. This observation motivated the current capture-depth validity contract: remove full-frame result clearing and avoid optical foreground/radiance loads at pixels without captured glass. The final comparison above includes the compositor because that refinement changes its work.

Final raw screen acquisitions, the launch plan, frozen identities, summaries, logs, and sidecars are retained under `timing-validity-screen/`. Its `report.json` identifies the analysis script and prior report hashes for the derived combined-compositor metric, and records that raw data was unchanged. `run_refraction_timing.py` preserves the acquisition and analysis procedure. The earlier `timing-screen-03/` evidence is retained to explain the design decision, rather than presented as qualification of the final implementation.

## Hardware timing observations

`timing-validity-hardware/report.json` records a completed hardware acquisition with the same frozen arms, continuous fixture, 960×720 extent, fixed simulation delta, balanced two-block order, two discarded reports, and 12 retained positive reports per trial. Runtime admission requires the hardware refraction diagnostic and rejects a screen-space fallback. Framebuffer capture and GPU validation remain disabled for these timing runs.

| Block and order | Arm | Positive reports | Completed GPU frames | Retained report duration | GPU frame mean |
| --- | --- | ---: | ---: | ---: | ---: |
| 0, first | Baseline | 12 | 1,221 | 6.0249 s | 4.8414 ms |
| 0, second | Final candidate | 12 | 1,263 | 6.0320 s | 4.6780 ms |
| 1, first | Final candidate | 12 | 1,171 | 6.0355 s | 4.9870 ms |
| 1, second | Baseline | 12 | 1,191 | 6.0266 s | 4.9564 ms |

| GPU work | Baseline mean | Final candidate mean | Difference, block 0 | Difference, block 1 |
| --- | ---: | ---: | ---: | ---: |
| Classification + resolve + compositor per completed frame | 0.706346 ms | 0.629521 ms | -0.071381 ms | -0.082269 ms |
| Classification + resolve per completed frame | 0.571164 ms | 0.519251 ms | -0.043892 ms | -0.059936 ms |
| Refraction resolve scope | 0.571164 ms | 0.476509 ms | -0.085589 ms | -0.103722 ms |
| Deferred compositor scope | 0.135182 ms | 0.110271 ms | -0.027489 ms | -0.022334 ms |
| AVBOIT integration scope | 0.102228 ms | 0.019574 ms | -0.084298 ms | -0.081009 ms |
| Entire GPU frame | 4.898894 ms | 4.832522 ms | -0.163395 ms | +0.030652 ms |
| Opaque control | 0.009428 ms | 0.008862 ms | -0.000918 ms | -0.000213 ms |
| Shadow visibility control | 0.799826 ms | 0.787548 ms | -0.024494 ms | -0.000063 ms |
| Deferred lighting control | 0.150046 ms | 0.125064 ms | -0.017057 ms | -0.032907 ms |
| Presentation control | 0.068660 ms | 0.065849 ms | -0.007493 ms | +0.001870 ms |

The observed classification-plus-resolve reduction is 9.09% across the two arm means; including the affected compositor gives 10.88% lower measured work. AVBOIT integration is 80.85% lower across the arm means, with decreases of approximately 0.084 ms and 0.081 ms in the separate blocks. These are adapter-local observations from two balanced blocks, not confidence-qualified percentage estimates. The combined refraction metrics exclude the independently untimed argument clear, while whole-frame timing includes it.

Whole-frame block differences have opposing signs, and unaffected controls also move. Deferred lighting, for example, decreases by approximately 0.017 ms and 0.033 ms although its implementation is unchanged. These data support retaining the targeted work reductions as observations; they do not isolate a repeatable whole-frame improvement or establish presentation FPS or CPU savings. Launch identities, raw timing sidecars, runtime logs, per-trial summaries, and the complete comparison are retained under `timing-validity-hardware/`.

## Scope of the result

These checks establish the stated scheduling, numerical, lifecycle, identity, and visual invariants on the qualified host and fixtures. Exact RGB equality applies to the three matched ordinary captures, not every arbitrary scene. CSG cap captures establish that the producer participates in the normal refraction path; they do not establish a CSG performance result. The renderer still selects one primary refractor per pixel and retains its existing bounded tracing and fallback rules. This work does not add physically complete transport through arbitrary stacked or nested media.

The timing sample covers this sparse-glass fixture on explicitly selected screen-space and confirmed hardware refraction routes. It does not establish dense-glass performance, an arbitrary-scene speedup, CPU savings, presentation FPS, or performance on another GPU or driver. Broader timing qualification should retain matched scene/view/extent/route and frozen binary/asset identities, include classification, argument-clear, resolve and compositor costs, and assess unaffected controls over additional independent balanced blocks.
