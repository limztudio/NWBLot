// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"

#include <core/perf/named_registry.h>

#include <global/binary.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_perf_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_MEMORY_PROJECT_ARENA = "memory/project_arena";
static constexpr AStringView s_RENDERER_FRAME = "renderer/frame";
static constexpr AStringView s_RENDERER_FRAME_TEXT = "Renderer Frame";
static constexpr AStringView s_PROJECT_ARENA_TEXT = "Project Arena";
static constexpr u32 s_ScopeNameConversionFailure = 0x5C0FEu;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;


struct GenericScopeRecord{
    Name name;
    u32 generation = 1u;
};

struct BorrowedScopePointer{
    GenericScopeRecord* record = nullptr;


public:
    [[nodiscard]] GenericScopeRecord* const& get()const noexcept{ return record; }
};

struct ThrowingScopeName{
    explicit operator Name() & noexcept{ return s_NameNone; }
    operator const Name&()const&{ throw s_ScopeNameConversionFailure; }
};

struct ThrowingScopeRecord{
    ThrowingScopeName name;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Telemetry, PerfTimingPayloadRejectsNonCurrentVersionsAndCorruptMagicAfterValidParse){
    TestArena testArena;
    const Name scopeName{s_RENDERER_FRAME};
    const NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildPerfTimingPayload(testArena.arena, Telemetry::PerfTimingSource::Gpu, scopeName, s_RENDERER_FRAME_TEXT, stats, payload));

    Expected<Telemetry::PerfTimingPayload> parsed = MakeUnexpected(Failure{});
    ASSERT_TRUE((parsed = Telemetry::ParsePerfTimingPayload(testArena.arena, payload.data(), payload.size())));

    const Telemetry::TelemetryBytes current = payload;
    const u16 unsupportedVersions[] = { 0u, static_cast<u16>(Telemetry::s_PerfTimingPayloadVersion + 1u), Limit<u16>::s_Max, };
    for(const u16 version : unsupportedVersions){
        SCOPED_TRACE(version);
        usize cursor = 0u;
        const auto readHeader = ReadPOD<Telemetry::EncodedPerfTimingPayloadHeader>(current, cursor);
        ASSERT_TRUE(readHeader);
        Telemetry::EncodedPerfTimingPayloadHeader header = *readHeader;
        header.version = version;
        NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
        EXPECT_FALSE((parsed = Telemetry::ParsePerfTimingPayload(testArena.arena, payload.data(), payload.size())));
        ASSERT_TRUE((parsed = Telemetry::ParsePerfTimingPayload(testArena.arena, current.data(), current.size())));
        EXPECT_EQ(parsed->scopeName, scopeName);
    }
    payload = current;
    payload[0u] = 0u;
    EXPECT_FALSE((parsed = Telemetry::ParsePerfTimingPayload(testArena.arena, payload.data(), payload.size())));
}

TEST(Telemetry, PerfTimingPayloadRejectsInvalidInput){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();

    EXPECT_FALSE(Telemetry::BuildPerfTimingPayload(testArena.arena, Telemetry::PerfTimingSource::Unknown, Name(s_RENDERER_FRAME), s_RENDERER_FRAME_TEXT, stats, payload));

    stats.sampleCount = 0u;
    EXPECT_FALSE(Telemetry::BuildPerfTimingPayload(testArena.arena, Telemetry::PerfTimingSource::Cpu, Name(s_RENDERER_FRAME), s_RENDERER_FRAME_TEXT, stats, payload));
}

TEST(Telemetry, PerfTimingRejectsNonfiniteDurationsAtBuildAndWireAdmission){
    using TimingStats = NWB::Core::Perf::TimingStats;
    using TimingField = f64 TimingStats::*;
    using WireField = f64 Telemetry::EncodedPerfTimingPayloadHeader::*;
    constexpr Array<TimingField, 4u> s_Fields = { &TimingStats::seconds, &TimingStats::minSeconds, &TimingStats::maxSeconds, &TimingStats::lastSeconds };
    constexpr WireField s_WireFields[] = {
        &Telemetry::EncodedPerfTimingPayloadHeader::seconds, &Telemetry::EncodedPerfTimingPayloadHeader::minSeconds,
        &Telemetry::EncodedPerfTimingPayloadHeader::maxSeconds, &Telemetry::EncodedPerfTimingPayloadHeader::lastSeconds,
    };
    constexpr u64 s_NonfiniteBits[] = { 0x7FF0000000000000ull, 0xFFF0000000000000ull, 0x7FF8000000000001ull };
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    Telemetry::TelemetryBytes current(testArena.arena);
    const Name scopeName(s_RENDERER_FRAME);
    const TimingStats validStats = MakeTestTimingStats();
    ASSERT_TRUE(Telemetry::BuildPerfTimingPayload(testArena.arena, Telemetry::PerfTimingSource::Cpu, scopeName, s_RENDERER_FRAME_TEXT, validStats, current));
    for(usize field = 0u; field < s_Fields.size(); ++field){
        for(const u64 bits : s_NonfiniteBits){
            SCOPED_TRACE(field);
            SCOPED_TRACE(bits);
            TimingStats invalid = validStats;
            invalid.*s_Fields[field] = BitCast<f64>(bits);
            EXPECT_FALSE(Telemetry::BuildPerfTimingPayload(testArena.arena, Telemetry::PerfTimingSource::Cpu, scopeName, s_RENDERER_FRAME_TEXT, invalid, payload));
            payload = current;
            usize cursor = 0u;
            const auto readHeader = ReadPOD<Telemetry::EncodedPerfTimingPayloadHeader>(current, cursor);
            ASSERT_TRUE(readHeader);
            auto header = *readHeader;
            header.*s_WireFields[field] = BitCast<f64>(bits);
            NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
            EXPECT_FALSE(Telemetry::ParsePerfTimingPayload(testArena.arena, payload.data(), payload.size()));
            const auto recovered = Telemetry::ParsePerfTimingPayload(testArena.arena, current.data(), current.size());
            ASSERT_TRUE(recovered);
            EXPECT_EQ(recovered->stats.seconds, validStats.seconds);
        }
    }
}

TEST(Telemetry, PerfMemoryPayloadRejectsCorruptedHeaderAfterValidParse){
    TestArena testArena;
    const Name scopeName{s_MEMORY_PROJECT_ARENA};
    const NWB::Core::Perf::MemorySnapshot snapshot = MakeTestMemorySnapshot(scopeName);
    const NWB::Core::Perf::MemoryDelta delta = MakeTestMemoryDelta();

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildPerfMemoryPayload(testArena.arena, scopeName, s_PROJECT_ARENA_TEXT, snapshot, delta, payload));

    Expected<Telemetry::PerfMemoryPayload> parsed = MakeUnexpected(Failure{});
    ASSERT_TRUE((parsed = Telemetry::ParsePerfMemoryPayload(testArena.arena, payload.data(), payload.size())));

    payload[0u] = 0u;
    EXPECT_FALSE((parsed = Telemetry::ParsePerfMemoryPayload(testArena.arena, payload.data(), payload.size())));
}

TEST(Telemetry, PerfMemoryPayloadRejectsInvalidInput){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    const Name scopeName{s_MEMORY_PROJECT_ARENA};
    NWB::Core::Perf::MemorySnapshot snapshot = MakeTestMemorySnapshot(scopeName);
    NWB::Core::Perf::MemoryDelta delta = MakeTestMemoryDelta();

    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(testArena.arena, s_NameNone, s_PROJECT_ARENA_TEXT, snapshot, delta, payload));

    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(testArena.arena, scopeName, AStringView(), snapshot, delta, payload));

    snapshot.scopeName = Name("memory/other_arena");
    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(testArena.arena, scopeName, s_PROJECT_ARENA_TEXT, snapshot, delta, payload));

    snapshot = MakeTestMemorySnapshot(scopeName);
    delta.currentFrameIndex = 99u;
    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(testArena.arena, scopeName, s_PROJECT_ARENA_TEXT, snapshot, delta, payload));
}

TEST(Telemetry, MemoryDifferencePreservesSignedLimitsAndLargeUnsignedCounters){
    using MemorySnapshot = NWB::Core::Perf::MemorySnapshot;
    constexpr u64 s_MaxSigned = static_cast<u64>(Limit<i64>::s_Max);
    constexpr u64 s_MinMagnitude = s_MaxSigned + 1u;
    struct CounterCase{
        u64 current;
        u64 previous;
        i64 expected;
    };
    constexpr CounterCase s_Cases[] = {
        { 0u, 0u, 0 },
        { s_MaxSigned, 0u, Limit<i64>::s_Max },
        { 0u, s_MaxSigned, -Limit<i64>::s_Max },
        { 0u, s_MinMagnitude, Limit<i64>::s_Min },
        { s_MinMagnitude - 1u, s_MinMagnitude, -1 },
        { s_MinMagnitude + 5u, s_MinMagnitude - 5u, 10 },
        { s_MinMagnitude - 5u, s_MinMagnitude + 5u, -10 },
        { Limit<u64>::s_Max, Limit<u64>::s_Max - 17u, 17 },
        { Limit<u64>::s_Max - 17u, Limit<u64>::s_Max, -17 },
        { 0u, 128u, -128 },
    };
    const Name scopeName(s_MEMORY_PROJECT_ARENA);
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
    constexpr CounterField s_Fields[] = {
        &MemorySnapshot::reservedBytes, &MemorySnapshot::usedBytes, &MemorySnapshot::peakUsedBytes,
        &MemorySnapshot::allocationCount, &MemorySnapshot::reallocationCount, &MemorySnapshot::deallocationCount,
    };
    struct CounterCase{
        u64 current;
        u64 previous;
    };
    constexpr u64 s_MinMagnitude = static_cast<u64>(Limit<i64>::s_Max) + 1u;
    constexpr CounterCase s_Cases[] = {
        { s_MinMagnitude, 0u },
        { 0u, s_MinMagnitude + 1u },
        { Limit<u64>::s_Max, 0u },
        { 0u, Limit<u64>::s_Max },
    };
    TestArena testArena;
    const Name scopeName(s_MEMORY_PROJECT_ARENA);
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
            ASSERT_TRUE(Telemetry::BuildPerfMemoryPayload(testArena.arena, scopeName, s_PROJECT_ARENA_TEXT, current, delta, payload));
            const auto parsed = Telemetry::ParsePerfMemoryPayload(testArena.arena, payload.data(), payload.size());
            ASSERT_TRUE(parsed);
            EXPECT_FALSE(parsed->delta.valid());
            EXPECT_EQ(parsed->snapshot.*field, test.current);
            EXPECT_EQ(parsed->snapshot.frameIndex, 2u);
        }
    }
}

TEST(Telemetry, MemoryReportMarksUnrepresentableTotalsWithoutChangingIndividualRecords){
    struct DeltaCase{
        i64 first;
        i64 second;
        i64 expected;
        bool valid;
    };
    constexpr DeltaCase s_Cases[] = {
        { Limit<i64>::s_Max, 1, 0, false },
        { Limit<i64>::s_Min, -1, 0, false },
        { Limit<i64>::s_Max - 1, 1, Limit<i64>::s_Max, true },
        { Limit<i64>::s_Min + 1, -1, Limit<i64>::s_Min, true },
        { Limit<i64>::s_Max, -Limit<i64>::s_Max, 0, true },
        { Limit<i64>::s_Min, Limit<i64>::s_Max, -1, true },
    };
    TestArena testArena;
    const Name scopeName(s_MEMORY_PROJECT_ARENA);
    for(const DeltaCase& test : s_Cases){
        SCOPED_TRACE(test.first);
        SCOPED_TRACE(test.second);
        Telemetry::Recorder recorder(testArena.arena);
        recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
        auto snapshot = MakeTestMemorySnapshot(scopeName);
        auto delta = MakeTestMemoryDelta();
        delta.usedBytes = test.first;
        ASSERT_TRUE(Telemetry::RecordPerfMemory(recorder, scopeName, s_PROJECT_ARENA_TEXT, snapshot, delta));
        ++snapshot.frameIndex;
        ++delta.previousFrameIndex;
        ++delta.currentFrameIndex;
        delta.usedBytes = test.second;
        ASSERT_TRUE(Telemetry::RecordPerfMemory(recorder, scopeName, s_PROJECT_ARENA_TEXT, snapshot, delta));
        const auto report = Log::BuildTelemetryReport(testArena.arena, recorder.view(), testArena.scratchArena);
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
            ASSERT_TRUE(Telemetry::RecordPerfMemory(recorder, scopeName, s_PROJECT_ARENA_TEXT, snapshot, delta));
            const auto continued = Log::BuildTelemetryReport(testArena.arena, recorder.view(), testArena.scratchArena);
            ASSERT_TRUE(continued);
            EXPECT_FALSE(continued->summary.memorySources[NWB::Core::Perf::MemorySource::ExplicitScope].totalUsedDeltaValid);
            EXPECT_EQ(continued->summary.memoryEventCount, 3u);
        }
        const i64 values[] = { test.first, test.second };
        for(const i64 value : values){
            const auto expected = StringFormat(testArena.arena, "\"usedBytes\": {}", value);
            EXPECT_NE(json.find(AStringView(expected)), AStringView::npos);
        }

        recorder.clear();
        delta.usedBytes = 0;
        ASSERT_TRUE(Telemetry::RecordPerfMemory(recorder, scopeName, s_PROJECT_ARENA_TEXT, snapshot, delta));
        const auto reset = Log::BuildTelemetryReport(testArena.arena, recorder.view(), testArena.scratchArena);
        ASSERT_TRUE(reset);
        EXPECT_TRUE(reset->summary.memorySources[NWB::Core::Perf::MemorySource::ExplicitScope].totalUsedDeltaValid);
        EXPECT_EQ(reset->summary.memorySources[NWB::Core::Perf::MemorySource::ExplicitScope].totalUsedDeltaBytes, 0);
    }
}

TEST(Telemetry, GenericScopeHelpersPreserveBorrowedPointerValuesAndImplicitNameFailures){
    using NWB::Core::Perf::FindNamedScope;
    using NWB::Core::Perf::ScopeNameAt;
    using NWB::Core::Perf::TimingScopeId;
    GenericScopeRecord record{ Name("perf/generic/scope"), 3u };
    const Array<BorrowedScopePointer, 1u> owners{ BorrowedScopePointer{ &record } };
    const TimingScopeId scope{ 0u, 3u };
    static_assert(IsSame_V<decltype(FindNamedScope<TimingScopeId>(owners, scope)), GenericScopeRecord*>);
    static_assert(noexcept(FindNamedScope<TimingScopeId>(owners, scope)));
    EXPECT_EQ(FindNamedScope<TimingScopeId>(owners, scope), &record);
    EXPECT_EQ(FindNamedScope<TimingScopeId>(owners, TimingScopeId{ 0u, 4u }), nullptr);
    EXPECT_EQ(FindNamedScope<TimingScopeId>(owners, TimingScopeId{ 1u, 3u }), nullptr);

    const Array<GenericScopeRecord*, 1u> records{ &record };
    static_assert(noexcept(ScopeNameAt(records, 0u)));
    EXPECT_EQ(ScopeNameAt(records, 0u), record.name);

    ThrowingScopeRecord throwing{};
    const Array<ThrowingScopeRecord*, 1u> throwingRecords{ &throwing };
    static_assert(noexcept(Name(throwing.name)));
    static_assert(!noexcept(DeclVal<void(*)(Name)noexcept>()(throwing.name)));
    static_assert(!noexcept(ScopeNameAt(throwingRecords, 0u)));
    EXPECT_THROW({ const Name value = ScopeNameAt(throwingRecords, 0u); EXPECT_EQ(value, s_NameNone); }, u32);
}

TEST(Telemetry, PerfTimingViewRejectsIndexAtScopeCount){
    TestArena testArena;
    NWB::Core::Perf::TimingRecorder cpuTiming(testArena.arena);
    cpuTiming.setEnabled(true);
    const auto scopeResult = cpuTiming.registerScope(Name("perf/cpu/update"));
    ASSERT_TRUE(scopeResult);
    const NWB::Core::Perf::TimingScopeId scope = *scopeResult;
    cpuTiming.recordSample(scope, 0.010, 100u);
    cpuTiming.publishFrame(101u);
    const NWB::Core::Perf::TimingView view(cpuTiming);

    ASSERT_EQ(view.scopeCount(), 1u);
    EXPECT_FALSE(view.scopeAt(view.scopeCount()).valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

