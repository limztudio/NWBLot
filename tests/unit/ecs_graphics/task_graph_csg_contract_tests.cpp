// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_csg_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


// Every CSG work-region gather must use the same immutable mesh-view value scheduled for upload.  Falling back to
// the previously accepted CPU mirror during graph declaration can under-bound work after the camera moves.
TEST(EcsGraphics, CsgWorkRegionsUseTheFrozenMeshViewUploadPayload){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString graphSource;
    AString prefixSource;
    AString materialHeaderSource;
    AString materialPassSource;
    AString csgHeaderSource;
    AString csgResourcesSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp", graphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graphics_prefix.cpp", prefixSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_system.h", materialHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass.cpp", materialPassSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_system.h", csgHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_resources.cpp", csgResourcesSource));
    const AStringView graph(graphSource.data(), graphSource.size());
    const AStringView prefix(prefixSource.data(), prefixSource.size());
    const AStringView materialHeader(materialHeaderSource.data(), materialHeaderSource.size());
    const AStringView materialPass(materialPassSource.data(), materialPassSource.size());
    const AStringView csgHeader(csgHeaderSource.data(), csgHeaderSource.size());
    const AStringView csgResources(csgResourcesSource.data(), csgResourcesSource.size());

    EXPECT_EQ(CountText(graph, "m_meshSystem.prepareMeshViewBufferUpload("), 1u);
    EXPECT_EQ(CountText(prefix, "m_meshSystem.prepareMeshViewBufferUpload("), 0u);
    EXPECT_EQ(CountText(graph, "ECSRenderDetail::MeshViewGpuData meshViewState;"), 1u);
    EXPECT_FALSE(ContainsText(graph, "transparentCsgMeshViewState"));
    EXPECT_TRUE(ContainsText(prefix, ".viewState = meshViewState,"));

    const usize prepareOffset = graph.find("m_meshSystem.prepareMeshViewBufferUpload(");
    const usize prefixDeclarationOffset = graph.find("declareDeferredGraphicsPrefixTasks(", prepareOffset);
    ASSERT_NE(prepareOffset, AStringView::npos);
    ASSERT_NE(prefixDeclarationOffset, AStringView::npos);
    EXPECT_LT(prepareOffset, prefixDeclarationOffset);

    const auto expectFrozenPreparedGather = [](const AStringView source, const AStringView passMarker){
        SCOPED_TRACE(passMarker.data());
        EXPECT_EQ(CountText(source, passMarker), 1u);
        const usize passOffset = source.find(passMarker);
        ASSERT_NE(passOffset, AStringView::npos);
        const usize callEnd = source.find(");", passOffset);
        ASSERT_NE(callEnd, AStringView::npos);
        const AStringView callTail = source.substr(passOffset, callEnd + 2u - passOffset);
        EXPECT_EQ(CountText(callTail, "RendererResourceLookupMode::PreparedOnly"), 1u);
        EXPECT_EQ(CountText(callTail, "&meshViewState"), 1u);
        EXPECT_FALSE(ContainsText(callTail, "nullptr"));
    };
    expectFrozenPreparedGather(prefix, "MaterialPipelinePass::Opaque");
    expectFrozenPreparedGather(graph, "MaterialPipelinePass::CsgReceiverSurface");
    expectFrozenPreparedGather(graph, "MaterialPipelinePass::AvboitOccupancy");
    expectFrozenPreparedGather(graph, "MaterialPipelinePass::AvboitExtinction");
    expectFrozenPreparedGather(graph, "MaterialPipelinePass::AvboitAccumulate");

    const usize compatibilityBegin = materialPass.find("bool RendererMaterialSystem::prepareMaterialPassResources(");
    ASSERT_NE(compatibilityBegin, AStringView::npos);
    const usize compatibilityEnd = materialPass.find("\nvoid RendererMaterialSystem::renderPreparedMaterialPass(", compatibilityBegin);
    ASSERT_NE(compatibilityEnd, AStringView::npos);
    const AStringView compatibility = materialPass.substr(compatibilityBegin, compatibilityEnd - compatibilityBegin);
    EXPECT_EQ(CountText(compatibility, "gatherMaterialPassDrawItems("), 1u);
    EXPECT_TRUE(ContainsText(compatibility, "RendererResourceLookupMode::CreateMissing,\n        nullptr"));

    constexpr AStringView viewPointerParameter = "const ECSRenderDetail::MeshViewGpuData* csgWorkRegionMeshViewState";
    EXPECT_EQ(CountText(materialHeader, viewPointerParameter), 1u);
    EXPECT_EQ(CountText(csgHeader, viewPointerParameter), 1u);
    EXPECT_FALSE(ContainsText(materialHeader, "csgWorkRegionMeshViewState = nullptr"));
    EXPECT_FALSE(ContainsText(csgHeader, "csgWorkRegionMeshViewState = nullptr"));
    EXPECT_TRUE(ContainsText(csgResources, "m_meshSystem.snapshotAcceptedMeshViewWorldToClip(acceptedWorldToClip)"));
}


// CSG buffers, descriptors, and capacities are frozen once at the root graph boundary. Every task that can consume
// them owns the retained tuple by value, so deferred recording cannot observe a later CSG resource generation.
TEST(EcsGraphics, CsgGraphResourcesAreFrozenOnceAndOwnedByEveryRecordPayload){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskPayloadHeadersSource;
    AString taskRecordSources;
    AString materialRecordSources;
    AString boundarySources;
    AString rootGraphSource;
    AString rootPrefixSource;
    AString csgResourcesSource;
    AString csgIntervalSource;
    AString materialResourcesSource;
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "deferred/task_graph_gbuffer_task.h",
            "csg/task_graph_opaque_compute_tasks.h",
            "csg/task_graph_opaque_interval_tasks.h",
            "csg/task_graph_transparent_interval_tasks.h",
            "avboit/task_graph_occupancy_tasks.h",
            "avboit/task_graph_extinction_integration_tasks.h",
            "avboit/task_graph_accumulation_tasks.h",
        },
        taskPayloadHeadersSource
    ));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "deferred/task_graph_gbuffer_task.cpp",
            "csg/task_graph_opaque_compute_tasks.cpp",
            "csg/task_graph_opaque_interval_tasks.cpp",
            "csg/task_graph_transparent_interval_tasks.cpp",
            "avboit/task_graph_occupancy_tasks.cpp",
            "avboit/task_graph_extinction_integration_tasks.cpp",
            "avboit/task_graph_accumulation_tasks.cpp",
        },
        taskRecordSources
    ));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "material/material_pass.cpp",
            "material/material_pass_draw.cpp",
            "material/material_pass_resources.cpp",
            "avboit/avboit_pass.cpp",
        },
        materialRecordSources
    ));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "csg/csg_graph_resource_snapshot.h",
            "csg/csg_system.h",
            "csg/csg_resources.cpp",
            "csg/csg_interval_peel.cpp",
            "material/material_system.h",
            "material/material_pass.cpp",
            "material/material_pass_draw.cpp",
            "material/material_pass_resources.cpp",
            "avboit/avboit_system.h",
            "avboit/avboit_pass.cpp",
            "renderer_frame_pipeline.h",
            "renderer_frame_pipeline_graphics_prefix.cpp",
            "renderer_frame_pipeline_graph.cpp",
        },
        boundarySources
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp", rootGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graphics_prefix.cpp", rootPrefixSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_resources.cpp", csgResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_interval_peel.cpp", csgIntervalSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass_resources.cpp", materialResourcesSource));
    const AStringView taskPayloadHeaders(taskPayloadHeadersSource.data(), taskPayloadHeadersSource.size());
    const AStringView taskRecords(taskRecordSources.data(), taskRecordSources.size());
    const AStringView materialRecords(materialRecordSources.data(), materialRecordSources.size());
    const AStringView boundaries(boundarySources.data(), boundarySources.size());
    const AStringView rootGraph(rootGraphSource.data(), rootGraphSource.size());
    const AStringView rootPrefix(rootPrefixSource.data(), rootPrefixSource.size());
    const AStringView csgResources(csgResourcesSource.data(), csgResourcesSource.size());
    const AStringView csgInterval(csgIntervalSource.data(), csgIntervalSource.size());
    const AStringView materialResources(materialResourcesSource.data(), materialResourcesSource.size());

    const auto expectOwnedSnapshotPayload = [](
        const AStringView source,
        const AStringView taskMarker,
        const AStringView endMarker,
        const bool csgSystemDependencyForbidden
    ){
        SCOPED_TRACE(taskMarker.data());
        const usize taskBegin = source.find(taskMarker);
        ASSERT_NE(taskBegin, AStringView::npos);
        const usize taskEnd = source.find(endMarker, taskBegin + taskMarker.size());
        ASSERT_NE(taskEnd, AStringView::npos);
        const usize payloadBegin = source.find("    struct Payload{", taskBegin);
        ASSERT_NE(payloadBegin, AStringView::npos);
        ASSERT_LT(payloadBegin, taskEnd);
        const usize payloadEnd = source.find("\n    };", payloadBegin);
        ASSERT_NE(payloadEnd, AStringView::npos);
        ASSERT_LT(payloadEnd, taskEnd);
        const AStringView payloadDeclaration = source.substr(payloadBegin, payloadEnd - payloadBegin);
        EXPECT_EQ(CountText(payloadDeclaration, "CsgGraphResourceSnapshot csgResources;"), 1u);
        EXPECT_FALSE(ContainsText(payloadDeclaration, "CsgGraphResourceSnapshot*"));
        EXPECT_FALSE(ContainsText(payloadDeclaration, "CsgGraphResourceSnapshot&"));
        if(csgSystemDependencyForbidden)
            EXPECT_EQ(CountText(payloadDeclaration, "RendererCsgSystem"), 0u);
    };
    expectOwnedSnapshotPayload(
        taskPayloadHeaders,
        "struct GbufferGraphTask{",
        "struct OpaqueCsgReceiverComputeEmulationGraphTask{",
        false
    );
    expectOwnedSnapshotPayload(
        taskPayloadHeaders,
        "struct OpaqueCsgReceiverComputeEmulationGraphTask{",
        "struct OpaqueCsgIntervalSampleComputeEmulationGraphTask{",
        true
    );
    expectOwnedSnapshotPayload(
        taskPayloadHeaders,
        "struct OpaqueCsgIntervalSampleComputeEmulationGraphTask{",
        "struct CsgReceiverSpanBuildGraphTask{",
        true
    );
    expectOwnedSnapshotPayload(
        taskPayloadHeaders,
        "struct CsgReceiverSpanBuildGraphTask{",
        "struct CsgIntervalCombineGraphTask{",
        false
    );
    expectOwnedSnapshotPayload(
        taskPayloadHeaders,
        "struct CsgIntervalCombineGraphTask{",
        "struct CsgIntervalSampleGraphTask{",
        false
    );
    expectOwnedSnapshotPayload(
        taskPayloadHeaders,
        "struct CsgIntervalSampleGraphTask{",
        "struct AvboitCsgReceiverSpanGraphTask{",
        false
    );
    expectOwnedSnapshotPayload(
        taskPayloadHeaders,
        "struct AvboitCsgReceiverSpanGraphTask{",
        "struct AvboitCsgIntervalCombineGraphTask{",
        false
    );
    expectOwnedSnapshotPayload(
        taskPayloadHeaders,
        "struct AvboitCsgIntervalCombineGraphTask{",
        "struct AvboitPreGraphTask{",
        false
    );
    expectOwnedSnapshotPayload(
        taskPayloadHeaders,
        "struct AvboitPreGraphTask{",
        "struct AvboitOccupancyComputeEmulationGraphTask{",
        false
    );
    expectOwnedSnapshotPayload(
        taskPayloadHeaders,
        "struct AvboitOccupancyComputeEmulationGraphTask{",
        "struct AvboitOccupancySharedComputeEmulationGraphTask{",
        true
    );
    expectOwnedSnapshotPayload(
        taskPayloadHeaders,
        "struct AvboitOccupancyGraphTask{",
        "struct AvboitDepthWarpGraphTask{",
        false
    );
    expectOwnedSnapshotPayload(
        taskPayloadHeaders,
        "struct AvboitExtinctionComputeEmulationGraphTask{",
        "struct AvboitExtinctionSharedComputeEmulationGraphTask{",
        true
    );
    expectOwnedSnapshotPayload(
        taskPayloadHeaders,
        "struct AvboitExtinctionGraphTask{",
        "struct AvboitIntegrationGraphTask{",
        false
    );
    expectOwnedSnapshotPayload(
        taskPayloadHeaders,
        "struct AvboitAccumulationComputeEmulationGraphTask{",
        "struct AvboitAccumulationSharedComputeEmulationGraphTask{",
        true
    );
    expectOwnedSnapshotPayload(
        taskPayloadHeaders,
        "struct AvboitAccumulationGraphTask{",
        "struct AvboitAccumulationFinalizeGraphTask{",
        false
    );
    EXPECT_EQ(CountText(taskPayloadHeaders, "CsgGraphResourceSnapshot csgResources;"), 15u);

    const auto expectRecordUsesOwnedSnapshot = [](
        const AStringView source,
        const AStringView recordMarker,
        const AStringView endMarker
    ){
        SCOPED_TRACE(recordMarker.data());
        const usize recordBegin = source.find(recordMarker);
        ASSERT_NE(recordBegin, AStringView::npos);
        const usize recordEnd = source.find(endMarker, recordBegin);
        ASSERT_NE(recordEnd, AStringView::npos);
        const AStringView record = source.substr(recordBegin, recordEnd - recordBegin);
        EXPECT_GE(CountText(record, "payload.csgResources"), 1u);
        EXPECT_FALSE(ContainsText(record, "csgGraphResourceSnapshot("));
    };
    expectRecordUsesOwnedSnapshot(taskRecords, "bool GbufferGraphTask::record(", "NWB_IMPL_END");
    expectRecordUsesOwnedSnapshot(
        taskRecords,
        "bool OpaqueCsgReceiverComputeEmulationGraphTask::record(",
        "bool OpaqueCsgIntervalSampleComputeEmulationGraphTask::record("
    );
    expectRecordUsesOwnedSnapshot(
        taskRecords,
        "bool OpaqueCsgIntervalSampleComputeEmulationGraphTask::record(",
        "NWB_IMPL_END"
    );
    expectRecordUsesOwnedSnapshot(taskRecords, "bool CsgReceiverSpanBuildGraphTask::record(", "bool CsgIntervalCombineGraphTask::record(");
    expectRecordUsesOwnedSnapshot(taskRecords, "bool CsgIntervalCombineGraphTask::record(", "bool CsgIntervalSampleGraphTask::record(");
    expectRecordUsesOwnedSnapshot(taskRecords, "bool CsgIntervalSampleGraphTask::record(", "NWB_IMPL_END");
    expectRecordUsesOwnedSnapshot(
        taskRecords,
        "bool AvboitCsgReceiverSpanGraphTask::record(",
        "bool AvboitCsgIntervalCombineGraphTask::record("
    );
    expectRecordUsesOwnedSnapshot(
        taskRecords,
        "bool AvboitCsgIntervalCombineGraphTask::record(",
        "NWB_IMPL_END"
    );
    expectRecordUsesOwnedSnapshot(taskRecords, "bool AvboitPreGraphTask::record(", "bool AvboitOccupancyComputeEmulationGraphTask::record(");
    expectRecordUsesOwnedSnapshot(
        taskRecords,
        "bool AvboitOccupancyComputeEmulationGraphTask::record(",
        "bool AvboitOccupancySharedComputeEmulationGraphTask::record("
    );
    expectRecordUsesOwnedSnapshot(taskRecords, "bool AvboitOccupancyGraphTask::record(", "bool AvboitDepthWarpGraphTask::record(");
    expectRecordUsesOwnedSnapshot(
        taskRecords,
        "bool AvboitExtinctionComputeEmulationGraphTask::record(",
        "bool AvboitExtinctionSharedComputeEmulationGraphTask::record("
    );
    expectRecordUsesOwnedSnapshot(taskRecords, "bool AvboitExtinctionGraphTask::record(", "bool AvboitIntegrationGraphTask::record(");
    expectRecordUsesOwnedSnapshot(
        taskRecords,
        "bool AvboitAccumulationComputeEmulationGraphTask::record(",
        "bool AvboitAccumulationSharedComputeEmulationGraphTask::record("
    );
    expectRecordUsesOwnedSnapshot(taskRecords, "bool AvboitAccumulationGraphTask::record(", "bool AvboitAccumulationFinalizeGraphTask::record(");

    EXPECT_EQ(CountText(rootGraph, "m_csgSystem.csgGraphResourceSnapshot()"), 1u);
    EXPECT_EQ(CountText(rootPrefix, "m_csgSystem.csgGraphResourceSnapshot()"), 0u);
    EXPECT_EQ(CountText(taskRecords, "csgGraphResourceSnapshot("), 0u);
    const usize snapshotCapture = rootGraph.find(
        "const ECSRenderDetail::CsgGraphResourceSnapshot csgResources = m_csgSystem.csgGraphResourceSnapshot();"
    );
    ASSERT_NE(snapshotCapture, AStringView::npos);
    const usize prefixDeclaration = rootGraph.find("declareDeferredGraphicsPrefixTasks(", snapshotCapture);
    ASSERT_NE(prefixDeclaration, AStringView::npos);
    const usize prefixDeclarationEnd = rootGraph.find(
        "NWB_LOGGER_WARNING(NWB_TEXT(\"RendererSystem: could not declare deferred graphics-prefix packet\"));",
        prefixDeclaration
    );
    ASSERT_NE(prefixDeclarationEnd, AStringView::npos);
    const AStringView prefixDeclarationCall = rootGraph.substr(
        prefixDeclaration,
        prefixDeclarationEnd - prefixDeclaration
    );
    EXPECT_LT(snapshotCapture, prefixDeclaration);
    EXPECT_TRUE(ContainsText(
        prefixDeclarationCall,
        "csgFrameState,\n        frameBindings,\n        csgResources,\n        hasOpaqueCsgFrameWork,"
    ));
    EXPECT_EQ(CountText(rootPrefix, "gbufferPayload.csgResources = csgResources;"), 1u);
    EXPECT_EQ(CountText(rootPrefix, "csgReceiverSpanPayload.csgResources = csgResources;"), 1u);
    EXPECT_EQ(CountText(rootPrefix, "csgIntervalCombinePayload.csgResources = csgResources;"), 1u);
    EXPECT_EQ(CountText(rootPrefix, "csgIntervalSamplePayload.csgResources = csgResources;"), 1u);
    EXPECT_EQ(CountText(rootPrefix, "opaqueCsgReceiverComputeEmulationPayload.csgResources = csgResources;"), 1u);
    EXPECT_EQ(CountText(rootPrefix, "opaqueCsgIntervalSampleComputeEmulationPayload.csgResources = csgResources;"), 1u);
    EXPECT_EQ(CountText(rootPrefix, ".csgResources = csgResources;"), 6u);
    EXPECT_EQ(CountText(rootGraph, "avboitPrePayload.csgResources = csgResources;"), 1u);
    EXPECT_EQ(CountText(rootGraph, "avboitCsgReceiverSpanPayload.csgResources = csgResources;"), 1u);
    EXPECT_EQ(CountText(rootGraph, "avboitCsgIntervalCombinePayload.csgResources = csgResources;"), 1u);
    EXPECT_EQ(CountText(rootGraph, "avboitOccupancyPayload.csgResources = csgResources;"), 1u);
    EXPECT_EQ(CountText(rootGraph, "avboitOccupancyComputeEmulationPayload.csgResources = csgResources;"), 1u);
    EXPECT_EQ(CountText(rootGraph, "avboitExtinctionPayload.csgResources = csgResources;"), 1u);
    EXPECT_EQ(CountText(rootGraph, "avboitExtinctionComputeEmulationPayload.csgResources = csgResources;"), 1u);
    EXPECT_EQ(CountText(rootGraph, "avboitAccumulationPayload.csgResources = csgResources;"), 1u);
    EXPECT_EQ(CountText(rootGraph, "avboitAccumulationComputeEmulationPayload.csgResources = csgResources;"), 1u);
    EXPECT_EQ(CountText(rootGraph, ".csgResources = csgResources;"), 9u);

    EXPECT_FALSE(ContainsText(boundaries, "CsgGraphResourceBuffers"));
    EXPECT_FALSE(ContainsText(boundaries, "csgFrameBuffersReady("));
    EXPECT_FALSE(ContainsText(boundaries, "populateCsgGraphResourceBuffers("));
    EXPECT_FALSE(ContainsText(boundaries, "findCsgClipContextHeapSlot("));
    EXPECT_FALSE(ContainsText(taskRecords, "csgFrameBuffersReady("));
    EXPECT_FALSE(ContainsText(taskRecords, "populateCsgGraphResourceBuffers("));
    EXPECT_FALSE(ContainsText(taskRecords, "findCsgClipContextHeapSlot("));
    EXPECT_FALSE(ContainsText(materialRecords, "csgFrameBuffersReady("));
    EXPECT_FALSE(ContainsText(materialRecords, "populateCsgGraphResourceBuffers("));
    EXPECT_FALSE(ContainsText(materialRecords, "findCsgClipContextHeapSlot("));
    EXPECT_EQ(CountText(materialRecords, "csgGraphResourceSnapshot("), 0u);

    const auto expectNoLiveCsgResourceState = [](const AStringView source, const AStringView scopeMarker){
        SCOPED_TRACE(scopeMarker.data());
        EXPECT_FALSE(ContainsText(source, "m_csgState.m_receiverRangeBuffer"));
        EXPECT_FALSE(ContainsText(source, "m_csgState.m_cutterBuffer"));
        EXPECT_FALSE(ContainsText(source, "m_csgState.m_clipContextSlotsBuffer"));
        EXPECT_FALSE(ContainsText(source, "m_csgState.m_clipContextSlotsHeapHandle"));
        EXPECT_FALSE(ContainsText(source, "m_csgState.m_intervalSampleStateBuffer"));
        EXPECT_FALSE(ContainsText(source, "m_csgState.m_intervalSampleStateHeapHandle"));
    };
    expectNoLiveCsgResourceState(taskRecords, "task record callbacks");
    expectNoLiveCsgResourceState(materialRecords, "material and AVBOIT record callbacks");

    const usize intervalRecordBegin = csgInterval.find("bool RendererCsgSystem::prepareCsgIntervalSampleStateData(");
    ASSERT_NE(intervalRecordBegin, AStringView::npos);
    const AStringView intervalRecord = csgInterval.substr(intervalRecordBegin);
    expectNoLiveCsgResourceState(intervalRecord, "CSG interval preparation and record callbacks");

    const usize clipContextPrepareBegin = csgResources.find("bool RendererCsgSystem::prepareCsgClipContextSlotData(");
    ASSERT_NE(clipContextPrepareBegin, AStringView::npos);
    const usize clipContextPrepareEnd = csgResources.find(
        "void RendererCsgSystem::setCsgReceiverSurfaceImageStates(",
        clipContextPrepareBegin
    );
    ASSERT_NE(clipContextPrepareEnd, AStringView::npos);
    const AStringView clipContextPrepare = csgResources.substr(
        clipContextPrepareBegin,
        clipContextPrepareEnd - clipContextPrepareBegin
    );
    expectNoLiveCsgResourceState(clipContextPrepare, "CSG clip-context preparation");
    EXPECT_TRUE(ContainsText(clipContextPrepare, "csgResources.frameReady(csgFrameData)"));

    const usize clipBufferStatesBegin = csgResources.find("void RendererCsgSystem::setCsgClipBufferStates(");
    ASSERT_NE(clipBufferStatesBegin, AStringView::npos);
    const usize clipBufferStatesEnd = csgResources.find(
        "bool RendererCsgSystem::resolveCsgReceiverClipDrawInfo(",
        clipBufferStatesBegin
    );
    ASSERT_NE(clipBufferStatesEnd, AStringView::npos);
    const AStringView clipBufferStates = csgResources.substr(
        clipBufferStatesBegin,
        clipBufferStatesEnd - clipBufferStatesBegin
    );
    expectNoLiveCsgResourceState(clipBufferStates, "CSG record-time buffer states");
    EXPECT_TRUE(ContainsText(clipBufferStates, "csgResources.receiverRanges.get()"));
    EXPECT_TRUE(ContainsText(clipBufferStates, "csgResources.cutters.get()"));
    EXPECT_TRUE(ContainsText(clipBufferStates, "csgResources.clipContextSlots.get()"));
    EXPECT_TRUE(ContainsText(clipBufferStates, "csgResources.intervalSampleState.get()"));

    const usize materialUploadPrepareBegin = materialResources.find(
        "void RendererMaterialSystem::prepareMaterialPassInstanceUploadData("
    );
    ASSERT_NE(materialUploadPrepareBegin, AStringView::npos);
    const usize materialUploadPrepareEnd = materialResources.find(
        "\nNWB_IMPL_END",
        materialUploadPrepareBegin
    );
    ASSERT_NE(materialUploadPrepareEnd, AStringView::npos);
    const AStringView materialUploadPrepare = materialResources.substr(
        materialUploadPrepareBegin,
        materialUploadPrepareEnd - materialUploadPrepareBegin
    );
    expectNoLiveCsgResourceState(materialUploadPrepare, "material instance-upload preparation");
    EXPECT_TRUE(ContainsText(materialUploadPrepare, "const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources"));
    EXPECT_TRUE(ContainsText(materialUploadPrepare, "csgResources.findClipContextHeapSlot(csgContextHeapSlot)"));
}


// CSG's peel/event payload images carry no validity by themselves: interval ID and receiver-event count gate every
// later load. Keep those payload declarations write-only so a transparent-only frame can begin from fresh native
// textures without inventing a prior state, while the cleared validity images retain their ReadWrite contracts.
TEST(EcsGraphics, CsgIntervalProducerPayloadsDoNotRequirePriorNativeState){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString graphicsPrefixSource;
    AString deferredGraphSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graphics_prefix.cpp",
        graphicsPrefixSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp",
        deferredGraphSource
    ));
    const AStringView graphicsPrefix(graphicsPrefixSource.data(), graphicsPrefixSource.size());
    const AStringView deferredGraph(deferredGraphSource.data(), deferredGraphSource.size());

    const auto expectWriteOnlyPayloads = [](
        const AStringView source,
        const AStringView beginMarker,
        const AStringView endMarker
    ){
        const usize begin = source.find(beginMarker);
        ASSERT_NE(begin, AStringView::npos);
        const usize end = source.find(endMarker, begin);
        ASSERT_NE(end, AStringView::npos);
        ASSERT_LT(begin, end);
        const AStringView producerUses = source.substr(begin, end - begin);

        EXPECT_TRUE(ContainsText(producerUses, "            WriteTextureUse(csgCapBackNormal,"));
        EXPECT_TRUE(ContainsText(producerUses, "            WriteTextureUse(csgIntervalDepth,"));
        EXPECT_TRUE(ContainsText(
            producerUses,
            ".push_back(WriteTextureUse(\n            csgReceiverEventData,"
        ));
        EXPECT_FALSE(ContainsText(producerUses, "            ReadWriteTextureUse(csgCapBackNormal,"));
        EXPECT_FALSE(ContainsText(producerUses, "            ReadWriteTextureUse(csgIntervalDepth,"));
        EXPECT_FALSE(ContainsText(
            producerUses,
            ".push_back(ReadWriteTextureUse(\n            csgReceiverEventData,"
        ));
        EXPECT_TRUE(ContainsText(producerUses, "            ReadWriteTextureUse(csgIntervalId,"));
        EXPECT_TRUE(ContainsText(
            producerUses,
            ".push_back(ReadWriteTextureUse(\n            csgReceiverEventCount,"
        ));
    };
    expectWriteOnlyPayloads(
        graphicsPrefix,
        "csgReceiverSpanResourceUses.reserve(4u + (hasCsgFrameGpuWork ? 2u : 0u));",
        "csgReceiverSpanResourceUses.push_back(ReadTextureUse("
    );
    expectWriteOnlyPayloads(
        deferredGraph,
        "avboitIntervalResourceUses.reserve(16u);",
        "const Core::GpuTaskResourceSetUse transparentCsgMaterialGeometrySetUse"
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

