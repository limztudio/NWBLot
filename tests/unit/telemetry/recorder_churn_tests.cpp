// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"

#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_recorder_churn_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;
namespace Perf = NWB::Core::Perf;

void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char buffer[32u] = {};
    const AStringView formatted = FormatDecimal(value, buffer);
    buffer[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), buffer);
}

[[nodiscard]] bool AppendMemoryFrame(
    Telemetry::Recorder& recorder,
    const Name& scopeName,
    const u32 ownerCount,
    const u64 frameIndex
){
    Perf::MemorySnapshot snapshot{
        .scopeName = scopeName,
        .source = Perf::MemorySource::Arena,
        .frameIndex = frameIndex,
        .allocationCount = frameIndex + 1u,
        .deallocationCount = frameIndex,
    };
    const Perf::MemoryDelta delta{
        .previousFrameIndex = frameIndex == 0u ? 0u : frameIndex - 1u,
        .currentFrameIndex = frameIndex,
        .allocationCount = 1,
        .deallocationCount = 1,
        .hasSamples = frameIndex != 0u,
    };
    for(u32 ownerIndex = 0u; ownerIndex < ownerCount; ++ownerIndex){
        snapshot.reservedBytes = 256u + ownerIndex;
        snapshot.usedBytes = snapshot.reservedBytes;
        snapshot.peakUsedBytes = snapshot.usedBytes;
        if(!Telemetry::RecordPerfMemory(recorder, scopeName, "Recorder churn owner", snapshot, delta, ownerIndex))
            return false;
    }
    return recorder.eventCount() == ownerCount;
}

void RunRecorderChurn(const u32 ownerCount){
    constexpr u32 s_FrameCount = 120u;
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    const Name scopeName("tests/telemetry/recorder_churn_owner");
    ASSERT_TRUE(AppendMemoryFrame(recorder, scopeName, ownerCount, 0u));
    for(u32 ownerIndex = 0u; ownerIndex < ownerCount; ++ownerIndex){
        const auto* event = recorder.view().eventAt(ownerIndex);
        ASSERT_NE(event, nullptr);
        EXPECT_EQ(event->header.kind, Telemetry::EventKind::MemoryFrame);
        EXPECT_EQ(event->header.frameIndex, 0u);
        EXPECT_EQ(event->header.streamId, ownerIndex);
        Telemetry::PerfMemoryPayload parsed(testArena.arena);
        ASSERT_TRUE(Telemetry::ParsePerfMemoryPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
        EXPECT_EQ(parsed.scopeName, scopeName);
        EXPECT_EQ(parsed.snapshot.source, Perf::MemorySource::Arena);
        EXPECT_EQ(parsed.snapshot.usedBytes, 256u + ownerIndex);
    }
    recorder.clear();

    const ArenaMemoryStats before = testArena.arena.memoryStats();
    const Timer begin = TimerNow();
    bool succeeded = true;
    for(u32 frameIndex = 1u; frameIndex <= s_FrameCount; ++frameIndex){
        if(!AppendMemoryFrame(recorder, scopeName, ownerCount, frameIndex)){
            succeeded = false;
            break;
        }
        recorder.clear();
    }
    const u64 nanoseconds = DurationInNS<u64>(TimerNow(), begin);
    const ArenaMemoryStats after = testArena.arena.memoryStats();
    ASSERT_TRUE(succeeded);
    EXPECT_EQ(recorder.eventCount(), 0u);
    EXPECT_EQ(after.usedBytes, before.usedBytes);

    testing::Test::RecordProperty("owner_count", ownerCount);
    testing::Test::RecordProperty("frame_count", s_FrameCount);
    RecordUnsignedProperty(MakeNotNull("record_clear_ns"), nanoseconds);
    RecordUnsignedProperty(MakeNotNull("allocation_count"), after.allocationCount - before.allocationCount);
    RecordUnsignedProperty(MakeNotNull("deallocation_count"), after.deallocationCount - before.deallocationCount);
}

// Each frame mirrors automatic memory-event capture followed by successful-upload clear; opt in explicitly.
TEST(Telemetry, DISABLED_RecorderMemoryChurnBenchmark128Owners){
    RunRecorderChurn(128u);
}

TEST(Telemetry, DISABLED_RecorderMemoryChurnBenchmark512Owners){
    RunRecorderChurn(512u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

