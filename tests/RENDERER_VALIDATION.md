# Renderer error cleanup (September 12, 2026)

This cleanup repairs the failures left open by the [optical appearance experiment](smoke/REFLECTION_OPTICAL_EXPERIMENT.md) and the additional failures found by broad validation. The final full debug run has zero failures, and every applicable optimized target has a passing result across the full campaign and focused repair runs. The rejected optical optimization remains removed. No performance improvement is claimed by this cleanup.

## Repairs and ownership

Commit `d6cd6e4f9` repairs renderer ownership and the extracted source contracts:

- `DeferredFrameTailBuilder` takes the task graph and explicit frame inputs. It no longer depends on or accesses private state in `RendererFramePipeline`. The pipeline owns graph compilation, surfel readback, timing publication, and feedback; the builder owns deferred tail task declaration.
- `PrefixSceneUploadBuilder` receives a lighting-classification snapshot. The pipeline publishes ray-tracing classification after successful scene-upload declaration instead of exposing the ray-tracing system to the upload builder.
- Mesh frame-heap release is private to its owner.
- Source-contract tests read the extracted implementation files and check the relevant caller/callee wiring, ordering, and ownership. They do not concatenate files into a synthetic old implementation or drop the original behavioral assertions.

The smoke-test repair follows those changes:

- The GPU timing fixture models a cold recording attempt that declares query demand without allocating query pools. After discarding that attempt, the next preamble materializes capacity before the measured attempt. The measured prefix, total, and discard counts remain 2, 10, and 10. The test also checks that the cold attempt has zero recorded scopes and two capacity skips, and the measured attempt adds no capacity skips.
- The transparent CSG fixture uses a project-owned unlit green material for its center receiver and disables reflection/refraction for the clipping and coverage check. This prevents facet lighting and optical background transport from obscuring the geometry being tested. Ordinary transparency, refraction, reflection, and combined caustic fixtures retain their separate coverage.
- Each CSG capture selects its actual frozen pose: early, mid, or late (yaw 0, 1, or 2 radians). The oracle checks inset regions derived from the projected receiver and retained geometry. It no longer chooses whichever of three regions contains the most background, which could accept a missing cutter. Retained pixels must also identify the green receiver. Existing contrast thresholds, density fractions, minimum pixel counts, settle times, and rejection of the first invalid nonblank frame remain intact.

There were 50 distinct observed failing cases: 44 ECS GTest cases, three swapchain presentation cases, one GPU-task resource-import case, one descriptor timing case, and the early transparent CSG visual smoke. Residual failures during intermediate repairs are not counted again.

## Validation

Validation uses Windows ARM64, the `windows-clang-arm64-opt` and `windows-clang-arm64-dbg` presets, and the available Qualcomm Adreno X2-90 Vulkan device. Builds, asset cooking, and GPU tests ran sequentially.

| Run | Result | Local evidence under `__artifacts/renderer_error_cleanup/` |
| --- | --- | --- |
| Final complete optimized build | Passed | `complete_opt_build.log` |
| Final complete debug build | Passed | `complete_dbg_build.log` |
| Full optimized CTest campaign before smoke repairs | 87 targets: 83 passed, two failed, two capability skips | `final_full_opt_tests.log`, `final_opt_junit.xml` |
| Repaired optimized descriptor target | Passed | `descriptor_opt_tests.log`, `descriptor_opt_junit.xml` |
| Repaired optimized capture harness and all three CSG poses | Four targets passed; the harness contains 59 Python tests | `repaired_opt_tests.log`, `repaired_opt_junit.xml` |
| Full final debug CTest run | 89 targets: 87 passed, zero failed, two capability skips; 1232.53 seconds | `complete_dbg_tests.log`, `complete_dbg_junit.xml`, `complete_dbg_ctest_details.log` |

All 85 applicable optimized targets have passing results across the full campaign and focused repair runs; `optimized_campaign_results.json` maps each target to its result and source report. The initial full optimized report remains a failed report; it has not been replaced or represented as a single clean full rerun. The final builds include all smoke fixture and asset changes.

The ECS suite passes all 359 active cases in both configurations. The complete debug run also passes the reflection, temporal/feedback, optical, refraction gallery/duplicate, transparent CSG, caustic, skinned CSG, stress, and GPU resource suites. Pre-existing disabled cases were not enabled for this cleanup. Passing CTest targets can also contain individual capability skips; target totals do not imply that every internal GTest case ran.

## Transparent CSG negative controls

Six application client-window captures were acquired at the three frozen poses, with the cutter enabled and then disabled through the fixture-only `NWB_TRANSPARENT_CSG_DISABLE_CUTTER=1` switch. The runtime log confirms the cutter state. All three clipped controls pass and all three uncut controls are rejected.

| Pose | Clipped void pixels | Clipped retained receiver pixels | Uncut void pixels | Uncut result |
| --- | ---: | ---: | ---: | --- |
| Early | 2916 / 2916 | 2916 / 2916 | 0 / 2916 | Rejected |
| Mid | 1476 / 1476 | 972 / 972 | 0 / 1476 | Rejected |
| Late | 594 / 594 | 665 / 736 | 0 / 594 | Rejected |

Images, runtime logs, and analysis are retained in `__artifacts/renderer_error_cleanup/csg_controls/`, including `oracle_verification.json`. The 59-case capture harness suite also tests independent projected geometry, region margins, missing cutters, missing receivers with dense red/blue neighbors, contrast and density boundaries, explicit pose selection, and failure without retrying for a more favorable frame. These controls validate transparent clipping and coverage; they are not an optical-CSG transport claim.

## Capability limits and retained history

- `nwb_transparent_multi_sw_capture_smoke` skips on this RayQuery device because the natural renderer route is hybrid. The software-only route requires its intended device configuration.
- `nwb_csg_visible_capture_smoke` skips because native Meshlets are unavailable. The compute-emulation counterpart passes.
- Internal GPU tests retain their existing capability and queue-dependent skips. Existing disabled tests, including 61 ECS cases and 27 GPU-task cases, were not forced on.

Initial and intermediate failures remain in the artifact directory, including `baseline_other_tests.log`, `affected_opt_tests.log`, `csg_opt_tests.log`, and the original CSG captures in `transparent_csg_initial/` and `transparent_csg_optics_off/`. The earlier experiment artifacts under `__artifacts/reflection_stage9_after_pull/` remain historical evidence. Generated artifacts are ignored local diagnostics, not runtime dependencies.

## Reproduction

Run one build or GPU test job at a time. From the repository root:

```powershell
cmake --preset windows-clang-arm64
cmake --build --preset windows-clang-arm64-opt
cmake --build --preset windows-clang-arm64-dbg
ctest --preset windows-clang-arm64-opt -j 1 --output-on-failure
ctest --preset windows-clang-arm64-dbg -j 1 --output-on-failure
```

The focused optimized repair commands were:

```powershell
ctest --preset windows-clang-arm64-opt -j 1 --output-on-failure -R '^nwb_descriptor_buffer_tests$'
ctest --preset windows-clang-arm64-opt -j 1 --output-on-failure -R '^(nwb_testbed_window_capture_unit|nwb_transparent_csg_capture_(early|mid|late)_smoke)$'
```
