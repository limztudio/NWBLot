// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"
#include "frame_graph_test_helpers.h"

#include <tests/unit/telemetry/perf_case_types.adhd.h>

#include <core/alloc/scratch.h>

#include <global/floating_point_environment.h>
#include <global/scope_exit.h>
#include <global/text_utils.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_report_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_ReportArenaName = "tests/telemetry/report_work";

static constexpr AStringView s_GBuffer = "gbuffer";
static constexpr AStringView s_RuntimeStatisticsNone = "runtime_statistics=\"none\"";
static constexpr AStringView s_RuntimeStatisticsNullJson = "\"runtimeStatistics\": null";
static constexpr AStringView s_ResourceWithNullStatisticsJson = "\"kind\": \"resource\", \"flags\": 0, \"queueAssignment\": null, \"compiledTask\": null, \"runtimeStatistics\": null";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;


TEST(Telemetry, TelemetryReportKeepsCaptureOrderAndCorrelatesTimingByFrame){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    const Name gbufferScopeName{s_GBuffer};
    NWB::Core::Perf::TimingStats firstTiming = MakeTestTimingStats();
    firstTiming.seconds = 0.041;
    firstTiming.sampleCount = 1u;
    firstTiming.publishFrameIndex = 41u;
    firstTiming.firstSampleFrameIndex = 40u;
    firstTiming.lastSampleFrameIndex = 40u;
    ASSERT_TRUE(Telemetry::RecordPerfTiming(recorder, Telemetry::PerfTimingSource::Gpu, gbufferScopeName, "gbuffer", firstTiming, 70u));

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 40u, nodes, edges, 7u));

    NWB::Core::Perf::TimingStats secondTiming = MakeTestTimingStats();
    secondTiming.seconds = 0.042;
    secondTiming.sampleCount = 1u;
    secondTiming.publishFrameIndex = 42u;
    secondTiming.firstSampleFrameIndex = 41u;
    secondTiming.lastSampleFrameIndex = 41u;
    ASSERT_TRUE(Telemetry::RecordPerfTiming(recorder, Telemetry::PerfTimingSource::Gpu, gbufferScopeName, "gbuffer", secondTiming, 80u));

    nodes[0u].label = "Second GBuffer Pass";
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 41u, nodes, edges, 8u));

    nodes[0u].label = "Unmatched GBuffer Pass";
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 42u, nodes, edges, 9u));

    Log::TelemetryReport report(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena{ Name(s_ReportArenaName) };
    auto reportResult1 = Log::BuildTelemetryReport(recorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult1);
    report = Move(*reportResult1);
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
    EXPECT_TRUE(Contains(firstDotRecord, "GBuffer Pass\\n41.000 ms"));
    EXPECT_TRUE(Contains(secondDotRecord, "Second GBuffer Pass\\n42.000 ms"));
    EXPECT_TRUE(Contains(thirdDotRecord, "Unmatched GBuffer Pass"));
    EXPECT_FALSE(Contains(thirdDotRecord, " ms"));
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
    ASSERT_TRUE(Telemetry::RecordPerfTiming(recorder, Telemetry::PerfTimingSource::Gpu, Name(s_GBuffer), "gbuffer", aggregatedTiming, 70u));

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 43u, nodes, edges, 7u));
    nodes[0u].label = "Publish Frame GBuffer Pass";
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 44u, nodes, edges, 8u));

    Log::TelemetryReport report(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena{ Name(s_ReportArenaName) };
    auto reportResult2 = Log::BuildTelemetryReport(recorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult2);
    report = Move(*reportResult2);
    const AStringView dot(report.graph.data(), report.graph.size());
    EXPECT_TRUE(Contains(dot, "digraph frame_graph_43_7_0"));
    EXPECT_TRUE(Contains(dot, "digraph frame_graph_44_8_1"));
    EXPECT_FALSE(Contains(dot, " ms"));
    EXPECT_TRUE(Contains(AStringView(report.perfCsv.data(), report.perfCsv.size()), "gpu,gbuffer"));
}

TEST(Telemetry, TelemetryReportDistinguishesExactEmptyPacketSubmissionsFromAbsentStatistics){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords physicalQueueRuntimeStatistics(testArena.arena);
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords packetSubmissionStatistics(testArena.arena);
    BuildTestPacketSubmissionFrameGraph(testArena.arena, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics);
    nodes[0u].runtimeStatistics.submission = {};
    physicalQueueRuntimeStatistics.clear();
    packetSubmissionStatistics.clear();

    Telemetry::Recorder exactRecorder(testArena.arena);
    exactRecorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    ASSERT_TRUE(Telemetry::RecordFrameGraph(exactRecorder, testArena.scratchArena, 58u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, 18u));
    Log::TelemetryReport exactReport(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena{ Name(s_ReportArenaName) };
    auto reportResult3 = Log::BuildTelemetryReport(exactRecorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult3);
    exactReport = Move(*reportResult3);
    const AStringView exactJson(exactReport.json.data(), exactReport.json.size());
    const AStringView exactDot(exactReport.graph.data(), exactReport.graph.size());
    EXPECT_TRUE(Contains(exactJson, "\"packetSubmissions\": []"));
    EXPECT_TRUE(Contains(exactDot, "runtime_packet_submission_count=0"));

    Telemetry::Recorder absentRecorder(testArena.arena);
    absentRecorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    ASSERT_TRUE(Telemetry::RecordFrameGraph(absentRecorder, testArena.scratchArena, 59u, nodes, edges, 19u));
    Log::TelemetryReport absentReport(testArena.arena);
    auto reportResult4 = Log::BuildTelemetryReport(absentRecorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult4);
    absentReport = Move(*reportResult4);
    const AStringView absentJson(absentReport.json.data(), absentReport.json.size());
    const AStringView absentDot(absentReport.graph.data(), absentReport.graph.size());
    EXPECT_TRUE(Contains(absentJson, "\"packetSubmissions\": null"));
    EXPECT_TRUE(Contains(absentDot, "runtime_packet_submission_count=\"unknown\""));
}

TEST(Telemetry, TelemetryReportRejectsNonCurrentFrameGraphPayloads){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);
    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(55u, nodes, edges, testArena.scratchArena, payload));

    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 55u, nodes, edges, 15u));

    const Array<u16, 12> unsupportedVersions = { 0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, static_cast<u16>(Telemetry::s_FrameGraphPayloadVersion + 1u), Limit<u16>::s_Max, };
    Telemetry::EncodedFrameGraphPayloadHeader header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    for(const u16 version : unsupportedVersions){
        SCOPED_TRACE(version);
        header.version = version;
        NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
        ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::FrameGraphFrame, 55u, payload.data(), payload.size(), 15u));
    }

    Log::TelemetryReport report(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena{ Name(s_ReportArenaName) };
    auto reportResult5 = Log::BuildTelemetryReport(recorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult5);
    report = Move(*reportResult5);
    EXPECT_EQ(report.summary.eventCount, LengthOf(unsupportedVersions) + 1u);
    EXPECT_EQ(report.summary.parseFailureCount, LengthOf(unsupportedVersions));
    EXPECT_EQ(report.summary.frameGraphFrameCount, 1u);
    EXPECT_TRUE(Contains(AStringView(report.graph.data(), report.graph.size()), "GBuffer Pass"));
}

TEST(Telemetry, TelemetryReportMarksAbsentRuntimeStatistics){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestCompiledFrameGraph(testArena.arena, nodes, edges);
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 53u, nodes, edges, 13u));

    Log::TelemetryReport report(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena{ Name(s_ReportArenaName) };
    auto reportResult6 = Log::BuildTelemetryReport(recorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult6);
    report = Move(*reportResult6);

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
    EXPECT_TRUE(Contains(firstPassJson, s_RuntimeStatisticsNullJson));
    EXPECT_TRUE(Contains(resourceJson, s_ResourceWithNullStatisticsJson));
    EXPECT_TRUE(Contains(secondPassJson, s_RuntimeStatisticsNullJson));

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
    EXPECT_TRUE(Contains(firstPassDot, s_RuntimeStatisticsNone));
    EXPECT_TRUE(Contains(resourceDot, s_RuntimeStatisticsNone));
    EXPECT_TRUE(Contains(secondPassDot, s_RuntimeStatisticsNone));
}

TEST(Telemetry, TelemetryReportRetainsFiniteRecordsWhenTimingTotalsOverflow){
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    Tests::FloatingPointEnvironmentGuard restoreEnvironment(*originalEnvironment);
    ASSERT_TRUE(SetFloatingPointRoundingMode(FE_TONEAREST));

    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    NWB::Core::Perf::TimingStats timing = MakeTestTimingStats();
    timing.seconds = Limit<f64>::s_Max * 0.75;
    timing.minSeconds = timing.seconds;
    timing.maxSeconds = timing.seconds;
    timing.lastSeconds = timing.seconds;
    timing.sampleCount = 1u;
    timing.firstSampleFrameIndex = 70u;
    timing.lastSampleFrameIndex = 70u;
    for(const auto source : { Telemetry::PerfTimingSource::Cpu, Telemetry::PerfTimingSource::Gpu }){
        ASSERT_TRUE(Telemetry::RecordPerfTiming(recorder, source, Name(NWB::Tests::TelemetryCases::s_TelemetryCaseLargeFirst), "large_first", timing));
        ASSERT_TRUE(Telemetry::RecordPerfTiming(recorder, source, Name(NWB::Tests::TelemetryCases::s_TelemetryCaseLargeSecond), "large_second", timing));
    }

    const auto report = Log::BuildTelemetryReport(recorder.view(), testArena.arena, testArena.scratchArena);
    ASSERT_TRUE(report);
    EXPECT_EQ(report->summary.eventCount, 4u);
    EXPECT_EQ(report->summary.parseFailureCount, 0u);
    EXPECT_EQ(report->summary.cpuTimingEventCount, 2u);
    EXPECT_EQ(report->summary.gpuTimingEventCount, 2u);

    const AStringView json(report->json.data(), report->json.size());
    EXPECT_TRUE(Contains(json, "\"cpuTimingSeconds\": null"));
    EXPECT_TRUE(Contains(json, "\"gpuTimingSeconds\": null"));
    const AStringView csv(report->perfCsv.data(), report->perfCsv.size());
    EXPECT_TRUE(Contains(csv, "cpu,large_first,"));
    EXPECT_TRUE(Contains(csv, "cpu,large_second,"));
    EXPECT_TRUE(Contains(csv, "gpu,large_first,"));
    EXPECT_TRUE(Contains(csv, "gpu,large_second,"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

