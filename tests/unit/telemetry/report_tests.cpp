// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"
#include <gtest/gtest.h>
#include "frame_graph_test_helpers.h"
#include "frame_graph_wire_test_helpers.h"
#include <logger/telemetry/ingest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_report_tests{


using namespace TelemetryTestDetail;



TEST(Telemetry, TelemetryReportSummarizesBenchmarkEvents){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    EXPECT_TRUE(Telemetry::RecordTextLog(
        recorder,
        NWB::Core::Common::LogType::Info,
        NWB_TEXT("benchmark report"),
        4u,
        1u
    ));

    const Name cpuScopeName("gbuffer");
    NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();
    stats.sampleCount = 1u;
    stats.firstSampleFrameIndex = stats.publishFrameIndex;
    stats.lastSampleFrameIndex = stats.publishFrameIndex;
    EXPECT_TRUE(Telemetry::RecordPerfTiming(recorder, Telemetry::PerfTimingSource::Cpu, cpuScopeName, "gbuffer", stats, 2u));

    const Name memoryScopeName("memory/project_arena");
    const NWB::Core::Perf::MemorySnapshot snapshot = MakeTestMemorySnapshot(memoryScopeName);
    const NWB::Core::Perf::MemoryDelta delta = MakeTestMemoryDelta();
    EXPECT_TRUE(Telemetry::RecordPerfMemory(recorder, memoryScopeName, "memory/project_arena", snapshot, delta, 3u));

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);
    EXPECT_TRUE(Telemetry::RecordFrameGraph(recorder, stats.publishFrameIndex, nodes, edges, 4u));

    Log::TelemetryReport report(testArena.arena);
    EXPECT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));

    EXPECT_EQ(report.summary.eventCount, 4u);
    EXPECT_EQ(report.summary.eventKindCounts[static_cast<usize>(Telemetry::EventKind::TextLog)], 1u);
    EXPECT_EQ(report.summary.eventKindCounts[static_cast<usize>(Telemetry::EventKind::PerfFrame)], 1u);
    EXPECT_EQ(report.summary.eventKindCounts[static_cast<usize>(Telemetry::EventKind::MemoryFrame)], 1u);
    EXPECT_EQ(report.summary.eventKindCounts[static_cast<usize>(Telemetry::EventKind::FrameGraphFrame)], 1u);
    EXPECT_EQ(report.summary.parseFailureCount, 0u);
    EXPECT_TRUE(report.summary.hasFrameRange);
    EXPECT_EQ(report.summary.minFrameIndex, 4u);
    EXPECT_EQ(report.summary.maxFrameIndex, snapshot.frameIndex);
    EXPECT_EQ(report.summary.cpuTimingEventCount, 1u);
    EXPECT_EQ(report.summary.cpuTimingSampleCount, stats.sampleCount);
    EXPECT_EQ(report.summary.cpuTimingSeconds, stats.seconds);
    EXPECT_EQ(report.summary.memoryEventCount, 1u);
    const Log::TelemetryMemorySummary& memory = report.summary.memorySources[NWB::Core::Perf::MemorySource::ExplicitScope];
    EXPECT_EQ(memory.eventCount, 1u);
    EXPECT_EQ(memory.maxUsedBytes, snapshot.usedBytes);
    EXPECT_EQ(memory.maxPeakUsedBytes, snapshot.peakUsedBytes);
    EXPECT_EQ(memory.totalUsedDeltaBytes, delta.usedBytes);
    EXPECT_EQ(report.summary.frameGraphFrameCount, 1u);
    EXPECT_EQ(report.summary.frameGraphNodeCount, 3u);
    EXPECT_EQ(report.summary.frameGraphEdgeCount, 2u);
    EXPECT_TRUE(ContainsText(AStringView(report.json.data(), report.json.size()), "\"eventCount\": 4"));
    EXPECT_FALSE(ContainsText(AStringView(report.json.data(), report.json.size()), "\"maxMemoryUsedBytes\":"));
    EXPECT_FALSE(ContainsText(AStringView(report.json.data(), report.json.size()), "\"maxMemoryPeakUsedBytes\":"));
    EXPECT_FALSE(ContainsText(AStringView(report.json.data(), report.json.size()), "\"totalMemoryUsedDeltaBytes\":"));
    EXPECT_TRUE(ContainsText(AStringView(report.perfCsv.data(), report.perfCsv.size()), "source,scope,publish_frame"));
    EXPECT_TRUE(ContainsText(AStringView(report.perfCsv.data(), report.perfCsv.size()), "cpu,gbuffer"));
    EXPECT_TRUE(ContainsText(AStringView(report.graph.data(), report.graph.size()), "GBuffer Pass\\n125.000 ms"));
}

TEST(Telemetry, TelemetryReportPreservesEveryFrameGraphAndCorrelatesTimingByFrame){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    const Name gbufferScopeName("gbuffer");
    NWB::Core::Perf::TimingStats firstTiming = MakeTestTimingStats();
    firstTiming.seconds = 0.041;
    firstTiming.sampleCount = 1u;
    firstTiming.publishFrameIndex = 41u;
    firstTiming.firstSampleFrameIndex = 40u;
    firstTiming.lastSampleFrameIndex = 40u;
    ASSERT_TRUE(Telemetry::RecordPerfTiming(
        recorder,
        Telemetry::PerfTimingSource::Gpu,
        gbufferScopeName,
        "gbuffer",
        firstTiming,
        70u
    ));

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, 40u, nodes, edges, 7u));

    NWB::Core::Perf::TimingStats secondTiming = MakeTestTimingStats();
    secondTiming.seconds = 0.042;
    secondTiming.sampleCount = 1u;
    secondTiming.publishFrameIndex = 42u;
    secondTiming.firstSampleFrameIndex = 41u;
    secondTiming.lastSampleFrameIndex = 41u;
    ASSERT_TRUE(Telemetry::RecordPerfTiming(
        recorder,
        Telemetry::PerfTimingSource::Gpu,
        gbufferScopeName,
        "gbuffer",
        secondTiming,
        80u
    ));

    nodes[0u].label = "Second GBuffer Pass";
    nodes[0u].flags = 129u;
    edges[0u].flags = 64u;
    edges[1u].flags = 3u;
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, 41u, nodes, edges, 8u));

    nodes[0u].label = "Unmatched GBuffer Pass";
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, 42u, nodes, edges, 9u));

    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));
    EXPECT_EQ(report.summary.frameGraphFrameCount, 3u);

    const AStringView json(report.json.data(), report.json.size());
    const usize firstJsonGraph = json.find("\"frameIndex\": 40");
    const usize secondJsonGraph = json.find("\"frameIndex\": 41");
    const usize thirdJsonGraph = json.find("\"frameIndex\": 42");
    ASSERT_NE(firstJsonGraph, AStringView::npos);
    ASSERT_NE(secondJsonGraph, AStringView::npos);
    ASSERT_NE(thirdJsonGraph, AStringView::npos);
    EXPECT_LT(firstJsonGraph, secondJsonGraph);
    EXPECT_LT(secondJsonGraph, thirdJsonGraph);
    const AStringView firstJsonRecord = json.substr(firstJsonGraph, secondJsonGraph - firstJsonGraph);
    const AStringView secondJsonRecord = json.substr(secondJsonGraph, thirdJsonGraph - secondJsonGraph);
    const AStringView thirdJsonRecord = json.substr(thirdJsonGraph);
    EXPECT_TRUE(ContainsText(firstJsonRecord, "\"streamId\": 7"));
    EXPECT_TRUE(ContainsText(secondJsonRecord, "\"streamId\": 8"));
    EXPECT_TRUE(ContainsText(thirdJsonRecord, "\"streamId\": 9"));
    EXPECT_TRUE(ContainsText(firstJsonRecord, "\"label\": \"GBuffer Pass\", \"kind\": \"pass\", \"flags\": 1"));
    EXPECT_TRUE(ContainsText(secondJsonRecord, "\"label\": \"Second GBuffer Pass\", \"kind\": \"pass\", \"flags\": 129"));
    EXPECT_TRUE(ContainsText(thirdJsonRecord, "\"label\": \"Unmatched GBuffer Pass\", \"kind\": \"pass\", \"flags\": 129"));
    EXPECT_TRUE(ContainsText(firstJsonRecord, "\"from\": 1, \"to\": 2, \"kind\": \"reads\", \"flags\": 2"));
    EXPECT_TRUE(ContainsText(secondJsonRecord, "\"from\": 1, \"to\": 2, \"kind\": \"reads\", \"flags\": 3"));
    EXPECT_TRUE(ContainsText(firstJsonRecord, "\"from\": 0, \"to\": 1, \"kind\": \"writes\", \"flags\": 0"));
    EXPECT_TRUE(ContainsText(secondJsonRecord, "\"from\": 0, \"to\": 1, \"kind\": \"writes\", \"flags\": 64"));

    char gbufferIdentityText[NameDetail::s_DebugHashTextLength + 1u] = {};
    NameDetail::HashToDebugString(Name("gbuffer").hash(), gbufferIdentityText, sizeof(gbufferIdentityText));
    constexpr AStringView identityPrefix = "\"identity\": \"";
    const usize identityOffset = json.find(identityPrefix);
    ASSERT_NE(identityOffset, AStringView::npos);
    EXPECT_EQ(
        json.substr(identityOffset + identityPrefix.size(), NameDetail::s_DebugHashTextLength),
        AStringView(gbufferIdentityText, NameDetail::s_DebugHashTextLength)
    );

    const AStringView dot(report.graph.data(), report.graph.size());
    const usize firstDotGraph = dot.find("digraph frame_graph_40_7_0");
    const usize secondDotGraph = dot.find("digraph frame_graph_41_8_1");
    const usize thirdDotGraph = dot.find("digraph frame_graph_42_9_2");
    ASSERT_NE(firstDotGraph, AStringView::npos);
    ASSERT_NE(secondDotGraph, AStringView::npos);
    ASSERT_NE(thirdDotGraph, AStringView::npos);
    EXPECT_LT(firstDotGraph, secondDotGraph);
    EXPECT_LT(secondDotGraph, thirdDotGraph);
    EXPECT_EQ(dot.find("digraph frame_graph_", thirdDotGraph + 1u), AStringView::npos);
    const AStringView firstDotRecord = dot.substr(firstDotGraph, secondDotGraph - firstDotGraph);
    const AStringView secondDotRecord = dot.substr(secondDotGraph, thirdDotGraph - secondDotGraph);
    const AStringView thirdDotRecord = dot.substr(thirdDotGraph);
    EXPECT_TRUE(ContainsText(firstDotRecord, AStringView(gbufferIdentityText, NameDetail::s_DebugHashTextLength)));
    EXPECT_TRUE(ContainsText(secondDotRecord, AStringView(gbufferIdentityText, NameDetail::s_DebugHashTextLength)));
    EXPECT_TRUE(ContainsText(thirdDotRecord, AStringView(gbufferIdentityText, NameDetail::s_DebugHashTextLength)));
    EXPECT_TRUE(ContainsText(firstDotRecord, "GBuffer Pass\\n41.000 ms"));
    EXPECT_TRUE(ContainsText(secondDotRecord, "Second GBuffer Pass\\n42.000 ms"));
    EXPECT_TRUE(ContainsText(thirdDotRecord, "Unmatched GBuffer Pass"));
    EXPECT_FALSE(ContainsText(thirdDotRecord, " ms"));
    EXPECT_TRUE(ContainsText(firstDotRecord, "kind=\"pass\", flags=1"));
    EXPECT_TRUE(ContainsText(secondDotRecord, "kind=\"pass\", flags=129"));
    EXPECT_TRUE(ContainsText(firstDotRecord, "label=\"reads\", flags=2"));
    EXPECT_TRUE(ContainsText(secondDotRecord, "label=\"reads\", flags=3"));
    EXPECT_TRUE(ContainsText(firstDotRecord, "label=\"writes\", flags=0"));
    EXPECT_TRUE(ContainsText(secondDotRecord, "label=\"writes\", flags=64"));
}

TEST(Telemetry, TelemetryReportDoesNotAttachAggregatedTimingToOneGraph){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    NWB::Core::Perf::TimingStats aggregatedTiming = MakeTestTimingStats();
    aggregatedTiming.seconds = 0.043;
    aggregatedTiming.sampleCount = 2u;
    aggregatedTiming.publishFrameIndex = 44u;
    aggregatedTiming.firstSampleFrameIndex = 43u;
    aggregatedTiming.lastSampleFrameIndex = 43u;
    ASSERT_TRUE(Telemetry::RecordPerfTiming(
        recorder,
        Telemetry::PerfTimingSource::Gpu,
        Name("gbuffer"),
        "gbuffer",
        aggregatedTiming,
        70u
    ));

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, 43u, nodes, edges, 7u));
    nodes[0u].label = "Publish Frame GBuffer Pass";
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, 44u, nodes, edges, 8u));

    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));
    const AStringView dot(report.graph.data(), report.graph.size());
    EXPECT_TRUE(ContainsText(dot, "digraph frame_graph_43_7_0"));
    EXPECT_TRUE(ContainsText(dot, "digraph frame_graph_44_8_1"));
    EXPECT_FALSE(ContainsText(dot, " ms"));
    EXPECT_EQ(report.summary.gpuTimingEventCount, 1u);
    EXPECT_EQ(report.summary.gpuTimingSampleCount, aggregatedTiming.sampleCount);
    EXPECT_EQ(report.summary.gpuTimingSeconds, aggregatedTiming.seconds);
    EXPECT_TRUE(ContainsText(AStringView(report.perfCsv.data(), report.perfCsv.size()), "gpu,gbuffer"));
}

TEST(Telemetry, TelemetryReportPreservesExactQueueAssignments){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    nodes[2u].queueAssignment.previousAcceptedQueue = {};
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, 52u, nodes, edges, 12u));

    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));

    const AStringView json(report.json.data(), report.json.size());
    const usize changedJsonOffset = json.find("\"label\": \"GBuffer Pass\"");
    const usize unassignedJsonOffset = json.find("\"label\": \"Albedo Texture\"");
    const usize rejectedJsonOffset = json.find("\"label\": \"Lighting Pass\"");
    const usize jsonEdgesOffset = json.find("\"edges\": [", rejectedJsonOffset);
    ASSERT_NE(changedJsonOffset, AStringView::npos);
    ASSERT_NE(unassignedJsonOffset, AStringView::npos);
    ASSERT_NE(rejectedJsonOffset, AStringView::npos);
    ASSERT_NE(jsonEdgesOffset, AStringView::npos);
    ASSERT_LT(changedJsonOffset, unassignedJsonOffset);
    ASSERT_LT(unassignedJsonOffset, rejectedJsonOffset);
    ASSERT_LT(rejectedJsonOffset, jsonEdgesOffset);

    const AStringView changedJson = json.substr(changedJsonOffset, unassignedJsonOffset - changedJsonOffset);
    const AStringView unassignedJson = json.substr(unassignedJsonOffset, rejectedJsonOffset - unassignedJsonOffset);
    const AStringView rejectedJson = json.substr(rejectedJsonOffset, jsonEdgesOffset - rejectedJsonOffset);
    EXPECT_TRUE(ContainsText(
        changedJson,
        "\"queueAssignment\": {\"initialQueue\": {\"index\": 1, \"deviceGeneration\": 17}, "
        "\"plannedQueue\": {\"index\": 3, \"deviceGeneration\": 17}, "
        "\"acceptedQueue\": {\"index\": 3, \"deviceGeneration\": 17}, "
        "\"previousAcceptedQueue\": {\"index\": 2, \"deviceGeneration\": 17}, \"queueClass\": \"compute\", "
        "\"reason\": \"fallback\", \"modifierMask\": 63, \"acceptance\": \"changed\", \"dedicated\": true, "
        "\"score\": {\"preference\": 11, \"overlap\": 7, \"queueLoad\": 3, \"incomingCrossings\": 2, "
        "\"outgoingCrossings\": 1, \"ownershipTransfers\": 4, \"total\": 8}}"
    ));
    EXPECT_TRUE(ContainsText(unassignedJson, "\"queueAssignment\": null"));
    EXPECT_TRUE(ContainsText(
        changedJson,
        "\"compiledTask\": {\"planGeneration\": 41, \"packetIndex\": 7, "
        "\"packetizationDecision\": \"firstTask\"}"
    ));
    EXPECT_TRUE(ContainsText(unassignedJson, "\"compiledTask\": null"));
    EXPECT_TRUE(ContainsText(
        rejectedJson,
        "\"compiledTask\": {\"planGeneration\": 41, \"packetIndex\": 7, "
        "\"packetizationDecision\": \"mergedExplicit\"}"
    ));
    EXPECT_TRUE(ContainsText(
        rejectedJson,
        "\"queueAssignment\": {\"initialQueue\": {\"index\": 4, \"deviceGeneration\": 17}, "
        "\"plannedQueue\": {\"index\": 5, \"deviceGeneration\": 17}, \"acceptedQueue\": null, "
        "\"previousAcceptedQueue\": null, \"queueClass\": \"transfer\", \"reason\": \"scoredAny\", "
        "\"modifierMask\": 32, \"acceptance\": \"notAccepted\", \"dedicated\": false, "
        "\"score\": {\"preference\": 5, \"overlap\": 6, \"queueLoad\": 1, \"incomingCrossings\": 2, "
        "\"outgoingCrossings\": 3, \"ownershipTransfers\": 4, \"total\": 1}}"
    ));
    EXPECT_TRUE(ContainsText(
        changedJson,
        "\"runtimeStatistics\": {\"graphGeneration\": 51, \"planGeneration\": 52, "
        "\"recordingAttemptGeneration\": 53, \"deviceGeneration\": 17"
    ));
    EXPECT_TRUE(ContainsText(
        changedJson,
        "\"compile\": {\"taskCount\": 78, \"resourceCount\": 2, \"resourceVersionCount\": 3, "
        "\"resourceVersionEdgeCount\": 6, \"resourceUseCount\": 50, "
        "\"explicitDependencyCount\": 4, \"inferredDependencyCount\": 5, \"packetCount\": 76, "
        "\"packetDependencyCount\": 7, \"mergedTaskCount\": 2, \"transitionBarrierCount\": 9, "
        "\"uavBarrierCount\": 10, \"ownershipReleaseBarrierCount\": 11, \"ownershipAcquireBarrierCount\": 12, "
        "\"stateExportBarrierCount\": 13, \"logicalOwnershipTransferCount\": 60, "
        "\"logicalOwnershipTransferSignatureCount\": 15, \"repeatedOwnershipTransferSignatureCount\": 14, "
        "\"concurrentSharingCouldAvoidTransferCount\": 17, \"concurrentSharingAdviceResourceCount\": 2, "
        "\"logicalOwnershipTransferInternalCount\": 19, \"logicalOwnershipTransferExternalImportCount\": 20, "
        "\"logicalOwnershipTransferExternalExportCount\": 21, \"resourceSetCount\": 22, "
        "\"resourceSetMemberCount\": 23, \"directResourceUseCount\": 24, \"declaredResourceSetUseCount\": 25, "
        "\"expandedResourceSetMemberUseCount\": 26, \"payloadObjectCount\": 27, \"payloadObjectBytes\": 28, "
        "\"uploadBlobCount\": 29, \"uploadBlobBytes\": 30, \"declarationSeconds\": 0.001, "
        "\"analysisSeconds\": 0.002, \"validationSeconds\": 0.003, \"dependencyAnalysisSeconds\": 0.004, "
        "\"hazardAnalysisSeconds\": 0.005, \"topologicalOrderSeconds\": 0.006, "
        "\"queueAssignmentSeconds\": 0.007, \"planningSeconds\": 0.008, \"packetizationSeconds\": 0.009, "
        "\"resourceStatePlanningSeconds\": 0.01, \"packetDependencyPlanningSeconds\": 0.011, "
        "\"totalSeconds\": 0.012}"
    ));
    EXPECT_TRUE(ContainsText(
        changedJson,
        "\"recording\": {\"packetCount\": 31, \"taskCount\": 32, \"commandListCount\": 33, "
        "\"barrierCount\": 34, \"workerRoutedPacketCount\": 30, \"parallelPacketCount\": 29, "
        "\"commandListAcquisitionSeconds\": 0.013, \"graphBarrierRecordingSeconds\": 0.014, "
        "\"taskRecordSeconds\": 0.015, \"recordingSeconds\": 0.016, \"recordingElapsedSeconds\": 0.017, "
        "\"readyFrontierElapsedSeconds\": 0.018, \"readyFrontierWorkerBusySeconds\": 0.019, "
        "\"readyFrontierWorkerCapacitySeconds\": 0.02}"
    ));
    EXPECT_TRUE(ContainsText(
        changedJson,
        "\"submission\": {\"acceptedPacketCount\": 37, \"acceptedTaskCount\": 38, "
        "\"rejectedPacketCount\": 39, \"rejectedTaskCount\": 40, \"nativeSubmissionCount\": 30, "
        "\"rejectedSubmissionCount\": 38, \"nativeCommandListCount\": 32, \"plannedWaitTokenCount\": 44, "
        "\"sameQueueWaitElisionCount\": 12, \"timelineWaitCount\": 14, \"mergedTimelineWaitCount\": 18, "
        "\"acceptedFrontierSubmissionCount\": 28, \"recoverySubmissionCount\": 8, \"submissionSeconds\": 0.021}"
    ));
    EXPECT_TRUE(ContainsText(changedJson, "\"physicalQueues\": null"));
    EXPECT_TRUE(ContainsText(
        unassignedJson,
        "\"kind\": \"resource\", \"flags\": 0, \"queueAssignment\": null, \"compiledTask\": null, "
        "\"runtimeStatistics\": null"
    ));
    EXPECT_FALSE(ContainsText(unassignedJson, "\"runtimeStatistics\": {"));
    EXPECT_TRUE(ContainsText(
        rejectedJson,
        "\"runtimeStatistics\": {\"graphGeneration\": 61, \"planGeneration\": 62, "
        "\"recordingAttemptGeneration\": 63, \"deviceGeneration\": 17"
    ));
    EXPECT_TRUE(ContainsText(rejectedJson, "\"physicalQueues\": null"));

    const AStringView dot(report.graph.data(), report.graph.size());
    const usize changedDotOffset = dot.find("  n0 [");
    const usize unassignedDotOffset = dot.find("  n1 [");
    const usize rejectedDotOffset = dot.find("  n2 [");
    const usize dotEdgesOffset = dot.find("  n0 ->", rejectedDotOffset);
    ASSERT_NE(changedDotOffset, AStringView::npos);
    ASSERT_NE(unassignedDotOffset, AStringView::npos);
    ASSERT_NE(rejectedDotOffset, AStringView::npos);
    ASSERT_NE(dotEdgesOffset, AStringView::npos);
    ASSERT_LT(changedDotOffset, unassignedDotOffset);
    ASSERT_LT(unassignedDotOffset, rejectedDotOffset);
    ASSERT_LT(rejectedDotOffset, dotEdgesOffset);

    const AStringView changedDot = dot.substr(changedDotOffset, unassignedDotOffset - changedDotOffset);
    const AStringView unassignedDot = dot.substr(unassignedDotOffset, rejectedDotOffset - unassignedDotOffset);
    const AStringView rejectedDot = dot.substr(rejectedDotOffset, dotEdgesOffset - rejectedDotOffset);
    EXPECT_TRUE(ContainsText(
        changedDot,
        "queue_assignment=\"present\", queue_initial_index=1, queue_initial_device_generation=17, "
        "queue_planned_index=3, queue_planned_device_generation=17, queue_accepted_index=3, "
        "queue_accepted_device_generation=17, queue_previous_accepted_index=2, "
        "queue_previous_accepted_device_generation=17, queue_class=\"compute\", queue_reason=\"fallback\", "
        "queue_modifier_mask=63, queue_acceptance=\"changed\", queue_dedicated=true, queue_score_preference=11, "
        "queue_score_overlap=7, queue_score_queue_load=3, queue_score_incoming_crossings=2, "
        "queue_score_outgoing_crossings=1, queue_score_ownership_transfers=4, queue_score_total=8"
    ));
    EXPECT_TRUE(ContainsText(unassignedDot, "queue_assignment=\"none\""));
    EXPECT_TRUE(ContainsText(
        changedDot,
        "compiled_task=\"present\", compiled_plan_generation=41, compiled_packet_index=7, "
        "packetization_decision=\"firstTask\""
    ));
    EXPECT_TRUE(ContainsText(unassignedDot, "compiled_task=\"none\""));
    EXPECT_TRUE(ContainsText(
        rejectedDot,
        "compiled_task=\"present\", compiled_plan_generation=41, compiled_packet_index=7, "
        "packetization_decision=\"mergedExplicit\""
    ));
    EXPECT_TRUE(ContainsText(
        rejectedDot,
        "queue_assignment=\"present\", queue_initial_index=4, queue_initial_device_generation=17, "
        "queue_planned_index=5, queue_planned_device_generation=17, queue_accepted_index=\"none\", "
        "queue_accepted_device_generation=\"none\", queue_previous_accepted_index=\"none\", "
        "queue_previous_accepted_device_generation=\"none\", queue_class=\"transfer\", queue_reason=\"scoredAny\", "
        "queue_modifier_mask=32, queue_acceptance=\"notAccepted\", queue_dedicated=false, queue_score_preference=5, "
        "queue_score_overlap=6, queue_score_queue_load=1, queue_score_incoming_crossings=2, "
        "queue_score_outgoing_crossings=3, queue_score_ownership_transfers=4, queue_score_total=1"
    ));
    EXPECT_TRUE(ContainsText(
        changedDot,
        "runtime_statistics=\"present\", runtime_graph_generation=51, runtime_plan_generation=52, "
        "runtime_recording_attempt_generation=53, runtime_device_generation=17"
    ));
    EXPECT_TRUE(ContainsText(unassignedDot, "runtime_statistics=\"none\""));
    EXPECT_TRUE(ContainsText(
        rejectedDot,
        "runtime_statistics=\"present\", runtime_graph_generation=61, runtime_plan_generation=62, "
        "runtime_recording_attempt_generation=63, runtime_device_generation=17"
    ));
    EXPECT_TRUE(ContainsText(changedDot, "runtime_physical_queue_count=\"unknown\""));
    EXPECT_TRUE(ContainsText(rejectedDot, "runtime_physical_queue_count=\"unknown\""));
}

TEST(Telemetry, TelemetryReportPreservesExactPhysicalQueueRuntimeStatistics){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords records(testArena.arena);
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, 54u, nodes, edges, records, 14u));

    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));
    const AStringView json(report.json.data(), report.json.size());
    const usize firstPassOffset = json.find("\"label\": \"GBuffer Pass\"");
    const usize resourceOffset = json.find("\"label\": \"Albedo Texture\"");
    const usize secondPassOffset = json.find("\"label\": \"Lighting Pass\"");
    const usize edgesOffset = json.find("\"edges\": [", secondPassOffset);
    ASSERT_NE(firstPassOffset, AStringView::npos);
    ASSERT_NE(resourceOffset, AStringView::npos);
    ASSERT_NE(secondPassOffset, AStringView::npos);
    ASSERT_NE(edgesOffset, AStringView::npos);
    const AStringView firstPass = json.substr(firstPassOffset, resourceOffset - firstPassOffset);
    const AStringView secondPass = json.substr(secondPassOffset, edgesOffset - secondPassOffset);
    const usize graphicsQueueOffset = firstPass.find("\"queue\": {\"index\": 1");
    const usize computeQueueOffset = firstPass.find("\"queue\": {\"index\": 3");
    ASSERT_NE(graphicsQueueOffset, AStringView::npos);
    ASSERT_NE(computeQueueOffset, AStringView::npos);
    EXPECT_LT(graphicsQueueOffset, computeQueueOffset);
    EXPECT_TRUE(ContainsText(
        firstPass,
        "\"compile\": {\"taskCount\": 50, \"packetCount\": 49, \"mergedTaskCount\": 1, "
        "\"prologueBarrierCount\": 11, \"epilogueBarrierCount\": 12, \"ownershipReleaseBarrierCount\": 6, "
        "\"ownershipAcquireBarrierCount\": 7, \"incomingLogicalOwnershipTransferCount\": 30, "
        "\"outgoingLogicalOwnershipTransferCount\": 31, "
        "\"incomingLogicalOwnershipTransferSignatureCount\": 8, "
        "\"outgoingLogicalOwnershipTransferSignatureCount\": 9, "
        "\"incomingRepeatedOwnershipTransferSignatureCount\": 7, "
        "\"outgoingRepeatedOwnershipTransferSignatureCount\": 8, "
        "\"concurrentSharingAdviceResourceCount\": 1}"
    ));
    EXPECT_TRUE(ContainsText(
        firstPass,
        "\"recording\": {\"packetCount\": 20, \"taskCount\": 21, \"commandListCount\": 21, "
        "\"barrierCount\": 23, \"workerRoutedPacketCount\": 19, \"parallelPacketCount\": 18, "
        "\"commandListAcquisitionSeconds\": 0.005, \"graphBarrierRecordingSeconds\": 0.006, "
        "\"taskRecordSeconds\": 0.007, \"recordingSeconds\": 0.008}"
    ));
    EXPECT_TRUE(ContainsText(
        firstPass,
        "\"submission\": {\"acceptedPacketCount\": 25, \"acceptedTaskCount\": 26, "
        "\"rejectedPacketCount\": 24, \"rejectedTaskCount\": 24, \"nativeSubmissionCount\": 19, "
        "\"rejectedSubmissionCount\": 23, \"nativeCommandListCount\": 20, \"plannedWaitTokenCount\": 20, "
        "\"sameQueueWaitElisionCount\": 5, \"timelineWaitCount\": 8, \"mergedTimelineWaitCount\": 7, "
        "\"acceptedFrontierSubmissionCount\": 18, \"recoverySubmissionCount\": 5, \"submissionSeconds\": 0.011}"
    ));
    EXPECT_TRUE(ContainsText(firstPass, "\"recoverySubmissionCount\": 3"));
    EXPECT_TRUE(ContainsText(secondPass, "\"physicalQueues\": null"));

    const AStringView dot(report.graph.data(), report.graph.size());
    const usize firstPassDotOffset = dot.find("  n0 [");
    const usize secondPassDotOffset = dot.find("  n2 [");
    const usize dotEdgesOffset = dot.find("  n0 ->", secondPassDotOffset);
    ASSERT_NE(firstPassDotOffset, AStringView::npos);
    ASSERT_NE(secondPassDotOffset, AStringView::npos);
    ASSERT_NE(dotEdgesOffset, AStringView::npos);
    const AStringView firstPassDot = dot.substr(firstPassDotOffset, secondPassDotOffset - firstPassDotOffset);
    const AStringView secondPassDot = dot.substr(secondPassDotOffset, dotEdgesOffset - secondPassDotOffset);
    EXPECT_TRUE(ContainsText(firstPassDot, "runtime_physical_queue_count=2"));
    EXPECT_TRUE(ContainsText(secondPassDot, "runtime_physical_queue_count=\"unknown\""));
}

TEST(Telemetry, TelemetryReportPreservesExactPacketSubmissionStatistics){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords physicalQueueRuntimeStatistics(testArena.arena);
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords packetSubmissionStatistics(testArena.arena);
    BuildTestPacketSubmissionFrameGraph(
        testArena.arena,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics
    );
    ASSERT_TRUE(Telemetry::RecordFrameGraph(
        recorder,
        57u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        17u
    ));

    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));
    const AStringView json(report.json.data(), report.json.size());
    const usize packetZeroOffset = json.find("\"packet\": {\"index\": 0, \"generation\": 72}");
    const usize packetOneOffset = json.find("\"packet\": {\"index\": 1, \"generation\": 72}");
    const usize packetTwoOffset = json.find("\"packet\": {\"index\": 2, \"generation\": 72}");
    ASSERT_NE(packetZeroOffset, AStringView::npos);
    ASSERT_NE(packetOneOffset, AStringView::npos);
    ASSERT_NE(packetTwoOffset, AStringView::npos);
    EXPECT_LT(packetZeroOffset, packetOneOffset);
    EXPECT_LT(packetOneOffset, packetTwoOffset);
    EXPECT_TRUE(ContainsText(
        json,
        "\"packetSubmissions\": [{\"packet\": {\"index\": 0, \"generation\": 72}, "
        "\"queue\": {\"index\": 1, \"deviceGeneration\": 17}, \"queueClass\": \"graphics\", "
        "\"taskCount\": 2, \"commandListCount\": 1, \"plannedWaitTokenCount\": 2, "
        "\"sameQueueWaitElisionCount\": 1, \"timelineWaitCount\": 1, \"mergedTimelineWaitCount\": 0, "
        "\"joinsAcceptedQueueFrontier\": false, \"recoverySubmission\": false, "
        "\"submissionSeconds\": 0.125}"
    ));
    EXPECT_TRUE(ContainsText(
        json,
        "\"packet\": {\"index\": 1, \"generation\": 72}, "
        "\"queue\": {\"index\": 3, \"deviceGeneration\": 17}, \"queueClass\": \"compute\", "
        "\"taskCount\": 1, \"commandListCount\": 2, \"plannedWaitTokenCount\": 3, "
        "\"sameQueueWaitElisionCount\": 0, \"timelineWaitCount\": 1, \"mergedTimelineWaitCount\": 2, "
        "\"joinsAcceptedQueueFrontier\": true, \"recoverySubmission\": false, "
        "\"submissionSeconds\": 0.25}"
    ));
    EXPECT_TRUE(ContainsText(
        json,
        "\"packet\": {\"index\": 2, \"generation\": 72}, "
        "\"queue\": {\"index\": 1, \"deviceGeneration\": 17}, \"queueClass\": \"graphics\", "
        "\"taskCount\": 2, \"commandListCount\": 1, \"plannedWaitTokenCount\": 1, "
        "\"sameQueueWaitElisionCount\": 1, \"timelineWaitCount\": 0, \"mergedTimelineWaitCount\": 0, "
        "\"joinsAcceptedQueueFrontier\": true, \"recoverySubmission\": true"
    ));

    const AStringView dot(report.graph.data(), report.graph.size());
    EXPECT_TRUE(ContainsText(dot, "runtime_packet_submission_count=3"));
}

TEST(Telemetry, TelemetryReportDistinguishesExactEmptyPacketSubmissionsFromLegacyUnknown){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords physicalQueueRuntimeStatistics(testArena.arena);
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords packetSubmissionStatistics(testArena.arena);
    BuildTestPacketSubmissionFrameGraph(
        testArena.arena,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics
    );
    nodes[0u].runtimeStatistics.submission = {};
    physicalQueueRuntimeStatistics.clear();
    packetSubmissionStatistics.clear();

    Telemetry::Recorder exactRecorder(testArena.arena);
    exactRecorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    ASSERT_TRUE(Telemetry::RecordFrameGraph(
        exactRecorder,
        58u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        18u
    ));
    Log::TelemetryReport exactReport(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, exactRecorder.view(), exactReport));
    const AStringView exactJson(exactReport.json.data(), exactReport.json.size());
    const AStringView exactDot(exactReport.graph.data(), exactReport.graph.size());
    EXPECT_TRUE(ContainsText(exactJson, "\"packetSubmissions\": []"));
    EXPECT_TRUE(ContainsText(exactDot, "runtime_packet_submission_count=0"));

    Telemetry::Recorder legacyRecorder(testArena.arena);
    legacyRecorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    ASSERT_TRUE(Telemetry::RecordFrameGraph(legacyRecorder, 59u, nodes, edges, 19u));
    Log::TelemetryReport legacyReport(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, legacyRecorder.view(), legacyReport));
    const AStringView legacyJson(legacyReport.json.data(), legacyReport.json.size());
    const AStringView legacyDot(legacyReport.graph.data(), legacyReport.graph.size());
    EXPECT_TRUE(ContainsText(legacyJson, "\"packetSubmissions\": null"));
    EXPECT_TRUE(ContainsText(legacyDot, "runtime_packet_submission_count=\"unknown\""));
}

TEST(Telemetry, TelemetryReportMarksV4RecoverySubmissionCountUnknown){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes currentPayload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 55u, nodes, edges, currentPayload));
    Telemetry::TelemetryBytes legacyPayload(testArena.arena);
    ASSERT_TRUE(ConvertFrameGraphPayloadV8ToLegacy(
        currentPayload,
        Telemetry::s_FrameGraphRuntimeStatisticsPayloadVersion,
        legacyPayload
    ));

    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    ASSERT_TRUE(recorder.recordBinary(
        Telemetry::EventKind::FrameGraphFrame,
        55u,
        legacyPayload.data(),
        legacyPayload.size(),
        15u
    ));

    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));
    const AStringView json(report.json.data(), report.json.size());
    EXPECT_TRUE(ContainsText(
        json,
        "\"resourceVersionCount\": null, \"resourceVersionEdgeCount\": null"
    ));
    EXPECT_TRUE(ContainsText(
        json,
        "\"acceptedFrontierSubmissionCount\": 28, \"recoverySubmissionCount\": null, "
        "\"submissionSeconds\": 0.021}"
    ));
    EXPECT_TRUE(ContainsText(json, "\"physicalQueues\": null"));
    EXPECT_FALSE(ContainsText(json, "\"recoverySubmissionCount\": 0"));

    const AStringView dot(report.graph.data(), report.graph.size());
    EXPECT_TRUE(ContainsText(dot, "runtime_physical_queue_count=\"unknown\""));
}

TEST(Telemetry, TelemetryReportMarksV5RecoverySubmissionCountsUnknown){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords records(testArena.arena);
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);

    Telemetry::TelemetryBytes currentPayload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        56u,
        nodes,
        edges,
        records,
        currentPayload
    ));
    Telemetry::TelemetryBytes legacyPayload(testArena.arena);
    ASSERT_TRUE(ConvertFrameGraphPayloadV8ToLegacy(
        currentPayload,
        Telemetry::s_FrameGraphPhysicalQueueRuntimeStatisticsPayloadVersion,
        legacyPayload
    ));

    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    ASSERT_TRUE(recorder.recordBinary(
        Telemetry::EventKind::FrameGraphFrame,
        56u,
        legacyPayload.data(),
        legacyPayload.size(),
        16u
    ));

    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));
    const AStringView json(report.json.data(), report.json.size());
    const usize firstPassOffset = json.find("\"label\": \"GBuffer Pass\"");
    const usize resourceOffset = json.find("\"label\": \"Albedo Texture\"");
    ASSERT_NE(firstPassOffset, AStringView::npos);
    ASSERT_NE(resourceOffset, AStringView::npos);
    const AStringView firstPass = json.substr(firstPassOffset, resourceOffset - firstPassOffset);
    EXPECT_TRUE(ContainsText(
        firstPass,
        "\"acceptedFrontierSubmissionCount\": 28, \"recoverySubmissionCount\": null"
    ));
    EXPECT_TRUE(ContainsText(
        firstPass,
        "\"acceptedFrontierSubmissionCount\": 18, \"recoverySubmissionCount\": null"
    ));
    EXPECT_TRUE(ContainsText(
        firstPass,
        "\"acceptedFrontierSubmissionCount\": 10, \"recoverySubmissionCount\": null"
    ));
    EXPECT_TRUE(ContainsText(firstPass, "\"physicalQueues\": [{"));
    EXPECT_FALSE(ContainsText(json, "\"recoverySubmissionCount\": 0"));
}

TEST(Telemetry, TelemetryReportMarksLegacyRuntimeStatisticsAbsent){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestCompiledFrameGraph(testArena.arena, nodes, edges);
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, 53u, nodes, edges, 13u));

    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));

    const AStringView json(report.json.data(), report.json.size());
    const usize firstPassJsonOffset = json.find("\"label\": \"GBuffer Pass\"");
    const usize resourceJsonOffset = json.find("\"label\": \"Albedo Texture\"");
    const usize secondPassJsonOffset = json.find("\"label\": \"Lighting Pass\"");
    const usize jsonEdgesOffset = json.find("\"edges\": [", secondPassJsonOffset);
    ASSERT_NE(firstPassJsonOffset, AStringView::npos);
    ASSERT_NE(resourceJsonOffset, AStringView::npos);
    ASSERT_NE(secondPassJsonOffset, AStringView::npos);
    ASSERT_NE(jsonEdgesOffset, AStringView::npos);
    ASSERT_LT(firstPassJsonOffset, resourceJsonOffset);
    ASSERT_LT(resourceJsonOffset, secondPassJsonOffset);
    ASSERT_LT(secondPassJsonOffset, jsonEdgesOffset);

    const AStringView firstPassJson = json.substr(firstPassJsonOffset, resourceJsonOffset - firstPassJsonOffset);
    const AStringView resourceJson = json.substr(resourceJsonOffset, secondPassJsonOffset - resourceJsonOffset);
    const AStringView secondPassJson = json.substr(secondPassJsonOffset, jsonEdgesOffset - secondPassJsonOffset);
    EXPECT_TRUE(ContainsText(firstPassJson, "\"kind\": \"pass\""));
    EXPECT_TRUE(ContainsText(firstPassJson, "\"runtimeStatistics\": null"));
    EXPECT_TRUE(ContainsText(
        resourceJson,
        "\"kind\": \"resource\", \"flags\": 0, \"queueAssignment\": null, \"compiledTask\": null, "
        "\"runtimeStatistics\": null"
    ));
    EXPECT_TRUE(ContainsText(secondPassJson, "\"kind\": \"pass\""));
    EXPECT_TRUE(ContainsText(secondPassJson, "\"runtimeStatistics\": null"));

    const AStringView dot(report.graph.data(), report.graph.size());
    const usize firstPassDotOffset = dot.find("  n0 [");
    const usize resourceDotOffset = dot.find("  n1 [");
    const usize secondPassDotOffset = dot.find("  n2 [");
    const usize dotEdgesOffset = dot.find("  n0 ->", secondPassDotOffset);
    ASSERT_NE(firstPassDotOffset, AStringView::npos);
    ASSERT_NE(resourceDotOffset, AStringView::npos);
    ASSERT_NE(secondPassDotOffset, AStringView::npos);
    ASSERT_NE(dotEdgesOffset, AStringView::npos);
    ASSERT_LT(firstPassDotOffset, resourceDotOffset);
    ASSERT_LT(resourceDotOffset, secondPassDotOffset);
    ASSERT_LT(secondPassDotOffset, dotEdgesOffset);

    const AStringView firstPassDot = dot.substr(firstPassDotOffset, resourceDotOffset - firstPassDotOffset);
    const AStringView resourceDot = dot.substr(resourceDotOffset, secondPassDotOffset - resourceDotOffset);
    const AStringView secondPassDot = dot.substr(secondPassDotOffset, dotEdgesOffset - secondPassDotOffset);
    EXPECT_TRUE(ContainsText(firstPassDot, "runtime_statistics=\"none\""));
    EXPECT_TRUE(ContainsText(resourceDot, "runtime_statistics=\"none\""));
    EXPECT_TRUE(ContainsText(secondPassDot, "runtime_statistics=\"none\""));
}

TEST(Telemetry, TelemetryIngestStoresRawAndReports){
    TestArena testArena;
    const ::Path<NWB::Core::Alloc::GlobalArena> storageDirectory = TelemetryTestStorageDirectory(testArena.arena) / "ingest";

    ErrorCode error;
    EXPECT_TRUE(RemoveAllIfExists(storageDirectory, error));

    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    EXPECT_TRUE(Telemetry::RecordTextLog(
        recorder,
        NWB::Core::Common::LogType::Info,
        NWB_TEXT("ingest log"),
        10u,
        1u
    ));

    const Name cpuScopeName("ingest/cpu");
    const NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();
    EXPECT_TRUE(Telemetry::RecordPerfTiming(recorder, Telemetry::PerfTimingSource::Cpu, cpuScopeName, "ingest/cpu", stats, 2u));

    Telemetry::TelemetryBytes encoded(testArena.arena);
    EXPECT_TRUE(Telemetry::EncodeEventStream(recorder.view(), encoded));

    Log::TelemetryIngestConfig config(testArena.arena);
    config.storageDirectory = storageDirectory;
    const Log::TelemetryIngestResult result = Log::ProcessTelemetryUpload(testArena.arena, encoded.data(), encoded.size(), config);

    EXPECT_TRUE(result.ok());
    EXPECT_TRUE(result.decode.ok());
    EXPECT_EQ(result.decode.bytesRead, encoded.size());
    EXPECT_EQ(result.summary.eventCount, 2u);
    EXPECT_EQ(result.summary.cpuTimingEventCount, 1u);
    EXPECT_TRUE(FileExists(result.rawPath, error));
    EXPECT_FALSE(error);
    error.clear();
    EXPECT_TRUE(FileExists(result.jsonPath, error));
    EXPECT_FALSE(error);
    error.clear();
    EXPECT_TRUE(FileExists(result.perfCsvPath, error));
    EXPECT_FALSE(error);

    AString<NWB::Core::Alloc::GlobalArena> perfCsv(testArena.arena);
    EXPECT_TRUE(ReadTextFile(result.perfCsvPath, perfCsv));
    EXPECT_TRUE(ContainsText(AStringView(perfCsv.data(), perfCsv.size()), "cpu,ingest/cpu"));

    EXPECT_TRUE(RemoveAllIfExists(storageDirectory, error));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

