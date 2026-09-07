// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_perf_tests{


using namespace TelemetryTestDetail;



TEST(Telemetry, PerfTimingPayloadRoundTrip){
    TestArena testArena;
    const Name scopeName("renderer/frame");
    const NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();

    Telemetry::TelemetryBytes payload(testArena.arena);
    EXPECT_TRUE(Telemetry::BuildPerfTimingPayload(
        testArena.arena,
        Telemetry::PerfTimingSource::Gpu,
        scopeName,
        "Renderer Frame",
        stats,
        payload
    ));
    EXPECT_EQ(payload.size(), sizeof(Telemetry::EncodedPerfTimingPayloadHeader) + sizeof("Renderer Frame") - 1u);

    Telemetry::PerfTimingPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParsePerfTimingPayload(testArena.arena, payload.data(), payload.size(), parsed));
    EXPECT_EQ(parsed.source, Telemetry::PerfTimingSource::Gpu);
    EXPECT_EQ(parsed.scopeName, scopeName);
    EXPECT_EQ(parsed.scopeText, "Renderer Frame");
    EXPECT_EQ(parsed.stats.seconds, stats.seconds);
    EXPECT_EQ(parsed.stats.sampleCount, stats.sampleCount);
    EXPECT_EQ(parsed.stats.publishFrameIndex, stats.publishFrameIndex);
    EXPECT_EQ(parsed.stats.firstSampleFrameIndex, stats.firstSampleFrameIndex);
    EXPECT_EQ(parsed.stats.lastSampleFrameIndex, stats.lastSampleFrameIndex);

    payload[0u] = 0u;
    EXPECT_FALSE(Telemetry::ParsePerfTimingPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

TEST(Telemetry, PerfTimingPayloadRejectsInvalidInput){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();

    EXPECT_FALSE(Telemetry::BuildPerfTimingPayload(
        testArena.arena,
        Telemetry::PerfTimingSource::Unknown,
        Name("renderer/frame"),
        "Renderer Frame",
        stats,
        payload
    ));

    stats.sampleCount = 0u;
    EXPECT_FALSE(Telemetry::BuildPerfTimingPayload(
        testArena.arena,
        Telemetry::PerfTimingSource::Cpu,
        Name("renderer/frame"),
        "Renderer Frame",
        stats,
        payload
    ));
}

TEST(Telemetry, RecordPerfTimingUsesTelemetryEvent){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    const Name scopeName("cpu/update");
    const NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();
    EXPECT_TRUE(Telemetry::RecordPerfTiming(recorder, Telemetry::PerfTimingSource::Cpu, scopeName, "cpu/update", stats, 11u));

    const Telemetry::EventRecord* event = recorder.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    EXPECT_EQ(event->header.kind, Telemetry::EventKind::PerfFrame);
    EXPECT_EQ(event->header.frameIndex, stats.publishFrameIndex);
    EXPECT_EQ(event->header.streamId, 11u);

    Telemetry::PerfTimingPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParsePerfTimingPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    EXPECT_EQ(parsed.source, Telemetry::PerfTimingSource::Cpu);
    EXPECT_EQ(parsed.scopeName, scopeName);
    EXPECT_EQ(parsed.scopeText, "cpu/update");
    EXPECT_EQ(parsed.stats.sampleCount, stats.sampleCount);
}

TEST(Telemetry, PerfMemoryPayloadRoundTrip){
    TestArena testArena;
    const Name scopeName("memory/project_arena");
    const NWB::Core::Perf::MemorySnapshot snapshot = MakeTestMemorySnapshot(scopeName);
    const NWB::Core::Perf::MemoryDelta delta = MakeTestMemoryDelta();

    Telemetry::TelemetryBytes payload(testArena.arena);
    EXPECT_TRUE(Telemetry::BuildPerfMemoryPayload(
        testArena.arena,
        scopeName,
        "Project Arena",
        snapshot,
        delta,
        payload
    ));
    EXPECT_EQ(payload.size(), sizeof(Telemetry::EncodedPerfMemoryPayloadHeader) + sizeof("Project Arena") - 1u);

    Telemetry::PerfMemoryPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParsePerfMemoryPayload(testArena.arena, payload.data(), payload.size(), parsed));
    EXPECT_EQ(parsed.scopeName, scopeName);
    EXPECT_EQ(parsed.scopeText, "Project Arena");
    EXPECT_EQ(parsed.snapshot.scopeName, scopeName);
    EXPECT_EQ(parsed.snapshot.frameIndex, snapshot.frameIndex);
    EXPECT_EQ(parsed.snapshot.reservedBytes, snapshot.reservedBytes);
    EXPECT_EQ(parsed.snapshot.usedBytes, snapshot.usedBytes);
    EXPECT_EQ(parsed.snapshot.peakUsedBytes, snapshot.peakUsedBytes);
    EXPECT_EQ(parsed.snapshot.allocationCount, snapshot.allocationCount);
    EXPECT_EQ(parsed.snapshot.reallocationCount, snapshot.reallocationCount);
    EXPECT_EQ(parsed.snapshot.deallocationCount, snapshot.deallocationCount);
    EXPECT_TRUE(parsed.delta.hasSamples);
    EXPECT_EQ(parsed.delta.previousFrameIndex, delta.previousFrameIndex);
    EXPECT_EQ(parsed.delta.currentFrameIndex, snapshot.frameIndex);
    EXPECT_EQ(parsed.delta.reservedBytes, delta.reservedBytes);
    EXPECT_EQ(parsed.delta.usedBytes, delta.usedBytes);
    EXPECT_EQ(parsed.delta.peakUsedBytes, delta.peakUsedBytes);
    EXPECT_EQ(parsed.delta.allocationCount, delta.allocationCount);
    EXPECT_EQ(parsed.delta.reallocationCount, delta.reallocationCount);
    EXPECT_EQ(parsed.delta.deallocationCount, delta.deallocationCount);

    payload[0u] = 0u;
    EXPECT_FALSE(Telemetry::ParsePerfMemoryPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

TEST(Telemetry, PerfMemoryPayloadRejectsInvalidInput){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    const Name scopeName("memory/project_arena");
    NWB::Core::Perf::MemorySnapshot snapshot = MakeTestMemorySnapshot(scopeName);
    NWB::Core::Perf::MemoryDelta delta = MakeTestMemoryDelta();

    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(
        testArena.arena,
        NAME_NONE,
        "Project Arena",
        snapshot,
        delta,
        payload
    ));

    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(
        testArena.arena,
        scopeName,
        AStringView(),
        snapshot,
        delta,
        payload
    ));

    snapshot.scopeName = Name("memory/other_arena");
    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(
        testArena.arena,
        scopeName,
        "Project Arena",
        snapshot,
        delta,
        payload
    ));

    snapshot = MakeTestMemorySnapshot(scopeName);
    delta.currentFrameIndex = 99u;
    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(
        testArena.arena,
        scopeName,
        "Project Arena",
        snapshot,
        delta,
        payload
    ));
}

TEST(Telemetry, RecordPerfMemoryUsesTelemetryEvent){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    const Name scopeName("memory/project_arena");
    const NWB::Core::Perf::MemorySnapshot snapshot = MakeTestMemorySnapshot(scopeName);
    const NWB::Core::Perf::MemoryDelta delta = MakeTestMemoryDelta();
    EXPECT_TRUE(Telemetry::RecordPerfMemory(recorder, scopeName, "memory/project_arena", snapshot, delta, 12u));

    const Telemetry::EventRecord* event = recorder.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    EXPECT_EQ(event->header.kind, Telemetry::EventKind::MemoryFrame);
    EXPECT_EQ(event->header.frameIndex, snapshot.frameIndex);
    EXPECT_EQ(event->header.streamId, 12u);

    Telemetry::PerfMemoryPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParsePerfMemoryPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    EXPECT_EQ(parsed.scopeName, scopeName);
    EXPECT_EQ(parsed.scopeText, "memory/project_arena");
    EXPECT_EQ(parsed.snapshot.usedBytes, snapshot.usedBytes);
    EXPECT_TRUE(parsed.delta.hasSamples);
    EXPECT_EQ(parsed.delta.usedBytes, delta.usedBytes);
}

TEST(Telemetry, PerfViewsExposeScopes){
    TestArena testArena;
    NWB::Core::Perf::TimingRecorder cpuTiming(testArena.arena);
    NWB::Core::Perf::TimingRecorder gpuTiming(testArena.arena);
    NWB::Core::Perf::MemoryRecorder memory(testArena.arena);
    NWB::Core::Perf::SessionReport report;
    BuildTestPerfReport(cpuTiming, gpuTiming, memory, report);

    EXPECT_EQ(report.cpuTiming.scopeCount(), 1u);
    EXPECT_EQ(report.gpuTiming.scopeCount(), 1u);
    EXPECT_EQ(report.memory.scopeCount(), 1u);
    EXPECT_EQ(report.cpuTiming.scopeNameAt(0u), Name("perf/cpu/update"));
    EXPECT_EQ(report.gpuTiming.scopeNameAt(0u), Name("perf/gpu/frame"));
    EXPECT_EQ(report.memory.scopeNameAt(0u), Name("perf/memory/project"));
    EXPECT_TRUE(report.cpuTiming.scopeAt(0u).valid());
    EXPECT_FALSE(report.cpuTiming.scopeAt(1u).valid());
    EXPECT_EQ(report.cpuTiming.statsAt(0u).sampleCount, 2u);
    EXPECT_EQ(report.gpuTiming.statsAt(0u).sampleCount, 1u);
    EXPECT_EQ(report.memory.snapshotAt(0u).usedBytes, 2048u);
    EXPECT_EQ(report.memory.deltaAt(0u).usedBytes, 1024u);
}

TEST(Telemetry, RecordPerfSessionReportUsesTelemetryEvents){
    TestArena testArena;
    NWB::Core::Perf::TimingRecorder cpuTiming(testArena.arena);
    NWB::Core::Perf::TimingRecorder gpuTiming(testArena.arena);
    NWB::Core::Perf::MemoryRecorder memory(testArena.arena);
    NWB::Core::Perf::SessionReport report;
    BuildTestPerfReport(cpuTiming, gpuTiming, memory, report);

    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    const Telemetry::PerfSessionRecordResult result = Telemetry::RecordPerfSessionReport(recorder, report, 17u);
    EXPECT_TRUE(result.ok());
    EXPECT_TRUE(result.recordedAny());
    EXPECT_EQ(result.cpuTimingEvents, 1u);
    EXPECT_EQ(result.gpuTimingEvents, 1u);
    EXPECT_EQ(result.memoryEvents, 1u);
    EXPECT_EQ(result.eventCount(), 3u);
    EXPECT_EQ(recorder.eventCount(), 3u);

    const Telemetry::EventRecord* cpuEvent = recorder.view().eventAt(0u);
    const Telemetry::EventRecord* gpuEvent = recorder.view().eventAt(1u);
    const Telemetry::EventRecord* memoryEvent = recorder.view().eventAt(2u);
    ASSERT_NE(cpuEvent, nullptr);
    ASSERT_NE(gpuEvent, nullptr);
    ASSERT_NE(memoryEvent, nullptr);

    EXPECT_EQ(cpuEvent->header.kind, Telemetry::EventKind::PerfFrame);
    EXPECT_EQ(gpuEvent->header.kind, Telemetry::EventKind::PerfFrame);
    EXPECT_EQ(memoryEvent->header.kind, Telemetry::EventKind::MemoryFrame);
    EXPECT_EQ(cpuEvent->header.streamId, 17u);
    EXPECT_EQ(gpuEvent->header.streamId, 17u);
    EXPECT_EQ(memoryEvent->header.streamId, 17u);

    Telemetry::PerfTimingPayload cpuPayload(testArena.arena);
    Telemetry::PerfTimingPayload gpuPayload(testArena.arena);
    Telemetry::PerfMemoryPayload memoryPayload(testArena.arena);
    EXPECT_TRUE(Telemetry::ParsePerfTimingPayload(testArena.arena, cpuEvent->payload.data(), cpuEvent->payload.size(), cpuPayload));
    EXPECT_TRUE(Telemetry::ParsePerfTimingPayload(testArena.arena, gpuEvent->payload.data(), gpuEvent->payload.size(), gpuPayload));
    EXPECT_TRUE(Telemetry::ParsePerfMemoryPayload(testArena.arena, memoryEvent->payload.data(), memoryEvent->payload.size(), memoryPayload));
    EXPECT_EQ(cpuPayload.source, Telemetry::PerfTimingSource::Cpu);
    EXPECT_EQ(gpuPayload.source, Telemetry::PerfTimingSource::Gpu);
    EXPECT_EQ(cpuPayload.scopeName, Name("perf/cpu/update"));
    EXPECT_EQ(gpuPayload.scopeName, Name("perf/gpu/frame"));
    EXPECT_EQ(memoryPayload.scopeName, Name("perf/memory/project"));
    EXPECT_EQ(memoryPayload.snapshot.frameIndex, 102u);
    EXPECT_TRUE(memoryPayload.delta.hasSamples);
}

TEST(Telemetry, CaptureSessionRecordsPerfReport){
    TestArena testArena;
    NWB::Core::Perf::TimingRecorder cpuTiming(testArena.arena);
    NWB::Core::Perf::TimingRecorder gpuTiming(testArena.arena);
    NWB::Core::Perf::MemoryRecorder memory(testArena.arena);
    NWB::Core::Perf::SessionReport report;
    BuildTestPerfReport(cpuTiming, gpuTiming, memory, report);

    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());

    const Telemetry::PerfSessionRecordResult result = session.recordPerfReport(report, 23u);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.eventCount(), 3u);
    EXPECT_EQ(session.eventCount(), 3u);

    const Telemetry::EventRecord* cpuEvent = session.view().eventAt(0u);
    const Telemetry::EventRecord* gpuEvent = session.view().eventAt(1u);
    const Telemetry::EventRecord* memoryEvent = session.view().eventAt(2u);
    ASSERT_NE(cpuEvent, nullptr);
    ASSERT_NE(gpuEvent, nullptr);
    ASSERT_NE(memoryEvent, nullptr);

    EXPECT_EQ(cpuEvent->header.kind, Telemetry::EventKind::PerfFrame);
    EXPECT_EQ(gpuEvent->header.kind, Telemetry::EventKind::PerfFrame);
    EXPECT_EQ(memoryEvent->header.kind, Telemetry::EventKind::MemoryFrame);
    EXPECT_EQ(cpuEvent->header.streamId, 23u);
    EXPECT_EQ(gpuEvent->header.streamId, 23u);
    EXPECT_EQ(memoryEvent->header.streamId, 23u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

