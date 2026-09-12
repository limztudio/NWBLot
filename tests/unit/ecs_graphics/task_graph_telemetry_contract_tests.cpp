// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_telemetry_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


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
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "\"Task graph: tasks={} resources={} resource versions={} version edges={} packets={} deps={} transitions={}\\n\""
    ));
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
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.resourceVersionCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.resourceVersionEdgeCount,"));
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
        ".resourceVersionCount = static_cast<u64>(compileStatistics.resourceVersionCount),"
    ));
    EXPECT_TRUE(ContainsText(
        runtimeStatistics,
        ".resourceVersionEdgeCount = static_cast<u64>(compileStatistics.resourceVersionEdgeCount),"
    ));
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


// Renderer-side graph declaration is intentionally separate from core compilation, and a failed attempt must not
// publish its elapsed time because the compiler only publishes the supplied value with a completed immutable plan.
TEST(EcsGraphics, DeferredGraphMeasuresDeclarationAttemptBeforeCoreCompile){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "renderer_frame_pipeline_graph.cpp" }, taskGraphSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    const usize lightingOffset = taskGraph.find("void RendererFramePipeline::buildDeferredLightingTaskGraph");
    const usize resetOffset = taskGraph.find("resetDeferredTaskGraphRuntime();", lightingOffset);
    const usize declarationBeginOffset = taskGraph.find("const Timer declarationBegin = TimerNow();", resetOffset);
    const usize tailDeclarationOffset = taskGraph.find("if(!deferredFrameTailBuilder.declare(", declarationBeginOffset);
    const usize feedbackOffset = taskGraph.find(
        "m_deferredTaskTimingFeedback.configureCompileOptions(",
        tailDeclarationOffset
    );
    const usize declarationEndOffset = taskGraph.find(
        "compileOptions.declarationSeconds = DurationInSeconds<f64>(TimerNow(), declarationBegin);",
        feedbackOffset
    );
    const usize compilerOffset = taskGraph.find("if(!compiler.compile(", declarationEndOffset);
    ASSERT_NE(lightingOffset, AStringView::npos);
    ASSERT_NE(resetOffset, AStringView::npos);
    ASSERT_NE(declarationBeginOffset, AStringView::npos);
    ASSERT_NE(tailDeclarationOffset, AStringView::npos);
    ASSERT_NE(feedbackOffset, AStringView::npos);
    ASSERT_NE(declarationEndOffset, AStringView::npos);
    ASSERT_NE(compilerOffset, AStringView::npos);
    EXPECT_TRUE(ContainsText(taskGraph, "#include <global/timer.h>"));
    EXPECT_LT(resetOffset, declarationBeginOffset);
    EXPECT_LT(declarationBeginOffset, tailDeclarationOffset);
    EXPECT_LT(tailDeclarationOffset, feedbackOffset);
    EXPECT_TRUE(ContainsText(taskGraph.substr(tailDeclarationOffset, feedbackOffset - tailDeclarationOffset), "))\n        return;"));
    EXPECT_LT(feedbackOffset, declarationEndOffset);
    EXPECT_LT(declarationEndOffset, compilerOffset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

