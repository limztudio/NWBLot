// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"

#include <tests/unit/telemetry/perf_case_types.adhd.h>

#include <global/binary.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_memory_perf_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TestCases = NWB::Tests::TelemetryCases;

static constexpr AStringView s_MemoryProjectArena = TestCases::s_TelemetryMemoryProjectArena;
static constexpr AStringView s_ProjectArenaText = TestCases::s_TelemetryProjectArenaText;
using namespace TelemetryTestDetail;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Telemetry, PerfMemoryPayloadRejectsInvalidInput){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    const Name scopeName{ s_MemoryProjectArena };
    NWB::Core::Perf::MemorySnapshot snapshot = MakeTestMemorySnapshot(scopeName);
    NWB::Core::Perf::MemoryDelta delta = MakeTestMemoryDelta();

    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(s_NameNone, s_ProjectArenaText, snapshot, delta, payload));

    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(scopeName, AStringView(), snapshot, delta, payload));

    snapshot.scopeName = Name(TestCases::s_TelemetryCaseMemoryOtherArena);
    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(scopeName, s_ProjectArenaText, snapshot, delta, payload));

    snapshot = MakeTestMemorySnapshot(scopeName);
    delta.currentFrameIndex = 99u;
    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(scopeName, s_ProjectArenaText, snapshot, delta, payload));
}

TEST(Telemetry, MemoryDifferencePreservesSignedLimitsAndLargeUnsignedCounters){
    using MemorySnapshot = NWB::Core::Perf::MemorySnapshot;
    constexpr u64 s_MaxSigned = static_cast<u64>(Limit<i64>::s_Max);
    constexpr u64 s_MinMagnitude = s_MaxSigned + 1u;
    using CounterCase = NWB::Tests::TelemetryCases::MemoryDifferenceCase;
    constexpr Array<CounterCase, 10> s_Cases = {{
        { .current = 0u, .previous = 0u, .expected = 0 },
        { .current = s_MaxSigned, .previous = 0u, .expected = Limit<i64>::s_Max },
        { .current = 0u, .previous = s_MaxSigned, .expected = -Limit<i64>::s_Max },
        { .current = 0u, .previous = s_MinMagnitude, .expected = Limit<i64>::s_Min },
        { .current = s_MinMagnitude - 1u, .previous = s_MinMagnitude, .expected = -1 },
        { .current = s_MinMagnitude + 5u, .previous = s_MinMagnitude - 5u, .expected = 10 },
        { .current = s_MinMagnitude - 5u, .previous = s_MinMagnitude + 5u, .expected = -10 },
        { .current = Limit<u64>::s_Max, .previous = Limit<u64>::s_Max - 17u, .expected = 17 },
        { .current = Limit<u64>::s_Max - 17u, .previous = Limit<u64>::s_Max, .expected = -17 },
        { .current = 0u, .previous = 128u, .expected = -128 },
    }};
    const Name scopeName(s_MemoryProjectArena);
    const auto makeSnapshot = [scopeName](const u64 counter, const u64 frame)noexcept{
        MemorySnapshot snapshot;
        snapshot.scopeName = scopeName;
        snapshot.frameIndex = frame;
        snapshot.reservedBytes = counter;
        snapshot.usedBytes = counter;
        snapshot.peakUsedBytes = counter;
        snapshot.allocationCount = counter;
        snapshot.reallocationCount = counter;
        snapshot.deallocationCount = counter;
        return snapshot;
    };
    for(const CounterCase& test : s_Cases){
        SCOPED_TRACE(test.current);
        SCOPED_TRACE(test.previous);
        const auto delta = NWB::Core::Perf::Difference(makeSnapshot(test.current, 2u), makeSnapshot(test.previous, 1u));
        ASSERT_TRUE(delta.valid());
        EXPECT_EQ(delta.previousFrameIndex, 1u);
        EXPECT_EQ(delta.currentFrameIndex, 2u);
        EXPECT_EQ(delta.reservedBytes, test.expected);
        EXPECT_EQ(delta.usedBytes, test.expected);
        EXPECT_EQ(delta.peakUsedBytes, test.expected);
        EXPECT_EQ(delta.allocationCount, test.expected);
        EXPECT_EQ(delta.reallocationCount, test.expected);
        EXPECT_EQ(delta.deallocationCount, test.expected);
    }
}

TEST(Telemetry, MemoryDifferenceOmitsUnrepresentableCountersFromTelemetry){
    using MemorySnapshot = NWB::Core::Perf::MemorySnapshot;
    using CounterField = u64 MemorySnapshot::*;
    constexpr Array<CounterField, 6> s_Fields = { &MemorySnapshot::reservedBytes, &MemorySnapshot::usedBytes, &MemorySnapshot::peakUsedBytes, &MemorySnapshot::allocationCount, &MemorySnapshot::reallocationCount, &MemorySnapshot::deallocationCount };
    using CounterCase = NWB::Tests::TelemetryCases::MemoryUnrepresentableCase;
    constexpr u64 s_MinMagnitude = static_cast<u64>(Limit<i64>::s_Max) + 1u;
    constexpr Array<CounterCase, 4> s_Cases = {{
        { .current = s_MinMagnitude, .previous = 0u },
        { .current = 0u, .previous = s_MinMagnitude + 1u },
        { .current = Limit<u64>::s_Max, .previous = 0u },
        { .current = 0u, .previous = Limit<u64>::s_Max },
    }};
    TestArena testArena;
    const Name scopeName(s_MemoryProjectArena);
    Telemetry::TelemetryBytes payload(testArena.arena);
    for(const CounterField field : s_Fields){
        for(const CounterCase& test : s_Cases){
            SCOPED_TRACE(test.current);
            SCOPED_TRACE(test.previous);
            MemorySnapshot previous;
            previous.scopeName = scopeName;
            previous.frameIndex = 1u;
            previous.*field = test.previous;
            MemorySnapshot current;
            current.scopeName = scopeName;
            current.frameIndex = 2u;
            current.*field = test.current;
            const auto delta = NWB::Core::Perf::Difference(current, previous);
            EXPECT_FALSE(delta.valid());
            ASSERT_TRUE(Telemetry::BuildPerfMemoryPayload(scopeName, s_ProjectArenaText, current, delta, payload));
            const auto parsed = Telemetry::ParsePerfMemoryPayload(payload.data(), payload.size());
            ASSERT_TRUE(parsed);
            EXPECT_FALSE(parsed->delta.valid());
            EXPECT_EQ(parsed->snapshot.*field, test.current);
            EXPECT_EQ(parsed->snapshot.frameIndex, 2u);
        }
    }
}

TEST(Telemetry, MemoryReportMarksUnrepresentableTotalsWithoutChangingIndividualRecords){
    using DeltaCase = NWB::Tests::TelemetryCases::MemoryDeltaCase;
    constexpr Array<DeltaCase, 6> s_Cases = {{
        { .first = Limit<i64>::s_Max, .second = 1, .expected = 0, .valid = false },
        { .first = Limit<i64>::s_Min, .second = -1, .expected = 0, .valid = false },
        { .first = Limit<i64>::s_Max - 1, .second = 1, .expected = Limit<i64>::s_Max, .valid = true },
        { .first = Limit<i64>::s_Min + 1, .second = -1, .expected = Limit<i64>::s_Min, .valid = true },
        { .first = Limit<i64>::s_Max, .second = -Limit<i64>::s_Max, .expected = 0, .valid = true },
        { .first = Limit<i64>::s_Min, .second = Limit<i64>::s_Max, .expected = -1, .valid = true },
    }};
    TestArena testArena;
    const Name scopeName(s_MemoryProjectArena);
    for(const DeltaCase& test : s_Cases){
        SCOPED_TRACE(test.first);
        SCOPED_TRACE(test.second);
        Telemetry::Recorder recorder(testArena.arena);
        recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
        auto snapshot = MakeTestMemorySnapshot(scopeName);
        auto delta = MakeTestMemoryDelta();
        delta.usedBytes = test.first;
        ASSERT_TRUE(Telemetry::RecordPerfMemory(recorder, scopeName, s_ProjectArenaText, snapshot, delta));
        ++snapshot.frameIndex;
        ++delta.previousFrameIndex;
        ++delta.currentFrameIndex;
        delta.usedBytes = test.second;
        ASSERT_TRUE(Telemetry::RecordPerfMemory(recorder, scopeName, s_ProjectArenaText, snapshot, delta));
        const auto report = Log::BuildTelemetryReport(recorder.view(), testArena.arena, testArena.scratchArena);
        ASSERT_TRUE(report);
        EXPECT_EQ(report->summary.parseFailureCount, 0u);
        EXPECT_EQ(report->summary.memoryEventCount, 2u);
        const auto& summary = report->summary.memorySources[NWB::Core::Perf::MemorySource::ExplicitScope];
        EXPECT_EQ(summary.eventCount, 2u);
        EXPECT_EQ(summary.maxUsedBytes, snapshot.usedBytes);
        EXPECT_EQ(summary.totalUsedDeltaValid, test.valid);
        const AStringView json(report->json);
        if(test.valid){
            EXPECT_EQ(summary.totalUsedDeltaBytes, test.expected);
            const auto expected = StringFormat(testArena.arena, "\"totalUsedDeltaBytes\": {}", test.expected);
            EXPECT_NE(json.find(AStringView(expected)), AStringView::npos);
            EXPECT_EQ(json.find("\"totalUsedDeltaBytes\": null"), AStringView::npos);
        }
        else{
            EXPECT_NE(json.find("\"totalUsedDeltaBytes\": null"), AStringView::npos);
            ++snapshot.frameIndex;
            ++delta.previousFrameIndex;
            ++delta.currentFrameIndex;
            delta.usedBytes = -test.second;
            ASSERT_TRUE(Telemetry::RecordPerfMemory(recorder, scopeName, s_ProjectArenaText, snapshot, delta));
            const auto continued = Log::BuildTelemetryReport(recorder.view(), testArena.arena, testArena.scratchArena);
            ASSERT_TRUE(continued);
            EXPECT_FALSE(continued->summary.memorySources[NWB::Core::Perf::MemorySource::ExplicitScope].totalUsedDeltaValid);
            EXPECT_EQ(continued->summary.memoryEventCount, 3u);
        }
        const Array<i64, 2> values = { test.first, test.second };
        for(const i64 value : values){
            const auto expected = StringFormat(testArena.arena, "\"usedBytes\": {}", value);
            EXPECT_NE(json.find(AStringView(expected)), AStringView::npos);
        }

        recorder.clear();
        delta.usedBytes = 0;
        ASSERT_TRUE(Telemetry::RecordPerfMemory(recorder, scopeName, s_ProjectArenaText, snapshot, delta));
        const auto reset = Log::BuildTelemetryReport(recorder.view(), testArena.arena, testArena.scratchArena);
        ASSERT_TRUE(reset);
        EXPECT_TRUE(reset->summary.memorySources[NWB::Core::Perf::MemorySource::ExplicitScope].totalUsedDeltaValid);
        EXPECT_EQ(reset->summary.memorySources[NWB::Core::Perf::MemorySource::ExplicitScope].totalUsedDeltaBytes, 0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

