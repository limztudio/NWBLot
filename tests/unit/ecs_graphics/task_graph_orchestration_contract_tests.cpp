// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_orchestration_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


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
    EXPECT_TRUE(ContainsText(system, "deferredCompiledPlan.presentEndpoint()"));
    EXPECT_FALSE(ContainsText(system, "presentationEndpoint->packet"));
    EXPECT_FALSE(ContainsText(system, "terminalPresentationPacket"));
    EXPECT_TRUE(ContainsText(system, "normalExecution.terminalTask = terminalPresentationTask;"));
    EXPECT_TRUE(ContainsText(system, "presentationEndpoint->producer != m_deferredFrameTimingEndTask"));
    EXPECT_TRUE(ContainsText(system, "presentationEndpoint->queue != primaryGraphicsQueue"));
    EXPECT_TRUE(ContainsText(queueLookup, "return context.compiledPlan.queueInfoForTask(*task);"));
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

