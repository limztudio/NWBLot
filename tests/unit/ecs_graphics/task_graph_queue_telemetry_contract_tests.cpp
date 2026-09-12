// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_queue_telemetry_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


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

    EXPECT_TRUE(ContainsText(systemHeader, "#include <core/task/gpu/queue_assignment_telemetry.h>"));
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
    const usize stateResetCallOffset = render.find("resetFrameTaskState();", previousRefreshOffset);
    ASSERT_NE(previousRefreshOffset, AStringView::npos);
    ASSERT_NE(stateResetCallOffset, AStringView::npos);
    EXPECT_LT(previousRefreshOffset, stateResetCallOffset);
    EXPECT_TRUE(ContainsText(render, "deferred queue-assignment history refresh failed before graph reset"));

    const usize stateResetOffset = resources.find("void RendererFramePipeline::resetFrameTaskState(){");
    const usize validResetOffset = resources.find("m_deferredLightingTaskGraphValid = false;", stateResetOffset);
    const usize runtimeResetOffset = resources.find("resetDeferredTaskGraphRuntime();", stateResetOffset);
    ASSERT_NE(stateResetOffset, AStringView::npos);
    ASSERT_NE(validResetOffset, AStringView::npos);
    ASSERT_NE(runtimeResetOffset, AStringView::npos);
    EXPECT_LT(stateResetOffset, validResetOffset);
    EXPECT_LT(validResetOffset, runtimeResetOffset);
    EXPECT_FALSE(ContainsText(resources.substr(stateResetOffset, runtimeResetOffset - stateResetOffset),
        "m_deferredLightingTaskGraphQueueAssignmentTelemetry.reset();"));

    const usize resetHelperOffset = resources.find("void RendererFramePipeline::resetDeferredTaskGraphRuntime()");
    const usize planReadOffset = resources.find("Core::GpuCompiledGraph::ReadView planAccess(", resetHelperOffset);
    const usize discardOffset = resources.find(
        "m_deferredLightingSubmissionTransaction.discardUnaccepted(",
        planReadOffset
    );
    const usize transactionResetOffset = resources.find(
        "m_deferredLightingSubmissionTransaction.reset(m_deferredLightingCompiledGraph);",
        discardOffset
    );
    const usize recordedResetOffset = resources.find(
        "m_deferredLightingRecordedGraph.reset(m_deferredLightingCompiledGraph);",
        transactionResetOffset
    );
    const usize graphResetOffset = resources.find("m_deferredLightingTaskGraph.reset();", recordedResetOffset);
    const usize analysisResetOffset = resources.find("m_deferredLightingTaskGraphAnalysis.reset();", graphResetOffset);
    const usize assignmentResetOffset = resources.find(
        "m_deferredLightingTaskGraphQueueAssignments.reset();",
        analysisResetOffset
    );
    const usize compiledResetOffset = resources.find("m_deferredLightingCompiledGraph.reset();", assignmentResetOffset);
    ASSERT_NE(resetHelperOffset, AStringView::npos);
    ASSERT_NE(planReadOffset, AStringView::npos);
    ASSERT_NE(discardOffset, AStringView::npos);
    ASSERT_NE(transactionResetOffset, AStringView::npos);
    ASSERT_NE(recordedResetOffset, AStringView::npos);
    ASSERT_NE(graphResetOffset, AStringView::npos);
    ASSERT_NE(analysisResetOffset, AStringView::npos);
    ASSERT_NE(assignmentResetOffset, AStringView::npos);
    ASSERT_NE(compiledResetOffset, AStringView::npos);
    EXPECT_LT(planReadOffset, discardOffset);
    EXPECT_LT(discardOffset, transactionResetOffset);
    EXPECT_LT(transactionResetOffset, recordedResetOffset);
    EXPECT_LT(recordedResetOffset, graphResetOffset);
    EXPECT_LT(graphResetOffset, analysisResetOffset);
    EXPECT_LT(analysisResetOffset, assignmentResetOffset);
    EXPECT_LT(assignmentResetOffset, compiledResetOffset);

    const usize currentRefreshOffset = frameGraph.find(
        "m_deferredLightingTaskGraphQueueAssignmentTelemetry.update("
    );
    const usize guardedExportOffset = frameGraph.find("else{", currentRefreshOffset);
    const usize telemetryOptionsOffset = frameGraph.find(
        "const Core::GpuTaskGraphTelemetryOptions deferredLightingTelemetryOptions",
        guardedExportOffset
    );
    const usize taskGraphExportOffset = frameGraph.find(
        "deferredTaskGraphView.appendFrameGraphTelemetry(",
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
    EXPECT_TRUE(ContainsText(frameGraph, ".compiledPlan = &deferredCompiledPlan,"));
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
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "task" / "gpu" / "compiled_graph.h", compiledGraphHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "task" / "gpu" / "compiled_graph.cpp", compiledGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_telemetry.cpp", frameGraphSource));
    const AStringView commandHeader(commandHeaderSource.data(), commandHeaderSource.size());
    const AStringView compiledGraphHeader(compiledGraphHeaderSource.data(), compiledGraphHeaderSource.size());
    const AStringView compiledGraph(compiledGraphSource.data(), compiledGraphSource.size());
    const AStringView frameGraph(frameGraphSource.data(), frameGraphSource.size());

    EXPECT_TRUE(ContainsText(commandHeader, "Borrowed immutable topology view; its producer owns the storage."));
    EXPECT_TRUE(ContainsText(commandHeader, "struct GpuCommandArenaStatistics{"));
    EXPECT_TRUE(ContainsText(commandHeader, "struct GpuCommandArenaWorkerStatistics{"));
    EXPECT_TRUE(ContainsText(commandHeader, "Direct recording is domain/index {0,0}"));
    EXPECT_TRUE(ContainsText(commandHeader, "The storage estimate covers client-visible pool and"));
    EXPECT_TRUE(ContainsText(compiledGraphHeader, "GpuPhysicalQueueTopology queueTopology()const & noexcept;"));
    EXPECT_TRUE(ContainsText(compiledGraphHeader, "Pointer- and slice-bearing results borrow immutable plan storage."));
    EXPECT_TRUE(ContainsText(compiledGraphHeader, "deliberately reject calls on a temporary proof."));
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
    EXPECT_TRUE(ContainsText(frameGraph, "deferredCompiledPlan.queueTopology()"));
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
        "deferredCompiledPlan.physicalQueueCompileStatistics(queue)"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "m_deferredLightingRecordedGraph.physicalQueueRecordingStatistics(\n"
        "                        m_deferredLightingCompiledGraph,\n"
        "                        deferredCompiledPlan,\n"
        "                        queue\n"
        "                    )"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "m_deferredLightingSubmissionTransaction.physicalQueueSubmissionStatistics(\n"
        "                        deferredCompiledPlan,\n"
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
        "deferredCompiledPlan.physicalQueueCompileStatistics(queue)"
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
        "deferredCompiledPlan.packetIdAt(packetIndex)"
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

    EXPECT_EQ(CountText(frameGraph, "deferredCompiledPlan.logicalOwnershipTransfers()"), 1u);
    EXPECT_EQ(CountText(frameGraph, "deferredCompiledPlan.logicalOwnershipTransferAt("), 0u);
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "const Core::GpuCompiledOwnershipTransfer* const logicalOwnershipTransfers =\n"
        "            deferredCompiledPlan.logicalOwnershipTransfers()"
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

