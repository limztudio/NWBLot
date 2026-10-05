// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_timing_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_CORE = "core";
static constexpr AStringView s_GRAPHICS = "graphics";
static constexpr AStringView s_IMPL = "impl";
static constexpr AStringView s_ECS_RENDER = "ecs_render";
static constexpr AStringView s_NOEXCEPT_BRACE = ")noexcept{";
static constexpr AStringView s_RENDERER_FRAME_PIPELINE_GRAPH_CPP = "renderer_frame_pipeline_graph.cpp";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


// Timing availability and outcomes belong to the device-wide recorder rather than one compiled graph attempt. Keep the snapshot by value, export explicit skip reasons, and enumerate every live physical queue even on no-graph frames so unsupported capabilities remain distinguishable from measured zero-duration work.
TEST(EcsGraphics, GpuTimingStagesCompletionBeforeRetirementAndInvokesSinksOutsideRecorderLock){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString timingHeaderSource;
    AString timingFrameTransactionSource;
    AString timingSource;
    AString timingAccumulatorSource;
    AString timingMetricCorrelatorSource;
    AString timingSubmissionSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / s_CORE / s_GRAPHICS / "gpu_timing.h", timingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_CORE / s_GRAPHICS / "gpu_timing_frame_transaction.cpp", timingFrameTransactionSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_CORE / s_GRAPHICS / "gpu_timing.cpp", timingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_CORE / s_GRAPHICS / "gpu_timing_accumulator.cpp", timingAccumulatorSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "gpu_timing_metric_correlator.cpp",
        timingMetricCorrelatorSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_CORE / s_GRAPHICS / "gpu_timing_submission.cpp", timingSubmissionSource));
    const AStringView timingHeader(timingHeaderSource.data(), timingHeaderSource.size());
    const AStringView timingFrameTransaction(timingFrameTransactionSource.data(), timingFrameTransactionSource.size());
    const AStringView timing(timingSource.data(), timingSource.size());
    const AStringView timingAccumulator(timingAccumulatorSource.data(), timingAccumulatorSource.size());
    const AStringView timingMetricCorrelator(timingMetricCorrelatorSource.data(), timingMetricCorrelatorSource.size());
    const AStringView timingSubmission(timingSubmissionSource.data(), timingSubmissionSource.size());

    const usize collectLockedOffset = timing.find("bool GpuTimingRecorder::collectLocked(");
    const usize submissionCompletedOffset = timing.find("bool GpuTimingRecorder::submissionCompleted(", collectLockedOffset);
    ASSERT_NE(collectLockedOffset, AStringView::npos);
    ASSERT_NE(submissionCompletedOffset, AStringView::npos);
    const AStringView collectLocked = timing.substr(collectLockedOffset, submissionCompletedOffset - collectLockedOffset);
    EXPECT_TRUE(ContainsText(collectLocked, "const bool publishPerformanceSamples = m_performanceCollectionActive;"));
    EXPECT_TRUE(ContainsText(collectLocked, "return publishPerformanceSamples;"));
    EXPECT_FALSE(ContainsText(collectLocked, "m_enabled && m_timing.enabled()"));
    EXPECT_FALSE(ContainsText(collectLocked, "m_feedbackScopeDemands"));
    EXPECT_FALSE(ContainsText(collectLocked, "m_timing.recordSample("));
    EXPECT_FALSE(ContainsText(collectLocked, "m_timing.publishFrame("));

    EXPECT_TRUE(ContainsText(timingHeader, "Futex m_collectionMutex;"));
    EXPECT_EQ(CountText(timing, "ScopedLock collectionLock(m_collectionMutex);"), s_ExpectedDualCount);
    EXPECT_EQ(CountText(timing, "for(const GpuTimingSinkSample& sample : performanceSamples)"), s_ExpectedDualCount);
    EXPECT_EQ(CountText(
        timing,
        "m_timing.recordSample(sample.scope, sample.durationSeconds, sample.sourceFrameIndex);"
    ), s_ExpectedDualCount);
    EXPECT_EQ(CountText(timing, "m_timing.publishFrame(publishFrameIndex);"), s_ExpectedDualCount);

    const usize implicitCollectOffset = timing.find("void GpuTimingRecorder::collect(Device& device){");
    const usize explicitCollectOffset = timing.find(
        "void GpuTimingRecorder::collect(Device& device, const u64 publishFrameIndex){",
        implicitCollectOffset
    );
    const usize beginFrameOffset = timing.find("void GpuTimingRecorder::beginFrame(", explicitCollectOffset);
    ASSERT_NE(implicitCollectOffset, AStringView::npos);
    ASSERT_NE(explicitCollectOffset, AStringView::npos);
    ASSERT_NE(beginFrameOffset, AStringView::npos);
    const AStringView implicitCollect = timing.substr(
        implicitCollectOffset,
        explicitCollectOffset - implicitCollectOffset
    );
    const AStringView explicitCollect = timing.substr(explicitCollectOffset, beginFrameOffset - explicitCollectOffset);
    for(const AStringView collection : { implicitCollect, explicitCollect }){
        const usize stateLockOffset = collection.find("ScopedLock recorderLock(m_mutex);");
        const usize stagingOffset = collection.find("publishPerformanceSamples = collectLocked(", stateLockOffset);
        const usize sinkOffset = collection.find(
            "m_timing.recordSample(sample.scope, sample.durationSeconds, sample.sourceFrameIndex);",
            stagingOffset
        );
        const usize publishOffset = collection.find("m_timing.publishFrame(publishFrameIndex);", sinkOffset);
        const usize dispatchOffset = collection.find("dispatchCompletedSamples(completedSamples);", publishOffset);
        ASSERT_NE(stateLockOffset, AStringView::npos);
        ASSERT_NE(stagingOffset, AStringView::npos);
        ASSERT_NE(sinkOffset, AStringView::npos);
        ASSERT_NE(publishOffset, AStringView::npos);
        ASSERT_NE(dispatchOffset, AStringView::npos);
        EXPECT_LT(stateLockOffset, stagingOffset);
        EXPECT_LT(stagingOffset, sinkOffset);
        EXPECT_LT(sinkOffset, publishOffset);
        EXPECT_LT(publishOffset, dispatchOffset);
        EXPECT_TRUE(ContainsText(
            collection,
            "        }\n        for(const GpuTimingSinkSample& sample : performanceSamples)"
        ));
        EXPECT_TRUE(ContainsText(collection, "    }\n    dispatchCompletedSamples(completedSamples);"));
    }

    const usize resetQueriesOffset = timing.find("void GpuTimingRecorder::resetQueries(){");
    const usize collectOffset = timing.find("void GpuTimingRecorder::collect(", resetQueriesOffset);
    ASSERT_NE(resetQueriesOffset, AStringView::npos);
    ASSERT_NE(collectOffset, AStringView::npos);
    const AStringView resetQueries = timing.substr(resetQueriesOffset, collectOffset - resetQueriesOffset);
    const usize advanceEpochOffset = resetQueries.find("advanceEpoch();");
    const usize resetPerformanceCaptureOffset = resetQueries.find("m_performanceCollectionActive = false;");
    ASSERT_NE(advanceEpochOffset, AStringView::npos);
    ASSERT_NE(resetPerformanceCaptureOffset, AStringView::npos);
    EXPECT_LT(advanceEpochOffset, resetPerformanceCaptureOffset);
    EXPECT_TRUE(ContainsText(timing, "if(publishPerformanceSamples)\n            m_timing.publishFrame(publishFrameIndex);"));
    EXPECT_TRUE(ContainsText(timingAccumulator, "GpuTimingScopeSkipReason::QueryCapacityUnavailable"));
    EXPECT_TRUE(ContainsText(timingAccumulator, "GpuTimingScopeSkipReason::RecordingPositionUnavailable"));
    EXPECT_TRUE(ContainsText(timingAccumulator, "const bool retirementPending = quarantineRecord("));
    const usize queryResultOffset = timingAccumulator.find("if(!device.getTimerQueryResult(*record.query, result))");
    const usize sampleStageOffset = timingAccumulator.find("completedSamples.push_back(SampleDispatch{", queryResultOffset);
    const usize performanceSampleStageOffset = timingAccumulator.find(
        "performanceSamples.push_back(GpuTimingSinkSample{",
        sampleStageOffset
    );
    const usize derivedSampleStageOffset = timingAccumulator.find(
        "recorder.m_metricCorrelator.recordTimestampRange(",
        performanceSampleStageOffset
    );
    const usize queryReleaseOffset = timingAccumulator.find("releaseQuery(record);", derivedSampleStageOffset);
    ASSERT_NE(queryResultOffset, AStringView::npos);
    ASSERT_NE(sampleStageOffset, AStringView::npos);
    ASSERT_NE(performanceSampleStageOffset, AStringView::npos);
    ASSERT_NE(derivedSampleStageOffset, AStringView::npos);
    ASSERT_NE(queryReleaseOffset, AStringView::npos);
    EXPECT_LT(sampleStageOffset, performanceSampleStageOffset);
    EXPECT_LT(performanceSampleStageOffset, derivedSampleStageOffset);
    EXPECT_LT(derivedSampleStageOffset, queryReleaseOffset);
    EXPECT_FALSE(ContainsText(timingAccumulator, "recorder.m_timing.recordSample("));
    EXPECT_FALSE(ContainsText(timingMetricCorrelator, "m_timing.recordSample("));

    const usize correlatorRangeOffset = timingMetricCorrelator.find(
        "void GpuTimingMetricCorrelator::recordTimestampRange("
    );
    const usize packetEnvelopeOffset = timingMetricCorrelator.find(
        "for(auto it = m_pendingPacketEnvelopeMetrics.begin();",
        correlatorRangeOffset
    );
    const usize outputRoleOffset = timingMetricCorrelator.find(
        "bool GpuTimingMetricCorrelator::hasOutputRole(",
        packetEnvelopeOffset
    );
    ASSERT_NE(correlatorRangeOffset, AStringView::npos);
    ASSERT_NE(packetEnvelopeOffset, AStringView::npos);
    ASSERT_NE(outputRoleOffset, AStringView::npos);
    const AStringView overlapRange = timingMetricCorrelator.substr(
        correlatorRangeOffset,
        packetEnvelopeOffset - correlatorRangeOffset
    );
    const AStringView packetEnvelopeRange = timingMetricCorrelator.substr(
        packetEnvelopeOffset,
        outputRoleOffset - packetEnvelopeOffset
    );
    const usize overlapStageOffset = overlapRange.find("performanceSamples.push_back(GpuTimingSinkSample{");
    const usize overlapRetireOffset = overlapRange.find("record.pendingFrames.erase(it);", overlapStageOffset);
    const usize packetReserveOffset = packetEnvelopeRange.find(
        "performanceSamples.reserve(performanceSamples.size() + 1u + it->queueOutputs.size());"
    );
    const usize packetScratchOffset = packetEnvelopeRange.find(
        "Vector<GpuComparableTimestampRange, Alloc::ScratchArena> packetRanges{scratchArena};",
        packetReserveOffset
    );
    const usize packetOverlapStageOffset = packetEnvelopeRange.find(
        "performanceSamples.push_back(GpuTimingSinkSample{",
        packetScratchOffset
    );
    const usize packetIdleStageOffset = packetEnvelopeRange.find(
        "performanceSamples.push_back(GpuTimingSinkSample{",
        packetOverlapStageOffset + 1u
    );
    const usize packetRetireOffset = packetEnvelopeRange.find(
        "it = m_pendingPacketEnvelopeMetrics.erase(it);",
        packetIdleStageOffset
    );
    ASSERT_NE(overlapStageOffset, AStringView::npos);
    ASSERT_NE(overlapRetireOffset, AStringView::npos);
    ASSERT_NE(packetReserveOffset, AStringView::npos);
    ASSERT_NE(packetScratchOffset, AStringView::npos);
    ASSERT_NE(packetOverlapStageOffset, AStringView::npos);
    ASSERT_NE(packetIdleStageOffset, AStringView::npos);
    ASSERT_NE(packetRetireOffset, AStringView::npos);
    EXPECT_LT(overlapStageOffset, overlapRetireOffset);
    EXPECT_LT(packetReserveOffset, packetScratchOffset);
    EXPECT_LT(packetScratchOffset, packetOverlapStageOffset);
    EXPECT_LT(packetOverlapStageOffset, packetIdleStageOffset);
    EXPECT_LT(packetIdleStageOffset, packetRetireOffset);

    const usize confirmQueryDeclarationOffset = timingHeader.find("[[nodiscard]] bool confirmQuery(");
    const usize prepareQueryForRecoveryDeclarationOffset = timingHeader.find(
        "[[nodiscard]] bool prepareQueryForRecovery(",
        confirmQueryDeclarationOffset
    );
    const usize confirmScopeDeclarationOffset = timingHeader.find("[[nodiscard]] bool confirmScope(");
    const usize prepareRecoveryDeclarationOffset = timingHeader.find(
        "[[nodiscard]] bool prepareDeferredScopeForRecovery(",
        confirmScopeDeclarationOffset
    );
    ASSERT_NE(confirmQueryDeclarationOffset, AStringView::npos);
    ASSERT_NE(prepareQueryForRecoveryDeclarationOffset, AStringView::npos);
    ASSERT_NE(confirmScopeDeclarationOffset, AStringView::npos);
    ASSERT_NE(prepareRecoveryDeclarationOffset, AStringView::npos);
    EXPECT_TRUE(ContainsText(timingHeader.substr(
        confirmQueryDeclarationOffset,
        prepareQueryForRecoveryDeclarationOffset - confirmQueryDeclarationOffset
    ), ")noexcept;"));
    EXPECT_TRUE(ContainsText(timingHeader.substr(
        confirmScopeDeclarationOffset,
        prepareRecoveryDeclarationOffset - confirmScopeDeclarationOffset
    ), ")noexcept;"));
    EXPECT_TRUE(ContainsText(
        timingHeader,
        "[[nodiscard]] bool resolveSubmission(const QueueSubmissionToken& token)noexcept;"
    ));
    EXPECT_TRUE(ContainsText(
        timingHeader,
        "[[nodiscard]] bool confirm(const QueueSubmissionToken& token)noexcept;"
    ));

    const usize confirmQueryDefinitionOffset = timingAccumulator.find("bool GpuTimingAccumulator::confirmQuery(");
    const usize prepareQueryForRecoveryDefinitionOffset = timingAccumulator.find(
        "bool GpuTimingAccumulator::prepareQueryForRecovery(",
        confirmQueryDefinitionOffset
    );
    const usize confirmScopeDefinitionOffset = timing.find("bool GpuTimingRecorder::confirmScope(");
    const usize prepareRecoveryDefinitionOffset = timing.find(
        "bool GpuTimingRecorder::prepareDeferredScopeForRecovery(",
        confirmScopeDefinitionOffset
    );
    const usize resolveSubmissionDefinitionOffset = timingSubmission.find(
        "bool GpuTimingSubmissionTicket::resolveSubmission(const QueueSubmissionToken& token)noexcept{"
    );
    const usize reserveScopeDefinitionOffset = timingSubmission.find(
        "usize GpuTimingSubmissionTicket::reserveScopePublication()",
        resolveSubmissionDefinitionOffset
    );
    const usize ticketConfirmDefinitionOffset = timingSubmission.find(
        "bool GpuTimingSubmissionTicket::confirm(const QueueSubmissionToken& token)noexcept{"
    );
    const usize ticketConfirmEndOffset = timingSubmission.find("\n}", ticketConfirmDefinitionOffset);
    ASSERT_NE(confirmQueryDefinitionOffset, AStringView::npos);
    ASSERT_NE(prepareQueryForRecoveryDefinitionOffset, AStringView::npos);
    ASSERT_NE(confirmScopeDefinitionOffset, AStringView::npos);
    ASSERT_NE(prepareRecoveryDefinitionOffset, AStringView::npos);
    ASSERT_NE(resolveSubmissionDefinitionOffset, AStringView::npos);
    ASSERT_NE(reserveScopeDefinitionOffset, AStringView::npos);
    ASSERT_NE(ticketConfirmDefinitionOffset, AStringView::npos);
    ASSERT_NE(ticketConfirmEndOffset, AStringView::npos);
    EXPECT_TRUE(ContainsText(timingAccumulator.substr(
        confirmQueryDefinitionOffset,
        prepareQueryForRecoveryDefinitionOffset - confirmQueryDefinitionOffset
    ), s_NOEXCEPT_BRACE));
    EXPECT_TRUE(ContainsText(timing.substr(
        confirmScopeDefinitionOffset,
        prepareRecoveryDefinitionOffset - confirmScopeDefinitionOffset
    ), s_NOEXCEPT_BRACE));
    EXPECT_TRUE(ContainsText(timing.substr(
        confirmScopeDefinitionOffset,
        prepareRecoveryDefinitionOffset - confirmScopeDefinitionOffset
    ), "NothrowScopedLock lock(m_mutex);"));
    const AStringView resolveSubmission = timingSubmission.substr(
        resolveSubmissionDefinitionOffset,
        reserveScopeDefinitionOffset - resolveSubmissionDefinitionOffset
    );
    EXPECT_TRUE(ContainsText(resolveSubmission, "abandonWithoutCallbacks();"));
    EXPECT_FALSE(ContainsText(resolveSubmission, "discardPreparedSubmission();"));
    const AStringView ticketConfirm = timingSubmission.substr(
        ticketConfirmDefinitionOffset,
        ticketConfirmEndOffset - ticketConfirmDefinitionOffset
    );
    EXPECT_TRUE(ContainsText(ticketConfirm, "NothrowScopedLock lock(m_mutex);"));
    EXPECT_TRUE(ContainsText(timingFrameTransaction, "beginScope(scopeDefinition.identity, device, commandList, attribution, false, m_scope)"));
    EXPECT_FALSE(ContainsText(timingFrameTransaction, "if(!device.supportsComparableGpuTimestamps(commandList.getResolvedDescription().physicalQueue))"));

}


// The frame timing query must record its published endpoint after the optional presentation contributor.
// A rejected endpoint remains recoverable through the separate non-publishing recovery task instead of silently publishing a partial frame duration.
TEST(EcsGraphics, FrameTimingUsesGraphOwnedTerminalPresentationEndpoint){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    AString taskGraphSource;
    AString frameTimingBeginSource;
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "task" / "gpu" / "frame_timing_begin_task.cpp",
        frameTimingBeginSource
    ));
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
            "deferred/task_graph_suffix_builder.h",
            "deferred/task_graph_suffix_builder.cpp",
            s_RENDERER_FRAME_PIPELINE_GRAPH_CPP,
            "renderer_frame_pipeline_graph_schedule.cpp",
        },
        taskGraphSource
    ));
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const AStringView frameTimingBegin(frameTimingBeginSource.data(), frameTimingBeginSource.size());

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
    EXPECT_TRUE(ContainsText(taskGraph, ".producer = outResult.frameTimingEndTask,"));
    EXPECT_TRUE(ContainsText(taskGraph, ".backBuffer = backbuffer,"));

    const AStringView shadowPrepare = taskGraph.substr(shadowPrepareOffset, meshViewSetupOffset - shadowPrepareOffset);
    EXPECT_FALSE(ContainsText(shadowPrepare, "frameTimingTransaction->begin("));
    const AStringView meshViewSetup = taskGraph.substr(meshViewSetupOffset, deferredPresentOffset - meshViewSetupOffset);
    EXPECT_FALSE(ContainsText(meshViewSetup, "frameTimingTransaction->begin("));
    EXPECT_TRUE(ContainsText(taskGraph, "m_deferredLightingTaskGraph.setNormalExecutionPrelude(m_deferredFrameTimingBeginTask)"));
    EXPECT_TRUE(ContainsText(taskGraph, ".scopeDefinition = RendererGpuTimingScope::s_Frame,"));
    EXPECT_TRUE(ContainsText(frameTimingBegin, "frameTimingTransaction->begin("));
    EXPECT_TRUE(ContainsText(frameTimingBegin, "frameTimingTransaction->confirmBeginSubmission(token)"));

    const AStringView recovery = taskGraph.substr(recoveryOffset, deferredPresentOffset - recoveryOffset);
    EXPECT_TRUE(ContainsText(recovery, "frameTimingTransaction->recordEnd(commandList)"));
    EXPECT_TRUE(ContainsText(recovery, "confirmEndSubmission(token, false)"));
    EXPECT_TRUE(ContainsText(system, "presentationEndpoint->producer"));
    EXPECT_TRUE(ContainsText(system, "presentationEndpoint->queue != primaryGraphicsQueue"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredFrameTimingEndTask)"));

    const usize shadowPrepareAcceptanceOffset = system.find("FrameExecuteLifecycle::AcceptShadowPrepareTask(");
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
    EXPECT_FALSE(ContainsText(shadowPrepareAcceptance, "confirmBeginSubmission(token)"));
    EXPECT_TRUE(ContainsText(shadowPrepareAcceptance, ".task = m_deferredShadowPrepareTask,"));
    EXPECT_TRUE(ContainsText(shadowPrepareAcceptance, ".invoke = FrameExecuteLifecycle::AcceptShadowPrepareTask,"));
    EXPECT_TRUE(ContainsText(system, "frameTimingBeginQueue->id != primaryGraphicsQueue"));
    EXPECT_TRUE(ContainsText(system, "frameTimingBeginPacket.index != 0u"));
    EXPECT_TRUE(ContainsText(system, "frameTimingBeginSubmissionToken.matchesPhysicalQueue("));
    EXPECT_TRUE(ContainsText(system, "frameTimingTransaction.confirmEndSubmission(finalPresentationSubmissionToken, true)"));
    EXPECT_TRUE(ContainsText(system, "surfelCounterReadbackFollowsPresentation"));
    EXPECT_TRUE(ContainsText(system, "laggedLightingHistoryFollowsPresentation"));
    AString frameTailSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / "deferred" / "frame_tail_builder.cpp", frameTailSource));
    const AStringView frameTail(frameTailSource.data(), frameTailSource.size());
    EXPECT_TRUE(ContainsText(taskGraph, ".terminalPresentationTask = m_deferredFrameTimingEndTask,"));
    EXPECT_TRUE(ContainsText(frameTail, "const Core::GpuTaskId historyCopyDependencies[] = { inputs.terminalPresentationTask };"));
    EXPECT_TRUE(ContainsText(frameTail, ".setDependencies(historyCopyDependencies, LengthOf(historyCopyDependencies))"));
    EXPECT_FALSE(ContainsText(system, "acceptGraphicsPrefixBeginTask"));
}


// Split measures retain non-owning links to their recording tickets. Declare each ticket first
// reverse local destruction keeps it alive while an incomplete measure relinquishes its scope during exception unwinding.
TEST(EcsGraphics, RendererSplitGpuTimingTicketsOutliveTheirMeasures){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString renderSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_execute.cpp",
        renderSource
    ));
    const AStringView render(renderSource.data(), renderSource.size());

    const usize renderFunctionOffset = render.find("void RendererFramePipeline::render(");
    ASSERT_NE(renderFunctionOffset, AStringView::npos);
    const usize graphBuildOffset = render.find("buildDeferredLightingTaskGraph(", renderFunctionOffset);
    ASSERT_NE(graphBuildOffset, AStringView::npos);
    const AStringView timingOwners = render.substr(renderFunctionOffset, graphBuildOffset - renderFunctionOffset);

    const usize avboitPreTicketOffset = timingOwners.find("GpuTimingSubmissionTicket avboitPreTimingTicket");
    const usize avboitDepthWarpTicketOffset = timingOwners.find("GpuTimingSubmissionTicket avboitDepthWarpTimingTicket");
    const usize avboitExtinctionTicketOffset = timingOwners.find("GpuTimingSubmissionTicket avboitExtinctionTimingTicket");
    const usize avboitIntegrationTicketOffset = timingOwners.find("GpuTimingSubmissionTicket avboitIntegrationTimingTicket");
    const usize avboitAccumulationTicketOffset = timingOwners.find("GpuTimingSubmissionTicket avboitAccumulationTimingTicket");
    const usize deferredPresentTicketOffset = timingOwners.find("GpuTimingSubmissionTicket deferredPresentTimingTicket");
    const usize transparentCsgMeasureOffset = timingOwners.find("GpuTimingMeasure> transparentCsgIntervalsTiming");
    const usize occupancyMeasureOffset = timingOwners.find("GpuTimingMeasure> avboitOccupancyComputeEmulationTiming");
    const usize extinctionMeasureOffset = timingOwners.find("GpuTimingMeasure> avboitExtinctionComputeEmulationTiming");
    const usize accumulationMeasureOffset = timingOwners.find("GpuTimingMeasure> avboitAccumulationComputeEmulationTiming");
    const usize asyncFinalMeasureOffset = timingOwners.find("GpuTimingMeasure> asyncFinalTiming");

    ASSERT_NE(avboitPreTicketOffset, AStringView::npos);
    ASSERT_NE(avboitDepthWarpTicketOffset, AStringView::npos);
    ASSERT_NE(avboitExtinctionTicketOffset, AStringView::npos);
    ASSERT_NE(avboitIntegrationTicketOffset, AStringView::npos);
    ASSERT_NE(avboitAccumulationTicketOffset, AStringView::npos);
    ASSERT_NE(deferredPresentTicketOffset, AStringView::npos);
    ASSERT_NE(transparentCsgMeasureOffset, AStringView::npos);
    ASSERT_NE(occupancyMeasureOffset, AStringView::npos);
    ASSERT_NE(extinctionMeasureOffset, AStringView::npos);
    ASSERT_NE(accumulationMeasureOffset, AStringView::npos);
    ASSERT_NE(asyncFinalMeasureOffset, AStringView::npos);
    EXPECT_LT(avboitPreTicketOffset, transparentCsgMeasureOffset);
    EXPECT_LT(avboitPreTicketOffset, occupancyMeasureOffset);
    EXPECT_LT(avboitDepthWarpTicketOffset, transparentCsgMeasureOffset);
    EXPECT_LT(avboitExtinctionTicketOffset, extinctionMeasureOffset);
    EXPECT_LT(avboitIntegrationTicketOffset, transparentCsgMeasureOffset);
    EXPECT_LT(avboitAccumulationTicketOffset, accumulationMeasureOffset);
    EXPECT_LT(deferredPresentTicketOffset, asyncFinalMeasureOffset);
}


// Queue timing feedback is deliberately opt-in, but the two graph-owned AVBOIT Compute tasks must route accepted timestamp samples back into the next immutable compiler snapshot. Keep this source-level contract focused on the renderer boundary rather than coupling it to one physical queue topology.
TEST(EcsGraphics, TimingFeedbackRollsBackRejectedCollectionAndRetainsSubscriptionsThroughGraphDestruction){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    AString timingFeedbackSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / "renderer_frame_pipeline.cpp", systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / "kernel" / "task_timing_feedback.cpp", timingFeedbackSource));
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView timingFeedback(timingFeedbackSource.data(), timingFeedbackSource.size());

    const usize setPolicyOffset = timingFeedback.find("bool RendererTaskTimingFeedback::setPolicy(");
    const usize beginSampleOffset = timingFeedback.find(
        "Core::GpuTimingSampleAttribution RendererTaskTimingFeedback::beginSample("
    );
    ASSERT_NE(setPolicyOffset, AStringView::npos);
    ASSERT_NE(beginSampleOffset, AStringView::npos);
    const AStringView setPolicy = timingFeedback.substr(setPolicyOffset, beginSampleOffset - setPolicyOffset);
    const usize collectionResultOffset = setPolicy.find("bool collectionUpdated = false;");
    const usize resolutionGuardOffset = setPolicy.find("ScopeExit resolvePolicy([&]()noexcept{");
    const usize resolutionOffset = setPolicy.find(
        "ResolveRendererTaskTimingFeedbackPolicyTransition(m_policy, transition, collectionUpdated);"
    );
    const usize collectionUpdateOffset = setPolicy.find("collectionUpdated = m_graphics.gpuTiming().");
    ASSERT_NE(collectionResultOffset, AStringView::npos);
    ASSERT_NE(resolutionGuardOffset, AStringView::npos);
    ASSERT_NE(resolutionOffset, AStringView::npos);
    ASSERT_NE(collectionUpdateOffset, AStringView::npos);
    EXPECT_LT(collectionResultOffset, resolutionGuardOffset);
    EXPECT_LT(resolutionGuardOffset, resolutionOffset);
    EXPECT_LT(resolutionOffset, collectionUpdateOffset);
    EXPECT_FALSE(ContainsText(setPolicy, "resolvePolicy.release()"));
    EXPECT_FALSE(ContainsText(timingFeedback, "catch("));
    EXPECT_FALSE(ContainsText(timingFeedback, "throw;"));

    const usize activateOffset = timingFeedback.find("void RendererTaskTimingFeedback::activate(){");
    const usize feedbackDeactivateOffset = timingFeedback.find("void RendererTaskTimingFeedback::deactivate()noexcept{");
    ASSERT_NE(activateOffset, AStringView::npos);
    ASSERT_NE(feedbackDeactivateOffset, AStringView::npos);
    const AStringView activate = timingFeedback.substr(activateOffset, feedbackDeactivateOffset - activateOffset);
    const usize subscriptionGuardOffset = activate.find("ScopeExit discardSubscription(");
    const usize collectionEnableOffset = activate.find("timing.setFeedbackCollectionScopes(");
    const usize subscriptionPublicationOffset = activate.find("m_subscription = subscription;");
    const usize subscriptionReleaseOffset = activate.find("discardSubscription.release();");
    ASSERT_NE(subscriptionGuardOffset, AStringView::npos);
    ASSERT_NE(collectionEnableOffset, AStringView::npos);
    ASSERT_NE(subscriptionPublicationOffset, AStringView::npos);
    ASSERT_NE(subscriptionReleaseOffset, AStringView::npos);
    EXPECT_LT(subscriptionGuardOffset, collectionEnableOffset);
    EXPECT_LT(collectionEnableOffset, subscriptionPublicationOffset);
    EXPECT_LT(subscriptionPublicationOffset, subscriptionReleaseOffset);

    const usize resetOffset = timingFeedback.find("void RendererTaskTimingFeedback::reset()noexcept{");
    const usize sampleCallbackOffset = timingFeedback.find("void RendererTaskTimingFeedback::onGpuTimingSample(");
    ASSERT_NE(resetOffset, AStringView::npos);
    ASSERT_NE(sampleCallbackOffset, AStringView::npos);
    const AStringView reset = timingFeedback.substr(resetOffset, sampleCallbackOffset - resetOffset);
    EXPECT_TRUE(ContainsText(reset, "m_state.reset();"));
    EXPECT_TRUE(ContainsText(reset, "m_history.reset(m_snapshot);"));

    const usize destructorOffset = system.find("RendererFramePipeline::~RendererFramePipeline(){");
    const usize graphResetOffset = system.find("m_deferredLightingTaskGraph.reset();", destructorOffset);
    const usize deactivateOffset = system.find("m_deferredTaskTimingFeedback.deactivate();", destructorOffset);
    ASSERT_NE(destructorOffset, AStringView::npos);
    ASSERT_NE(graphResetOffset, AStringView::npos);
    ASSERT_NE(deactivateOffset, AStringView::npos);
    EXPECT_LT(graphResetOffset, deactivateOffset);

    const usize acceptOffset = timingFeedback.find("void RendererTaskTimingFeedback::acceptSubmission(");
    const usize discardOffset = timingFeedback.find("void RendererTaskTimingFeedback::discardRecording(", acceptOffset);
    ASSERT_NE(acceptOffset, AStringView::npos);
    ASSERT_NE(discardOffset, AStringView::npos);
    const AStringView accept = timingFeedback.substr(acceptOffset, discardOffset - acceptOffset);
    EXPECT_TRUE(ContainsText(accept, s_NOEXCEPT_BRACE));
    EXPECT_TRUE(ContainsText(accept, "m_state.acceptSubmission(attribution, token, m_active);"));
    EXPECT_FALSE(ContainsText(accept, "m_history"));
    EXPECT_FALSE(ContainsText(accept, ".erase("));
    EXPECT_FALSE(ContainsText(accept, "NWB_LOGGER"));

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

