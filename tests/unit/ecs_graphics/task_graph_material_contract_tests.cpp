// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_material_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


// A material draw packet owns the exact mesh buffers, descriptor handles, and executable pipelines selected while
// the graph is declared. Recording and compute-plan materialization must never resolve a mutable registry key again.
TEST(EcsGraphics, MaterialDrawSnapshotsRetainExactGraphResourceGenerations){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString drawTypesSource;
    AString materialPassSource;
    AString materialSurfaceSource;
    AString resourceSetsSource;
    AString planSources;
    AString taskHeaderSources;
    AString taskRecordSources;
    AString rootGraphSources;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "renderer_draw_types.h", drawTypesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass.cpp", materialPassSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_surface.cpp", materialSurfaceSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "task_graph_resource_sets.h", resourceSetsSource));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "material/task_graph_compute_emulation_plan.h",
            "material/task_graph_opaque_compute_emulation_plan.h",
            "csg/task_graph_opaque_compute_emulation_plan.h",
            "avboit/task_graph_compute_emulation_plan.h",
        },
        planSources
    ));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "material/task_graph_opaque_compute_tasks.h",
            "csg/task_graph_opaque_compute_tasks.h",
            "avboit/task_graph_occupancy_tasks.h",
            "avboit/task_graph_extinction_integration_tasks.h",
            "avboit/task_graph_accumulation_tasks.h",
        },
        taskHeaderSources
    ));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "material/task_graph_opaque_compute_tasks.cpp",
            "csg/task_graph_opaque_compute_tasks.cpp",
            "avboit/task_graph_occupancy_tasks.cpp",
            "avboit/task_graph_extinction_integration_tasks.cpp",
            "avboit/task_graph_accumulation_tasks.cpp",
        },
        taskRecordSources
    ));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "renderer_frame_pipeline_graphics_prefix.cpp",
            "renderer_frame_pipeline_graph.cpp",
        },
        rootGraphSources
    ));
    const AStringView drawTypes(drawTypesSource.data(), drawTypesSource.size());
    const AStringView materialPass(materialPassSource.data(), materialPassSource.size());
    const AStringView materialSurface(materialSurfaceSource.data(), materialSurfaceSource.size());
    const AStringView resourceSets(resourceSetsSource.data(), resourceSetsSource.size());
    const AStringView plans(planSources.data(), planSources.size());
    const AStringView taskHeaders(taskHeaderSources.data(), taskHeaderSources.size());
    const AStringView taskRecords(taskRecordSources.data(), taskRecordSources.size());
    const AStringView rootGraphs(rootGraphSources.data(), rootGraphSources.size());

    EXPECT_TRUE(ContainsText(drawTypes, "#include <impl/assets/graphics/mesh/binding_slots.h>"));
    EXPECT_EQ(CountText(drawTypes, "NWB_MESH_BINDING_"), 11u);
    EXPECT_FALSE(ContainsText(drawTypes, "NWB_MESH_BINDING_MATERIAL_TYPED"));
    EXPECT_TRUE(ContainsText(drawTypes, "Name meshKey = NAME_NONE;"));
    EXPECT_TRUE(ContainsText(drawTypes, "MaterialPipelineKey pipelineKey;"));
    EXPECT_TRUE(ContainsText(materialSurface, "m_materialState.m_surfaceInfos.find(drawItem.pipelineKey.material)"));
    EXPECT_TRUE(ContainsText(materialPass, "csgReceiverSurfaceDrawItem.pipelineResources ="));

    EXPECT_TRUE(ContainsText(resourceSets, "const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;"));
    EXPECT_TRUE(ContainsText(resourceSets, "ForEachMaterialPassMeshSourceBuffer("));
    EXPECT_FALSE(ContainsText(resourceSets, "RendererMeshSystem& meshSystem"));
    EXPECT_FALSE(ContainsText(resourceSets, "findMeshResources("));

    EXPECT_EQ(CountText(plans, "const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;"), 5u);
    EXPECT_EQ(CountText(plans, "outputBuffers.push_back(mesh.emulationVertexBuffer);"), 4u);
    EXPECT_EQ(CountText(plans, "outputHeapSlots.push_back(mesh.emulationVertexHeapHandle.slot());"), 2u);
    EXPECT_TRUE(ContainsText(plans, "outputBuffer = mesh.emulationVertexBuffer;"));
    EXPECT_TRUE(ContainsText(plans, "outputHeapSlot = mesh.emulationVertexHeapHandle.slot();"));
    EXPECT_FALSE(ContainsText(plans, "RendererMeshSystem"));
    EXPECT_FALSE(ContainsText(plans, "findMeshResources("));

    EXPECT_FALSE(ContainsText(taskHeaders, "RendererMeshSystem* meshSystem"));
    EXPECT_FALSE(ContainsText(taskRecords, "payload.meshSystem"));
    EXPECT_FALSE(ContainsText(taskRecords, "matches(meshSystem"));
    EXPECT_TRUE(ContainsText(taskRecords, "payload.csgPlan.matches()"));
    EXPECT_TRUE(ContainsText(taskRecords, "payload.plan.matches(payload.drawIndex)"));

    constexpr AStringView s_RemovedMeshPayloadAssignments[] = {
        "opaqueComputeEmulationPayload.meshSystem",
        "opaqueCsgReceiverComputeEmulationPayload.meshSystem",
        "opaqueCsgIntervalSampleComputeEmulationPayload.meshSystem",
        "avboitOccupancyComputeEmulationPayload.meshSystem",
        "avboitExtinctionComputeEmulationPayload.meshSystem",
        "avboitAccumulationComputeEmulationPayload.meshSystem",
    };
    for(const AStringView assignment : s_RemovedMeshPayloadAssignments)
        EXPECT_FALSE(ContainsText(rootGraphs, assignment));
    EXPECT_FALSE(ContainsText(rootGraphs, "payload.meshSystem = &m_meshSystem;"));
}


// Scene-light and shading constants are prepared before graph declaration and published from accepted graph work.
// The previous direct compatibility writer had no callers, so keep it retired instead of letting an unreachable
// native state/submit bridge silently return to the deferred subsystem.
TEST(EcsGraphics, DeferredSceneShadingUploadsHaveNoNativeCompatibilityDispatcher){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString deferredHeaderSource;
    AString deferredLightingSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_system.h", deferredHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_lighting.cpp", deferredLightingSource));
    const AStringView deferredHeader(deferredHeaderSource.data(), deferredHeaderSource.size());
    const AStringView deferredLighting(deferredLightingSource.data(), deferredLightingSource.size());

    EXPECT_TRUE(ContainsText(deferredHeader, "prepareSceneShadingBufferUploads"));
    EXPECT_TRUE(ContainsText(deferredHeader, "confirmSceneShadingBufferUploads"));
    EXPECT_FALSE(ContainsText(deferredHeader, "updateSceneShadingBuffer"));
    EXPECT_FALSE(ContainsText(deferredLighting, "updateSceneShadingBuffer"));
    EXPECT_FALSE(ContainsText(deferredLighting, "commandList.writeBuffer("));
}


// AVBOIT's normal path rejects an uncaptured transparent phase before recording. Its last aggregate native
// dispatcher therefore had no caller and only kept mutable mesh/material/CSG writes reachable in dead code.
// Keep that bridge retired so every supported transparent phase starts from its declaration-time graph snapshot.
TEST(EcsGraphics, AvboitMaterialUploadsHaveNoNativeCompatibilityDispatcher){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString avboitHeaderSource;
    AString avboitSource;
    AString materialHeaderSource;
    AString materialPassSource;
    AString materialResourcesSource;
    AString meshHeaderSource;
    AString meshViewSource;
    AString csgHeaderSource;
    AString csgResourcesSource;
    AString csgIntervalSource;
    AString avboitOccupancyTasksSource;
    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_system.h", avboitHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_pass.cpp", avboitSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_system.h", materialHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass.cpp", materialPassSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass_resources.cpp", materialResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_system.h", meshHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_view.cpp", meshViewSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_system.h", csgHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_resources.cpp", csgResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_interval_peel.cpp", csgIntervalSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "task_graph_occupancy_tasks.cpp", avboitOccupancyTasksSource));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "shared/task_graph_draw_snapshots.h",
            "avboit/task_graph_occupancy_tasks.h",
            "avboit/task_graph_occupancy_tasks.cpp",
            "avboit/task_graph_extinction_integration_tasks.h",
            "avboit/task_graph_extinction_integration_tasks.cpp",
            "avboit/task_graph_accumulation_tasks.h",
            "avboit/task_graph_accumulation_tasks.cpp",
            "renderer_frame_pipeline_graph.cpp",
        },
        taskGraphSource
    ));

    const AStringView avboitHeader(avboitHeaderSource.data(), avboitHeaderSource.size());
    const AStringView avboit(avboitSource.data(), avboitSource.size());
    const AStringView materialHeader(materialHeaderSource.data(), materialHeaderSource.size());
    const AStringView materialPass(materialPassSource.data(), materialPassSource.size());
    const AStringView materialResources(materialResourcesSource.data(), materialResourcesSource.size());
    const AStringView meshHeader(meshHeaderSource.data(), meshHeaderSource.size());
    const AStringView meshView(meshViewSource.data(), meshViewSource.size());
    const AStringView csgHeader(csgHeaderSource.data(), csgHeaderSource.size());
    const AStringView csgResources(csgResourcesSource.data(), csgResourcesSource.size());
    const AStringView csgInterval(csgIntervalSource.data(), csgIntervalSource.size());
    const AStringView avboitOccupancyTasks(avboitOccupancyTasksSource.data(), avboitOccupancyTasksSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    EXPECT_FALSE(ContainsText(avboitHeader, "renderAvboitPasses"));
    EXPECT_FALSE(ContainsText(avboitHeader, "renderAvboitPreDepthWarpPasses"));
    EXPECT_FALSE(ContainsText(avboitHeader, "renderAvboitPostOccupancyPasses"));
    EXPECT_FALSE(ContainsText(avboitHeader, "buildTransparentCsgIntervals"));
    EXPECT_FALSE(ContainsText(avboitHeader, "clearAvboitTargets"));
    EXPECT_FALSE(ContainsText(avboit, "renderAvboitPasses"));
    EXPECT_FALSE(ContainsText(avboit, "renderAvboitPreDepthWarpPasses"));
    EXPECT_FALSE(ContainsText(avboit, "renderAvboitPostOccupancyPasses"));
    EXPECT_FALSE(ContainsText(avboit, "buildTransparentCsgIntervals"));
    EXPECT_FALSE(ContainsText(avboit, "clearAvboitTargets"));
    EXPECT_FALSE(ContainsText(avboit, "ClearAvboitTargetValues"));

    EXPECT_FALSE(ContainsText(materialHeader, "renderMaterialPass("));
    EXPECT_FALSE(ContainsText(materialHeader, "uploadMaterialPassDrawBuffers"));
    EXPECT_FALSE(ContainsText(materialPass, "renderMaterialPass("));
    EXPECT_FALSE(ContainsText(materialResources, "commandList.writeBuffer("));
    EXPECT_FALSE(ContainsText(meshHeader, "updateMeshViewBuffer"));
    EXPECT_FALSE(ContainsText(meshView, "commandList.writeBuffer("));
    EXPECT_FALSE(ContainsText(csgHeader, "uploadCsgFrameBuffers"));
    EXPECT_FALSE(ContainsText(csgHeader, "uploadCsgIntervalSampleState"));
    EXPECT_FALSE(ContainsText(csgResources, "commandList.writeBuffer("));
    EXPECT_FALSE(ContainsText(csgInterval, "commandList.writeBuffer("));

    EXPECT_TRUE(ContainsText(taskGraph, "TransparentMaterialPassGraphSnapshot"));
    EXPECT_TRUE(ContainsText(taskGraph, "if(payload.hasTransparentRenderers && (!payload.occupancyPhasePrepared || !payload.occupancySnapshot.captured))"));
    EXPECT_TRUE(ContainsText(taskGraph, "if(payload.hasTransparentRenderers && (!payload.extinctionPhasePrepared || !payload.extinctionSnapshot.captured))"));
    EXPECT_TRUE(ContainsText(taskGraph, "if(payload.hasTransparentRenderers && (!payload.accumulationPhasePrepared || !payload.accumulationSnapshot.captured))"));
    EXPECT_TRUE(ContainsText(taskGraph, "addUploadBufferTask("));

    EXPECT_TRUE(ContainsText(avboitOccupancyTasks, "if(payload.transparentCsgStreamsUploaded != payload.transparentCsgSnapshot.captured)"));
    EXPECT_FALSE(ContainsText(avboitOccupancyTasks, "CsgFrameState"));

    const usize transparentCsgCaptureOffset = taskGraph.find("avboitPrePayload.transparentCsgSnapshot.capture(");
    const usize transparentCsgSpanCaptureOffset = taskGraph.find("avboitCsgReceiverSpanPayload.transparentCsgSnapshot.capture(");
    const usize transparentCsgCombineCaptureOffset = taskGraph.find("avboitCsgIntervalCombinePayload.transparentCsgSnapshot.capture(");
    const usize transparentCsgUploadedOffset = taskGraph.find("avboitPrePayload.transparentCsgStreamsUploaded = true");
    ASSERT_NE(transparentCsgCaptureOffset, AStringView::npos);
    ASSERT_NE(transparentCsgSpanCaptureOffset, AStringView::npos);
    ASSERT_NE(transparentCsgCombineCaptureOffset, AStringView::npos);
    ASSERT_NE(transparentCsgUploadedOffset, AStringView::npos);
    EXPECT_LT(transparentCsgCaptureOffset, transparentCsgUploadedOffset);
    EXPECT_LT(transparentCsgSpanCaptureOffset, transparentCsgUploadedOffset);
    EXPECT_LT(transparentCsgCombineCaptureOffset, transparentCsgUploadedOffset);
}


// Shadow Prepare consumes the immutable material-context selector uploaded before graph recording. Do not retain
// a native writer that can re-read descriptor slots after that declaration boundary.
TEST(EcsGraphics, RayTraceMaterialContextSelectorHasNoNativeCompatibilityDispatcher){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString rayTracingHeaderSource;
    AString shadowSource;
    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_shadow.cpp", shadowSource));
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "renderer_frame_pipeline_graph_shadow_prepare.cpp" }, taskGraphSource));
    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView shadow(shadowSource.data(), shadowSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    EXPECT_TRUE(ContainsText(rayTracingHeader, "snapshotRayTraceMaterialContextSlots"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "uploadRayTraceMaterialContextSlots"));
    EXPECT_FALSE(ContainsText(shadow, "uploadRayTraceMaterialContextSlots"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.raytrace.material_context_slots_upload"));
    EXPECT_TRUE(ContainsText(taskGraph, "m_rayTraceMaterialContextSlotsUploadTask"));
    EXPECT_FALSE(ContainsText(taskGraph, "rayTraceMaterialContextSlotsGraphOwned"));
}


// A frozen caustic-emission stream is either represented by a graph upload blob or absent. Recording must not
// re-upload it natively after the packet has declared the target buffer.
TEST(EcsGraphics, CausticEmissionTargetsHaveNoNativeCompatibilityDispatcher){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString rayTracingHeaderSource;
    AString causticsSource;
    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_caustics.cpp", causticsSource));
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "renderer_frame_pipeline_graph_shadow_prepare.cpp" }, taskGraphSource));
    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView caustics(causticsSource.data(), causticsSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    EXPECT_TRUE(ContainsText(rayTracingHeader, "retainPreparedCausticEmissionTargetUpload"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "recordPreparedCausticEmissionTargets"));
    EXPECT_FALSE(ContainsText(caustics, "recordPreparedCausticEmissionTargets"));
    EXPECT_FALSE(ContainsText(caustics, "commandList.writeBuffer("));
    EXPECT_TRUE(ContainsText(taskGraph, "render.raytrace.caustic_emission_targets_upload"));
    EXPECT_TRUE(ContainsText(taskGraph, "m_causticEmissionTargetsUploadTask"));
    EXPECT_FALSE(ContainsText(taskGraph, "causticEmissionTargetsGraphOwned"));
}


// Surfel frame constants are frozen while the graph is declared. The Shadow Prepare and optional hybrid-tail
// callbacks must consume that upload rather than recomputing and writing a later mutable constant buffer.
TEST(EcsGraphics, SurfelFrameConstantsHaveNoNativeCompatibilityDispatcher){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString rayTracingHeaderSource;
    AString surfelSource;
    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_surfel_gi.cpp", surfelSource));
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "renderer_frame_pipeline_graph_shadow_prepare.cpp" }, taskGraphSource));
    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView surfel(surfelSource.data(), surfelSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    EXPECT_TRUE(ContainsText(rayTracingHeader, "retainPreparedSurfelFrameConstantsUpload"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "recordPreparedSurfelFrameConstants"));
    EXPECT_FALSE(ContainsText(surfel, "recordPreparedSurfelFrameConstants"));
    EXPECT_FALSE(ContainsText(surfel, "commandList.writeBuffer("));
    EXPECT_TRUE(ContainsText(taskGraph, "render.surfel_gi.constants_upload"));
    EXPECT_TRUE(ContainsText(taskGraph, "m_surfelFrameConstantsUploadTask"));
    EXPECT_FALSE(ContainsText(taskGraph, "surfelFrameConstantsGraphOwned"));
}


// The current deferred bindless selector has a mandatory graph upload before any Shadow/Lighting/Composite/Present
// consumer. Those render callbacks must not retain a native writer for a target-generation selector.
TEST(EcsGraphics, DeferredBindlessSelectorHasNoNativeCompatibilityDispatcher){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString deferredHeaderSource;
    AString deferredTargetsSource;
    AString deferredLightingSource;
    AString deferredCompositeSource;
    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_system.h", deferredHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_targets.cpp", deferredTargetsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_lighting.cpp", deferredLightingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_composite.cpp", deferredCompositeSource));
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "renderer_frame_pipeline_graph_shadow_prepare.cpp" }, taskGraphSource));
    const AStringView deferredHeader(deferredHeaderSource.data(), deferredHeaderSource.size());
    const AStringView deferredTargets(deferredTargetsSource.data(), deferredTargetsSource.size());
    const AStringView deferredLighting(deferredLightingSource.data(), deferredLightingSource.size());
    const AStringView deferredComposite(deferredCompositeSource.data(), deferredCompositeSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    EXPECT_FALSE(ContainsText(deferredHeader, "uploadDeferredBindlessFrameResources"));
    EXPECT_FALSE(ContainsText(deferredTargets, "uploadDeferredBindlessFrameResources"));
    EXPECT_FALSE(ContainsText(deferredTargets, "bindless.slotsBuffer.get(), &bindless.slots"));
    EXPECT_FALSE(ContainsText(deferredLighting, "currentBindlessSlotsGraphOwned"));
    EXPECT_FALSE(ContainsText(deferredComposite, "currentBindlessSlotsGraphOwned"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.deferred.bindless_slots_upload"));
    EXPECT_TRUE(ContainsText(taskGraph, "m_deferredBindlessSlotsUploadTask"));
}


// Frozen graph batches and the explicit hybrid restore own every supported shadow material-context upload. Keep
// mutable compatibility writers and the no-data hybrid restore overload out of the ray-tracing subsystem.
TEST(EcsGraphics, ShadowMaterialContextHasNoDeadNativeBulkUploader){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString rayTracingHeaderSource;
    AString shadowSource;
    AString swBvhSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_shadow.cpp", shadowSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh.cpp", swBvhSource));
    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView shadow(shadowSource.data(), shadowSource.size());
    const AStringView swBvh(swBvhSource.data(), swBvhSource.size());

    EXPECT_FALSE(ContainsText(rayTracingHeader, "uploadShadowMaterialContextBuffers"));
    EXPECT_FALSE(ContainsText(shadow, "uploadShadowMaterialContextBuffers"));
    EXPECT_EQ(CountText(rayTracingHeader, "recordPreparedHybridHardwareMaterialContextFallback("), 1u);
    EXPECT_EQ(CountText(swBvh, "recordPreparedHybridHardwareMaterialContextFallback("), 1u);
    EXPECT_FALSE(ContainsText(swBvh, "UploadPreparedShadowMaterialContextBuffers"));
}


// Renderer-owned hybrid material/scene bytes must enter through immutable graph batches. Native AS build/state work
// remains separately documented, but it must not grow another live buffer writer in the scene gather recorder.
TEST(EcsGraphics, NativeRendererWritesRemainExplicitCompatibilityBoundaries){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString swBvhSource;
    AString shadowSource;
    AString shadowTaskGraphSource;
    AString adaptiveLifecycleSource;
    AString uiSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh.cpp", swBvhSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_shadow.cpp", shadowSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp", shadowTaskGraphSource));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "renderer_frame_pipeline_execute.cpp",
            "raytrace/raytracing_system.h",
            "raytrace/raytracing_system.cpp",
            "raytrace/rt_shadow.cpp",
            "raytrace/renderer_raytracing_state.h",
            "raytrace/renderer_raytracing_state.cpp",
        },
        adaptiveLifecycleSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    const AStringView swBvh(swBvhSource.data(), swBvhSource.size());
    const AStringView shadow(shadowSource.data(), shadowSource.size());
    const AStringView shadowTaskGraph(shadowTaskGraphSource.data(), shadowTaskGraphSource.size());
    const AStringView adaptiveLifecycle(adaptiveLifecycleSource.data(), adaptiveLifecycleSource.size());
    const AStringView ui(uiSource.data(), uiSource.size());

    EXPECT_EQ(CountText(swBvh, "writeBuffer("), 0u);
    EXPECT_TRUE(ContainsText(swBvh, "if(shadowMaterialContextBatchGraphOwned){"));
    EXPECT_TRUE(ContainsText(swBvh, "graph-owned HW shadow material context unexpectedly reused a native cache"));
    EXPECT_TRUE(ContainsText(swBvh, "graph-owned SW shadow material context unexpectedly reused a native cache"));
    EXPECT_TRUE(ContainsText(swBvh, "recordPreparedHybridHardwareMaterialContextFallback"));
    EXPECT_FALSE(ContainsText(adaptiveLifecycle, "forceHybridHardwareFallbackSnapshotStaleForTesting"));
    EXPECT_FALSE(ContainsText(adaptiveLifecycle, "hybrid hardware material-context fallback retried directly"));

    // Adaptive diagnostics are graph-owned on every prepared route. Frames without clear/copy work still freeze
    // an enabled lifecycle plan so only acceptance advances the tick; compatibility calls disable diagnostics.
    EXPECT_EQ(CountText(shadow, "clearBufferUInt("), 0u);
    EXPECT_EQ(CountText(shadow, "copyBuffer("), 0u);
    EXPECT_EQ(CountText(shadow, "m_swShadowCompactEnabled"), 0u);
    EXPECT_EQ(CountText(shadow, "m_swShadowEdgeStatsEnabled"), 0u);
    EXPECT_FALSE(ContainsText(shadow, "m_swShadowEdgeStatsTick++"));
    EXPECT_FALSE(ContainsText(shadow, "mapBuffer("));
    EXPECT_FALSE(ContainsText(shadow, "m_swShadowEdgeStatsPending"));
    EXPECT_FALSE(ContainsText(adaptiveLifecycle, "confirmShadowVisibilitySubmission"));
    EXPECT_FALSE(ContainsText(adaptiveLifecycle, "PendingSubmissionUnconfirmed"));
    EXPECT_TRUE(ContainsText(adaptiveLifecycle, "m_swShadowEdgeStatsTick = plan.statsTick + 1u;"));
    EXPECT_EQ(CountText(adaptiveLifecycle, "m_swShadowEdgeStatsPending = true;"), 1u);
    EXPECT_TRUE(ContainsText(adaptiveLifecycle, "m_swShadowEdgeStatsPendingSubmissionID = submissionToken.value;"));
    EXPECT_TRUE(ContainsText(adaptiveLifecycle, "submissionToken.physicalQueueIndex"));
    EXPECT_TRUE(ContainsText(adaptiveLifecycle, "m_raytracingSystem.retireCompletedAdaptiveShadowStatisticsReadback();"));
    EXPECT_FALSE(ContainsText(shadowTaskGraph, "appendOptionalWriteBuffer"));
    EXPECT_EQ(CountText(shadowTaskGraph, "addClearBufferTask("), 2u);
    EXPECT_EQ(CountText(shadowTaskGraph, "addCopyBufferTask("), 1u);
    EXPECT_TRUE(ContainsText(shadowTaskGraph, "!splitSoftTransparentFold && rayTracingPlan.adaptivePlan.enabled"));
    EXPECT_TRUE(ContainsText(shadowTaskGraph, "GraphOwnedAdaptiveShadowPlan graphOwnedAdaptivePlan = graphOwnedAdaptiveCandidate"));

    // UI presentation is fully graph-owned. Callback-free rejection retains the live frame, while an opaque callback
    // rejection stops the device generation instead of replaying arbitrary user code.
    EXPECT_EQ(CountText(ui, "executeCommandLists("), 0u);
    EXPECT_FALSE(ContainsText(ui, "ensureRenderCommandList"));
    EXPECT_TRUE(ContainsText(ui, "retaining callback-free frame for graph retry"));
    EXPECT_TRUE(ContainsText(ui, "after an opaque callback may have executed; requesting recreation"));
}


// The current renderer has exactly two runtime-selected sampled-image domains: material Texture2D assets (shared
// by raster and ray-trace surface dispatch) and ImGui textures.  A new domain must not silently rely on a global
// descriptor slot: keep the supported domain small and require each one to retain handles before graph declaration.
TEST(EcsGraphics, DynamicBindlessSampledImagesHaveFrozenGraphDeclarationOwners){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString materialAssetHeaderSource;
    AString materialSurfaceSource;
    AString taskGraphSource;
    AString uiHeaderSource;
    AString uiSource;
    AString uiTextureSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets_material" / "asset.h", materialAssetHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_surface.cpp", materialSurfaceSource));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "material/task_graph_resource_sets.h",
            "renderer_frame_pipeline_graphics_prefix.cpp",
            "csg/task_graph_transparent_interval_tasks.cpp",
            "avboit/task_graph_occupancy_tasks.cpp",
            "avboit/task_graph_extinction_integration_tasks.cpp",
            "avboit/task_graph_accumulation_tasks.cpp",
            "renderer_frame_pipeline_graph_shadow_prepare.cpp",
            "renderer_frame_pipeline_graph.cpp",
        },
        taskGraphSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.h", uiHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "texture_resources.cpp", uiTextureSource));

    const AStringView materialAssetHeader(materialAssetHeaderSource.data(), materialAssetHeaderSource.size());
    const AStringView materialSurface(materialSurfaceSource.data(), materialSurfaceSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const AStringView uiHeader(uiHeaderSource.data(), uiHeaderSource.size());
    const AStringView ui(uiSource.data(), uiSource.size());
    const AStringView uiTextures(uiTextureSource.data(), uiTextureSource.size());

    // Material resource validation supports only a Texture2D asset and a sampler. The prepared collector must
    // resolve the former to a retained texture handle, while samplers deliberately have no resource state to track.
    EXPECT_TRUE(ContainsText(materialAssetHeader, "SampledImage2D = 1"));
    EXPECT_TRUE(ContainsText(materialAssetHeader, "Sampler = 2"));
    EXPECT_TRUE(ContainsText(materialAssetHeader, "return resourceKind == MaterialResourceKind::SampledImage2D || resourceKind == MaterialResourceKind::Sampler"));
    EXPECT_TRUE(ContainsText(materialSurface, "appendPreparedMaterialSurfaceSampledTextures"));
    EXPECT_TRUE(ContainsText(materialSurface, "inOutTextures.push_back(textureResource.texture)"));
    EXPECT_TRUE(ContainsText(materialSurface, "default:\n            return false;"));

    // Raster and trace consumers share the frozen material collection. The named sets make a future dynamic
    // bindless consumer visible to the audit rather than allowing it to hide behind the descriptor heap.
    EXPECT_TRUE(ContainsText(taskGraph, "GatherPreparedMaterialSampledTextureResourceSet"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.graphics_prefix.gbuffer.material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.graphics_prefix.csg_interval_sample.material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.avboit.intervals.transparent_csg_material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.avboit.occupancy.material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.avboit.extinction.material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.avboit.accumulation.material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.trace_material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "traceMaterialSampledTextureSetUse"));

    // ImGui is the other dynamic domain. Its draw command retains the selected texture and heap slot, its upload
    // path imports the exact destination, and the terminal task declares that frozen texture rather than reading
    // the mutable ImGui command list.
    EXPECT_TRUE(ContainsText(uiHeader, "Core::TextureHandle texture;"));
    EXPECT_TRUE(ContainsText(uiHeader, "Core::GpuDescriptorHandle textureHeapHandle"));
    EXPECT_TRUE(ContainsText(uiTextures, "heap.allocate(Core::GpuDescriptorClass::SampledImage)"));
    EXPECT_TRUE(ContainsText(uiTextures, "importTaskGraphTexture(graph, *resource)"));
    EXPECT_TRUE(ContainsText(uiTextures, "graph.addUploadTextureTask("));
    EXPECT_TRUE(ContainsText(ui, "appendDrawTextureUse(drawCommand)"));
    EXPECT_TRUE(ContainsText(ui, "m_taskGraphDrawCommands.push_back(TaskGraphDrawCommand{"));
    EXPECT_TRUE(ContainsText(ui, "graph-owned ImGui overlay cannot safely record a custom draw callback"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

