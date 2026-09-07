// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_topology_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


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
    EXPECT_TRUE(ContainsText(skinning, "compiler.compile(declarations, analysis, topology, assignments, compiledGraph, scratchArena, compileOptions)"));
    EXPECT_EQ(CountText(skinning, "mergeWithPrevious"), 0u);
    EXPECT_EQ(CountText(skinning, "scheduling.frontierScoredMergeDomain = Name(\"mesh_skinning.serial\");"), 2u);
    EXPECT_EQ(CountText(skinning, "setDependencies(&terminalTask, 1u);"), 4u);
    EXPECT_TRUE(ContainsText(skinning, "if(compiledPlan.packetCount() != 1u)"));
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
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_graphicsPrefixDeferredClearTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredShadowVisibilityTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredSoftwareCausticsTask)"));
    EXPECT_FALSE(ContainsText(system, "m_graphicsPrefixDeferredClearFirstTask"));
    EXPECT_FALSE(ContainsText(system, "graphicsPrefixDeferredClearBundleMerged"));
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
        "        || !deferredCompiledPlan.tasksSharePacket(\n"
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

