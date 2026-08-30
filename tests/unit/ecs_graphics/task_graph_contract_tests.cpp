// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = NWB::Tests::TestAString;
using TestPath = ::Path<NWB::Core::Alloc::GlobalArena>;

struct TaskGraphContractTestArenaTag{};
using TestArena = NWB::Tests::TestArena<TaskGraphContractTestArenaTag>;


static bool ContainsText(const AStringView text, const AStringView expected){
    AString normalized;
    normalized.reserve(text.size());
    for(const char ch : text){
        if(ch != '\r')
            normalized += ch;
    }
    return AStringView(normalized.data(), normalized.size()).find(expected) != AStringView::npos;
}

static usize CountText(const AStringView text, const AStringView expected){
    if(expected.empty())
        return 0u;
    AString normalized;
    normalized.reserve(text.size());
    for(const char ch : text){
        if(ch != '\r')
            normalized += ch;
    }
    const AStringView normalizedText(normalized.data(), normalized.size());
    usize count = 0u;
    usize offset = 0u;
    while(offset < normalizedText.size()){
        const usize found = normalizedText.find(expected, offset);
        if(found == AStringView::npos)
            break;
        ++count;
        offset = found + expected.size();
    }
    return count;
}

static TestPath RepoRoot(TestArena& testArena){
    return TestPath(testArena.arena, __FILE__).parent_path().parent_path().parent_path().parent_path().lexically_normal();
}


// Source contracts name only their implementation owners. Do not sweep every task-graph source here: an unrelated
// file split must not change a contract that has no dependency on it.
static bool ReadRendererSources(
    const TestPath& repoRoot,
    const InitializerList<StringView> sourcePaths,
    AString& outSource
){
    const TestPath rendererDirectory = repoRoot / "impl" / "ecs_render";
    outSource.clear();
    for(const StringView sourcePath : sourcePaths){
        AString source;
        if(!ReadTextFile(rendererDirectory / sourcePath.data(), source))
            return false;
        if(!outSource.empty())
            outSource += "\n\n";
        outSource.append(source.data(), source.size());
    }
    return true;
}


static bool ReadGraphicsModuleSources(const TestPath& repoRoot, AString& outSource){
    static constexpr StringView s_SourceNames[] = {
        "module.cpp",
        "module_graph_setup.cpp",
        "module_texture_upload.cpp",
        "module_setup.cpp",
    };

    const TestPath graphicsDirectory = repoRoot / "core" / "graphics";
    outSource.clear();
    for(const StringView sourceName : s_SourceNames){
        AString source;
        if(!ReadTextFile(graphicsDirectory / sourceName.data(), source))
            return false;
        if(!outSource.empty())
            outSource += "\n\n";
        outSource.append(source.data(), source.size());
    }
    return true;
}


static bool ReadRendererFramePipelineRuntimeSources(const TestPath& repoRoot, AString& outSource){
    static constexpr StringView s_SourceNames[] = {
        "renderer_frame_pipeline.cpp",
        "renderer_frame_pipeline_resources.cpp",
        "renderer_frame_pipeline_execute.cpp",
    };

    const TestPath pipelineDirectory = repoRoot / "impl" / "ecs_render";
    outSource.clear();
    for(const StringView sourceName : s_SourceNames){
        AString source;
        if(!ReadTextFile(pipelineDirectory / sourceName.data(), source))
            return false;
        if(!outSource.empty())
            outSource += "\n\n";
        outSource.append(source.data(), source.size());
    }
    return true;
}


// RendererSystem is the ECS/render-pass adapter exposed by module.h. It owns only the root frame pipeline and
// delegates frame work without absorbing graph task identities or feature-domain state.
TEST(EcsGraphics, RendererModuleKeepsFramePipelineBehindSingleAdapterOwner){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString moduleHeaderSource;
    AString moduleSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "module.h", moduleHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "module.cpp", moduleSource));
    const AStringView moduleHeader(moduleHeaderSource.data(), moduleHeaderSource.size());
    const AStringView module(moduleSource.data(), moduleSource.size());

    EXPECT_TRUE(ContainsText(moduleHeader, "class RendererFramePipeline;"));
    EXPECT_TRUE(ContainsText(moduleHeader, "NotNullUniquePtr<RendererFramePipeline, PipelineOwner::deleter_type> m_pipeline;"));
    EXPECT_EQ(CountText(moduleHeader, " m_"), 1u);
    EXPECT_FALSE(ContainsText(moduleHeader, "Core::GpuTaskId"));
    EXPECT_FALSE(ContainsText(moduleHeader, "TaskGraph m_"));
    EXPECT_FALSE(ContainsText(moduleHeader, "State m_"));

    EXPECT_TRUE(ContainsText(module, "Core::MakeGlobalUnique<RendererFramePipeline>("));
    EXPECT_TRUE(ContainsText(module, "return m_pipeline->validateResources(width, height, sampleCount);"));
    EXPECT_TRUE(ContainsText(module, "m_pipeline->invalidateResources();"));
    EXPECT_TRUE(ContainsText(module, "m_pipeline->update(world, delta);"));
    EXPECT_TRUE(ContainsText(module, "return m_pipeline->prepareResources(framebuffer);"));
    EXPECT_TRUE(ContainsText(module, "m_pipeline->render(framebuffer);"));
    EXPECT_TRUE(ContainsText(module, "return m_pipeline->appendFrameGraph(builder);"));
    EXPECT_EQ(CountText(module, "m_pipeline->"), 10u);
    EXPECT_FALSE(ContainsText(module, "m_deferredLightingTaskGraph"));
    EXPECT_FALSE(ContainsText(module, "Core::GpuTaskId"));
}


// Graph callbacks consume only the services and domain systems required by their payload. Retaining the concrete
// root pipeline here would let task owners bypass those contracts and require privileged access to orchestration state.
TEST(EcsGraphics, RendererTaskPayloadsDependOnExactDomainsInsteadOfFramePipeline){
    struct ExpectedDependency{
        StringView declaration;
        usize count = 0u;
    };

    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    const TestPath rendererDirectory = repoRoot / "impl" / "ecs_render";

    const auto verifyTaskHeader = [&rendererDirectory](
        const StringView sourcePath,
        const InitializerList<ExpectedDependency> expectedDependencies
    ){
        SCOPED_TRACE(sourcePath.data());

        AString source;
        ASSERT_TRUE(ReadTextFile(rendererDirectory / sourcePath.data(), source));
        const AStringView taskHeader(source.data(), source.size());

        EXPECT_FALSE(ContainsText(taskHeader, "RendererFramePipeline"));
        EXPECT_FALSE(ContainsText(taskHeader, "renderer_frame_pipeline.h"));
        EXPECT_FALSE(ContainsText(taskHeader, "* renderer = nullptr;"));
        for(const ExpectedDependency& dependency : expectedDependencies)
            EXPECT_EQ(CountText(taskHeader, dependency.declaration), dependency.count);
    };

    verifyTaskHeader(
        "raytrace/task_graph_shadow_prepare_tasks.h",
        {
            { "Core::Graphics* graphics = nullptr;", 1u },
            { "RendererRayTracingSystem* raytracingSystem = nullptr;", 3u },
            { "ShadowPreparationOutcome* outcome = nullptr;", 1u },
        }
    );
    verifyTaskHeader(
        "mesh/task_graph_prefix_tasks.h",
        {
            { "Core::Graphics* graphics = nullptr;", 1u },
            { "RendererMeshSystem* meshSystem = nullptr;", 1u },
        }
    );
    verifyTaskHeader(
        "deferred/task_graph_prefix_tasks.h",
        {
            { "RendererDeferredSystem* deferredSystem = nullptr;", 1u },
        }
    );
    verifyTaskHeader(
        "deferred/task_graph_gbuffer_task.h",
        {
            { "Core::Graphics* graphics = nullptr;", 1u },
            { "RendererMaterialSystem* materialSystem = nullptr;", 1u },
            { "RendererCsgSystem* csgSystem = nullptr;", 1u },
        }
    );
    verifyTaskHeader(
        "material/task_graph_opaque_compute_tasks.h",
        {
            { "RendererMeshSystem* meshSystem = nullptr;", 0u },
            { "RendererMaterialSystem* materialSystem = nullptr;", 2u },
        }
    );
    verifyTaskHeader(
        "csg/task_graph_opaque_compute_tasks.h",
        {
            { "Core::Graphics* graphics = nullptr;", 1u },
            { "RendererMeshSystem* meshSystem = nullptr;", 0u },
            { "RendererMaterialSystem* materialSystem = nullptr;", 2u },
            { "RendererCsgSystem", 0u },
        }
    );
    verifyTaskHeader(
        "csg/task_graph_opaque_interval_tasks.h",
        {
            { "Core::Graphics* graphics = nullptr;", 1u },
            { "RendererMaterialSystem* materialSystem = nullptr;", 3u },
            { "RendererCsgSystem* csgSystem = nullptr;", 3u },
        }
    );
    verifyTaskHeader(
        "csg/task_graph_transparent_interval_tasks.h",
        {
            { "RendererMaterialSystem* materialSystem = nullptr;", 2u },
            { "RendererCsgSystem* csgSystem = nullptr;", 2u },
        }
    );
    verifyTaskHeader(
        "avboit/task_graph_occupancy_tasks.h",
        {
            { "Core::Graphics* graphics = nullptr;", 2u },
            { "RendererMeshSystem* meshSystem = nullptr;", 0u },
            { "RendererMaterialSystem* materialSystem = nullptr;", 2u },
            { "RendererCsgSystem", 0u },
        }
    );
    verifyTaskHeader(
        "avboit/task_graph_extinction_integration_tasks.h",
        {
            { "Core::Graphics* graphics = nullptr;", 2u },
            { "RendererMeshSystem* meshSystem = nullptr;", 0u },
            { "RendererMaterialSystem* materialSystem = nullptr;", 2u },
            { "RendererCsgSystem", 0u },
        }
    );
    verifyTaskHeader(
        "avboit/task_graph_accumulation_tasks.h",
        {
            { "Core::Graphics* graphics = nullptr;", 2u },
            { "RendererMeshSystem* meshSystem = nullptr;", 0u },
            { "RendererMaterialSystem* materialSystem = nullptr;", 2u },
            { "RendererCsgSystem", 0u },
        }
    );

    AString pipelineHeaderSource;
    ASSERT_TRUE(ReadTextFile(rendererDirectory / "renderer_frame_pipeline.h", pipelineHeaderSource));
    const AStringView pipelineHeader(pipelineHeaderSource.data(), pipelineHeaderSource.size());
    static constexpr StringView s_RemovedTaskFriendDeclarations[] = {
        "friend struct ECSRenderDetail::ShadowPrepareGraphTask;",
        "friend struct ECSRenderDetail::MeshViewSetupGraphTask;",
        "friend struct ECSRenderDetail::MeshViewUploadCommitGraphTask;",
        "friend struct ECSRenderDetail::SceneShadingSetupGraphTask;",
        "friend struct ECSRenderDetail::OpaqueRegularComputeEmulationGraphTask;",
        "friend struct ECSRenderDetail::OpaqueRegularSharedComputeEmulationGraphTask;",
        "friend struct ECSRenderDetail::OpaqueCsgReceiverComputeEmulationGraphTask;",
        "friend struct ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphTask;",
        "friend struct RendererTaskGraphDetail::AvboitOccupancyComputeEmulationGraphTask;",
        "friend struct RendererTaskGraphDetail::AvboitOccupancySharedComputeEmulationGraphTask;",
        "friend struct RendererTaskGraphDetail::AvboitExtinctionComputeEmulationGraphTask;",
        "friend struct RendererTaskGraphDetail::AvboitExtinctionSharedComputeEmulationGraphTask;",
        "friend struct RendererTaskGraphDetail::AvboitAccumulationComputeEmulationGraphTask;",
        "friend struct RendererTaskGraphDetail::AvboitAccumulationSharedComputeEmulationGraphTask;",
        "friend struct ECSRenderDetail::GbufferGraphTask;",
        "friend struct ECSRenderDetail::CsgReceiverSpanBuildGraphTask;",
        "friend struct ECSRenderDetail::CsgIntervalCombineGraphTask;",
        "friend struct ECSRenderDetail::AvboitCsgReceiverSpanGraphTask;",
        "friend struct ECSRenderDetail::AvboitCsgIntervalCombineGraphTask;",
        "friend struct ECSRenderDetail::CsgIntervalSampleGraphTask;",
    };
    for(const StringView friendDeclaration : s_RemovedTaskFriendDeclarations)
        EXPECT_FALSE(ContainsText(pipelineHeader, friendDeclaration));
}


// Compile, recording, and accepted-submission statistics live with the immutable graph artifacts. Keep the renderer
// bridge by-value so debug tooling can inspect one coherent generation without reaching into private packet storage.
TEST(EcsGraphics, DeferredGraphExposesRuntimeTelemetryArtifacts){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemHeaderSource;
    AString systemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", systemHeaderSource));
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    const AStringView systemHeader(systemHeaderSource.data(), systemHeaderSource.size());
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(systemHeader, "deferredTaskGraphRuntimeStatistics()const noexcept"));
    EXPECT_TRUE(ContainsText(system, "Core::GpuTaskGraphRuntimeStatistics RendererFramePipeline::deferredTaskGraphRuntimeStatistics()const noexcept"));
    EXPECT_TRUE(ContainsText(system, "Core::CollectGpuTaskGraphRuntimeStatistics("));
    EXPECT_TRUE(ContainsText(system, "m_deferredLightingCompiledGraph,"));
    EXPECT_TRUE(ContainsText(system, "m_deferredLightingRecordedGraph,"));
    EXPECT_TRUE(ContainsText(system, "m_deferredLightingSubmissionTransaction"));
}


// Accepted queue assignment history must survive ordinary frame graph resets and update even when capture is off.
// Detailed export binds that history to the exact compiled plan; resource invalidation is the only reset boundary.
TEST(EcsGraphics, DeferredGraphExportsAcceptedQueueAssignmentHistory){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemHeaderSource;
    AString systemSource;
    AString resourcesSource;
    AString renderSource;
    AString buildSource;
    AString frameGraphSource;
    AString frameModuleSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", systemHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.cpp", systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_resources.cpp", resourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_execute.cpp", renderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp", buildSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_telemetry.cpp", frameGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "frame" / "module.cpp", frameModuleSource));
    const AStringView systemHeader(systemHeaderSource.data(), systemHeaderSource.size());
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView resources(resourcesSource.data(), resourcesSource.size());
    const AStringView render(renderSource.data(), renderSource.size());
    const AStringView build(buildSource.data(), buildSource.size());
    const AStringView frameGraph(frameGraphSource.data(), frameGraphSource.size());
    const AStringView frameModule(frameModuleSource.data(), frameModuleSource.size());

    EXPECT_TRUE(ContainsText(systemHeader, "#include <core/graphics/task_graph/queue_assignment_telemetry.h>"));
    EXPECT_TRUE(ContainsText(
        systemHeader,
        "Core::GpuTaskGraphQueueAssignmentTelemetryTracker m_deferredLightingTaskGraphQueueAssignmentTelemetry;"
    ));
    EXPECT_TRUE(ContainsText(system, ", m_deferredLightingTaskGraphQueueAssignmentTelemetry(arena)"));
    EXPECT_TRUE(ContainsText(resources, "m_deferredLightingTaskGraphQueueAssignmentTelemetry.reset();"));
    EXPECT_FALSE(ContainsText(render, "m_deferredLightingTaskGraphQueueAssignmentTelemetry.reset();"));
    EXPECT_FALSE(ContainsText(build, "m_deferredLightingTaskGraphQueueAssignmentTelemetry.reset();"));

    const usize previousRefreshOffset = render.find(
        "m_deferredLightingTaskGraphQueueAssignmentTelemetry.update("
    );
    const usize validResetOffset = render.find("m_deferredLightingTaskGraphValid = false;", previousRefreshOffset);
    const usize graphResetOffset = render.find("m_deferredLightingTaskGraph.reset();", previousRefreshOffset);
    ASSERT_NE(previousRefreshOffset, AStringView::npos);
    ASSERT_NE(validResetOffset, AStringView::npos);
    ASSERT_NE(graphResetOffset, AStringView::npos);
    EXPECT_LT(previousRefreshOffset, validResetOffset);
    EXPECT_LT(previousRefreshOffset, graphResetOffset);
    EXPECT_TRUE(ContainsText(render, "deferred queue-assignment history refresh failed before graph reset"));

    const usize currentRefreshOffset = frameGraph.find(
        "m_deferredLightingTaskGraphQueueAssignmentTelemetry.update("
    );
    const usize guardedExportOffset = frameGraph.find("else{", currentRefreshOffset);
    const usize telemetryOptionsOffset = frameGraph.find(
        "const Core::GpuTaskGraphTelemetryOptions deferredLightingTelemetryOptions",
        guardedExportOffset
    );
    const usize taskGraphExportOffset = frameGraph.find(
        "m_deferredLightingTaskGraph.appendFrameGraphTelemetry(",
        telemetryOptionsOffset
    );
    ASSERT_NE(currentRefreshOffset, AStringView::npos);
    ASSERT_NE(guardedExportOffset, AStringView::npos);
    ASSERT_NE(telemetryOptionsOffset, AStringView::npos);
    ASSERT_NE(taskGraphExportOffset, AStringView::npos);
    EXPECT_LT(currentRefreshOffset, guardedExportOffset);
    EXPECT_LT(guardedExportOffset, telemetryOptionsOffset);
    EXPECT_LT(currentRefreshOffset, telemetryOptionsOffset);
    EXPECT_LT(telemetryOptionsOffset, taskGraphExportOffset);
    EXPECT_TRUE(ContainsText(frameGraph, "skipping detailed task graph export"));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        ".queueAssignments = &m_deferredLightingTaskGraphQueueAssignments,"
    ));
    EXPECT_TRUE(ContainsText(frameGraph, ".compiledGraph = &m_deferredLightingCompiledGraph,"));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        ".queueAssignmentTelemetry = &m_deferredLightingTaskGraphQueueAssignmentTelemetry,"
    ));

    const usize graphicsRunOffset = frameModule.find("m_graphics.runFrame()");
    const usize frameGraphRecordOffset = frameModule.find("m_frameGraphRegistry.record(", graphicsRunOffset);
    ASSERT_NE(graphicsRunOffset, AStringView::npos);
    ASSERT_NE(frameGraphRecordOffset, AStringView::npos);
    EXPECT_LT(graphicsRunOffset, frameGraphRecordOffset);
}


// FrameGraphBuilder retains labels by view until the capture is encoded. Keep the renderer's human-readable
// runtime snapshot in persistent renderer-owned storage, and reset the label when no coherent attempt exists.
TEST(EcsGraphics, DeferredGraphRuntimeTelemetryUsesPersistentFrameGraphLabel){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemHeaderSource;
    AString systemSource;
    AString frameGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", systemHeaderSource));
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_telemetry.cpp", frameGraphSource));
    const AStringView systemHeader(systemHeaderSource.data(), systemHeaderSource.size());
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView frameGraph(frameGraphSource.data(), frameGraphSource.size());

    EXPECT_TRUE(ContainsText(systemHeader, "AString<Core::Alloc::GlobalArena> m_frameGraphRendererLabel;"));
    EXPECT_TRUE(ContainsText(system, ", m_frameGraphRendererLabel(arena)"));
    EXPECT_TRUE(ContainsText(frameGraph, "Core::GpuTaskGraphRuntimeStatistics deferredRuntimeStatistics{};"));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "if(builder.frameIndex() == m_frameGraphSourceFrameIndex)\n"
        "        deferredRuntimeStatistics = deferredTaskGraphRuntimeStatistics();"
    ));
    EXPECT_TRUE(ContainsText(frameGraph, "if(deferredRuntimeStatistics.valid()){"));
    EXPECT_TRUE(ContainsText(frameGraph, "StringAppendFormat(\n            m_frameGraphRendererLabel,"));
    EXPECT_TRUE(ContainsText(frameGraph, "\"Task graph: tasks={} packets={} deps={} transitions={}\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"Declarations: resource sets={} resource-set members={} direct uses={} declared set uses={} expanded set-member uses={} materialized uses={}\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"Data: payload objects={} payload object bytes={} upload blobs={} upload blob bytes={}\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"Recording: packets={} tasks={} command lists={} barriers={} worker-routed={} overlapped={}\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"Submission: accepted packets={} accepted tasks={} rejected packets={} rejected tasks={} submissions={} accepted frontier={} recovery submissions={} command lists={} waits={} failed submissions={}\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"CPU: declaration={:.3f} ms compile={:.3f} ms native recording elapsed={:.3f} ms submit={:.3f} ms\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.declarationSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "\"CPU compile phases: analysis={:.3f} ms queue assignment={:.3f} ms planning={:.3f} ms\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"CPU analysis detail: validation={:.3f} ms dependencies={:.3f} ms hazards={:.3f} ms cycles/topology={:.3f} ms\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"CPU planning detail: packetization={:.3f} ms resource states/barriers={:.3f} ms packet dependencies={:.3f} ms\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"CPU recording summed spans: packet={:.3f} ms command-list acquisition={:.3f} ms graph barrier lowering={:.3f} ms task={:.3f} ms\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"Ready-frontier recording: elapsed={:.3f} ms logical-worker busy={:.3f} ms logical-worker capacity={:.3f} ms utilization={:.1f}%\""));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.analysisSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.queueAssignmentSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.planningSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.validationSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.dependencyAnalysisSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.hazardAnalysisSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.topologicalOrderSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.packetizationSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.resourceStatePlanningSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.packetDependencyPlanningSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.resourceSetCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.resourceSetMemberCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.directResourceUseCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.declaredResourceSetUseCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.expandedResourceSetMemberUseCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.resourceUseCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.payloadObjectCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.payloadObjectBytes,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.uploadBlobCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.uploadBlobBytes,"));
    EXPECT_TRUE(ContainsText(frameGraph, "recordingStatistics.commandListAcquisitionSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "recordingStatistics.graphBarrierRecordingSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "recordingStatistics.taskRecordSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "recordingStatistics.recordingElapsedSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "recordingStatistics.readyFrontierElapsedSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "recordingStatistics.readyFrontierWorkerBusySeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "recordingStatistics.readyFrontierWorkerCapacitySeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "recordingStatistics.readyFrontierWorkerUtilization() * 100.0"));
    EXPECT_TRUE(ContainsText(frameGraph, "submissionStatistics.acceptedPacketCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "submissionStatistics.acceptedTaskCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "submissionStatistics.rejectedPacketCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "submissionStatistics.rejectedTaskCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "submissionStatistics.acceptedFrontierSubmissionCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "submissionStatistics.recoverySubmissionCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "submissionStatistics.rejectedSubmissionCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "accepted frontier={} recovery submissions={} CPU={:.3f} ms"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueStatistics.recoverySubmissionCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "m_frameGraphRendererLabel += \"Renderer Frame\";"));
    EXPECT_TRUE(ContainsText(frameGraph, "AStringView(m_frameGraphRendererLabel.data(), m_frameGraphRendererLabel.size())"));
}


// Structured runtime statistics belong to the renderer-frame pass, not its label or every semantic child pass.
// Translate the coherent by-value graph snapshot into telemetry-owned fields and omit stale/invalid generations.
TEST(EcsGraphics, DeferredGraphAttachesStructuredRuntimeStatisticsOnlyToRendererFramePass){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString frameGraphSource;
    AString runtimeStatisticsSource;
    AString systemHeaderSource;
    AString renderSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_telemetry.cpp", frameGraphSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "kernel" / "frame_graph_runtime_statistics.cpp",
        runtimeStatisticsSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", systemHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_execute.cpp", renderSource));
    const AStringView frameGraph(frameGraphSource.data(), frameGraphSource.size());
    const AStringView runtimeStatistics(runtimeStatisticsSource.data(), runtimeStatisticsSource.size());
    const AStringView systemHeader(systemHeaderSource.data(), systemHeaderSource.size());
    const AStringView render(renderSource.data(), renderSource.size());

    EXPECT_TRUE(ContainsText(
        runtimeStatistics,
        "Core::Telemetry::FrameGraphRuntimeStatistics ECSRenderDetail::BuildFrameGraphRuntimeStatistics("
    ));
    EXPECT_TRUE(ContainsText(
        runtimeStatistics,
        "if(captureFrameIndex != sourceFrameIndex || !statistics.valid())\n        return {};"
    ));
    EXPECT_TRUE(ContainsText(runtimeStatistics, ".graphGeneration = compileStatistics.graphGeneration,"));
    EXPECT_TRUE(ContainsText(runtimeStatistics, ".planGeneration = compileStatistics.planGeneration,"));
    EXPECT_TRUE(ContainsText(
        runtimeStatistics,
        ".recordingAttemptGeneration = recordingStatistics.recordingAttemptGeneration,"
    ));
    EXPECT_TRUE(ContainsText(runtimeStatistics, ".deviceGeneration = compileStatistics.deviceGeneration,"));
    EXPECT_TRUE(ContainsText(runtimeStatistics, ".taskCount = static_cast<u64>(compileStatistics.taskCount),"));
    EXPECT_TRUE(ContainsText(runtimeStatistics, ".resourceCount = static_cast<u64>(compileStatistics.resourceCount),"));
    EXPECT_TRUE(ContainsText(
        runtimeStatistics,
        "compileStatistics.logicalOwnershipTransferCountByRoute[Core::GpuOwnershipTransferRoute::Internal]"
    ));
    EXPECT_EQ(CountText(runtimeStatistics, "compileStatistics.logicalOwnershipTransferCountByRoute["), 3u);
    EXPECT_TRUE(ContainsText(runtimeStatistics, ".totalSeconds = compileStatistics.totalSeconds,"));
    EXPECT_TRUE(ContainsText(
        runtimeStatistics,
        ".recordingElapsedSeconds = recordingStatistics.recordingElapsedSeconds,"
    ));
    EXPECT_TRUE(ContainsText(
        runtimeStatistics,
        ".acceptedFrontierSubmissionCount = static_cast<u64>("
    ));
    EXPECT_EQ(CountText(runtimeStatistics, ".recoverySubmissionCount = static_cast<u64>("), 2u);
    EXPECT_EQ(CountText(runtimeStatistics, "submissionStatistics.recoverySubmissionCount"), 2u);
    EXPECT_TRUE(ContainsText(runtimeStatistics, ".submissionSeconds = submissionStatistics.submissionSeconds,"));
    EXPECT_TRUE(ContainsText(runtimeStatistics, ".present = true,"));
    EXPECT_TRUE(ContainsText(
        runtimeStatistics,
        "if(!Core::Telemetry::IsValidFrameGraphRuntimeStatistics(result))\n        return {};"
    ));
    EXPECT_TRUE(ContainsText(
        runtimeStatistics,
        "ECSRenderDetail::BuildFrameGraphPacketSubmissionStatistics("
    ));
    EXPECT_TRUE(ContainsText(runtimeStatistics, ".packetIndex = statistics.packet.index,"));
    EXPECT_TRUE(ContainsText(runtimeStatistics, ".packetGeneration = statistics.packet.generation,"));
    EXPECT_TRUE(ContainsText(
        runtimeStatistics,
        ".commandListCount = static_cast<u64>(statistics.nativeCommandListCount),"
    ));
    EXPECT_TRUE(ContainsText(
        runtimeStatistics,
        ".plannedWaitTokenCount = static_cast<u64>(statistics.plannedWaitTokenCount),"
    ));
    EXPECT_TRUE(ContainsText(runtimeStatistics, ".recoverySubmission = statistics.isRecoverySubmission,"));

    EXPECT_TRUE(ContainsText(systemHeader, "u64 m_frameGraphSourceFrameIndex = Limit<u64>::s_Max;"));
    EXPECT_TRUE(ContainsText(render, "m_frameGraphSourceFrameIndex = m_graphics.getFrameIndex();"));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "Core::GpuTaskGraphRuntimeStatistics deferredRuntimeStatistics{};\n"
        "    if(builder.frameIndex() == m_frameGraphSourceFrameIndex)\n"
        "        deferredRuntimeStatistics = deferredTaskGraphRuntimeStatistics();"
    ));
    EXPECT_TRUE(ContainsText(frameGraph, "if(deferredRuntimeStatistics.valid()){"));

    EXPECT_TRUE(ContainsText(frameGraph, "Core::Telemetry::FrameGraphPassMetadata rendererFrameMetadata;"));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "rendererFrameMetadata.runtimeStatistics = ECSRenderDetail::BuildFrameGraphRuntimeStatistics(\n"
        "        deferredRuntimeStatistics,\n"
        "        builder.frameIndex(),\n"
        "        m_frameGraphSourceFrameIndex\n"
        "    );"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "const Handle rendererFrame = builder.addPass(\n"
        "        Name(\"ecs_render/frame\"),\n"
        "        AStringView(m_frameGraphRendererLabel.data(), m_frameGraphRendererLabel.size()),\n"
        "        rendererFrameMetadata\n"
        "    );"
    ));
    EXPECT_EQ(CountText(
        frameGraph,
        "Core::Telemetry::FrameGraphPassMetadata rendererFrameMetadata;"
    ), 1u);
    EXPECT_EQ(CountText(
        frameGraph,
        "rendererFrameMetadata.runtimeStatistics = ECSRenderDetail::BuildFrameGraphRuntimeStatistics("
    ), 1u);
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "m_deferredLightingSubmissionTransaction.packetSubmissionStatistics("
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "packetSubmissionStatistics.size() != deferredRuntimeStatistics.submission.nativeSubmissionCount"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "ECSRenderDetail::BuildFrameGraphPacketSubmissionStatistics(packetStatistics, rendererFrame.index)"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "builder.addPacketSubmissionStatistics(rendererFrame, telemetryStatistics)"
    ));
}


// Timing availability and outcomes belong to the device-wide recorder rather than one compiled graph attempt. Keep
// the snapshot by value, export explicit skip reasons, and enumerate every live physical queue even on no-graph
// frames so unsupported capabilities remain distinguishable from measured zero-duration work.
TEST(EcsGraphics, FrameGraphExportsDeviceWideGpuTimingCapabilitiesAndOutcomes){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString timingHeaderSource;
    AString timingSource;
    AString timingSubmissionSource;
    AString frameGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "gpu_timing.h", timingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "gpu_timing.cpp", timingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "gpu_timing_submission.cpp", timingSubmissionSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_telemetry.cpp", frameGraphSource));
    const AStringView timingHeader(timingHeaderSource.data(), timingHeaderSource.size());
    const AStringView timing(timingSource.data(), timingSource.size());
    const AStringView timingSubmission(timingSubmissionSource.data(), timingSubmissionSource.size());
    const AStringView frameGraph(frameGraphSource.data(), frameGraphSource.size());

    EXPECT_TRUE(ContainsText(timingHeader, "namespace GpuTimingScopeSkipReason{"));
    EXPECT_TRUE(ContainsText(timingHeader, "CollectionInactive,"));
    EXPECT_TRUE(ContainsText(timingHeader, "QueueTimestampsUnsupported,"));
    EXPECT_TRUE(ContainsText(timingHeader, "ComparableTimestampsUnsupported,"));
    EXPECT_TRUE(ContainsText(timingHeader, "ScopeNotPrepared,"));
    EXPECT_TRUE(ContainsText(timingHeader, "QueryCapacityUnavailable,"));
    EXPECT_TRUE(ContainsText(timingHeader, "RecordingPositionUnavailable,"));
    EXPECT_TRUE(ContainsText(timingHeader, "struct GpuTimingRecorderStatistics{"));
    EXPECT_TRUE(ContainsText(timingHeader, "u64 publishedSampleCount = 0u;"));
    EXPECT_TRUE(ContainsText(timingHeader, "u64 unpublishedSampleCount = 0u;"));
    EXPECT_TRUE(ContainsText(timingHeader, "u64 skippedScopeCountByReason[GpuTimingScopeSkipReason::kCount]{};"));
    EXPECT_TRUE(ContainsText(timingHeader, "GpuTimingRecorderStatistics statistics(const Device& device)const;"));
    EXPECT_FALSE(ContainsText(timingHeader, "const GpuTimingRecorderStatistics& statistics("));
    EXPECT_FALSE(ContainsText(timingHeader, "holdSubmissionCompletionForTesting"));
    EXPECT_FALSE(ContainsText(timingHeader, "releaseSubmissionCompletionForTesting"));
    EXPECT_FALSE(ContainsText(timingHeader, "m_heldSubmissionCompletion"));
    EXPECT_TRUE(ContainsText(timing, "GpuTimingRecorderStatistics result = m_statistics;"));
    EXPECT_TRUE(ContainsText(timing, "result.deviceGeneration = device.getDeviceGeneration();"));
    EXPECT_TRUE(ContainsText(timing, "m_statistics = {};"));
    EXPECT_TRUE(ContainsText(timing, "device.queueGetCompletedInstance(physicalQueue)"));
    EXPECT_FALSE(ContainsText(timing, "holdSubmissionCompletionForTesting"));
    EXPECT_FALSE(ContainsText(timing, "releaseSubmissionCompletionForTesting"));
    EXPECT_FALSE(ContainsText(timing, "m_heldSubmissionCompletion"));
    EXPECT_TRUE(ContainsText(timing, "++m_publishedSampleCount;"));
    EXPECT_TRUE(ContainsText(timing, "++m_unpublishedSampleCount;"));
    EXPECT_TRUE(ContainsText(timing, "++m_statistics.scopeAttemptCount;"));
    EXPECT_TRUE(ContainsText(timing, "GpuTimingScopeSkipReason::CollectionInactive"));
    EXPECT_TRUE(ContainsText(timing, "GpuTimingScopeSkipReason::QueueTimestampsUnsupported"));
    EXPECT_TRUE(ContainsText(timing, "GpuTimingScopeSkipReason::ComparableTimestampsUnsupported"));
    EXPECT_TRUE(ContainsText(timing, "GpuTimingScopeSkipReason::ScopeNotPrepared"));
    EXPECT_TRUE(ContainsText(timing, "GpuTimingScopeSkipReason::QueryCapacityUnavailable"));
    EXPECT_TRUE(ContainsText(timing, "GpuTimingScopeSkipReason::RecordingPositionUnavailable"));
    EXPECT_TRUE(ContainsText(timingSubmission, "beginScope(scopeDefinition.identity, device, commandList, attribution, false, m_scope)"));
    EXPECT_FALSE(ContainsText(timingSubmission, "if(!device.supportsComparableGpuTimestamps(commandList.getResolvedDescription().physicalQueue))"));

    const usize fallbackOffset = frameGraph.find("m_frameGraphRendererLabel += \"Renderer Frame\";");
    const usize snapshotOffset = frameGraph.find("const Core::GpuTimingRecorderStatistics gpuTimingStatistics");
    const usize topologyOffset = frameGraph.find("const Core::GpuPhysicalQueueTopology gpuTimingQueueTopology");
    const usize descriptorOffset = frameGraph.find("const Core::GpuDescriptorHeapLifecycleStatistics");
    ASSERT_NE(fallbackOffset, AStringView::npos);
    ASSERT_NE(snapshotOffset, AStringView::npos);
    ASSERT_NE(topologyOffset, AStringView::npos);
    ASSERT_NE(descriptorOffset, AStringView::npos);
    EXPECT_LT(fallbackOffset, snapshotOffset);
    EXPECT_LT(snapshotOffset, topologyOffset);
    EXPECT_LT(topologyOffset, descriptorOffset);
    EXPECT_TRUE(ContainsText(frameGraph, "m_graphics.gpuTiming().statistics(device)"));
    EXPECT_TRUE(ContainsText(frameGraph, "GPU timing (device-wide cumulative since query reset):"));
    EXPECT_TRUE(ContainsText(frameGraph, "GPU timing outcomes: attempts={} recorded={} accepted={} published={} completed unpublished={}"));
    EXPECT_TRUE(ContainsText(frameGraph, "skipped inactive/no timestamps/no comparable timestamps/unprepared/no capacity/recording unavailable={}/{}/{}/{}/{}/{}"));
    EXPECT_TRUE(ContainsText(frameGraph, "gpuTimingStatistics.unpublishedSampleCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "gpuTimingStatistics.skippedScopeCountByReason[Core::GpuTimingScopeSkipReason::RecordingPositionUnavailable]"));
    EXPECT_TRUE(ContainsText(frameGraph, "device.getPhysicalQueueTopology()"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueInfo.id.deviceGeneration,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueInfo.familyIndex,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueInfo.queueIndex,"));
    EXPECT_TRUE(ContainsText(frameGraph, "static_cast<u32>(queueInfo.capabilities),"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueInfo.timestampValidBits != 0u,"));
    EXPECT_TRUE(ContainsText(frameGraph, "device.supportsComparableGpuTimestamps(queueInfo.id)"));
    const AStringView timingQueueSlice = frameGraph.substr(topologyOffset, descriptorOffset - topologyOffset);
    EXPECT_FALSE(ContainsText(timingQueueSlice, "continue;"));
    EXPECT_FALSE(ContainsText(timingQueueSlice, "durationSeconds"));
    EXPECT_FALSE(ContainsText(timingQueueSlice, "0.0"));
}


// Descriptor heap lifetime is owned by the Device rather than any deferred graph attempt or physical queue. Keep
// one by-value current snapshot on the persistent renderer label so no-graph frames retain this diagnostic context.
TEST(EcsGraphics, FrameGraphExportsDeviceWideDescriptorHeapLifecycle){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString frameGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_telemetry.cpp", frameGraphSource));
    const AStringView frameGraph(frameGraphSource.data(), frameGraphSource.size());

    EXPECT_TRUE(ContainsText(
        frameGraph,
        "const Core::GpuDescriptorHeapLifecycleStatistics descriptorHeapLifecycleStatistics =\n"
        "        device.getDescriptorHeap().lifecycleStatistics()\n"
        "    ;"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "\"\\nDescriptor heap lifecycle (device-wide current): initialized={} \"\n"
        "        \"resource live/capacity={}/{} sampler live/capacity={}/{} \"\n"
        "        \"acceleration structure live/capacity={}/{} pending retired slots={} \"\n"
        "        \"accepted heap uses={} unsubmitted heap uses={} abandoned heap uses={}\""
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "StringAppendFormat(\n"
        "        m_frameGraphRendererLabel,\n"
        "        \"\\nDescriptor heap lifecycle (device-wide current): initialized={} \"\n"
        "        \"resource live/capacity={}/{} sampler live/capacity={}/{} \"\n"
        "        \"acceleration structure live/capacity={}/{} pending retired slots={} \"\n"
        "        \"accepted heap uses={} unsubmitted heap uses={} abandoned heap uses={}\""
    ));

    const usize snapshotOffset = frameGraph.find("const Core::GpuDescriptorHeapLifecycleStatistics descriptorHeapLifecycleStatistics");
    const usize runtimeValidOffset = frameGraph.find("if(deferredRuntimeStatistics.valid()){");
    const usize queueLoopOffset = frameGraph.find(
        "for(usize queueIndex = 0u; queueIndex < physicalQueueRuntimeStatistics.size(); ++queueIndex){"
    );
    const usize fallbackOffset = frameGraph.find("m_frameGraphRendererLabel += \"Renderer Frame\";");
    const usize lifecycleLabelOffset = frameGraph.find("Descriptor heap lifecycle (device-wide current):");
    const usize rendererFrameOffset = frameGraph.find("const Handle rendererFrame = builder.addPass(");
    ASSERT_NE(snapshotOffset, AStringView::npos);
    ASSERT_NE(runtimeValidOffset, AStringView::npos);
    ASSERT_NE(queueLoopOffset, AStringView::npos);
    ASSERT_NE(fallbackOffset, AStringView::npos);
    ASSERT_NE(lifecycleLabelOffset, AStringView::npos);
    ASSERT_NE(rendererFrameOffset, AStringView::npos);
    EXPECT_LT(runtimeValidOffset, snapshotOffset);
    EXPECT_LT(queueLoopOffset, snapshotOffset);
    EXPECT_LT(fallbackOffset, snapshotOffset);
    EXPECT_LT(snapshotOffset, lifecycleLabelOffset);
    EXPECT_LT(fallbackOffset, lifecycleLabelOffset);
    EXPECT_LT(lifecycleLabelOffset, rendererFrameOffset);

    const AStringView lifecycleLabel = frameGraph.substr(lifecycleLabelOffset, rendererFrameOffset - lifecycleLabelOffset);
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.initialized,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.resourceLiveSlotCount,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.resourceCapacity,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.samplerLiveSlotCount,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.samplerCapacity,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.accelStructLiveSlotCount,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.accelStructCapacity,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.pendingRetiredSlotCount,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.acceptedHeapUseCount,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.unsubmittedHeapUseCount,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.abandonedHeapUseCount"));
    EXPECT_FALSE(ContainsText(lifecycleLabel, "queueStatistics."));
    EXPECT_FALSE(ContainsText(lifecycleLabel, "queueInfo."));
}


// Renderer-side graph declaration is intentionally separate from core compilation, and a failed attempt must not
// publish its elapsed time because the compiler only publishes the supplied value with a completed immutable plan.
TEST(EcsGraphics, DeferredGraphMeasuresDeclarationAttemptBeforeCoreCompile){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "renderer_frame_pipeline_graph.cpp" }, taskGraphSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    const usize lightingOffset = taskGraph.find("void RendererFramePipeline::buildDeferredLightingTaskGraph");
    const usize resetOffset = taskGraph.find(
        "m_deferredLightingSubmissionTransaction.reset(m_deferredLightingCompiledGraph);",
        lightingOffset
    );
    const usize declarationBeginOffset = taskGraph.find("const Timer declarationBegin = TimerNow();", resetOffset);
    const usize feedbackOffset = taskGraph.find(
        "m_deferredTaskTimingFeedback.configureCompileOptions(",
        declarationBeginOffset
    );
    const usize declarationEndOffset = taskGraph.find(
        "compileOptions.declarationSeconds = DurationInSeconds<f64>(TimerNow(), declarationBegin);",
        feedbackOffset
    );
    const usize compilerOffset = taskGraph.find("if(!compiler.compile(", declarationEndOffset);
    ASSERT_NE(lightingOffset, AStringView::npos);
    ASSERT_NE(resetOffset, AStringView::npos);
    ASSERT_NE(declarationBeginOffset, AStringView::npos);
    ASSERT_NE(feedbackOffset, AStringView::npos);
    ASSERT_NE(declarationEndOffset, AStringView::npos);
    ASSERT_NE(compilerOffset, AStringView::npos);
    EXPECT_TRUE(ContainsText(taskGraph, "#include <global/timer.h>"));
    EXPECT_LT(resetOffset, declarationBeginOffset);
    EXPECT_LT(declarationBeginOffset, feedbackOffset);
    EXPECT_LT(feedbackOffset, declarationEndOffset);
    EXPECT_LT(declarationEndOffset, compilerOffset);
}


// The renderer label must enumerate the immutable compiled plan, not the live Device registry: compile, transaction,
// and recording telemetry are generation-bound to that plan. Retain terminal-work and logical ownership boundary
// queues while keeping truly idle topology entries out of the persistent FrameGraph label.
TEST(EcsGraphics, DeferredGraphFrameTelemetryUsesCompiledPhysicalQueueSnapshots){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString commandHeaderSource;
    AString compiledGraphHeaderSource;
    AString compiledGraphSource;
    AString frameGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "rhi" / "command.h", commandHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "task_graph" / "compiled_graph.h", compiledGraphHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "task_graph" / "compiled_graph.cpp", compiledGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_telemetry.cpp", frameGraphSource));
    const AStringView commandHeader(commandHeaderSource.data(), commandHeaderSource.size());
    const AStringView compiledGraphHeader(compiledGraphHeaderSource.data(), compiledGraphHeaderSource.size());
    const AStringView compiledGraph(compiledGraphSource.data(), compiledGraphSource.size());
    const AStringView frameGraph(frameGraphSource.data(), frameGraphSource.size());

    EXPECT_TRUE(ContainsText(commandHeader, "Borrowed immutable topology view; its producer owns the storage."));
    EXPECT_TRUE(ContainsText(commandHeader, "struct GpuCommandArenaStatistics{"));
    EXPECT_TRUE(ContainsText(commandHeader, "struct GpuCommandArenaWorkerStatistics{"));
    EXPECT_TRUE(ContainsText(commandHeader, "Direct recording is always queryable"));
    EXPECT_TRUE(ContainsText(commandHeader, "nativeHandleStorageLowerBoundBytes counts only the"));
    EXPECT_TRUE(ContainsText(compiledGraphHeader, "GpuPhysicalQueueTopology queueTopology()const noexcept;"));
    EXPECT_TRUE(ContainsText(compiledGraphHeader, "Borrowed immutable-plan topology view."));
    EXPECT_TRUE(ContainsText(compiledGraphHeader, "serialize access\n    // with reset/recompile"));
    EXPECT_TRUE(ContainsText(
        compiledGraphHeader,
        "GpuTaskGraphPhysicalQueueCompileStatistics physicalQueueCompileStatistics("
    ));
    EXPECT_TRUE(ContainsText(
        compiledGraphHeader,
        "    usize epilogueBarrierCount = 0u;\n"
        "    usize ownershipReleaseBarrierCount = 0u;\n"
        "    usize ownershipAcquireBarrierCount = 0u;\n"
        "    usize incomingLogicalOwnershipTransferCount = 0u;\n"
        "    usize outgoingLogicalOwnershipTransferCount = 0u;\n"
        "    usize incomingLogicalOwnershipTransferSignatureCount = 0u;\n"
        "    usize outgoingLogicalOwnershipTransferSignatureCount = 0u;\n"
        "    usize incomingRepeatedOwnershipTransferSignatureCount = 0u;\n"
        "    usize outgoingRepeatedOwnershipTransferSignatureCount = 0u;\n"
        "    usize concurrentSharingAdviceResourceCount = 0u;\n\n"
        "    [[nodiscard]] bool valid()const noexcept{"
    ));
    EXPECT_TRUE(ContainsText(compiledGraph, "if(!valid())\n        return {};"));
    EXPECT_TRUE(ContainsText(compiledGraph, ".queues = m_queueTopology.empty() ? nullptr : m_queueTopology.data(),"));
    EXPECT_TRUE(ContainsText(compiledGraph, ".queueCount = m_queueTopology.size(),"));

    EXPECT_TRUE(ContainsText(frameGraph, "if(deferredRuntimeStatistics.valid()){"));
    EXPECT_TRUE(ContainsText(frameGraph, "m_deferredLightingCompiledGraph.queueTopology()"));
    const usize gpuTimingTopologyOffset = frameGraph.find(
        "const Core::GpuPhysicalQueueTopology gpuTimingQueueTopology"
    );
    ASSERT_NE(gpuTimingTopologyOffset, AStringView::npos);
    const AStringView compiledQueueTelemetry = frameGraph.substr(0u, gpuTimingTopologyOffset);
    EXPECT_FALSE(ContainsText(compiledQueueTelemetry, "getPhysicalQueueTopology()"));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "const Core::GpuPhysicalQueueId queue = runtimeQueueTopology.queues[queueIndex].id;"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "m_deferredLightingCompiledGraph.physicalQueueCompileStatistics(queue)"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "m_deferredLightingRecordedGraph.physicalQueueRecordingStatistics(\n"
        "                        m_deferredLightingCompiledGraph,\n"
        "                        queue\n"
        "                    )"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "m_deferredLightingSubmissionTransaction.physicalQueueSubmissionStatistics(\n"
        "                        m_deferredLightingCompiledGraph,\n"
        "                        queue\n"
        "                    )"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "ECSRenderDetail::BuildFrameGraphPhysicalQueueRuntimeStatistics("
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "Core::Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner("
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "physicalQueueRuntimeStatistics.push_back(queueStatistics);"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "const Core::GpuPhysicalQueueInfo& queueInfo = runtimeQueueTopology.queues[queueIndex];"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "const Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatistics& queueRuntimeStatistics =\n"
        "                physicalQueueRuntimeStatistics[queueIndex]"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "const Core::Telemetry::FrameGraphPhysicalQueueSubmissionRuntimeStatistics& queueStatistics =\n"
        "                queueRuntimeStatistics.submission"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "const Core::Telemetry::FrameGraphPhysicalQueueCompileRuntimeStatistics& queueCompileStatistics =\n"
        "                queueRuntimeStatistics.compile"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "const Core::Telemetry::FrameGraphPhysicalQueueRecordingRuntimeStatistics& queueRecordingStatistics =\n"
        "                queueRuntimeStatistics.recording"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "const bool hasTerminalSubmissionWork =\n"
        "                queueStatistics.acceptedPacketCount != 0u || queueStatistics.rejectedPacketCount != 0u\n"
        "            ;"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "const bool hasLogicalOwnershipTelemetry =\n"
        "                queueCompileStatistics.incomingLogicalOwnershipTransferCount != 0u\n"
        "                || queueCompileStatistics.outgoingLogicalOwnershipTransferCount != 0u"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "if(!hasTerminalSubmissionWork && !hasLogicalOwnershipTelemetry)\n"
        "                continue;"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "m_graphics.getDevice().getCommandArenaStatistics(queueInfo.id)"
    ));
    EXPECT_TRUE(ContainsText(frameGraph, "if(!commandArenaStatistics.valid())\n                continue;"));
    const usize snapshotLoopOffset = frameGraph.find(
        "for(usize queueIndex = 0u; queueIndex < runtimeQueueTopology.queueCount; ++queueIndex){"
    );
    const usize queueCompileQueryOffset = frameGraph.find(
        "m_deferredLightingCompiledGraph.physicalQueueCompileStatistics(queue)"
    );
    const usize queueRecordingQueryOffset = frameGraph.find(
        "m_deferredLightingRecordedGraph.physicalQueueRecordingStatistics("
    );
    const usize queueSubmissionQueryOffset = frameGraph.find(
        "m_deferredLightingSubmissionTransaction.physicalQueueSubmissionStatistics("
    );
    const usize queueValidationOffset = frameGraph.find(
        "Core::Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner("
    );
    const usize queueCacheOffset = frameGraph.find("physicalQueueRuntimeStatistics.push_back(queueStatistics);");
    const usize cachedLabelLoopOffset = frameGraph.find(
        "for(usize queueIndex = 0u; queueIndex < physicalQueueRuntimeStatistics.size(); ++queueIndex){"
    );
    const usize terminalSubmissionGateOffset = frameGraph.find("const bool hasTerminalSubmissionWork =");
    const usize logicalOwnershipGateOffset = frameGraph.find("const bool hasLogicalOwnershipTelemetry =");
    const usize idleQueueGateOffset = frameGraph.find("if(!hasTerminalSubmissionWork && !hasLogicalOwnershipTelemetry)");
    const usize queueFamilyIndexOffset = frameGraph.find("queueInfo.familyIndex,");
    const usize queueNativeIndexOffset = frameGraph.find("queueInfo.queueIndex,");
    const usize queueDedicatedOffset = frameGraph.find("queueInfo.dedicated,");
    ASSERT_NE(snapshotLoopOffset, AStringView::npos);
    ASSERT_NE(queueCompileQueryOffset, AStringView::npos);
    ASSERT_NE(queueRecordingQueryOffset, AStringView::npos);
    ASSERT_NE(queueSubmissionQueryOffset, AStringView::npos);
    ASSERT_NE(queueValidationOffset, AStringView::npos);
    ASSERT_NE(queueCacheOffset, AStringView::npos);
    ASSERT_NE(cachedLabelLoopOffset, AStringView::npos);
    ASSERT_NE(terminalSubmissionGateOffset, AStringView::npos);
    ASSERT_NE(logicalOwnershipGateOffset, AStringView::npos);
    ASSERT_NE(idleQueueGateOffset, AStringView::npos);
    ASSERT_NE(queueFamilyIndexOffset, AStringView::npos);
    ASSERT_NE(queueNativeIndexOffset, AStringView::npos);
    ASSERT_NE(queueDedicatedOffset, AStringView::npos);
    EXPECT_LT(snapshotLoopOffset, queueCompileQueryOffset);
    EXPECT_LT(queueCompileQueryOffset, queueRecordingQueryOffset);
    EXPECT_LT(queueRecordingQueryOffset, queueSubmissionQueryOffset);
    EXPECT_LT(queueSubmissionQueryOffset, queueValidationOffset);
    EXPECT_LT(queueValidationOffset, queueCacheOffset);
    EXPECT_LT(queueCacheOffset, cachedLabelLoopOffset);
    EXPECT_LT(cachedLabelLoopOffset, terminalSubmissionGateOffset);
    EXPECT_LT(terminalSubmissionGateOffset, logicalOwnershipGateOffset);
    EXPECT_LT(logicalOwnershipGateOffset, idleQueueGateOffset);
    EXPECT_LT(idleQueueGateOffset, queueFamilyIndexOffset);
    EXPECT_LT(queueFamilyIndexOffset, queueNativeIndexOffset);
    EXPECT_LT(queueNativeIndexOffset, queueDedicatedOffset);
    EXPECT_EQ(CountText(frameGraph, ".physicalQueueCompileStatistics("), 1u);
    EXPECT_EQ(CountText(frameGraph, ".physicalQueueRecordingStatistics("), 1u);
    EXPECT_EQ(CountText(frameGraph, ".physicalQueueSubmissionStatistics("), 1u);
    EXPECT_FALSE(ContainsText(frameGraph, "queueCompileStatistics.taskCount == 0u"));
    EXPECT_FALSE(ContainsText(frameGraph, "queueCompileStatistics.packetCount == 0u"));
    EXPECT_FALSE(ContainsText(frameGraph, "queueRecordingStatistics.packetCount == 0u"));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "Physical queue index={} generation={} class={} family index={} native queue index={} dedicated={}:"
    ));
    EXPECT_TRUE(ContainsText(frameGraph, "accepted packets={} accepted tasks={} rejected packets={} rejected tasks={}"));
    EXPECT_TRUE(ContainsText(frameGraph, "native submissions={} rejected submit paths={} command lists={}"));
    EXPECT_TRUE(ContainsText(frameGraph, "planned waits={} same-queue elisions={} timeline waits={} merged timeline waits={}"));
    EXPECT_TRUE(ContainsText(frameGraph, "accepted frontier={} recovery submissions={} CPU={:.3f} ms"));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "  Compile plan: tasks={} packets={} merged tasks={} prologue barriers={} epilogue barriers={} raw ownership release barriers (subset)={} raw ownership acquire barriers (subset)={}"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "  Logical ownership: incoming/outgoing records={}/{} signatures={}/{} repeated signatures={}/{} attributed advice resources={}"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "  Recording: packets={} tasks={} command lists={} barriers={} worker-routed={} overlapped={} CPU summed spans: command-list acquisition={:.3f} ms graph barrier lowering={:.3f} ms task={:.3f} ms packet={:.3f} ms"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "  Command arena: workers={} epochs={} pending epochs={} command buffers current/high-water={}/{} reusable={} leased={} pending={} growth={} resets={} native handle storage lower bound={} bytes"
    ));
    EXPECT_TRUE(ContainsText(frameGraph, "commandArenaStatistics.pendingCommandPoolEpochCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "commandArenaStatistics.nativeHandleStorageLowerBoundBytes"));
    EXPECT_TRUE(ContainsText(frameGraph, "__hidden_frame_graph_export::AppendCommandArenaWorkerStatistics("));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "m_graphics.getDevice().getCommandArenaWorkerStatistics(queueInfo.id, 0u, 0u)"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "m_deferredLightingCompiledGraph.packetIdAt(packetIndex)"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "previousRecordedPacket->recordingWorkerDomain == recordedPacket->recordingWorkerDomain"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "previousRecordedPacket->recordingWorkerIndex == recordedPacket->recordingWorkerIndex"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "Worker arena domain={} index={}: epochs={} pending epochs={} command buffers current/high-water={}/{}"
    ));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRuntimeStatistics.queue.index,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRuntimeStatistics.queue.deviceGeneration,"));
    EXPECT_TRUE(ContainsText(frameGraph, "__hidden_frame_graph_export::PhysicalQueueClassLabel(queueInfo.queueClass),"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueInfo.familyIndex,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueInfo.queueIndex,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueInfo.dedicated,"));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "__hidden_frame_graph_export::PhysicalQueueClassLabel(queueInfo.queueClass),\n"
        "                queueInfo.familyIndex,\n"
        "                queueInfo.queueIndex,\n"
        "                queueInfo.dedicated,\n"
        "                queueStatistics.acceptedPacketCount,"
    ));
    EXPECT_TRUE(ContainsText(frameGraph, "queueStatistics.rejectedSubmissionCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueStatistics.submissionSeconds * 1000.0"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueCompileStatistics.taskCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueCompileStatistics.packetCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueCompileStatistics.mergedTaskCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueCompileStatistics.prologueBarrierCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueCompileStatistics.epilogueBarrierCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueCompileStatistics.ownershipReleaseBarrierCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueCompileStatistics.ownershipAcquireBarrierCount,"));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "queueCompileStatistics.epilogueBarrierCount,\n"
        "                queueCompileStatistics.ownershipReleaseBarrierCount,\n"
        "                queueCompileStatistics.ownershipAcquireBarrierCount,\n"
        "                queueCompileStatistics.incomingLogicalOwnershipTransferCount,\n"
        "                queueCompileStatistics.outgoingLogicalOwnershipTransferCount,\n"
        "                queueCompileStatistics.incomingLogicalOwnershipTransferSignatureCount,\n"
        "                queueCompileStatistics.outgoingLogicalOwnershipTransferSignatureCount,\n"
        "                queueCompileStatistics.incomingRepeatedOwnershipTransferSignatureCount,\n"
        "                queueCompileStatistics.outgoingRepeatedOwnershipTransferSignatureCount,\n"
        "                queueCompileStatistics.concurrentSharingAdviceResourceCount,\n"
        "                queueRecordingStatistics.packetCount,"
    ));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.packetCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.taskCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.commandListCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.barrierCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.workerRoutedPacketCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.parallelPacketCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.commandListAcquisitionSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.graphBarrierRecordingSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.taskRecordSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.recordingSeconds * 1000.0"));
}


// Logical ownership records are plan-level movements, not a restatement of their raw release/acquire markers. Export
// the immutable records and both aggregation layers so diagnostics preserve resource identity, the exact route, and
// enough queue-family and sharing evidence to make concurrent-sharing advice actionable.
TEST(EcsGraphics, DeferredGraphFrameTelemetryReportsLogicalOwnershipPlan){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString frameGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_telemetry.cpp", frameGraphSource));
    const AStringView frameGraph(frameGraphSource.data(), frameGraphSource.size());
    const usize logicalOwnershipEnumerationOffset = frameGraph.find("const usize logicalOwnershipTransferCount =");
    const usize logicalOwnershipEnumerationEndOffset = frameGraph.find(
        "const Core::GpuDescriptorHeapLifecycleStatistics descriptorHeapLifecycleStatistics =",
        logicalOwnershipEnumerationOffset
    );
    ASSERT_NE(logicalOwnershipEnumerationOffset, AStringView::npos);
    ASSERT_NE(logicalOwnershipEnumerationEndOffset, AStringView::npos);
    const AStringView logicalOwnershipEnumeration = frameGraph.substr(
        logicalOwnershipEnumerationOffset,
        logicalOwnershipEnumerationEndOffset - logicalOwnershipEnumerationOffset
    );

    EXPECT_TRUE(ContainsText(
        frameGraph,
        "Raw barriers: transitions={} UAV={} ownership releases={} ownership acquires={} state exports={}"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "Logical ownership transfers: records={} signatures={} repeated signatures={} concurrent-sharing candidate records={} advised repeated resources={} route records internal/import/export={}/{}/{}"
    ));
    // This positional sequence requires the logical count from the compiler snapshot directly. Summing the raw
    // release/acquire marker counters would double-count an internal movement and cannot satisfy this contract.
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "compileStatistics.transitionBarrierCount,\n"
        "            compileStatistics.uavBarrierCount,\n"
        "            compileStatistics.ownershipReleaseBarrierCount,\n"
        "            compileStatistics.ownershipAcquireBarrierCount,\n"
        "            compileStatistics.stateExportBarrierCount,\n"
        "            compileStatistics.logicalOwnershipTransferCount,\n"
        "            compileStatistics.logicalOwnershipTransferSignatureCount,\n"
        "            compileStatistics.repeatedOwnershipTransferSignatureCount,\n"
        "            compileStatistics.concurrentSharingCouldAvoidTransferCount,\n"
        "            compileStatistics.concurrentSharingAdviceResourceCount,\n"
        "            compileStatistics.logicalOwnershipTransferCountByRoute[Core::GpuOwnershipTransferRoute::Internal],\n"
        "            compileStatistics.logicalOwnershipTransferCountByRoute[Core::GpuOwnershipTransferRoute::ExternalImport],\n"
        "            compileStatistics.logicalOwnershipTransferCountByRoute[Core::GpuOwnershipTransferRoute::ExternalExport],"
    ));
    EXPECT_EQ(CountText(frameGraph, "compileStatistics.logicalOwnershipTransferCount,"), 1u);
    // The human-readable route triplet stays here; the structured triplet has its own mapper owner and contract.
    EXPECT_EQ(CountText(frameGraph, "compileStatistics.logicalOwnershipTransferCountByRoute["), 3u);

    EXPECT_EQ(CountText(frameGraph, "m_deferredLightingCompiledGraph.logicalOwnershipTransfers()"), 1u);
    EXPECT_EQ(CountText(frameGraph, "m_deferredLightingCompiledGraph.logicalOwnershipTransferAt("), 0u);
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "const Core::GpuCompiledOwnershipTransfer* const logicalOwnershipTransfers =\n"
        "            m_deferredLightingCompiledGraph.logicalOwnershipTransfers()"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "for(usize transferIndex = 0u; transferIndex < logicalOwnershipTransferCount; ++transferIndex){\n"
        "                const Core::GpuCompiledOwnershipTransfer& transfer = logicalOwnershipTransfers[transferIndex];"
    ));
    EXPECT_TRUE(ContainsText(logicalOwnershipEnumeration, "NWB_ASSERT(transfer.valid());"));
    EXPECT_EQ(CountText(logicalOwnershipEnumeration, "transfer.valid()"), 1u);
    EXPECT_FALSE(ContainsText(logicalOwnershipEnumeration, "if(!transfer.valid())"));
    EXPECT_EQ(CountText(logicalOwnershipEnumeration, "continue;"), 0u);
    EXPECT_FALSE(ContainsText(logicalOwnershipEnumeration, "taskPrologueBarriers("));
    EXPECT_FALSE(ContainsText(logicalOwnershipEnumeration, "taskEpilogueBarriers("));
    EXPECT_FALSE(ContainsText(logicalOwnershipEnumeration, "GpuCompiledBarrier"));
    EXPECT_FALSE(ContainsText(logicalOwnershipEnumeration, "m_deferredLightingTaskGraph."));
    EXPECT_FALSE(ContainsText(logicalOwnershipEnumeration, "resourceAt("));
    EXPECT_FALSE(ContainsText(logicalOwnershipEnumeration, "queueInfo("));
    EXPECT_FALSE(ContainsText(logicalOwnershipEnumeration, "queueTopology"));
    EXPECT_FALSE(ContainsText(logicalOwnershipEnumeration, "m_graphics.getDevice()"));
    EXPECT_FALSE(ContainsText(logicalOwnershipEnumeration, "sameTransferSignature"));
    EXPECT_FALSE(ContainsText(logicalOwnershipEnumeration, "signatureAlreadyCounted"));
    EXPECT_FALSE(ContainsText(logicalOwnershipEnumeration, "hasEarlierDistinctSignature"));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "Logical ownership transfer {}: resource identity={} route={} source physical queue index={} generation={} family={} destination physical queue index={} generation={} family={} declared sharing={} mask={} concurrent sharing could avoid={}"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "transfer.resourceIdentity.c_str(),\n"
        "                    __hidden_frame_graph_export::OwnershipTransferRouteLabel(transfer.route),\n"
        "                    transfer.sourceQueue.index,\n"
        "                    transfer.sourceQueue.deviceGeneration,\n"
        "                    transfer.sourceQueueFamilyIndex,\n"
        "                    transfer.destinationQueue.index,\n"
        "                    transfer.destinationQueue.deviceGeneration,\n"
        "                    transfer.destinationQueueFamilyIndex,\n"
        "                    __hidden_frame_graph_export::ResourceQueueSharingLabel(transfer.declaredQueueSharing),\n"
        "                    static_cast<u32>(transfer.declaredQueueSharing),\n"
        "                    transfer.concurrentSharingCouldAvoid"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "case Core::GpuOwnershipTransferRoute::Internal:\n"
        "        return \"Internal\";"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "case Core::GpuOwnershipTransferRoute::ExternalImport:\n"
        "        return \"ExternalImport\";"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "case Core::GpuOwnershipTransferRoute::ExternalExport:\n"
        "        return \"ExternalExport\";\n"
        "    default:\n"
        "        return \"Unknown\";"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "case Core::ResourceQueueSharing::Exclusive:\n"
        "        return \"Exclusive\";\n"
        "    case Core::ResourceQueueSharing::Graphics:\n"
        "        return \"Graphics\";\n"
        "    case Core::ResourceQueueSharing::AsyncCompute:\n"
        "        return \"AsyncCompute\";\n"
        "    case Core::ResourceQueueSharing::Transfer:\n"
        "        return \"Transfer\";\n"
        "    case Core::ResourceQueueSharing::GraphicsAndAsyncCompute:\n"
        "        return \"GraphicsAndAsyncCompute\";\n"
        "    case Core::ResourceQueueSharing::GraphicsAndTransfer:\n"
        "        return \"GraphicsAndTransfer\";\n"
        "    case Core::ResourceQueueSharing::AsyncComputeAndTransfer:\n"
        "        return \"AsyncComputeAndTransfer\";\n"
        "    case Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer:\n"
        "        return \"GraphicsAsyncComputeAndTransfer\";\n"
        "    default:\n"
        "        return \"Unknown\";"
    ));
}


// MeshSkinning has one complete primary-Graphics packet. Its recorded-state preparation and accepted-state commit
// therefore stay semantic callbacks on the generic whole-graph executor rather than splitting record and submit.
TEST(EcsGraphics, MeshSkinningUsesFrontierScoredSerialPacketization){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString skinningSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_mesh" / "skinning" / "system.cpp", skinningSource));
    const AStringView skinning(skinningSource.data(), skinningSource.size());

    EXPECT_TRUE(ContainsText(skinning, "Core::GpuTaskGraphCompileOptions compileOptions;"));
    EXPECT_TRUE(ContainsText(skinning, "compileOptions.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierScored;"));
    EXPECT_TRUE(ContainsText(skinning, "compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena, compileOptions)"));
    EXPECT_EQ(CountText(skinning, "mergeWithPrevious"), 0u);
    EXPECT_EQ(CountText(skinning, "scheduling.frontierScoredMergeDomain = Name(\"mesh_skinning.serial\");"), 2u);
    EXPECT_EQ(CountText(skinning, "setDependencies(&terminalTask, 1u);"), 4u);
    EXPECT_TRUE(ContainsText(skinning, "if(compiledGraph.packetCount() != 1u)"));
    EXPECT_TRUE(ContainsText(skinning, "const Core::GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);"));
    EXPECT_TRUE(ContainsText(skinning, "terminalQueue->id != graphicsQueue"));
    EXPECT_EQ(CountText(skinning, "Core::GpuTaskGraphTaskTimingTicket{"), 1u);
    EXPECT_TRUE(ContainsText(skinning, ".task = terminalTask,\n            .timingTicket = &timingTicket,"));
    EXPECT_TRUE(ContainsText(skinning, "context->cache->buildMergedBufferSubset("));
    EXPECT_TRUE(ContainsText(skinning, "const Core::GpuTaskGraphTaskRecordedCallback recordedCallback{"));
    EXPECT_TRUE(ContainsText(skinning, "normalExecution.taskRecordedCallbacks = &recordedCallback;"));
    EXPECT_TRUE(ContainsText(skinning, "const Core::GpuTaskGraphTaskAcceptedCallback acceptedCallback{"));
    EXPECT_TRUE(ContainsText(skinning, ".task = terminalTask,\n        .context = &skinningState,"));
    EXPECT_TRUE(ContainsText(skinning, "context->stateAccepted = context->cache->commit(*context->candidate);"));
    EXPECT_TRUE(ContainsText(skinning, "submitter.recordAndSubmitNormalGraph("));
    EXPECT_FALSE(ContainsText(skinning, "recorder.recordPacketRangeInCompileOrder("));
    EXPECT_FALSE(ContainsText(skinning, "submitter.submitPacketRangeInCompileOrderFromTasks("));
    EXPECT_TRUE(ContainsText(skinning, "const Core::QueueSubmissionToken skinningToken = transaction.taskToken("));
    EXPECT_TRUE(ContainsText(skinning, "if(!skinningSubmitted || !skinningState.stateAccepted){"));
    EXPECT_FALSE(ContainsText(skinning, "mergeAcceptedSkinningState("));
}


// Caustics and Surfel GI choose a semantic producer task at graph declaration. Keep their normal-frame merge and
// presence validation task-based so a later packet split cannot leak compiler packet identities back into the
// renderer's effect policy.
TEST(EcsGraphics, EffectsTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(system, "const Core::GpuTaskId causticsTask"));
    EXPECT_TRUE(ContainsText(system, "m_deferredCausticPhotonTask,\n            causticsTask"));
    EXPECT_TRUE(ContainsText(system, "m_deferredCausticResolveUpsampleTask,\n            causticsTask"));
    EXPECT_TRUE(ContainsText(system, "m_deferredSurfelGiIrradianceClearTask,\n            m_deferredSurfelGiTask"));
    EXPECT_TRUE(ContainsText(system, "m_deferredSurfelGiResolveTask,\n                m_deferredSurfelGiTask"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredSurfelGiTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredHardwareCausticsTask)"));

    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId hardwareCausticsPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId causticPhotonPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId surfelGiPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId causticsPacket"));
}


// Prefix and shadow state, lifecycle, and timing contracts stay task-addressed while the shared normal executor
// owns compiler-generated packet coverage.
TEST(EcsGraphics, PrefixAndShadowTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    AString shadowPrepareSource;
    AString shadowVisibilitySource;
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "renderer_frame_pipeline_graph_shadow_prepare.cpp",
            "renderer_frame_pipeline_graph_shadow_visibility.cpp",
        },
        shadowPrepareSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp",
        shadowVisibilitySource
    ));
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView shadowPrepare(shadowPrepareSource.data(), shadowPrepareSource.size());
    const AStringView shadowVisibility(shadowVisibilitySource.data(), shadowVisibilitySource.size());

    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredShadowPrepareTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_graphicsPrefixTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_graphicsPrefixDeferredClearFirstTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredShadowVisibilityTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredSoftwareCausticsTask)"));
    EXPECT_TRUE(ContainsText(system, "tasksSharePacket(\n            m_graphicsPrefixDeferredClearFirstTask"));
    EXPECT_FALSE(ContainsText(system, "shadowPrepareAndMeshViewSetupTimingPacketsAreDistinct"));
    EXPECT_TRUE(ContainsText(shadowPrepare, ".states = m_shadowPreparePersistentState.source(),"));
    EXPECT_TRUE(ContainsText(shadowPrepare, ".setExternalStateSources("));
    EXPECT_TRUE(ContainsText(
        shadowVisibility,
        "const auto* const shadowScratchStates = m_shadowComputePersistentState.source();"
    ));
    EXPECT_TRUE(ContainsText(
        shadowVisibility,
        "const auto* const shadowReturnStates = m_shadowVisibilityReturnState.source();"
    ));
    EXPECT_TRUE(ContainsText(shadowVisibility, ".states = shadowScratchStates,"));
    EXPECT_TRUE(ContainsText(shadowVisibility, ".states = shadowReturnStates,"));
    EXPECT_TRUE(ContainsText(
        shadowVisibility,
        ".applicableConsumerQueueClass = Core::CommandQueue::Compute,"
    ));
    EXPECT_EQ(
        CountText(
            shadowVisibility,
            ".setExternalStateSources(shadowVisibilityStateSourceData, shadowVisibilityStateSourceCount)"
        ),
        6u
    );
    EXPECT_FALSE(ContainsText(system, "deferredStateBindings"));
    EXPECT_TRUE(ContainsText(system, ".context = &shadowPrepareStateLifecycle,\n        .invoke = prepareShadowPrepareTask,"));
    EXPECT_TRUE(ContainsText(system, ".context = &shadowPrepareStateLifecycle,\n        .invoke = acceptShadowPrepareTask,"));
    EXPECT_TRUE(ContainsText(
        system,
        ".context = &shadowVisibilityStateLifecycle,\n"
        "        .invoke = prepareShadowVisibilityTask,"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        ".context = &shadowVisibilityStateLifecycle,\n"
        "        .invoke = acceptShadowVisibilityTask,"
    ));
    EXPECT_TRUE(ContainsText(system, "normalTimingTicketCount == 1u + graphicsPrefixUniquePacketCount"));
    EXPECT_TRUE(ContainsText(system, "appendNormalTimingTicket(m_deferredShadowVisibilityTask, shadowVisibilityTimingTicket)"));

    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId shadowPreparePacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId graphicsPrefixPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId shadowVisibilityPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId softwareCausticsPacket"));
}


// Software visibility and caustics contribute their semantic state, lifecycle, and timing bindings to the one
// compiler-owned normal execution.
TEST(EcsGraphics, SoftwareShadowEffectsTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    AString causticsSource;
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_caustics.cpp",
        causticsSource
    ));
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView caustics(causticsSource.data(), causticsSource.size());

    EXPECT_FALSE(ContainsText(system, "softwareShadowEffectsTimingPacketsAreDistinct"));
    EXPECT_TRUE(ContainsText(caustics, ".states = m_causticsComputePersistentState.source(),"));
    EXPECT_TRUE(ContainsText(caustics, ".states = m_causticIrradianceReturnState.source(),"));
    EXPECT_TRUE(ContainsText(
        caustics,
        ".applicableConsumerQueueClass = Core::CommandQueue::Compute,"
    ));
    EXPECT_EQ(
        CountText(caustics, ".setExternalStateSources(scratchStateSources, scratchStateSourceCount)"),
        6u
    );
    EXPECT_EQ(
        CountText(
            caustics,
            ".setExternalStateSources(irradianceReturnStateSources, irradianceReturnStateSourceCount)"
        ),
        1u
    );
    EXPECT_FALSE(ContainsText(system, "deferredStateBindings"));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredSoftwareCausticsTask,\n"
        "            .context = &softwareCausticsStateLifecycle,\n"
        "            .invoke = prepareSoftwareCausticsTask,"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredSoftwareCausticsTask,\n"
        "            .context = &softwareCausticsStateLifecycle,\n"
        "            .invoke = acceptSoftwareCausticsTask,"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "hardwareShadowSupported ? m_deferredHardwareCausticsTask : m_deferredSoftwareCausticsTask,\n"
        "            hardwareShadowSupported ? hardwareCausticsTimingTicket : softwareCausticsTimingTicket"
    ));
    EXPECT_FALSE(ContainsText(system, "s_SoftwareShadowEffectsPacketCount"));
    EXPECT_FALSE(ContainsText(system, "shadowEffectsSubmitter"));
}


// Snapshot Copy and the timed Surfel GI endpoint retain distinct acceptance boundaries. Preparation may alias/share
// Snapshot, while declaration-owned state and lifecycle publication remain semantic-task contracts.
TEST(EcsGraphics, SurfelGiTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    AString surfelGiSource;
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_surfel_gi.cpp", surfelGiSource));
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView surfelGi(surfelGiSource.data(), surfelGiSource.size());

    EXPECT_TRUE(ContainsText(
        system,
        "const bool surfelGiSnapshotCopyAndTimingPacketsAreDistinct =\n"
        "        !m_deferredSurfelGiSnapshotCopyTask.valid()\n"
        "        || !m_deferredLightingCompiledGraph.tasksSharePacket(\n"
        "            m_deferredSurfelGiSnapshotCopyTask,\n"
        "            m_deferredSurfelGiTask"
    ));
    EXPECT_EQ(CountText(system, "surfelGiSnapshotCopyAndTimingPacketsAreDistinct"), 2u);
    EXPECT_TRUE(ContainsText(system, "|| !surfelGiSnapshotCopyAndTimingPacketsAreDistinct"));
    EXPECT_TRUE(ContainsText(surfelGi, ".states = m_surfelGiComputePersistentState.source(),"));
    EXPECT_TRUE(ContainsText(surfelGi, ".states = m_surfelGiCounterPersistentState.source(),"));
    EXPECT_TRUE(ContainsText(surfelGi, ".states = m_surfelIrradianceReturnState.source(),"));
    EXPECT_TRUE(ContainsText(
        surfelGi,
        ".applicableConsumerQueueClass = Core::CommandQueue::Compute,"
    ));
    EXPECT_TRUE(ContainsText(surfelGi, "surfelGiComputeCounterStateSources"));
    EXPECT_TRUE(ContainsText(surfelGi, "surfelGiComputeReturnStateSources"));
    EXPECT_TRUE(ContainsText(surfelGi, "surfelGiAllStateSources"));
    EXPECT_FALSE(ContainsText(system, "deferredStateBindings"));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredSurfelGiTask,\n"
        "        .context = &surfelGiStateLifecycle,\n"
        "        .invoke = prepareSurfelGiTask,"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredSurfelGiTask,\n"
        "        .context = &surfelGiStateLifecycle,\n"
        "        .invoke = acceptSurfelGiTask,"
    ));
    EXPECT_TRUE(ContainsText(system, "appendNormalTimingTicket(m_deferredSurfelGiTask, surfelGiTimingTicket)"));

    const usize surfelLifecycleOffset = system.find("struct SurfelGiStateLifecycleContext{");
    const usize hardwareLifecycleOffset = system.find("struct HardwareCausticsStateLifecycleContext{", surfelLifecycleOffset);
    ASSERT_NE(surfelLifecycleOffset, AStringView::npos);
    ASSERT_NE(hardwareLifecycleOffset, AStringView::npos);
    ASSERT_LT(surfelLifecycleOffset, hardwareLifecycleOffset);
    const AStringView surfelLifecycle = system.substr(
        surfelLifecycleOffset,
        hardwareLifecycleOffset - surfelLifecycleOffset
    );
    EXPECT_FALSE(ContainsText(surfelLifecycle, "runsOnCompute"));
    EXPECT_TRUE(ContainsText(surfelLifecycle, "m_surfelGiComputePersistentState.buildFilteredResourceSubset("));
    EXPECT_TRUE(ContainsText(surfelLifecycle, "m_surfelGiComputePersistentState.commit(*context->computeStateCandidate)"));

    EXPECT_TRUE(ContainsText(
        surfelGi,
        "if(!m_deferredSurfelGiPreparationTask.valid())\n"
        "            m_deferredSurfelGiPreparationTask = m_deferredSurfelGiSnapshotCopyTask;"
    ));
    EXPECT_TRUE(ContainsText(system, "|| !surfelGiPreparedPrefixMergedIntoGiPacket"));
    EXPECT_TRUE(ContainsText(system, "|| !surfelGiInitializationLifecycleMergedIntoPreparationPacket"));
    EXPECT_FALSE(ContainsText(
        system,
        "tasksSharePacket(\n"
        "            m_deferredSurfelGiPreparationTask,\n"
        "            m_deferredSurfelGiSnapshotCopyTask"
    ));
    EXPECT_FALSE(ContainsText(system, "s_SurfelGiMergedPreparationAndCopyPacketCount"));
    EXPECT_FALSE(ContainsText(system, "s_SurfelGiSeparatePreparationAndCopyPacketCount"));
    EXPECT_FALSE(ContainsText(system, "expectedSurfelGiPacketCount"));
    EXPECT_FALSE(ContainsText(system, "surfelGiPacketRange.packetCount =="));
    EXPECT_FALSE(ContainsText(system, "surfelGiPacketRange.packetCount !="));
    EXPECT_TRUE(ContainsText(
        surfelGi,
        "const Core::GpuTaskId dependencies[] = {\n"
        "        m_deferredSurfelGiTask,\n"
        "        m_deferredFrameTimingEndTask,"
    ));
}


// AVBOIT validation follows semantic stage anchors and accepts their inclusive compiler-owned order. It must not
// constrain that order to the currently generated one-packet or five-packet topology.
TEST(EcsGraphics, AvboitTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemHeaderSource;
    AString systemSource;
    AString sharedStageHeaderSource;
    AString avboitSystemHeaderSource;
    AString avboitStageHeaderSource;
    AString avboitStageSource;
    AString avboitValidationSource;
    AString avboitSubmissionSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", systemHeaderSource));
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "task_graph_stage.h", sharedStageHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_system.h", avboitSystemHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "task_graph_stage.h", avboitStageHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "task_graph_stage.cpp", avboitStageSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "task_graph_stage_validation.cpp", avboitValidationSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "task_graph_stage_submission.cpp", avboitSubmissionSource));
    const AStringView systemHeader(systemHeaderSource.data(), systemHeaderSource.size());
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView sharedStageHeader(sharedStageHeaderSource.data(), sharedStageHeaderSource.size());
    const AStringView avboitSystemHeader(avboitSystemHeaderSource.data(), avboitSystemHeaderSource.size());
    const AStringView avboitStageHeader(avboitStageHeaderSource.data(), avboitStageHeaderSource.size());
    const AStringView avboitStage(avboitStageSource.data(), avboitStageSource.size());
    const AStringView avboitValidation(avboitValidationSource.data(), avboitValidationSource.size());
    const AStringView avboitSubmission(avboitSubmissionSource.data(), avboitSubmissionSource.size());

    EXPECT_TRUE(ContainsText(avboitSystemHeader, "RendererAvboitTaskGraphValidation validateTaskGraphStage("));
    EXPECT_TRUE(ContainsText(avboitSystemHeader, "bool appendTaskGraphTimingTickets("));
    EXPECT_TRUE(ContainsText(sharedStageHeader, "bool hasTransparentTasks = false;"));
    EXPECT_TRUE(ContainsText(avboitStageHeader, "struct RendererAvboitTaskGraphValidation"));
    EXPECT_TRUE(ContainsText(avboitStageHeader, "RendererTaskGraphTransparencyStage m_stage;"));
    EXPECT_TRUE(ContainsText(avboitStageHeader, "s_AvboitTaskGraphTimingTicketCapacity = 7u;"));
    EXPECT_FALSE(ContainsText(avboitStageHeader, "m_submissionCompletionTask"));
    EXPECT_TRUE(ContainsText(avboitStage, ".hasTransparentTasks = m_depthWarpTask.valid(),"));
    EXPECT_TRUE(ContainsText(avboitValidation, "taskIsCompiled(taskGraphStage.m_preTask)"));
    EXPECT_TRUE(ContainsText(avboitValidation, "taskIsCompiled(taskGraphStage.m_depthWarpTask)"));
    EXPECT_TRUE(ContainsText(avboitValidation, "taskIsCompiled(taskGraphStage.m_extinctionTask)"));
    EXPECT_TRUE(ContainsText(avboitValidation, "taskIsCompiled(taskGraphStage.m_integrationTask)"));
    EXPECT_TRUE(ContainsText(avboitValidation, "taskIsCompiled(taskGraphStage.m_accumulationTask)"));
    EXPECT_TRUE(ContainsText(avboitValidation, "const bool hasAllTransparentTasks ="));
    EXPECT_TRUE(ContainsText(avboitValidation, "const bool hasAnyTransparentTask ="));
    EXPECT_TRUE(ContainsText(
        avboitValidation,
        "const bool transparentTaskShapeValid = hasTransparentRenderers\n"
        "        ? hasAllTransparentTasks\n"
        "        : !hasAnyTransparentTask"
    ));
    const usize allTransparentTasksOffset = avboitValidation.find("const bool hasAllTransparentTasks =");
    const usize anyTransparentTaskOffset = avboitValidation.find("const bool hasAnyTransparentTask =", allTransparentTasksOffset);
    const usize transparentTaskShapeOffset = avboitValidation.find("const bool transparentTaskShapeValid =", anyTransparentTaskOffset);
    const usize extinctionStreamsOffset = avboitValidation.find(
        "const bool avboitExtinctionPacketContainsStreams =",
        transparentTaskShapeOffset
    );
    ASSERT_NE(allTransparentTasksOffset, AStringView::npos);
    ASSERT_NE(anyTransparentTaskOffset, AStringView::npos);
    ASSERT_NE(transparentTaskShapeOffset, AStringView::npos);
    ASSERT_NE(extinctionStreamsOffset, AStringView::npos);
    ASSERT_LT(allTransparentTasksOffset, anyTransparentTaskOffset);
    ASSERT_LT(anyTransparentTaskOffset, transparentTaskShapeOffset);
    ASSERT_LT(transparentTaskShapeOffset, extinctionStreamsOffset);
    const AStringView allTransparentTasks = avboitValidation.substr(
        allTransparentTasksOffset,
        anyTransparentTaskOffset - allTransparentTasksOffset
    );
    const AStringView anyTransparentTasks = avboitValidation.substr(
        anyTransparentTaskOffset,
        transparentTaskShapeOffset - anyTransparentTaskOffset
    );
    for(const AStringView transparentTask : {
        AStringView("m_depthWarpTask.valid()"),
        AStringView("m_extinctionTask.valid()"),
        AStringView("m_integrationTask.valid()"),
        AStringView("m_accumulationTask.valid()"),
        AStringView("m_accumulationFinalizeTask.valid()"),
    }){
        EXPECT_TRUE(ContainsText(allTransparentTasks, transparentTask));
        EXPECT_TRUE(ContainsText(anyTransparentTasks, transparentTask));
    }
    EXPECT_TRUE(ContainsText(avboitValidation, "&& transparentTaskShapeValid"));
    EXPECT_TRUE(ContainsText(avboitValidation, "&& stage.hasTransparentTasks == hasTransparentRenderers"));
    EXPECT_TRUE(ContainsText(avboitValidation, "const bool depthWarpRunsOnGraphics ="));
    EXPECT_TRUE(ContainsText(avboitValidation, "const bool depthWarpRunsOnCompute ="));
    EXPECT_TRUE(ContainsText(avboitValidation, "const bool integrationRunsOnGraphics ="));
    EXPECT_TRUE(ContainsText(avboitValidation, "const bool integrationRunsOnCompute ="));
    EXPECT_TRUE(ContainsText(avboitValidation, "&& (depthWarpRunsOnGraphics || depthWarpRunsOnCompute)"));
    EXPECT_TRUE(ContainsText(avboitValidation, "&& (integrationRunsOnGraphics || integrationRunsOnCompute)"));
    EXPECT_TRUE(ContainsText(
        avboitValidation,
        "taskBoundaryIsOrdered(taskGraphStage.m_occupancyTask, taskGraphStage.m_depthWarpTask)"
    ));
    EXPECT_TRUE(ContainsText(
        avboitValidation,
        "taskBoundaryIsOrdered(taskGraphStage.m_depthWarpTask, taskGraphStage.m_extinctionTask)"
    ));
    EXPECT_TRUE(ContainsText(
        avboitValidation,
        "taskBoundaryIsOrdered(taskGraphStage.m_extinctionTask, taskGraphStage.m_integrationTask)"
    ));
    EXPECT_TRUE(ContainsText(
        avboitValidation,
        "taskBoundaryIsOrdered(taskGraphStage.m_integrationTask, taskGraphStage.m_accumulationTask)"
    ));
    EXPECT_TRUE(ContainsText(
        avboitValidation,
        "taskBoundaryIsOrdered(taskGraphStage.m_accumulationTask, taskGraphStage.m_accumulationFinalizeTask)"
    ));
    const usize occupancyDepthBoundaryOffset = avboitValidation.find(
        "taskBoundaryIsOrdered(taskGraphStage.m_occupancyTask, taskGraphStage.m_depthWarpTask)"
    );
    const usize depthExtinctionBoundaryOffset = avboitValidation.find(
        "taskBoundaryIsOrdered(taskGraphStage.m_depthWarpTask, taskGraphStage.m_extinctionTask)",
        occupancyDepthBoundaryOffset
    );
    const usize extinctionIntegrationBoundaryOffset = avboitValidation.find(
        "taskBoundaryIsOrdered(taskGraphStage.m_extinctionTask, taskGraphStage.m_integrationTask)",
        depthExtinctionBoundaryOffset
    );
    const usize integrationAccumulationBoundaryOffset = avboitValidation.find(
        "taskBoundaryIsOrdered(taskGraphStage.m_integrationTask, taskGraphStage.m_accumulationTask)",
        extinctionIntegrationBoundaryOffset
    );
    const usize accumulationFinalizerBoundaryOffset = avboitValidation.find(
        "taskBoundaryIsOrdered(taskGraphStage.m_accumulationTask, taskGraphStage.m_accumulationFinalizeTask)",
        integrationAccumulationBoundaryOffset
    );
    ASSERT_NE(occupancyDepthBoundaryOffset, AStringView::npos);
    ASSERT_NE(depthExtinctionBoundaryOffset, AStringView::npos);
    ASSERT_NE(extinctionIntegrationBoundaryOffset, AStringView::npos);
    ASSERT_NE(integrationAccumulationBoundaryOffset, AStringView::npos);
    ASSERT_NE(accumulationFinalizerBoundaryOffset, AStringView::npos);
    EXPECT_LT(occupancyDepthBoundaryOffset, depthExtinctionBoundaryOffset);
    EXPECT_LT(depthExtinctionBoundaryOffset, extinctionIntegrationBoundaryOffset);
    EXPECT_LT(extinctionIntegrationBoundaryOffset, integrationAccumulationBoundaryOffset);
    EXPECT_LT(integrationAccumulationBoundaryOffset, accumulationFinalizerBoundaryOffset);
    EXPECT_TRUE(ContainsText(avboitValidation, "(!depthWarpRunsOnGraphics || ("));
    EXPECT_TRUE(ContainsText(avboitValidation, "(!integrationRunsOnGraphics || ("));
    EXPECT_TRUE(ContainsText(avboitValidation, "&& avboitNaturalStagePlacementValid"));
    EXPECT_FALSE(ContainsText(avboitValidation, "avboitUsesAsyncCompute"));
    EXPECT_TRUE(ContainsText(avboitValidation, "compiledGraph.tasksSharePacket(\n            taskGraphStage.m_preTask"));
    EXPECT_TRUE(ContainsText(
        avboitValidation,
        "compiledGraph.taskPrecedesOrSharesPacket(stage.firstTask, stage.completionTask)"
    ));
    EXPECT_FALSE(ContainsText(avboitValidation, "compiledGraph.packetRangeForTasks("));
    EXPECT_FALSE(ContainsText(avboitValidation, "compiledGraph.validPacketRange("));
    EXPECT_FALSE(ContainsText(avboitValidation, "s_AsyncComputePacketCount"));
    EXPECT_FALSE(ContainsText(avboitValidation, "s_SinglePacketCount"));
    EXPECT_FALSE(ContainsText(avboitValidation, "expectedPacketCount"));
    EXPECT_FALSE(ContainsText(avboitValidation, "packetRange.packetCount =="));
    EXPECT_TRUE(ContainsText(avboitSubmission, "bool RendererAvboitSystem::appendTaskGraphTimingTickets("));
    EXPECT_TRUE(ContainsText(avboitSubmission, "if(!validation.valid() || !bindings || bindingCount > bindingCapacity)"));
    EXPECT_TRUE(ContainsText(avboitSubmission, "requiredBindingCount > s_AvboitTaskGraphTimingTicketCapacity"));
    EXPECT_TRUE(ContainsText(avboitSubmission, "requiredBindingCount > bindingCapacity - bindingCount"));
    EXPECT_FALSE(ContainsText(avboitSubmission, "GpuTaskGraphSubmitter"));
    EXPECT_FALSE(ContainsText(avboitSubmission, "GpuGraphSubmissionTransaction"));
    EXPECT_TRUE(ContainsText(avboitSubmission, "if(validation.stage().hasTransparentTasks){"));
    EXPECT_TRUE(ContainsText(
        avboitSubmission,
        "appendTimingTicket(m_taskGraphStage.m_preTask, timingTickets.m_pre);"
    ));
    EXPECT_TRUE(ContainsText(
        avboitSubmission,
        "appendTimingTicket(m_taskGraphStage.m_depthWarpTask, timingTickets.m_depthWarp);"
    ));
    EXPECT_TRUE(ContainsText(
        avboitSubmission,
        "appendTimingTicket(m_taskGraphStage.m_extinctionTask, timingTickets.m_extinction);"
    ));
    EXPECT_TRUE(ContainsText(
        avboitSubmission,
        "appendTimingTicket(m_taskGraphStage.m_integrationTask, timingTickets.m_integration);"
    ));
    EXPECT_TRUE(ContainsText(
        avboitSubmission,
        "appendTimingTicket(m_taskGraphStage.m_accumulationTask, timingTickets.m_accumulation);"
    ));
    const usize preTimingTicketOffset = avboitSubmission.find(
        "appendTimingTicket(m_taskGraphStage.m_preTask, timingTickets.m_pre);"
    );
    const usize depthWarpTimingTicketOffset = avboitSubmission.find(
        "appendTimingTicket(m_taskGraphStage.m_depthWarpTask, timingTickets.m_depthWarp);",
        preTimingTicketOffset
    );
    const usize extinctionTimingTicketOffset = avboitSubmission.find(
        "appendTimingTicket(m_taskGraphStage.m_extinctionTask, timingTickets.m_extinction);",
        depthWarpTimingTicketOffset
    );
    const usize integrationTimingTicketOffset = avboitSubmission.find(
        "appendTimingTicket(m_taskGraphStage.m_integrationTask, timingTickets.m_integration);",
        extinctionTimingTicketOffset
    );
    const usize accumulationTimingTicketOffset = avboitSubmission.find(
        "appendTimingTicket(m_taskGraphStage.m_accumulationTask, timingTickets.m_accumulation);",
        integrationTimingTicketOffset
    );
    ASSERT_NE(preTimingTicketOffset, AStringView::npos);
    ASSERT_NE(depthWarpTimingTicketOffset, AStringView::npos);
    ASSERT_NE(extinctionTimingTicketOffset, AStringView::npos);
    ASSERT_NE(integrationTimingTicketOffset, AStringView::npos);
    ASSERT_NE(accumulationTimingTicketOffset, AStringView::npos);
    EXPECT_LT(preTimingTicketOffset, depthWarpTimingTicketOffset);
    EXPECT_LT(depthWarpTimingTicketOffset, extinctionTimingTicketOffset);
    EXPECT_LT(extinctionTimingTicketOffset, integrationTimingTicketOffset);
    EXPECT_LT(integrationTimingTicketOffset, accumulationTimingTicketOffset);
    EXPECT_TRUE(ContainsText(avboitSubmission, "timingTickets.m_depthWarp.discard();"));
    EXPECT_TRUE(ContainsText(avboitSubmission, "timingTickets.m_extinction.discard();"));
    EXPECT_TRUE(ContainsText(avboitSubmission, "timingTickets.m_integration.discard();"));
    EXPECT_TRUE(ContainsText(avboitSubmission, "timingTickets.m_accumulation.discard();"));
    EXPECT_TRUE(ContainsText(system, "m_avboitSystem.validateTaskGraphStage("));
    EXPECT_TRUE(ContainsText(system, "m_avboitSystem.appendTaskGraphTimingTickets("));
    EXPECT_TRUE(ContainsText(system, "s_DeferredTimingTicketCapacity = 15u + s_AvboitTaskGraphTimingTicketCapacity;"));
    EXPECT_FALSE(ContainsText(system, "RendererAvboitTaskGraphSubmitContext"));
    EXPECT_FALSE(ContainsText(system, "submitTaskGraphStage("));
    EXPECT_FALSE(ContainsText(systemHeader, "m_deferredAvboit"));
    EXPECT_FALSE(ContainsText(system, "m_deferredAvboit"));
    EXPECT_FALSE(ContainsText(system, "s_AvboitAsyncComputePacketCount"));
    EXPECT_FALSE(ContainsText(sharedStageHeader, "asynchronous"));
    EXPECT_FALSE(ContainsText(avboitStageHeader, "asynchronous"));
    EXPECT_FALSE(ContainsText(avboitStage, "asynchronous"));
    EXPECT_FALSE(ContainsText(avboitValidation, "asynchronous"));
    EXPECT_FALSE(ContainsText(avboitSubmission, "asynchronous"));

    for(const AStringView avboitSource : { avboitValidation, avboitSubmission }){
        EXPECT_FALSE(ContainsText(avboitSource, "GpuSubmissionPacketId avboitPrePacket"));
        EXPECT_FALSE(ContainsText(avboitSource, "GpuSubmissionPacketId avboitDepthWarpPacket"));
        EXPECT_FALSE(ContainsText(avboitSource, "GpuSubmissionPacketId avboitExtinctionPacket"));
        EXPECT_FALSE(ContainsText(avboitSource, "GpuSubmissionPacketId avboitIntegrationPacket"));
        EXPECT_FALSE(ContainsText(avboitSource, "GpuSubmissionPacketId avboitAccumulationPacket"));
    }
}


// Lighting and Composite keep semantic state and timing bindings while the shared normal executor owns generated
// packet coverage.
TEST(EcsGraphics, DeferredLightingCompositeTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    AString deferredSource;
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp",
        deferredSource
    ));
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView deferred(deferredSource.data(), deferredSource.size());

    EXPECT_FALSE(ContainsText(system, "deferredLightingCompositeTimingPacketsAreDistinct"));
    EXPECT_FALSE(ContainsText(system, "deferredStateBindings"));
    EXPECT_FALSE(ContainsText(deferred, "laggedReadsHaveIndependentStateSources"));
    EXPECT_FALSE(ContainsText(deferred, "laggedBindlessSlotsHaveIndependentStateSource"));
    EXPECT_TRUE(ContainsText(system, "appendNormalTimingTicket(m_deferredLightingTask, deferredLightingTimingTicket)"));
    EXPECT_TRUE(ContainsText(system, "appendNormalTimingTicket(m_deferredCompositeTask, deferredCompositeTimingTicket)"));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredLightingTask,\n"
        "        .context = &deferredLightingStateLifecycle,\n"
        "        .invoke = prepareDeferredLightingTask,"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredLightingTask,\n"
        "        .context = &deferredLightingStateLifecycle,\n"
        "        .invoke = acceptDeferredLightingTask,"
    ));
    EXPECT_FALSE(ContainsText(system, "deferredSubmitter"));
    EXPECT_FALSE(ContainsText(system, "s_DeferredLightingCompositePacketCount"));
    EXPECT_FALSE(ContainsText(system, "deferredLightingCompositePacketRange.packetCount =="));
    EXPECT_FALSE(ContainsText(system, "deferredLightingCompositePacketRange.packetCount\n            !="));
}


// Renderer policy addresses semantic tasks only. The compiler-owned presentation endpoint supplies the terminal
// task and queue without exposing generated packet identities.
TEST(EcsGraphics, RendererNormalExecutionUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    AString queueLookupSource;
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph_queue_lookup.h",
        queueLookupSource
    ));
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView queueLookup(queueLookupSource.data(), queueLookupSource.size());

    EXPECT_EQ(CountText(system, "packetForTask("), 0u);
    EXPECT_EQ(CountText(system, "GpuSubmissionPacketId"), 0u);
    EXPECT_EQ(CountText(system, "GpuSubmissionPacketRange"), 0u);
    EXPECT_TRUE(ContainsText(system, "GpuCompiledPresentEndpoint* const presentationEndpoint"));
    EXPECT_TRUE(ContainsText(system, "m_deferredLightingCompiledGraph.presentEndpoint()"));
    EXPECT_FALSE(ContainsText(system, "presentationEndpoint->packet"));
    EXPECT_FALSE(ContainsText(system, "terminalPresentationPacket"));
    EXPECT_TRUE(ContainsText(system, "normalExecution.terminalTask = terminalPresentationTask;"));
    EXPECT_TRUE(ContainsText(system, "presentationEndpoint->producer != m_deferredFrameTimingEndTask"));
    EXPECT_TRUE(ContainsText(system, "presentationEndpoint->queue != primaryGraphicsQueue"));
    EXPECT_TRUE(ContainsText(queueLookup, "return context.graph.queueInfoForTask(*task);"));
    EXPECT_EQ(CountText(queueLookup, "packetForTask("), 0u);
    EXPECT_EQ(CountText(queueLookup, "GpuSubmissionPacketId"), 0u);
    EXPECT_EQ(CountText(system, ".recordAndSubmitNormalGraph("), 1u);
    EXPECT_EQ(CountText(system, ".recordAndSubmitAcceptedFrontierTask("), 1u);
    EXPECT_EQ(CountText(system, ".recordAndSubmitTask("), 2u);
    EXPECT_EQ(CountText(system, ".recordTaskRangeInReadyFrontiers("), 0u);
    EXPECT_EQ(CountText(system, ".submitTaskRangeInCompileOrderFromTasks("), 0u);
    EXPECT_EQ(CountText(system, "taskFinalStateSeed("), 0u);
    EXPECT_TRUE(ContainsText(system, "normalExecution.readyFrontierWorkerPool = &m_world.taskPool();"));
    EXPECT_FALSE(ContainsText(system, "normalExecution.taskStateBindings"));
    EXPECT_FALSE(ContainsText(system, "normalExecution.taskStateBindingCount"));
    EXPECT_FALSE(ContainsText(system, "deferredStateBindings"));
    EXPECT_TRUE(ContainsText(system, "normalExecution.taskRecordedCallbacks = normalRecordedCallbacks;"));
    EXPECT_TRUE(ContainsText(system, "normalExecution.taskRecordedCallbackCount = normalRecordedCallbackCount;"));
    EXPECT_TRUE(ContainsText(system, "normalExecution.taskTimingTickets = normalTimingTickets;"));
    EXPECT_TRUE(ContainsText(system, "normalExecution.taskTimingTicketCount = normalTimingTicketCount;"));
    EXPECT_TRUE(ContainsText(system, "normalExecution.taskAcceptedCallbacks = normalAcceptedCallbacks;"));
    EXPECT_TRUE(ContainsText(system, "normalExecution.taskAcceptedCallbackCount = normalAcceptedCallbackCount;"));
    EXPECT_TRUE(ContainsText(
        system,
        "normalExecution.taskSubmissionHooks = framePresentationSignal.valid()\n"
        "        ? terminalPresentationSubmissionHooks\n"
        "        : nullptr"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "normalExecution.taskSubmissionHookCount = framePresentationSignal.valid()\n"
        "        ? LengthOf(terminalPresentationSubmissionHooks)\n"
        "        : 0u"
    ));
}


// Recovery packets join the accepted native frontier but remain explicitly distinguishable from generic frontier
// finalization in both production graph declaration paths.
TEST(EcsGraphics, ProductionRecoveryTasksDeclareExactSubmissionRole){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString standaloneSource;
    AString deferredSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "module_graph_setup.cpp", standaloneSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp",
        deferredSource
    ));
    const AStringView standalone(standaloneSource.data(), standaloneSource.size());
    const AStringView deferred(deferredSource.data(), deferredSource.size());

    const usize standaloneRecoveryOffset = standalone.find("DeclareStandaloneTaskGraphRecoveryTask");
    const usize standaloneRecoveryDescOffset = standalone.find("GpuTaskDesc recoveryDesc;", standaloneRecoveryOffset);
    ASSERT_NE(standaloneRecoveryOffset, AStringView::npos);
    ASSERT_NE(standaloneRecoveryDescOffset, AStringView::npos);
    const AStringView standaloneScheduling = standalone.substr(
        standaloneRecoveryOffset,
        standaloneRecoveryDescOffset - standaloneRecoveryOffset
    );
    EXPECT_TRUE(ContainsText(standaloneScheduling, "scheduling.joinsAcceptedQueueFrontier = true;"));
    EXPECT_TRUE(ContainsText(standaloneScheduling, "scheduling.isRecoverySubmission = true;"));

    const usize deferredRecoveryOffset = deferred.find("Core::GpuTaskSchedulingHint recoveryScheduling;");
    const usize deferredRecoveryDescOffset = deferred.find("Core::GpuTaskDesc recoveryDesc;", deferredRecoveryOffset);
    ASSERT_NE(deferredRecoveryOffset, AStringView::npos);
    ASSERT_NE(deferredRecoveryDescOffset, AStringView::npos);
    const AStringView deferredScheduling = deferred.substr(
        deferredRecoveryOffset,
        deferredRecoveryDescOffset - deferredRecoveryOffset
    );
    EXPECT_TRUE(ContainsText(deferredScheduling, "recoveryScheduling.joinsAcceptedQueueFrontier = true;"));
    EXPECT_TRUE(ContainsText(deferredScheduling, "recoveryScheduling.isRecoverySubmission = true;"));
}


// The frame timing query must record its published endpoint after the optional presentation contributor. A rejected
// endpoint remains recoverable through the separate non-publishing recovery task instead of silently publishing a
// partial frame duration.
TEST(EcsGraphics, FrameTimingUsesGraphOwnedTerminalPresentationEndpoint){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    AString taskGraphSource;
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "kernel/task_graph_frame_recovery_task.h",
            "kernel/task_graph_frame_recovery_task.cpp",
            "raytrace/task_graph_shadow_prepare_tasks.h",
            "raytrace/task_graph_shadow_prepare_tasks.cpp",
            "mesh/task_graph_prefix_tasks.h",
            "mesh/task_graph_prefix_tasks.cpp",
            "deferred/task_graph_present_task.h",
            "deferred/task_graph_present_task.cpp",
            "kernel/task_graph_frame_timing_end_task.h",
            "kernel/task_graph_frame_timing_end_task.cpp",
            "renderer_frame_pipeline_graph.cpp",
        },
        taskGraphSource
    ));
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    const usize deferredPresentOffset = taskGraph.find("struct DeferredPresentGraphTask");
    const usize frameTimingEndOffset = taskGraph.find("struct FrameTimingEndGraphTask", deferredPresentOffset);
    const usize recoveryOffset = taskGraph.find("struct FrameRecoveryGraphTask");
    const usize shadowPrepareOffset = taskGraph.find("struct ShadowPrepareGraphTask");
    const usize meshViewSetupOffset = taskGraph.find("struct MeshViewSetupGraphTask", shadowPrepareOffset);
    ASSERT_NE(deferredPresentOffset, AStringView::npos);
    ASSERT_NE(frameTimingEndOffset, AStringView::npos);
    ASSERT_NE(recoveryOffset, AStringView::npos);
    ASSERT_NE(shadowPrepareOffset, AStringView::npos);
    ASSERT_NE(meshViewSetupOffset, AStringView::npos);
    ASSERT_LT(deferredPresentOffset, frameTimingEndOffset);
    ASSERT_LT(shadowPrepareOffset, meshViewSetupOffset);

    const AStringView deferredPresent = taskGraph.substr(deferredPresentOffset, frameTimingEndOffset - deferredPresentOffset);
    EXPECT_FALSE(ContainsText(deferredPresent, "frameTimingTransaction"));
    EXPECT_FALSE(ContainsText(deferredPresent, "recordEnd(commandList)"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.frame_timing_end"));
    EXPECT_TRUE(ContainsText(taskGraph, "setDependencies(&frameTimingEndDependency, 1u)"));
    EXPECT_TRUE(ContainsText(taskGraph, "frameTimingTransaction->recordEnd(commandList)"));
    EXPECT_TRUE(ContainsText(taskGraph, "declarePresentEndpoint(Core::GpuPresentEndpoint{"));
    EXPECT_TRUE(ContainsText(taskGraph, ".producer = m_deferredFrameTimingEndTask,"));
    EXPECT_TRUE(ContainsText(taskGraph, ".backBuffer = backbuffer,"));

    const AStringView shadowPrepare = taskGraph.substr(shadowPrepareOffset, meshViewSetupOffset - shadowPrepareOffset);
    EXPECT_TRUE(ContainsText(shadowPrepare, "frameTimingTransaction->begin("));
    const AStringView meshViewSetup = taskGraph.substr(meshViewSetupOffset, deferredPresentOffset - meshViewSetupOffset);
    EXPECT_FALSE(ContainsText(meshViewSetup, "frameTimingTransaction->begin("));

    const AStringView recovery = taskGraph.substr(recoveryOffset, deferredPresentOffset - recoveryOffset);
    EXPECT_TRUE(ContainsText(recovery, "frameTimingTransaction->recordEnd(commandList)"));
    EXPECT_TRUE(ContainsText(recovery, "confirmEndSubmission(token, false)"));
    EXPECT_TRUE(ContainsText(system, "presentationEndpoint->producer"));
    EXPECT_TRUE(ContainsText(system, "presentationEndpoint->queue != primaryGraphicsQueue"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredFrameTimingEndTask)"));

    const usize shadowPrepareAcceptanceOffset = system.find("const auto acceptShadowPrepareTask = [](");
    ASSERT_NE(shadowPrepareAcceptanceOffset, AStringView::npos);
    const usize normalTimingCallbacksOffset = system.find(
        "Core::GpuTaskGraphTaskTimingTicket normalTimingTickets[",
        shadowPrepareAcceptanceOffset
    );
    ASSERT_NE(normalTimingCallbacksOffset, AStringView::npos);
    const AStringView shadowPrepareAcceptance = system.substr(
        shadowPrepareAcceptanceOffset,
        normalTimingCallbacksOffset - shadowPrepareAcceptanceOffset
    );
    EXPECT_TRUE(ContainsText(shadowPrepareAcceptance, "context->frameTimingTransaction->confirmBeginSubmission(token)"));
    EXPECT_TRUE(ContainsText(shadowPrepareAcceptance, ".task = m_deferredShadowPrepareTask,"));
    EXPECT_TRUE(ContainsText(shadowPrepareAcceptance, ".invoke = acceptShadowPrepareTask,"));
    EXPECT_TRUE(ContainsText(system, "frameTimingTransaction.confirmEndSubmission(finalPresentationSubmissionToken, true)"));
    EXPECT_TRUE(ContainsText(system, "surfelCounterReadbackFollowsPresentation"));
    EXPECT_TRUE(ContainsText(system, "laggedLightingHistoryFollowsPresentation"));
    EXPECT_TRUE(ContainsText(taskGraph, "const Core::GpuTaskId historyCopyDependencies[] = { m_deferredFrameTimingEndTask };"));
    EXPECT_FALSE(ContainsText(system, "acceptGraphicsPrefixBeginTask"));
}


// Every normal renderer packet from the packet containing Shadow Preparation through the accepted presentation
// endpoint owns compiler-selected timing. All recorders for that compiled graph retain the shared timing recorder
// because even an untimed late-tail attempt validates the graph-owned plan before opening its native command list.
TEST(EcsGraphics, DeferredGraphConfiguresCompilerOwnedPacketTiming){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString buildSource;
    AString renderSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp",
        buildSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_execute.cpp",
        renderSource
    ));
    const AStringView build(buildSource.data(), buildSource.size());
    const AStringView render(renderSource.data(), renderSource.size());

    const usize buildOffset = build.find("void RendererFramePipeline::buildDeferredLightingTaskGraph");
    ASSERT_NE(buildOffset, AStringView::npos);
    const usize optionsOffset = build.find("Core::GpuTaskGraphCompileOptions compileOptions;", buildOffset);
    ASSERT_NE(optionsOffset, AStringView::npos);
    const usize firstTaskOffset = build.find(
        "compileOptions.packetTimingEnvelope.firstTask = m_deferredShadowPrepareTask;",
        optionsOffset
    );
    ASSERT_NE(firstTaskOffset, AStringView::npos);
    const usize lastTaskOffset = build.find(
        "compileOptions.packetTimingEnvelope.lastTask = m_deferredFrameTimingEndTask;",
        firstTaskOffset
    );
    ASSERT_NE(lastTaskOffset, AStringView::npos);
    const usize compilerOffset = build.find("if(!compiler.compile(", lastTaskOffset);
    ASSERT_NE(compilerOffset, AStringView::npos);
    EXPECT_LT(optionsOffset, firstTaskOffset);
    EXPECT_LT(firstTaskOffset, lastTaskOffset);
    EXPECT_LT(lastTaskOffset, compilerOffset);

    const AStringView compileSetup = build.substr(optionsOffset, compilerOffset - optionsOffset);
    EXPECT_EQ(CountText(compileSetup, "packetTimingEnvelope.firstTask"), 1u);
    EXPECT_EQ(CountText(compileSetup, "packetTimingEnvelope.lastTask"), 1u);
    EXPECT_FALSE(ContainsText(compileSetup, "m_deferredFrameRecoveryTask"));

    const usize metricHelperOffset = build.find("[[nodiscard]] bool PreparePacketEnvelopeMetrics(");
    ASSERT_NE(metricHelperOffset, AStringView::npos);
    const AStringView metricHelper = build.substr(metricHelperOffset, buildOffset - metricHelperOffset);
    EXPECT_TRUE(ContainsText(metricHelper, "compiledGraph.packetTimingEnvelopeRange()"));
    EXPECT_TRUE(ContainsText(metricHelper, "compiledGraph.packetTasks(packetID)"));
    EXPECT_TRUE(ContainsText(metricHelper, "graph.taskAt(packetTasks[0u].index).identity"));
    EXPECT_TRUE(ContainsText(metricHelper, ".physicalQueue = packet.queue,"));
    EXPECT_TRUE(ContainsText(metricHelper, "DeferredGraphQueueInternalIdle(packet.queue, scratchArena)"));
    EXPECT_TRUE(ContainsText(metricHelper, "RendererGpuTimingScope::s_DeferredGraphQueueOverlap.identity"));
    EXPECT_TRUE(ContainsText(metricHelper, "timingRecorder.preparePacketEnvelopeMetrics("));

    const usize metricPrepareOffset = build.find(
        "if(!__hidden_task_graph_deferred_lighting::PreparePacketEnvelopeMetrics(",
        compilerOffset
    );
    const usize recordedGraphResetOffset = build.find("m_deferredLightingRecordedGraph.reset(", compilerOffset);
    ASSERT_NE(metricPrepareOffset, AStringView::npos);
    ASSERT_NE(recordedGraphResetOffset, AStringView::npos);
    EXPECT_LT(compilerOffset, metricPrepareOffset);
    EXPECT_LT(metricPrepareOffset, recordedGraphResetOffset);

    const usize renderFunctionOffset = render.find("void RendererFramePipeline::render(");
    ASSERT_NE(renderFunctionOffset, AStringView::npos);
    const AStringView renderFunction = render.substr(renderFunctionOffset);
    EXPECT_EQ(CountText(renderFunction, "const Core::GpuNativePacketRecorder"), 3u);
    EXPECT_EQ(CountText(
        renderFunction,
        "const Core::GpuNativePacketRecorder deferredRecorder(device, m_graphics.gpuTiming());"
    ), 1u);
    EXPECT_EQ(CountText(
        renderFunction,
        "const Core::GpuNativePacketRecorder recorder(device, m_graphics.gpuTiming());"
    ), 2u);
    EXPECT_FALSE(ContainsText(renderFunction, "GpuNativePacketRecorder deferredRecorder(device);"));
    EXPECT_FALSE(ContainsText(renderFunction, "GpuNativePacketRecorder recorder(device);"));
}


// One typed CPU snapshot binds an acquired WSI image to its exact owning framebuffer. Graphics publishes it only
// after attachment identity validation, clears it on every lifecycle boundary, and never asks mutable backend
// current-image state which framebuffer should render.
TEST(EcsGraphics, PresentationAcquisitionPublishesOneValidatedSnapshot){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString graphicsHeaderSource;
    AString graphicsSource;
    AString backendContractSource;
    AString backendOrchestrationSource;
    AString rendererResourcesSource;
    AString uiSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "module.h", graphicsHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "module.cpp", graphicsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "backend_contract.h", backendContractSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "backend_context_orchestration.cpp", backendOrchestrationSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_resources.cpp", rendererResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    const AStringView graphicsHeader(graphicsHeaderSource.data(), graphicsHeaderSource.size());
    const AStringView graphics(graphicsSource.data(), graphicsSource.size());
    const AStringView backendContract(backendContractSource.data(), backendContractSource.size());
    const AStringView backendOrchestration(backendOrchestrationSource.data(), backendOrchestrationSource.size());
    const AStringView rendererResources(rendererResourcesSource.data(), rendererResourcesSource.size());
    const AStringView ui(uiSource.data(), uiSource.size());

    EXPECT_TRUE(ContainsText(graphicsHeader, "const AcquiredPresentationFrame& acquiredPresentationFrame()const noexcept"));
    EXPECT_TRUE(ContainsText(graphicsHeader, "AcquiredPresentationFrame m_acquiredPresentationFrame;"));
    EXPECT_FALSE(ContainsText(graphicsHeader, "getCurrentBackBuffer"));
    EXPECT_FALSE(ContainsText(graphicsHeader, "getCurrentBackBufferIndex"));
    EXPECT_FALSE(ContainsText(graphicsHeader, "getCurrentFramebuffer"));
    EXPECT_TRUE(ContainsText(backendContract, "{ backend.abandonAcquiredFrame() }->SameAs<bool>;"));

    const usize abandonmentOffset = backendOrchestration.find("bool BackendContext::abandonAcquiredFrame()noexcept{");
    const usize presentDefinitionOffset = backendOrchestration.find("bool BackendContext::present(){", abandonmentOffset);
    ASSERT_NE(abandonmentOffset, AStringView::npos);
    ASSERT_NE(presentDefinitionOffset, AStringView::npos);
    const AStringView abandonment = backendOrchestration.substr(
        abandonmentOffset,
        presentDefinitionOffset - abandonmentOffset
    );
    const usize quarantineOffset = abandonment.find("m_swapChainIndex = Limit<u32>::s_Max;");
    const usize signalIdleOffset = abandonment.find("presentationSignalNeedsIdle && !m_rhiDevice->waitForIdle()");
    const usize signalResetOffset = abandonment.find("resetFramePresentationSignal();", signalIdleOffset);
    const usize forceSubmitOffset = abandonment.find("drainSubmitDesc.forceNativeSubmission = true;");
    const usize drainSubmitOffset = abandonment.find("m_rhiDevice->executeCommandLists(nullptr, 0u, primaryGraphicsQueue, drainSubmitDesc)");
    const usize idleOffset = abandonment.find("m_rhiDevice->waitForIdle()", drainSubmitOffset);
    ASSERT_NE(quarantineOffset, AStringView::npos);
    ASSERT_NE(signalIdleOffset, AStringView::npos);
    ASSERT_NE(signalResetOffset, AStringView::npos);
    ASSERT_NE(forceSubmitOffset, AStringView::npos);
    ASSERT_NE(drainSubmitOffset, AStringView::npos);
    ASSERT_NE(idleOffset, AStringView::npos);
    EXPECT_LT(quarantineOffset, signalIdleOffset);
    EXPECT_LT(signalIdleOffset, signalResetOffset);
    EXPECT_LT(signalResetOffset, forceSubmitOffset);
    EXPECT_LT(forceSubmitOffset, drainSubmitOffset);
    EXPECT_LT(drainSubmitOffset, idleOffset);
    EXPECT_TRUE(ContainsText(abandonment, "if(!m_frameAcquired)\n        return true;"));
    EXPECT_TRUE(ContainsText(abandonment, "if(m_frameAbandonmentComplete){"));
    EXPECT_TRUE(ContainsText(abandonment, "replaceFramePresentationSemaphoreAfterIdle()"));
    EXPECT_TRUE(ContainsText(abandonment, "m_rhiDevice->captureGpuCrash(\"acquired-frame abandonment drain\")"));
    EXPECT_TRUE(ContainsText(abandonment, "m_rhiDevice->captureGpuCrash(\"acquired-frame abandonment drain idle\")"));
    EXPECT_TRUE(ContainsText(abandonment, "m_frameAbandonmentComplete = true;"));
    EXPECT_FALSE(ContainsText(abandonment, "m_frameAcquired = false"));

    const AStringView present = backendOrchestration.substr(presentDefinitionOffset);
    const usize nonConsumedOffset = present.find(
        "presentWaitDisposition != VulkanDetail::QueuePresentWaitDisposition::Consumed"
    );
    const usize deviceLostOffset = present.find(
        "presentWaitDisposition == VulkanDetail::QueuePresentWaitDisposition::DeviceLost",
        nonConsumedOffset
    );
    const usize unconsumedIdleOffset = present.find("if(!m_rhiDevice->waitForIdle())", deviceLostOffset);
    const usize idleFailureCaptureOffset = present.find("captureGpuCrash(\"present semaphore idle\")", unconsumedIdleOffset);
    const usize replacementOffset = present.find("if(!replaceFramePresentationSemaphoreAfterIdle())", idleFailureCaptureOffset);
    const usize replacementFailureCaptureOffset = present.find(
        "captureGpuCrash(\"present semaphore replacement\")",
        replacementOffset
    );
    const usize unconsumedResetOffset = present.find("resetFramePresentationSignal();", replacementFailureCaptureOffset);
    ASSERT_NE(nonConsumedOffset, AStringView::npos);
    ASSERT_NE(deviceLostOffset, AStringView::npos);
    ASSERT_NE(unconsumedIdleOffset, AStringView::npos);
    ASSERT_NE(idleFailureCaptureOffset, AStringView::npos);
    ASSERT_NE(replacementOffset, AStringView::npos);
    ASSERT_NE(replacementFailureCaptureOffset, AStringView::npos);
    ASSERT_NE(unconsumedResetOffset, AStringView::npos);
    EXPECT_LT(nonConsumedOffset, deviceLostOffset);
    EXPECT_LT(deviceLostOffset, unconsumedIdleOffset);
    EXPECT_LT(unconsumedIdleOffset, idleFailureCaptureOffset);
    EXPECT_LT(idleFailureCaptureOffset, replacementOffset);
    EXPECT_LT(replacementOffset, replacementFailureCaptureOffset);
    EXPECT_LT(replacementFailureCaptureOffset, unconsumedResetOffset);
    EXPECT_TRUE(ContainsText(present, "if(!frameSignalAccepted || !m_rhiDevice){"));
    EXPECT_FALSE(ContainsText(
        present.substr(nonConsumedOffset, replacementFailureCaptureOffset - nonConsumedOffset),
        "resetFramePresentationSignal();"
    ));

    const usize renderOffset = graphics.find("void Graphics::render(){");
    const usize averageOffset = graphics.find("void Graphics::updateAverageFrameTime", renderOffset);
    ASSERT_NE(renderOffset, AStringView::npos);
    ASSERT_NE(averageOffset, AStringView::npos);
    const AStringView render = graphics.substr(renderOffset, averageOffset - renderOffset);
    EXPECT_TRUE(ContainsText(render, "Framebuffer* const framebuffer = m_acquiredPresentationFrame.framebuffer.get();"));
    EXPECT_FALSE(ContainsText(render, "getCurrent"));

    const usize animateOffset = graphics.find("bool Graphics::animateRenderPresentInternal");
    ASSERT_NE(animateOffset, AStringView::npos);
    const AStringView animate = graphics.substr(animateOffset);
    const usize entryClearOffset = animate.find("m_acquiredPresentationFrame = {};");
    const usize acquireOffset = animate.find("AcquiredBackBuffer acquiredBackBuffer = m_backend->beginFrame(resizeCallbacks);");
    const usize identityOffset = animate.find("acquiredFramebufferDesc.colorAttachments[0].texture != acquiredBackBuffer.texture.get()");
    const usize publishOffset = animate.find("m_acquiredPresentationFrame = {", acquireOffset);
    const usize resetOffset = animate.find("ScopedAcquiredPresentationFrameReset acquiredFrameReset", publishOffset);
    const usize preambleOffset = animate.find("prepareFramePreamble()", resetOffset);
    ASSERT_NE(entryClearOffset, AStringView::npos);
    ASSERT_NE(acquireOffset, AStringView::npos);
    ASSERT_NE(identityOffset, AStringView::npos);
    ASSERT_NE(publishOffset, AStringView::npos);
    ASSERT_NE(resetOffset, AStringView::npos);
    ASSERT_NE(preambleOffset, AStringView::npos);
    EXPECT_LT(entryClearOffset, acquireOffset);
    EXPECT_LT(acquireOffset, identityOffset);
    EXPECT_LT(identityOffset, publishOffset);
    EXPECT_LT(publishOffset, resetOffset);
    EXPECT_LT(resetOffset, preambleOffset);
    EXPECT_TRUE(ContainsText(animate, "acquiredFramebufferDesc.colorAttachments.size() != 1u"));
    const usize missingFramebufferWarningOffset = animate.find("acquired swap-chain image has no matching framebuffer");
    const usize missingFramebufferAbandonOffset = animate.find("m_backend->abandonAcquiredFrame()", missingFramebufferWarningOffset);
    const usize missingFramebufferRecreationOffset = animate.find("requestDeviceRecreation()", missingFramebufferAbandonOffset);
    const usize attachmentWarningOffset = animate.find("acquired swap-chain image mismatches its framebuffer attachment");
    const usize attachmentAbandonOffset = animate.find("m_backend->abandonAcquiredFrame()", attachmentWarningOffset);
    const usize attachmentRecreationOffset = animate.find("requestDeviceRecreation()", attachmentAbandonOffset);
    ASSERT_NE(missingFramebufferWarningOffset, AStringView::npos);
    ASSERT_NE(missingFramebufferAbandonOffset, AStringView::npos);
    ASSERT_NE(missingFramebufferRecreationOffset, AStringView::npos);
    ASSERT_NE(attachmentWarningOffset, AStringView::npos);
    ASSERT_NE(attachmentAbandonOffset, AStringView::npos);
    ASSERT_NE(attachmentRecreationOffset, AStringView::npos);
    EXPECT_LT(missingFramebufferWarningOffset, missingFramebufferAbandonOffset);
    EXPECT_LT(missingFramebufferAbandonOffset, missingFramebufferRecreationOffset);
    EXPECT_LT(missingFramebufferRecreationOffset, attachmentWarningOffset);
    EXPECT_LT(attachmentWarningOffset, attachmentAbandonOffset);
    EXPECT_LT(attachmentAbandonOffset, attachmentRecreationOffset);
    EXPECT_LT(attachmentRecreationOffset, publishOffset);
    const usize renderCallOffset = animate.find("render();", preambleOffset);
    const usize postRenderExitOffset = animate.find("if(m_deviceRecreationRequested || device.isDeviceLost()){", renderCallOffset);
    const usize postRenderAbandonOffset = animate.find("else if(!m_backend->abandonAcquiredFrame())", postRenderExitOffset);
    const usize presentCallOffset = animate.find("const bool presented = m_backend->present();", postRenderAbandonOffset);
    const usize presentFailureOffset = animate.find("if(!presented){", presentCallOffset);
    const usize presentAbandonOffset = animate.find("!device.isDeviceLost() && !m_backend->abandonAcquiredFrame()", presentFailureOffset);
    ASSERT_NE(renderCallOffset, AStringView::npos);
    ASSERT_NE(postRenderExitOffset, AStringView::npos);
    ASSERT_NE(postRenderAbandonOffset, AStringView::npos);
    ASSERT_NE(presentCallOffset, AStringView::npos);
    ASSERT_NE(presentFailureOffset, AStringView::npos);
    ASSERT_NE(presentAbandonOffset, AStringView::npos);
    EXPECT_LT(renderCallOffset, postRenderExitOffset);
    EXPECT_LT(postRenderExitOffset, postRenderAbandonOffset);
    EXPECT_LT(postRenderAbandonOffset, presentCallOffset);
    EXPECT_LT(presentCallOffset, presentFailureOffset);
    EXPECT_LT(presentFailureOffset, presentAbandonOffset);
    EXPECT_EQ(CountText(animate, "m_backend->abandonAcquiredFrame()"), 4u);
    EXPECT_TRUE(ContainsText(animate, "prepareFramePreamble() returns false only after terminal device loss"));
    EXPECT_TRUE(ContainsText(animate, "required device teardown owns the unresolved acquired image and synchronization"));
    EXPECT_TRUE(ContainsText(graphics, "~ScopedAcquiredPresentationFrameReset(){ m_frame = {}; }"));
    EXPECT_TRUE(ContainsText(graphics, "bool Graphics::init(const Common::FrameData& data){\n    m_acquiredPresentationFrame = {};"));
    EXPECT_TRUE(ContainsText(graphics, "bool Graphics::createHeadlessDevice(){\n    m_acquiredPresentationFrame = {};"));
    EXPECT_TRUE(ContainsText(graphics, "void Graphics::destroy(){\n    m_acquiredPresentationFrame = {};"));
    EXPECT_TRUE(ContainsText(graphics, "void Graphics::backBufferResizing(){\n    m_acquiredPresentationFrame = {};"));

    for(const AStringView setupSource : { rendererResources, ui }){
        EXPECT_TRUE(ContainsText(setupSource, "Pipeline compatibility setup uses the stable framebuffer-zero prototype"));
        EXPECT_TRUE(ContainsText(setupSource, "m_graphics.getFramebuffer(0u)"));
        EXPECT_FALSE(ContainsText(setupSource, "getCurrentFramebuffer"));
    }
}


// A compatibility present is still a real native transition submission. It must stay entirely behind the missing
// graph-signal branch, target the exact acquired WSI texture on the primary Graphics transport, and fail closed
// before vkQueuePresentKHR whenever recording, signal claiming, or submission cannot prove that transition.
TEST(EcsGraphics, CompatibilityPresentTransitionsExactAcquiredImageBeforeSignal){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString backendOrchestrationSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "backend_context_orchestration.cpp",
        backendOrchestrationSource
    ));
    const AStringView backendOrchestration(backendOrchestrationSource.data(), backendOrchestrationSource.size());

    const usize presentOffset = backendOrchestration.find("bool BackendContext::present(){");
    ASSERT_NE(presentOffset, AStringView::npos);
    const AStringView present = backendOrchestration.substr(presentOffset);
    const usize compatibilityBranchOffset = present.find("if(!frameSignalAccepted){");
    const usize presentInfoOffset = present.find("VkPresentInfoKHR presentInfo = {};");
    ASSERT_NE(compatibilityBranchOffset, AStringView::npos);
    ASSERT_NE(presentInfoOffset, AStringView::npos);
    ASSERT_LT(compatibilityBranchOffset, presentInfoOffset);

    const AStringView acceptedGraphSignalPath = present.substr(0u, compatibilityBranchOffset);
    const AStringView compatibilityBranch = present.substr(
        compatibilityBranchOffset,
        presentInfoOffset - compatibilityBranchOffset
    );
    EXPECT_TRUE(ContainsText(present, "if(!m_rhiDevice || !m_frameAcquired || !m_swapChain"));
    EXPECT_TRUE(ContainsText(
        acceptedGraphSignalPath,
        "m_framePresentationSignalState == FramePresentationSignalState::Accepted"
    ));
    EXPECT_FALSE(ContainsText(acceptedGraphSignalPath, "ResolveCompatibilityPresentTransitionPolicy"));
    EXPECT_FALSE(ContainsText(acceptedGraphSignalPath, "createCommandList"));
    EXPECT_FALSE(ContainsText(acceptedGraphSignalPath, "setTextureState"));
    EXPECT_EQ(CountText(present, "ResolveCompatibilityPresentTransitionPolicy"), 1u);
    EXPECT_EQ(CountText(present, "createCommandList"), 1u);
    EXPECT_EQ(CountText(present, "setTextureState"), 1u);

    const usize acquiredImageOffset = compatibilityBranch.find(
        "SwapChainImage& swapChainImage = m_swapChainImages[m_swapChainIndex];"
    );
    const usize policyOffset = compatibilityBranch.find("ResolveCompatibilityPresentTransitionPolicy(");
    const usize primaryQueueOffset = compatibilityBranch.find(
        "m_rhiDevice->getPrimaryPhysicalQueue(CommandQueue::Graphics)"
    );
    const usize exactQueueOffset = compatibilityBranch.find("commandListParams.setPhysicalQueue(primaryGraphicsQueue);");
    const usize createOffset = compatibilityBranch.find("m_rhiDevice->createCommandList(commandListParams)");
    const usize openOffset = compatibilityBranch.find("compatibilityCommandList->open();");
    const usize seedOffset = compatibilityBranch.find("compatibilityCommandList->beginTrackingTextureState(");
    const usize transitionOffset = compatibilityBranch.find("compatibilityCommandList->setTextureState(", seedOffset);
    const usize commitOffset = compatibilityBranch.find("compatibilityCommandList->commitBarriers();", transitionOffset);
    const usize closeOffset = compatibilityBranch.find("compatibilityCommandList->close();", commitOffset);
    const usize claimOffset = compatibilityBranch.find("claimFramePresentationSignal();", closeOffset);
    const usize hookOffset = compatibilityBranch.find("submitDesc.setPreSubmitHook(presentationSignalHook);", claimOffset);
    const usize listOffset = compatibilityBranch.find(
        "CommandList* const compatibilityCommandLists[] = { compatibilityCommandList.get() };",
        hookOffset
    );
    const usize executeOffset = compatibilityBranch.find("m_rhiDevice->executeCommandLists(", listOffset);
    const usize confirmOffset = compatibilityBranch.find("confirmFramePresentationSignal(fallbackToken)", executeOffset);
    const usize acceptedOffset = compatibilityBranch.find("frameSignalAccepted = true;", confirmOffset);
    ASSERT_NE(acquiredImageOffset, AStringView::npos);
    ASSERT_NE(policyOffset, AStringView::npos);
    ASSERT_NE(primaryQueueOffset, AStringView::npos);
    ASSERT_NE(exactQueueOffset, AStringView::npos);
    ASSERT_NE(createOffset, AStringView::npos);
    ASSERT_NE(openOffset, AStringView::npos);
    ASSERT_NE(seedOffset, AStringView::npos);
    ASSERT_NE(transitionOffset, AStringView::npos);
    ASSERT_NE(commitOffset, AStringView::npos);
    ASSERT_NE(closeOffset, AStringView::npos);
    ASSERT_NE(claimOffset, AStringView::npos);
    ASSERT_NE(hookOffset, AStringView::npos);
    ASSERT_NE(listOffset, AStringView::npos);
    ASSERT_NE(executeOffset, AStringView::npos);
    ASSERT_NE(confirmOffset, AStringView::npos);
    ASSERT_NE(acceptedOffset, AStringView::npos);
    EXPECT_LT(acquiredImageOffset, policyOffset);
    EXPECT_LT(policyOffset, primaryQueueOffset);
    EXPECT_LT(primaryQueueOffset, exactQueueOffset);
    EXPECT_LT(exactQueueOffset, createOffset);
    EXPECT_LT(createOffset, openOffset);
    EXPECT_LT(openOffset, seedOffset);
    EXPECT_LT(seedOffset, transitionOffset);
    EXPECT_LT(transitionOffset, commitOffset);
    EXPECT_LT(commitOffset, closeOffset);
    EXPECT_LT(closeOffset, claimOffset);
    EXPECT_LT(claimOffset, hookOffset);
    EXPECT_LT(hookOffset, listOffset);
    EXPECT_LT(listOffset, executeOffset);
    EXPECT_LT(executeOffset, confirmOffset);
    EXPECT_LT(confirmOffset, acceptedOffset);

    EXPECT_TRUE(ContainsText(
        compatibilityBranch,
        "swapChainImage.presentationState.nativeInitialState()"
    ));
    EXPECT_TRUE(ContainsText(
        compatibilityBranch,
        "transitionPolicy == VulkanDetail::CompatibilityPresentTransitionPolicy::PreservePresent"
    ));
    EXPECT_EQ(CountText(compatibilityBranch, "swapChainImage.rhiHandle.get()"), 2u);
    EXPECT_EQ(CountText(compatibilityBranch, "s_AllSubresources"), 2u);
    EXPECT_TRUE(ContainsText(
        compatibilityBranch,
        "!swapChainImage.rhiHandle\n"
        "            || transitionPolicy == VulkanDetail::CompatibilityPresentTransitionPolicy::Invalid\n"
        "            || !primaryGraphicsQueue.valid()"
    ));
    EXPECT_TRUE(ContainsText(compatibilityBranch, "if(!compatibilityCommandList){"));
    EXPECT_TRUE(ContainsText(
        compatibilityBranch,
        "!compatibilityCommandList->hasCommandBuffer()\n"
        "            || !compatibilityCommandList->isRecording()\n"
        "            || compatibilityCommandList->commandRecordingFailed()"
    ));
    EXPECT_TRUE(ContainsText(
        compatibilityBranch,
        "!compatibilityCommandList->hasCommandBuffer()\n"
        "            || compatibilityCommandList->isRecording()\n"
        "            || compatibilityCommandList->commandRecordingFailed()"
    ));
    EXPECT_TRUE(ContainsText(compatibilityBranch, "if(!presentationSignalHook.valid()){"));
    EXPECT_TRUE(ContainsText(
        compatibilityBranch,
        "compatibilityCommandLists,\n"
        "            LengthOf(compatibilityCommandLists),\n"
        "            primaryGraphicsQueue,\n"
        "            submitDesc"
    ));
    EXPECT_FALSE(ContainsText(compatibilityBranch, "executeCommandLists(nullptr, 0u"));
    EXPECT_TRUE(ContainsText(
        compatibilityBranch,
        "if(!fallbackToken.valid()){\n"
        "            cancelFramePresentationSignal();\n"
        "            NWB_LOGGER_ERROR(NWB_TEXT(\"Vulkan: Compatibility presentation transition/signal "
        "submission was rejected.\"));\n"
        "            return false;\n"
        "        }"
    ));
    EXPECT_TRUE(ContainsText(
        compatibilityBranch,
        "if(!confirmFramePresentationSignal(fallbackToken)){\n"
        "            cancelFramePresentationSignal();\n"
        "            NWB_LOGGER_ERROR(NWB_TEXT(\"Vulkan: Accepted compatibility presentation submission "
        "failed signal confirmation/tracking.\"));\n"
        "            return false;\n"
        "        }"
    ));
    EXPECT_EQ(CountText(compatibilityBranch, "return false;"), 8u);
    EXPECT_EQ(CountText(compatibilityBranch, "cancelFramePresentationSignal();"), 5u);
}


// Renderer presentation must import the exact acquired swap-chain texture, preserve its captured native origin,
// and own the RenderTarget-to-Present state closure. The late record callback revalidates both graph identity and
// framebuffer attachment identity before touching the image.
TEST(EcsGraphics, RendererPresentationGraphBindsExactAcquiredTexture){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString contributorHeaderSource;
    AString rendererHeaderSource;
    AString rendererResourcesSource;
    AString rendererSource;
    AString presentationBuildSource;
    AString presentationTaskHeaderSource;
    AString presentationTaskSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "task_graph" / "presentation_contributor.h",
        contributorHeaderSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", rendererHeaderSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_resources.cpp",
        rendererResourcesSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_execute.cpp", rendererSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp",
        presentationBuildSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "deferred" / "task_graph_present_task.h",
        presentationTaskHeaderSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "deferred" / "task_graph_present_task.cpp",
        presentationTaskSource
    ));
    const AStringView contributorHeader(contributorHeaderSource.data(), contributorHeaderSource.size());
    const AStringView rendererHeader(rendererHeaderSource.data(), rendererHeaderSource.size());
    const AStringView rendererResources(rendererResourcesSource.data(), rendererResourcesSource.size());
    const AStringView renderer(rendererSource.data(), rendererSource.size());
    const AStringView presentationBuild(presentationBuildSource.data(), presentationBuildSource.size());
    const AStringView presentationTaskHeader(presentationTaskHeaderSource.data(), presentationTaskHeaderSource.size());
    const AStringView presentationTask(presentationTaskSource.data(), presentationTaskSource.size());

    EXPECT_TRUE(ContainsText(
        contributorHeader,
        "prepareTaskGraphPresentation(const AcquiredPresentationFrame& frame)"
    ));
    EXPECT_TRUE(ContainsText(
        contributorHeader,
        "const AcquiredPresentationFrame& frame,\n"
        "        GpuGraphResourceId backbuffer,"
    ));
    EXPECT_TRUE(ContainsText(contributorHeader, "exact typed acquired texture imported by"));
    EXPECT_FALSE(ContainsText(contributorHeader, "presentation hazard domain"));
    EXPECT_TRUE(ContainsText(rendererHeader, "const Core::AcquiredPresentationFrame& presentationFrame,"));
    EXPECT_TRUE(ContainsText(
        rendererResources,
        "else if(m_graphics.isDeviceRecreationRequested()){\n"
        "            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT(\"RendererSystem: presentation contributor requested recreation during preparation\"));\n"
        "            return false;"
    ));

    EXPECT_FALSE(ContainsText(
        presentationBuild,
        "HazardDomainDesc(Name(\"render.deferred_present.backbuffer\")"
    ));
    EXPECT_TRUE(ContainsText(
        presentationBuild,
        ".setInitialState(presentationFrame.backBuffer.nativeInitialState)\n"
        "        .setExternalFinalState(Core::ResourceStates::Present)"
    ));
    EXPECT_TRUE(ContainsText(
        presentationBuild,
        "const Core::GpuGraphResourceId backbuffer = m_deferredLightingTaskGraph.importTexture(\n"
        "        presentationFrame.backBuffer.texture,"
    ));
    EXPECT_TRUE(ContainsText(
        presentationBuild,
        "WriteTextureUse(\n"
        "            backbuffer,\n"
        "            presentationFramebufferDesc.colorAttachments[0].subresources,\n"
        "            Core::ResourceStates::RenderTarget"
    ));
    EXPECT_TRUE(ContainsText(presentationBuild, ".presentationFrame = presentationFrame,"));
    EXPECT_TRUE(ContainsText(presentationBuild, ".backBuffer = backbuffer,"));
    EXPECT_TRUE(ContainsText(
        presentationBuild,
        "declarePresentEndpoint(Core::GpuPresentEndpoint{\n"
        "        .producer = m_deferredFrameTimingEndTask,\n"
        "        .backBuffer = backbuffer,"
    ));

    EXPECT_TRUE(ContainsText(presentationTaskHeader, "Core::AcquiredPresentationFrame presentationFrame;"));
    EXPECT_TRUE(ContainsText(presentationTaskHeader, "Core::GpuGraphResourceId backBuffer;"));
    EXPECT_TRUE(ContainsText(presentationTask, "!payload.presentationFrame.valid()"));
    EXPECT_TRUE(ContainsText(
        presentationTask,
        "presentationFramebufferDesc.colorAttachments[0].texture != payload.presentationFrame.backBuffer.texture.get()"
    ));
    EXPECT_TRUE(ContainsText(
        presentationTask,
        "context.taskGraph.textureForResource(payload.backBuffer) != payload.presentationFrame.backBuffer.texture.get()"
    ));
    EXPECT_TRUE(ContainsText(
        presentationTask,
        "payload.deferredSystem->renderDeferredPresent(\n"
        "        commandList,\n"
        "        *payload.targets,\n"
        "        payload.presentationFrame"
    ));

    EXPECT_TRUE(ContainsText(
        renderer,
        "const Core::AcquiredPresentationFrame presentationFrame = m_graphics.acquiredPresentationFrame();"
    ));
    EXPECT_TRUE(ContainsText(renderer, "presentationFrame.framebuffer.get() != framebuffer"));
    EXPECT_TRUE(ContainsText(
        renderer,
        "presentationFramebufferDesc.colorAttachments[0].texture != presentationFrame.backBuffer.texture.get()"
    ));

    // Once the exact back-buffer writer accepted, generic suffix recovery cannot make the acquired image reusable.
    // The renderer must escalate that partial-acceptance edge to Graphics' recreation/abandonment path.
    const usize partialAcceptanceOffset = renderer.find(
        "const Core::QueueSubmissionToken deferredPresentSubmissionToken ="
    );
    const usize acceptedWriterOffset = renderer.find("m_deferredPresentTask", partialAcceptanceOffset);
    const usize recreationOffset = renderer.find(
        "if(deferredPresentSubmissionToken.valid() && !finalPresentationSubmissionToken.valid()){",
        acceptedWriterOffset
    );
    const usize recoveryFailureOffset = renderer.find("failFrameRenderRecovery();", recreationOffset);
    ASSERT_NE(partialAcceptanceOffset, AStringView::npos);
    ASSERT_NE(acceptedWriterOffset, AStringView::npos);
    ASSERT_NE(recreationOffset, AStringView::npos);
    ASSERT_NE(recoveryFailureOffset, AStringView::npos);
    EXPECT_LT(partialAcceptanceOffset, acceptedWriterOffset);
    EXPECT_LT(acceptedWriterOffset, recreationOffset);
    EXPECT_LT(recreationOffset, recoveryFailureOffset);
    EXPECT_TRUE(ContainsText(
        renderer.substr(recreationOffset, recoveryFailureOffset - recreationOffset),
        "acquired back buffer was written before presentation suffix rejection; requesting recreation"
    ));
}


// Every UI presentation route consumes the same acquired-frame identity. Normal overlay work reuses the renderer's
// typed resource, standalone raster paths import that exact TextureHandle, and upload-only standalone work skips the
// external-final texture import entirely so it cannot manufacture an unwritten presentation endpoint.
TEST(EcsGraphics, UiPresentationGraphsBindExactAcquiredTexture){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString uiHeaderSource;
    AString uiSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.h", uiHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    const AStringView uiHeader(uiHeaderSource.data(), uiHeaderSource.size());
    const AStringView ui(uiSource.data(), uiSource.size());

    EXPECT_TRUE(ContainsText(
        uiHeader,
        "virtual bool prepareTaskGraphPresentation(const Core::AcquiredPresentationFrame& frame)override;"
    ));
    EXPECT_TRUE(ContainsText(
        uiHeader,
        "const Core::AcquiredPresentationFrame& frame,\n"
        "        Core::GpuGraphResourceId backbuffer,"
    ));
    EXPECT_TRUE(ContainsText(uiHeader, "Core::AcquiredPresentationFrame m_taskGraphPresentationFrame;"));
    EXPECT_TRUE(ContainsText(
        ui,
        "framebufferDesc.colorAttachments[0].texture == frame.backBuffer.texture.get()"
    ));
    EXPECT_TRUE(ContainsText(
        ui,
        "graph.textureForResource(backbuffer) == frame.backBuffer.texture.get()"
    ));
    EXPECT_TRUE(ContainsText(ui, "PresentationBackBufferResourceDesc("));
    EXPECT_TRUE(ContainsText(ui, ".setType(Core::GpuGraphResourceType::Texture)"));
    EXPECT_TRUE(ContainsText(ui, ".setInitialState(frame.backBuffer.nativeInitialState)"));
    EXPECT_TRUE(ContainsText(ui, ".setExternalFinalState(Core::ResourceStates::Present)"));

    const usize renderTaskOffset = ui.find("struct UiSystem::TaskGraphRenderTask{");
    const usize uploadCompletionOffset = ui.find("struct UiSystem::TaskGraphUploadCompletionTask{", renderTaskOffset);
    const usize legacyTaskOffset = ui.find("struct UiSystem::StandaloneLegacyPresentationTask{");
    const usize nextTaskOffset = ui.find("UiSystem::UiSystem(", legacyTaskOffset);
    ASSERT_NE(renderTaskOffset, AStringView::npos);
    ASSERT_NE(uploadCompletionOffset, AStringView::npos);
    ASSERT_NE(legacyTaskOffset, AStringView::npos);
    ASSERT_NE(nextTaskOffset, AStringView::npos);
    const AStringView renderTask = ui.substr(renderTaskOffset, uploadCompletionOffset - renderTaskOffset);
    const AStringView legacyTask = ui.substr(legacyTaskOffset, nextTaskOffset - legacyTaskOffset);
    for(const AStringView task : { renderTask, legacyTask }){
        EXPECT_TRUE(ContainsText(task, "Core::AcquiredPresentationFrame frame;"));
        EXPECT_TRUE(ContainsText(task, "Core::GpuGraphResourceId backbuffer;"));
    }
    EXPECT_TRUE(ContainsText(renderTask, "payload.backbuffer,\n            context"));
    EXPECT_TRUE(ContainsText(legacyTask, "payload.backbuffer,"));
    EXPECT_TRUE(ContainsText(legacyTask, "context"));

    const usize declarationOffset = ui.find("Core::GpuTaskId UiSystem::declareTaskGraphPresentation");
    const usize standaloneOffset = ui.find("bool UiSystem::submitStandaloneTaskGraphPresentation", declarationOffset);
    const usize legacyDeclarationOffset = ui.find(
        "Core::GpuTaskId UiSystem::declareStandaloneLegacyTaskGraphPresentation",
        standaloneOffset
    );
    const usize legacySubmitOffset = ui.find(
        "bool UiSystem::submitStandaloneLegacyTaskGraphPresentation",
        legacyDeclarationOffset
    );
    const usize uploadGraphOffset = ui.find(
        "Core::GpuTaskId UiSystem::declareStandaloneTextureUploadGraph",
        legacySubmitOffset
    );
    ASSERT_NE(declarationOffset, AStringView::npos);
    ASSERT_NE(standaloneOffset, AStringView::npos);
    ASSERT_NE(legacyDeclarationOffset, AStringView::npos);
    ASSERT_NE(legacySubmitOffset, AStringView::npos);
    ASSERT_NE(uploadGraphOffset, AStringView::npos);
    const AStringView declaration = ui.substr(declarationOffset, standaloneOffset - declarationOffset);
    const AStringView standalone = ui.substr(standaloneOffset, legacyDeclarationOffset - standaloneOffset);
    const AStringView legacyDeclaration = ui.substr(
        legacyDeclarationOffset,
        legacySubmitOffset - legacyDeclarationOffset
    );
    const AStringView legacySubmit = ui.substr(legacySubmitOffset, uploadGraphOffset - legacySubmitOffset);

    EXPECT_TRUE(ContainsText(
        declaration,
        "GraphBindsAcquiredPresentationTexture(graph, frame, backbuffer)"
    ));
    EXPECT_TRUE(ContainsText(
        declaration,
        ".resource = backbuffer,\n"
        "            .range = {},\n"
        "            // Rasterization writes the exact renderer-owned presentation texture."
    ));
    EXPECT_TRUE(ContainsText(declaration, ".requiredState = Core::ResourceStates::RenderTarget,"));
    EXPECT_TRUE(ContainsText(declaration, ".frame = frame,"));
    EXPECT_TRUE(ContainsText(declaration, ".backbuffer = backbuffer,"));

    const usize drawBranchOffset = standalone.find("context->ui->m_taskGraphDrawUploadsPrepared");
    const usize standaloneImportOffset = standalone.find("backbuffer = graph.importTexture(", drawBranchOffset);
    const usize standaloneDeclareOffset = standalone.find(
        "return context->ui->declareTaskGraphPresentation(",
        standaloneImportOffset
    );
    ASSERT_NE(drawBranchOffset, AStringView::npos);
    ASSERT_NE(standaloneImportOffset, AStringView::npos);
    ASSERT_NE(standaloneDeclareOffset, AStringView::npos);
    EXPECT_LT(drawBranchOffset, standaloneImportOffset);
    EXPECT_LT(standaloneImportOffset, standaloneDeclareOffset);
    EXPECT_TRUE(ContainsText(standalone, "Core::GpuGraphResourceId backbuffer;"));
    EXPECT_TRUE(ContainsText(standalone, "context->frame.backBuffer.texture,"));
    EXPECT_TRUE(ContainsText(standalone, "PresentationBackBufferResourceDesc("));
    EXPECT_FALSE(ContainsText(standalone, "importHazardDomain"));

    EXPECT_TRUE(ContainsText(
        legacyDeclaration,
        "const Core::GpuGraphResourceId backbuffer = graph.importTexture(\n"
        "        frame.backBuffer.texture,"
    ));
    EXPECT_TRUE(ContainsText(legacyDeclaration, "PresentationBackBufferResourceDesc("));
    EXPECT_TRUE(ContainsText(legacyDeclaration, ".requiredState = Core::ResourceStates::RenderTarget,"));
    EXPECT_TRUE(ContainsText(
        legacyDeclaration,
        "GraphBindsAcquiredPresentationTexture(graph, frame, backbuffer)"
    ));
    EXPECT_EQ(CountText(legacyDeclaration, "importHazardDomain("), 1u);
    EXPECT_TRUE(ContainsText(legacyDeclaration, "ui.imgui_standalone_legacy_presentation.callback"));
    EXPECT_EQ(CountText(ui, ".setType(Core::GpuGraphResourceType::HazardDomain)"), 1u);

    EXPECT_TRUE(ContainsText(
        ui,
        "GraphBindsAcquiredPresentationTexture(context.taskGraph, frame, backbuffer)"
    ));
    EXPECT_EQ(
        CountText(ui, "failed after its terminal packet was accepted; requesting recreation"),
        2u
    );
    for(const AStringView submit : { standalone, legacySubmit }){
        const usize acceptedFailureOffset = submit.find("if(!m_frameFinished){");
        const usize recreationRequestOffset = submit.find("m_graphics.requestDeviceRecreation();", acceptedFailureOffset);
        ASSERT_NE(acceptedFailureOffset, AStringView::npos);
        ASSERT_NE(recreationRequestOffset, AStringView::npos);
        EXPECT_LT(acceptedFailureOffset, recreationRequestOffset);
    }
}


// Late recovery, readback, and history tasks own their record/submit/reject sequencing in the generic runtime.
// Keep the renderer limited to payload validation, timing arming, and device-recreation policy rather than
// reconstructing compiler packet ranges around every late tail.
TEST(EcsGraphics, LateGraphTailsUseRuntimeHelpers){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(system, "recordAndSubmitAcceptedFrontierTask("));
    EXPECT_TRUE(ContainsText(system, "deferredRecorder,\n            m_deferredLightingRecordedGraph,\n            m_deferredFrameRecoveryTask"));
    EXPECT_FALSE(ContainsText(system, "deferredFrameRecoveryPacketRange"));
    EXPECT_FALSE(ContainsText(system, "surfelGiCounterReadbackPacketRange"));
    EXPECT_FALSE(ContainsText(system, "deferredLaggedLightingHistoryPacketRange"));
    EXPECT_EQ(CountText(system, "recordAndSubmitTask("), 2u);
    EXPECT_FALSE(ContainsText(system, "recordTaskRangeInCompileOrder("));
    EXPECT_FALSE(ContainsText(system, "submitTaskRangeInCompileOrder("));
    EXPECT_FALSE(ContainsText(system, "const auto discardFrameRecovery"));
    EXPECT_FALSE(ContainsText(system, "failed to late-record deferred frame recovery packet"));
    EXPECT_FALSE(ContainsText(system, "deferred frame recovery submission was rejected"));
}


// The late history-copy packet prepares all three filtered return candidates while recording and commits them only
// from its exact accepted callback. Its task token remains the source of truth when that callback reports failure.
TEST(EcsGraphics, LaggedHistoryReturnCachesPublishOnlyOnTaskAcceptance){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    const usize acceptanceOffset = system.find("struct HistoryCopyAcceptanceContext{");
    const usize submitOffset = system.find("const bool historyCopyAccepted = submitter.recordAndSubmitTask(", acceptanceOffset);
    const usize tokenOffset = system.find("const Core::QueueSubmissionToken historyCopySubmissionToken =", submitOffset);
    ASSERT_NE(acceptanceOffset, AStringView::npos);
    ASSERT_NE(submitOffset, AStringView::npos);
    ASSERT_NE(tokenOffset, AStringView::npos);
    ASSERT_LT(acceptanceOffset, submitOffset);
    ASSERT_LT(submitOffset, tokenOffset);
    const AStringView acceptance = system.substr(acceptanceOffset, submitOffset - acceptanceOffset);
    EXPECT_EQ(CountText(acceptance, "buildFilteredResourceSubset("), 3u);
    EXPECT_EQ(CountText(acceptance, ".commit("), 3u);
    EXPECT_TRUE(ContainsText(acceptance, ".task = m_deferredLaggedLightingHistoryTask,"));
    EXPECT_TRUE(ContainsText(acceptance, ".context = &historyCopyAcceptance,"));
    EXPECT_TRUE(ContainsText(acceptance, ".invoke = acceptHistoryCopyFinalState,"));
    EXPECT_FALSE(ContainsText(acceptance, ".replaceTextureSubset("));
    EXPECT_FALSE(ContainsText(system, "historyCopyStateBindings"));
    EXPECT_FALSE(ContainsText(system, "GpuTaskPacketStateBinding"));
    EXPECT_TRUE(ContainsText(
        system.substr(submitOffset, tokenOffset - submitOffset),
        "m_deferredLaggedLightingHistoryTask,\n"
        "                &historyCopyRecordedCallback,"
    ));

    const usize failureOffset = system.find(
        "if(historyCopySubmissionToken.valid() && (!historyCopyAccepted || !historyCopyAcceptance.acceptedStateReady))",
        tokenOffset
    );
    ASSERT_NE(failureOffset, AStringView::npos);
    EXPECT_TRUE(ContainsText(
        system.substr(submitOffset, failureOffset - submitOffset),
        "scratchArena,\n                nullptr,\n                &historyCopyAcceptedCallback"
    ));
}


// The graph-owned ImGui terminal task must record from declaration-time data. Re-reading ImGui's mutable command
// arrays after the task declares its sampled textures would allow an undeclared bindless access into the packet.
TEST(EcsGraphics, UiPresentationSnapshotsLateRecordInputs){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString uiSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    const AStringView ui(uiSource.data(), uiSource.size());

    AString uiHeaderSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.h", uiHeaderSource));
    const AStringView uiHeader(uiHeaderSource.data(), uiHeaderSource.size());

    EXPECT_TRUE(ContainsText(uiHeader, "struct TaskGraphDrawCommand"));
    EXPECT_TRUE(ContainsText(ui, "m_taskGraphDrawCommands"));
    EXPECT_TRUE(ContainsText(ui, "recordTaskGraphDrawSnapshot"));
    EXPECT_TRUE(ContainsText(ui, "graph-owned ImGui overlay cannot safely record a custom draw callback"));
    EXPECT_TRUE(ContainsText(ui, "appendDrawTextureUse(drawCommand)"));

    const usize recordOffset = ui.find("bool UiSystem::recordTaskGraphPresentation");
    const usize opaqueRecordOffset = ui.find("bool UiSystem::recordStandaloneLegacyTaskGraphPresentation", recordOffset);
    ASSERT_NE(recordOffset, AStringView::npos);
    ASSERT_NE(opaqueRecordOffset, AStringView::npos);
    ASSERT_LT(recordOffset, opaqueRecordOffset);
    const AStringView recordBody = ui.substr(recordOffset, opaqueRecordOffset - recordOffset);
    EXPECT_TRUE(ContainsText(recordBody, "recordTaskGraphDrawSnapshot(commandList, frame, backbuffer, context)"));
    EXPECT_FALSE(ContainsText(recordBody, "ImGui::GetDrawData()"));
    EXPECT_FALSE(ContainsText(recordBody, "renderDrawData(commandList, frame.framebuffer.get()"));

    // The separately named opaque fallback is intentionally the sole graph task allowed to touch live callback
    // storage, and it must guard that synchronous boundary against a changed ImGui frame.
    const usize completionOffset = ui.find("bool UiSystem::recordTaskGraphUploadCompletion", opaqueRecordOffset);
    ASSERT_NE(completionOffset, AStringView::npos);
    const AStringView opaqueRecord = ui.substr(opaqueRecordOffset, completionOffset - opaqueRecordOffset);
    EXPECT_TRUE(ContainsText(opaqueRecord, "ImGui::GetDrawData() != drawData"));
    EXPECT_TRUE(ContainsText(opaqueRecord, "frameGeneration != m_frameGeneration"));
    EXPECT_TRUE(ContainsText(opaqueRecord, "renderDrawData(commandList, frame.framebuffer.get(), *drawData)"));
}


// Large immutable UI uploads may already select Transfer/Compute. Their persistent buffers and textures must be
// created with the matching graph-sharing contract before a same-class auxiliary queue can legally record them.
TEST(EcsGraphics, UiGraphUploadsDeclareConcurrentProducerFamilies){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString uiSystemSource;
    AString uiTextureSource;
    AString uiGraphicsResourceSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "texture_resources.cpp", uiTextureSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "graphics_resources.cpp", uiGraphicsResourceSource));
    const AStringView uiSystem(uiSystemSource.data(), uiSystemSource.size());
    const AStringView uiTextures(uiTextureSource.data(), uiTextureSource.size());
    const AStringView uiGraphicsResources(uiGraphicsResourceSource.data(), uiGraphicsResourceSource.size());

    EXPECT_TRUE(ContainsText(uiSystem, "allowSameClassQueueRouting = preferDedicatedTransport"));
    EXPECT_TRUE(ContainsText(uiSystem, "allowCrossFamilySameClassQueueRouting = preferDedicatedTransport"));
    EXPECT_TRUE(ContainsText(uiTextures, "allowSameClassQueueRouting = preferDedicatedTransport"));
    EXPECT_TRUE(ContainsText(uiTextures, "allowCrossFamilySameClassQueueRouting = preferDedicatedTransport"));
    EXPECT_TRUE(ContainsText(uiTextures, "ResourceQueueSharing::GraphicsAsyncComputeAndTransfer"));
    EXPECT_TRUE(ContainsText(uiGraphicsResources, "ResourceQueueSharing::GraphicsAsyncComputeAndTransfer"));
}


// A newly-created retained ImGui texture is natively Unknown until its upload accepts. The graph must preserve that
// origin for the first write, then use the descriptor ShaderResource state only after the accepted batch publishes it.
TEST(EcsGraphics, UiFreshTextureImportsPreserveNativeOrigins){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString uiHeaderSource;
    AString uiSource;
    AString uiTextureSource;
    AString uiSubmissionSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.h", uiHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "texture_resources.cpp", uiTextureSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "texture_submission.h", uiSubmissionSource));
    const AStringView uiHeader(uiHeaderSource.data(), uiHeaderSource.size());
    const AStringView ui(uiSource.data(), uiSource.size());
    const AStringView uiTextures(uiTextureSource.data(), uiTextureSource.size());
    const AStringView uiSubmission(uiSubmissionSource.data(), uiSubmissionSource.size());

    EXPECT_TRUE(ContainsText(uiHeader, "bool initialUploadAccepted = false;"));
    EXPECT_TRUE(ContainsText(uiHeader, "bool textureInitialUploadAccepted = false;"));
    EXPECT_TRUE(ContainsText(uiSubmission, "bool* initialUploadAccepted = nullptr;"));
    EXPECT_TRUE(ContainsText(uiSubmission, "void add(ImTextureData& textureData, bool* const initialUploadAccepted = nullptr)"));

    const usize createStatusOffset = uiSubmission.find("case ImTextureStatus_WantCreate:");
    const usize updateStatusOffset = uiSubmission.find("case ImTextureStatus_WantUpdates:", createStatusOffset);
    const usize okStatusOffset = uiSubmission.find("case ImTextureStatus_OK:", updateStatusOffset);
    ASSERT_NE(createStatusOffset, AStringView::npos);
    ASSERT_NE(updateStatusOffset, AStringView::npos);
    ASSERT_NE(okStatusOffset, AStringView::npos);
    ASSERT_LT(createStatusOffset, updateStatusOffset);
    ASSERT_LT(updateStatusOffset, okStatusOffset);
    const AStringView createStatus = uiSubmission.substr(createStatusOffset, updateStatusOffset - createStatusOffset);
    const AStringView updateStatus = uiSubmission.substr(updateStatusOffset, okStatusOffset - updateStatusOffset);
    EXPECT_TRUE(ContainsText(
        createStatus,
        "if(request.initialUploadAccepted)\n                        *request.initialUploadAccepted = true;"
    ));
    EXPECT_FALSE(ContainsText(updateStatus, "initialUploadAccepted"));

    for(const AStringView source : { uiTextures, ui }){
        EXPECT_TRUE(ContainsText(
            source,
            "TextureResourceDesc(\n    const Core::TextureDesc& textureDesc,\n    const bool initialUploadAccepted\n)"
        ));
        EXPECT_TRUE(ContainsText(
            source,
            ".setInitialState(initialUploadAccepted ? textureDesc.initialState : Core::ResourceStates::Unknown)"
        ));
    }
    EXPECT_TRUE(ContainsText(
        uiTextures,
        "__hidden_ui::TextureResourceDesc(resource.texture->getCreationDescription(), resource.initialUploadAccepted)"
    ));
    EXPECT_TRUE(ContainsText(uiTextures, "m_textureUploadBatch.add(*textureData, &resource->initialUploadAccepted)"));
    EXPECT_TRUE(ContainsText(ui, ".textureInitialUploadAccepted = textureResource->initialUploadAccepted,"));
    EXPECT_TRUE(ContainsText(ui, "const auto appendDrawTextureUse = [&](const TaskGraphDrawCommand& drawCommand){"));
    EXPECT_TRUE(ContainsText(
        ui,
        "__hidden_ui::TextureResourceDesc(\n"
        "                    drawCommand.texture->getCreationDescription(),\n"
        "                    drawCommand.textureInitialUploadAccepted\n"
        "                )"
    ));
    EXPECT_TRUE(ContainsText(ui, "appendDrawTextureUse(drawCommand)"));
    EXPECT_TRUE(ContainsText(ui, "m_textureUploadBatch.complete(true);"));
}


// Every ImGui presentation route remains graph-owned. Callback-free rejection keeps the exact live frame until a
// later acquired image can rebuild its snapshot; an opaque callback rejection cannot be replayed and fails closed.
TEST(EcsGraphics, UiPresentationRetriesOnlyThroughStandaloneGraphs){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString graphicsHeaderSource;
    AString graphicsSource;
    AString uiHeaderSource;
    AString uiSource;
    AString uiTextureSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "module.h", graphicsHeaderSource));
    ASSERT_TRUE(ReadGraphicsModuleSources(repoRoot, graphicsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.h", uiHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "texture_resources.cpp", uiTextureSource));
    const AStringView graphicsHeader(graphicsHeaderSource.data(), graphicsHeaderSource.size());
    const AStringView graphics(graphicsSource.data(), graphicsSource.size());
    const AStringView uiHeader(uiHeaderSource.data(), uiHeaderSource.size());
    const AStringView ui(uiSource.data(), uiSource.size());
    const AStringView uiTextures(uiTextureSource.data(), uiTextureSource.size());

    EXPECT_TRUE(ContainsText(graphicsHeader, "StandaloneTaskGraphDeclaration"));
    EXPECT_TRUE(ContainsText(graphicsHeader, "submitStandaloneTaskGraph"));
    EXPECT_TRUE(ContainsText(graphics, "Graphics::submitStandaloneTaskGraph"));
    EXPECT_TRUE(ContainsText(ui, "StandaloneTextureUploadCompletionTask"));
    EXPECT_TRUE(ContainsText(ui, "declareStandaloneTextureUploadGraph"));
    EXPECT_TRUE(ContainsText(ui, "submitStandaloneTaskGraphPresentation"));
    EXPECT_TRUE(ContainsText(ui, "StandaloneLegacyPresentationTask"));
    EXPECT_TRUE(ContainsText(ui, "declareStandaloneLegacyTaskGraphPresentation"));
    EXPECT_TRUE(ContainsText(ui, "submitStandaloneLegacyTaskGraphPresentation"));
    EXPECT_TRUE(ContainsText(ui, "OpaquePresentationQueueRequest"));
    EXPECT_TRUE(ContainsText(ui, "Standalone ImGui Presentation Back Buffer"));
    EXPECT_TRUE(ContainsText(ui, "ImGui Opaque Callback Domain"));
    EXPECT_TRUE(ContainsText(ui, "if(prepareTaskGraphPresentation(frame))"));
    EXPECT_TRUE(ContainsText(uiHeader, "m_taskGraphPresentationRetryPending"));
    EXPECT_FALSE(ContainsText(uiHeader, "ensureRenderCommandList"));
    EXPECT_FALSE(ContainsText(uiHeader, "m_renderCommandList"));
    EXPECT_FALSE(ContainsText(ui, "executeCommandLists("));
    EXPECT_FALSE(ContainsText(ui, "ensureRenderCommandList"));

    const usize opaquePresentationOffset = ui.find("Core::GpuTaskId UiSystem::declareStandaloneLegacyTaskGraphPresentation");
    const usize legacySubmitOffset = ui.find("bool UiSystem::submitPreparedLegacyTextureUploads");
    ASSERT_NE(opaquePresentationOffset, AStringView::npos);
    ASSERT_NE(legacySubmitOffset, AStringView::npos);
    ASSERT_LT(opaquePresentationOffset, legacySubmitOffset);
    const AStringView opaquePresentation = ui.substr(opaquePresentationOffset, legacySubmitOffset - opaquePresentationOffset);
    EXPECT_TRUE(ContainsText(opaquePresentation, "m_graphics.submitStandaloneTaskGraph"));
    EXPECT_TRUE(ContainsText(opaquePresentation, "importTaskGraphTexture(graph, *textureResource)"));
    EXPECT_TRUE(ContainsText(opaquePresentation, "m_frameGeneration"));
    EXPECT_TRUE(ContainsText(opaquePresentation, "setQueue(__hidden_ui::OpaquePresentationQueueRequest())"));
    EXPECT_TRUE(ContainsText(ui, "recordStandaloneLegacyTaskGraphPresentation"));
    EXPECT_FALSE(ContainsText(opaquePresentation, "executeCommandLists"));
    EXPECT_FALSE(ContainsText(opaquePresentation, "createCommandList"));

    const usize renderOffset = ui.find("void UiSystem::render", legacySubmitOffset);
    ASSERT_NE(renderOffset, AStringView::npos);
    const AStringView legacySubmit = ui.substr(legacySubmitOffset, renderOffset - legacySubmitOffset);
    EXPECT_TRUE(ContainsText(legacySubmit, "m_graphics.submitStandaloneTaskGraph"));
    EXPECT_TRUE(ContainsText(legacySubmit, "getPrimaryPhysicalQueue(Core::CommandQueue::Graphics)"));
    EXPECT_TRUE(ContainsText(legacySubmit, "submissionToken,\n        graphicsQueue"));
    EXPECT_FALSE(ContainsText(legacySubmit, "executeCommandLists"));
    EXPECT_FALSE(ContainsText(legacySubmit, "createCommandList"));
    EXPECT_FALSE(ContainsText(legacySubmit, "prepareTextureRequests"));
    EXPECT_FALSE(ContainsText(ui, "m_prepareCommandList"));
    EXPECT_FALSE(ContainsText(uiTextures, "recordTextureUpload"));
    EXPECT_TRUE(ContainsText(uiTextures, "if(previousTask.valid())"));

    const usize presentationDeclareOffset = ui.find("Core::GpuTaskId UiSystem::declareTaskGraphPresentation");
    const usize standaloneTextureOffset = ui.find("Core::GpuTaskId UiSystem::declareStandaloneTextureUploadGraph");
    ASSERT_NE(presentationDeclareOffset, AStringView::npos);
    ASSERT_NE(standaloneTextureOffset, AStringView::npos);
    const AStringView presentationDeclare = ui.substr(
        presentationDeclareOffset,
        standaloneTextureOffset - presentationDeclareOffset
    );
    EXPECT_FALSE(ContainsText(presentationDeclare, "|| !previousTask.valid()"));
    EXPECT_TRUE(ContainsText(presentationDeclare, "if(previousTask.valid())"));

    const usize resizeOffset = ui.find("void UiSystem::backBufferResizing", renderOffset);
    ASSERT_NE(resizeOffset, AStringView::npos);
    const AStringView presentationRenderBody = ui.substr(renderOffset, resizeOffset - renderOffset);
    const usize standalonePresentationOffset = presentationRenderBody.find("submitStandaloneTaskGraphPresentation(frame)");
    const usize opaquePresentationFallbackOffset = presentationRenderBody.find("submitStandaloneLegacyTaskGraphPresentation(frame)");
    const usize retainedRetryOffset = presentationRenderBody.find("retainTaskGraphPresentationForRetry();");
    const usize textureOnlyOffset = presentationRenderBody.find("submitPreparedLegacyTextureUploads(*drawData)");
    ASSERT_NE(standalonePresentationOffset, AStringView::npos);
    ASSERT_NE(opaquePresentationFallbackOffset, AStringView::npos);
    ASSERT_NE(retainedRetryOffset, AStringView::npos);
    ASSERT_NE(textureOnlyOffset, AStringView::npos);
    EXPECT_LT(standalonePresentationOffset, opaquePresentationFallbackOffset);
    EXPECT_LT(opaquePresentationFallbackOffset, retainedRetryOffset);
    EXPECT_LT(retainedRetryOffset, textureOnlyOffset);
    EXPECT_TRUE(ContainsText(presentationRenderBody, "const bool retrySafe = m_taskGraphDrawUploadsPrepared;"));
    EXPECT_TRUE(ContainsText(presentationRenderBody, "if(m_graphics.isDeviceRecreationRequested())"));
    EXPECT_TRUE(ContainsText(presentationRenderBody, "if(!retrySafe){"));
    EXPECT_TRUE(ContainsText(presentationRenderBody, "after an opaque callback may have executed; requesting recreation"));
    EXPECT_TRUE(ContainsText(presentationRenderBody, "retaining callback-free frame for graph retry"));

    const usize visibleDrawOffset = presentationRenderBody.find("if(hasVisibleDraw){");
    ASSERT_NE(visibleDrawOffset, AStringView::npos);
    ASSERT_LT(visibleDrawOffset, textureOnlyOffset);
    const AStringView visibleDrawBody = presentationRenderBody.substr(visibleDrawOffset, textureOnlyOffset - visibleDrawOffset);
    const usize legacyAttemptOffset = visibleDrawBody.find("submitStandaloneLegacyTaskGraphPresentation(frame)");
    const usize acceptedTerminalExclusionOffset = visibleDrawBody.find("if(!m_frameFinished)", legacyAttemptOffset);
    const usize recreationExclusionOffset = visibleDrawBody.find(
        "if(m_graphics.isDeviceRecreationRequested())",
        acceptedTerminalExclusionOffset
    );
    const usize callbackPolicyOffset = visibleDrawBody.find("if(!retrySafe){", recreationExclusionOffset);
    const usize callbackFreeRetryOffset = visibleDrawBody.find(
        "retainTaskGraphPresentationForRetry();",
        callbackPolicyOffset
    );
    ASSERT_NE(legacyAttemptOffset, AStringView::npos);
    ASSERT_NE(acceptedTerminalExclusionOffset, AStringView::npos);
    ASSERT_NE(recreationExclusionOffset, AStringView::npos);
    ASSERT_NE(callbackPolicyOffset, AStringView::npos);
    ASSERT_NE(callbackFreeRetryOffset, AStringView::npos);
    EXPECT_LT(legacyAttemptOffset, acceptedTerminalExclusionOffset);
    EXPECT_LT(acceptedTerminalExclusionOffset, recreationExclusionOffset);
    EXPECT_LT(recreationExclusionOffset, callbackPolicyOffset);
    EXPECT_LT(callbackPolicyOffset, callbackFreeRetryOffset);
    EXPECT_FALSE(ContainsText(visibleDrawBody, "submitPreparedLegacyTextureUploads"));
    EXPECT_TRUE(ContainsText(visibleDrawBody, "if(!m_frameFinished)"));
    EXPECT_TRUE(ContainsText(visibleDrawBody, "if(m_graphics.isDeviceRecreationRequested())"));
    EXPECT_TRUE(ContainsText(visibleDrawBody, "m_graphics.requestDeviceRecreation();"));
    EXPECT_TRUE(ContainsText(visibleDrawBody, "retainTaskGraphPresentationForRetry();"));

    const usize updateOffset = ui.find("void UiSystem::update");
    const usize beginFrameOffset = ui.find("void UiSystem::beginFrame", updateOffset);
    ASSERT_NE(updateOffset, AStringView::npos);
    ASSERT_NE(beginFrameOffset, AStringView::npos);
    const AStringView updateBody = ui.substr(updateOffset, beginFrameOffset - updateOffset);
    const usize retryGateOffset = updateBody.find("if(m_taskGraphPresentationRetryPending)");
    const usize beginCallOffset = updateBody.find("beginFrame(delta)");
    ASSERT_NE(retryGateOffset, AStringView::npos);
    ASSERT_NE(beginCallOffset, AStringView::npos);
    EXPECT_LT(retryGateOffset, beginCallOffset);

    const usize confirmOffset = ui.find("void UiSystem::confirmTaskGraphPresentationSubmission");
    const usize retainDefinitionOffset = ui.find("void UiSystem::retainTaskGraphPresentationForRetry", confirmOffset);
    const usize discardOffset = ui.find("void UiSystem::discardStandaloneLegacyTaskGraphPresentation", retainDefinitionOffset);
    ASSERT_NE(confirmOffset, AStringView::npos);
    ASSERT_NE(retainDefinitionOffset, AStringView::npos);
    ASSERT_NE(discardOffset, AStringView::npos);
    const AStringView confirmBody = ui.substr(confirmOffset, retainDefinitionOffset - confirmOffset);
    const AStringView retainBody = ui.substr(retainDefinitionOffset, discardOffset - retainDefinitionOffset);
    EXPECT_TRUE(ContainsText(confirmBody, "m_taskGraphPresentationRetryPending = false;"));
    EXPECT_TRUE(ContainsText(retainBody, "m_textureUploadBatch.complete(false);"));
    EXPECT_TRUE(ContainsText(retainBody, "m_taskGraphPresentationRetryPending = true;"));
    EXPECT_TRUE(ContainsText(retainBody, "m_taskGraphPresentationPrepared = false;"));
    EXPECT_TRUE(ContainsText(retainBody, "m_taskGraphPresentationFrame = {};"));
    EXPECT_TRUE(ContainsText(retainBody, "clearTaskGraphDrawSnapshot();"));
    EXPECT_FALSE(ContainsText(retainBody, "m_frameStarted = false;"));
    EXPECT_FALSE(ContainsText(retainBody, "m_frameFinished = false;"));
    EXPECT_FALSE(ContainsText(retainBody, "m_frameGeneration"));

    const usize textureCompletionTaskOffset = ui.find("struct UiSystem::StandaloneTextureUploadCompletionTask");
    const usize legacyTaskOffset = ui.find("struct UiSystem::StandaloneLegacyPresentationTask", textureCompletionTaskOffset);
    ASSERT_NE(textureCompletionTaskOffset, AStringView::npos);
    ASSERT_NE(legacyTaskOffset, AStringView::npos);
    const AStringView textureCompletionTask = ui.substr(
        textureCompletionTaskOffset,
        legacyTaskOffset - textureCompletionTaskOffset
    );
    const AStringView textureOnlyRenderBody = presentationRenderBody.substr(textureOnlyOffset);
    EXPECT_TRUE(ContainsText(textureCompletionTask, "m_textureUploadBatch.complete(true);"));
    EXPECT_FALSE(ContainsText(textureCompletionTask, "m_frameStarted = false;"));
    EXPECT_FALSE(ContainsText(textureCompletionTask, "m_frameFinished = false;"));
    EXPECT_TRUE(ContainsText(textureOnlyRenderBody, "m_frameStarted = false;"));
    EXPECT_TRUE(ContainsText(textureOnlyRenderBody, "m_frameFinished = false;"));
    EXPECT_TRUE(ContainsText(textureOnlyRenderBody, "m_taskGraphPresentationRetryPending = false;"));

    const usize invalidateOffset = ui.find("void UiSystem::invalidateResources");
    const usize prepareResourcesOffset = ui.find("bool UiSystem::prepareResources", invalidateOffset);
    ASSERT_NE(invalidateOffset, AStringView::npos);
    ASSERT_NE(prepareResourcesOffset, AStringView::npos);
    const AStringView invalidateBody = ui.substr(invalidateOffset, prepareResourcesOffset - invalidateOffset);
    const AStringView resizeBody = ui.substr(resizeOffset);
    EXPECT_TRUE(ContainsText(invalidateBody, "m_taskGraphPresentationRetryPending = false;"));
    EXPECT_TRUE(ContainsText(resizeBody, "m_taskGraphPresentationRetryPending = false;"));
    EXPECT_TRUE(ContainsText(presentationRenderBody, "m_taskGraphPresentationRetryPending = false;"));
}


// Public setup uploads return only a resource handle, so a Transfer/Compute producer must still establish queue
// order for later legacy consumers. Keep those readiness packets inside the same graph transaction instead of
// issuing an opaque direct zero-command submission after graph acceptance.
TEST(EcsGraphics, SetupUploadReadinessBridgeRemainsGraphOwned){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString graphicsSource;
    AString textureUploadSource;
    ASSERT_TRUE(ReadGraphicsModuleSources(repoRoot, graphicsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "module_texture_upload.cpp", textureUploadSource));
    const AStringView graphics(graphicsSource.data(), graphicsSource.size());
    const AStringView textureUpload(textureUploadSource.data(), textureUploadSource.size());

    EXPECT_TRUE(ContainsText(graphics, "SetupUploadReadinessBridgeGraphTask"));
    EXPECT_TRUE(ContainsText(graphics, "DeclareSetupUploadReadinessBridgeTasks"));
    EXPECT_TRUE(ContainsText(graphics, "graphics.setup_upload.readiness_bridge"));
    EXPECT_FALSE(ContainsText(graphics, "BridgeSetupUploadToConsumerQueues"));

    const usize setupGraphOffset = graphics.find("GpuTaskId DeclareSetupUploadGraph");
    const usize timingResetOffset = graphics.find("struct FrameTimingResetGraphTask", setupGraphOffset);
    const usize setupUploadOffset = graphics.find("bool SubmitGraphOwnedSetupUpload");
    const usize standaloneGraphOffset = graphics.find("bool Graphics::submitStandaloneTaskGraph", setupUploadOffset);
    ASSERT_NE(setupGraphOffset, AStringView::npos);
    ASSERT_NE(timingResetOffset, AStringView::npos);
    ASSERT_NE(setupUploadOffset, AStringView::npos);
    ASSERT_NE(standaloneGraphOffset, AStringView::npos);
    const AStringView setupGraph = graphics.substr(setupGraphOffset, timingResetOffset - setupGraphOffset);
    const AStringView setupUpload = graphics.substr(setupUploadOffset, standaloneGraphOffset - setupUploadOffset);
    EXPECT_TRUE(ContainsText(setupGraph, "DeclareSetupUploadReadinessBridgeTasks"));
    EXPECT_TRUE(ContainsText(setupUpload, "bridgePrimaryUploadQueue"));
    EXPECT_TRUE(ContainsText(setupUpload, "requiredTerminalQueue"));
    EXPECT_FALSE(ContainsText(setupUpload, "executeCommandLists"));
    EXPECT_TRUE(ContainsText(graphics, "outSubmissionToken = transaction.taskToken(compiledGraph, terminalTask);"));
    EXPECT_FALSE(ContainsText(graphics, "outSubmissionToken = transaction.packetToken(terminalPacket);"));
    EXPECT_TRUE(ContainsText(graphics, "ResolveSetupUploadSameClassRouting"));
    EXPECT_TRUE(ContainsText(graphics, "preferNonPrimarySameClassQueue"));
    EXPECT_TRUE(ContainsText(graphics, "sameClassRouting.enabled ? sameClassRouting.primaryQueue"));

    const usize textureBatchOffset = graphics.find("bool Graphics::uploadTextureBatch");
    const usize meshSetupOffset = graphics.find("Graphics::MeshResource Graphics::setupMesh", textureBatchOffset);
    ASSERT_NE(textureBatchOffset, AStringView::npos);
    ASSERT_NE(meshSetupOffset, AStringView::npos);
    const AStringView textureBatch = graphics.substr(textureBatchOffset, meshSetupOffset - textureBatchOffset);
    EXPECT_TRUE(ContainsText(textureUpload, "preserveSameClassQueueWithDirectDependency"));
    EXPECT_TRUE(ContainsText(textureBatch, "sameClassRouting.crossesQueueFamily"));
}


// Windows may deny foreground activation to the parent smoke harness after bootstrap. Keep the opt-in local to the
// frame-lagged executable so it bypasses only its focus throttle, uses Graphics' normal render-pass extension, and
// unregisters before the renderer/world can be destroyed.
TEST(EcsGraphics, FrameLaggedSmokeRendersWhenUnfocused){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "transparent_multi_project.cpp", smokeSource));
    const AStringView smoke(smokeSource.data(), smokeSource.size());

    EXPECT_TRUE(ContainsText(smoke, "class FrameLaggedAsyncLightingUnfocusedPass final : public NWB::Core::IRenderPass"));
    EXPECT_TRUE(ContainsText(smoke, "virtual bool shouldRenderUnfocused()override{ return true; }"));
    EXPECT_TRUE(ContainsText(smoke, "m_context.graphics.addRenderPassToBack(m_frameLaggedAsyncLightingUnfocusedPass);"));
    EXPECT_TRUE(ContainsText(smoke, "m_context.graphics.removeRenderPass(m_frameLaggedAsyncLightingUnfocusedPass);"));

    const usize shutdownOffset = smoke.find("virtual void onShutdown()override");
    const usize updateOffset = smoke.find("virtual bool onUpdate", shutdownOffset);
    ASSERT_NE(shutdownOffset, AStringView::npos);
    ASSERT_NE(updateOffset, AStringView::npos);
    const AStringView shutdown = smoke.substr(shutdownOffset, updateOffset - shutdownOffset);
    const usize removeOffset = shutdown.find("removeFrameLaggedAsyncLightingUnfocusedPass();");
    const usize destroyOffset = shutdown.find("destroyWorld();");
    ASSERT_NE(removeOffset, AStringView::npos);
    ASSERT_NE(destroyOffset, AStringView::npos);
    EXPECT_LT(removeOffset, destroyOffset);
}


// The parity baseline must pin temporal AVBOIT in test code, not introduce a renderer/core feature toggle. The
// runner waits for the smoke marker only after that project has suspended its next submission.
TEST(EcsGraphics, TransparentAvboitBaselineCaptureIsFrameLockedAndTestOwned){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString profileSource;
    AString runnerSource;
    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "profiles.py", profileSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "run.py", runnerSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "transparent_multi_project.cpp", smokeSource));
    const AStringView profiles(profileSource.data(), profileSource.size());
    const AStringView runner(runnerSource.data(), runnerSource.size());
    const AStringView smoke(smokeSource.data(), smokeSource.size());

    EXPECT_TRUE(ContainsText(profiles, "capture_freeze_frame=96"));
    EXPECT_TRUE(ContainsText(profiles, "capture_ready_log=\"TransparentMultiSmokeProject: renderer baseline capture ready after\""));
    EXPECT_TRUE(ContainsText(profiles, "fixed_delta_seconds=1.0 / 60.0"));
    EXPECT_TRUE(ContainsText(runner, "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"));
    EXPECT_TRUE(ContainsText(runner, "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"));
    EXPECT_TRUE(ContainsText(runner, "wait_for_log_message("));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineCaptureFreezeFrame"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineFixedDelta"));
    EXPECT_TRUE(ContainsText(smoke, "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"));
    EXPECT_TRUE(ContainsText(smoke, "m_context.graphics.setFrameSubmissionSuspended(true)"));
    EXPECT_TRUE(ContainsText(smoke, "renderer baseline capture ready after {} rendered frames; render submission suspended"));
    EXPECT_TRUE(ContainsText(smoke, "m_context.graphics.setFrameSubmissionSuspended(false)"));
}


// Skinned CSG animates both the receiver and the cutter. It must use the same test-only freeze contract before its
// baseline can act as a runtime-skinning/CSG parity reference.
TEST(EcsGraphics, SkinnedCsgBaselineCaptureIsFrameLockedAndTestOwned){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString profileSource;
    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "profiles.py", profileSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "csg_skinned_visible_project.cpp", smokeSource));
    const AStringView profiles(profileSource.data(), profileSource.size());
    const AStringView smoke(smokeSource.data(), smokeSource.size());

    EXPECT_TRUE(ContainsText(profiles, "window_title=\"NWB Skinned CSG Smoke\""));
    EXPECT_TRUE(ContainsText(profiles, "capture_ready_log=\"CsgSkinnedVisibleSmokeProject: renderer baseline capture ready after\""));
    EXPECT_TRUE(ContainsText(profiles, "fixed_delta_seconds=1.0 / 60.0"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineCaptureFreezeFrame"));
    EXPECT_TRUE(ContainsText(smoke, "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineFixedDelta"));
    EXPECT_TRUE(ContainsText(smoke, "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"));
    EXPECT_TRUE(ContainsText(smoke, "m_context.graphics.setFrameSubmissionSuspended(true)"));
    EXPECT_TRUE(ContainsText(smoke, "CsgSkinnedVisibleSmokeProject: renderer baseline capture ready after {} rendered frames; render submission suspended"));
    EXPECT_TRUE(ContainsText(smoke, "m_context.graphics.setFrameSubmissionSuspended(false)"));
}


// Caustic accumulation is temporal even with a static refractor. Keep its baseline warm-up frame-counted and on the
// same test-only fixed clock rather than allowing host throughput to choose an arbitrary convergence point.
TEST(EcsGraphics, CausticBaselineUsesFixedTemporalWarmup){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString profileSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "profiles.py", profileSource));
    const AStringView profiles(profileSource.data(), profileSource.size());
    const usize causticsOffset = profiles.find("\"caustics\": BaselineProfile(");
    const usize surfelOffset = profiles.find("\"surfel-gi\": BaselineProfile(", causticsOffset);
    ASSERT_NE(causticsOffset, AStringView::npos);
    ASSERT_NE(surfelOffset, AStringView::npos);
    ASSERT_LT(causticsOffset, surfelOffset);
    const AStringView caustics = profiles.substr(causticsOffset, surfelOffset - causticsOffset);

    EXPECT_TRUE(ContainsText(caustics, "settle_seconds=0.75"));
    EXPECT_TRUE(ContainsText(caustics, "capture_freeze_frame=360"));
    EXPECT_TRUE(ContainsText(caustics, "capture_ready_log=\"TransparentMultiSmokeProject: renderer baseline capture ready after\""));
    EXPECT_TRUE(ContainsText(caustics, "fixed_delta_seconds=1.0 / 60.0"));
}


// Surfel GI has its own temporal producer/resolve sequence. Its baseline therefore needs a project-local, fixed
// frame boundary rather than inheriting wall-clock capture timing from an unrelated smoke scene.
TEST(EcsGraphics, SurfelGiBaselineUsesFixedTemporalWarmup){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString profileSource;
    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "profiles.py", profileSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "gi_test_project.cpp", smokeSource));
    const AStringView profiles(profileSource.data(), profileSource.size());
    const AStringView smoke(smokeSource.data(), smokeSource.size());
    const usize surfelOffset = profiles.find("\"surfel-gi\": BaselineProfile(");
    const usize stressOffset = profiles.find("\"stress\": BaselineProfile(", surfelOffset);
    ASSERT_NE(surfelOffset, AStringView::npos);
    ASSERT_NE(stressOffset, AStringView::npos);
    ASSERT_LT(surfelOffset, stressOffset);
    const AStringView surfel = profiles.substr(surfelOffset, stressOffset - surfelOffset);

    EXPECT_TRUE(ContainsText(surfel, "settle_seconds=0.75"));
    EXPECT_TRUE(ContainsText(surfel, "capture_freeze_frame=360"));
    EXPECT_TRUE(ContainsText(surfel, "capture_ready_log=\"GiTestSmokeProject: renderer baseline capture ready after\""));
    EXPECT_TRUE(ContainsText(surfel, "fixed_delta_seconds=1.0 / 60.0"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineCaptureFreezeFrame"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineFixedDelta"));
    EXPECT_TRUE(ContainsText(smoke, "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"));
    EXPECT_TRUE(ContainsText(smoke, "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"));
    EXPECT_TRUE(ContainsText(smoke, "GiTestSmokeProject: renderer baseline capture ready after {} rendered frames; render submission suspended"));
}


// Every surfel stage consumes the same packed [0,1] G-buffer normal contract. Keep the decode in one shader include so
// spawn cannot silently store a different normal space from resolve and upsample.
TEST(EcsGraphics, SurfelGbufferNormalsSharePackedDecodeContract){
    TestArena testArena;
    const TestPath surfelDirectory = RepoRoot(testArena) / "impl" / "assets" / "graphics" / "gi" / "surfel";

    AString gbufferSource;
    AString spawnSource;
    AString resolveSource;
    AString upsampleSource;
    ASSERT_TRUE(ReadTextFile(surfelDirectory / "surfel_gbuffer.slangi", gbufferSource));
    ASSERT_TRUE(ReadTextFile(surfelDirectory / "surfel_spawn_cs.slang", spawnSource));
    ASSERT_TRUE(ReadTextFile(surfelDirectory / "surfel_resolve_cs.slang", resolveSource));
    ASSERT_TRUE(ReadTextFile(surfelDirectory / "surfel_upsample_cs.slang", upsampleSource));
    const AStringView gbuffer(gbufferSource.data(), gbufferSource.size());
    const AStringView spawn(spawnSource.data(), spawnSource.size());
    const AStringView resolve(resolveSource.data(), resolveSource.size());
    const AStringView upsample(upsampleSource.data(), upsampleSource.size());

    EXPECT_TRUE(ContainsText(gbuffer, "return normalize(packedNormal * 2.0 - 1.0);"));
    EXPECT_TRUE(ContainsText(spawn, "#include \"surfel_gbuffer.slangi\""));
    EXPECT_TRUE(ContainsText(resolve, "#include \"surfel_gbuffer.slangi\""));
    EXPECT_TRUE(ContainsText(upsample, "#include \"surfel_gbuffer.slangi\""));
    EXPECT_TRUE(ContainsText(spawn, "const float3 worldNormal = nwbSurfelDecodeGbufferNormal(rawNormal);"));
    EXPECT_TRUE(ContainsText(resolve, "const float3 normal = nwbSurfelDecodeGbufferNormal(rawNormal);"));
    EXPECT_TRUE(ContainsText(upsample, "const float3 centerNormal = nwbSurfelDecodeGbufferNormal(rawNormal);"));
    EXPECT_TRUE(ContainsText(upsample, "const float3 tapNormal = nwbSurfelDecodeGbufferNormal(tapRawNormal);"));
    EXPECT_FALSE(ContainsText(spawn, "normalize(rawNormal);"));
    EXPECT_FALSE(ContainsText(spawn, "normalize(rawNormal * 2.0 - 1.0)"));
    EXPECT_FALSE(ContainsText(resolve, "normalize(rawNormal * 2.0 - 1.0)"));
    EXPECT_FALSE(ContainsText(upsample, "normalize(rawNormal * 2.0 - 1.0)"));
}


// Soft shadows retain temporal history even when the camera/yaw are frozen. Capture the same accepted history phase
// through the smoke-only fixed clock before using this scene as a parity reference.
TEST(EcsGraphics, SoftShadowBaselineUsesFixedTemporalWarmup){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString profileSource;
    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "profiles.py", profileSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "soft_shadow_test_project.cpp", smokeSource));
    const AStringView profiles(profileSource.data(), profileSource.size());
    const AStringView smoke(smokeSource.data(), smokeSource.size());
    const usize softShadowOffset = profiles.find("\"soft-shadows\": BaselineProfile(");
    const usize causticsOffset = profiles.find("\"caustics\": BaselineProfile(", softShadowOffset);
    ASSERT_NE(softShadowOffset, AStringView::npos);
    ASSERT_NE(causticsOffset, AStringView::npos);
    ASSERT_LT(softShadowOffset, causticsOffset);
    const AStringView softShadows = profiles.substr(softShadowOffset, causticsOffset - softShadowOffset);

    EXPECT_TRUE(ContainsText(softShadows, "settle_seconds=0.75"));
    EXPECT_TRUE(ContainsText(softShadows, "capture_freeze_frame=360"));
    EXPECT_TRUE(ContainsText(softShadows, "capture_ready_log=\"SoftShadowTestSmokeProject: renderer baseline capture ready after\""));
    EXPECT_TRUE(ContainsText(softShadows, "fixed_delta_seconds=1.0 / 60.0"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineCaptureFreezeFrame"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineFixedDelta"));
    EXPECT_TRUE(ContainsText(smoke, "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"));
    EXPECT_TRUE(ContainsText(smoke, "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"));
    EXPECT_TRUE(ContainsText(smoke, "SoftShadowTestSmokeProject: renderer baseline capture ready after {} rendered frames; render submission suspended"));
}


// The stress scene already has a dedicated M4 capture contract. Its baseline path must remain separate, while still
// pinning the dense skinned transparent/opaque workload to a reproducible frame and simulation clock.
TEST(EcsGraphics, StressBaselineUsesSeparateFixedTemporalWarmup){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString profileSource;
    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "profiles.py", profileSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "stress_test_project.cpp", smokeSource));
    const AStringView profiles(profileSource.data(), profileSource.size());
    const AStringView smoke(smokeSource.data(), smokeSource.size());
    const usize stressOffset = profiles.find("\"stress\": BaselineProfile(");
    ASSERT_NE(stressOffset, AStringView::npos);
    const AStringView stress = profiles.substr(stressOffset);

    EXPECT_TRUE(ContainsText(stress, "settle_seconds=0.75"));
    EXPECT_TRUE(ContainsText(stress, "capture_freeze_frame=96"));
    EXPECT_TRUE(ContainsText(stress, "capture_ready_log=\"StressTestSmokeProject: renderer baseline capture ready after\""));
    EXPECT_TRUE(ContainsText(stress, "fixed_delta_seconds=1.0 / 60.0"));
    EXPECT_TRUE(ContainsText(smoke, "m4PixelCaptureFreezeFrame"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineCaptureFreezeFrame"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineFixedDelta"));
    EXPECT_TRUE(ContainsText(smoke, "StressTestSmokeProject: M4 pixel capture ready after {} rendered frames; render submission suspended"));
    EXPECT_TRUE(ContainsText(smoke, "StressTestSmokeProject: renderer baseline capture ready after {} rendered frames; render submission suspended"));
    EXPECT_TRUE(ContainsText(smoke, "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"));
}


// Surfel GI is an explicitly promoted Compute adopter. It can select an alternate Compute family only for the
// graph-owned output-clear/compute chain; the compiler remains responsible for rejecting an undeclared resource
// sharing contract or lowering the required exclusive ownership transfer.
TEST(EcsGraphics, SurfelGiPermitsOptInCrossFamilyComputeRouting){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString surfelGiSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_surfel_gi.cpp", surfelGiSource));
    const AStringView surfelGi(surfelGiSource.data(), surfelGiSource.size());

    EXPECT_TRUE(ContainsText(surfelGi, "#include <impl/ecs_render/raytrace/task_graph_surfel_tasks.h>"));
    EXPECT_TRUE(ContainsText(surfelGi, "EnableSameFamilyComputeEffectRouting(surfelIrradianceClearScheduling, false)"));
    EXPECT_TRUE(ContainsText(surfelGi, "EnableCrossFamilyComputeEffectRouting(surfelIrradianceClearScheduling)"));
    EXPECT_TRUE(ContainsText(surfelGi, "EnableSameFamilyComputeEffectRouting(surfelGiScheduling)"));
    EXPECT_TRUE(ContainsText(surfelGi, "EnableCrossFamilyComputeEffectRouting(surfelGiScheduling)"));
}


// The persistent counter crosses the Compute GI packet and optional Transfer readback tail.  Its next-frame
// imported cache must therefore be concurrently shared by each actual transport rather than retaining a stale
// exclusive Transfer owner with no future release destination.
TEST(EcsGraphics, SurfelCounterSharesComputeAndTransferReadbackPath){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString surfelSource;
    AString surfelTaskGraphSource;
    AString systemSource;
    AString rayTracingSystemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_surfel_gi.cpp", surfelSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_surfel_gi.cpp", surfelTaskGraphSource));
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSystemSource));
    const AStringView surfel(surfelSource.data(), surfelSource.size());
    const AStringView surfelTaskGraph(surfelTaskGraphSource.data(), surfelTaskGraphSource.size());
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView rayTracingSystem(rayTracingSystemSource.data(), rayTracingSystemSource.size());

    const usize counterOffset = surfel.find("if(!m_rayTracingState.m_surfelCounterBuffer){");
    const usize traceArgsOffset = surfel.find("// Build-args rewrites the indirect dispatch buffer each frame.", counterOffset);
    ASSERT_NE(counterOffset, AStringView::npos);
    ASSERT_NE(traceArgsOffset, AStringView::npos);
    ASSERT_LT(counterOffset, traceArgsOffset);
    const AStringView counter = surfel.substr(counterOffset, traceArgsOffset - counterOffset);
    EXPECT_TRUE(ContainsText(counter, ".setCanHaveUAVs(true)"));
    EXPECT_TRUE(ContainsText(counter, ".setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)"));
    EXPECT_TRUE(ContainsText(counter, ".setDebugName(Name(\"surfel_counter\"))"));

    const usize readbackOffset = surfelTaskGraph.find("void RendererFramePipeline::declareDeferredSurfelCountReadbackTask");
    ASSERT_NE(readbackOffset, AStringView::npos);
    const AStringView readback = surfelTaskGraph.substr(readbackOffset);
    EXPECT_TRUE(ContainsText(readback, "rayTracingSurfelResources.counterBuffer"));
    EXPECT_TRUE(ContainsText(readback, ".source = counter,"));
    EXPECT_TRUE(ContainsText(readback, ".setQueue(TransferQueueRequest())"));
    EXPECT_FALSE(ContainsText(readback, ".acceptedToken ="));

    EXPECT_TRUE(ContainsText(surfelTaskGraph, ".states = m_surfelGiCounterPersistentState.source(),"));
    EXPECT_TRUE(ContainsText(system, "m_surfelGiCounterPersistentState.buildFilteredBufferSubset("));
    EXPECT_TRUE(ContainsText(system, "m_surfelGiCounterPersistentState.commit(\n                    *context->candidate"));
    EXPECT_TRUE(ContainsText(system, ".task = m_deferredSurfelGiCounterReadbackTask,"));
    EXPECT_TRUE(ContainsText(system, "scratchArena,\n                nullptr,\n                &readbackAcceptedCallback"));
    EXPECT_TRUE(ContainsText(
        system,
        "if(context->acceptedStateReady)\n"
        "                    context->renderer->m_raytracingSystem.confirmSurfelCountReadbackSubmission(token);"
    ));
    EXPECT_TRUE(ContainsText(rayTracingSystem, "m_rayTracingState.m_surfelCountReadbackSubmissionToken = submissionToken;"));
    const usize readbackSubmitOffset = system.find("const bool readbackAccepted = submitter.recordAndSubmitTask(");
    const usize readbackTokenOffset = system.find(
        "const Core::QueueSubmissionToken readbackSubmissionToken =",
        readbackSubmitOffset
    );
    ASSERT_NE(readbackSubmitOffset, AStringView::npos);
    ASSERT_NE(readbackTokenOffset, AStringView::npos);
    EXPECT_LT(readbackSubmitOffset, readbackTokenOffset);
    EXPECT_TRUE(ContainsText(system, "else if(!readbackAccepted || !readbackContext.acceptedStateReady){"));
}


// The full irradiance clear is deliberately renderer-local: the generic helper conservatively declares Graphics
// for render-pass lowering, while this native clear is constrained to the direct Compute GI packet and captures the
// same typed command-IR record after the graph-owned CopyDest transition.
TEST(EcsGraphics, SurfelIrradianceClearUsesComputeGraphCallback){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString surfelTasksSource;
    AString surfelGiSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "task_graph_surfel_tasks.cpp", surfelTasksSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_surfel_gi.cpp", surfelGiSource));
    const AStringView callback(surfelTasksSource.data(), surfelTasksSource.size());
    const AStringView surfelGi(surfelGiSource.data(), surfelGiSource.size());

    EXPECT_TRUE(ContainsText(callback, "context.taskGraph.textureForResource(payload.destination)"));
    EXPECT_TRUE(ContainsText(callback, "if(!destination || commandList.isRenderPassActive())"));
    EXPECT_FALSE(ContainsText(callback, "endRenderPass()"));
    EXPECT_TRUE(ContainsText(callback, "Core::GpuClearTextureTaskDesc clearDesc{"));
    EXPECT_TRUE(ContainsText(callback, ".destination = payload.destination,"));
    EXPECT_TRUE(ContainsText(callback, ".subresources = s_FramebufferSubresources,"));
    EXPECT_TRUE(ContainsText(callback, ".valueType = Core::GpuClearTextureTaskValueType::Float,"));
    EXPECT_TRUE(ContainsText(callback, ".floatValue = Core::Color(0.f, 0.f, 0.f, 0.f),"));
    EXPECT_TRUE(ContainsText(callback, "context.commandIrCapture"));
    EXPECT_TRUE(ContainsText(callback, "captureClearTexture("));
    EXPECT_TRUE(ContainsText(callback, "commandList.clearTextureFloat(destination, clearDesc.subresources, clearDesc.floatValue);"));

    const usize resourceUseOffset = surfelGi.find("const Core::GpuTaskResourceUse surfelIrradianceClearResourceUse");
    const usize giSchedulingOffset = surfelGi.find("Core::GpuTaskSchedulingHint surfelGiScheduling", resourceUseOffset);
    ASSERT_NE(resourceUseOffset, AStringView::npos);
    ASSERT_NE(giSchedulingOffset, AStringView::npos);
    ASSERT_LT(resourceUseOffset, giSchedulingOffset);
    const AStringView irradianceClear = surfelGi.substr(resourceUseOffset, giSchedulingOffset - resourceUseOffset);

    EXPECT_TRUE(ContainsText(irradianceClear, "WriteTextureUse(\n        surfelIrradiance,\n        ECSRenderDetail::s_FramebufferSubresources,\n        Core::ResourceStates::CopyDest\n    )"));
    EXPECT_TRUE(ContainsText(irradianceClear, ".setQueue(ComputePacketQueueRequest())"));
    EXPECT_TRUE(ContainsText(irradianceClear, ".setResourceUses(&surfelIrradianceClearResourceUse, 1u)"));
    EXPECT_TRUE(ContainsText(irradianceClear, "addTask<ECSRenderDetail::SurfelIrradianceClearGraphTask>("));
    EXPECT_FALSE(ContainsText(irradianceClear, "addClearTextureTask("));
}


// The retained monolithic soft-shadow route must clear all-lit visibility on the selected Compute packet. Its
// renderer-local callback retains typed command-IR capture while avoiding the generic clear helper's Graphics path.
TEST(EcsGraphics, ShadowVisibilityAllLitClearUsesComputeGraphCallback){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString allLitClearTaskSource;
    AString shadowVisibilityTaskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "task_graph_shadow_visibility_tasks.cpp", allLitClearTaskSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp", shadowVisibilityTaskGraphSource));
    const AStringView callback(allLitClearTaskSource.data(), allLitClearTaskSource.size());
    const AStringView shadowVisibility(shadowVisibilityTaskGraphSource.data(), shadowVisibilityTaskGraphSource.size());

    EXPECT_TRUE(ContainsText(callback, "context.taskGraph.textureForResource(payload.destination)"));
    EXPECT_TRUE(ContainsText(callback, "if(!destination || commandList.isRenderPassActive())"));
    EXPECT_FALSE(ContainsText(callback, "endRenderPass()"));
    EXPECT_TRUE(ContainsText(callback, "Core::GpuClearTextureTaskDesc clearDesc{"));
    EXPECT_TRUE(ContainsText(callback, ".destination = payload.destination,"));
    EXPECT_TRUE(ContainsText(callback, ".subresources = s_ShadowVisibilitySubresources,"));
    EXPECT_TRUE(ContainsText(callback, ".valueType = Core::GpuClearTextureTaskValueType::Float,"));
    EXPECT_TRUE(ContainsText(callback, ".floatValue = Core::Color(1.f, 1.f, 1.f, 1.f),"));
    EXPECT_TRUE(ContainsText(callback, "context.commandIrCapture"));
    EXPECT_TRUE(ContainsText(callback, "captureClearTexture("));
    EXPECT_TRUE(ContainsText(callback, "commandList.clearTextureFloat(destination, clearDesc.subresources, clearDesc.floatValue);"));

    const usize resourceUseOffset = shadowVisibility.find("const Core::GpuTaskResourceUse allLitClearResourceUse");
    const usize shadowSchedulingOffset = shadowVisibility.find("Core::GpuTaskSchedulingHint scheduling;", resourceUseOffset);
    ASSERT_NE(resourceUseOffset, AStringView::npos);
    ASSERT_NE(shadowSchedulingOffset, AStringView::npos);
    ASSERT_LT(resourceUseOffset, shadowSchedulingOffset);
    const AStringView allLitClear = shadowVisibility.substr(resourceUseOffset, shadowSchedulingOffset - resourceUseOffset);

    EXPECT_TRUE(ContainsText(allLitClear, "WriteTextureUse(\n        shadowVisibility,\n        ECSRenderDetail::s_ShadowVisibilitySubresources,\n        Core::ResourceStates::CopyDest\n    )"));
    EXPECT_TRUE(ContainsText(allLitClear, ".setQueue(ComputePacketQueueRequest())"));
    EXPECT_TRUE(ContainsText(allLitClear, ".setResourceUses(&allLitClearResourceUse, 1u)"));
    EXPECT_TRUE(ContainsText(allLitClear, "m_deferredLightingTaskGraph.addTask<"));
    EXPECT_TRUE(ContainsText(allLitClear, "ECSRenderDetail::ShadowVisibilityAllLitClearGraphTask"));
    EXPECT_FALSE(ContainsText(allLitClear, "addClearTextureTask("));
}


// Hardware Caustics is a separate Graphics-capable effect chain. Its clear and every independently created
// temporal-accumulator prefix must carry the explicit cross-family opt-in so copied photon/resolve schedules
// retain one selected physical Graphics queue without making a windowed present eligible for that queue.
TEST(EcsGraphics, HardwareCausticsPermitsOptInCrossFamilyGraphicsRouting){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "renderer_frame_pipeline_graph.cpp" }, taskGraphSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    const usize lightingOffset = taskGraph.find("void RendererFramePipeline::buildDeferredLightingTaskGraph");
    ASSERT_NE(lightingOffset, AStringView::npos);
    const AStringView lighting = taskGraph.substr(lightingOffset);

    EXPECT_TRUE(ContainsText(lighting, "EnableCrossFamilyComputeEffectRouting(hardwareScheduling)"));
    EXPECT_TRUE(ContainsText(lighting, "EnableCrossFamilyComputeEffectRouting(irradianceClearScheduling)"));
    EXPECT_TRUE(ContainsText(lighting, "EnableCrossFamilyComputeEffectRouting(accumulatorBootstrapClearScheduling)"));
    EXPECT_TRUE(ContainsText(lighting, "EnableCrossFamilyComputeEffectRouting(accumulatorDecayScheduling)"));
}


// Caustic resolve targets start Unknown after recreation. Geometry downsample and prepare must therefore publish
// their first results as writes, while a warm hardware accumulator imports only accepted Graphics packet state.
TEST(EcsGraphics, CausticGraphScratchUsesFirstWritesAndHardwareRetainsAcceptedAccumulatorState){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    AString systemSource;
    AString systemHeaderSource;
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "renderer_frame_pipeline_graph_caustics.cpp",
            "renderer_frame_pipeline_graph_surfel_gi.cpp",
            "renderer_frame_pipeline_graph.cpp",
        },
        taskGraphSource
    ));
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", systemHeaderSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView systemHeader(systemHeaderSource.data(), systemHeaderSource.size());

    const usize softwareCausticsOffset = taskGraph.find("bool RendererFramePipeline::declareDeferredSoftwareCausticsTask");
    const usize surfelGiOffset = taskGraph.find("bool RendererFramePipeline::declareDeferredSurfelGiTask", softwareCausticsOffset);
    const usize deferredLightingOffset = taskGraph.find("void RendererFramePipeline::buildDeferredLightingTaskGraph", surfelGiOffset);
    const usize hardwareCausticsOffset = taskGraph.find("if(declaresHardwareCaustics){", deferredLightingOffset);
    const usize avboitOffset = taskGraph.find("AvboitPreGraphTask::Payload", hardwareCausticsOffset);
    ASSERT_NE(softwareCausticsOffset, AStringView::npos);
    ASSERT_NE(surfelGiOffset, AStringView::npos);
    ASSERT_NE(deferredLightingOffset, AStringView::npos);
    ASSERT_NE(hardwareCausticsOffset, AStringView::npos);
    ASSERT_NE(avboitOffset, AStringView::npos);
    ASSERT_LT(softwareCausticsOffset, surfelGiOffset);
    ASSERT_LT(surfelGiOffset, deferredLightingOffset);
    ASSERT_LT(deferredLightingOffset, hardwareCausticsOffset);
    ASSERT_LT(hardwareCausticsOffset, avboitOffset);
    const AStringView softwareCaustics = taskGraph.substr(softwareCausticsOffset, surfelGiOffset - softwareCausticsOffset);
    const AStringView hardwareCaustics = taskGraph.substr(hardwareCausticsOffset, avboitOffset - hardwareCausticsOffset);

    const usize softwareGeometryOffset = softwareCaustics.find("geometryResourceUses.push_back(ReadTextureUse(");
    const usize softwarePrepareOffset = softwareCaustics.find("constexpr bool s_CausticResolvePrepareWritesHalf", softwareGeometryOffset);
    const usize softwareUpsampleOffset = softwareCaustics.find("resolveUpsampleResourceUses.push_back(", softwarePrepareOffset);
    const usize hardwareGeometryOffset = hardwareCaustics.find("hardwareGeometryResourceUses.push_back(ReadTextureUse(");
    const usize hardwarePrepareOffset = hardwareCaustics.find("constexpr bool s_HardwareCausticResolvePrepareWritesHalf", hardwareGeometryOffset);
    const usize hardwareUpsampleOffset = hardwareCaustics.find("hardwareResolveUpsampleResourceUses.push_back(", hardwarePrepareOffset);
    ASSERT_NE(softwareGeometryOffset, AStringView::npos);
    ASSERT_NE(softwarePrepareOffset, AStringView::npos);
    ASSERT_NE(softwareUpsampleOffset, AStringView::npos);
    ASSERT_NE(hardwareGeometryOffset, AStringView::npos);
    ASSERT_NE(hardwarePrepareOffset, AStringView::npos);
    ASSERT_NE(hardwareUpsampleOffset, AStringView::npos);
    ASSERT_LT(softwareGeometryOffset, softwarePrepareOffset);
    ASSERT_LT(softwarePrepareOffset, softwareUpsampleOffset);
    ASSERT_LT(hardwareGeometryOffset, hardwarePrepareOffset);
    ASSERT_LT(hardwarePrepareOffset, hardwareUpsampleOffset);
    const AStringView softwareGeometry = softwareCaustics.substr(
        softwareGeometryOffset,
        softwarePrepareOffset - softwareGeometryOffset
    );
    const AStringView softwarePrepare = softwareCaustics.substr(
        softwarePrepareOffset,
        softwareUpsampleOffset - softwarePrepareOffset
    );
    const AStringView hardwareGeometry = hardwareCaustics.substr(
        hardwareGeometryOffset,
        hardwarePrepareOffset - hardwareGeometryOffset
    );
    const AStringView hardwarePrepare = hardwareCaustics.substr(
        hardwarePrepareOffset,
        hardwareUpsampleOffset - hardwarePrepareOffset
    );

    EXPECT_TRUE(ContainsText(softwareGeometry, "geometryResourceUses.push_back(WriteTextureUse(\n        causticResolveGeometry,"));
    EXPECT_FALSE(ContainsText(softwareGeometry, "geometryResourceUses.push_back(ReadWriteTextureUse("));
    EXPECT_EQ(CountText(softwarePrepare, "resolvePrepareResourceUses.push_back(ReadTextureUse("), 2u);
    EXPECT_FALSE(ContainsText(softwarePrepare, "resolvePrepareResourceUses.push_back(ReadWriteTextureUse("));
    EXPECT_EQ(CountText(softwarePrepare, "resolvePrepareResourceUses.push_back(WriteTextureUse("), 2u);
    EXPECT_TRUE(ContainsText(hardwareGeometry, "hardwareGeometryResourceUses.push_back(WriteTextureUse(\n            causticResolveGeometry,"));
    EXPECT_FALSE(ContainsText(hardwareGeometry, "hardwareGeometryResourceUses.push_back(ReadWriteTextureUse("));
    EXPECT_EQ(CountText(hardwarePrepare, "hardwareResolvePrepareResourceUses.push_back(ReadTextureUse("), 2u);
    EXPECT_FALSE(ContainsText(hardwarePrepare, "hardwareResolvePrepareResourceUses.push_back(ReadWriteTextureUse("));
    EXPECT_EQ(CountText(hardwarePrepare, "hardwareResolvePrepareResourceUses.push_back(WriteTextureUse("), 2u);

    const usize hardwareLifecycleOffset = system.find("struct HardwareCausticsStateLifecycleContext{");
    const usize deferredLightingLifecycleOffset = system.find(
        "const Core::TextureHandle deferredLightingShadowReturnTextures[]",
        hardwareLifecycleOffset
    );
    ASSERT_NE(hardwareLifecycleOffset, AStringView::npos);
    ASSERT_NE(deferredLightingLifecycleOffset, AStringView::npos);
    ASSERT_LT(hardwareLifecycleOffset, deferredLightingLifecycleOffset);
    const AStringView hardwareLifecycle = system.substr(
        hardwareLifecycleOffset,
        deferredLightingLifecycleOffset - hardwareLifecycleOffset
    );

    EXPECT_TRUE(ContainsText(system, "m_hardwareCausticAccumulatorPersistentState(arena)"));
    EXPECT_TRUE(ContainsText(system, "m_hardwareCausticAccumulatorPersistentState.reset();"));
    EXPECT_EQ(CountText(system, "m_hardwareCausticAccumulatorPersistentState.reset();"), 1u);
    EXPECT_TRUE(ContainsText(systemHeader, "Core::GpuPersistentResourceStateCache m_hardwareCausticAccumulatorPersistentState;"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, "const Core::GpuTaskExternalStateSource accumulatorStateSources[]"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, ".states = m_hardwareCausticAccumulatorPersistentState.source(),"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, "m_hardwareCausticAccumulatorPersistentState.valid()"));
    EXPECT_EQ(
        CountText(
            hardwareCaustics,
            ".setExternalStateSources(accumulatorStateSources, accumulatorStateSourceCount)"
        ),
        3u
    );
    EXPECT_FALSE(ContainsText(system, "deferredStateBindings"));
    EXPECT_FALSE(ContainsText(system, "m_causticIrradianceLightingState"));
    EXPECT_TRUE(ContainsText(hardwareLifecycle, "prepareHardwareCausticsTask"));
    EXPECT_TRUE(ContainsText(hardwareLifecycle, "m_hardwareCausticAccumulatorPersistentState.buildFilteredResourceSubset("));
    EXPECT_TRUE(ContainsText(hardwareLifecycle, "acceptHardwareCausticsTask"));
    EXPECT_TRUE(ContainsText(hardwareLifecycle, "m_hardwareCausticAccumulatorPersistentState.commit("));
    EXPECT_FALSE(ContainsText(hardwareLifecycle, "replaceTextureSubset("));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredHardwareCausticsTask,\n"
        "            .context = &hardwareCausticsStateLifecycle,\n"
        "            .invoke = prepareHardwareCausticsTask,"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredHardwareCausticsTask,\n"
        "            .context = &hardwareCausticsStateLifecycle,\n"
        "            .invoke = acceptHardwareCausticsTask,"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "(selectedCausticsSubmissionToken.valid() && !selectedCausticsStateReady)"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "if(!selectedCausticsSubmissionToken.valid())\n"
        "                    restoreCausticsCpuState();"
    ));
}


// FrontierSafe normally closes a packet at a cross-queue consumer. These direct serial effect chains instead own
// one timing/acceptance packet, so every accumulator alternative and semantic tail must opt in explicitly.
TEST(EcsGraphics, FrontierSafeEffectChainsRetainTheirSemanticPackets){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString softwareCausticsSource;
    AString surfelGiSource;
    AString deferredLightingSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_caustics.cpp", softwareCausticsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_surfel_gi.cpp", surfelGiSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp", deferredLightingSource));
    const AStringView softwareCaustics(softwareCausticsSource.data(), softwareCausticsSource.size());
    const AStringView surfelGi(surfelGiSource.data(), surfelGiSource.size());
    const AStringView hardwareCaustics(deferredLightingSource.data(), deferredLightingSource.size());
    const AStringView avboitOccupancy(deferredLightingSource.data(), deferredLightingSource.size());
    const AStringView avboitAccumulation(deferredLightingSource.data(), deferredLightingSource.size());

    EXPECT_TRUE(ContainsText(softwareCaustics, "accumulatorNonTemporalClearScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(softwareCaustics, "accumulatorBootstrapClearScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(softwareCaustics, "accumulatorDecayScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(softwareCaustics, "causticsScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(softwareCaustics, ".setDependencies(&causticsDependency, 1u)"));
    EXPECT_TRUE(ContainsText(softwareCaustics, "render.software_caustics.resolve_timing_close"));

    EXPECT_TRUE(ContainsText(surfelGi, "surfelGiScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(surfelGi, ".setDependencies(&surfelGiDependency, 1u)"));
    EXPECT_TRUE(ContainsText(surfelGi, "render.surfel_gi"));

    EXPECT_TRUE(ContainsText(hardwareCaustics, "accumulatorNonTemporalClearScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, "accumulatorBootstrapClearScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, "accumulatorDecayScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, "hardwareCausticsScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, ".setDependencies(&causticsDependency, 1u)"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, "render.hardware_caustics.resolve_timing_close"));

    EXPECT_TRUE(ContainsText(avboitOccupancy, "avboitClearScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(avboitOccupancy, "avboitOccupancyScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(avboitOccupancy, ".setDependencies(&occupancyDependency, 1u)"));
    EXPECT_TRUE(ContainsText(avboitAccumulation, "accumulationFinalizeScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(
        avboitAccumulation,
        ".setDependencies(&m_avboitSystem.taskGraphStage().m_accumulationTask, 1u)"
    ));
}


// A fully prepared soft-transparent frame selects the split graph route from production state alone. The retained
// monolithic callback remains a natural compatibility fallback, not a behavior-selectable benchmark arm.
TEST(EcsGraphics, SoftTransparentFoldHasNoProductionTestControl){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemHeaderSource;
    AString systemSource;
    AString shadowVisibilitySource;
    AString smokeCmakeSource;
    AString stressTestProjectSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", systemHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.cpp", systemSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp",
        shadowVisibilitySource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "CMakeLists.txt", smokeCmakeSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "stress_test_project.cpp", stressTestProjectSource));
    const AStringView systemHeader(systemHeaderSource.data(), systemHeaderSource.size());
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView shadowVisibility(shadowVisibilitySource.data(), shadowVisibilitySource.size());
    const AStringView smokeCmake(smokeCmakeSource.data(), smokeCmakeSource.size());
    const AStringView stressTestProject(stressTestProjectSource.data(), stressTestProjectSource.size());

    EXPECT_TRUE(ContainsText(
        shadowVisibility,
        "const bool splitSoftTransparentFold = preparedSoftTransparentFoldCandidate;"
    ));
    EXPECT_TRUE(ContainsText(shadowVisibility, "if(splitSoftTransparentFold){"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "Core::GpuTaskId shadowVisibilityDependency = prefixTask;"));
    EXPECT_FALSE(ContainsText(shadowVisibility, "graphOwnedSoftTransparentFoldEnabled"));
    EXPECT_FALSE(ContainsText(shadowVisibility, "SoftTransparentShadowFoldEnabledForTesting"));
    EXPECT_FALSE(ContainsText(shadowVisibility, "soft-transparent shadow-fold benchmark path active"));
    EXPECT_FALSE(ContainsText(systemHeader, "setGraphOwnedSoftTransparentShadowFoldEnabledForTesting"));
    EXPECT_FALSE(ContainsText(systemHeader, "m_graphOwnedSoftTransparentShadowFoldEnabledForTesting"));
    EXPECT_FALSE(ContainsText(systemHeader, "m_graphOwnedSoftTransparentShadowFoldBenchmarkForTesting"));
    EXPECT_FALSE(ContainsText(systemHeader, "m_reportedGraphOwnedSoftTransparentShadowFoldBenchmarkForTesting"));
    EXPECT_FALSE(ContainsText(system, "setGraphOwnedSoftTransparentShadowFoldEnabledForTesting"));
    EXPECT_FALSE(ContainsText(smokeCmake, "nwb_soft_transparent_shadow_fold_graph_benchmark"));
    EXPECT_FALSE(ContainsText(smokeCmake, "nwb_soft_transparent_shadow_fold_monolithic_benchmark"));
    EXPECT_FALSE(ContainsText(smokeCmake, "NWB_SOFT_TRANSPARENT_SHADOW_FOLD_BENCHMARK"));
    EXPECT_FALSE(ContainsText(smokeCmake, "NWB_SOFT_TRANSPARENT_SHADOW_FOLD_MONOLITHIC_BENCHMARK"));
    EXPECT_FALSE(ContainsText(stressTestProject, "NWB_SOFT_TRANSPARENT_SHADOW_FOLD_BENCHMARK"));
    EXPECT_FALSE(ContainsText(stressTestProject, "NWB_SOFT_TRANSPARENT_SHADOW_FOLD_MONOLITHIC_BENCHMARK"));
    EXPECT_FALSE(ContainsText(
        stressTestProject,
        "StressTestSmokeProject: enabled graph-owned soft-transparent shadow-fold benchmark"
    ));
    EXPECT_FALSE(ContainsText(
        stressTestProject,
        "StressTestSmokeProject: enabled retained monolithic soft-transparent shadow-fold benchmark"
    ));
    EXPECT_FALSE(ContainsText(stressTestProject, "NWB Soft Transparent Shadow Fold Benchmark"));
}


// A retained generated-vertex output needs an explicit graph phase for every producer/raster handoff. Keep the
// narrow fifth regular draw visible rather than allowing it to fall through to a callback-local compatibility path.
TEST(EcsGraphics, SharedComputeEmulationRetainsFiveRegularDraws){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString sharedTaskGraphStageSource;
    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "task_graph_stage.h", sharedTaskGraphStageSource));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "renderer_frame_pipeline_graphics_prefix.cpp",
            "renderer_frame_pipeline_graph.cpp",
        },
        taskGraphSource
    ));
    const AStringView sharedTaskGraphStage(sharedTaskGraphStageSource.data(), sharedTaskGraphStageSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    EXPECT_TRUE(ContainsText(sharedTaskGraphStage, "s_SharedComputeEmulationMaximumDrawCount = 5u;"));
    EXPECT_TRUE(ContainsText(
        sharedTaskGraphStage,
        "s_SharedComputeEmulationMaximumPhaseCount =\n"
        "    s_SharedComputeEmulationMaximumDrawCount * s_SharedComputeEmulationPhasesPerDraw;"
    ));

    const AStringView fifthPhaseIdentities[] = {
        "render.graphics_prefix.opaque_shared_compute_emulation_generate_e",
        "render.graphics_prefix.opaque_shared_compute_emulation_raster_e",
        "render.avboit.occupancy.shared_compute_emulation_generate_e",
        "render.avboit.occupancy.shared_compute_emulation_raster_e",
        "render.avboit.extinction.shared_compute_emulation_generate_e",
        "render.avboit.extinction.shared_compute_emulation_raster_e",
        "render.avboit.accumulation.shared_compute_emulation_generate_e",
        "render.avboit.accumulation.shared_compute_emulation_raster_e",
    };
    for(const AStringView identity : fifthPhaseIdentities)
        EXPECT_TRUE(ContainsText(taskGraph, identity));

    const AStringView fifthPhaseMarkers[] = {
        "Opaque Shared Compute Emulation Generate E",
        "Opaque Shared Compute Emulation Raster E",
        "AVBOIT Occupancy Shared Compute Emulation Generate E",
        "AVBOIT Occupancy Shared Compute Emulation Raster E",
        "AVBOIT Extinction Shared Compute Emulation Generate E",
        "AVBOIT Extinction Shared Compute Emulation Raster E",
        "AVBOIT Accumulation Shared Compute Emulation Generate E",
        "AVBOIT Accumulation Shared Compute Emulation Raster E",
    };
    for(const AStringView marker : fifthPhaseMarkers)
        EXPECT_TRUE(ContainsText(taskGraph, marker));
}


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
    EXPECT_TRUE(ContainsText(resourceSets, "ForEachMaterialPassMeshSourceBuffer(mesh,"));
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


// Shadow Visibility has both a fully split soft-transparent route and a retained monolithic compatibility route.
// Each graph-owned chain may choose an alternate Compute family, while its direct successors retain that physical
// queue and the explicit primary-Graphics presentation guard remains outside this effect.
TEST(EcsGraphics, ShadowVisibilityPermitsOptInCrossFamilyComputeRouting){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString shadowVisibilitySource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp", shadowVisibilitySource));
    const AStringView shadowVisibility(shadowVisibilitySource.data(), shadowVisibilitySource.size());

    EXPECT_TRUE(ContainsText(shadowVisibility, "EnableCrossFamilyComputeEffectRouting(opaqueScheduling)"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "EnableCrossFamilyComputeEffectRouting(tailScheduling)"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "EnableCrossFamilyComputeEffectRouting(primitiveScheduling)"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "EnableCrossFamilyComputeEffectRouting(allLitClearScheduling)"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "EnableCrossFamilyComputeEffectRouting(scheduling)"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "EnableCrossFamilyComputeEffectRouting(statsReadbackScheduling)"));
}


// Split shadow callbacks must declare only resources their native body touches. Fresh retained scratch stays
// Unknown until a graph writer publishes it, while an accepted temporal history remains a real sampled input.
TEST(EcsGraphics, SplitShadowVisibilityKeepsFreshScratchAsFirstWrites){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString shadowVisibilitySource;
    AString shadowSource;
    AString softShadowSource;
    AString rayTracingSystemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp", shadowVisibilitySource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_shadow.cpp", shadowSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_softshadow.cpp", softShadowSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSystemSource));
    const AStringView shadowVisibility(shadowVisibilitySource.data(), shadowVisibilitySource.size());
    const AStringView shadowSourceView(shadowSource.data(), shadowSource.size());
    const AStringView softShadowSourceView(softShadowSource.data(), softShadowSource.size());
    const AStringView rayTracingSystem(rayTracingSystemSource.data(), rayTracingSystemSource.size());

    const usize opaqueResourcesOffset = shadowVisibility.find("opaqueResourceUses.reserve(");
    const usize opaqueFirstWaveletOffset = shadowVisibility.find("opaqueFirstWaveletResourceUses.reserve(", opaqueResourcesOffset);
    const usize opaqueDescOffset = shadowVisibility.find("Core::GpuTaskDesc opaqueDesc;", opaqueFirstWaveletOffset);
    const usize traceUsesOffset = shadowVisibility.find("transparentTraceResourceUses.reserve(", opaqueFirstWaveletOffset);
    const usize transparentHistoryOffset = shadowVisibility.find("graphOwnsTransparentTemporalMergeEntryStates =", traceUsesOffset);
    ASSERT_NE(opaqueResourcesOffset, AStringView::npos);
    ASSERT_NE(opaqueFirstWaveletOffset, AStringView::npos);
    ASSERT_NE(opaqueDescOffset, AStringView::npos);
    ASSERT_NE(traceUsesOffset, AStringView::npos);
    ASSERT_NE(transparentHistoryOffset, AStringView::npos);
    ASSERT_LT(opaqueResourcesOffset, opaqueFirstWaveletOffset);
    ASSERT_LT(opaqueFirstWaveletOffset, opaqueDescOffset);
    ASSERT_LT(traceUsesOffset, transparentHistoryOffset);
    const AStringView opaqueResources = shadowVisibility.substr(
        opaqueResourcesOffset,
        opaqueFirstWaveletOffset - opaqueResourcesOffset
    );
    const AStringView opaqueDesc = shadowVisibility.substr(opaqueDescOffset, traceUsesOffset - opaqueDescOffset);
    const AStringView transparentTraceUses = shadowVisibility.substr(
        traceUsesOffset,
        transparentHistoryOffset - traceUsesOffset
    );

    EXPECT_TRUE(ContainsText(opaqueResources, "WriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess)"));
    EXPECT_TRUE(ContainsText(opaqueResources, "WriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess)"));
    EXPECT_TRUE(ContainsText(opaqueResources, "WriteUse(shadowSoftGeometry, Core::ResourceStates::UnorderedAccess)"));
    EXPECT_TRUE(ContainsText(opaqueResources, "if(hardwareShadowSupported){"));
    EXPECT_TRUE(ContainsText(opaqueResources, "ReadUse(sceneTlas, Core::ResourceStates::AccelStructRead)"));
    EXPECT_TRUE(ContainsText(opaqueResources, "}else{\n            opaqueResourceUses.push_back(ReadUse(sceneBvhNodes"));
    EXPECT_FALSE(ContainsText(opaqueResources, "shadowCoarseTransmittance"));
    EXPECT_FALSE(ContainsText(opaqueResources, "shadowSoftHalfB"));
    EXPECT_FALSE(ContainsText(opaqueResources, "shadowHistA"));
    EXPECT_FALSE(ContainsText(opaqueResources, "transparentSoftHalf"));
    EXPECT_TRUE(ContainsText(opaqueDesc, ".setResourceUses(opaqueResourceUses.data(), opaqueResourceUses.size())"));
    EXPECT_TRUE(ContainsText(opaqueDesc, "!hardwareShadowSupported && softwareTraceGeometryStatesGraphOwned"));

    EXPECT_TRUE(ContainsText(transparentTraceUses, "WriteUse(transparentSoftHalf, Core::ResourceStates::UnorderedAccess)"));
    EXPECT_FALSE(ContainsText(transparentTraceUses, "ReadWriteUse(shadowVisibility"));
    EXPECT_FALSE(ContainsText(transparentTraceUses, "shadowCoarseTransmittance"));
    EXPECT_FALSE(ContainsText(transparentTraceUses, "ReadWriteUse(shadowSoftHalfA"));
    EXPECT_FALSE(ContainsText(transparentTraceUses, "ReadWriteUse(shadowSoftHalfB"));

    EXPECT_TRUE(ContainsText(shadowVisibility, "const bool softShadowHistoryReadable = rayTracingPlan.softShadowHistoryReadable;"));
    EXPECT_TRUE(ContainsText(rayTracingSystem, "state.m_softShadowTemporalReady\n            && state.m_prevWorldToClipValid\n            && state.m_softShadowTemporalSeeded"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "if(softShadowHistoryReadable){\n                opaqueFirstWaveletResourceUses.push_back(ReadUse(shadowSoftGeometryPrevious"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "if(softShadowHistoryReadable){\n                transparentTemporalMergeResourceUses.push_back(ReadUse(shadowSoftGeometryPrevious"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "WriteUse(transparentHistoryOut, Core::ResourceStates::UnorderedAccess)"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "splitSoftTransparentFold\n            || appendOptionalWriteTexture("));

    const usize softwareVisibilityOffset = shadowSourceView.find("bool RendererRayTracingSystem::renderGpuBvhShadowVisibility(");
    const usize softwareOpaqueOffset = shadowSourceView.find("bool RendererRayTracingSystem::renderGpuBvhShadowVisibilityOpaque(", softwareVisibilityOffset);
    ASSERT_NE(softwareVisibilityOffset, AStringView::npos);
    ASSERT_NE(softwareOpaqueOffset, AStringView::npos);
    ASSERT_LT(softwareVisibilityOffset, softwareOpaqueOffset);
    const AStringView softwareVisibility = shadowSourceView.substr(softwareVisibilityOffset, softwareOpaqueOffset - softwareVisibilityOffset);
    const usize adaptiveOffset = softwareVisibility.find("if(!softTransparentRan && m_rayTracingState.m_swShadowAdaptiveEnabled)");
    ASSERT_NE(adaptiveOffset, AStringView::npos);
    const AStringView preAdaptive = softwareVisibility.substr(0u, adaptiveOffset);
    const AStringView adaptive = softwareVisibility.substr(adaptiveOffset);
    EXPECT_FALSE(ContainsText(preAdaptive, "targets.shadowCoarseTransmittance.get()"));
    EXPECT_TRUE(ContainsText(adaptive, "commandList.setEnableUavBarriersForTexture(targets.shadowCoarseTransmittance.get(), true)"));
    EXPECT_TRUE(ContainsText(adaptive, "commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess)"));

    const usize softDispatchOffset = softShadowSourceView.find("void RendererRayTracingSystem::dispatchSoftShadowDenoiseAndTransparentFold(");
    ASSERT_NE(softDispatchOffset, AStringView::npos);
    const AStringView softDispatch = softShadowSourceView.substr(softDispatchOffset);
    EXPECT_TRUE(ContainsText(softDispatch, "const bool temporalHistoryReadable = softShadowTemporalHistoryUsable();"));
    EXPECT_TRUE(ContainsText(softDispatch, "if(temporalHistoryReadable){\n                commandList.setTextureState(resources.historyIn"));
    EXPECT_TRUE(ContainsText(softDispatch, "if(temporalHistoryReadable)\n                commandList.setTextureState(targets.shadowSoftGeometryPrev"));
}


// Both software-shadow recording routes publish the same production-owned one-shot diagnostic. The split
// transparent trace reports only after recording its dispatch, while the retained monolithic path shares it.
TEST(EcsGraphics, SoftwareShadowTraversalDiagnosticCoversSplitAndMonolithicRoutes){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString shadowSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_shadow.cpp", shadowSource));
    const AStringView shadow(shadowSource.data(), shadowSource.size());

    const usize splitTraceOffset = shadow.find("bool RendererRayTracingSystem::renderSoftTransparentShadowTrace(");
    const usize reportOffset = shadow.find("void RendererRayTracingSystem::reportSoftwareShadowTraversal(", splitTraceOffset);
    const usize temporalMergeOffset = shadow.find("bool RendererRayTracingSystem::renderSoftTransparentShadowTemporalMerge(", reportOffset);
    const usize monolithicOffset = shadow.find("bool RendererRayTracingSystem::renderGpuBvhShadowVisibility(", temporalMergeOffset);
    const usize opaqueOffset = shadow.find("bool RendererRayTracingSystem::renderGpuBvhShadowVisibilityOpaque(", monolithicOffset);
    ASSERT_NE(splitTraceOffset, AStringView::npos);
    ASSERT_NE(reportOffset, AStringView::npos);
    ASSERT_NE(temporalMergeOffset, AStringView::npos);
    ASSERT_NE(monolithicOffset, AStringView::npos);
    ASSERT_NE(opaqueOffset, AStringView::npos);
    ASSERT_LT(splitTraceOffset, reportOffset);
    ASSERT_LT(reportOffset, temporalMergeOffset);
    ASSERT_LT(temporalMergeOffset, monolithicOffset);
    ASSERT_LT(monolithicOffset, opaqueOffset);

    const AStringView splitTrace = shadow.substr(splitTraceOffset, reportOffset - splitTraceOffset);
    const AStringView report = shadow.substr(reportOffset, temporalMergeOffset - reportOffset);
    const AStringView monolithic = shadow.substr(monolithicOffset, opaqueOffset - monolithicOffset);
    EXPECT_TRUE(ContainsText(splitTrace, "dispatchSoftShadowDenoiseAndTransparentFold("));
    EXPECT_TRUE(ContainsText(splitTrace, "reportSoftwareShadowTraversal(targets);\n    return true;"));
    EXPECT_TRUE(ContainsText(report, "if(m_rayTracingState.m_swShadowDispatchLogged)"));
    EXPECT_TRUE(ContainsText(report, "m_rayTracingState.m_swShadowDispatchLogged = true;"));
    EXPECT_TRUE(ContainsText(report, "RendererSystem: dispatched software shadow traversal"));
    EXPECT_EQ(CountText(monolithic, "reportSoftwareShadowTraversal(targets);"), 2u);
    EXPECT_FALSE(ContainsText(monolithic, "const auto logSoftwareShadowTraversal"));
}


// The retained monolithic callback owns later native scratch transitions, but its graph entry must still reflect
// each fresh target's first write. Only an accepted temporal history may enter as a sampled input.
TEST(EcsGraphics, MonolithicShadowVisibilityKeepsFreshScratchAsFirstWrites){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString shadowVisibilitySource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp", shadowVisibilitySource));
    const AStringView shadowVisibility(shadowVisibilitySource.data(), shadowVisibilitySource.size());

    const usize opaqueHistoryHelperOffset = shadowVisibility.find("const auto appendOptionalOpaqueTemporalHistoryTexture");
    const usize transparentHistoryHelperOffset = shadowVisibility.find(
        "const auto appendOptionalTransparentTemporalHistoryTexture",
        opaqueHistoryHelperOffset
    );
    const usize optionalImportsOffset = shadowVisibility.find("bool optionalResourcesImported =", transparentHistoryHelperOffset);
    const usize sceneTlasOffset = shadowVisibility.find("Core::GpuGraphResourceId sceneTlas;", optionalImportsOffset);
    ASSERT_NE(opaqueHistoryHelperOffset, AStringView::npos);
    ASSERT_NE(transparentHistoryHelperOffset, AStringView::npos);
    ASSERT_NE(optionalImportsOffset, AStringView::npos);
    ASSERT_NE(sceneTlasOffset, AStringView::npos);
    ASSERT_LT(opaqueHistoryHelperOffset, transparentHistoryHelperOffset);
    ASSERT_LT(transparentHistoryHelperOffset, optionalImportsOffset);
    ASSERT_LT(optionalImportsOffset, sceneTlasOffset);
    const AStringView opaqueHistoryHelper = shadowVisibility.substr(
        opaqueHistoryHelperOffset,
        transparentHistoryHelperOffset - opaqueHistoryHelperOffset
    );
    const AStringView transparentHistoryHelper = shadowVisibility.substr(
        transparentHistoryHelperOffset,
        optionalImportsOffset - transparentHistoryHelperOffset
    );
    const AStringView optionalImports = shadowVisibility.substr(optionalImportsOffset, sceneTlasOffset - optionalImportsOffset);

    EXPECT_TRUE(ContainsText(opaqueHistoryHelper, "else if(!splitSoftTransparentFold){"));
    EXPECT_TRUE(ContainsText(opaqueHistoryHelper, "if(softShadowHistoryReadable)\n                    resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));"));
    EXPECT_TRUE(ContainsText(opaqueHistoryHelper, "else if(rayTracingPlan.opaqueTemporalMergeReady)\n                resourceUses.push_back(WriteUse(resource, Core::ResourceStates::UnorderedAccess));"));
    EXPECT_TRUE(ContainsText(transparentHistoryHelper, "if(rayTracingPlan.transparentTemporalMergeReady){"));
    EXPECT_TRUE(ContainsText(transparentHistoryHelper, "if(softShadowHistoryReadable)\n                    resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));"));
    EXPECT_TRUE(ContainsText(transparentHistoryHelper, "resourceUses.push_back(WriteUse(resource, Core::ResourceStates::UnorderedAccess));"));

    EXPECT_TRUE(ContainsText(optionalImports, "appendOptionalWriteTexture(\n            deferredTargets.shadowSoftHalfA"));
    EXPECT_TRUE(ContainsText(optionalImports, "appendOptionalWriteTexture(\n            deferredTargets.shadowSoftHalfB"));
    EXPECT_TRUE(ContainsText(optionalImports, "appendOptionalWriteTexture(\n            deferredTargets.shadowSoftGeometry"));
    EXPECT_TRUE(ContainsText(optionalImports, "appendOptionalWriteTexture(\n            deferredTargets.transparentSoftHalf"));
    EXPECT_TRUE(ContainsText(optionalImports, "appendOptionalTransparentTemporalHistoryTexture(\n            deferredTargets.transparentHistA"));
    EXPECT_TRUE(ContainsText(optionalImports, "appendOptionalTransparentTemporalHistoryTexture(\n            deferredTargets.transparentHistB"));
    EXPECT_TRUE(ContainsText(optionalImports, "appendOptionalTransparentTemporalHistoryTexture(\n            deferredTargets.transparentMomentsA"));
    EXPECT_TRUE(ContainsText(optionalImports, "appendOptionalTransparentTemporalHistoryTexture(\n            deferredTargets.transparentMomentsB"));
    EXPECT_FALSE(ContainsText(optionalImports, "ReadWriteUse("));
}


// Temporal soft-shadow scratch stays private, but its accepted native state must seed the next shadow packet on
// either the Graphics fallback or dedicated Compute route. The separate return-state cache stays Compute-only.
TEST(EcsGraphics, ShadowTemporalScratchRetainsAcceptedStateAcrossGraphicsRoute){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    AString shadowVisibilitySource;
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp",
        shadowVisibilitySource
    ));
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView shadowVisibility(shadowVisibilitySource.data(), shadowVisibilitySource.size());

    EXPECT_TRUE(ContainsText(
        shadowVisibility,
        "const auto* const shadowScratchStates = m_shadowComputePersistentState.source();"
    ));
    EXPECT_TRUE(ContainsText(
        shadowVisibility,
        "const auto* const shadowReturnStates = m_shadowVisibilityReturnState.source();"
    ));
    EXPECT_TRUE(ContainsText(shadowVisibility, ".states = shadowScratchStates,"));
    EXPECT_TRUE(ContainsText(shadowVisibility, ".states = shadowReturnStates,"));
    EXPECT_TRUE(ContainsText(
        shadowVisibility,
        ".applicableConsumerQueueClass = Core::CommandQueue::Compute,"
    ));
    EXPECT_FALSE(ContainsText(shadowVisibility, "shadowVisibilityRunsOnCompute"));

    const usize acceptedShadowOffset = system.find("const Core::TextureHandle shadowVisibilityReturnTextures[]");
    const usize scratchStateOffset = system.find("m_shadowComputePersistentState.buildFilteredResourceSubset(", acceptedShadowOffset);
    const usize acceptedCallbackOffset = system.find("const auto acceptShadowVisibilityTask = [](", scratchStateOffset);
    const usize returnCommitOffset = system.find("m_shadowVisibilityReturnState.commit(", acceptedCallbackOffset);
    const usize scratchCommitOffset = system.find("m_shadowComputePersistentState.commit(", returnCommitOffset);
    const usize temporalFinalizeOffset = system.find(
        "m_raytracingSystem.finalizeSoftShadowTemporalHistory(*context->targets);",
        scratchCommitOffset
    );
    ASSERT_NE(acceptedShadowOffset, AStringView::npos);
    ASSERT_NE(scratchStateOffset, AStringView::npos);
    ASSERT_NE(acceptedCallbackOffset, AStringView::npos);
    ASSERT_NE(returnCommitOffset, AStringView::npos);
    ASSERT_NE(scratchCommitOffset, AStringView::npos);
    ASSERT_NE(temporalFinalizeOffset, AStringView::npos);
    EXPECT_LT(returnCommitOffset, scratchCommitOffset);
    EXPECT_LT(scratchCommitOffset, temporalFinalizeOffset);
    const AStringView acceptedShadow = system.substr(acceptedShadowOffset, temporalFinalizeOffset - acceptedShadowOffset);
    EXPECT_TRUE(ContainsText(acceptedShadow, "deferredTargets.shadowSoftGeometry,"));
    EXPECT_TRUE(ContainsText(acceptedShadow, "deferredTargets.shadowSoftGeometryPrev,"));
    EXPECT_TRUE(ContainsText(acceptedShadow, "m_shadowComputePersistentState.buildFilteredResourceSubset("));
    EXPECT_TRUE(ContainsText(acceptedShadow, "if(context->runsOnCompute){"));
    EXPECT_TRUE(ContainsText(acceptedShadow, "m_shadowVisibilityReturnState.buildFilteredResourceSubset("));
    EXPECT_TRUE(ContainsText(acceptedShadow, "context->renderer->m_shadowVisibilityReturnState.commit("));
    EXPECT_TRUE(ContainsText(acceptedShadow, "context->renderer->m_shadowComputePersistentState.commit("));
    EXPECT_TRUE(ContainsText(system, "finalizeSoftShadowTemporalHistory(*context->targets)"));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredShadowVisibilityTask,\n"
        "        .context = &shadowVisibilityStateLifecycle,\n"
        "        .invoke = prepareShadowVisibilityTask,"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredShadowVisibilityTask,\n"
        "        .context = &shadowVisibilityStateLifecycle,\n"
        "        .invoke = acceptShadowVisibilityTask,"
    ));
}


// Software-caustics scratch is private on both the dedicated Compute route and its legal Graphics fallback. Only
// the cross-queue irradiance return cache is route-conditional; all retained state publishes from the exact task.
TEST(EcsGraphics, SoftwareCausticsScratchRetainsAcceptedStateAcrossGraphicsRoute){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    AString causticsSource;
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_caustics.cpp",
        causticsSource
    ));
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView caustics(causticsSource.data(), causticsSource.size());

    EXPECT_TRUE(ContainsText(caustics, ".states = m_causticsComputePersistentState.source(),"));
    EXPECT_TRUE(ContainsText(caustics, ".states = m_causticIrradianceReturnState.source(),"));
    EXPECT_TRUE(ContainsText(
        caustics,
        ".applicableConsumerQueueClass = Core::CommandQueue::Compute,"
    ));
    EXPECT_FALSE(ContainsText(caustics, "softwareCausticsRunsOnCompute"));

    const usize candidatesOffset = system.find("const Core::TextureHandle causticsComputeScratchTextures[]");
    const usize callbacksOffset = system.find(
        "Core::GpuTaskGraphTaskRecordedCallback normalRecordedCallbacks[",
        candidatesOffset
    );
    ASSERT_NE(candidatesOffset, AStringView::npos);
    ASSERT_NE(callbacksOffset, AStringView::npos);
    ASSERT_LT(candidatesOffset, callbacksOffset);
    const AStringView acceptedCaustics = system.substr(candidatesOffset, callbacksOffset - candidatesOffset);
    EXPECT_FALSE(ContainsText(system, "m_causticIrradianceLightingState"));
    EXPECT_TRUE(ContainsText(acceptedCaustics, "if(context->runsOnCompute){"));
    EXPECT_TRUE(ContainsText(acceptedCaustics, "m_causticIrradianceReturnState.buildFilteredResourceSubset("));
    EXPECT_TRUE(ContainsText(acceptedCaustics, "m_causticsComputePersistentState.buildFilteredResourceSubset("));
    EXPECT_TRUE(ContainsText(acceptedCaustics, "context->renderer->m_causticIrradianceReturnState.commit("));
    EXPECT_TRUE(ContainsText(acceptedCaustics, "context->renderer->m_causticsComputePersistentState.commit("));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredSoftwareCausticsTask,\n"
        "            .context = &softwareCausticsStateLifecycle,\n"
        "            .invoke = prepareSoftwareCausticsTask,"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredSoftwareCausticsTask,\n"
        "            .context = &softwareCausticsStateLifecycle,\n"
        "            .invoke = acceptSoftwareCausticsTask,"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "(selectedCausticsSubmissionToken.valid() && !selectedCausticsStateReady)"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "if(!selectedCausticsSubmissionToken.valid())\n"
        "                    restoreCausticsCpuState();"
    ));
}


// Depth Warp and Integration are each declared once as merge-capable, Compute-preferred semantic tasks. The
// compiler may independently retain or collapse either stage, while any retained Compute route keeps the explicit
// same-family and cross-family auxiliary-transport opt-ins.
TEST(EcsGraphics, NaturalAvboitComputeStagesPermitCompilerOwnedRouting){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "renderer_frame_pipeline_graph.cpp" }, taskGraphSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    const usize schedulingOffset = taskGraph.find("Core::GpuTaskSchedulingHint avboitComputeScheduling");
    const usize accumulationOffset = taskGraph.find("AvboitAccumulationGraphTask::Payload", schedulingOffset);
    ASSERT_NE(schedulingOffset, AStringView::npos);
    ASSERT_NE(accumulationOffset, AStringView::npos);
    ASSERT_LT(schedulingOffset, accumulationOffset);
    const AStringView naturalComputeStages = taskGraph.substr(schedulingOffset, accumulationOffset - schedulingOffset);

    EXPECT_TRUE(ContainsText(naturalComputeStages, "avboitComputeScheduling.forceSubmissionBoundary = false"));
    EXPECT_TRUE(ContainsText(naturalComputeStages, "avboitComputeScheduling.allowPacketMerge = true"));
    EXPECT_TRUE(ContainsText(naturalComputeStages, "avboitComputeScheduling.mergeWithPrevious = true"));
    EXPECT_TRUE(ContainsText(naturalComputeStages, "avboitComputeScheduling.allowMergeAcrossConsumerFrontier = true"));
    EXPECT_TRUE(ContainsText(naturalComputeStages, "EnableSameFamilyComputeEffectRouting(avboitComputeScheduling);"));
    EXPECT_FALSE(ContainsText(naturalComputeStages, "EnableSameFamilyComputeEffectRouting(avboitComputeScheduling, false)"));
    EXPECT_TRUE(ContainsText(naturalComputeStages, "EnableCrossFamilyComputeEffectRouting(avboitComputeScheduling)"));
    EXPECT_TRUE(ContainsText(naturalComputeStages, "avboitComputeScheduling.allowTimingFeedbackRouting = true"));
    EXPECT_TRUE(ContainsText(naturalComputeStages, "avboitComputeScheduling.allowCrossClassTimingFeedbackRouting = true"));
    const usize depthWarpDescOffset = naturalComputeStages.find("Core::GpuTaskDesc depthWarpDesc;");
    const usize depthWarpFailureOffset = naturalComputeStages.find(
        "if(!m_avboitSystem.taskGraphStage().m_depthWarpTask.valid())",
        depthWarpDescOffset
    );
    const usize integrationDescOffset = naturalComputeStages.find("Core::GpuTaskDesc integrationDesc;", depthWarpFailureOffset);
    const usize integrationFailureOffset = naturalComputeStages.find(
        "if(!m_avboitSystem.taskGraphStage().m_integrationTask.valid())",
        integrationDescOffset
    );
    ASSERT_NE(depthWarpDescOffset, AStringView::npos);
    ASSERT_NE(depthWarpFailureOffset, AStringView::npos);
    ASSERT_NE(integrationDescOffset, AStringView::npos);
    ASSERT_NE(integrationFailureOffset, AStringView::npos);
    ASSERT_LT(depthWarpDescOffset, depthWarpFailureOffset);
    ASSERT_LT(depthWarpFailureOffset, integrationDescOffset);
    ASSERT_LT(integrationDescOffset, integrationFailureOffset);
    const AStringView depthWarpStage = naturalComputeStages.substr(
        depthWarpDescOffset,
        depthWarpFailureOffset - depthWarpDescOffset
    );
    const AStringView integrationStage = naturalComputeStages.substr(
        integrationDescOffset,
        integrationFailureOffset - integrationDescOffset
    );
    for(const AStringView computeStage : { depthWarpStage, integrationStage }){
        EXPECT_TRUE(ContainsText(computeStage, ".setQueue(ComputeQueueRequest())"));
        EXPECT_TRUE(ContainsText(computeStage, ".setScheduling(avboitComputeScheduling)"));
        EXPECT_TRUE(ContainsText(computeStage, ".setTimingMetadata(avboitComputeStageTiming)"));
        EXPECT_FALSE(ContainsText(computeStage, "GraphicsComputeQueueRequest()"));
    }
    EXPECT_TRUE(ContainsText(
        naturalComputeStages,
        "const Core::GpuTaskTimingMetadata avboitComputeStageTiming ="
    ));
    EXPECT_TRUE(ContainsText(
        naturalComputeStages,
        "AvboitComputeStageTimingMetadata(deferredTargets.avboit)"
    ));
    EXPECT_FALSE(ContainsText(naturalComputeStages, "AvboitIntegrationTimingMetadata"));
    EXPECT_TRUE(ContainsText(depthWarpStage, "render.avboit.depth_warp"));
    EXPECT_TRUE(ContainsText(integrationStage, "render.avboit.integration"));
    EXPECT_FALSE(ContainsText(taskGraph, "splitAvboitStages"));
}


// Queue timing feedback is deliberately opt-in, but the two graph-owned AVBOIT Compute tasks must route accepted
// timestamp samples back into the next immutable compiler snapshot. Keep this source-level contract focused on the
// renderer boundary rather than coupling it to one physical queue topology.
TEST(EcsGraphics, DeferredGraphWiresAcceptedTaskTimingFeedback){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemHeaderSource;
    AString timingFeedbackHeaderSource;
    AString timingFeedbackSource;
    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", systemHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_timing_feedback.h", timingFeedbackHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_timing_feedback.cpp", timingFeedbackSource));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "avboit/task_graph_occupancy_tasks.h",
            "avboit/task_graph_occupancy_tasks.cpp",
            "avboit/task_graph_extinction_integration_tasks.h",
            "avboit/task_graph_extinction_integration_tasks.cpp",
            "avboit/task_graph_accumulation_tasks.h",
            "avboit/task_graph_accumulation_tasks.cpp",
            "avboit/task_graph_timing_metadata.h",
            "renderer_frame_pipeline_graph.cpp",
        },
        taskGraphSource
    ));
    const AStringView systemHeader(systemHeaderSource.data(), systemHeaderSource.size());
    const AStringView timingFeedbackHeader(timingFeedbackHeaderSource.data(), timingFeedbackHeaderSource.size());
    const AStringView timingFeedback(timingFeedbackSource.data(), timingFeedbackSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    EXPECT_TRUE(ContainsText(timingFeedbackHeader, "class RendererTaskTimingFeedback final"));
    EXPECT_TRUE(ContainsText(timingFeedbackHeader, "Core::GpuTimingSampleSubscription m_subscription"));
    EXPECT_FALSE(ContainsText(timingFeedbackHeader, "m_nextAttribution"));
    EXPECT_TRUE(ContainsText(timingFeedback, "subscribeSampleListener(Core::GpuTimingSampleListener{"));
    EXPECT_TRUE(ContainsText(timingFeedback, "unsubscribeSampleListener(subscription)"));
    EXPECT_TRUE(ContainsText(timingFeedback, "setFeedbackCollectionEnabled(subscription, policy.enabled)"));
    EXPECT_TRUE(ContainsText(timingFeedback, "m_graphics.gpuTiming().allocateSampleAttribution()"));
    EXPECT_TRUE(ContainsText(timingFeedback, "!m_active || !m_policy.enabled || !m_subscription.valid()"));
    EXPECT_TRUE(ContainsText(timingFeedback, "sample.physicalQueue != pending.expectedQueue"));
    EXPECT_TRUE(ContainsText(timingFeedbackHeader, "bool recordsNonCommittingTimingSample = false"));
    EXPECT_TRUE(ContainsText(timingFeedback, "!pending.recordsNonCommittingTimingSample"));
    EXPECT_TRUE(ContainsText(timingFeedback, "m_history.recordNonCommittingSample("));
    EXPECT_TRUE(ContainsText(systemHeader, "RendererTaskTimingFeedback m_deferredTaskTimingFeedback"));

    const usize lightingOffset = taskGraph.find("void RendererFramePipeline::buildDeferredLightingTaskGraph");
    const usize compilerOffset = taskGraph.find("if(!compiler.compile(", lightingOffset);
    const usize feedbackOffset = taskGraph.find("m_deferredTaskTimingFeedback.configureCompileOptions(", lightingOffset);
    ASSERT_NE(lightingOffset, AStringView::npos);
    ASSERT_NE(compilerOffset, AStringView::npos);
    ASSERT_NE(feedbackOffset, AStringView::npos);
    EXPECT_LT(feedbackOffset, compilerOffset);

    const usize depthWarpOffset = taskGraph.find("struct AvboitDepthWarpGraphTask");
    const usize extinctionComputeEmulationOffset = taskGraph.find(
        "struct AvboitExtinctionComputeEmulationGraphTask",
        depthWarpOffset
    );
    const usize integrationOffset = taskGraph.find("struct AvboitIntegrationGraphTask", extinctionComputeEmulationOffset);
    const usize accumulationOffset = taskGraph.find("struct AvboitAccumulationGraphTask", integrationOffset);
    ASSERT_NE(depthWarpOffset, AStringView::npos);
    ASSERT_NE(extinctionComputeEmulationOffset, AStringView::npos);
    ASSERT_NE(integrationOffset, AStringView::npos);
    ASSERT_NE(accumulationOffset, AStringView::npos);
    ASSERT_LT(depthWarpOffset, extinctionComputeEmulationOffset);
    ASSERT_LT(extinctionComputeEmulationOffset, integrationOffset);
    ASSERT_LT(integrationOffset, accumulationOffset);
    const AStringView depthWarp = taskGraph.substr(depthWarpOffset, extinctionComputeEmulationOffset - depthWarpOffset);
    const AStringView integration = taskGraph.substr(integrationOffset, accumulationOffset - integrationOffset);

    for(const AStringView task : { depthWarp, integration }){
        EXPECT_TRUE(ContainsText(task, ".timingFeedback"));
        EXPECT_TRUE(ContainsText(task, ".timingScope"));
        EXPECT_TRUE(ContainsText(task, "beginSample("));
        EXPECT_TRUE(ContainsText(task, "compiledTask->recordsNonCommittingTimingSample"));
        EXPECT_TRUE(ContainsText(task, "static void accepted("));
        EXPECT_TRUE(ContainsText(task, "acceptSubmission("));
        EXPECT_TRUE(ContainsText(task, "static void discarded("));
        EXPECT_TRUE(ContainsText(task, "discardRecording("));
    }

    const AStringView lighting = taskGraph.substr(lightingOffset);
    EXPECT_TRUE(ContainsText(lighting, "allowTimingFeedbackRouting = true"));
    const usize depthWarpDeclarationOffset = lighting.find("Core::GpuTaskDesc depthWarpDesc;");
    const usize depthWarpDeclarationEnd = lighting.find(
        "if(!m_avboitSystem.taskGraphStage().m_depthWarpTask.valid())",
        depthWarpDeclarationOffset
    );
    const usize integrationDeclarationOffset = lighting.find("Core::GpuTaskDesc integrationDesc;", depthWarpDeclarationEnd);
    const usize integrationDeclarationEnd = lighting.find(
        "if(!m_avboitSystem.taskGraphStage().m_integrationTask.valid())",
        integrationDeclarationOffset
    );
    ASSERT_NE(depthWarpDeclarationOffset, AStringView::npos);
    ASSERT_NE(depthWarpDeclarationEnd, AStringView::npos);
    ASSERT_NE(integrationDeclarationOffset, AStringView::npos);
    ASSERT_NE(integrationDeclarationEnd, AStringView::npos);
    const AStringView depthWarpDeclaration = lighting.substr(
        depthWarpDeclarationOffset,
        depthWarpDeclarationEnd - depthWarpDeclarationOffset
    );
    const AStringView integrationDeclaration = lighting.substr(
        integrationDeclarationOffset,
        integrationDeclarationEnd - integrationDeclarationOffset
    );
    EXPECT_TRUE(ContainsText(depthWarpDeclaration, ".timingFeedback = &m_deferredTaskTimingFeedback"));
    EXPECT_TRUE(ContainsText(depthWarpDeclaration, ".timingScope = &RendererGpuTimingScope::s_AvboitDepthWarp"));
    EXPECT_TRUE(ContainsText(depthWarpDeclaration, ".timingTicket = &avboitDepthWarpTimingTicket"));
    EXPECT_TRUE(ContainsText(integrationDeclaration, ".timingFeedback = &m_deferredTaskTimingFeedback"));
    EXPECT_TRUE(ContainsText(integrationDeclaration, ".timingScope = &RendererGpuTimingScope::s_AvboitIntegration"));
    EXPECT_TRUE(ContainsText(integrationDeclaration, ".timingTicket = &avboitIntegrationTimingTicket"));
    EXPECT_TRUE(ContainsText(taskGraph, "AvboitComputeStageTimingMetadata"));
    EXPECT_TRUE(ContainsText(
        taskGraph,
        ".resolutionClass = bucketDimension(targets.lowWidth) | (bucketDimension(targets.lowHeight) << 16u)"
    ));
    EXPECT_TRUE(ContainsText(depthWarpDeclaration, ".setTimingMetadata(avboitComputeStageTiming)"));
    EXPECT_TRUE(ContainsText(integrationDeclaration, ".setTimingMetadata(avboitComputeStageTiming)"));
    EXPECT_FALSE(ContainsText(taskGraph, "AvboitIntegrationTimingMetadata"));
    EXPECT_FALSE(ContainsText(taskGraph, "splitAvboitStages"));
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
    expectRecordUsesOwnedSnapshot(taskRecords, "bool GbufferGraphTask::record(", "void GbufferGraphTask::discarded(");
    expectRecordUsesOwnedSnapshot(
        taskRecords,
        "bool OpaqueCsgReceiverComputeEmulationGraphTask::record(",
        "bool OpaqueCsgIntervalSampleComputeEmulationGraphTask::record("
    );
    expectRecordUsesOwnedSnapshot(
        taskRecords,
        "bool OpaqueCsgIntervalSampleComputeEmulationGraphTask::record(",
        "void OpaqueCsgIntervalSampleComputeEmulationGraphTask::discarded("
    );
    expectRecordUsesOwnedSnapshot(taskRecords, "bool CsgReceiverSpanBuildGraphTask::record(", "bool CsgIntervalCombineGraphTask::record(");
    expectRecordUsesOwnedSnapshot(taskRecords, "bool CsgIntervalCombineGraphTask::record(", "bool CsgIntervalSampleGraphTask::record(");
    expectRecordUsesOwnedSnapshot(taskRecords, "bool CsgIntervalSampleGraphTask::record(", "void CsgIntervalSampleGraphTask::discarded(");
    expectRecordUsesOwnedSnapshot(
        taskRecords,
        "bool AvboitCsgReceiverSpanGraphTask::record(",
        "void AvboitCsgReceiverSpanGraphTask::discarded("
    );
    expectRecordUsesOwnedSnapshot(
        taskRecords,
        "bool AvboitCsgIntervalCombineGraphTask::record(",
        "void AvboitCsgIntervalCombineGraphTask::discarded("
    );
    expectRecordUsesOwnedSnapshot(taskRecords, "bool AvboitPreGraphTask::record(", "void AvboitPreGraphTask::discarded(");
    expectRecordUsesOwnedSnapshot(
        taskRecords,
        "bool AvboitOccupancyComputeEmulationGraphTask::record(",
        "void AvboitOccupancyComputeEmulationGraphTask::discarded("
    );
    expectRecordUsesOwnedSnapshot(taskRecords, "bool AvboitOccupancyGraphTask::record(", "void AvboitOccupancyGraphTask::discarded(");
    expectRecordUsesOwnedSnapshot(
        taskRecords,
        "bool AvboitExtinctionComputeEmulationGraphTask::record(",
        "void AvboitExtinctionComputeEmulationGraphTask::discarded("
    );
    expectRecordUsesOwnedSnapshot(taskRecords, "bool AvboitExtinctionGraphTask::record(", "void AvboitExtinctionGraphTask::discarded(");
    expectRecordUsesOwnedSnapshot(
        taskRecords,
        "bool AvboitAccumulationComputeEmulationGraphTask::record(",
        "void AvboitAccumulationComputeEmulationGraphTask::discarded("
    );
    expectRecordUsesOwnedSnapshot(taskRecords, "bool AvboitAccumulationGraphTask::record(", "void AvboitAccumulationGraphTask::discarded(");

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


// The optional lagged-history selector is either already accepted or uploaded as a declared dependency of Lighting.
// Keep the render callback free of a native writer so rejected graph work cannot make that selector look resident.
TEST(EcsGraphics, LaggedLightingSelectorHasNoNativeCompatibilityDispatcher){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString deferredHeaderSource;
    AString deferredTargetsSource;
    AString deferredLightingSource;
    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_system.h", deferredHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_targets.cpp", deferredTargetsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_lighting.cpp", deferredLightingSource));
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "renderer_frame_pipeline_graph.cpp" }, taskGraphSource));
    const AStringView deferredHeader(deferredHeaderSource.data(), deferredHeaderSource.size());
    const AStringView deferredTargets(deferredTargetsSource.data(), deferredTargetsSource.size());
    const AStringView deferredLighting(deferredLightingSource.data(), deferredLightingSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    EXPECT_FALSE(ContainsText(deferredHeader, "uploadLaggedLightingHistoryResources"));
    EXPECT_FALSE(ContainsText(deferredTargets, "uploadLaggedLightingHistoryResources"));
    EXPECT_FALSE(ContainsText(deferredTargets, "history.slotsBuffer.get(), &history.slots"));
    EXPECT_FALSE(ContainsText(deferredLighting, "laggedBindlessSlotsGraphOwned"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.lagged_lighting.bindless_slots_upload"));
    EXPECT_TRUE(ContainsText(taskGraph, "laggedBindlessSlotsGraphOwned"));
}


// The next graph declaration clears its history-tail output token. Lighting owns a read-ready completion for the
// immutable prior snapshot, while Shadow and Hardware Caustics own a distinct writer-drain completion before their
// first live writes. Even when both carry one native token, no semantic graph ID can be rebound at submission time.
TEST(EcsGraphics, LaggedLightingHistoryConsumersOwnSemanticPriorTokens){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    AString systemHeaderSource;
    AString shadowVisibilityTaskGraphSource;
    AString deferredLightingTaskGraphSource;
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", systemHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp", shadowVisibilityTaskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp", deferredLightingTaskGraphSource));
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView systemHeader(systemHeaderSource.data(), systemHeaderSource.size());
    const AStringView shadowVisibility(shadowVisibilityTaskGraphSource.data(), shadowVisibilityTaskGraphSource.size());
    const AStringView lighting(deferredLightingTaskGraphSource.data(), deferredLightingTaskGraphSource.size());
    const usize renderOffset = system.find("void RendererFramePipeline::render(");
    ASSERT_NE(renderOffset, AStringView::npos);
    const AStringView render = system.substr(renderOffset);
    const usize priorReadReadyOffset = render.find(
        "const Core::QueueSubmissionToken priorLaggedLightingHistoryReadReadyToken"
    );
    const usize priorDrainOffset = render.find("const Core::QueueSubmissionToken priorLaggedLightingHistoryWriterDrainToken");
    const usize armDrainOffset = render.find(
        "m_laggedLightingHistoryWriterDrainToken = laggedLightingHistorySubmissionToken"
    );
    const usize graphBuildOffset = render.find("buildDeferredLightingTaskGraph(");
    ASSERT_NE(priorReadReadyOffset, AStringView::npos);
    ASSERT_NE(priorDrainOffset, AStringView::npos);
    ASSERT_NE(armDrainOffset, AStringView::npos);
    ASSERT_NE(graphBuildOffset, AStringView::npos);

    EXPECT_LT(priorReadReadyOffset, graphBuildOffset);
    EXPECT_LT(priorDrainOffset, graphBuildOffset);
    EXPECT_LT(armDrainOffset, graphBuildOffset);
    EXPECT_TRUE(ContainsText(render, "&& priorLaggedLightingHistoryReadReadyToken.valid()"));
    EXPECT_TRUE(ContainsText(render, "const auto laggedLightingHistoryTokenPending ="));
    EXPECT_TRUE(ContainsText(render, "tokenTargetGeneration == laggedLightingHistoryTargetGeneration"));
    EXPECT_TRUE(ContainsText(render, "const bool laggedLightingHistorySubmissionPending = laggedLightingHistoryTokenPending("));
    EXPECT_TRUE(ContainsText(render, "if(laggedLightingHistorySubmissionPending){"));
    EXPECT_TRUE(ContainsText(render, "else if(\n        m_laggedLightingHistoryWriterDrainToken.valid()"));
    EXPECT_TRUE(ContainsText(render, ") < token.value"));
    EXPECT_TRUE(ContainsText(render, "laggedLightingHistoryWriterWaitPending"));
    EXPECT_TRUE(ContainsText(
        render,
        "const bool laggedLightingHistoryWriterWaitPending = priorLaggedLightingHistoryWriterDrainToken.valid();"
    ));
    EXPECT_TRUE(ContainsText(
        render,
        ".laggedLightingHistoryReadReady = priorLaggedLightingHistoryReadReadyToken.valid()"
    ));
    EXPECT_TRUE(ContainsText(render, ".laggedLightingHistoryWriterWaitPending = laggedLightingHistoryWriterWaitPending"));
    EXPECT_EQ(CountText(render, "priorLaggedLightingHistoryReadReadyToken,"), 2u);
    EXPECT_EQ(CountText(render, "priorLaggedLightingHistoryWriterDrainToken,"), 2u);
    EXPECT_FALSE(ContainsText(render, "GpuTaskGraphExternalCompletionToken"));
    EXPECT_FALSE(ContainsText(render, "deferredLightingCompletionTokens"));
    EXPECT_FALSE(ContainsText(render, "shadowEffectsCompletionTokens"));
    EXPECT_FALSE(ContainsText(render, "hardwareCausticsCompletionTokens"));
    EXPECT_FALSE(ContainsText(render, ".token = m_laggedLightingHistorySubmissionToken"));
    EXPECT_TRUE(ContainsText(
        render,
        "laggedAsyncLightingSchedule && !m_deferredLightingHistoryReadReadyCompletion.valid()"
    ));
    EXPECT_TRUE(ContainsText(
        render,
        "laggedLightingHistoryWriterWaitPending && !m_deferredLightingHistoryWriterDrainCompletion.valid()"
    ));
    EXPECT_EQ(CountText(
        render,
        "laggedLightingHistoryWriterWaitPending && !m_deferredLightingHistoryWriterDrainCompletion.valid()"
    ), 1u);
    EXPECT_TRUE(ContainsText(render, "device.queueGetCompletedInstance("));
    EXPECT_FALSE(ContainsText(render, "consumeLaggedLightingHistoryWriterDrain"));
    EXPECT_TRUE(ContainsText(lighting, ".acceptedToken = &m_laggedLightingHistorySubmissionToken"));
    EXPECT_TRUE(ContainsText(lighting, "if(useLaggedLightingHistory){"));
    EXPECT_TRUE(ContainsText(lighting, "if(features.laggedLightingHistoryWriterWaitPending){"));
    EXPECT_EQ(CountText(lighting, ".setToken(laggedLightingHistoryReadReadyToken)"), 1u);
    EXPECT_EQ(CountText(lighting, ".setToken(laggedLightingHistoryWriterDrainToken)"), 1u);
    EXPECT_TRUE(ContainsText(lighting, "render.deferred_lighting.lagged_history_read_ready"));
    EXPECT_TRUE(ContainsText(lighting, "render.deferred_lighting.lagged_history_writer_drain"));
    EXPECT_TRUE(ContainsText(
        lighting,
        "const Core::GpuExternalCompletionId laggedLightingExternalDependencies[] = {\n"
        "        m_deferredLightingHistoryReadReadyCompletion,"
    ));
    EXPECT_TRUE(ContainsText(
        lighting,
        "features.laggedLightingHistoryWriterWaitPending\n"
        "            ? m_deferredLightingHistoryWriterDrainCompletion\n"
        "            : Core::GpuExternalCompletionId{}"
    ));
    EXPECT_TRUE(ContainsText(
        lighting,
        "hardwareExternalDependencies = features.laggedLightingHistoryWriterWaitPending\n"
        "            ? &m_deferredLightingHistoryWriterDrainCompletion"
    ));
    EXPECT_FALSE(ContainsText(lighting, "m_deferredLightingHistoryCompletion"));
    EXPECT_EQ(CountText(system, "m_deferredLightingHistoryReadReadyCompletion = {};"), 2u);
    EXPECT_EQ(CountText(system, "m_deferredLightingHistoryWriterDrainCompletion = {};"), 2u);
    EXPECT_EQ(CountText(lighting, "m_deferredLightingHistoryReadReadyCompletion = {};"), 1u);
    EXPECT_EQ(CountText(lighting, "m_deferredLightingHistoryWriterDrainCompletion = {};"), 1u);

    EXPECT_TRUE(ContainsText(shadowVisibility, "laggedLightingHistoryWriterDrainDependencies"));
    EXPECT_TRUE(ContainsText(
        shadowVisibility,
        "Core::GpuExternalCompletionId laggedLightingHistoryWriterDrainCompletion"
    ));
    EXPECT_TRUE(ContainsText(
        systemHeader,
        "Core::GpuExternalCompletionId laggedLightingHistoryWriterDrainCompletion"
    ));
    EXPECT_TRUE(ContainsText(shadowVisibility, ".setExternalDependencies(\n                laggedLightingHistoryWriterDrainDependencies,"));
    EXPECT_TRUE(ContainsText(shadowVisibility, ".setExternalDependencies(\n            laggedLightingHistoryWriterDrainDependencies,"));
    EXPECT_TRUE(ContainsText(
        systemHeader,
        "const Core::QueueSubmissionToken& laggedLightingHistoryReadReadyToken"
    ));
    EXPECT_TRUE(ContainsText(
        systemHeader,
        "const Core::QueueSubmissionToken& laggedLightingHistoryWriterDrainToken"
    ));
    EXPECT_TRUE(ContainsText(systemHeader, "m_deferredLightingHistoryReadReadyCompletion;"));
    EXPECT_TRUE(ContainsText(systemHeader, "m_deferredLightingHistoryWriterDrainCompletion;"));
    EXPECT_FALSE(ContainsText(systemHeader, "m_deferredLightingHistoryCompletion;"));

    const usize setterOffset = systemHeader.find("void setFrameLaggedAsyncLightingEnabled(");
    const usize nextSetterOffset = systemHeader.find("[[nodiscard]] bool frameLaggedAsyncLightingEnabled", setterOffset);
    ASSERT_NE(setterOffset, AStringView::npos);
    ASSERT_NE(nextSetterOffset, AStringView::npos);
    const AStringView setter = systemHeader.substr(setterOffset, nextSetterOffset - setterOffset);
    EXPECT_TRUE(ContainsText(setter, "m_laggedLightingHistoryWriterDrainToken = m_laggedLightingHistorySubmissionToken"));
    EXPECT_TRUE(ContainsText(setter, "m_laggedLightingHistoryWriterDrainGeneration = m_laggedLightingHistoryGeneration"));
    EXPECT_TRUE(ContainsText(setter, "resetLaggedLightingHistoryReadTracking();"));
    EXPECT_FALSE(ContainsText(setter, "resetLaggedLightingHistoryTracking();"));
    EXPECT_FALSE(ContainsText(systemHeader, "NWB_ASSERT(!m_laggedLightingHistoryWriterDrainToken.valid())"));

    const usize readTrackingOffset = system.find("void RendererFramePipeline::resetLaggedLightingHistoryReadTracking");
    const usize fullTrackingOffset = system.find("void RendererFramePipeline::resetLaggedLightingHistoryTracking", readTrackingOffset);
    const usize targetResetOffset = system.find("void RendererFramePipeline::resetTargetGenerationStateHandoffs", fullTrackingOffset);
    ASSERT_NE(readTrackingOffset, AStringView::npos);
    ASSERT_NE(fullTrackingOffset, AStringView::npos);
    ASSERT_NE(targetResetOffset, AStringView::npos);
    const AStringView readTracking = system.substr(readTrackingOffset, fullTrackingOffset - readTrackingOffset);
    const AStringView fullTracking = system.substr(fullTrackingOffset, targetResetOffset - fullTrackingOffset);
    EXPECT_FALSE(ContainsText(readTracking, "invalidateLaggedLightingHistoryWriterDrain"));
    EXPECT_TRUE(ContainsText(fullTracking, "invalidateLaggedLightingHistoryWriterDrain();"));
    EXPECT_EQ(CountText(system, "invalidateLaggedLightingHistoryWriterDrain();"), 2u);
}


// Fresh deferred outputs must lower their first graph write from the native image origin. Active lagged history
// remains a generic import because Lighting reads its accepted descriptor-state handoff.
TEST(EcsGraphics, DeferredFirstWriteTextureImportsPreserveNativeOrigins){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    AString avboitTargetsSource;
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "renderer_frame_pipeline_graph.cpp" }, taskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_targets.cpp", avboitTargetsSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const AStringView avboitTargets(avboitTargetsSource.data(), avboitTargetsSource.size());
    const usize lightingOffset = taskGraph.find("void RendererFramePipeline::buildDeferredLightingTaskGraph");
    const usize compileOffset = taskGraph.find("if(!compiler.compile(", lightingOffset);
    ASSERT_NE(lightingOffset, AStringView::npos);
    ASSERT_NE(compileOffset, AStringView::npos);
    const AStringView deferredLighting = taskGraph.substr(lightingOffset, compileOffset - lightingOffset);

    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const auto importFirstWriteTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){\n"
        "        Core::GpuGraphResourceDesc desc = TextureResourceDesc(identity, label);\n"
        "        desc.setInitialState(Core::ResourceStates::Unknown);\n"
        "        return m_deferredLightingTaskGraph.importTexture(texture, desc);\n"
        "    };"
    ));
    EXPECT_EQ(CountText(deferredLighting, "importFirstWriteTexture("), 6u);
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const auto importAvboitTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){\n"
        "        return clearAvboitTargets\n"
        "            ? importFirstWriteTexture(texture, identity, label)\n"
        "            : importTexture(texture, identity, label)\n"
        "        ;\n"
        "    };"
    ));
    EXPECT_EQ(CountText(deferredLighting, " = importAvboitTexture("), 4u);
    EXPECT_EQ(CountText(avboitTargets, ".setInitialState(Core::ResourceStates::Common)"), 2u);
    EXPECT_EQ(CountText(avboitTargets, ".setKeepInitialState(true)"), 2u);
    EXPECT_EQ(CountText(
        avboitTargets,
        ".enableAutomaticStateTracking(Core::ResourceStates::Common)"
    ), 1u);
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const Core::GpuGraphResourceId opaqueColor = importFirstWriteTexture(\n"
        "        deferredTargets.opaqueColor,"
    ));
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const Core::GpuGraphResourceId compositeColor = importFirstWriteTexture(\n"
        "        deferredTargets.compositeColor,"
    ));
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "historyCopyDestinationShadowVisibility = history\n"
        "            ? shadowVisibility\n"
        "            : importFirstWriteTexture("
    ));
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "historyCopyDestinationCausticIrradiance = history\n"
        "            ? causticIrradiance\n"
        "            : importFirstWriteTexture("
    ));
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "historyCopyDestinationSurfelIrradiance = history\n"
        "            ? surfelIrradiance\n"
        "            : importFirstWriteTexture("
    ));
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const Core::GpuGraphResourceId shadowVisibility = importTexture(\n"
        "        history ? history->shadowVisibility : deferredTargets.shadowVisibility,"
    ));
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const Core::GpuGraphResourceId causticIrradiance = importTexture(\n"
        "        history ? history->causticIrradiance : deferredTargets.causticIrradiance,"
    ));
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const Core::GpuGraphResourceId surfelIrradiance = importTexture(\n"
        "        history ? history->surfelIrradiance : deferredTargets.surfelIrradiance,"
    ));
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


// Accepted static scene-BVH and software-material cache hits have no upload bytes, but they still freeze the exact
// storage identities and traversal table during preflight. Pure software consumes that snapshot or rejects the
// packet; hybrid consumes it or restores HW. Neither route regathers ECS/material data after graph declaration.
TEST(EcsGraphics, SoftwareStaticSceneCacheFreezesTraversalWithoutRecordingTimeRegather){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString rayTracingHeaderSource;
    AString rayTracingSource;
    AString swBvhSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh.cpp", swBvhSource));

    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView rayTracing(rayTracingSource.data(), rayTracingSource.size());
    const AStringView swBvh(swBvhSource.data(), swBvhSource.size());

    EXPECT_TRUE(ContainsText(rayTracingHeader, "capturePreparedSceneBvhCacheReuse"));
    EXPECT_TRUE(ContainsText(rayTracingHeader, "capturePreparedShadowMaterialContextCacheReuse"));
    EXPECT_TRUE(ContainsText(rayTracingHeader, "bool m_preparedSceneBvhUploadRequired = false;"));
    EXPECT_TRUE(ContainsText(rayTracingHeader, "bool m_preparedShadowMaterialContextUploadRequired = false;"));
    EXPECT_TRUE(ContainsText(swBvh, "&& !capturePreparedSceneBvhCacheReuse(sceneStaticHash, instanceCount)"));
    EXPECT_TRUE(ContainsText(swBvh, "&& !capturePreparedShadowMaterialContextCacheReuse("));

    const usize cacheCaptureOffset = rayTracing.find("bool RendererRayTracingSystem::capturePreparedSceneBvhCacheReuse(");
    const usize cacheCaptureEndOffset = rayTracing.find("bool RendererRayTracingSystem::matchesPreparedSceneBvh(", cacheCaptureOffset);
    ASSERT_NE(cacheCaptureOffset, AStringView::npos);
    ASSERT_NE(cacheCaptureEndOffset, AStringView::npos);
    const AStringView cacheCapture = rayTracing.substr(cacheCaptureOffset, cacheCaptureEndOffset - cacheCaptureOffset);
    EXPECT_TRUE(ContainsText(cacheCapture, "state.m_sceneSwBvhStaticSceneHashValid"));
    EXPECT_TRUE(ContainsText(cacheCapture, "m_preparedSceneBvhReady = true;"));
    EXPECT_TRUE(ContainsText(cacheCapture, "m_preparedSceneBvhUploadRequired = false;"));
    EXPECT_FALSE(ContainsText(cacheCapture, "m_preparedSceneBvhNodeBytes.resize"));
    EXPECT_FALSE(ContainsText(cacheCapture, "m_preparedSceneBvhInstanceBytes.resize"));

    const usize materialCacheCaptureOffset = rayTracing.find(
        "bool RendererRayTracingSystem::capturePreparedShadowMaterialContextCacheReuse("
    );
    const usize materialCacheCaptureEndOffset = rayTracing.find(
        "bool RendererRayTracingSystem::matchesPreparedShadowMaterialContext(",
        materialCacheCaptureOffset
    );
    ASSERT_NE(materialCacheCaptureOffset, AStringView::npos);
    ASSERT_NE(materialCacheCaptureEndOffset, AStringView::npos);
    const AStringView materialCacheCapture = rayTracing.substr(
        materialCacheCaptureOffset,
        materialCacheCaptureEndOffset - materialCacheCaptureOffset
    );
    EXPECT_TRUE(ContainsText(materialCacheCapture, "state.m_swShadowMaterialContextHashValid"));
    EXPECT_TRUE(ContainsText(materialCacheCapture, "m_preparedShadowMaterialContextReady = true;"));
    EXPECT_TRUE(ContainsText(materialCacheCapture, "m_preparedShadowMaterialContextUploadRequired = false;"));
    EXPECT_FALSE(ContainsText(materialCacheCapture, "m_preparedShadowInstanceMaterialBytes.resize"));
    EXPECT_FALSE(ContainsText(materialCacheCapture, "m_preparedShadowInstanceBytes.resize"));
    EXPECT_FALSE(ContainsText(materialCacheCapture, "m_preparedShadowMaterialTypedBytes.resize"));

    const usize retainOffset = rayTracing.find("bool RendererRayTracingSystem::retainPreparedSceneBvhUploads(");
    const usize retainEndOffset = rayTracing.find("void RendererRayTracingSystem::confirmPreparedSceneBvhUploads()", retainOffset);
    ASSERT_NE(retainOffset, AStringView::npos);
    ASSERT_NE(retainEndOffset, AStringView::npos);
    const AStringView retain = rayTracing.substr(retainOffset, retainEndOffset - retainOffset);
    EXPECT_TRUE(ContainsText(retain, "outNodeBlob = {};"));
    EXPECT_TRUE(ContainsText(retain, "outInstanceBlob = {};"));
    EXPECT_TRUE(ContainsText(retain, "if(!m_preparedSceneBvhUploadRequired)"));
    EXPECT_TRUE(ContainsText(retain, "state.m_sceneSwBvhStaticSceneHash != m_preparedSceneBvhStaticSceneHash"));
    EXPECT_TRUE(ContainsText(rayTracing, "if(!m_preparedSceneBvhReady || !m_preparedSceneBvhUploadRequired)"));

    const usize materialRetainOffset = rayTracing.find(
        "bool RendererRayTracingSystem::retainPreparedShadowMaterialContextUploads("
    );
    const usize materialRetainEndOffset = rayTracing.find(
        "bool RendererRayTracingSystem::retainPreparedHybridHardwareMaterialContextFallbackUploads(",
        materialRetainOffset
    );
    ASSERT_NE(materialRetainOffset, AStringView::npos);
    ASSERT_NE(materialRetainEndOffset, AStringView::npos);
    const AStringView materialRetain = rayTracing.substr(
        materialRetainOffset,
        materialRetainEndOffset - materialRetainOffset
    );
    const usize materialCacheRetainOffset = materialRetain.find(
        "if(!m_preparedShadowMaterialContextUploadRequired)"
    );
    const usize materialUploadCopyOffset = materialRetain.find("outInstanceMaterialBlob = graph.copyUploadData(");
    ASSERT_NE(materialCacheRetainOffset, AStringView::npos);
    ASSERT_NE(materialUploadCopyOffset, AStringView::npos);
    ASSERT_LT(materialCacheRetainOffset, materialUploadCopyOffset);
    const AStringView materialCacheRetain = materialRetain.substr(
        materialCacheRetainOffset,
        materialUploadCopyOffset - materialCacheRetainOffset
    );
    EXPECT_TRUE(ContainsText(materialCacheRetain, "state.m_swShadowMaterialContextHash != m_preparedShadowMaterialContextHash"));
    EXPECT_TRUE(ContainsText(materialCacheRetain, "return true;"));

    const usize materialConfirmOffset = rayTracing.find(
        "void RendererRayTracingSystem::confirmPreparedShadowMaterialContextUploads()"
    );
    const usize materialConfirmEndOffset = rayTracing.find(
        "void RendererRayTracingSystem::clearPreparedSceneBvh()",
        materialConfirmOffset
    );
    ASSERT_NE(materialConfirmOffset, AStringView::npos);
    ASSERT_NE(materialConfirmEndOffset, AStringView::npos);
    const AStringView materialConfirm = rayTracing.substr(
        materialConfirmOffset,
        materialConfirmEndOffset - materialConfirmOffset
    );
    EXPECT_TRUE(ContainsText(
        materialConfirm,
        "if(m_preparedShadowMaterialContextReady && m_preparedShadowMaterialContextUploadRequired)"
    ));
    EXPECT_EQ(CountText(materialConfirm, "clearPreparedShadowMaterialContext();"), 1u);

    const usize materialHashOffset = swBvh.find("[[nodiscard]] u64 ComputeShadowMaterialContextHash(");
    const usize materialHashEndOffset = swBvh.find("// Cross-frame cache pins raw keys", materialHashOffset);
    ASSERT_NE(materialHashOffset, AStringView::npos);
    ASSERT_NE(materialHashEndOffset, AStringView::npos);
    const AStringView materialHash = swBvh.substr(materialHashOffset, materialHashEndOffset - materialHashOffset);
    const usize instanceMaterialsHashOffset = materialHash.find("reinterpret_cast<const u8*>(instanceMaterials.data())");
    const usize instanceDataHashOffset = materialHash.find("reinterpret_cast<const u8*>(instanceData.data())");
    const usize typedBytesHashOffset = materialHash.find("materialTypedBytes.data()");
    ASSERT_NE(instanceMaterialsHashOffset, AStringView::npos);
    ASSERT_NE(instanceDataHashOffset, AStringView::npos);
    ASSERT_NE(typedBytesHashOffset, AStringView::npos);
    EXPECT_LT(instanceMaterialsHashOffset, instanceDataHashOffset);
    EXPECT_LT(instanceDataHashOffset, typedBytesHashOffset);

    const usize traversalOffset = rayTracing.find("bool RendererRayTracingSystem::recordPreparedSceneSwBvhTraversal()");
    const usize traversalEndOffset = rayTracing.find("bool RendererRayTracingSystem::retainPreparedSceneBvhUploads(", traversalOffset);
    ASSERT_NE(traversalOffset, AStringView::npos);
    ASSERT_NE(traversalEndOffset, AStringView::npos);
    const AStringView traversal = rayTracing.substr(traversalOffset, traversalEndOffset - traversalOffset);
    EXPECT_TRUE(ContainsText(traversal, "const bool sceneSnapshotValid ="));
    EXPECT_TRUE(ContainsText(traversal, "const bool materialSnapshotValid ="));
    const usize rejectionOffset = traversal.find("const auto rejectPreparedTraversal = [&]()");
    const usize rejectionEndOffset = traversal.find("    };", rejectionOffset);
    ASSERT_NE(rejectionOffset, AStringView::npos);
    ASSERT_NE(rejectionEndOffset, AStringView::npos);
    const AStringView rejection = traversal.substr(rejectionOffset, rejectionEndOffset - rejectionOffset);
    EXPECT_TRUE(ContainsText(
        rejection,
        "if(!m_preparedSceneBvhUploadRequired)\n"
        "            state.m_sceneSwBvhStaticSceneHashValid = false;"
    ));
    EXPECT_TRUE(ContainsText(
        rejection,
        "if(!m_preparedShadowMaterialContextUploadRequired)\n"
        "            state.m_swShadowMaterialContextHashValid = false;"
    ));
    const AStringView traversalValidation = traversal.substr(rejectionEndOffset + 6u);
    EXPECT_EQ(CountText(traversalValidation, "return false;"), 0u);
    EXPECT_EQ(CountText(traversalValidation, "return rejectPreparedTraversal();"), 5u);
    EXPECT_TRUE(ContainsText(traversal, "const bool tablesMatch ="));
    EXPECT_FALSE(ContainsText(traversal, "restoreMutableTables"));
    EXPECT_EQ(CountText(traversal, ".clear();"), 0u);
    EXPECT_EQ(CountText(traversal, ".push_back("), 0u);
    EXPECT_FALSE(ContainsText(traversal, "seenThisFrame"));

    const usize pureRecordOffset = rayTracing.find(
        "bool RendererRayTracingSystem::recordPreflightShadowVisibilityResources("
    );
    const usize pureRecordEndOffset = rayTracing.find(
        "bool RendererRayTracingSystem::recordPreflightHybridSoftwareTail(",
        pureRecordOffset
    );
    ASSERT_NE(pureRecordOffset, AStringView::npos);
    ASSERT_NE(pureRecordEndOffset, AStringView::npos);
    const AStringView pureRecord = rayTracing.substr(pureRecordOffset, pureRecordEndOffset - pureRecordOffset);
    EXPECT_TRUE(ContainsText(pureRecord, "if(!m_preparedSceneSwBvhReady)"));
    EXPECT_TRUE(ContainsText(pureRecord, "if(!recordPreparedSceneSwBvhTraversal())"));
    EXPECT_FALSE(ContainsText(pureRecord, "buildSceneSwBvh("));

    const usize hybridTailOffset = rayTracing.find("bool RendererRayTracingSystem::recordPreflightHybridSoftwareTail(");
    const usize hybridTailEndOffset = rayTracing.find("// A graph-owned hybrid plan may exist", hybridTailOffset);
    ASSERT_NE(hybridTailOffset, AStringView::npos);
    ASSERT_NE(hybridTailEndOffset, AStringView::npos);
    const AStringView hybridTail = rayTracing.substr(hybridTailOffset, hybridTailEndOffset - hybridTailOffset);
    EXPECT_TRUE(ContainsText(hybridTail, "const bool hybridSceneTraversalFrozen ="));
    EXPECT_TRUE(ContainsText(hybridTail, "&& m_preparedSceneSwBvhReady"));
    EXPECT_TRUE(ContainsText(hybridTail, "&& recordPreparedSceneSwBvhTraversal()"));
    EXPECT_TRUE(ContainsText(hybridTail, "recordPreparedHybridHardwareMaterialContextFallback("));
    EXPECT_FALSE(ContainsText(hybridTail, "buildSceneSwBvh("));
    EXPECT_FALSE(ContainsText(hybridTail, "Core::Alloc::ScratchArena"));
}


// A descriptor-buffer bind validates every retained resource against the exact physical queue, not just descriptors
// referenced by the current shader. Keep every renderer-owned heap resource admissible to Graphics and AsyncCompute.
TEST(EcsGraphics, GlobalHeapRetainedResourcesAdmitAsyncCompute){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString rendererSource;
    AString skinningCacheSource;
    AString skinningResourcesSource;
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "material/material_pass_resources.cpp",
            "csg/csg_peel_targets.cpp",
            "csg/csg_resources.cpp",
            "csg/csg_interval_resources.cpp",
            "mesh/mesh_bindings.cpp",
            "raytrace/rt_shadow.cpp",
            "raytrace/rt_caustics.cpp",
            "raytrace/rt_surfel_gi.cpp",
            "raytrace/rt_swbvh.cpp",
        },
        rendererSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_mesh" / "skinning" / "runtime_cache_resources.cpp",
        skinningCacheSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_mesh" / "skinning" / "resources.cpp",
        skinningResourcesSource
    ));

    const AStringView renderer(rendererSource.data(), rendererSource.size());
    const AStringView skinningCache(skinningCacheSource.data(), skinningCacheSource.size());
    const AStringView skinningResources(skinningResourcesSource.data(), skinningResourcesSource.size());
    constexpr AStringView s_Sharing = "Core::ResourceQueueSharing::GraphicsAndAsyncCompute";
    const auto expectSharedBlock = [&](const AStringView source, const AStringView beginMarker, const AStringView endMarker){
        const usize beginOffset = source.find(beginMarker);
        const usize endOffset = source.find(endMarker, beginOffset);
        ASSERT_NE(beginOffset, AStringView::npos);
        ASSERT_NE(endOffset, AStringView::npos);
        ASSERT_LT(beginOffset, endOffset);
        EXPECT_TRUE(ContainsText(source.substr(beginOffset, endOffset - beginOffset), s_Sharing));
    };

    expectSharedBlock(renderer, "Core::BufferDesc instanceBufferDesc;", "Core::BufferHandle instanceBuffer =");
    expectSharedBlock(renderer, "Core::BufferDesc materialTypedBufferDesc;", "Core::BufferHandle materialTypedBuffer =");
    expectSharedBlock(renderer, "auto createCsgTexture =", "auto createPeelTexture =");
    expectSharedBlock(renderer, "[[nodiscard]] static bool ReserveCsgStructuredBuffer(", "[[nodiscard]] static CsgClipCutterResolveResult::Enum");
    expectSharedBlock(renderer, "if(!m_csgState.m_clipContextSlotsBuffer){", "EnsureCsgBufferHeapHandle(");
    expectSharedBlock(renderer, "bool RendererCsgSystem::createCsgIntervalSampleStateBuffer(){", "m_csgState.m_intervalSampleStateBuffer =");
    expectSharedBlock(renderer, "Core::BufferDesc emulationVertexBufferDesc;", "mesh.emulationVertexBuffer =");
    expectSharedBlock(renderer, "Core::TextureDesc coarseDesc;", "targets.shadowCoarseTransmittance =");
    expectSharedBlock(renderer, "Core::TextureDesc softHalfADesc;", "targets.shadowSoftHalfA =");
    expectSharedBlock(renderer, "Core::TextureDesc softGeometryDesc;", "targets.shadowSoftGeometry =");
    expectSharedBlock(renderer, "Core::BufferDesc edgeListDesc;", "Core::BufferHandle edgeListBuffer =");
    expectSharedBlock(renderer, "Core::BufferDesc edgeStatsDesc;", "m_rayTracingState.m_swShadowEdgeStatsBuffer =");
    expectSharedBlock(renderer, "Core::BufferDesc edgeCounterDesc;", "m_rayTracingState.m_swShadowEdgeCounterBuffer =");
    expectSharedBlock(renderer, "Core::BufferDesc indirectArgsDesc;", "m_rayTracingState.m_swShadowIndirectArgsBuffer =");
    expectSharedBlock(renderer, "Core::TextureDesc surfelIrradianceHalfDesc;", "targets.surfelIrradianceHalf =");
    expectSharedBlock(renderer, "Core::TextureDesc accumulatorDesc;", "targets.causticAccumulator =");
    expectSharedBlock(renderer, "Core::TextureDesc historyDesc;", "targets.causticHistory =");
    expectSharedBlock(renderer, "Core::TextureDesc halfBDesc;", "targets.causticResolveHalf =");
    expectSharedBlock(renderer, "Core::TextureDesc geometryDesc;", "targets.causticResolveGeometry =");
    expectSharedBlock(renderer, "if(!m_rayTracingState.m_surfelTraceIndirectArgsBuffer){", "m_rayTracingState.m_surfelTraceIndirectArgsBuffer =");
    expectSharedBlock(renderer, "if(!m_rayTracingState.m_surfelFreeListBuffer){", "m_rayTracingState.m_surfelFreeListBuffer =");
    expectSharedBlock(renderer, "Core::BufferDesc keysBufferDesc;", "Core::BufferHandle keysBuffer =");
    expectSharedBlock(renderer, "Core::BufferDesc payloadBufferDesc;", "Core::BufferHandle payloadBuffer =");
    expectSharedBlock(renderer, "Core::BufferDesc counterBufferDesc;", "Core::BufferHandle counterBuffer =");
    expectSharedBlock(renderer, "Core::BufferDesc parentBufferDesc;", "Core::BufferHandle newParentBuffer =");

    EXPECT_EQ(
        CountText(
            skinningCache,
            "const Core::ResourceQueueSharing::Mask queueSharing = Core::ResourceQueueSharing::GraphicsAndAsyncCompute"
        ),
        3u
    );
    expectSharedBlock(skinningResources, "return RuntimeMeshBufferUpload::SetupBuffer<PayloadT>(", "static bool RegisterStorageBuffer(");
    expectSharedBlock(skinningResources, "Core::BufferDesc bindlessSlotsBufferDesc;", "rebuilt.bindlessResourceSlotsBuffer =");
}


// The split software-BVH task records only compute commands after graph-owned clears. The compatibility Shadow
// Preparation endpoint can still record both transfer clears and compute dispatches, while preferring Graphics.
TEST(EcsGraphics, ShadowPreparationQueueCapabilitiesMatchNativeCommands){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_prepare.cpp",
        taskGraphSource
    ));

    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const usize softwareBuildOffset = taskGraph.find(".setMarkerLabel(\"Shadow Prepare SW-BVH Build\")");
    const usize softwareBuildEndOffset = taskGraph.find("m_deferredLightingTaskGraph.addTask<", softwareBuildOffset);
    ASSERT_NE(softwareBuildOffset, AStringView::npos);
    ASSERT_NE(softwareBuildEndOffset, AStringView::npos);
    const AStringView softwareBuild = taskGraph.substr(
        softwareBuildOffset,
        softwareBuildEndOffset - softwareBuildOffset
    );
    EXPECT_TRUE(ContainsText(softwareBuild, ".setQueue(GraphicsPreferredComputeQueueRequest())"));
    EXPECT_FALSE(ContainsText(softwareBuild, ".setQueue(GraphicsComputeQueueRequest())"));

    const usize shadowPrepareOffset = taskGraph.find(".setMarkerLabel(\"Shadow Preparation\")", softwareBuildEndOffset);
    const usize shadowPrepareEndOffset = taskGraph.find(
        "m_deferredLightingTaskGraph.addTask<ECSRenderDetail::ShadowPrepareGraphTask>",
        shadowPrepareOffset
    );
    ASSERT_NE(shadowPrepareOffset, AStringView::npos);
    ASSERT_NE(shadowPrepareEndOffset, AStringView::npos);
    const AStringView shadowPrepare = taskGraph.substr(
        shadowPrepareOffset,
        shadowPrepareEndOffset - shadowPrepareOffset
    );
    EXPECT_TRUE(ContainsText(shadowPrepare, ".setQueue(GraphicsComputeUploadQueueRequest())"));
    EXPECT_FALSE(ContainsText(shadowPrepare, ".setQueue(GraphicsPreferredComputeQueueRequest())"));
}


// Opaque hardware shadows never prepare the optional software traversal resources. Keep the direct compatibility
// builder behind the frozen hybrid-resource gate without removing the real pure-software fallback.
TEST(EcsGraphics, HardwareOpaqueShadowPreparationDoesNotEagerlyBuildSoftwareBvhs){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString rayTracingSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSource));

    const AStringView rayTracing(rayTracingSource.data(), rayTracingSource.size());
    const usize recordOffset = rayTracing.find(
        "bool RendererRayTracingSystem::recordPreflightShadowVisibilityResources("
    );
    const usize recordEndOffset = rayTracing.find(
        "bool RendererRayTracingSystem::recordPreflightHybridSoftwareTail(",
        recordOffset
    );
    ASSERT_NE(recordOffset, AStringView::npos);
    ASSERT_NE(recordEndOffset, AStringView::npos);
    const AStringView record = rayTracing.substr(recordOffset, recordEndOffset - recordOffset);

    const usize hardwareBranchOffset = record.find("if(m_shadowVisibilityHardwareSupported){");
    const usize softwareBranchOffset = record.find("const bool meshSwBvhReady =", hardwareBranchOffset);
    ASSERT_NE(hardwareBranchOffset, AStringView::npos);
    ASSERT_NE(softwareBranchOffset, AStringView::npos);
    const AStringView hardwareBranch = record.substr(
        hardwareBranchOffset,
        softwareBranchOffset - hardwareBranchOffset
    );
    const usize directBuildAssignmentOffset = hardwareBranch.find("const bool directMeshSwBvhBuildReady =");
    const usize hybridTailCallOffset = hardwareBranch.find("return recordPreflightHybridSoftwareTail(");
    const usize directBuildCallOffset = hardwareBranch.find("buildPendingMeshSwBvh(commandList, scratchArena)");
    ASSERT_NE(directBuildAssignmentOffset, AStringView::npos);
    ASSERT_NE(hybridTailCallOffset, AStringView::npos);
    ASSERT_NE(directBuildCallOffset, AStringView::npos);
    EXPECT_TRUE(ContainsText(
        hardwareBranch,
        "const bool directMeshSwBvhBuildReady = !m_shadowVisibilityHybridResourcesPreflighted\n"
        "            || meshSwBvhBuildsGraphOwned\n"
        "            || buildPendingMeshSwBvh(commandList, scratchArena)"
    ));
    EXPECT_EQ(CountText(hardwareBranch, "buildPendingMeshSwBvh(commandList, scratchArena)"), 1u);
    EXPECT_LT(directBuildAssignmentOffset, directBuildCallOffset);
    EXPECT_LT(directBuildCallOffset, hybridTailCallOffset);

    const AStringView softwareBranch = record.substr(softwareBranchOffset);
    EXPECT_TRUE(ContainsText(softwareBranch, ": buildPendingMeshSwBvh(commandList, scratchArena)"));
}


// Hybrid transparent shadows build a software BVH on ray-tracing hardware, so both static and skinned trace inputs
// must expose the raw views consumed by their global descriptor-heap slots.
TEST(EcsGraphics, HybridSoftwareBvhInputsExposeRawViewsOnRayTracingHardware){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString meshResourcesSource;
    AString skinningRuntimeCacheSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_resources.cpp", meshResourcesSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_mesh" / "skinning" / "runtime_cache_resources.cpp",
        skinningRuntimeCacheSource
    ));

    const AStringView meshResources(meshResourcesSource.data(), meshResourcesSource.size());
    const AStringView skinningRuntimeCache(skinningRuntimeCacheSource.data(), skinningRuntimeCacheSource.size());
    EXPECT_TRUE(ContainsText(
        meshResources,
        "NWB_TEXT(\"position\"),\n"
        "        true,\n"
        "        rtSupported"
    ));
    EXPECT_TRUE(ContainsText(meshResources, "indexFlags.canHaveRawViews = true;"));
    EXPECT_TRUE(ContainsText(
        skinningRuntimeCache,
        "NWB_TEXT(\"skinned position\"),\n"
        "        true,\n"
        "        rtSupported"
    ));
    EXPECT_TRUE(ContainsText(
        skinningRuntimeCache,
        "NWB_TEXT(\"rt triangle index\"),\n"
        "            true,\n"
        "            rtSupported"
    ));
}


// A healthy hybrid tail requires both a fresh software triple and a complete frozen hardware restore triple. A tail
// miss restores only declared blobs; invalid snapshots reject the merged packet for a fresh preflight.
TEST(EcsGraphics, HybridHardwareFallbackRequiresCompleteGraphOwnedBlobs){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    AString rayTracingHeaderSource;
    AString rayTracingSource;
    AString swBvhSource;
    AString kernelSystemHeaderSource;
    AString kernelSystemSource;
    AString smokeCmakeSource;
    AString stressTestProjectSource;
    AString transparentMultiProjectSource;
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "raytrace/task_graph_shadow_prepare_tasks.h",
            "raytrace/task_graph_shadow_prepare_tasks.cpp",
            "renderer_frame_pipeline_graph_shadow_prepare.cpp",
        },
        taskGraphSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh.cpp", swBvhSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", kernelSystemHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.cpp", kernelSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "CMakeLists.txt", smokeCmakeSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "stress_test_project.cpp", stressTestProjectSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "transparent_multi_project.cpp", transparentMultiProjectSource));

    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView rayTracing(rayTracingSource.data(), rayTracingSource.size());
    const AStringView swBvh(swBvhSource.data(), swBvhSource.size());
    const AStringView kernelSystemHeader(kernelSystemHeaderSource.data(), kernelSystemHeaderSource.size());
    const AStringView kernelSystem(kernelSystemSource.data(), kernelSystemSource.size());
    const AStringView smokeCmake(smokeCmakeSource.data(), smokeCmakeSource.size());
    const AStringView stressTestProject(stressTestProjectSource.data(), stressTestProjectSource.size());
    const AStringView transparentMultiProject(transparentMultiProjectSource.data(), transparentMultiProjectSource.size());

    EXPECT_TRUE(ContainsText(rayTracingHeader, "retainPreparedHybridHardwareMaterialContextFallbackUploads"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "hybridHardwareFallbackUploadsGraphOwned"));
    EXPECT_EQ(CountText(rayTracingHeader, "recordPreparedHybridHardwareMaterialContextFallback("), 1u);
    EXPECT_FALSE(ContainsText(rayTracingHeader, "recordPreparedHybridHardwareMaterialContextFallback(Core::CommandList& commandList);"));
    const usize retainFallbackOffset = rayTracing.find(
        "bool RendererRayTracingSystem::retainPreparedHybridHardwareMaterialContextFallbackUploads("
    );
    const usize retainFallbackEndOffset = rayTracing.find(
        "void RendererRayTracingSystem::confirmPreparedShadowMaterialContextUploads()noexcept",
        retainFallbackOffset
    );
    ASSERT_NE(retainFallbackOffset, AStringView::npos);
    ASSERT_NE(retainFallbackEndOffset, AStringView::npos);
    const AStringView retainFallback = rayTracing.substr(
        retainFallbackOffset,
        retainFallbackEndOffset - retainFallbackOffset
    );
    EXPECT_TRUE(ContainsText(retainFallback, "outInstanceMaterialBlob = {};"));
    EXPECT_TRUE(ContainsText(retainFallback, "outInstanceBlob = {};"));
    EXPECT_TRUE(ContainsText(retainFallback, "outMaterialTypedBlob = {};"));
    EXPECT_EQ(CountText(retainFallback, "graph.copyUploadData("), 3u);
    EXPECT_EQ(CountText(retainFallback, "outInstanceMaterialBlob = graph.copyUploadData("), 1u);
    EXPECT_EQ(CountText(retainFallback, "outInstanceBlob = graph.copyUploadData("), 1u);
    EXPECT_EQ(CountText(retainFallback, "outMaterialTypedBlob = graph.copyUploadData("), 1u);
    EXPECT_EQ(CountText(
        retainFallback,
        "outInstanceMaterialBlob = graph.copyUploadData(\n"
        "        bytes,\n"
        "        instanceMaterialByteCount,\n"
        "        alignof(NwbRtInstanceMaterialGpu)\n"
        "    );"
    ), 1u);
    EXPECT_EQ(CountText(
        retainFallback,
        "outInstanceBlob = graph.copyUploadData(\n"
        "        bytes + instanceMaterialByteCount,\n"
        "        instanceByteCount,\n"
        "        alignof(InstanceGpuData)\n"
        "    );"
    ), 1u);
    EXPECT_EQ(CountText(
        retainFallback,
        "outMaterialTypedBlob = graph.copyUploadData(\n"
        "        bytes + instanceMaterialByteCount + instanceByteCount,\n"
        "        materialTypedByteCount,\n"
        "        alignof(u32)\n"
        "    );"
    ), 1u);
    EXPECT_TRUE(ContainsText(
        retainFallback,
        "return outInstanceMaterialBlob.valid() && outInstanceBlob.valid() && outMaterialTypedBlob.valid();"
    ));

    const usize hybridTailRecordOffset = taskGraph.find("bool ShadowPrepareHybridSoftwareTailGraphTask::record(");
    const usize hybridTailRecordEndOffset = taskGraph.find(
        "void ShadowPrepareHybridSoftwareTailGraphTask::discarded(",
        hybridTailRecordOffset
    );
    ASSERT_NE(hybridTailRecordOffset, AStringView::npos);
    ASSERT_NE(hybridTailRecordEndOffset, AStringView::npos);
    const AStringView hybridTailRecord = taskGraph.substr(
        hybridTailRecordOffset,
        hybridTailRecordEndOffset - hybridTailRecordOffset
    );
    const usize hybridTailRecordValidationEndOffset = hybridTailRecord.find(
        "// The tail may record SW-BVH timing scopes"
    );
    ASSERT_NE(hybridTailRecordValidationEndOffset, AStringView::npos);
    const AStringView hybridTailRecordValidation = hybridTailRecord.substr(0u, hybridTailRecordValidationEndOffset);
    EXPECT_EQ(CountText(hybridTailRecordValidation, "context.taskGraph.uploadBlobData("), 3u);
    EXPECT_EQ(CountText(
        hybridTailRecordValidation,
        "hybridHardwareFallbackInstanceMaterialData = context.taskGraph.uploadBlobData(\n"
        "        payload.hybridHardwareFallbackInstanceMaterialBlob,\n"
        "        hybridHardwareFallbackInstanceMaterialByteCount\n"
        "    );"
    ), 1u);
    EXPECT_EQ(CountText(
        hybridTailRecordValidation,
        "hybridHardwareFallbackInstanceData = context.taskGraph.uploadBlobData(\n"
        "        payload.hybridHardwareFallbackInstanceBlob,\n"
        "        hybridHardwareFallbackInstanceByteCount\n"
        "    );"
    ), 1u);
    EXPECT_EQ(CountText(
        hybridTailRecordValidation,
        "hybridHardwareFallbackMaterialTypedData = context.taskGraph.uploadBlobData(\n"
        "        payload.hybridHardwareFallbackMaterialTypedBlob,\n"
        "        hybridHardwareFallbackMaterialTypedByteCount\n"
        "    );"
    ), 1u);
    EXPECT_EQ(CountText(
        hybridTailRecordValidation,
        "    if(\n"
        "        !hybridHardwareFallbackInstanceMaterialData\n"
        "        || !hybridHardwareFallbackInstanceData\n"
        "        || !hybridHardwareFallbackMaterialTypedData\n"
        "        || hybridHardwareFallbackInstanceMaterialByteCount == 0u\n"
        "        || hybridHardwareFallbackInstanceByteCount == 0u\n"
        "        || hybridHardwareFallbackMaterialTypedByteCount == 0u\n"
        "    )\n"
        "        return false;"
    ), 1u);
    EXPECT_EQ(CountText(
        hybridTailRecord,
        "        hybridHardwareFallbackInstanceMaterialData,\n"
        "        hybridHardwareFallbackInstanceMaterialByteCount,\n"
        "        hybridHardwareFallbackInstanceData,\n"
        "        hybridHardwareFallbackInstanceByteCount,\n"
        "        hybridHardwareFallbackMaterialTypedData,\n"
        "        hybridHardwareFallbackMaterialTypedByteCount\n"
        "    );"
    ), 1u);
    EXPECT_FALSE(ContainsText(taskGraph, "hybridHardwareFallbackUploadsGraphOwned"));
    const usize hybridFallbackRetentionOffset = taskGraph.find(
        "    if(hybridSoftwareTailGraphOwned){",
        taskGraph.find("Core::GpuUploadBlobId hybridHardwareFallbackInstanceMaterialBlob;")
    );
    const usize hybridFallbackRetentionEndOffset = taskGraph.find(
        "// A fully frozen hybrid packet has a separate software-tail callback",
        hybridFallbackRetentionOffset
    );
    ASSERT_NE(hybridFallbackRetentionOffset, AStringView::npos);
    ASSERT_NE(hybridFallbackRetentionEndOffset, AStringView::npos);
    const AStringView hybridFallbackRetention = taskGraph.substr(
        hybridFallbackRetentionOffset,
        hybridFallbackRetentionEndOffset - hybridFallbackRetentionOffset
    );
    EXPECT_TRUE(ContainsText(hybridFallbackRetention, "retainPreparedHybridHardwareMaterialContextFallbackUploads("));
    EXPECT_TRUE(ContainsText(hybridFallbackRetention, "hybridHardwareFallbackInstanceMaterialBlob.valid()"));
    EXPECT_TRUE(ContainsText(hybridFallbackRetention, "hybridHardwareFallbackInstanceBlob.valid()"));
    EXPECT_TRUE(ContainsText(hybridFallbackRetention, "hybridHardwareFallbackMaterialTypedBlob.valid()"));
    EXPECT_TRUE(ContainsText(hybridFallbackRetention, "!shadowInstanceMaterials.valid()"));
    EXPECT_TRUE(ContainsText(hybridFallbackRetention, "!shadowInstances.valid()"));
    EXPECT_TRUE(ContainsText(hybridFallbackRetention, "!shadowMaterialTyped.valid()"));
    EXPECT_TRUE(ContainsText(
        hybridFallbackRetention,
        "healthy hybrid tail requires a complete graph-owned hardware material fallback"
    ));
    EXPECT_TRUE(ContainsText(hybridFallbackRetention, "return false;"));

    const usize hybridTailUsesOffset = taskGraph.find(
        "    if(hybridSoftwareTailGraphOwned){",
        taskGraph.find("hybridSoftwareTailResourceUses.reserve(")
    );
    const usize hybridTailUsesEndOffset = taskGraph.find("    bool resourcesImported = true;", hybridTailUsesOffset);
    ASSERT_NE(hybridTailUsesOffset, AStringView::npos);
    ASSERT_NE(hybridTailUsesEndOffset, AStringView::npos);
    const AStringView hybridTailUses = taskGraph.substr(hybridTailUsesOffset, hybridTailUsesEndOffset - hybridTailUsesOffset);
    EXPECT_TRUE(ContainsText(hybridTailUses, "if(hybridSoftwareTailGraphOwned)"));
    EXPECT_EQ(CountText(hybridTailUses, "hybridSoftwareTailResourceUses.push_back("), 3u);
    EXPECT_EQ(CountText(hybridTailUses, "WriteUse(shadowInstanceMaterials, Core::ResourceStates::ShaderResource)"), 1u);
    EXPECT_EQ(CountText(hybridTailUses, "WriteUse(shadowInstances, Core::ResourceStates::ShaderResource)"), 1u);
    EXPECT_EQ(CountText(hybridTailUses, "WriteUse(shadowMaterialTyped, Core::ResourceStates::ShaderResource)"), 1u);
    EXPECT_TRUE(ContainsText(swBvh, "const void* const instanceMaterialData"));
    EXPECT_TRUE(ContainsText(swBvh, "graph-owned hybrid hardware fallback bytes differ from preflight"));
    EXPECT_TRUE(ContainsText(swBvh, "tryWriteBuffer(instanceMaterialBuffer, instanceMaterialData"));
    EXPECT_FALSE(ContainsText(swBvh, "bool RendererRayTracingSystem::recordPreparedHybridHardwareMaterialContextFallback(Core::CommandList& commandList){"));
    const usize restoreOffset = swBvh.find("bool RendererRayTracingSystem::recordPreparedHybridHardwareMaterialContextFallback(");
    const usize restoreEndOffset = swBvh.find("bool RendererRayTracingSystem::buildSceneSwBvhImpl(", restoreOffset);
    ASSERT_NE(restoreOffset, AStringView::npos);
    ASSERT_NE(restoreEndOffset, AStringView::npos);
    const AStringView restore = swBvh.substr(restoreOffset, restoreEndOffset - restoreOffset);
    EXPECT_EQ(CountText(restore, "NWB_MEMCMP("), 3u);
    EXPECT_EQ(CountText(restore, "commandList.tryWriteBuffer("), 3u);
    const usize restoreMismatchOffset = restore.find("graph-owned hybrid hardware fallback bytes differ from preflight");
    const usize firstRestoreTransitionOffset = restore.find("commandList.setBufferState(");
    ASSERT_NE(restoreMismatchOffset, AStringView::npos);
    ASSERT_NE(firstRestoreTransitionOffset, AStringView::npos);
    EXPECT_LT(restoreMismatchOffset, firstRestoreTransitionOffset);
    const AStringView restoreValidation = restore.substr(0u, firstRestoreTransitionOffset);
    EXPECT_EQ(CountText(restoreValidation, "NWB_MEMCMP("), 3u);
    EXPECT_EQ(CountText(restoreValidation, "commandList.tryWriteBuffer("), 0u);
    EXPECT_EQ(CountText(restoreValidation, "commandList.setBufferState("), 0u);
    EXPECT_TRUE(ContainsText(restoreValidation, "graph-owned hybrid hardware fallback bytes differ from preflight"));
    EXPECT_EQ(CountText(
        restoreValidation,
        "m_preparedHybridHardwareFallbackBytes.data(),\n"
        "            instanceMaterialData,\n"
        "            instanceMaterialByteCount"
    ), 1u);
    EXPECT_EQ(CountText(
        restoreValidation,
        "m_preparedHybridHardwareFallbackBytes.data() + instanceMaterialByteCount,\n"
        "            instanceData,\n"
        "            instanceByteCount"
    ), 1u);
    EXPECT_EQ(CountText(
        restoreValidation,
        "m_preparedHybridHardwareFallbackBytes.data() + instanceMaterialByteCount + instanceByteCount,\n"
        "            materialTypedData,\n"
        "            materialTypedByteCount"
    ), 1u);
    const usize restoreValidationEndOffset = restoreValidation.find("Core::Buffer* const instanceMaterialBuffer");
    ASSERT_NE(restoreValidationEndOffset, AStringView::npos);
    EXPECT_TRUE(ContainsText(
        restoreValidation.substr(restoreMismatchOffset, restoreValidationEndOffset - restoreMismatchOffset),
        "return false;"
    ));
    EXPECT_EQ(CountText(restore, "commandList.setBufferState("), 6u);
    const usize firstRestoreCommitOffset = restore.find("commandList.commitBarriers();", firstRestoreTransitionOffset);
    ASSERT_NE(firstRestoreCommitOffset, AStringView::npos);
    const AStringView restoreCopyDestStates = restore.substr(
        firstRestoreTransitionOffset,
        firstRestoreCommitOffset - firstRestoreTransitionOffset
    );
    EXPECT_EQ(CountText(restoreCopyDestStates, "Core::ResourceStates::CopyDest"), 3u);
    EXPECT_EQ(CountText(restoreCopyDestStates, "Core::ResourceStates::ShaderResource"), 0u);
    EXPECT_EQ(CountText(
        restoreCopyDestStates,
        "commandList.setBufferState(instanceMaterialBuffer, Core::ResourceStates::CopyDest);"
    ), 1u);
    EXPECT_EQ(CountText(
        restoreCopyDestStates,
        "commandList.setBufferState(instanceBuffer, Core::ResourceStates::CopyDest);"
    ), 1u);
    EXPECT_EQ(CountText(
        restoreCopyDestStates,
        "commandList.setBufferState(materialTypedBuffer, Core::ResourceStates::CopyDest);"
    ), 1u);
    const usize firstRestoreWriteOffset = restore.find("commandList.tryWriteBuffer(", firstRestoreCommitOffset);
    const usize finalRestoreStateOffset = restore.find(
        "commandList.setBufferState(instanceMaterialBuffer, Core::ResourceStates::ShaderResource);",
        firstRestoreWriteOffset
    );
    ASSERT_NE(firstRestoreWriteOffset, AStringView::npos);
    ASSERT_NE(finalRestoreStateOffset, AStringView::npos);
    const AStringView restoreWrites = restore.substr(firstRestoreWriteOffset, finalRestoreStateOffset - firstRestoreWriteOffset);
    EXPECT_EQ(CountText(restoreWrites, "commandList.tryWriteBuffer("), 3u);
    EXPECT_EQ(CountText(
        restoreWrites,
        "commandList.tryWriteBuffer(instanceMaterialBuffer, instanceMaterialData, instanceMaterialByteCount)"
    ), 1u);
    EXPECT_EQ(CountText(
        restoreWrites,
        "commandList.tryWriteBuffer(instanceBuffer, instanceData, instanceByteCount)"
    ), 1u);
    EXPECT_EQ(CountText(
        restoreWrites,
        "commandList.tryWriteBuffer(materialTypedBuffer, materialTypedData, materialTypedByteCount)"
    ), 1u);
    const usize finalRestoreCommitOffset = restore.find("commandList.commitBarriers();", finalRestoreStateOffset);
    ASSERT_NE(finalRestoreCommitOffset, AStringView::npos);
    const AStringView finalRestoreStates = restore.substr(
        finalRestoreStateOffset,
        finalRestoreCommitOffset - finalRestoreStateOffset
    );
    EXPECT_EQ(CountText(finalRestoreStates, "Core::ResourceStates::ShaderResource"), 3u);
    EXPECT_EQ(CountText(
        finalRestoreStates,
        "commandList.setBufferState(instanceMaterialBuffer, Core::ResourceStates::ShaderResource);"
    ), 1u);
    EXPECT_EQ(CountText(
        finalRestoreStates,
        "commandList.setBufferState(instanceBuffer, Core::ResourceStates::ShaderResource);"
    ), 1u);
    EXPECT_EQ(CountText(
        finalRestoreStates,
        "commandList.setBufferState(materialTypedBuffer, Core::ResourceStates::ShaderResource);"
    ), 1u);
    const usize restorePublicationOffset = restore.find("m_preparedHybridHardwareFallbackRecorded = true;");
    ASSERT_NE(restorePublicationOffset, AStringView::npos);
    EXPECT_LT(finalRestoreStateOffset, finalRestoreCommitOffset);
    EXPECT_LT(finalRestoreCommitOffset, restorePublicationOffset);
    EXPECT_TRUE(ContainsText(
        swBvh,
        "const bool canReuseSwMaterialContext =\n"
        "        !hybridSoftwareMaterialContextCaptureRequired\n"
        "        && staticScene"
    ));
    const usize hybridFallbackCaptureOffset = swBvh.find("|| !capturePreparedHybridHardwareMaterialContextFallback()");
    const usize softwareMaterialCaptureOffset = swBvh.find("if(!capturePreparedShadowMaterialContext(", hybridFallbackCaptureOffset);
    ASSERT_NE(hybridFallbackCaptureOffset, AStringView::npos);
    ASSERT_NE(softwareMaterialCaptureOffset, AStringView::npos);
    EXPECT_TRUE(ContainsText(
        swBvh.substr(hybridFallbackCaptureOffset, softwareMaterialCaptureOffset - hybridFallbackCaptureOffset),
        "return false;"
    ));

    const usize swPipelineOffset = rayTracing.find("const bool swPipelineReady = meshResourcesReady && ensureSwShadowPipeline();");
    const usize swGatherOffset = rayTracing.find("&& prepareSceneSwBvhResources(scratchArena)");
    ASSERT_NE(swPipelineOffset, AStringView::npos);
    ASSERT_NE(swGatherOffset, AStringView::npos);
    EXPECT_LT(swPipelineOffset, swGatherOffset);
    EXPECT_FALSE(ContainsText(rayTracing, "m_shadowVisibilityHybridResourcesPreflighted && !m_shadowVisibilityHybridPipelinePreflighted"));
    const usize hybridPreflightOffset = rayTracing.find("bool RendererRayTracingSystem::recordPreflightHybridSoftwareTail(");
    const usize hybridPreflightEndOffset = rayTracing.find("// A graph-owned hybrid plan may exist", hybridPreflightOffset);
    ASSERT_NE(hybridPreflightOffset, AStringView::npos);
    ASSERT_NE(hybridPreflightEndOffset, AStringView::npos);
    const AStringView hybridPreflight = rayTracing.substr(
        hybridPreflightOffset,
        hybridPreflightEndOffset - hybridPreflightOffset
    );
    const usize restoreFailureOffset = hybridPreflight.find("if(!recordPreparedHybridHardwareMaterialContextFallback(");
    ASSERT_NE(restoreFailureOffset, AStringView::npos);
    const AStringView restoreFailureGuard = hybridPreflight.substr(restoreFailureOffset);
    EXPECT_TRUE(ContainsText(
        restoreFailureGuard,
        "frozen hybrid hardware material-context restore failed; rejecting shadow preparation packet"
    ));
    EXPECT_EQ(CountText(restoreFailureGuard, "return false;"), 1u);
    EXPECT_FALSE(ContainsText(rayTracing, "buildSceneTlas(commandList, scratchArena, false)"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "forceHybridHardwareFallbackSnapshotStaleForTesting"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "forceHybridSceneTraversalFallbackForTesting"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "forceHybridSceneTraversalFallbackEveryFrameForTesting"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "m_forceHybridSceneTraversalFallbackForTesting"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "m_forceHybridSceneTraversalFallbackEveryFrameForTesting"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "m_expectHybridSceneTraversalRecoveryForTesting"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "m_reportedHybridSceneTraversalFallbackLoopForTesting"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "m_reportedHybridSceneTraversalFallbackLoopFailureForTesting"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "m_reportedHybridHardwareFallbackRestoreLoopForTesting"));
    EXPECT_FALSE(ContainsText(rayTracing, "forceHybridSceneTraversalFallback"));
    EXPECT_FALSE(ContainsText(rayTracing, "test forced hybrid software traversal fallback"));
    EXPECT_FALSE(ContainsText(rayTracing, "test hybrid software traversal recovered"));
    EXPECT_FALSE(ContainsText(swBvh, "m_reportedHybridHardwareFallbackRestoreLoopForTesting"));
    EXPECT_FALSE(ContainsText(rayTracing, "retried directly"));
    EXPECT_FALSE(ContainsText(kernelSystemHeader, "forceHybridHardwareFallbackSnapshotStaleForTesting"));
    EXPECT_FALSE(ContainsText(kernelSystemHeader, "forceHybridSceneTraversalFallback"));
    EXPECT_FALSE(ContainsText(kernelSystem, "forceHybridHardwareFallbackSnapshotStaleForTesting"));
    EXPECT_FALSE(ContainsText(kernelSystem, "forceHybridSceneTraversalFallback"));
    EXPECT_FALSE(ContainsText(smokeCmake, "NWB_TRANSPARENT_MULTI_FORCE_HYBRID_HARDWARE_FALLBACK_STALE"));
    EXPECT_FALSE(ContainsText(smokeCmake, "NWB_TRANSPARENT_MULTI_FORCE_HYBRID_TRAVERSAL_FALLBACK"));
    EXPECT_FALSE(ContainsText(smokeCmake, "nwb_transparent_multi_hybrid_fallback_smoke"));
    EXPECT_FALSE(ContainsText(smokeCmake, "nwb_transparent_multi_hybrid_fallback_capture_smoke"));
    EXPECT_FALSE(ContainsText(smokeCmake, "NWB_HYBRID_SHADOW_BOUNDARY_FALLBACK_BENCHMARK"));
    EXPECT_FALSE(ContainsText(smokeCmake, "nwb_hybrid_shadow_boundary_fallback_benchmark"));
    EXPECT_FALSE(ContainsText(smokeCmake, "nwb_hybrid_shadow_boundary_fallback_capture_smoke"));
    EXPECT_FALSE(ContainsText(smokeCmake, "nwb_transparent_multi_hybrid_fallback_stale_smoke"));
    const usize healthyCaptureOffset = smokeCmake.find("            nwb_hybrid_shadow_boundary_healthy_capture_smoke");
    const usize opaqueCaptureOffset = smokeCmake.find(
        "            nwb_hybrid_shadow_boundary_opaque_capture_smoke",
        healthyCaptureOffset
    );
    const usize opaqueCaptureEndOffset = smokeCmake.find(
        "        nwb_declare_executable(nwb_async_shadow_m4_sync_benchmark)",
        opaqueCaptureOffset
    );
    ASSERT_NE(healthyCaptureOffset, AStringView::npos);
    ASSERT_NE(opaqueCaptureOffset, AStringView::npos);
    ASSERT_NE(opaqueCaptureEndOffset, AStringView::npos);
    ASSERT_LT(healthyCaptureOffset, opaqueCaptureOffset);
    ASSERT_LT(opaqueCaptureOffset, opaqueCaptureEndOffset);
    const AStringView healthyCapture = smokeCmake.substr(
        healthyCaptureOffset,
        opaqueCaptureOffset - healthyCaptureOffset
    );
    const AStringView opaqueCapture = smokeCmake.substr(
        opaqueCaptureOffset,
        opaqueCaptureEndOffset - opaqueCaptureOffset
    );
    EXPECT_TRUE(ContainsText(healthyCapture, "$<TARGET_FILE:nwb_hybrid_shadow_boundary_healthy_benchmark>"));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--expect-log-message\" \"StressTestSmokeProject: enabled healthy hybrid transparent-shadow benchmark\""
    ));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--expect-log-message\" \"StressTestSmokeProject: RayQuery-capable hybrid shadow hardware available\""
    ));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--expect-log-message\" \"RendererSystem: dispatched software shadow traversal\""
    ));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--skip-log-message\" \"StressTestSmokeProject: hybrid shadow boundary skipped because RayQuery-capable hardware is unavailable\""
    ));
    EXPECT_TRUE(ContainsText(healthyCapture, "\"--skip-blocking-log-message\" \"[ERROR]\""));
    EXPECT_TRUE(ContainsText(healthyCapture, "\"--skip-blocking-log-message\" \"failed to resolve shader\""));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--reject-log-message\" \"RendererSystem: split opaque soft-shadow producer failed\\; retaining all-lit visibility\""
    ));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--reject-log-message\" \"RendererSystem: frozen hybrid hardware material-context restore failed\\; rejecting shadow preparation packet\""
    ));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--reject-log-message\" \"RendererSystem: restored frozen hybrid hardware material context\""
    ));
    EXPECT_TRUE(ContainsText(healthyCapture, "NWB_HYBRID_SHADOW_BOUNDARY_OPAQUE_BASELINE=0"));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "CONFIGURATIONS dbg opt"
    ));
    EXPECT_FALSE(ContainsText(healthyCapture, "enabled natural opaque hardware-shadow baseline"));
    EXPECT_FALSE(ContainsText(healthyCapture, "RendererSystem: created RayQuery shadow compute pipeline"));

    EXPECT_TRUE(ContainsText(opaqueCapture, "$<TARGET_FILE:nwb_hybrid_shadow_boundary_healthy_benchmark>"));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--expect-log-message\" \"StressTestSmokeProject: enabled natural opaque hardware-shadow baseline\""
    ));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--expect-log-message\" \"StressTestSmokeProject: RayQuery-capable hybrid shadow hardware available\""
    ));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--skip-log-message\" \"StressTestSmokeProject: hybrid shadow boundary skipped because RayQuery-capable hardware is unavailable\""
    ));
    EXPECT_TRUE(ContainsText(opaqueCapture, "\"--skip-blocking-log-message\" \"[ERROR]\""));
    EXPECT_TRUE(ContainsText(opaqueCapture, "\"--skip-blocking-log-message\" \"failed to resolve shader\""));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--reject-log-message\" \"RendererSystem: dispatched software shadow traversal\""
    ));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--reject-log-message\" \"RendererSystem: split opaque soft-shadow producer failed\\; retaining all-lit visibility\""
    ));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--reject-log-message\" \"RendererSystem: frozen hybrid hardware material-context restore failed\\; rejecting shadow preparation packet\""
    ));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--reject-log-message\" \"RendererSystem: restored frozen hybrid hardware material context\""
    ));
    EXPECT_TRUE(ContainsText(opaqueCapture, "NWB_HYBRID_SHADOW_BOUNDARY_OPAQUE_BASELINE=1"));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "CONFIGURATIONS dbg opt"
    ));
    EXPECT_FALSE(ContainsText(opaqueCapture, "enabled healthy hybrid transparent-shadow benchmark"));
    EXPECT_FALSE(ContainsText(opaqueCapture, "RendererSystem: created RayQuery shadow compute pipeline"));

    const usize transparentCaptureOffset = smokeCmake.find("            nwb_transparent_multi_capture_smoke");
    const usize transparentCaptureEndOffset = smokeCmake.find(
        "            nwb_transparent_multi_sw_capture_smoke",
        transparentCaptureOffset
    );
    ASSERT_NE(transparentCaptureOffset, AStringView::npos);
    ASSERT_NE(transparentCaptureEndOffset, AStringView::npos);
    ASSERT_LT(transparentCaptureOffset, transparentCaptureEndOffset);
    const AStringView transparentCapture = smokeCmake.substr(
        transparentCaptureOffset,
        transparentCaptureEndOffset - transparentCaptureOffset
    );
    EXPECT_TRUE(ContainsText(
        transparentCapture,
        "\"--expect-log-message\" \"RendererSystem: dispatched software shadow traversal\""
    ));
    EXPECT_TRUE(ContainsText(
        transparentCapture,
        "\"--reject-log-message\" \"RendererSystem: frozen hybrid hardware material-context restore failed\\; rejecting shadow preparation packet\""
    ));
    EXPECT_TRUE(ContainsText(
        transparentCapture,
        "ENVIRONMENT \"NWB_TRANSPARENT_MULTI_SPIN_ANGLE=0.6;NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME=6\""
    ));
    EXPECT_FALSE(ContainsText(transparentCapture, "RendererSystem: created RayQuery shadow compute pipeline"));

    const usize opaqueBaselineHelperOffset = stressTestProject.find(
        "[[nodiscard]] static bool hybridShadowOpaqueBaseline()"
    );
    const usize opaqueBaselineHelperEndOffset = stressTestProject.find(
        "static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(",
        opaqueBaselineHelperOffset
    );
    ASSERT_NE(opaqueBaselineHelperOffset, AStringView::npos);
    ASSERT_NE(opaqueBaselineHelperEndOffset, AStringView::npos);
    const AStringView opaqueBaselineHelper = stressTestProject.substr(
        opaqueBaselineHelperOffset,
        opaqueBaselineHelperEndOffset - opaqueBaselineHelperOffset
    );
    EXPECT_TRUE(ContainsText(opaqueBaselineHelper, "#if defined(NWB_HYBRID_SHADOW_BOUNDARY_BENCHMARK)"));
    EXPECT_TRUE(ContainsText(
        opaqueBaselineHelper,
        "ReadSmokeEnvironmentFlag(\"NWB_HYBRID_SHADOW_BOUNDARY_OPAQUE_BASELINE\")"
    ));
    EXPECT_TRUE(ContainsText(opaqueBaselineHelper, "return s_enabled;"));

    const usize createCharacterOffset = stressTestProject.find(
        "[[nodiscard]] NWB::Core::ECS::EntityID createCharacter(const u32 index)"
    );
    const usize createCharacterEndOffset = stressTestProject.find(
        "[[nodiscard]] NWB::Core::ECS::EntityID createWall(",
        createCharacterOffset
    );
    ASSERT_NE(createCharacterOffset, AStringView::npos);
    ASSERT_NE(createCharacterEndOffset, AStringView::npos);
    const AStringView createCharacter = stressTestProject.substr(
        createCharacterOffset,
        createCharacterEndOffset - createCharacterOffset
    );
    EXPECT_TRUE(ContainsText(createCharacter, "const bool transparentMaterialClass = (index % 2u) == 0u;"));
    EXPECT_TRUE(ContainsText(
        createCharacter,
        "const bool transparent = !hybridShadowOpaqueBaseline() && transparentMaterialClass;"
    ));
    EXPECT_TRUE(ContainsText(createCharacter, "const f32 z = transparentMaterialClass ? s_TransparentRowZ : s_OpaqueRowZ;"));

    const usize hybridBenchmarkStartupOffset = stressTestProject.find(
        "#if defined(NWB_HYBRID_SHADOW_BOUNDARY_BENCHMARK)",
        createCharacterEndOffset
    );
    const usize hybridBenchmarkStartupEndOffset = stressTestProject.find(
        "const u32 transparentCharacterCount",
        hybridBenchmarkStartupOffset
    );
    ASSERT_NE(hybridBenchmarkStartupOffset, AStringView::npos);
    ASSERT_NE(hybridBenchmarkStartupEndOffset, AStringView::npos);
    const AStringView hybridBenchmarkStartup = stressTestProject.substr(
        hybridBenchmarkStartupOffset,
        hybridBenchmarkStartupEndOffset - hybridBenchmarkStartupOffset
    );
    EXPECT_TRUE(ContainsText(
        hybridBenchmarkStartup,
        "queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct)"
    ));
    EXPECT_TRUE(ContainsText(hybridBenchmarkStartup, "queryFeatureSupport(NWB::Core::Feature::RayQuery)"));
    EXPECT_TRUE(ContainsText(hybridBenchmarkStartup, "if(!rayQueryCapable)"));
    EXPECT_TRUE(ContainsText(
        hybridBenchmarkStartup,
        "hybrid shadow boundary skipped because RayQuery-capable hardware is unavailable"
    ));
    EXPECT_TRUE(ContainsText(hybridBenchmarkStartup, "if(hybridShadowOpaqueBaseline())"));
    EXPECT_TRUE(ContainsText(hybridBenchmarkStartup, "enabled natural opaque hardware-shadow baseline"));
    EXPECT_TRUE(ContainsText(hybridBenchmarkStartup, "enabled healthy hybrid transparent-shadow benchmark"));
    EXPECT_FALSE(ContainsText(hybridBenchmarkStartup, "NWB_FATAL_ASSERT_MSG("));
    EXPECT_FALSE(ContainsText(stressTestProject, "forceHybridSceneTraversalFallback"));
    EXPECT_FALSE(ContainsText(stressTestProject, "NWB_HYBRID_SHADOW_BOUNDARY_FALLBACK_BENCHMARK"));
    EXPECT_FALSE(ContainsText(transparentMultiProject, "forceHybridHardwareFallbackSnapshotStaleForTesting"));
    EXPECT_FALSE(ContainsText(transparentMultiProject, "forceHybridSceneTraversalFallback"));
    EXPECT_FALSE(ContainsText(transparentMultiProject, "NWB_TRANSPARENT_MULTI_FORCE_HYBRID_TRAVERSAL_FALLBACK"));
}


// A fresh acceleration-structure backing allocation has the device descriptor's Common state; a retained backing
// instead needs the exact accepted Shadow Preparation handoff. Keep freshness tied to the physical generation so a
// discarded plan retries Common while an accepted packet never fabricates AccelStructRead.
TEST(EcsGraphics, PreparedAccelStructInitialStatesTrackBackingGenerationHandoffs){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    AString rayTracingHeaderSource;
    AString rayTracingSource;
    AString swBvhSource;
    AString meshResourcesSource;
    AString meshTypesSource;
    AString rendererStateHeaderSource;
    AString rendererStateSource;
    AString systemSource;
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "renderer_frame_pipeline_graph_shadow_prepare.cpp",
            "renderer_frame_pipeline_graph_shadow_visibility.cpp",
            "renderer_frame_pipeline_graph_surfel_gi.cpp",
            "renderer_frame_pipeline_graph.cpp",
        },
        taskGraphSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh.cpp", swBvhSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_resources.cpp", meshResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "renderer_mesh_types.h", meshTypesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "renderer_raytracing_state.h", rendererStateHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "renderer_raytracing_state.cpp", rendererStateSource));
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView rayTracing(rayTracingSource.data(), rayTracingSource.size());
    const AStringView swBvh(swBvhSource.data(), swBvhSource.size());
    const AStringView meshResources(meshResourcesSource.data(), meshResourcesSource.size());
    const AStringView meshTypes(meshTypesSource.data(), meshTypesSource.size());
    const AStringView rendererStateHeader(rendererStateHeaderSource.data(), rendererStateHeaderSource.size());
    const AStringView rendererState(rendererStateSource.data(), rendererStateSource.size());
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(meshTypes, "bool blasBackingFresh = false;"));
    EXPECT_TRUE(ContainsText(meshTypes, "bool blasBackingStateHandoffPending = false;"));
    EXPECT_TRUE(ContainsText(rayTracingHeader, "bool backingFresh = false;"));
    EXPECT_TRUE(ContainsText(swBvh, "outBuild.backingFresh = meshResources.blasBackingFresh;"));
    EXPECT_TRUE(ContainsText(swBvh, "meshResources.blasBackingFresh != build.backingFresh"));
    EXPECT_EQ(CountText(swBvh, "meshResources.blasBackingFresh = true;"), 1u);
    EXPECT_TRUE(ContainsText(
        swBvh,
        "if(meshResources.blasBackingFresh)\n"
        "        meshResources.blasBackingStateHandoffPending = true;"
    ));
    EXPECT_TRUE(ContainsText(
        swBvh,
        "meshResources.blasBackingFresh = false;\n"
        "        meshResources.blasBackingStateHandoffPending = false;"
    ));
    EXPECT_TRUE(ContainsText(
        taskGraph,
        "const Core::ResourceStates::Mask blasInitialState = build.backingFresh\n"
        "                ? Core::ResourceStates::Common\n"
        "                : Core::ResourceStates::Unknown\n"
        "            ;"
    ));
    EXPECT_TRUE(ContainsText(taskGraph, "AccelStructResourceDesc(blasIdentity, \"Prepared Mesh BLAS\").setInitialState(blasInitialState)"));
    EXPECT_TRUE(ContainsText(
        taskGraph,
        "const Core::ResourceStates::Mask blasInitialState = state.backingFresh\n"
        "            ? Core::ResourceStates::Common\n"
        "            : Core::ResourceStates::Unknown\n"
        "        ;"
    ));
    EXPECT_TRUE(ContainsText(taskGraph, "AccelStructResourceDesc(blasIdentity, \"Mesh BLAS\").setInitialState(blasInitialState)"));

    EXPECT_TRUE(ContainsText(rendererStateHeader, "bool m_tlasBackingFresh = false;"));
    EXPECT_TRUE(ContainsText(rendererStateHeader, "bool m_tlasBackingStateHandoffPending = false;"));
    EXPECT_TRUE(ContainsText(rendererState, "m_tlasBackingFresh = false;"));
    EXPECT_TRUE(ContainsText(rendererState, "m_tlasBackingStateHandoffPending = false;"));
    EXPECT_TRUE(ContainsText(swBvh, "m_rayTracingState.m_tlasBackingFresh = true;"));
    EXPECT_TRUE(ContainsText(
        swBvh,
        "if(m_rayTracingState.m_tlasBackingFresh)\n"
        "            m_rayTracingState.m_tlasBackingStateHandoffPending = true;"
    ));
    EXPECT_TRUE(ContainsText(rayTracingHeader, "sceneTlasBackingInitialState()const noexcept"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "preparedSceneTlasBuildInitialState()const noexcept"));
    EXPECT_TRUE(ContainsText(
        rayTracing,
        "return state.m_tlasBackingFresh\n"
        "        ? Core::ResourceStates::Common\n"
        "        : Core::ResourceStates::Unknown\n"
        "    ;"
    ));
    EXPECT_TRUE(ContainsText(
        rayTracing,
        "if(preparedTlasMatchesCurrent){\n"
        "        state.m_tlasBackingFresh = false;\n"
        "        state.m_tlasBackingStateHandoffPending = false;"
    ));
    EXPECT_TRUE(ContainsText(
        rayTracing,
        "if(state.m_tlasBackingFresh && state.m_tlasBackingStateHandoffPending){\n"
        "        state.m_tlasBackingFresh = false;\n"
        "        state.m_tlasBackingStateHandoffPending = false;"
    ));
    EXPECT_TRUE(ContainsText(
        rayTracing,
        "m_meshSystem.confirmAcceptedRayTracingStateHandoffs();"
    ));
    EXPECT_TRUE(ContainsText(
        meshResources,
        "if(mesh.blasBackingFresh && mesh.blasBackingStateHandoffPending){\n"
        "            mesh.blasBackingFresh = false;\n"
        "            mesh.blasBackingStateHandoffPending = false;"
    ));
    EXPECT_TRUE(ContainsText(
        taskGraph,
        "const Core::ResourceStates::Mask sceneTlasInitialState = m_raytracingSystem.sceneTlasBackingInitialState();"
    ));
    EXPECT_TRUE(ContainsText(taskGraph, "AccelStructResourceDesc(Name(\"render.deferred_effects.tlas\"), \"Scene TLAS\").setInitialState(sceneTlasInitialState)"));
    EXPECT_EQ(CountText(taskGraph, "AccelStructResourceDesc(Name(\"render.deferred_effects.tlas\"), \"Scene TLAS\")"), 4u);
    EXPECT_EQ(CountText(taskGraph, "sceneTlasBackingInitialState()"), 4u);
    EXPECT_EQ(
        CountText(taskGraph, ".setInitialState(m_raytracingSystem.sceneTlasBackingInitialState())"),
        3u
    );

    const usize clearPreparedSceneTlasOffset = rayTracing.find("void RendererRayTracingSystem::clearPreparedSceneTlasBuild()noexcept");
    const usize capturePreparedSceneTlasOffset = rayTracing.find(
        "bool RendererRayTracingSystem::capturePreparedSceneTlasBuild(",
        clearPreparedSceneTlasOffset
    );
    const usize discardPreflightOffset = rayTracing.find("void RendererRayTracingSystem::discardPreflightShadowVisibilityResources()noexcept");
    const usize preflightOffset = rayTracing.find(
        "bool RendererRayTracingSystem::preflightShadowVisibilityResources(",
        discardPreflightOffset
    );
    const usize commitPersistentStateOffset = system.find("renderer.m_shadowPreparePersistentState.commit(");
    const usize confirmSceneTlasOffset = system.find("renderer.m_raytracingSystem.confirmPreparedSceneTlasBuild();");
    const usize confirmMeshBlasOffset = system.find("renderer.m_raytracingSystem.confirmPreparedMeshBlasBuilds();");
    const usize confirmDirectHandoffOffset = system.find(
        "renderer.m_raytracingSystem.confirmAcceptedShadowPrepareAccelStructStateHandoffs();"
    );
    ASSERT_NE(clearPreparedSceneTlasOffset, AStringView::npos);
    ASSERT_NE(capturePreparedSceneTlasOffset, AStringView::npos);
    ASSERT_NE(discardPreflightOffset, AStringView::npos);
    ASSERT_NE(preflightOffset, AStringView::npos);
    ASSERT_NE(commitPersistentStateOffset, AStringView::npos);
    ASSERT_NE(confirmSceneTlasOffset, AStringView::npos);
    ASSERT_NE(confirmMeshBlasOffset, AStringView::npos);
    ASSERT_NE(confirmDirectHandoffOffset, AStringView::npos);
    EXPECT_FALSE(ContainsText(
        rayTracing.substr(clearPreparedSceneTlasOffset, capturePreparedSceneTlasOffset - clearPreparedSceneTlasOffset),
        "m_tlasBackingFresh"
    ));
    EXPECT_TRUE(ContainsText(
        rayTracing.substr(discardPreflightOffset, preflightOffset - discardPreflightOffset),
        "clearPreparedSceneTlasBuild();"
    ));
    EXPECT_TRUE(ContainsText(
        rayTracing.substr(discardPreflightOffset, preflightOffset - discardPreflightOffset),
        "m_tlasBackingStateHandoffPending = false;"
    ));
    EXPECT_TRUE(ContainsText(
        rayTracing.substr(discardPreflightOffset, preflightOffset - discardPreflightOffset),
        "m_meshSystem.discardRayTracingBuildState();"
    ));
    EXPECT_FALSE(ContainsText(
        rayTracing.substr(discardPreflightOffset, preflightOffset - discardPreflightOffset),
        "m_tlasBackingFresh = false;"
    ));
    EXPECT_FALSE(ContainsText(
        rayTracing.substr(discardPreflightOffset, preflightOffset - discardPreflightOffset),
        "meshResources.blasBackingFresh = false;"
    ));
    const usize discardMeshBuildStateOffset = meshResources.find("void RendererMeshSystem::discardRayTracingBuildState()noexcept");
    const usize collectMeshBuildStateOffset = meshResources.find(
        "bool RendererMeshSystem::collectSoftwareBvhParentBuildStates(",
        discardMeshBuildStateOffset
    );
    ASSERT_NE(discardMeshBuildStateOffset, AStringView::npos);
    ASSERT_NE(collectMeshBuildStateOffset, AStringView::npos);
    const AStringView discardMeshBuildState = meshResources.substr(
        discardMeshBuildStateOffset,
        collectMeshBuildStateOffset - discardMeshBuildStateOffset
    );
    EXPECT_TRUE(ContainsText(discardMeshBuildState, "mesh.blasBackingStateHandoffPending = false;"));
    EXPECT_TRUE(ContainsText(discardMeshBuildState, "mesh.blasBuildPending = true;"));
    EXPECT_TRUE(ContainsText(discardMeshBuildState, "mesh.swBvhBuildPending = true;"));
    EXPECT_FALSE(ContainsText(discardMeshBuildState, "mesh.blasBackingFresh = false;"));
    // Candidate creation happens after recording but before native acceptance. A failed prepare callback therefore
    // rejects the graph without publishing state; a failed accepted callback is surfaced by the unified state guard.
    EXPECT_TRUE(ContainsText(
        system,
        "context->stateReady = false;\n"
        "            renderer.m_raytracingSystem.discardPreflightShadowVisibilityResources();\n"
        "            return false;"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "context->stateReady = renderer.m_shadowPreparePersistentState.commit(*context->stateCandidate);"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "if(!context->stateReady){\n"
        "            renderer.m_raytracingSystem.discardPreflightShadowVisibilityResources();"
    ));
    EXPECT_TRUE(ContainsText(system, "const bool acceptedStateLost ="));
    EXPECT_TRUE(ContainsText(
        system,
        "(shadowPrepareSubmissionToken.valid() && !shadowPrepareStateLifecycle.stateReady)"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "if(!recovered || acceptedStateLost || (!presentationSignalReady && finalPresentationSubmissionToken.valid()))"
    ));
    const usize stateReadyFalseOffset = system.find("context->stateReady = false;");
    const usize acceptedStateLostOffset = system.find("const bool acceptedStateLost =");
    const usize stateLossRecoveryGuardOffset = system.find(
        "if(!recovered || acceptedStateLost || (!presentationSignalReady && finalPresentationSubmissionToken.valid()))"
    );
    const usize stateLossRecoveryOffset = system.find("failFrameRenderRecovery();", stateLossRecoveryGuardOffset);
    ASSERT_NE(stateReadyFalseOffset, AStringView::npos);
    ASSERT_NE(acceptedStateLostOffset, AStringView::npos);
    ASSERT_NE(stateLossRecoveryGuardOffset, AStringView::npos);
    ASSERT_NE(stateLossRecoveryOffset, AStringView::npos);
    EXPECT_LT(commitPersistentStateOffset, confirmSceneTlasOffset);
    EXPECT_LT(confirmSceneTlasOffset, confirmMeshBlasOffset);
    EXPECT_LT(confirmMeshBlasOffset, confirmDirectHandoffOffset);
    EXPECT_LT(stateReadyFalseOffset, acceptedStateLostOffset);
    EXPECT_LT(commitPersistentStateOffset, acceptedStateLostOffset);
    EXPECT_LT(acceptedStateLostOffset, stateLossRecoveryGuardOffset);
    EXPECT_LT(stateLossRecoveryGuardOffset, stateLossRecoveryOffset);
}


// Device feature support is backend-owned immutable state after device creation. Runtime and smoke code may observe
// that state to qualify a route, but must not mutate it or compile duplicate executables that pretend a feature is
// absent. The paired captures therefore use one native executable and capability-skip the route this adapter lacks.
TEST(EcsGraphics, FeatureSupportAndSmokeRoutesRemainNativeCapabilityAuthoritative){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString moduleHeaderSource;
    AString moduleFeatureQueriesSource;
    AString testbedRuntimeSource;
    AString smokeHelperSource;
    AString smokeProjectSource;
    AString smokeCmakeSource;
    AString smokeLauncherSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "module.h", moduleHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "module_feature_queries.cpp", moduleFeatureQueriesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "CoolStuff" / "Testbed" / "runtime.cpp", testbedRuntimeSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "smoke_scene_helpers.h", smokeHelperSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "CMakeLists.txt", smokeCmakeSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "launch.py", smokeLauncherSource));

    static constexpr StringView s_SmokeProjectSourceNames[] = {
        "transparent_multi_project.cpp",
        "csg_visible_project.cpp",
        "skinned_caustic_project.cpp",
        "stress_test_project.cpp",
        "flicker_test_project.cpp",
        "soft_shadow_test_project.cpp",
        "gi_test_project.cpp",
    };
    for(const StringView sourceName : s_SmokeProjectSourceNames){
        AString source;
        ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / sourceName.data(), source));
        if(!smokeProjectSource.empty())
            smokeProjectSource += "\n\n";
        smokeProjectSource.append(source.data(), source.size());
    }

    const AStringView moduleHeader(moduleHeaderSource.data(), moduleHeaderSource.size());
    const AStringView moduleFeatureQueries(moduleFeatureQueriesSource.data(), moduleFeatureQueriesSource.size());
    const AStringView testbedRuntime(testbedRuntimeSource.data(), testbedRuntimeSource.size());
    const AStringView smokeHelper(smokeHelperSource.data(), smokeHelperSource.size());
    const AStringView smokeProjects(smokeProjectSource.data(), smokeProjectSource.size());
    const AStringView smokeCmake(smokeCmakeSource.data(), smokeCmakeSource.size());
    const AStringView smokeLauncher(smokeLauncherSource.data(), smokeLauncherSource.size());

    EXPECT_TRUE(ContainsText(
        moduleFeatureQueries,
        "bool Graphics::queryFeatureSupport(const Feature::Enum feature, void* featureInfo, const usize featureInfoSize)const{\n"
        "    auto& device = getDevice();\n"
        "    return device.queryFeatureSupport(feature, featureInfo, featureInfoSize);\n"
        "}"
    ));

    static constexpr StringView s_RetiredProductionTokens[] = {
        "setFeatureSupportDisabledForTesting",
        "clearFeatureSupportDisabledForTesting",
        "m_disabledFeatureSupportMask",
        "NWB_TESTBED_FORCE_RAYTRACING_EMULATION",
    };
    for(const StringView token : s_RetiredProductionTokens){
        EXPECT_FALSE(ContainsText(moduleHeader, token));
        EXPECT_FALSE(ContainsText(moduleFeatureQueries, token));
        EXPECT_FALSE(ContainsText(testbedRuntime, token));
    }
    EXPECT_FALSE(ContainsText(smokeHelper, "DisableSmokeRayTracingForTesting"));

    static constexpr StringView s_RetiredSmokeForceMacros[] = {
        "NWB_TRANSPARENT_MULTI_FORCE_RT_EMULATION",
        "NWB_SKINNED_CAUSTIC_FORCE_RT_EMULATION",
        "NWB_STRESS_TEST_FORCE_RT_EMULATION",
        "NWB_FLICKER_TEST_FORCE_RT_EMULATION",
        "NWB_SOFT_SHADOW_TEST_FORCE_RT_EMULATION",
        "NWB_GI_TEST_FORCE_RT_EMULATION",
        "NWB_CSG_VISIBLE_FORCE_MESHLET_EMULATION",
    };
    for(const StringView token : s_RetiredSmokeForceMacros){
        EXPECT_FALSE(ContainsText(smokeProjects, token));
        EXPECT_FALSE(ContainsText(smokeCmake, token));
        EXPECT_FALSE(ContainsText(smokeLauncher, token));
    }

    static constexpr StringView s_RetiredSmokeTargets[] = {
        "nwb_transparent_multi_sw_smoke",
        "nwb_caustic_sphere_sw_smoke",
        "nwb_csg_visible_compute_emulation_smoke",
        "nwb_skinned_caustic_sw_smoke",
        "nwb_stress_test_sw_smoke",
        "nwb_flicker_test_sw_smoke",
        "nwb_soft_shadow_test_sw_smoke",
        "nwb_gi_test_sw_smoke",
    };
    for(const StringView target : s_RetiredSmokeTargets){
        EXPECT_FALSE(ContainsText(smokeCmake, target));
        EXPECT_FALSE(ContainsText(smokeLauncher, target));
    }

    static constexpr StringView s_NativeHybridMarker =
        "TransparentMultiSmokeProject: natural hybrid shadow route selected on RayQuery-capable hardware";
    static constexpr StringView s_NativeSoftwareMarker =
        "TransparentMultiSmokeProject: natural software-only shadow route selected because RayQuery-capable hardware is unavailable";
    static constexpr StringView s_NativeMeshMarker =
        "CsgVisibleSmokeProject: natural native mesh-shader route selected";
    static constexpr StringView s_NativeComputeMarker =
        "CsgVisibleSmokeProject: natural compute-emulation route selected because Meshlets are unavailable";
    EXPECT_TRUE(ContainsText(smokeProjects, s_NativeHybridMarker));
    EXPECT_TRUE(ContainsText(smokeProjects, s_NativeSoftwareMarker));
    EXPECT_TRUE(ContainsText(smokeProjects, s_NativeMeshMarker));
    EXPECT_TRUE(ContainsText(smokeProjects, s_NativeComputeMarker));
    EXPECT_EQ(CountText(smokeCmake, s_NativeHybridMarker), 2u);
    EXPECT_EQ(CountText(smokeCmake, s_NativeSoftwareMarker), 2u);
    EXPECT_EQ(CountText(smokeCmake, s_NativeMeshMarker), 2u);
    EXPECT_EQ(CountText(smokeCmake, s_NativeComputeMarker), 2u);
    EXPECT_TRUE(ContainsText(
        smokeCmake,
        "nwb_transparent_multi_sw_capture_smoke\n"
        "            transparent_multi_sw_capture.bmp\n"
        "            \"$<TARGET_FILE:nwb_transparent_multi_smoke>\""
    ));
    EXPECT_TRUE(ContainsText(
        smokeCmake,
        "nwb_csg_visible_compute_emulation_capture_smoke\n"
        "            csg_visible_compute_emulation_capture.bmp\n"
        "            \"$<TARGET_FILE:nwb_csg_visible_smoke>\""
    ));
    EXPECT_GE(CountText(smokeCmake, "\"--skip-blocking-log-message\" \"VUID-\""), 4u);

    EXPECT_EQ(CountText(smokeLauncher, "\"native\": SmokeExecutable("), 14u);
    EXPECT_FALSE(ContainsText(smokeLauncher, "\"hw\": SmokeExecutable("));
    EXPECT_FALSE(ContainsText(smokeLauncher, "\"sw\": SmokeExecutable("));
    EXPECT_FALSE(ContainsText(smokeLauncher, "\"compute\": SmokeExecutable("));
    EXPECT_TRUE(ContainsText(smokeLauncher, "default=\"native\""));
}


// A frame graph captures raw bindless slots before native recording. The heap-wide lease is the lifetime bridge:
// frees become an exact pending-recording state, the final overlapping release promotes that batch against the
// latest recorded heap use, and a later lease cannot make an already-retired TLAS generation recordable again.
TEST(EcsGraphics, DescriptorHeapPendingRecordingLeaseBridgesFrameSnapshotsToNativeRecording){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString heapHeaderSource;
    AString heapSource;
    AString descriptorWriteSource;
    AString nativeBindingSource;
    AString rendererExecutionSource;
    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "backend.h", heapHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "gpu_descriptor_heap.cpp", heapSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "gpu_descriptor_heap_descriptor_buffer.cpp",
        descriptorWriteSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "resource_bindings_commands.cpp",
        nativeBindingSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_execute.cpp",
        rendererExecutionSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "tests" / "smoke" / "descriptor_buffer" / "descriptor_buffer_round_trip_tests.cpp",
        smokeSource
    ));
    const AStringView heapHeader(heapHeaderSource.data(), heapHeaderSource.size());
    const AStringView heap(heapSource.data(), heapSource.size());
    const AStringView descriptorWrite(descriptorWriteSource.data(), descriptorWriteSource.size());
    const AStringView nativeBinding(nativeBindingSource.data(), nativeBindingSource.size());
    const AStringView rendererExecution(rendererExecutionSource.data(), rendererExecutionSource.size());
    const AStringView smoke(smokeSource.data(), smokeSource.size());

    EXPECT_TRUE(ContainsText(heapHeader, "enum class SlotState : u8{"));
    EXPECT_TRUE(ContainsText(heapHeader, "PendingRecording,"));
    EXPECT_TRUE(ContainsText(heapHeader, "class PendingRecordingLease final{"));
    EXPECT_TRUE(ContainsText(heapHeader, "PendingRecordingLease(const PendingRecordingLease&) = delete;"));
    EXPECT_TRUE(ContainsText(heapHeader, "PendingRecordingLease(PendingRecordingLease&&) = delete;"));
    EXPECT_TRUE(ContainsText(heapHeader, "u64 m_descriptorBufferGeneration = 0u;"));
    EXPECT_TRUE(ContainsText(heapHeader, "Vector<GpuDescriptorHandle, Alloc::GlobalArena> m_pendingRecording;"));

    EXPECT_TRUE(ContainsText(
        heap,
        "if(m_activePendingRecordingLeaseCount != 0u){\n"
        "            allocator.slotStates[handle.slot()] = SlotState::PendingRecording;\n"
        "            m_pendingRecording.push_back(handle);"
    ));
    EXPECT_TRUE(ContainsText(heap, "allocator.slotStates[slot] = SlotState::Retired;"));
    EXPECT_TRUE(ContainsText(heap, "m_retired.push_back(RetiredSlot{ handle, m_lastHeapUseID });"));
    EXPECT_TRUE(ContainsText(
        heap,
        "descriptorBufferGeneration != m_descriptorBufferGeneration\n"
        "        )\n"
        "            return;"
    ));
    EXPECT_TRUE(ContainsText(
        heap,
        "statistics.pendingRetiredSlotCount = m_pendingRecording.size() + m_retired.size();"
    ));
    EXPECT_TRUE(ContainsText(
        heap,
        "if(m_activePendingRecordingLeaseCount != 0u){\n"
        "        NWB_LOGGER_ERROR(NWB_TEXT(\"Vulkan: GpuDescriptorHeap initialization rejected while pending-recording leases are active.\"));"
    ));
    EXPECT_TRUE(ContainsText(
        heap,
        "if(m_activePendingRecordingLeaseCount != 0u || !m_heapUses.empty())"
    ));
    EXPECT_TRUE(ContainsText(
        heap,
        "m_accelStructSlots.slotStates[handle.slot()] != SlotState::Live"
    ));
    EXPECT_FALSE(ContainsText(
        descriptorWrite,
        "allocator.slotStates[handle.slot()] == SlotState::PendingRecording"
    ));

    EXPECT_TRUE(ContainsText(
        nativeBinding,
        "slotState != GpuDescriptorHeap::SlotState::Live\n"
        "                && slotState != GpuDescriptorHeap::SlotState::PendingRecording"
    ));
    EXPECT_FALSE(ContainsText(nativeBinding, "m_activePendingRecordingLeaseCount != 0u"));

    const usize deviceCheckOffset = rendererExecution.find(
        "if(m_graphics.isDeviceRecreationRequested() || device.isDeviceLost())"
    );
    const usize recoveryCheckOffset = rendererExecution.find("if(m_frameRenderRecoveryFailed)", deviceCheckOffset);
    const usize leaseOffset = rendererExecution.find(
        "Core::GpuDescriptorHeap::PendingRecordingLease descriptorHeapPendingRecordingLease",
        recoveryCheckOffset
    );
    const usize snapshotOffset = rendererExecution.find(
        "const RayTracingFrameCpuStateSnapshot rayTracingCpuState",
        leaseOffset
    );
    const usize graphBuildOffset = rendererExecution.find("buildDeferredLightingTaskGraph(", snapshotOffset);
    ASSERT_NE(deviceCheckOffset, AStringView::npos);
    ASSERT_NE(recoveryCheckOffset, AStringView::npos);
    ASSERT_NE(leaseOffset, AStringView::npos);
    ASSERT_NE(snapshotOffset, AStringView::npos);
    ASSERT_NE(graphBuildOffset, AStringView::npos);
    EXPECT_LT(deviceCheckOffset, recoveryCheckOffset);
    EXPECT_LT(recoveryCheckOffset, leaseOffset);
    EXPECT_LT(leaseOffset, snapshotOffset);
    EXPECT_LT(snapshotOffset, graphBuildOffset);
    EXPECT_EQ(CountText(
        rendererExecution,
        "Core::GpuDescriptorHeap::PendingRecordingLease descriptorHeapPendingRecordingLease"
    ), 1u);

    EXPECT_TRUE(ContainsText(
        smoke,
        "TEST_F(DescriptorBufferRoundTripTest, DescriptorHeapPendingRecordingLeaseProtectsCapturedSlots)"
    ));
    EXPECT_TRUE(ContainsText(
        smoke,
        "TEST_F(DescriptorBufferRoundTripTest, DescriptorHeapPendingRecordingLeaseRejectsReinitializeAndRecyclesWithoutRecording)"
    ));
    EXPECT_TRUE(ContainsText(
        smoke,
        "TEST_F(DescriptorBufferRoundTripTest, DeviceDescriptorHeapPendingRecordingLeaseTracksRecordedUse)"
    ));
    EXPECT_TRUE(ContainsText(
        smoke,
        "heap.bindCompute(*commandList, *pipeline, handle);\n"
        "        ASSERT_FALSE(commandList->commandRecordingFailed())"
    ));
    EXPECT_TRUE(ContainsText(
        smoke,
        "native TLAS binding rejected the exact pending-recording generation"
    ));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

