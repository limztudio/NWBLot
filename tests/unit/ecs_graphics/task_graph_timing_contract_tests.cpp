// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_timing_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


// Timing availability and outcomes belong to the device-wide recorder rather than one compiled graph attempt. Keep
// the snapshot by value, export explicit skip reasons, and enumerate every live physical queue even on no-graph
// frames so unsupported capabilities remain distinguishable from measured zero-duration work.
TEST(EcsGraphics, FrameGraphExportsDeviceWideGpuTimingCapabilitiesAndOutcomes){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString timingHeaderSource;
    AString timingSource;
    AString timingAccumulatorSource;
    AString timingMetricCorrelatorSource;
    AString timingSubmissionSource;
    AString frameGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "gpu_timing.h", timingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "gpu_timing.cpp", timingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "gpu_timing_accumulator.cpp", timingAccumulatorSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "gpu_timing_metric_correlator.cpp",
        timingMetricCorrelatorSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "gpu_timing_submission.cpp", timingSubmissionSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_telemetry.cpp", frameGraphSource));
    const AStringView timingHeader(timingHeaderSource.data(), timingHeaderSource.size());
    const AStringView timing(timingSource.data(), timingSource.size());
    const AStringView timingAccumulator(timingAccumulatorSource.data(), timingAccumulatorSource.size());
    const AStringView timingMetricCorrelator(timingMetricCorrelatorSource.data(), timingMetricCorrelatorSource.size());
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
    EXPECT_TRUE(ContainsText(timingAccumulator, "++m_publishedSampleCount;"));
    EXPECT_TRUE(ContainsText(timingAccumulator, "++m_unpublishedSampleCount;"));
    EXPECT_TRUE(ContainsText(timingHeader, "bool m_performanceCollectionActive = false;"));
    EXPECT_TRUE(ContainsText(timing, "const bool performanceCollectionActive = m_enabled && m_timing.enabled();"));
    EXPECT_TRUE(ContainsText(timing, "m_performanceCollectionActive = performanceCollectionActive;"));
    EXPECT_TRUE(ContainsText(
        timing,
        "m_accumulatorsActive = m_performanceCollectionActive || !m_feedbackScopeDemands.empty();"
    ));

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
    EXPECT_EQ(CountText(timing, "ScopedLock collectionLock(m_collectionMutex);"), 2u);
    EXPECT_EQ(CountText(timing, "for(const GpuTimingSinkSample& sample : performanceSamples)"), 2u);
    EXPECT_EQ(CountText(
        timing,
        "m_timing.recordSample(sample.scope, sample.durationSeconds, sample.sourceFrameIndex);"
    ), 2u);
    EXPECT_EQ(CountText(timing, "m_timing.publishFrame(publishFrameIndex);"), 2u);

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
    EXPECT_TRUE(ContainsText(timing, "++m_statistics.scopeAttemptCount;"));
    EXPECT_TRUE(ContainsText(timing, "GpuTimingScopeSkipReason::CollectionInactive"));
    EXPECT_TRUE(ContainsText(timing, "GpuTimingScopeSkipReason::QueueTimestampsUnsupported"));
    EXPECT_TRUE(ContainsText(timing, "GpuTimingScopeSkipReason::ComparableTimestampsUnsupported"));
    EXPECT_TRUE(ContainsText(timing, "GpuTimingScopeSkipReason::ScopeNotPrepared"));
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
    const usize retireQueryDeclarationOffset = timingHeader.find(
        "[[nodiscard]] bool retireQuery(",
        confirmQueryDeclarationOffset
    );
    const usize confirmScopeDeclarationOffset = timingHeader.find("[[nodiscard]] bool confirmScope(");
    const usize prepareRecoveryDeclarationOffset = timingHeader.find(
        "[[nodiscard]] bool prepareDeferredScopeForRecovery(",
        confirmScopeDeclarationOffset
    );
    ASSERT_NE(confirmQueryDeclarationOffset, AStringView::npos);
    ASSERT_NE(retireQueryDeclarationOffset, AStringView::npos);
    ASSERT_NE(confirmScopeDeclarationOffset, AStringView::npos);
    ASSERT_NE(prepareRecoveryDeclarationOffset, AStringView::npos);
    EXPECT_TRUE(ContainsText(timingHeader.substr(
        confirmQueryDeclarationOffset,
        retireQueryDeclarationOffset - confirmQueryDeclarationOffset
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
    const usize retireQueryDefinitionOffset = timingAccumulator.find(
        "bool GpuTimingAccumulator::retireQuery(",
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
    const usize frameTransactionDefinitionOffset = timingSubmission.find(
        "GpuTimingFrameTransaction::GpuTimingFrameTransaction(",
        ticketConfirmDefinitionOffset
    );
    ASSERT_NE(confirmQueryDefinitionOffset, AStringView::npos);
    ASSERT_NE(retireQueryDefinitionOffset, AStringView::npos);
    ASSERT_NE(confirmScopeDefinitionOffset, AStringView::npos);
    ASSERT_NE(prepareRecoveryDefinitionOffset, AStringView::npos);
    ASSERT_NE(resolveSubmissionDefinitionOffset, AStringView::npos);
    ASSERT_NE(reserveScopeDefinitionOffset, AStringView::npos);
    ASSERT_NE(ticketConfirmDefinitionOffset, AStringView::npos);
    ASSERT_NE(frameTransactionDefinitionOffset, AStringView::npos);
    EXPECT_TRUE(ContainsText(timingAccumulator.substr(
        confirmQueryDefinitionOffset,
        retireQueryDefinitionOffset - confirmQueryDefinitionOffset
    ), ")noexcept{"));
    EXPECT_TRUE(ContainsText(timing.substr(
        confirmScopeDefinitionOffset,
        prepareRecoveryDefinitionOffset - confirmScopeDefinitionOffset
    ), ")noexcept{"));
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
        frameTransactionDefinitionOffset - ticketConfirmDefinitionOffset
    );
    EXPECT_TRUE(ContainsText(ticketConfirm, "NothrowScopedLock lock(m_mutex);"));
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
            "deferred/task_graph_suffix_builder.h",
            "deferred/task_graph_suffix_builder.cpp",
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
    EXPECT_TRUE(ContainsText(taskGraph, ".producer = outResult.frameTimingEndTask,"));
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


// Split measures retain non-owning links to their recording tickets. Declare each ticket first so reverse local
// destruction keeps it alive while an incomplete measure relinquishes its scope during exception unwinding.
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
    EXPECT_TRUE(ContainsText(metricHelper, "const Core::GpuCompiledPacketView packetView = compiledGraph.packet(packetID);"));
    EXPECT_TRUE(ContainsText(metricHelper, "graph.taskAt(packetView.tasks[0u].index).identity"));
    EXPECT_TRUE(ContainsText(metricHelper, ".physicalQueue = packetView.plan->queue,"));
    EXPECT_TRUE(ContainsText(metricHelper, "DeferredGraphQueueInternalIdle(packetView.plan->queue, scratchArena)"));
    EXPECT_TRUE(ContainsText(metricHelper, "RendererGpuTimingScope::s_DeferredGraphQueueOverlap.identity"));
    EXPECT_TRUE(ContainsText(metricHelper, "timingRecorder.preparePacketEnvelopeMetrics("));

    const usize metricPrepareOffset = build.find(
        "|| !__hidden_task_graph_deferred_lighting::PreparePacketEnvelopeMetrics(",
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


// Queue timing feedback is deliberately opt-in, but the two graph-owned AVBOIT Compute tasks must route accepted
// timestamp samples back into the next immutable compiler snapshot. Keep this source-level contract focused on the
// renderer boundary rather than coupling it to one physical queue topology.
TEST(EcsGraphics, DeferredGraphWiresAcceptedTaskTimingFeedback){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemHeaderSource;
    AString systemSource;
    AString timingFeedbackHeaderSource;
    AString timingFeedbackSource;
    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", systemHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.cpp", systemSource));
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
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView timingFeedbackHeader(timingFeedbackHeaderSource.data(), timingFeedbackHeaderSource.size());
    const AStringView timingFeedback(timingFeedbackSource.data(), timingFeedbackSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    EXPECT_TRUE(ContainsText(timingFeedbackHeader, "class RendererTaskTimingFeedbackState final"));
    EXPECT_TRUE(ContainsText(timingFeedbackHeader, "class RendererTaskTimingFeedback final"));
    EXPECT_TRUE(ContainsText(timingFeedbackHeader, "Core::GpuTimingSampleSubscription m_subscription"));
    EXPECT_TRUE(ContainsText(
        timingFeedbackHeader,
        "Vector<Name, Core::Alloc::GlobalArena> m_feedbackCollectionScopes"
    ));
    EXPECT_FALSE(ContainsText(timingFeedbackHeader, "m_nextAttribution"));
    EXPECT_TRUE(ContainsText(timingFeedback, "subscribeSampleListener(Core::GpuTimingSampleListener{"));
    EXPECT_TRUE(ContainsText(timingFeedback, "unsubscribeSampleListener(subscription)"));
    EXPECT_TRUE(ContainsText(
        timingFeedback,
        "m_feedbackCollectionScopes.assign(scopeNames, scopeNames + feedbackCollectionScopeCount);"
    ));
    EXPECT_EQ(CountText(timingFeedback, "setFeedbackCollectionScopes("), 2u);
    EXPECT_EQ(CountText(timingFeedback, "clearFeedbackCollectionScopes("), 1u);
    EXPECT_FALSE(ContainsText(timingFeedback, "setFeedbackCollectionEnabled("));
    EXPECT_TRUE(ContainsText(timingFeedback, "!scopeName || !collectsScope(scopeName)"));
    EXPECT_TRUE(ContainsText(timingFeedback, "PrepareRendererTaskTimingFeedbackPolicyTransition(m_policy, policy, m_active)"));
    EXPECT_TRUE(ContainsText(
        timingFeedback,
        "ResolveRendererTaskTimingFeedbackPolicyTransition(m_policy, transition, collectionUpdated)"
    ));
    EXPECT_TRUE(ContainsText(timingFeedback, "m_graphics.gpuTiming().allocateSampleAttribution()"));
    EXPECT_TRUE(ContainsText(timingFeedback, "!m_active || !m_policy.enabled || !m_subscription.valid()"));
    EXPECT_TRUE(ContainsText(timingFeedback, "sample.physicalQueue != pending.expectedQueue"));
    EXPECT_TRUE(ContainsText(timingFeedbackHeader, "bool recordsNonCommittingTimingSample = false"));
    EXPECT_TRUE(ContainsText(timingFeedbackHeader, "bool submissionResolved = false"));
    EXPECT_TRUE(ContainsText(timingFeedbackHeader, "bool sampleResolved = false"));
    EXPECT_TRUE(ContainsText(timingFeedback, "history.recordNonCommittingSample("));
    EXPECT_TRUE(ContainsText(timingFeedback, "drainResult = m_state.drain(m_history, deviceGeneration);"));
    EXPECT_TRUE(ContainsText(timingFeedback, "options.queueAssignmentOptions.timingHistory = nullptr;"));
    EXPECT_TRUE(ContainsText(timingFeedback, "options.queueAssignmentOptions.timingFeedbackPolicy = {};"));
    EXPECT_TRUE(ContainsText(timingFeedback, "options.queueAssignmentOptions.timingFrameIndex = 0u;"));
    EXPECT_TRUE(ContainsText(systemHeader, "RendererTaskTimingFeedback m_deferredTaskTimingFeedback"));
    EXPECT_TRUE(ContainsText(timingFeedback, "m_active && m_subscription.valid() && m_policy.enabled"));

    const usize feedbackScopesOffset = system.find("static constexpr Name s_DeferredTaskTimingFeedbackScopes[] = {");
    const usize feedbackScopesEnd = system.find("};", feedbackScopesOffset);
    ASSERT_NE(feedbackScopesOffset, AStringView::npos);
    ASSERT_NE(feedbackScopesEnd, AStringView::npos);
    const AStringView feedbackScopes = system.substr(feedbackScopesOffset, feedbackScopesEnd - feedbackScopesOffset);
    EXPECT_EQ(CountText(feedbackScopes, "RendererGpuTimingScope::"), 2u);
    EXPECT_TRUE(ContainsText(feedbackScopes, "RendererGpuTimingScope::s_AvboitDepthWarp.identity"));
    EXPECT_TRUE(ContainsText(feedbackScopes, "RendererGpuTimingScope::s_AvboitIntegration.identity"));
    EXPECT_TRUE(ContainsText(
        system,
        "NotNull<const Name*>(__hidden_renderer_frame_pipeline::s_DeferredTaskTimingFeedbackScopes)"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "LengthOf(__hidden_renderer_frame_pipeline::s_DeferredTaskTimingFeedbackScopes)"
    ));

    const usize feedbackDeclarationOffset = systemHeader.find("RendererTaskTimingFeedback m_deferredTaskTimingFeedback");
    const usize graphDeclarationOffset = systemHeader.find("Core::GpuTaskGraph m_deferredLightingTaskGraph");
    const usize rayTracingSystemDeclarationOffset = systemHeader.find("RendererRayTracingSystem m_raytracingSystem");
    ASSERT_NE(feedbackDeclarationOffset, AStringView::npos);
    ASSERT_NE(graphDeclarationOffset, AStringView::npos);
    ASSERT_NE(rayTracingSystemDeclarationOffset, AStringView::npos);
    EXPECT_LT(rayTracingSystemDeclarationOffset, feedbackDeclarationOffset);
    EXPECT_LT(feedbackDeclarationOffset, graphDeclarationOffset);

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
    EXPECT_TRUE(ContainsText(accept, ")noexcept{"));
    EXPECT_TRUE(ContainsText(accept, "m_state.acceptSubmission(attribution, token, m_active);"));
    EXPECT_FALSE(ContainsText(accept, "m_history"));
    EXPECT_FALSE(ContainsText(accept, ".erase("));
    EXPECT_FALSE(ContainsText(accept, "NWB_LOGGER"));

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
        EXPECT_TRUE(ContainsText(task, "compiledTask.plan->recordsNonCommittingTimingSample"));
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

