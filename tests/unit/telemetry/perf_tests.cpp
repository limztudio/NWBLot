// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"

#include <global/named_registry.h>

#include <global/binary.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_perf_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_RendererFrame = "renderer/frame";
static constexpr AStringView s_RendererFrameText = "Renderer Frame";
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
    explicit operator Name()& noexcept{ return s_NameNone; }

    operator const Name&()const&{ throw s_ScopeNameConversionFailure; }
};

struct ThrowingScopeRecord{
    ThrowingScopeName name;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Telemetry, PerfTimingPayloadRejectsNonCurrentVersionsAndCorruptMagicAfterValidParse){
    TestArena testArena;
    const Name scopeName{ s_RendererFrame };
    const NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildPerfTimingPayload(Telemetry::PerfTimingSource::Gpu, scopeName, s_RendererFrameText, stats, payload));

    Expected<Telemetry::PerfTimingPayload> parsed = MakeUnexpected(Failure{});
    ASSERT_TRUE((parsed = Telemetry::ParsePerfTimingPayload(payload.data(), payload.size())));

    const Telemetry::TelemetryBytes current = payload;
    const Array<u16, 3> unsupportedVersions = { 0u, static_cast<u16>(Telemetry::s_PerfTimingPayloadVersion + 1u), Limit<u16>::s_Max };
    for(const u16 version : unsupportedVersions){
        SCOPED_TRACE(version);
        usize cursor = 0u;
        const auto readHeader = ReadPOD<Telemetry::EncodedPerfTimingPayloadHeader>(current, cursor);
        ASSERT_TRUE(readHeader);
        Telemetry::EncodedPerfTimingPayloadHeader header = *readHeader;
        header.version = version;
        NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
        EXPECT_FALSE((parsed = Telemetry::ParsePerfTimingPayload(payload.data(), payload.size())));
        ASSERT_TRUE((parsed = Telemetry::ParsePerfTimingPayload(current.data(), current.size())));
        EXPECT_EQ(parsed->scopeName, scopeName);
    }
    payload = current;
    payload[0u] = 0u;
    EXPECT_FALSE((parsed = Telemetry::ParsePerfTimingPayload(payload.data(), payload.size())));
}

TEST(Telemetry, PerfPayloadBuildersPreserveAliasedScopeText){
    TestArena testArena;
    const Name scopeName{ s_RendererFrame };
    const NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();
    const auto snapshot = MakeTestMemorySnapshot(scopeName);
    const auto delta = MakeTestMemoryDelta();
    constexpr AStringView s_Text = "borrowed scope inside output";
    Telemetry::TelemetryBytes payload(testArena.arena);

    const auto seed = [&](){
        payload.clear();
        payload.reserve(512u);
        payload.resize(s_Text.size());
        NWB_MEMCPY(payload.data(), payload.size(), s_Text.data(), s_Text.size());
        return AStringView(reinterpret_cast<const char*>(payload.data()), payload.size());
    };

    ASSERT_TRUE(Telemetry::BuildPerfTimingPayload(Telemetry::PerfTimingSource::Cpu, scopeName, seed(), stats, payload));
    const auto timing = Telemetry::ParsePerfTimingPayload(payload.data(), payload.size());
    ASSERT_TRUE(timing);
    EXPECT_EQ(timing->scopeText, s_Text);

    ASSERT_TRUE(Telemetry::BuildPerfMemoryPayload(scopeName, seed(), snapshot, delta, payload));
    const auto memory = Telemetry::ParsePerfMemoryPayload(payload.data(), payload.size());
    ASSERT_TRUE(memory);
    EXPECT_EQ(memory->scopeText, s_Text);

    ASSERT_TRUE(Telemetry::BuildPerfTimingPayload(Telemetry::PerfTimingSource::Cpu, scopeName, memory->scopeText.substr(9u), stats, payload));
    const auto converted = Telemetry::ParsePerfTimingPayload(payload.data(), payload.size());
    ASSERT_TRUE(converted);
    EXPECT_EQ(converted->scopeText, "scope inside output");
    ASSERT_TRUE(Telemetry::BuildPerfMemoryPayload(scopeName, converted->scopeText, snapshot, delta, payload));
    const auto expanded = Telemetry::ParsePerfMemoryPayload(payload.data(), payload.size());
    ASSERT_TRUE(expanded);
    EXPECT_EQ(expanded->scopeText, "scope inside output");
}

TEST(Telemetry, PerfTimingPayloadRejectsInvalidInput){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();

    EXPECT_FALSE(Telemetry::BuildPerfTimingPayload(Telemetry::PerfTimingSource::Unknown, Name(s_RendererFrame), s_RendererFrameText, stats, payload));

    stats.sampleCount = 0u;
    EXPECT_FALSE(Telemetry::BuildPerfTimingPayload(Telemetry::PerfTimingSource::Cpu, Name(s_RendererFrame), s_RendererFrameText, stats, payload));
}

TEST(Telemetry, PerfTimingRejectsNonfiniteDurationsAtBuildAndWireAdmission){
    using TimingStats = NWB::Core::Perf::TimingStats;
    using TimingField = f64 TimingStats::*;
    using WireField = f64 Telemetry::EncodedPerfTimingPayloadHeader::*;
    constexpr Array<TimingField, 4u> s_Fields = { &TimingStats::seconds, &TimingStats::minSeconds, &TimingStats::maxSeconds, &TimingStats::lastSeconds };
    constexpr Array<WireField, 4> s_WireFields = {
        &Telemetry::EncodedPerfTimingPayloadHeader::seconds,
        &Telemetry::EncodedPerfTimingPayloadHeader::minSeconds,
        &Telemetry::EncodedPerfTimingPayloadHeader::maxSeconds,
        &Telemetry::EncodedPerfTimingPayloadHeader::lastSeconds,
    };
    constexpr Array<u64, 3> s_NonfiniteBits = { 0x7FF0000000000000ull, 0xFFF0000000000000ull, 0x7FF8000000000001ull };
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    Telemetry::TelemetryBytes current(testArena.arena);
    const Name scopeName(s_RendererFrame);
    const TimingStats validStats = MakeTestTimingStats();
    ASSERT_TRUE(Telemetry::BuildPerfTimingPayload(Telemetry::PerfTimingSource::Cpu, scopeName, s_RendererFrameText, validStats, current));
    for(usize field = 0u; field < s_Fields.size(); ++field){
        for(const u64 bits : s_NonfiniteBits){
            SCOPED_TRACE(field);
            SCOPED_TRACE(bits);
            TimingStats invalid = validStats;
            invalid.*s_Fields[field] = BitCast<f64>(bits);
            EXPECT_FALSE(Telemetry::BuildPerfTimingPayload(Telemetry::PerfTimingSource::Cpu, scopeName, s_RendererFrameText, invalid, payload));
            payload = current;
            usize cursor = 0u;
            const auto readHeader = ReadPOD<Telemetry::EncodedPerfTimingPayloadHeader>(current, cursor);
            ASSERT_TRUE(readHeader);
            auto header = *readHeader;
            header.*s_WireFields[field] = BitCast<f64>(bits);
            NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
            EXPECT_FALSE(Telemetry::ParsePerfTimingPayload(payload.data(), payload.size()));
            const auto recovered = Telemetry::ParsePerfTimingPayload(current.data(), current.size());
            ASSERT_TRUE(recovered);
            EXPECT_EQ(recovered->stats.seconds, validStats.seconds);
        }
    }
}

TEST(Telemetry, GenericScopeHelpersPreserveBorrowedPointerValuesAndImplicitNameFailures){
    using NWB::Core::Perf::TimingScopeId;
    GenericScopeRecord record{ Name("perf/generic/scope"), 3u };
    const Array<BorrowedScopePointer, 1u> ownerStorage{ BorrowedScopePointer{ &record } };
    const Span<const BorrowedScopePointer> owners(ownerStorage);
    const TimingScopeId scope{ 0u, 3u };
    static_assert(IsSame_V<decltype(FindNamedScope<TimingScopeId>(owners, scope)), GenericScopeRecord*>);
    static_assert(noexcept(FindNamedScope<TimingScopeId>(owners, scope)));
    EXPECT_EQ(FindNamedScope<TimingScopeId>(owners, scope), &record);
    EXPECT_EQ(FindNamedScope<TimingScopeId>(owners, TimingScopeId{ 0u, 4u }), nullptr);
    EXPECT_EQ(FindNamedScope<TimingScopeId>(owners, TimingScopeId{ 1u, 3u }), nullptr);

    const Array<NotNull<GenericScopeRecord*>, 1u> recordStorage{ MakeNotNull(&record) };
    const Span<const NotNull<GenericScopeRecord*>> records(recordStorage);
    static_assert(noexcept(ScopeNameAt(records, 0u)));
    EXPECT_EQ(ScopeNameAt(records, 0u), record.name);

    ThrowingScopeRecord throwing{};
    const Array<NotNull<ThrowingScopeRecord*>, 1u> throwingRecordStorage{ MakeNotNull(&throwing) };
    const Span<const NotNull<ThrowingScopeRecord*>> throwingRecords(throwingRecordStorage);
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

