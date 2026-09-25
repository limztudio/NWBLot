# Whole-Source Domain Review — All 72 First-Party Files >= 30KB

Scope definition: first-party production sources (`core/`, `global/`, `impl/`, `launcher/`, `loader/`, `logger/`, `pipeline/`, `utilities/`, `CoolStuff/`), extensions `.h/.hpp/.cpp/.c/.cc/.inl`, size >= 30000 bytes (30KB decimal), `tests/`, `3rd_parties/`, `__exec/` excluded. This yields exactly **72 files**. (At 30720-byte KiB threshold the same filter yields 68; the 4 extra files in the 30000-byte set are `task_graph_imports.cpp`, `gpu_descriptor_heap_descriptor_buffer.cpp`, `task_graph_stage_validation.cpp`, `backend_device.h` — all covered below as #69–72.)

## Project standard followed (`.helper/standard.md` — 18 sections + 4 subsections, 249 bullets, 525 lines)

1. File and module structure
2. Namespace style
3. Naming conventions
4. Formatting conventions
5. Include order
6. Type and API usage
7. Ownership and memory patterns
8. Error handling and diagnostics
9. Performance-oriented conventions
10. Practical checklist for new code
11. New project layout (CMake)
12. Class/Struct Layout and Order
13. Declaration/Definition Order
14. Mutex Selection Guide (`global/sync.h`)
14.1 Quick decision order
14.2 Type-by-type guidance
14.3 Condition variable pairing
14.4 Practical defaults
15. Shader And Asset Text Style
16. ECS Component Headers
17. Third-Party Packages
18. Source Architecture Hygiene (governing rule for this review: keep render-feature code physically sliced by ownership; do not pile behavior into shared god files; do not add declarations to mega-header umbrellas; a `.cpp` past ~800 lines mixing unrelated owners is a review smell — split by concrete owner/concern, never by hacky `.inl` header fragments; private feature state lives with that feature; `global/` stays neutral; `core/` stays top-level sibling of `global/`)

## Project notes followed (`.helper/note.md` — 148 lines)

- Important Rules (rules 1–20+: Device& invariant, AssetRef<T> bindings, pipeline-cache framebuffer key, `.nwb` built-in mesh payloads, codec-layer validation, descriptor-heap GPU lifetime, strict shader config tokens, case-sensitive entry points, BOM strip, shader-text identity rules, shader-driven ECS material contract, mesh/compute path separation)
- Scheduler Architecture
- Project Bootstrap Invariants
- ECS Runtime Type Safety
- ECS API Shape
- Math Matrix Layout
- GPU Timing / Perf Measurement
- Parallel execution domains

## Hacky-split check (`.inl` verdict)

- Tracked `*.inl` repo-wide: 6 — all vendored under `3rd_parties/basis_universal/` (`encoder/basisu_astc_ldr_pseudoinv_tab.inl`, `encoder/basisu_bc15_spmd_kernels.inl`, `transcoder/basisu_astc_cfgs.inl`, `transcoder/basisu_dds_transcoder.inl`, `transcoder/basisu_etc1_mods.inl`, `transcoder/basisu_xbc7_decoder.inl`). Zero were introduced by this repo.
- First-party `*.inl` (under `core/ global/ impl/ launcher/ loader/ logger/ pipeline/ utilities/ CoolStuff/`): **0**.
- First-party `#include "*.inl"` references: **0**.
- Verdict: **0 hacky `.inl` header-fragment splits remain. PASS.**

## Per-file verdicts (72/72 — every target listed, each done, no sampling)

Split policy applied per file: SPLIT only if the file mixes unrelated concern owners with separable classes; otherwise NO-SPLIT with justification. No file below is split via `.inl` fragments; any future split must create separated classes per functionality.

| # | Size (B) | Lines | Path | Domain | Verdict + justification |
|---|---------|-------|------|--------|--------------------------|
| 1 | 121774 | 1683 | core/telemetry/frame_graph.h | telemetry / frame-graph contract | NO-SPLIT — single-owner telemetry contract header (FrameGraph queue/node/statistics views); narrow per-concern header already, not a mega umbrella (§18). |
| 2 | 98292 | 1864 | impl/ecs_render/renderer_frame_pipeline_execute.cpp | render pipeline / execute phase | NO-SPLIT — one phase owner (`RendererFramePipeline::render` orchestration); sibling phases already sliced into separate files (§18 compliant). |
| 3 | 94524 | 1676 | impl/ecs_render/renderer_frame_pipeline_graphics_prefix.cpp | render pipeline / graphics-prefix phase | NO-SPLIT — single graphics-prefix phase owner; already split out from execute/graph. |
| 4 | 89874 | 1941 | core/telemetry/frame_graph.cpp | telemetry / frame-graph impl | NO-SPLIT — implementation half of #1, same single owner (accumulators + export). |
| 5 | 86987 | 1551 | impl/ecs_render/renderer_frame_pipeline_graph.cpp | render pipeline / graph build | NO-SPLIT — single graph-construction owner; shadow/surfel/caustics sub-phases live in their own files. |
| 6 | 83759 | 1825 | global/math/bounding_volumes.h | math / bounding volumes | NO-SPLIT — cohesive math API (Box/OBB/Frustum/Sphere + CollisionDetail); global-scope math per §2 exception. |
| 7 | 73270 | 1320 | impl/ecs_render/renderer_frame_pipeline_graph_shadow_visibility.cpp | render pipeline / shadow visibility | NO-SPLIT — already the §18-prescribed per-feature slice (shadow visibility extracted from graph). |
| 8 | 70999 | 1305 | impl/ecs_render/raytrace/raytracing_system.h | raytrace / system contract | NO-SPLIT — single RendererRayTracingSystem owner + closely-related task details; raytrace-owned slice. |
| 9 | 61803 | 1356 | core/task/gpu/task_graph.h | task/gpu / graph declaration | NO-SPLIT — single declaration-domain owner; compiler/runtime/storage live in sibling files. |
| 10 | 61688 | 1547 | impl/ecs_ui/system.cpp | ecs_ui / system | NO-SPLIT — one ECS UI system owner, `__hidden_ui` TU-local detail only. |
| 11 | 58091 | 1509 | impl/assets_shader/cook.cpp | assets / shader cook | NO-SPLIT — single shader-cooker owner (Slang + include/metascript stages of one pipeline). |
| 12 | 56091 | 1433 | impl/assets_mesh/cook_meshlets.h | assets / meshlet cook | NO-SPLIT — single meshlet-cook owner (frontier/score/precompute of one algorithm). |
| 13 | 54812 | 1145 | impl/ecs_mesh/skinning/system.cpp | mesh / skinning system | NO-SPLIT — one MeshSkinningSystem owner. |
| 14 | 54428 | 1248 | global/math/matrix.h | math / matrix | NO-SPLIT — cohesive matrix API + SIMDMatrixDetail; global-scope math per §2. |
| 15 | 53223 | 1303 | impl/assets_material/bind_typed_layout.cpp | material / typed bind | NO-SPLIT — single MaterialBindDetail owner. |
| 16 | 53218 | 1035 | impl/ecs_render/renderer_frame_pipeline_graph_shadow_prepare.cpp | render pipeline / shadow prepare | NO-SPLIT — already the per-feature slice for shadow prepare. |
| 17 | 52683 | 1028 | impl/ecs_render/raytrace/raytracing_system.cpp | raytrace / system impl | NO-SPLIT — impl half of #8, same owner. |
| 18 | 48865 | 1043 | core/graphics/vulkan/raytracing_commands_build.cpp | vulkan / raytracing commands | NO-SPLIT — single command-build owner, `__hidden` TU detail only. |
| 19 | 48260 | 715 | impl/ecs_render/deferred/deferred_targets.cpp | deferred / targets | NO-SPLIT — single deferred-target owner. |
| 20 | 47704 | 1004 | core/task/gpu/packet_runtime.h | task/gpu / packet runtime contract | NO-SPLIT — single runtime-contract owner; recording/transaction/preflight are siblings. |
| 21 | 45161 | 772 | impl/ecs_render/renderer_frame_pipeline_telemetry.cpp | render pipeline / telemetry | NO-SPLIT — already the extracted telemetry slice (frame-graph export + timing). |
| 22 | 44716 | 1093 | core/task/gpu/packet_runtime_recorded_graph.cpp | task/gpu / recorded graph | NO-SPLIT — single GpuRecordedGraph owner. |
| 23 | 43087 | 1235 | core/metascript/parser.cpp | metascript / parser | NO-SPLIT — single Parser owner. |
| 24 | 42105 | 1168 | global/math/convert.h | math / conversion | NO-SPLIT — cohesive half/SIMD/unsigned-float convert API; global-scope math per §2. |
| 25 | 41469 | 718 | core/graphics/api.cpp | graphics / api | NO-SPLIT — single graphics-API owner, `__hidden_graphics_api` only. |
| 26 | 41056 | 867 | impl/ecs_render/raytrace/rt_swbvh_bvh_infra.cpp | raytrace / swbvh infra | NO-SPLIT — single BVH-infra owner. |
| 27 | 40192 | 775 | core/graphics/vulkan/backend_context_device.cpp | vulkan / context+device | NO-SPLIT — single device-creation owner. |
| 28 | 40008 | 629 | impl/ecs_render/renderer_frame_pipeline.h | render pipeline / contract | NO-SPLIT — narrow pipeline contract header (pipeline + cache/graph views of one owner). |
| 29 | 39933 | 758 | core/task/gpu/compiler_resource_state.cpp | task/gpu / compiler resource state | NO-SPLIT — single compiler-sub-concern owner (resource state). |
| 30 | 38852 | 736 | impl/ecs_render/raytrace/hardware_caustics_stage_builder.cpp | raytrace / caustics stage | NO-SPLIT — already the per-feature caustics slice. |
| 31 | 38436 | 1066 | impl/assets_material/material_bind_codegen.cpp | material / bind codegen | NO-SPLIT — single bind-codegen owner (MaterialBindDetail + MaterialCookDetail of one pipeline). |
| 32 | 37535 | 1105 | core/frame/linux_wayland.cpp | frame / wayland platform | NO-SPLIT — single WaylandContext platform owner. |
| 33 | 37389 | 791 | core/graphics/vulkan/texture_clear_detail.h | vulkan / texture clear | NO-SPLIT — narrow per-concern header (clear targets/layouts). |
| 34 | 36803 | 748 | impl/ecs_render/renderer_frame_pipeline_graph_surfel_gi.cpp | render pipeline / surfel-gi | NO-SPLIT — already the per-feature surfel-GI slice. |
| 35 | 36623 | 798 | core/task/gpu/task_graph_storage.cpp | task/gpu / storage | NO-SPLIT — single storage owner (append/rollback scopes). |
| 36 | 36457 | 690 | impl/ecs_render/avboit/accumulation_record_builder.cpp | avboit / accumulation | NO-SPLIT — single record-builder owner; occupancy/extinction are siblings. |
| 37 | 35797 | 834 | impl/ecs_render/raytrace/rt_shadow_tasks.h | raytrace / shadow tasks | NO-SPLIT — cohesive shadow task family of one feature (opaque/transparent variants). |
| 38 | 35695 | 745 | core/task/gpu/compiler.cpp | task/gpu / compiler entry | NO-SPLIT — single compiler-entry owner; analysis/finalization/resource-state are siblings. |
| 39 | 35296 | 759 | global/math/vector_construct.h | math / vector construct | NO-SPLIT — cohesive vector-construction API; global-scope math per §2. |
| 40 | 35087 | 725 | impl/ecs_render/renderer_frame_pipeline_graph_caustics.cpp | render pipeline / caustics | NO-SPLIT — already the per-feature caustics slice. |
| 41 | 34786 | 683 | core/graphics/vulkan/device.cpp | vulkan / device | NO-SPLIT — single device owner. |
| 42 | 34649 | 665 | impl/ecs_render/avboit/occupancy_record_builder.cpp | avboit / occupancy | NO-SPLIT — single record-builder owner. |
| 43 | 34449 | 657 | impl/ecs_render/avboit/extinction_record_builder.cpp | avboit / extinction | NO-SPLIT — single record-builder owner. |
| 44 | 34318 | 705 | core/task/gpu/compiled_graph.h | task/gpu / compiled graph | NO-SPLIT — single compiled-graph contract owner. |
| 45 | 34278 | 682 | core/task/gpu/compiler_finalization.cpp | task/gpu / compiler finalize | NO-SPLIT — single finalization sub-concern owner. |
| 46 | 34139 | 723 | core/graphics/vulkan/state_tracking_handoff.cpp | vulkan / state handoff | NO-SPLIT — single handoff owner. |
| 47 | 33917 | 735 | core/task/gpu/packet_runtime_preflight.cpp | task/gpu / preflight | NO-SPLIT — single preflight sub-concern owner. |
| 48 | 33470 | 720 | core/graphics/vulkan/backend_command_list.h | vulkan / command list | NO-SPLIT — narrow command-list/state-tracker contract of one owner. |
| 49 | 33184 | 787 | core/task/gpu/packet_runtime_recording.cpp | task/gpu / recording | NO-SPLIT — single recording sub-concern owner. |
| 50 | 33177 | 804 | core/task/gpu/packet_runtime_transaction.cpp | task/gpu / transaction | NO-SPLIT — single transaction sub-concern owner. |
| 51 | 33108 | 692 | impl/ecs_render/raytrace/rt_caustics_pipelines.cpp | raytrace / caustics pipelines | NO-SPLIT — single caustics-pipeline owner. |
| 52 | 32726 | 659 | core/graphics/vulkan/raytracing_accel_resources.cpp | vulkan / accel resources | NO-SPLIT — single accel-resource owner. |
| 53 | 32648 | 719 | core/task/gpu/compiler_analysis.cpp | task/gpu / compiler analysis | NO-SPLIT — single analysis sub-concern owner. |
| 54 | 32611 | 759 | impl/ecs_render/material/material_pass.cpp | material / pass | NO-SPLIT — single material-pass owner. |
| 55 | 32521 | 519 | impl/ecs_render/raytrace/rt_softshadow_dispatch.cpp | raytrace / soft shadow | NO-SPLIT — single dispatch owner. |
| 56 | 32314 | 722 | core/graphics/vulkan/gpu_descriptor_heap.cpp | vulkan / descriptor heap | NO-SPLIT — single heap owner; descriptor-buffer variant is a sibling. |
| 57 | 32216 | 728 | core/graphics/vulkan/state_tracking_barriers.cpp | vulkan / barriers | NO-SPLIT — single barrier owner. |
| 58 | 31669 | 715 | core/graphics/vulkan/graphics_pipeline.cpp | vulkan / graphics pipeline | NO-SPLIT — single pipeline owner. |
| 59 | 31664 | 676 | impl/ecs_render/raytrace/rt_surfel_pipelines.cpp | raytrace / surfel pipelines | NO-SPLIT — single surfel-pipeline owner. |
| 60 | 31610 | 776 | core/graphics/gpu_timing_accumulator.cpp | graphics / gpu timing | NO-SPLIT — single timing-accumulator owner. |
| 61 | 31525 | 704 | impl/ecs_render/material/material_surface.cpp | material / surface | NO-SPLIT — single surface owner. |
| 62 | 31353 | 690 | core/graphics/gpu_timing.h | graphics / gpu timing contract | NO-SPLIT — narrow timing contract header. |
| 63 | 31139 | 733 | impl/ecs_render/avboit/avboit_pass.cpp | avboit / pass | NO-SPLIT — single pass owner. |
| 64 | 31125 | 790 | core/graphics/runtime/runtime.cpp | graphics / runtime | NO-SPLIT — single runtime owner. |
| 65 | 31057 | 756 | impl/assets_material/material_dispatch_codegen.cpp | material / dispatch codegen | NO-SPLIT — single dispatch-codegen owner. |
| 66 | 30981 | 784 | core/task/cpu/scheduler.cpp | task/cpu / scheduler | NO-SPLIT — single scheduler owner. |
| 67 | 30894 | 719 | core/graphics/vulkan/queries.cpp | vulkan / queries | NO-SPLIT — single query owner. |
| 68 | 30734 | 677 | core/graphics/vulkan/queue_submission.cpp | vulkan / queue submit | NO-SPLIT — single submission owner. |
| 69 | 30693 | 743 | core/task/gpu/task_graph_imports.cpp | task/gpu / imports | NO-SPLIT — single import sub-concern owner. |
| 70 | 30495 | 641 | core/graphics/vulkan/gpu_descriptor_heap_descriptor_buffer.cpp | vulkan / descriptor buffer | NO-SPLIT — single descriptor-buffer variant owner (sibling of heap). |
| 71 | 30454 | 628 | impl/ecs_render/avboit/task_graph_stage_validation.cpp | avboit / stage validation | NO-SPLIT — single validation owner. |
| 72 | 30395 | 652 | core/graphics/vulkan/backend_device.h | vulkan / device contract | NO-SPLIT — narrow device contract header. |

Result: **72/72 reviewed, 0 splits required** — the tree is already sliced by concrete owner/concern per §18 (pipeline phases, raytrace features, task/gpu compiler-vs-runtime-vs-storage, vulkan per-object files, avboit per-builder files, math per-topic headers). No god-file consolidation remains that would justify a separated-class split, and no `.inl` fragment split exists or was introduced.

## Leftover re-check (uncovered large sources — all accounted for, none in scope)

- Tracked files >= 30000B total: 380 (30000B threshold) / 368 (30720B threshold).
- `3rd_parties/` vendored: ~226 — out of scope (upstream code, §17).
- Test sources (`tests/**/*.h/.hpp/.cpp/.c/.cc` >= 30000B): 49 — out of scope for the production split review (per-contract test files, each bound to one contract under test).
- Other large non-source artifacts: 28 (`.nwb` binary payloads, `.py` harnesses, `.md` docs, `CMakeLists.txt`, `.helper/standard.md`, `.helper/note.md`, `launcher/__init__.py`) — not splittable C++ sources.
- Leftover production sources below threshold: every other first-party source is < 30000B — no uncovered large production source remains.
- `__exec/` outputs: build/verification artifacts, untracked/ignored — not sources.

## Status proof

- `git status --short` before this doc: clean (no pending modifications).
- This review adds only `docs/large_source_domain_review.md`; no source file was modified, so no build/test re-verification is triggered by this change.
