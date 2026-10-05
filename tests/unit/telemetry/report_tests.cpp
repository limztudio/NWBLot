// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"
#include <gtest/gtest.h>
#include "frame_graph_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_report_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_GBUFFER = "gbuffer";
static constexpr AStringView s_RUNTIME_STATISTICS_NONE = "runtime_statistics=\"none\"";
static constexpr AStringView s_RUNTIME_STATISTICS_NULL_JSON = "\"runtimeStatistics\": null";
static constexpr AStringView s_RESOURCE_WITH_NULL_STATISTICS_JSON = "\"kind\": \"resource\", \"flags\": 0, \"queueAssignment\": null, \"compiledTask\": null, \"runtimeStatistics\": null";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


using namespace TelemetryTestDetail;


TEST(Telemetry, TelemetryReportKeepsCaptureOrderAndCorrelatesTimingByFrame){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::all());

    const Name gbufferScopeName{s_GBUFFER};
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
    EXPECT_TRUE(ContainsText(firstDotRecord, "GBuffer Pass\\n41.000 ms"));
    EXPECT_TRUE(ContainsText(secondDotRecord, "Second GBuffer Pass\\n42.000 ms"));
    EXPECT_TRUE(ContainsText(thirdDotRecord, "Unmatched GBuffer Pass"));
    EXPECT_FALSE(ContainsText(thirdDotRecord, " ms"));
}

TEST(Telemetry, TelemetryReportDoesNotAttachAggregatedTimingToOneGraph){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::all());

    NWB::Core::Perf::TimingStats aggregatedTiming = MakeTestTimingStats();
    aggregatedTiming.seconds = 0.043;
    aggregatedTiming.sampleCount = s_ExpectedDualCount;
    aggregatedTiming.publishFrameIndex = 44u;
    aggregatedTiming.firstSampleFrameIndex = 43u;
    aggregatedTiming.lastSampleFrameIndex = 43u;
    ASSERT_TRUE(Telemetry::RecordPerfTiming(
        recorder,
        Telemetry::PerfTimingSource::Gpu,
        Name(s_GBUFFER),
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

TEST(Telemetry, TelemetryReportDistinguishesExactEmptyPacketSubmissionsFromAbsentStatistics){
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
    exactRecorder.setCaptureOptions(Telemetry::CaptureOptions::all());
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

    Telemetry::Recorder absentRecorder(testArena.arena);
    absentRecorder.setCaptureOptions(Telemetry::CaptureOptions::all());
    ASSERT_TRUE(Telemetry::RecordFrameGraph(absentRecorder, 59u, nodes, edges, 19u));
    Log::TelemetryReport absentReport(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, absentRecorder.view(), absentReport));
    const AStringView absentJson(absentReport.json.data(), absentReport.json.size());
    const AStringView absentDot(absentReport.graph.data(), absentReport.graph.size());
    EXPECT_TRUE(ContainsText(absentJson, "\"packetSubmissions\": null"));
    EXPECT_TRUE(ContainsText(absentDot, "runtime_packet_submission_count=\"unknown\""));
}

TEST(Telemetry, TelemetryReportRejectsNonCurrentFrameGraphPayloads){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);
    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 55u, nodes, edges, payload));

    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::all());
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, 55u, nodes, edges, 15u));

    const u16 unsupportedVersions[] = {
        0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u,
        static_cast<u16>(Telemetry::s_FrameGraphPayloadVersion + 1u),
        Limit<u16>::s_Max,
    };
    Telemetry::EncodedFrameGraphPayloadHeader header;
    GLB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    for(const u16 version : unsupportedVersions){
        SCOPED_TRACE(version);
        header.version = version;
        GLB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
        ASSERT_TRUE(recorder.recordBinary(
            Telemetry::EventKind::FrameGraphFrame,
            55u,
            payload.data(),
            payload.size(),
            15u
        ));
    }

    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));
    EXPECT_EQ(report.summary.eventCount, LengthOf(unsupportedVersions) + 1u);
    EXPECT_EQ(report.summary.parseFailureCount, LengthOf(unsupportedVersions));
    EXPECT_EQ(report.summary.frameGraphFrameCount, 1u);
    EXPECT_EQ(report.summary.frameGraphNodeCount, nodes.size());
    EXPECT_EQ(report.summary.frameGraphEdgeCount, edges.size());
    EXPECT_TRUE(ContainsText(AStringView(report.graph.data(), report.graph.size()), "GBuffer Pass"));
}

TEST(Telemetry, TelemetryReportMarksAbsentRuntimeStatistics){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::all());

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
    EXPECT_TRUE(ContainsText(firstPassJson, s_RUNTIME_STATISTICS_NULL_JSON));
    EXPECT_TRUE(ContainsText(
        resourceJson,
        s_RESOURCE_WITH_NULL_STATISTICS_JSON
    ));
    EXPECT_TRUE(ContainsText(secondPassJson, s_RUNTIME_STATISTICS_NULL_JSON));

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
    EXPECT_TRUE(ContainsText(firstPassDot, s_RUNTIME_STATISTICS_NONE));
    EXPECT_TRUE(ContainsText(resourceDot, s_RUNTIME_STATISTICS_NONE));
    EXPECT_TRUE(ContainsText(secondPassDot, s_RUNTIME_STATISTICS_NONE));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

