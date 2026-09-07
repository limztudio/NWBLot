// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <core/common/name_symbols.h>
#include <core/perf/session.h>
#include <core/telemetry/session.h>
#include <logger/telemetry/report.h>

#include <global/atomic.h>
#include <global/text_utils.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_allocation_owner_telemetry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = NWB::Tests::TestArena<struct AllocationOwnerTelemetryTestsTag>;
namespace Core = NWB::Core;
namespace Perf = Core::Perf;
namespace Telemetry = Core::Telemetry;
namespace Log = NWB::Log;
static Atomic<u64> s_OwnerSequence{ 0u };


struct OwnedAllocation{
    Core::Alloc::GlobalArena& arena;
    void* pointer = nullptr;
    usize requestedBytes = 0u;

    OwnedAllocation(Core::Alloc::GlobalArena& owner, const usize size)
        : arena(owner)
        , pointer(owner.allocate(1u, size))
        , requestedBytes(size)
    {}
    ~OwnedAllocation(){
        if(pointer)
            arena.deallocate(pointer, 1u, requestedBytes);
    }
};


[[nodiscard]] static Perf::MemorySnapshot MakeSnapshot(const Name& name, const Perf::MemorySource::Enum source){
    Perf::MemorySnapshot snapshot;
    snapshot.scopeName = name;
    snapshot.source = source;
    snapshot.frameIndex = 88u;
    snapshot.reservedBytes = 4096u;
    snapshot.usedBytes = 1536u;
    snapshot.peakUsedBytes = 2048u;
    snapshot.allocationCount = 7u;
    snapshot.reallocationCount = 2u;
    snapshot.deallocationCount = 1u;
    return snapshot;
}

[[nodiscard]] static Perf::MemoryDelta MakeDelta(){
    Perf::MemoryDelta delta;
    delta.previousFrameIndex = 87u;
    delta.currentFrameIndex = 88u;
    delta.reservedBytes = 512;
    delta.usedBytes = -128;
    delta.peakUsedBytes = 256;
    delta.allocationCount = 2;
    delta.reallocationCount = 1;
    delta.deallocationCount = -1;
    delta.hasSamples = true;
    return delta;
}

static void ExpectSnapshot(const Perf::MemorySnapshot& actual, const Perf::MemorySnapshot& expected){
    EXPECT_EQ(actual.scopeName, expected.scopeName);
    EXPECT_EQ(actual.source, expected.source);
    EXPECT_EQ(actual.frameIndex, expected.frameIndex);
    EXPECT_EQ(actual.reservedBytes, expected.reservedBytes);
    EXPECT_EQ(actual.usedBytes, expected.usedBytes);
    EXPECT_EQ(actual.peakUsedBytes, expected.peakUsedBytes);
    EXPECT_EQ(actual.allocationCount, expected.allocationCount);
    EXPECT_EQ(actual.reallocationCount, expected.reallocationCount);
    EXPECT_EQ(actual.deallocationCount, expected.deallocationCount);
}

static void ExpectDelta(const Perf::MemoryDelta& actual, const Perf::MemoryDelta& expected){
    EXPECT_EQ(actual.hasSamples, expected.hasSamples);
    EXPECT_EQ(actual.previousFrameIndex, expected.previousFrameIndex);
    EXPECT_EQ(actual.currentFrameIndex, expected.currentFrameIndex);
    EXPECT_EQ(actual.reservedBytes, expected.reservedBytes);
    EXPECT_EQ(actual.usedBytes, expected.usedBytes);
    EXPECT_EQ(actual.peakUsedBytes, expected.peakUsedBytes);
    EXPECT_EQ(actual.allocationCount, expected.allocationCount);
    EXPECT_EQ(actual.reallocationCount, expected.reallocationCount);
    EXPECT_EQ(actual.deallocationCount, expected.deallocationCount);
}

static void ExpectEmptyPayload(const Telemetry::PerfMemoryPayload& parsed){
    EXPECT_EQ(parsed.scopeName, NAME_NONE);
    EXPECT_TRUE(parsed.scopeText.empty());
    ExpectSnapshot(parsed.snapshot, Perf::MemorySnapshot{});
    ExpectDelta(parsed.delta, Perf::MemoryDelta{});
}

[[nodiscard]] static const Telemetry::EventRecord* FindOwnerEvent(
    Telemetry::TelemetryArena& arena,
    const Telemetry::EventView& events,
    const Name& owner,
    const Perf::MemorySource::Enum source
){
    for(usize eventIndex = 0u; eventIndex < events.eventCount(); ++eventIndex){
        const Telemetry::EventRecord* const event = events.eventAt(eventIndex);
        if(!event || event->header.kind != Telemetry::EventKind::MemoryFrame)
            continue;
        Telemetry::PerfMemoryPayload parsed(arena);
        if(
            Telemetry::ParsePerfMemoryPayload(arena, event->payload.data(), event->payload.size(), parsed)
            && parsed.scopeName == owner
            && parsed.snapshot.source == source
        )
            return event;
    }
    return nullptr;
}

[[nodiscard]] static AStringView FindOwnerJsonRecord(const AStringView json, const Name& owner, const AStringView sourceField){
    char identityText[NameDetail::s_DebugHashTextLength + 1u] = {};
    NameDetail::HashToDebugString(owner.hash(), identityText, sizeof(identityText));
    usize cursor = json.find("\"memoryRecords\": [");
    while(cursor != AStringView::npos && cursor < json.size()){
        const usize nextLine = json.find('\n', cursor);
        const AStringView line = json.substr(cursor, nextLine == AStringView::npos ? json.size() - cursor : nextLine - cursor);
        if(line.find(identityText) != AStringView::npos && line.find(sourceField) != AStringView::npos)
            return line;
        if(line.find("    ]") != AStringView::npos || nextLine == AStringView::npos)
            break;
        cursor = nextLine + 1u;
    }
    return {};
}

template<typename Number>
static void ExpectJsonNumber(
    Core::Alloc::ScratchArena& scratchArena,
    const AStringView record,
    const AStringView field,
    const Number value
){
    AString<Core::Alloc::ScratchArena> expected(scratchArena);
    expected.reserve(field.size() + 32u);
    StringAppendFormat(expected, "\"{}\": {}", field, value);
    const usize position = record.find(AStringView(expected.data(), expected.size()));
    ASSERT_NE(position, AStringView::npos);
    const usize end = position + expected.size();
    EXPECT_TRUE(end == record.size() || record[end] == ',' || record[end] == '}' || record[end] == ' ');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AllocationOwnerTelemetry, VersionTwoRoundTripsAllSourcesBinaryIdentityAndDeltas){
    TestArena testArena;
    NameHash binaryHash{};
    for(u32 lane = 0u; lane < NameDetail::s_HashLaneCount; ++lane)
        binaryHash.qwords[lane] = 0x9876543210ABCDEFuLL + lane;
    const Name owner(binaryHash);
    constexpr AStringView s_DisplayName = "Owner \"A\"\n\\buffer";
    const Perf::MemorySource::Enum sources[] = {
        Perf::MemorySource::ExplicitScope, Perf::MemorySource::Arena, Perf::MemorySource::HeapBacking,
    };
    for(const Perf::MemorySource::Enum source : sources){
        const Perf::MemorySnapshot snapshot = MakeSnapshot(owner, source);
        for(const bool hasDelta : { true, false }){
            const Perf::MemoryDelta delta = hasDelta ? MakeDelta() : Perf::MemoryDelta{};
            Telemetry::TelemetryBytes bytes(testArena.arena);
            ASSERT_TRUE(Telemetry::BuildPerfMemoryPayload(testArena.arena, owner, s_DisplayName, snapshot, delta, bytes));
            ASSERT_EQ(bytes.size(), sizeof(Telemetry::EncodedPerfMemoryPayloadHeader) + s_DisplayName.size());
            Telemetry::EncodedPerfMemoryPayloadHeader header;
            NWB_MEMCPY(&header, sizeof(header), bytes.data(), sizeof(header));
            EXPECT_EQ(header.version, 2u);
            EXPECT_EQ(header.source, static_cast<u32>(source));
            EXPECT_EQ(header.scopeHash, binaryHash);
            Telemetry::PerfMemoryPayload parsed(testArena.arena);
            ASSERT_TRUE(Telemetry::ParsePerfMemoryPayload(testArena.arena, bytes.data(), bytes.size(), parsed));
            EXPECT_EQ(parsed.scopeName.hash(), binaryHash);
            EXPECT_EQ(AStringView(parsed.scopeText.data(), parsed.scopeText.size()), s_DisplayName);
            ExpectSnapshot(parsed.snapshot, snapshot);
            ExpectDelta(parsed.delta, delta);
        }
    }
}

TEST(AllocationOwnerTelemetry, ReadsVersionOneAndResetsOutputForMalformedMemoryPayloads){
    TestArena testArena;
    const Name owner("tests/telemetry/legacy_owner");
    const Perf::MemorySnapshot snapshot = MakeSnapshot(owner, Perf::MemorySource::ExplicitScope);
    const Perf::MemoryDelta delta = MakeDelta();
    Telemetry::TelemetryBytes bytes(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildPerfMemoryPayload(testArena.arena, owner, "Legacy Owner", snapshot, delta, bytes));
    Telemetry::EncodedPerfMemoryPayloadHeader validHeader;
    NWB_MEMCPY(&validHeader, sizeof(validHeader), bytes.data(), sizeof(validHeader));
    Telemetry::EncodedPerfMemoryPayloadHeader legacyHeader = validHeader;
    legacyHeader.version = 1u;
    legacyHeader.source = 0u;
    NWB_MEMCPY(bytes.data(), bytes.size(), &legacyHeader, sizeof(legacyHeader));
    Telemetry::PerfMemoryPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParsePerfMemoryPayload(testArena.arena, bytes.data(), bytes.size(), parsed));
    EXPECT_EQ(parsed.scopeName, owner);
    EXPECT_EQ(parsed.scopeText, "Legacy Owner");
    ExpectSnapshot(parsed.snapshot, snapshot);
    ExpectDelta(parsed.delta, delta);

    for(u32 invalidCase = 0u; invalidCase < 9u; ++invalidCase){
        NWB_MEMCPY(bytes.data(), bytes.size(), &validHeader, sizeof(validHeader));
        ASSERT_TRUE(Telemetry::ParsePerfMemoryPayload(testArena.arena, bytes.data(), bytes.size(), parsed));
        Telemetry::EncodedPerfMemoryPayloadHeader malformed = validHeader;
        switch(invalidCase){
        case 0u: malformed.version = 0u; break;
        case 1u: malformed.version = 3u; break;
        case 2u: malformed.version = 1u; malformed.source = Perf::MemorySource::Arena; break;
        case 3u: malformed.source = 3u; break;
        case 4u: malformed.source = Limit<u32>::s_Max; break;
        case 5u: malformed.flags = 0x8000u; break;
        case 6u: malformed.magic = 0u; break;
        case 7u: malformed.scopeHash = {}; break;
        case 8u: ++malformed.scopeNameBytes; break;
        }
        NWB_MEMCPY(bytes.data(), bytes.size(), &malformed, sizeof(malformed));
        EXPECT_FALSE(Telemetry::ParsePerfMemoryPayload(testArena.arena, bytes.data(), bytes.size(), parsed));
        ExpectEmptyPayload(parsed);
    }
    for(const usize truncatedSize : { sizeof(validHeader) - 1u, bytes.size() - 1u }){
        NWB_MEMCPY(bytes.data(), bytes.size(), &validHeader, sizeof(validHeader));
        ASSERT_TRUE(Telemetry::ParsePerfMemoryPayload(testArena.arena, bytes.data(), bytes.size(), parsed));
        EXPECT_FALSE(Telemetry::ParsePerfMemoryPayload(testArena.arena, bytes.data(), truncatedSize, parsed));
        ExpectEmptyPayload(parsed);
    }

    Perf::MemorySnapshot invalidSnapshot = snapshot;
    invalidSnapshot.source = static_cast<Perf::MemorySource::Enum>(255u);
    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(testArena.arena, owner, "Invalid Source", invalidSnapshot, delta, bytes));
    EXPECT_TRUE(bytes.empty());
}

TEST(AllocationOwnerTelemetry, SeparatesRecorderSourcesAndKeepsPreviousFrameDuringReplacement){
    TestArena testArena;
    Perf::MemoryRecorder recorder(testArena.arena);
    recorder.setEnabled(true);
    const Perf::MemoryView view(recorder);
    const Name owner("tests/telemetry/recorder_source_collision");
    const Perf::MemorySource::Enum sources[] = {
        Perf::MemorySource::ExplicitScope, Perf::MemorySource::Arena, Perf::MemorySource::HeapBacking,
    };
    Perf::MemoryScopeId scopes[LengthOf(sources)];
    Perf::MemorySnapshot previous[LengthOf(sources)];
    for(usize sourceIndex = 0u; sourceIndex < LengthOf(sources); ++sourceIndex){
        scopes[sourceIndex] = recorder.registerScope(owner, sources[sourceIndex]);
        ASSERT_TRUE(scopes[sourceIndex].valid());
        for(usize earlier = 0u; earlier < sourceIndex; ++earlier)
            EXPECT_NE(scopes[earlier].index, scopes[sourceIndex].index);
        ArenaMemoryStats stats;
        stats.reservedBytes = 1000u * (sourceIndex + 1u);
        stats.usedBytes = 100u * (sourceIndex + 1u);
        stats.peakUsedBytes = stats.usedBytes;
        stats.allocationCount = sourceIndex + 1u;
        recorder.recordSnapshot(owner, stats, 10u, sources[sourceIndex]);
        previous[sourceIndex] = view.snapshot(owner, sources[sourceIndex]);
        EXPECT_EQ(previous[sourceIndex].source, sources[sourceIndex]);
        EXPECT_EQ(previous[sourceIndex].usedBytes, stats.usedBytes);
        EXPECT_FALSE(view.delta(owner, sources[sourceIndex]).hasSamples);
        stats.usedBytes += 25u;
        stats.peakUsedBytes = stats.usedBytes;
        recorder.recordSnapshot(scopes[sourceIndex], stats, 11u);
        stats.usedBytes += 75u;
        stats.peakUsedBytes = stats.usedBytes;
        ++stats.reallocationCount;
        recorder.recordSnapshot(owner, stats, 11u, sources[sourceIndex]);
        const Perf::MemorySnapshot replacement = view.snapshot(scopes[sourceIndex]);
        EXPECT_EQ(replacement.usedBytes, stats.usedBytes);
        EXPECT_EQ(replacement.source, sources[sourceIndex]);
        ExpectSnapshot(view.snapshot(owner, sources[sourceIndex]), replacement);
        const Perf::MemoryDelta expectedDelta = Perf::Difference(replacement, previous[sourceIndex]);
        ExpectDelta(view.delta(scopes[sourceIndex]), expectedDelta);
        ExpectDelta(view.delta(owner, sources[sourceIndex]), expectedDelta);
        EXPECT_EQ(expectedDelta.previousFrameIndex, 10u);
        EXPECT_EQ(expectedDelta.currentFrameIndex, 11u);
        EXPECT_EQ(expectedDelta.usedBytes, 100);
    }
    for(usize sourceIndex = 0u; sourceIndex < LengthOf(sources); ++sourceIndex){
        const Perf::MemorySnapshot& snapshot = view.snapshot(owner, sources[sourceIndex]);
        EXPECT_EQ(snapshot.source, sources[sourceIndex]);
        EXPECT_EQ(snapshot.usedBytes, previous[sourceIndex].usedBytes + 100u);
    }
    ExpectSnapshot(view.snapshot(owner), view.snapshot(scopes[0u]));
    ExpectDelta(view.delta(owner), view.delta(scopes[0u]));
}

TEST(AllocationOwnerTelemetry, CapturesAutomaticAllocationOwnersAcrossEnableReallocationAndDestruction){
    TestArena testArena;
    char sequenceText[32u] = {};
    const AStringView sequence = FormatDecimal(s_OwnerSequence.fetch_add(1u, MemoryOrder::relaxed), sequenceText);
    const Name ownerName = DeriveName(Name("tests/telemetry/automatic_owner/"), sequence);
    const Name scratchName = DeriveName(Name("tests/telemetry/retired_scratch/"), sequence);
    Core::Alloc::GlobalArena owner(ownerName);
    OwnedAllocation allocation(owner, 53u);
    ASSERT_NE(allocation.pointer, nullptr);
    const u64 firstBytes = static_cast<u64>(Core::Alloc::CoreMsize(allocation.pointer));
    EXPECT_GE(firstBytes, allocation.requestedBytes);
    ArenaMemoryStats scratchLiveStats;
    {
        Core::Alloc::ScratchArena temporary(scratchName);
        ASSERT_NE(temporary.allocate(1u, 97u), nullptr);
        scratchLiveStats = temporary.memoryStats();
        EXPECT_GT(scratchLiveStats.usedBytes, 0u);
    }

    Perf::Session perfSession(testArena.arena);
    Telemetry::CaptureSession capture(testArena.arena);
    capture.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    perfSession.beginFrame(40u);
    perfSession.publishFrame();
    const Telemetry::PerfSessionRecordResult disabled = capture.recordPerfReport(perfSession.report(), 23u);
    EXPECT_TRUE(disabled.ok());
    EXPECT_EQ(disabled.memoryEvents, 0u);
    EXPECT_EQ(capture.eventCount(), 0u);
    EXPECT_FALSE(perfSession.memoryView().snapshot(ownerName, Perf::MemorySource::Arena).valid());

    perfSession.setCaptureOptions(Perf::CaptureOptions{ .enabled = true, .memory = true });
    perfSession.beginFrame(41u);
    perfSession.recordMemorySnapshot(ownerName, owner);
    perfSession.publishFrame();
    const Perf::MemorySnapshot first = perfSession.memoryView().snapshot(ownerName, Perf::MemorySource::Arena);
    ASSERT_TRUE(first.valid());
    EXPECT_EQ(first.source, Perf::MemorySource::Arena);
    EXPECT_EQ(first.frameIndex, 41u);
    EXPECT_EQ(first.usedBytes, firstBytes);
    EXPECT_EQ(first.reservedBytes, firstBytes);
    EXPECT_EQ(first.peakUsedBytes, firstBytes);
    EXPECT_EQ(first.allocationCount, 1u);
    EXPECT_EQ(first.reallocationCount, 0u);
    EXPECT_EQ(first.deallocationCount, 0u);
    EXPECT_FALSE(perfSession.memoryView().delta(ownerName, Perf::MemorySource::Arena).hasSamples);
    const Perf::MemorySnapshot explicitAlias = perfSession.memoryView().snapshot(ownerName);
    ASSERT_TRUE(explicitAlias.valid());
    EXPECT_EQ(explicitAlias.source, Perf::MemorySource::ExplicitScope);
    EXPECT_EQ(explicitAlias.usedBytes, first.usedBytes);
    EXPECT_EQ(explicitAlias.frameIndex, first.frameIndex);
    const Perf::MemorySnapshot scratch = perfSession.memoryView().snapshot(scratchName, Perf::MemorySource::Arena);
    ASSERT_TRUE(scratch.valid());
    EXPECT_EQ(scratch.reservedBytes, 0u);
    EXPECT_EQ(scratch.usedBytes, 0u);
    EXPECT_EQ(scratch.peakUsedBytes, scratchLiveStats.peakUsedBytes);
    EXPECT_EQ(scratch.allocationCount, scratchLiveStats.allocationCount);
    EXPECT_EQ(scratch.reallocationCount, scratchLiveStats.reallocationCount);
    EXPECT_EQ(scratch.deallocationCount, scratchLiveStats.allocationCount);
    capture.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
    const Telemetry::PerfSessionRecordResult filtered = capture.recordPerfReport(perfSession.report(), 23u);
    EXPECT_FALSE(filtered.ok());
    EXPECT_EQ(filtered.memoryEvents, 0u);
    EXPECT_EQ(capture.eventCount(), 0u);
    capture.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    const Telemetry::PerfSessionRecordResult firstRecorded = capture.recordPerfReport(perfSession.report(), 23u);
    ASSERT_TRUE(firstRecorded.ok());
    EXPECT_GT(firstRecorded.memoryEvents, 0u);
    const Telemetry::EventRecord* const firstEvent = FindOwnerEvent(testArena.arena, capture.view(), ownerName, Perf::MemorySource::Arena);
    ASSERT_NE(firstEvent, nullptr);
    EXPECT_NE(FindOwnerEvent(testArena.arena, capture.view(), ownerName, Perf::MemorySource::ExplicitScope), nullptr);
    EXPECT_EQ(firstEvent->header.frameIndex, 41u);
    EXPECT_EQ(firstEvent->header.streamId, 23u);
    Telemetry::PerfMemoryPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParsePerfMemoryPayload(testArena.arena, firstEvent->payload.data(), firstEvent->payload.size(), parsed));
    ExpectSnapshot(parsed.snapshot, first);
    EXPECT_FALSE(parsed.delta.hasSamples);
    EXPECT_EQ(AStringView(parsed.scopeText.data(), parsed.scopeText.size()), AStringView(ownerName.c_str()));

    void* const resized = owner.reallocate(allocation.pointer, 1u, 101u);
    ASSERT_NE(resized, nullptr);
    allocation.pointer = resized;
    allocation.requestedBytes = 101u;
    const u64 resizedBytes = static_cast<u64>(Core::Alloc::CoreMsize(resized));
    perfSession.beginFrame(42u);
    perfSession.publishFrame();
    const Perf::MemorySnapshot second = perfSession.memoryView().snapshot(ownerName, Perf::MemorySource::Arena);
    EXPECT_EQ(second.usedBytes, resizedBytes);
    EXPECT_EQ(second.reservedBytes, resizedBytes);
    EXPECT_EQ(second.peakUsedBytes, Max(firstBytes, resizedBytes));
    EXPECT_EQ(second.allocationCount, 1u);
    EXPECT_EQ(second.reallocationCount, 1u);
    EXPECT_EQ(second.deallocationCount, 0u);
    ExpectDelta(perfSession.memoryView().delta(ownerName, Perf::MemorySource::Arena), Perf::Difference(second, first));

    owner.deallocate(allocation.pointer, 1u, allocation.requestedBytes);
    allocation.pointer = nullptr;
    perfSession.beginFrame(43u);
    perfSession.publishFrame();
    const Perf::MemorySnapshot retired = perfSession.memoryView().snapshot(ownerName, Perf::MemorySource::Arena);
    const Perf::MemoryDelta retiredDelta = perfSession.memoryView().delta(ownerName, Perf::MemorySource::Arena);
    EXPECT_EQ(retired.usedBytes, 0u);
    EXPECT_EQ(retired.reservedBytes, 0u);
    EXPECT_EQ(retired.peakUsedBytes, second.peakUsedBytes);
    EXPECT_EQ(retired.allocationCount, 1u);
    EXPECT_EQ(retired.reallocationCount, 1u);
    EXPECT_EQ(retired.deallocationCount, 1u);
    ExpectDelta(retiredDelta, Perf::Difference(retired, second));
    EXPECT_EQ(retiredDelta.usedBytes, -static_cast<i64>(resizedBytes));
    capture.clear();
    const Telemetry::PerfSessionRecordResult retiredRecorded = capture.recordPerfReport(perfSession.report(), 23u);
    ASSERT_TRUE(retiredRecorded.ok());
    const Telemetry::EventRecord* const retiredEvent = FindOwnerEvent(testArena.arena, capture.view(), ownerName, Perf::MemorySource::Arena);
    ASSERT_NE(retiredEvent, nullptr);
    ASSERT_TRUE(Telemetry::ParsePerfMemoryPayload(testArena.arena, retiredEvent->payload.data(), retiredEvent->payload.size(), parsed));
    ExpectSnapshot(parsed.snapshot, retired);
    ExpectDelta(parsed.delta, retiredDelta);
    EXPECT_NE(FindOwnerEvent(testArena.arena, capture.view(), scratchName, Perf::MemorySource::Arena), nullptr);
    const Name heapName("core/alloc/heap_backing");
    EXPECT_NE(FindOwnerEvent(testArena.arena, capture.view(), heapName, Perf::MemorySource::HeapBacking), nullptr);

    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, capture.view(), report));
    EXPECT_EQ(report.summary.parseFailureCount, 0u);
    const AStringView json(report.json.data(), report.json.size());
    const AStringView ownerRecord = FindOwnerJsonRecord(json, ownerName, "\"source\": \"arena\"");
    ASSERT_FALSE(ownerRecord.empty());
    EXPECT_NE(ownerRecord.find(ownerName.c_str()), AStringView::npos);
    const usize deltaBegin = ownerRecord.find("\"delta\": {");
    ASSERT_NE(deltaBegin, AStringView::npos);
    const AStringView ownerFields = ownerRecord.substr(0u, deltaBegin);
    const AStringView deltaFields = ownerRecord.substr(deltaBegin);
    Core::Alloc::ScratchArena scratchArena(Name("tests/telemetry/json_checks"));
    ExpectJsonNumber(scratchArena, ownerFields, "frameIndex", 43u);
    ExpectJsonNumber(scratchArena, ownerFields, "streamId", 23u);
    ExpectJsonNumber(scratchArena, ownerFields, "reservedBytes", retired.reservedBytes);
    ExpectJsonNumber(scratchArena, ownerFields, "usedBytes", retired.usedBytes);
    ExpectJsonNumber(scratchArena, ownerFields, "peakUsedBytes", retired.peakUsedBytes);
    ExpectJsonNumber(scratchArena, ownerFields, "allocationCount", retired.allocationCount);
    ExpectJsonNumber(scratchArena, ownerFields, "reallocationCount", retired.reallocationCount);
    ExpectJsonNumber(scratchArena, ownerFields, "deallocationCount", retired.deallocationCount);
    ExpectJsonNumber(scratchArena, deltaFields, "previousFrameIndex", 42u);
    ExpectJsonNumber(scratchArena, deltaFields, "reservedBytes", retiredDelta.reservedBytes);
    ExpectJsonNumber(scratchArena, deltaFields, "usedBytes", retiredDelta.usedBytes);
    ExpectJsonNumber(scratchArena, deltaFields, "peakUsedBytes", retiredDelta.peakUsedBytes);
    ExpectJsonNumber(scratchArena, deltaFields, "allocationCount", retiredDelta.allocationCount);
    ExpectJsonNumber(scratchArena, deltaFields, "reallocationCount", retiredDelta.reallocationCount);
    ExpectJsonNumber(scratchArena, deltaFields, "deallocationCount", retiredDelta.deallocationCount);
    EXPECT_FALSE(FindOwnerJsonRecord(json, scratchName, "\"source\": \"arena\"").empty());
    EXPECT_FALSE(FindOwnerJsonRecord(json, heapName, "\"source\": \"heapBacking\"").empty());
}

TEST(AllocationOwnerTelemetry, SharedNameSumsLiveUsageAndPreservesLargestIndividualArenaPeak){
    TestArena testArena;
    char sequenceText[32u] = {};
    const Name ownerName = DeriveName(
        Name("tests/telemetry/shared_arena_peak/"),
        FormatDecimal(s_OwnerSequence.fetch_add(1u, MemoryOrder::relaxed), sequenceText)
    );
    Perf::Session perfSession(testArena.arena);
    perfSession.setCaptureOptions(Perf::CaptureOptions{ .enabled = true, .memory = true });
    Core::Alloc::GlobalArena firstArena(ownerName);
    OwnedAllocation firstAllocation(firstArena, 53u);
    ASSERT_NE(firstAllocation.pointer, nullptr);
    const u64 firstBytes = static_cast<u64>(Core::Alloc::CoreMsize(firstAllocation.pointer));
    u64 secondBytes = 0u;
    {
        Core::Alloc::GlobalArena secondArena(ownerName);
        OwnedAllocation secondAllocation(secondArena, 101u);
        ASSERT_NE(secondAllocation.pointer, nullptr);
        secondBytes = static_cast<u64>(Core::Alloc::CoreMsize(secondAllocation.pointer));
        perfSession.beginFrame(71u);
        perfSession.publishFrame();
        const Perf::MemorySnapshot both = perfSession.memoryView().snapshot(ownerName, Perf::MemorySource::Arena);
        ASSERT_TRUE(both.valid());
        EXPECT_EQ(both.usedBytes, firstBytes + secondBytes);
        EXPECT_EQ(both.reservedBytes, firstBytes + secondBytes);
        EXPECT_EQ(both.peakUsedBytes, Max(firstBytes, secondBytes));
        EXPECT_LT(both.peakUsedBytes, both.usedBytes);
        EXPECT_EQ(both.allocationCount, 2u);
        EXPECT_EQ(both.deallocationCount, 0u);
    }

    perfSession.beginFrame(72u);
    perfSession.publishFrame();
    const Perf::MemorySnapshot surviving = perfSession.memoryView().snapshot(ownerName, Perf::MemorySource::Arena);
    ASSERT_TRUE(surviving.valid());
    EXPECT_EQ(surviving.usedBytes, firstBytes);
    EXPECT_EQ(surviving.reservedBytes, firstBytes);
    EXPECT_EQ(surviving.peakUsedBytes, Max(firstBytes, secondBytes));
    EXPECT_EQ(surviving.allocationCount, 2u);
    EXPECT_EQ(surviving.deallocationCount, 1u);
    const Perf::MemoryDelta delta = perfSession.memoryView().delta(ownerName, Perf::MemorySource::Arena);
    EXPECT_TRUE(delta.hasSamples);
    EXPECT_EQ(delta.usedBytes, -static_cast<i64>(secondBytes));
    EXPECT_EQ(delta.peakUsedBytes, 0);

    Telemetry::CaptureSession capture(testArena.arena);
    capture.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    const Telemetry::PerfSessionRecordResult recorded = capture.recordPerfReport(perfSession.report(), 17u);
    ASSERT_TRUE(recorded.ok());
    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, capture.view(), report));
    EXPECT_EQ(report.summary.parseFailureCount, 0u);
    const AStringView json(report.json.data(), report.json.size());
    const AStringView record = FindOwnerJsonRecord(json, ownerName, "\"source\": \"arena\"");
    ASSERT_FALSE(record.empty());
    EXPECT_NE(record.find("\"peakBasis\": \"largestArena\""), AStringView::npos);
    Core::Alloc::ScratchArena scratchArena(Name("tests/telemetry/shared_arena_peak_json_checks"));
    ExpectJsonNumber(scratchArena, record, "usedBytes", firstBytes);
    ExpectJsonNumber(scratchArena, record, "peakUsedBytes", Max(firstBytes, secondBytes));
}

TEST(AllocationOwnerTelemetry, KeepsHeapBackingRecordsWithoutCountingTheirUsageAgain){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    const Name owner("tests/telemetry/shared_source_identity");
    const Perf::MemorySource::Enum sources[] = {
        Perf::MemorySource::ExplicitScope, Perf::MemorySource::Arena, Perf::MemorySource::HeapBacking,
    };
    for(usize sourceIndex = 0u; sourceIndex < LengthOf(sources); ++sourceIndex){
        Perf::MemorySnapshot snapshot = MakeSnapshot(owner, sources[sourceIndex]);
        // The explicit scope aliases the same arena; their values must remain separate in summaries.
        snapshot.usedBytes = sources[sourceIndex] == Perf::MemorySource::HeapBacking ? 300u : 100u;
        snapshot.peakUsedBytes = sources[sourceIndex] == Perf::MemorySource::HeapBacking ? 3000u : 1000u;
        Perf::MemoryDelta delta = MakeDelta();
        delta.usedBytes = sources[sourceIndex] == Perf::MemorySource::HeapBacking ? 30 : 10;
        ASSERT_TRUE(Telemetry::RecordPerfMemory(recorder, owner, "Shared Owner", snapshot, delta, 7u));
    }
    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));
    EXPECT_EQ(report.summary.parseFailureCount, 0u);
    EXPECT_EQ(report.summary.memoryEventCount, 3u);
    EXPECT_EQ(report.summary.maxMemoryUsedBytes, 100u);
    EXPECT_EQ(report.summary.maxMemoryPeakUsedBytes, 1000u);
    EXPECT_EQ(report.summary.totalMemoryUsedDeltaBytes, 10);
    for(const Perf::MemorySource::Enum source : sources){
        const Log::TelemetryMemorySummary& summary = report.summary.memorySources[source];
        EXPECT_EQ(summary.eventCount, 1u);
        EXPECT_EQ(summary.maxUsedBytes, source == Perf::MemorySource::HeapBacking ? 300u : 100u);
        EXPECT_EQ(summary.maxPeakUsedBytes, source == Perf::MemorySource::HeapBacking ? 3000u : 1000u);
        EXPECT_EQ(summary.totalUsedDeltaBytes, source == Perf::MemorySource::HeapBacking ? 30 : 10);
    }
    const AStringView json(report.json.data(), report.json.size());
    EXPECT_NE(json.find("\"memorySources\": {"), AStringView::npos);
    EXPECT_FALSE(FindOwnerJsonRecord(json, owner, "\"source\": \"explicitScope\"").empty());
    EXPECT_FALSE(FindOwnerJsonRecord(json, owner, "\"source\": \"arena\"").empty());
    const AStringView heapRecord = FindOwnerJsonRecord(json, owner, "\"source\": \"heapBacking\"");
    ASSERT_FALSE(heapRecord.empty());
    EXPECT_NE(heapRecord.find("\"scope\": \"Shared Owner\""), AStringView::npos);
    Core::Alloc::ScratchArena scratchArena(Name("tests/telemetry/heap_json_checks"));
    struct PeakExpectation{
        AStringView recordSource;
        AStringView summarySource;
        AStringView basis;
    };
    constexpr PeakExpectation s_PeakExpectations[] = {
        { "\"source\": \"explicitScope\"", "\"explicitScope\": {", "\"peakBasis\": \"scope\"" },
        { "\"source\": \"arena\"", "\"arena\": {", "\"peakBasis\": \"largestArena\"" },
        { "\"source\": \"heapBacking\"", "\"heapBacking\": {", "\"peakBasis\": \"sampledHeap\"" },
    };
    for(const PeakExpectation& expectation : s_PeakExpectations){
        const AStringView record = FindOwnerJsonRecord(json, owner, expectation.recordSource);
        ASSERT_FALSE(record.empty());
        EXPECT_NE(record.find(expectation.basis), AStringView::npos);
        const usize summaryBegin = json.find(expectation.summarySource);
        ASSERT_NE(summaryBegin, AStringView::npos);
        const usize summaryEnd = json.find('}', summaryBegin);
        ASSERT_NE(summaryEnd, AStringView::npos);
        const AStringView summaryRecord = json.substr(summaryBegin, summaryEnd + 1u - summaryBegin);
        EXPECT_NE(summaryRecord.find(expectation.basis), AStringView::npos);
    }
    ExpectJsonNumber(scratchArena, heapRecord, "usedBytes", 300u);
    ExpectJsonNumber(scratchArena, heapRecord, "peakUsedBytes", 3000u);
    ExpectJsonNumber(scratchArena, heapRecord, "frameIndex", 88u);
    ExpectJsonNumber(scratchArena, heapRecord, "streamId", 7u);
}

TEST(AllocationOwnerTelemetry, ResolvesLoadedOwnerSymbolsByFullIdentityAndPreservesExplicitText){
    TestArena testArena;
    namespace NameSymbols = Core::Common::NameSymbols;
    NameSymbols::InstallRuntimeRegistry();
    NameSymbols::ClearRuntimeSymbols();
    NameHash firstHash{};
    for(u32 lane = 0u; lane < NameDetail::s_HashLaneCount; ++lane)
        firstHash.qwords[lane] = 0xBD876543210ACDEFuLL + lane;
    NameHash secondHash = firstHash;
    ++secondHash.qwords[NameDetail::s_HashLaneCount - 1u];
    NameHash unknownHash = secondHash;
    ++unknownHash.qwords[NameDetail::s_HashLaneCount - 1u];
    const Name firstOwner(firstHash);
    const Name secondOwner(secondHash);
    const Name unknownOwner(unknownHash);
    char firstHashText[NameDetail::s_DebugHashTextLength + 1u] = {};
    char secondHashText[NameDetail::s_DebugHashTextLength + 1u] = {};
    char unknownHashText[NameDetail::s_DebugHashTextLength + 1u] = {};
    NameDetail::HashToDebugString(firstHash, firstHashText, sizeof(firstHashText));
    NameDetail::HashToDebugString(secondHash, secondHashText, sizeof(secondHashText));
    NameDetail::HashToDebugString(unknownHash, unknownHashText, sizeof(unknownHashText));

    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    // Exact hash fallback text reproduces an opt/fin producer that has no local symbol sidecar loaded.
    ASSERT_TRUE(Telemetry::RecordPerfMemory(
        recorder, firstOwner, firstHashText, MakeSnapshot(firstOwner, Perf::MemorySource::Arena), MakeDelta(), 91u
    ));
    ASSERT_TRUE(Telemetry::RecordPerfMemory(
        recorder, secondOwner, secondHashText, MakeSnapshot(secondOwner, Perf::MemorySource::Arena), MakeDelta(), 92u
    ));
    ASSERT_TRUE(Telemetry::RecordPerfMemory(
        recorder, firstOwner, "Producer Display Name", MakeSnapshot(firstOwner, Perf::MemorySource::ExplicitScope), MakeDelta(), 93u
    ));
    ASSERT_TRUE(Telemetry::RecordPerfMemory(
        recorder, unknownOwner, unknownHashText, MakeSnapshot(unknownOwner, Perf::MemorySource::HeapBacking), MakeDelta(), 94u
    ));
    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));
    const AStringView beforeJson(report.json.data(), report.json.size());
    const AStringView beforeRecord = FindOwnerJsonRecord(beforeJson, firstOwner, "\"source\": \"arena\"");
    ASSERT_FALSE(beforeRecord.empty());
    EXPECT_EQ(NWB::Tests::CountText(beforeRecord, firstHashText), 2u);

    AString<Core::Alloc::GlobalArena> namesymText(testArena.arena);
    StringAppendFormat(
        namesymText,
        "{}\tproducer=runtime\n{}\truntime\tOwners/Loaded First\n{}\truntime\tOwners/Loaded Second\n",
        NameSymbols::s_FileHeader, firstHashText, secondHashText
    );
    ASSERT_TRUE(NameSymbols::LoadFromMemory(AStringView(namesymText.data(), namesymText.size())));
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));
    EXPECT_EQ(report.summary.parseFailureCount, 0u);
    const AStringView json(report.json.data(), report.json.size());
    const AStringView firstRecord = FindOwnerJsonRecord(json, firstOwner, "\"source\": \"arena\"");
    const AStringView secondRecord = FindOwnerJsonRecord(json, secondOwner, "\"source\": \"arena\"");
    const AStringView explicitRecord = FindOwnerJsonRecord(json, firstOwner, "\"source\": \"explicitScope\"");
    const AStringView unknownRecord = FindOwnerJsonRecord(json, unknownOwner, "\"source\": \"heapBacking\"");
    ASSERT_FALSE(firstRecord.empty());
    ASSERT_FALSE(secondRecord.empty());
    ASSERT_FALSE(explicitRecord.empty());
    ASSERT_FALSE(unknownRecord.empty());
    EXPECT_NE(firstRecord.find("\"scope\": \"owners/loaded first\""), AStringView::npos);
    EXPECT_NE(secondRecord.find("\"scope\": \"owners/loaded second\""), AStringView::npos);
    EXPECT_EQ(NWB::Tests::CountText(firstRecord, firstHashText), 1u);
    EXPECT_EQ(NWB::Tests::CountText(secondRecord, secondHashText), 1u);
    EXPECT_NE(explicitRecord.find("\"scope\": \"Producer Display Name\""), AStringView::npos);
    EXPECT_EQ(NWB::Tests::CountText(unknownRecord, unknownHashText), 2u);
}

TEST(AllocationOwnerTelemetry, ReportsRawOnlyHeapBackingInItsOwnSummaryDomain){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    const Name owner("core/alloc/heap_backing");
    const Perf::MemorySnapshot snapshot = MakeSnapshot(owner, Perf::MemorySource::HeapBacking);
    const Perf::MemoryDelta delta = MakeDelta();
    ASSERT_TRUE(Telemetry::RecordPerfMemory(recorder, owner, "Heap Backing", snapshot, delta, 9u));
    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));
    EXPECT_EQ(report.summary.parseFailureCount, 0u);
    EXPECT_EQ(report.summary.memoryEventCount, 1u);
    EXPECT_EQ(report.summary.maxMemoryUsedBytes, 0u);
    EXPECT_EQ(report.summary.maxMemoryPeakUsedBytes, 0u);
    EXPECT_EQ(report.summary.totalMemoryUsedDeltaBytes, 0);
    EXPECT_EQ(report.summary.memorySources[Perf::MemorySource::ExplicitScope].eventCount, 0u);
    EXPECT_EQ(report.summary.memorySources[Perf::MemorySource::Arena].eventCount, 0u);
    const Log::TelemetryMemorySummary& heap = report.summary.memorySources[Perf::MemorySource::HeapBacking];
    EXPECT_EQ(heap.eventCount, 1u);
    EXPECT_EQ(heap.maxUsedBytes, snapshot.usedBytes);
    EXPECT_EQ(heap.maxPeakUsedBytes, snapshot.peakUsedBytes);
    EXPECT_EQ(heap.totalUsedDeltaBytes, delta.usedBytes);
    const AStringView json(report.json.data(), report.json.size());
    const usize summaryBegin = json.find("\"heapBacking\": {");
    ASSERT_NE(summaryBegin, AStringView::npos);
    const usize summaryEnd = json.find('}', summaryBegin);
    ASSERT_NE(summaryEnd, AStringView::npos);
    const AStringView heapSummary = json.substr(summaryBegin, summaryEnd + 1u - summaryBegin);
    EXPECT_NE(heapSummary.find("\"peakBasis\": \"sampledHeap\""), AStringView::npos);
    Core::Alloc::ScratchArena scratchArena(Name("tests/telemetry/raw_heap_json_checks"));
    ExpectJsonNumber(scratchArena, heapSummary, "eventCount", 1u);
    ExpectJsonNumber(scratchArena, heapSummary, "maxUsedBytes", snapshot.usedBytes);
    ExpectJsonNumber(scratchArena, heapSummary, "maxPeakUsedBytes", snapshot.peakUsedBytes);
    ExpectJsonNumber(scratchArena, heapSummary, "totalUsedDeltaBytes", delta.usedBytes);
    EXPECT_FALSE(FindOwnerJsonRecord(json, owner, "\"source\": \"heapBacking\"").empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

