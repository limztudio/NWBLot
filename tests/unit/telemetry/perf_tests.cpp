// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"

#include <global/binary.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_perf_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_MEMORY_PROJECT_ARENA = "memory/project_arena";
static constexpr AStringView s_RENDERER_FRAME = "renderer/frame";
static constexpr AStringView s_RENDERER_FRAME_TEXT = "Renderer Frame";
static constexpr AStringView s_PROJECT_ARENA_TEXT = "Project Arena";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;


TEST(Telemetry, PerfTimingPayloadRejectsNonCurrentVersionsAndCorruptMagicAfterValidParse){
    TestArena testArena;
    const Name scopeName{s_RENDERER_FRAME};
    const NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildPerfTimingPayload(
        testArena.arena,
        Telemetry::PerfTimingSource::Gpu,
        scopeName,
        s_RENDERER_FRAME_TEXT,
        stats,
        payload
    ));

    Telemetry::PerfTimingPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParsePerfTimingPayload(testArena.arena, payload.data(), payload.size(), parsed));

    const Telemetry::TelemetryBytes current = payload;
    const u16 unsupportedVersions[] = {
        0u,
        static_cast<u16>(Telemetry::s_PerfTimingPayloadVersion + 1u),
        Limit<u16>::s_Max,
    };
    for(const u16 version : unsupportedVersions){
        SCOPED_TRACE(version);
        Telemetry::EncodedPerfTimingPayloadHeader header;
        usize cursor = 0u;
        ASSERT_TRUE(ReadPOD(current, cursor, header));
        header.version = version;
        GLB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
        EXPECT_FALSE(Telemetry::ParsePerfTimingPayload(testArena.arena, payload.data(), payload.size(), parsed));
        EXPECT_TRUE(parsed.scopeText.empty());
        ASSERT_TRUE(Telemetry::ParsePerfTimingPayload(testArena.arena, current.data(), current.size(), parsed));
        EXPECT_EQ(parsed.scopeName, scopeName);
    }
    payload = current;
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
        Name(s_RENDERER_FRAME),
        s_RENDERER_FRAME_TEXT,
        stats,
        payload
    ));

    stats.sampleCount = 0u;
    EXPECT_FALSE(Telemetry::BuildPerfTimingPayload(
        testArena.arena,
        Telemetry::PerfTimingSource::Cpu,
        Name(s_RENDERER_FRAME),
        s_RENDERER_FRAME_TEXT,
        stats,
        payload
    ));
}

TEST(Telemetry, PerfMemoryPayloadRejectsCorruptedHeaderAfterValidParse){
    TestArena testArena;
    const Name scopeName{s_MEMORY_PROJECT_ARENA};
    const NWB::Core::Perf::MemorySnapshot snapshot = MakeTestMemorySnapshot(scopeName);
    const NWB::Core::Perf::MemoryDelta delta = MakeTestMemoryDelta();

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildPerfMemoryPayload(
        testArena.arena,
        scopeName,
        s_PROJECT_ARENA_TEXT,
        snapshot,
        delta,
        payload
    ));

    Telemetry::PerfMemoryPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParsePerfMemoryPayload(testArena.arena, payload.data(), payload.size(), parsed));

    payload[0u] = 0u;
    EXPECT_FALSE(Telemetry::ParsePerfMemoryPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

TEST(Telemetry, PerfMemoryPayloadRejectsInvalidInput){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    const Name scopeName{s_MEMORY_PROJECT_ARENA};
    NWB::Core::Perf::MemorySnapshot snapshot = MakeTestMemorySnapshot(scopeName);
    NWB::Core::Perf::MemoryDelta delta = MakeTestMemoryDelta();

    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(
        testArena.arena,
        s_NameNone,
        s_PROJECT_ARENA_TEXT,
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
        s_PROJECT_ARENA_TEXT,
        snapshot,
        delta,
        payload
    ));

    snapshot = MakeTestMemorySnapshot(scopeName);
    delta.currentFrameIndex = 99u;
    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(
        testArena.arena,
        scopeName,
        s_PROJECT_ARENA_TEXT,
        snapshot,
        delta,
        payload
    ));
}

TEST(Telemetry, PerfTimingViewRejectsIndexAtScopeCount){
    TestArena testArena;
    NWB::Core::Perf::TimingRecorder cpuTiming(testArena.arena);
    cpuTiming.setEnabled(true);
    const NWB::Core::Perf::TimingScopeId scope = cpuTiming.registerScope(Name("perf/cpu/update"));
    cpuTiming.recordSample(scope, 0.010, 100u);
    cpuTiming.publishFrame(101u);
    const NWB::Core::Perf::TimingView view(cpuTiming);

    ASSERT_EQ(view.scopeCount(), 1u);
    EXPECT_FALSE(view.scopeAt(view.scopeCount()).valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

