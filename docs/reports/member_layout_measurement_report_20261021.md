# Member layout measurement report — 2026-10-21

This report records the complete first-party member-layout audit, concrete object-size comparisons and the native build/runtime qualification for this change. Raw measurement and verification outputs remain in `__artifacts/layout/`.

## Comparison identity and scope

The comparison baseline is `cfa296d43bcccb7615d2eebe5afb861c09e55cb7` on `main`. The baseline was synchronized with GitHub `origin`; the retained pull reported `Already up to date.` with exit 0. The layout measurements compare that original HEAD with the current source snapshot. The comparison uses the sealed first-party source manifest with SHA-256 `04de32b0c07f4237200891d7a48d358ff700041d2a062a3aae2395f25d839a34`. The publication commit is the commit containing this report; its exact GitHub `origin/main` identity and remote verification are recorded separately in the completion evidence.

The scope contains all 2,426 tracked first-party C++ files: 1,349 `.cpp` and 1,077 `.h`. An extension reconciliation found no additional first-party C/C++/Objective-C/CUDA/module source extensions. The handoff inventory contains 3,114 definition entries, including 58 unions, and 517 forward declarations. Worker inventories reconcile as `1322 + 1132 + (685 − 25 test forward declarations) = 3114` definition entries. Forward declarations have no instance fields to reorder.

The enhanced lexical enumeration has 3,205 named declaration entries: 852 `class`, 2,337 `struct` and 16 `interface` entries. This count includes macro placeholders and qualified nested-name corrections; it is not a count of distinct instantiated types. Anonymous overlays and unions remain covered by the handoff/compiler inventories. Every original and enhanced target has a disposition, including every one of the 72 initial compiler misses. Those misses resolve through 50 core/global/logger reviews, 18 test reviews and four qualified implementation definitions. The coverage manifest contains exact file/line entries and evidence pointers, with zero unclassified targets.

The change directly reorders 92 definitions across 121 C++ files. The owner ledger assigns 27 definitions to core/global/logger, 40 to implementation modules and 25 to tests. Constructor and aggregate-producer adjustments account for changed use files without a directly reordered definition. All member names, types, defaults and access remain: the registry finds zero normalized declaration-text changes, zero member-access changes and zero unclassified changed files. The audit preserves nontrivial owner/arena/callback relative lifetimes, union overlays, bit positions, explicit alignment, serialized formats and external GPU layouts.

The shader scope separately covers 187 declaration entries across 299 `.slang`, `.slangi`, `.bind`, `.bxdf` and `.surface` files. Their authored shader, binding, transport and stage contracts are unchanged. All 2,292 tracked vendor files are preserved. The original vendor C/C++ filter contains 1,998 files; a broader filter also finds one existing `.cppm`, for 1,999. Vendor layouts are outside the first-party reordering scope.

## Measurement method and limits

The retained libclang records contain actual field offsets, `sizeof`, alignment, base information, bitfield markers and concrete template identities under the configured Windows command lines. Each safe candidate is compared with its field/alignment lower bound while preserving ownership and explicit layout constraints. The final composite selects the latest valid evidence for every configured command profile; changed inputs invalidate affected evidence, and bounded reparses replace those entries. Whole-profile reconciliation ends with zero safe reordering candidates, zero current error profiles, zero stale-input profiles and zero coverage gaps.

The ordinary before/after entries below use measured original-HEAD **ARM64 dbg** records matched by concrete type and complete member-name/type multiset. They give exact object-byte counts for those records. The deduplicated baseline does not retain exact per-target macro/command provenance, so ordinary rows do not claim an exact command-pair comparison. CommandList, the 24 Transparent profiles and the optional benchmark probe have separate explicit before/after evidence. Other architecture/configuration rows in the registry are **after-only** unless an explicit pair is recorded; no before value is inferred for them.

Object-size changes can include the sizes of nested member objects. They are not additive across this table: adding a child row and its containing class would count part of the same storage twice. A smaller `sizeof` does not establish an equal reduction in heap allocation, resident set size, committed GPU memory, CPU cost or frame time. No FPS, RSS or GPU-memory improvement is claimed. Unchanged `sizeof` also does not establish unchanged member offsets or ABI.

The libclang extraction appends `-Wno-everything` and retains diagnostics with severity at least error. Its zero-error counts are not evidence of zero compiler warnings. Warning evidence comes from the actual native dbg builds and the full runtime-log classification.

## Directly reordered definitions

Every one of the 92 registry IDs appears once in the following three tables. The ID is `current file:line:name`; it links to the declaration. Byte values are decimal `sizeof` values. Full qualified names, all concrete instantiations, before/current field orders, profile IDs and raw record pointers are in the per-target JSON/TSV ledger.

Evidence codes: **M** is the measured ARM64 dbg concrete-type/member-set comparison described above; **C** is an explicit conditional/configuration comparison; **O** is an optional compiler probe with no configured native-target/runtime claim; **S** is source proof without an absolute native-size measurement.

### Core, global and logger — 27 definitions

| Current definition ID | Before → current bytes / scope | Evidence |
| --- | --- | --- |
| [core/crash/internal.h:365:CrashState](../../core/crash/internal.h#L365) | `170240 → 170224` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/0) |
| [core/filesystem/volume_file_system.h:23:VolumeFileSystem](../../core/filesystem/volume_file_system.h#L23) | `512 → 504` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/1) |
| [core/graphics/runtime/runtime.h:34:GraphicsRuntime](../../core/graphics/runtime/runtime.h#L34) | `1600 → 1592` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/2) |
| [core/graphics/runtime/runtime_texture_upload.cpp:130:TextureUploadBatchSubmissionData](../../core/graphics/runtime/runtime_texture_upload.cpp#L130) | `48 → 40` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/3) |
| [core/graphics/vulkan/backend_command_list.h:56:CommandList](../../core/graphics/vulkan/backend_command_list.h#L56) | `3184 → 3184` (exact dbg pair); opt/fin `3056 → 3048` | [C](../../__artifacts/layout/final-changed-targets.json#/targets/4) |
| [core/graphics/vulkan/backend_context.h:25:BackendContext](../../core/graphics/vulkan/backend_context.h#L25) | `8192 → 8176` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/5) |
| [core/graphics/vulkan/backend_memory_upload.h:107:UploadManager](../../core/graphics/vulkan/backend_memory_upload.h#L107) | `96 → 88` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/6) |
| [core/graphics/vulkan/backend_memory_upload.h:306:Texture](../../core/graphics/vulkan/backend_memory_upload.h#L306) | `1192 → 1184` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/7) |
| [core/graphics/vulkan/backend_memory_upload.h:385:StagingTexture](../../core/graphics/vulkan/backend_memory_upload.h#L385) | `968 → 960` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/8) |
| [core/graphics/vulkan/backend_pipeline_state.h:244:GpuDescriptorHeap](../../core/graphics/vulkan/backend_pipeline_state.h#L244) | `1016 → 1008` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/9) |
| [core/input/module.h:240:InputDispatcher](../../core/input/module.h#L240) | `704 → 696` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/10) |
| [core/os/clipboard_service.h:23:QueuedClipboardService](../../core/os/clipboard_service.h#L23) | `88 → 80` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/11) |
| [core/os/linux/wayland/clipboard_service.h:31:WaylandClipboardService](../../core/os/linux/wayland/clipboard_service.h#L31) | Absolute size unmeasured; enabled primary-selection form removes 8 B by source proof; disabled form retains size | [S](../../__artifacts/layout/final-changed-targets.json#/targets/12) |
| [core/os/text_input.h:100:TextInputEvent](../../core/os/text_input.h#L100) | `128 → 120` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/13) |
| [core/os/text_input_service.h:23:QueuedTextInputService](../../core/os/text_input_service.h#L23) | `224 → 216` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/14) |
| [core/os/win32/text_input_service.h:21:Win32TextInputService](../../core/os/win32/text_input_service.h#L21) | `432 → 416` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/15) |
| [core/task/cpu/scheduler.h:31:CpuTaskScheduler](../../core/task/cpu/scheduler.h#L31) | `1248 → 1232` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/16) |
| [core/task/gpu/capture/command_ir_raster.h:128:GpuCommandIrRasterTaskRecord](../../core/task/gpu/capture/command_ir_raster.h#L128) | `728 → 720` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/17) |
| [core/task/gpu/packet_runtime.h:524:PacketRuntime](../../core/task/gpu/packet_runtime.h#L524) | `88 → 80` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/18) |
| [core/task/gpu/task_desc.h:148:GpuGraphResourceDesc](../../core/task/gpu/task_desc.h#L148) | `408 → 392` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/19) |
| [core/task/gpu/task_graph.h:37:GpuTaskGraphTaskView](../../core/task/gpu/task_graph.h#L37) | `832 → 824` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/20) |
| [core/task/gpu/task_graph.h:599:GpuTaskNode](../../core/task/gpu/task_graph.h#L599) | `832 → 824` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/21) |
| [global/fixed_index_map.h:19:Slot](../../global/fixed_index_map.h#L19) | `u32` key, default/custom hasher: `24 → 16`; Buffer*/Texture* key: `24 → 24`; TextureSubresourceStateKey: `32 → 32` | [M; generic](../../__artifacts/layout/final-changed-targets.json#/targets/22) |
| [global/process_execution.h:110:ProcessCaptureFailure](../../global/process_execution.h#L110) | `24 → 20` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/23) |
| [global/unicode/grapheme.h:15:GraphemeBoundaryState](../../global/unicode/grapheme.h#L15) | `24 → 16` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/24) |
| [logger/server/module.cpp:64:ConnectionInfo](../../logger/server/module.cpp#L64) | `352 → 344` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/65) |
| [logger/telemetry/report.h:45:TelemetryReportSummary](../../logger/telemetry/report.h#L45) | `328 → 312` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/66) |

### Implementation modules — 40 definitions

| Current definition ID | Before → current bytes / scope | Evidence |
| --- | --- | --- |
| [impl/ecs_mesh/runtime/mesh.h:125:RuntimeMeshDesc](../../impl/ecs_mesh/runtime/mesh.h#L125) | `656 → 640` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/25) |
| [impl/ecs_model/system.h:81:ModelAttachmentJointQuery](../../impl/ecs_model/system.h#L81) | `80 → 64` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/26) |
| [impl/ecs_render/avboit/task_graph_accumulation_tasks.h:141:Payload](../../impl/ecs_render/avboit/task_graph_accumulation_tasks.h#L141) | `600 → 592` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/27) |
| [impl/ecs_render/avboit/task_graph_extinction_integration_tasks.h:144:Payload](../../impl/ecs_render/avboit/task_graph_extinction_integration_tasks.h#L144) | `600 → 592` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/28) |
| [impl/ecs_render/components.h:45:RendererComponent](../../impl/ecs_render/components.h#L45) | `664 → 656` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/29) |
| [impl/ecs_render/csg/shadow_cutter_snapshot.h:22:CsgShadowReceiverInput](../../impl/ecs_render/csg/shadow_cutter_snapshot.h#L22) | `36 → 32` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/30) |
| [impl/ecs_render/deferred/task_graph_prefix_tasks.h:39:Payload](../../impl/ecs_render/deferred/task_graph_prefix_tasks.h#L39) | `5184 → 5168` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/31) |
| [impl/ecs_render/deferred/task_graph_suffix_builder.h:38:DeferredGraphSuffixInputs](../../impl/ecs_render/deferred/task_graph_suffix_builder.h#L38) | `320 → 312` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/32) |
| [impl/ecs_render/graph/frame_graph_reflection_resolve.h:83:FrameGraphReflectionResolveResult](../../impl/ecs_render/graph/frame_graph_reflection_resolve.h#L83) | `120 → 112` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/33) |
| [impl/ecs_render/mesh/renderer_mesh_types.h:56:MeshResources](../../impl/ecs_render/mesh/renderer_mesh_types.h#L56) | `896 → 880` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/34) |
| [impl/ecs_render/raytrace/graph_snapshots.h:43:RayTracingShadowVisibilityGraphPlanSnapshot](../../impl/ecs_render/raytrace/graph_snapshots.h#L43) | `8416 → 8400` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/35) |
| [impl/ecs_render/raytrace/optical_scene.h:53:RayTracingOpticalRuntimeBounds](../../impl/ecs_render/raytrace/optical_scene.h#L53) | `96 → 80` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/36) |
| [impl/ecs_render/raytrace/raytracing_system.h:115:RendererRayTracingSystem](../../impl/ecs_render/raytrace/raytracing_system.h#L115) | `11872 → 11792` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/37) |
| [impl/ecs_render/raytrace/renderer_raytracing_state.h:53:RtSceneBvhState](../../impl/ecs_render/raytrace/renderer_raytracing_state.h#L53) | `656 → 648` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/38) |
| [impl/ecs_render/raytrace/rt_shadow_tasks.h:60:Payload](../../impl/ecs_render/raytrace/rt_shadow_tasks.h#L60) | `8512 → 8496` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/39) |
| [impl/ecs_render/raytrace/rt_shadow_tasks.h:544:Payload](../../impl/ecs_render/raytrace/rt_shadow_tasks.h#L544) | `88 → 80` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/40) |
| [impl/ecs_render/raytrace/task_graph_shadow_prepare_tasks.h:67:Payload](../../impl/ecs_render/raytrace/task_graph_shadow_prepare_tasks.h#L67) | `624 → 608` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/41) |
| [impl/ecs_render/reflection/task_graph_reflection.cpp:333:Payload](../../impl/ecs_render/reflection/task_graph_reflection.cpp#L333) | `512 → 504` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/42) |
| [impl/ecs_render/renderer_frame_pipeline.h:113:RendererFramePipeline](../../impl/ecs_render/renderer_frame_pipeline.h#L113) | `50560 → 50448` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/43) |
| [impl/ecs_render/shadow/light_space_shadow.h:172:LightSpaceShadowState](../../impl/ecs_render/shadow/light_space_shadow.h#L172) | `9360 → 9344` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/44) |
| [impl/ecs_ui/edit_box_host.h:26:UiEditBoxGeometry](../../impl/ecs_ui/edit_box_host.h#L26) | `272 → 264` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/45) |
| [impl/ecs_ui/edit_box_host.h:89:Event](../../impl/ecs_ui/edit_box_host.h#L89) | `280 → 264` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/46) |
| [impl/ecs_ui/layer_system.h:46:UiLayerSystem](../../impl/ecs_ui/layer_system.h#L46) | `47136 → 47096` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/47) |
| [impl/ecs_ui/text_edit_session.h:45:UiTextEditResult](../../impl/ecs_ui/text_edit_session.h#L45) | `24 → 16` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/48) |
| [impl/ecs_ui/toolkit/builder_scope.h:43:BuilderScopeFrame](../../impl/ecs_ui/toolkit/builder_scope.h#L43) | `13056 → 13040` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/49) |
| [impl/ecs_ui/toolkit/builder_scope.h:92:WindowFrame](../../impl/ecs_ui/toolkit/builder_scope.h#L92) | `5960 → 5952` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/50) |
| [impl/ecs_ui/toolkit/builder_scope.h:162:ListFrame](../../impl/ecs_ui/toolkit/builder_scope.h#L162) | `10968 → 10960` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/51) |
| [impl/ecs_ui/toolkit/context.h:25:Context](../../impl/ecs_ui/toolkit/context.h#L25) | `2400 → 2392` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/52) |
| [impl/ecs_ui/toolkit/edit/model.h:53:EditModel](../../impl/ecs_ui/toolkit/edit/model.h#L53) | `328 → 320` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/53) |
| [impl/ecs_ui/toolkit/gpu/renderer_internal.h:168:GpuFrameData](../../impl/ecs_ui/toolkit/gpu/renderer_internal.h#L168) | `1304 → 1296` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/54) |
| [impl/ecs_ui/toolkit/input/input.h:42:InputEvent](../../impl/ecs_ui/toolkit/input/input.h#L42) | `72 → 64` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/55) |
| [impl/ecs_ui/toolkit/input/pointer.h:30:PointerInput](../../impl/ecs_ui/toolkit/input/pointer.h#L30) | `64 → 56` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/56) |
| [impl/ecs_ui/toolkit/layout/tree.h:39:LayoutNodeDesc](../../impl/ecs_ui/toolkit/layout/tree.h#L39) | `52 → 48` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/57) |
| [impl/ecs_ui/toolkit/text/atlas.h:37:AtlasPage](../../impl/ecs_ui/toolkit/text/atlas.h#L37) | `104 → 96` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/58) |
| [impl/ecs_ui/toolkit/text/font.cpp:62:FontFaceState](../../impl/ecs_ui/toolkit/text/font.cpp#L62) | `480 → 472` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/59) |
| [impl/ecs_ui/toolkit/widgets/popup.h:50:PopupState](../../impl/ecs_ui/toolkit/widgets/popup.h#L50) | `104 → 96` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/60) |
| [impl/ecs_ui/toolkit/widgets/radio_group_frame.h:24:RadioGroupFrame](../../impl/ecs_ui/toolkit/widgets/radio_group_frame.h#L24) | `10032 → 10024` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/61) |
| [impl/ecs_ui/toolkit/widgets/slider.h:22:SliderOptions](../../impl/ecs_ui/toolkit/widgets/slider.h#L22) | `48 → 40` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/62) |
| [impl/ecs_ui/toolkit/widgets/tooltip.h:24:TooltipOptions](../../impl/ecs_ui/toolkit/widgets/tooltip.h#L24) | `20 → 16` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/63) |
| [impl/ecs_ui/toolkit/widgets/tooltip.h:37:TooltipState](../../impl/ecs_ui/toolkit/widgets/tooltip.h#L37) | `168 → 160` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/64) |

### Tests — 25 definitions

| Current definition ID | Before → current bytes / scope | Evidence |
| --- | --- | --- |
| [tests/common/capturing_logger.h:31:CapturingLogger](../../tests/common/capturing_logger.h#L31) | `192 → 184` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/67) |
| [tests/performance/ui_input/pointer.cpp:34:Workload](../../tests/performance/ui_input/pointer.cpp#L34) | `1216 → 1208` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/68) |
| [tests/smoke/gi_test_project.cpp:174:GiTestSmokeProject](../../tests/smoke/gi_test_project.cpp#L174) | `448 → 440` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/69) |
| [tests/smoke/reflection_csg_context_probe.h:42:ReflectionCsgContextProbe](../../tests/smoke/reflection_csg_context_probe.h#L42) | `160 → 152` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/70) |
| [tests/smoke/reflection_project.cpp:162:ReflectionSmokeProject](../../tests/smoke/reflection_project.cpp#L162) | `760 → 752` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/71) |
| [tests/smoke/renderer_gather_benchmark_probe.h:31:RendererGatherBenchmarkProbe](../../tests/smoke/renderer_gather_benchmark_probe.h#L31) | `1416 → 1408` under borrowed ARM64 dbg flags; optional native target unconfigured | [O](../../__artifacts/layout/final-changed-targets.json#/targets/72) |
| [tests/smoke/skinned_caustic_project.cpp:128:SkinnedCausticSmokeProject](../../tests/smoke/skinned_caustic_project.cpp#L128) | `504 → 496` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/73) |
| [tests/smoke/skinning_culling_benchmark_project.cpp:289:SkinningCullingBenchmarkProject](../../tests/smoke/skinning_culling_benchmark_project.cpp#L289) | `352 → 344` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/74) |
| [tests/smoke/soft_shadow_test_project.cpp:106:SoftShadowTestSmokeProject](../../tests/smoke/soft_shadow_test_project.cpp#L106) | `328 → 320` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/75) |
| [tests/smoke/stress_test_project.cpp:147:StressTestSmokeProject](../../tests/smoke/stress_test_project.cpp#L147) | `418456 → 418448` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/76) |
| [tests/smoke/transparent_multi_project.cpp:307:TransparentMultiSmokeProject](../../tests/smoke/transparent_multi_project.cpp#L307) | ARM64 dbg ordinary `328 → 328`; async `368 → 360`; CSG `336 → 336`; caustic `344 → 344`; all 24 pairs below | [C](../../__artifacts/layout/final-changed-targets.json#/targets/77) |
| [tests/smoke/ui_layer/combo_scene.h:32:UiComboSmokeScene](../../tests/smoke/ui_layer/combo_scene.h#L32) | `1000 → 984` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/78) |
| [tests/smoke/ui_layer/popup_tools_scene.h:33:UiPopupToolsSmokeScene](../../tests/smoke/ui_layer/popup_tools_scene.h#L33) | `1616 → 1584` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/79) |
| [tests/unit/ecs_ui/edit_box_host_fixture.h:96:UiEditBoxHostTests](../../tests/unit/ecs_ui/edit_box_host_fixture.h#L96) | `5424 → 5376` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/80) |
| [tests/unit/ecs_ui/popup_input_fixture.h:97:UiPopupInputTests](../../tests/unit/ecs_ui/popup_input_fixture.h#L97) | `6272 → 6192` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/81) |
| [tests/unit/os/text_input_tests.cpp:23:FakeTextInputService](../../tests/unit/os/text_input_tests.cpp#L23) | `568 → 552` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/82) |
| [tests/unit/os/win32_text_input_tests.cpp:29:Win32TextInputFixture](../../tests/unit/os/win32_text_input_tests.cpp#L29) | `416 → 400` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/83) |
| [tests/unit/ui/combo_behavior_tests.cpp:34:ComboSource](../../tests/unit/ui/combo_behavior_tests.cpp#L34) | `72 → 64` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/84) |
| [tests/unit/ui/edit_caret_fixture.h:38:CaretShaper](../../tests/unit/ui/edit_caret_fixture.h#L38) | `32 → 24` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/85) |
| [tests/unit/ui/nested_popup_reentry_tests.cpp:27:ReentrySource](../../tests/unit/ui/nested_popup_reentry_tests.cpp#L27) | `208 → 200` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/86) |
| [tests/unit/ui/radio_group_behavior_tests.cpp:39:ReconcileGate](../../tests/unit/ui/radio_group_behavior_tests.cpp#L39) | `32 → 24` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/87) |
| [tests/unit/ui/radio_group_behavior_tests.cpp:56:ChoiceSource](../../tests/unit/ui/radio_group_behavior_tests.cpp#L56) | `2160 → 2152` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/88) |
| [tests/unit/ui/radio_group_fixture.h:47:RadioSource](../../tests/unit/ui/radio_group_fixture.h#L47) | `136 → 128` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/89) |
| [tests/unit/ui/search_combo_fixture.h:173:SearchEditHost](../../tests/unit/ui/search_combo_fixture.h#L173) | `176 → 168` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/90) |
| [tests/unit/ui/text_area_fixture.h:38:AreaEvent](../../tests/unit/ui/text_area_fixture.h#L38) | `80 → 72` | [M](../../__artifacts/layout/final-changed-targets.json#/targets/91) |

## Conditional and generic layouts

### CommandList configuration pairs

Five default bool flags move into the alignment gap after the byte-sized atomic publication state. Their constant defaults, native IDs, atomic acquire/release/CAS protocol and nontrivial member order remain. The explicit before/after probes reach the lower bound in all three configurations with zero recorded errors.

| Architecture | Configuration | Before bytes | Current bytes | Difference |
| --- | --- | ---: | ---: | ---: |
| ARM64 | dbg | 3184 | 3184 | 0 |
| ARM64 | opt | 3056 | 3048 | 8 fewer |
| ARM64 | fin | 3056 | 3048 | 8 fewer |

### Transparent smoke project: all 24 paired profiles

The frame counter fills the gap before yaw/input state, and flags move beside compatible scalar state after optional helper/owner blocks. Every nontrivial member keeps its relative order. All four target macro forms, three configurations and two architectures have explicit before/after records. Six frame-lagged async-lighting profiles shrink by eight bytes; the other 18 retain object size. Every after size equals its measured alignment lower bound, with zero parse errors, error diagnostics, regressions or remaining candidates.

| Architecture | Configuration | Target macro form | Before bytes | Current bytes | Difference |
| --- | --- | --- | ---: | ---: | ---: |
| arm64 | dbg | `nwb_transparent_multi_smoke` | 328 | 328 | unchanged |
| arm64 | opt | `nwb_transparent_multi_smoke` | 320 | 320 | unchanged |
| arm64 | fin | `nwb_transparent_multi_smoke` | 320 | 320 | unchanged |
| arm64 | dbg | `nwb_frame_lagged_async_lighting_smoke` | 368 | 360 | 8 fewer |
| arm64 | opt | `nwb_frame_lagged_async_lighting_smoke` | 360 | 352 | 8 fewer |
| arm64 | fin | `nwb_frame_lagged_async_lighting_smoke` | 360 | 352 | 8 fewer |
| arm64 | dbg | `nwb_transparent_csg_smoke` | 336 | 336 | unchanged |
| arm64 | opt | `nwb_transparent_csg_smoke` | 328 | 328 | unchanged |
| arm64 | fin | `nwb_transparent_csg_smoke` | 328 | 328 | unchanged |
| arm64 | dbg | `nwb_caustic_sphere_smoke` | 344 | 344 | unchanged |
| arm64 | opt | `nwb_caustic_sphere_smoke` | 336 | 336 | unchanged |
| arm64 | fin | `nwb_caustic_sphere_smoke` | 336 | 336 | unchanged |
| x64 | dbg | `nwb_transparent_multi_smoke` | 328 | 328 | unchanged |
| x64 | opt | `nwb_transparent_multi_smoke` | 320 | 320 | unchanged |
| x64 | fin | `nwb_transparent_multi_smoke` | 320 | 320 | unchanged |
| x64 | dbg | `nwb_frame_lagged_async_lighting_smoke` | 368 | 360 | 8 fewer |
| x64 | opt | `nwb_frame_lagged_async_lighting_smoke` | 360 | 352 | 8 fewer |
| x64 | fin | `nwb_frame_lagged_async_lighting_smoke` | 360 | 352 | 8 fewer |
| x64 | dbg | `nwb_transparent_csg_smoke` | 336 | 336 | unchanged |
| x64 | opt | `nwb_transparent_csg_smoke` | 328 | 328 | unchanged |
| x64 | fin | `nwb_transparent_csg_smoke` | 328 | 328 | unchanged |
| x64 | dbg | `nwb_caustic_sphere_smoke` | 344 | 344 | unchanged |
| x64 | opt | `nwb_caustic_sphere_smoke` | 336 | 336 | unchanged |
| x64 | fin | `nwb_caustic_sphere_smoke` | 336 | 336 | unchanged |

### Generic map slot, source-only platform and optional target

`FixedIndexMap::Slot` retains its generic key, occupied flag and index value. Placing the occupied byte beside a `u32` key removes an alignment island: both the default-hasher and custom-hasher measured `u32` instantiations change from 24 to 16 bytes. The measured Buffer* and Texture* key slots remain 24 bytes; TextureSubresourceStateKey slots remain 32 bytes. Dependent or over-aligned keys do not receive a guessed absolute size or a universal saving claim. Default resizing and named field assignment preserve the private slot's use contract.

WaylandClipboardService has a source-reviewed improvement in the primary-selection-enabled branch: Timer and the focused flag no longer leave a separate eight-byte flag island. The disabled branch retains its extent. Native Linux headers/provider execution were unavailable, so this report records the source proof and does not supply an absolute Linux `sizeof` or runtime qualification.

RendererGatherBenchmarkProbe is measured 1416 → 1408 under borrowed ARM64 dbg compile flags, with full function bodies parsed. Its optional native target is unconfigured; these compiler probes do not establish a native target build or benchmark run. The explicit before files are `tests-extra-probe/0.json` and `2.json`; after files are `optional-final/0000.json` and `0002.json`.

StressTestSmokeProject's ordinary ARM64 dbg form is measured 418456 → 418448. A configured frame-graph-snapshot macro adds SmokeEnvironmentString storage. That extra profile has after-only evidence because the original deduplicated baseline contains the ordinary form; the larger conditional form is not presented as a measured before/after regression.

## Compiler and correctness qualification

The complete composite contains 6,165 selected command profiles, 34,702 architecture/configuration layout groups and 8,710 unique record keys. The first three modes contribute 4,632 retained compiler entries; the final x64 opt sweep contributes 1,533 fresh entries. All current sealed first-party input hashes match.

| Windows analysis mode | Command profiles | Layout groups | Safe candidates | Preserved native-protocol groups |
| --- | ---: | ---: | ---: | ---: |
| `arm64:dbg` | 1544 | 8689 | 0 | 1 |
| `arm64:opt` | 1544 | 8690 | 0 | 1 |
| `arm64:fin` | 1544 | 8696 | 0 | 1 |
| `x64:opt` | 1533 | 8627 | 0 | 1 |

CrashRequest deliberately retains its fixed version-2 native pipe and persisted Android emergency-request layout. It is not counted among the 92 reordered definitions. The compiler can identify theoretical padding in that record, but changing its current raw transport contract is outside a safe member reorder. The four protocol groups above are its four analysed modes. All 58 union records and seven bitfield records preserve overlay and bit-position semantics. Complete source dispositions cover 64 core dependent-template assessments, nine implementation symbolic assessments and 22 test assessments. Dependent records retain symbolic/source dispositions rather than receiving fictitious concrete sizes.

The final extraction has one retained provenance qualification: the initial dbg scan lacks a per-scan hash snapshot. Its provenance uses `source-hashes-stable.json`, the retained bounded-edit ledger and affected-entry supersession. The final composite reports zero stale profiles and exposes every selected profile and replacement. The optional nine-record probe set also has zero errors and zero safe candidates. These results do not claim that every operating system or optional provider was compiled.

Correctness review covers all changed definition and use files. In particular, the InputEvent reorder required 154 positional aggregates in 23 files to become explicit designators. A retained baseline AST walk over 689 translation units found 502 unique aggregate-initializer source groups, including anonymous function-argument braces. All 154 old argument-to-field mappings match their current designators exactly once, with zero unmatched mappings. Other changed positional and designated producers and constructor initializer order were reviewed alongside owner, arena, callback, cache/alignment and GPU/wire contracts. The final review has zero unresolved actionable findings; the field and file ledgers retain the exact scope and source hashes.

The canonical native dbg build completed using the existing build cache:

```text
python -m launcher build all --configure-preset windows-clang-arm64 --config dbg --jobs 8
```

The full retained final build exited 0 in 694.09 seconds. After the final conditional smoke edit, the sealed incremental build of the same target set exited 0 in 5.51 seconds. Full-log classification for both records reports zero compiler errors, zero compiler warnings, zero exceptions and zero unexpected diagnostic-word hits. Native opt/fin/x64 builds and runtime tests are not inferred from the libclang analyses.

## Runtime qualification

All 179 registered ARM64 dbg CTest suites have authoritative completed outcomes for unchanged sealed inputs: **178 passed, one capability skip, zero latest failures**. This includes actual application launches, GPU/window captures and scripted assertions in the registered smoke suites.

The original canonical invocation used:

```text
ctest --preset windows-clang-arm64-dbg --output-on-failure --parallel 4 --output-junit __artifacts/layout/ctest-dbg.xml
```

That outer process was interrupted after 113 complete per-suite records: 109 passed and four failed. Its outer exit is **null / unavailable**; no completed outer process result or missing JUnit is invented. Each retained result was independently matched between complete console and saved LastTest footers. The 109 successful suites remain valid because their source inputs did not change.

The exact 66-suite complement then completed with **exit 0 in 2554.8 seconds: 65 passed, one skipped**:

```text
ctest --preset windows-clang-arm64-dbg --tests-from-file __artifacts/layout/ctest-remaining-suites.txt --output-on-failure --parallel 4 --output-junit __artifacts/layout/ctest-dbg-remaining.xml
```

The skipped suite, `nwb_transparent_multi_sw_capture_smoke`, requires natural software-only shadow selection. Its complete output says that this RayQuery-capable host naturally selected the hardware route. CTest records the declared skip-return code 77. This is a capability limitation, not a software-route pass; the contract is in `tests/smoke/CMakeLists.txt:341–348` and `tests/smoke/transparent_multi_project.cpp:312–316`.

Each of the four original failures was rerun as its entire unchanged canonical suite, one at a time with `--parallel 1`. No timeout, assertion or test source was changed. Related-case successes were not substituted for those full retries. The first isolated numeric alternate retry also failed at the same caret gate (exit 8, 103.75 seconds); its complete logs, report and image are retained alongside the original failure.

| Original failed suite | Initial observation | Isolated full-suite retry |
| --- | --- | --- |
| `nwb_font_builder_cli_integration` | Timeout at 420.03 s during concurrent analysis; exact cause unproven | Passed; exit 0; 240.52 s; `ctest-dbg-retry-01-result.json` |
| `nwb_ui_layer_numeric_edit_skin_smoke` | Original failed at 104.48 s; isolated retry02 also failed at 103.75 s. Model/geometry and 82 of 83 probes matched, blinking float caret absent; cause unproven | Passed; exit 0; 175.83 s; `ctest-dbg-retry-05-result.json` |
| `nwb_refraction_gallery_capture_smoke` | Timeout at 900.03 s after earlier captures completed; exact cause unproven | Passed; exit 0; 754.46 s; `ctest-dbg-retry-03-result.json` |
| `nwb_reflection_optical_capture_smoke` | Failed at 247.11 s: inner 60 s capture deadline after first frame at 59.475 s; exact scheduling/startup cause unproven | Passed; exit 0; 588.48 s; `ctest-dbg-retry-04-result.json` |

A timing-only diagnostic wrapper subsequently completed all 82 alternate-atlas native/GPU gates with the same binary, inputs and assertions. It observed an initial blink-off capture at the failed clamp stage, followed by the existing phase synchronizer and a successful capture. Sequential live-pixel reads can span blink transitions, but the successful trace does not establish the cause of the two earlier failed attempts. No speculative harness or production edit was made. A separate unchanged canonical retry05 supplies the final suite pass above; the timing-instrumented run is additional diagnosis, not a substitute for that canonical result.

The four final successful retry commands are retained below. Each selection file contains exactly its one named suite:

```text
ctest --preset windows-clang-arm64-dbg --tests-from-file __artifacts/layout/ctest-dbg-retry-01-suites.txt --output-on-failure --parallel 1 --output-junit __artifacts/layout/ctest-dbg-retry-01.xml
ctest --preset windows-clang-arm64-dbg --tests-from-file __artifacts/layout/ctest-dbg-retry-05-suites.txt --output-on-failure --parallel 1 --output-junit __artifacts/layout/ctest-dbg-retry-05.xml
ctest --preset windows-clang-arm64-dbg --tests-from-file __artifacts/layout/ctest-dbg-retry-03-suites.txt --output-on-failure --parallel 1 --output-junit __artifacts/layout/ctest-dbg-retry-03.xml
ctest --preset windows-clang-arm64-dbg --tests-from-file __artifacts/layout/ctest-dbg-retry-04-suites.txt --output-on-failure --parallel 1 --output-junit __artifacts/layout/ctest-dbg-retry-04.xml
```

The full-log audit read 601 fresh application/report files and all retained CTest/JUnit/LastTest evidence, classifying 79,413 raw diagnostic-word matches with **zero unexplained matches**. Actual current timestamped engine records contain ERROR 0, WARNING 2, EXCEPTION 0, ASSERT 0 and FATAL 0; unexpected severity records are zero. The two warnings are the exact required `terminal65` capacity warnings, one each in the hardware/software open-shadow tests, verified against the full-message/count contract in `tests/smoke/csg_shadow_open_smoke.py:42,57–78`. Expected negative diagnostics, metric keys and test identifiers are classified separately. Historical failed attempts and overwritten log/report snapshots remain preserved.

Final evidence collection completed with exit 0 using `python __artifacts/layout/analyze_runtime_logs.py --label final --include-history --final`. It independently verifies the exact 179-name union, latest outcomes, complete source reads, source-required diagnostics and skip reasons. The interrupted outer invocation remains recorded as interrupted after successful retries.

Four inner native cases were explicitly skipped in the retained completed suites; they are limitations, not passed cases:

| Suite / inner case | Recorded reason |
| --- | --- |
| `nwb_math_tests` / `Math.StorageEqualityDoesNotReadBeyondUnalignedShortValues` | Protected-page boundaries are verified on Linux |
| `nwb_math_tests` / `Math.StorageEqualityPreservesSubnormalNaNAndSkippedLaneMxcsrFlags` | Raw MXCSR equality flags are specific to x86 SSE targets |
| `nwb_assets_graphics_tests` / `MetadataDiscoveryAdmission.CaseDistinctNativeFilesBothReachMetadataAdmission` | The current Windows host identity folds physical path case |
| `nwb_assets_graphics_tests` / `AssetsGraphics.ShaderDependencyCollectionRetainsCaseDistinctPhysicalInputs` | Windows shader path duplicate policy is case insensitive |

The completed font-builder Python integration suite also explicitly skipped `FontBuilderCli.test_dangling_output_and_work_symlinks_are_preserved_without_publication`. Its Linux-only dangling-symlink admission contract is unavailable on this Windows host (`tests/integration/font_builder/font_builder_cli_tests.py:352–353`). The full `..s.......`, ten-case completion and sole source skip decorator reconcile this one Python inner skip; it is not counted as an executed passing case.

Linux/Wayland/X11/POSIX-only paths, inactive SIMD branches and disabled optional Aftermath paths retain explicit source/contract dispositions where native environment evidence is unavailable. The optional gather benchmark remains parse-only and unconfigured. No provider/service provisioning or cross-platform runtime result is implied. These unavailable paths are qualification limits of this host.

## Evidence location and publication

Raw logs, layout records, snapshots and per-target lists remain under ignored `__artifacts/layout/`. They are local qualification evidence, not runtime dependencies. The named repository report carries the substantive comparison; README/wiki pages receive no run history or progress tables.

| Evidence | Scope |
| --- | --- |
| [final-changed-targets.json](../../__artifacts/layout/final-changed-targets.json) | All 92 IDs, before/current declaration and field lists, measured pairs, every after profile, exact conditional pairs, constraints and all 121 file attributions |
| [final-changed-targets.tsv](../../__artifacts/layout/final-changed-targets.tsv) | Complete readable per-definition measurement index |
| [final-changed-targets-summary.json](../../__artifacts/layout/final-changed-targets-summary.json) | 92 definitions; owner counts27/40/25; zero declaration/access changes and unclassified files |
| [final-scope-coverage.json](../../__artifacts/layout/final-scope-coverage.json) | Every first-party file, handoff/enhanced declaration, shader entry, manual miss, symbolic template, union/bitfield and vendor disposition |
| [final-scope-coverage-validation.json](../../__artifacts/layout/final-scope-coverage-validation.json) | 10,268 validated evidence-reference occurrences; zero reference errors, unclassified entries or sealed source mismatches |
| [core-audit.json](../../__artifacts/layout/core-audit.json) | Complete core/global/logger source and template reviews and full document instruction ledger |
| [impl-audit.json](../../__artifacts/layout/impl-audit.json) | Complete implementation target and symbolic-template dispositions |
| [tests-audit.inventory-dispositions.json](../../__artifacts/layout/tests-audit.inventory-dispositions.json) | Every test/CoolStuff inventory row including forward and conditional declarations |
| [tests-audit.summary.json](../../__artifacts/layout/tests-audit.summary.json) | All test changes, constraints and supporting measurements |
| [shader-audit.json](../../__artifacts/layout/shader-audit.json) | All187 authored shader/binding declaration dispositions |
| [final-composite-summary.json](../../__artifacts/layout/final-composite-summary.json) | Complete6165profile/34702group/8710key reconciliation, zero safe candidates/current errors/stale inputs/gaps |
| [final-composite-profiles.json](../../__artifacts/layout/final-composite-profiles.json) | Exact selected command-profile provenance and supersession |
| [final-layouts-portable.json.gz](../../__artifacts/layout/final-layouts-portable.json.gz) | Lossless compact delivery of all34,702 layout groups and6,165 profiles; repeated profile metadata stored once |
| [final-layouts-portable-validation.json](../../__artifacts/layout/final-layouts-portable-validation.json) | Every reconstructed record/profile matches original evidence; zero missing or changed groups |
| [final-composite-records.tsv](../../__artifacts/layout/final-composite-records.tsv) | Complete expanded record/group index |
| [final-composite-constrained.json](../../__artifacts/layout/final-composite-constrained.json) | Preserved CrashRequest native-protocol groups |
| [core-command-list-audit.json](../../__artifacts/layout/core-command-list-audit.json) | Explicit ARM64dbg/opt/fin before/after pairs |
| [transparent-multi-variants/summary.json](../../__artifacts/layout/transparent-multi-variants/summary.json) | All24 exact target/configuration/architecture comparisons |
| [review-findings.json](../../__artifacts/layout/review-findings.json) | Whole-diff ownership/initializer/ABI review and final source attribution |
| [review-input-event-mapping.json](../../__artifacts/layout/review-input-event-mapping.json) | All154 old positional-to-current-designator mappings |
| [review-initializer-sites.json](../../__artifacts/layout/review-initializer-sites.json) | All502 baseline initializer source groups |
| [build-dbg-final-result.json](../../__artifacts/layout/build-dbg-final-result.json) | Completed canonical native all-dbg build and full-log counts |
| [build-dbg-sealed-result.json](../../__artifacts/layout/build-dbg-sealed-result.json) | Sealed final incremental all-dbg build and full-log counts |
| [ctest-dbg-interrupted-result.json](../../__artifacts/layout/ctest-dbg-interrupted-result.json) | Authoritative113completed records,109passes/fourfailures,66remaining, interrupted outerexit null |
| [ctest-dbg-interrupted-LastTest.log](../../__artifacts/layout/ctest-dbg-interrupted-LastTest.log) | Preserved original complete per-suite blocks |
| [ctest-dbg-remaining.log](../../__artifacts/layout/ctest-dbg-remaining.log) | Completed exact66suite continuation; exit0,65passes/onecapabilityskip |
| [runtime-log-analysis/final-report.json](../../__artifacts/layout/runtime-log-analysis/final-report.json) | Final179suite authoritative reconciliation, complete-log classification, preserved historical failures, two expected warnings, one suite skip, four native inner skips and one Python inner skip |
| [review-optical-runtime-diagnosis.json](../../__artifacts/layout/review-optical-runtime-diagnosis.json) | Exact failed/later-case timelines and preserved capture/log evidence |
| [review-numeric-edit-runtime-diagnosis.json](../../__artifacts/layout/review-numeric-edit-runtime-diagnosis.json) | Exact caret failure, full log, immutable image hash and comparison evidence |
| [review-numeric-retry02-diagnosis.json](../../__artifacts/layout/review-numeric-retry02-diagnosis.json) | Preserved repeated caret failure, full model/geometry comparison and unchanged direct source paths |
| [review-numeric-diagnostic-timing-summary.json](../../__artifacts/layout/review-numeric-diagnostic-timing-summary.json) | Complete1,535-event timing audit, all82 final passing gates and explicit unproven failure causality |

The completion evidence records the final commit, `git pull --ff-only origin main`, `git push origin main` and matching remote/local main hashes. Publication uses the GitHub origin exclusively. This report is a substantive measurement record; raw outputs remain in their artifact locations and are not runtime dependencies.
