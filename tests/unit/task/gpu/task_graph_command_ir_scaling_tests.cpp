// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_command_ir_test_utils.h"

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_command_ir_scaling_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;

void RecordUnsignedProperty(const char* key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key, text);
}

[[nodiscard]] bool AppendMixedRecords(Graphics::GpuCommandIrCapture& capture, const usize begin, const usize end){
    for(usize index = begin; index < end; ++index){
        const Graphics::GpuTaskId task{ static_cast<u32>(index), s_CommandIrTask.generation };
        const Graphics::GpuSubmissionPacketId packet{ static_cast<u32>(index / 64u), s_CommandIrPacket.generation };
        if(index % 2u == 0u){
            if(!capture.captureCopyBuffer(
                task, packet, s_CommandIrQueue, s_CommandIrSource, index * 16u, s_CommandIrDestination, index * 32u, 64u
            ))
                return false;
        }
        else if(!capture.captureClearBuffer(task, packet, s_CommandIrQueue, s_CommandIrDestination, static_cast<u32>(index)))
            return false;
    }
    return true;
}

void VerifyMixedRecords(const Graphics::GpuCommandIrCapture& capture, const usize count){
    ASSERT_EQ(capture.recordCount(), count);
    Graphics::GpuCommandIrStreamReader reader(capture.commandBytes());
    EXPECT_EQ(reader.recordCount(), count);
    EXPECT_EQ(reader.graphGeneration(), count == 0u ? 0u : s_CommandIrTask.generation);
    EXPECT_EQ(reader.planGeneration(), count == 0u ? 0u : s_CommandIrPacket.generation);
    Graphics::GpuCommandIrBuiltinTaskRecord decoded;
    for(usize index = 0u; index < count; ++index){
        ASSERT_EQ(reader.next(decoded), Graphics::GpuCommandIrStreamReadStatus::Record);
        const auto* inspected = capture.recordAt(index);
        ASSERT_NE(inspected, nullptr);
        EXPECT_EQ(decoded.task, inspected->task);
        EXPECT_EQ(decoded.task.index, index);
        EXPECT_EQ(decoded.packet.index, index / 64u);
        EXPECT_EQ(decoded.queue, s_CommandIrQueue);
        EXPECT_EQ(decoded.destination, s_CommandIrDestination);
        EXPECT_EQ(decoded.opcode, inspected->opcode);
        if(index % 2u == 0u){
            EXPECT_EQ(decoded.opcode, Graphics::GpuCommandIrOpcode::CopyBuffer);
            EXPECT_EQ(decoded.source, s_CommandIrSource);
            EXPECT_EQ(decoded.sourceOffsetBytes, index * 16u);
            EXPECT_EQ(decoded.destinationOffsetBytes, index * 32u);
            EXPECT_EQ(decoded.dataSizeBytes, 64u);
        }
        else{
            EXPECT_EQ(decoded.opcode, Graphics::GpuCommandIrOpcode::ClearBuffer);
            EXPECT_EQ(decoded.uintClearValue.r, index);
        }
    }
    EXPECT_EQ(reader.next(decoded), Graphics::GpuCommandIrStreamReadStatus::End);
    EXPECT_TRUE(reader.validation().valid());
    EXPECT_EQ(capture.recordAt(count), nullptr);
}

void BenchmarkCapture(const usize count){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    ASSERT_TRUE(capture.beginRecordingAttempt(31u));
    const ArenaMemoryStats initial = testArena.arena.memoryStats();
    Timer begin = TimerNow();
    const bool captured = AppendMixedRecords(capture, 0u, count);
    const u64 captureNanoseconds = DurationInNS<u64>(TimerNow(), begin);
    ASSERT_TRUE(captured);
    const ArenaMemoryStats firstCapture = testArena.arena.memoryStats();
    const usize encodedBytes = capture.commandBytes().size();
    VerifyMixedRecords(capture, count);

    begin = TimerNow();
    capture.rollback(count / 2u);
    const u64 rollbackNanoseconds = DurationInNS<u64>(TimerNow(), begin);
    VerifyMixedRecords(capture, count / 2u);
    begin = TimerNow();
    const bool refilled = AppendMixedRecords(capture, count / 2u, count);
    const u64 refillNanoseconds = DurationInNS<u64>(TimerNow(), begin);
    ASSERT_TRUE(refilled);
    VerifyMixedRecords(capture, count);

    begin = TimerNow();
    capture.reset();
    const u64 resetNanoseconds = DurationInNS<u64>(TimerNow(), begin);
    VerifyMixedRecords(capture, 0u);
    ASSERT_TRUE(capture.beginRecordingAttempt(32u));
    begin = TimerNow();
    const bool reused = AppendMixedRecords(capture, 0u, count);
    const u64 reuseNanoseconds = DurationInNS<u64>(TimerNow(), begin);
    ASSERT_TRUE(reused);
    const ArenaMemoryStats after = testArena.arena.memoryStats();
    VerifyMixedRecords(capture, count);
    EXPECT_EQ(capture.recordingAttemptGeneration(), 32u);
    EXPECT_EQ(after.allocationCount, firstCapture.allocationCount);
    EXPECT_EQ(after.usedBytes, firstCapture.usedBytes);

    RecordUnsignedProperty("record_count", count);
    RecordUnsignedProperty("encoded_bytes", encodedBytes);
    RecordUnsignedProperty("capture_ns", captureNanoseconds);
    RecordUnsignedProperty("rollback_ns", rollbackNanoseconds);
    RecordUnsignedProperty("refill_ns", refillNanoseconds);
    RecordUnsignedProperty("reset_ns", resetNanoseconds);
    RecordUnsignedProperty("reused_capture_ns", reuseNanoseconds);
    RecordUnsignedProperty("capture_allocations", firstCapture.allocationCount - initial.allocationCount);
    RecordUnsignedProperty("reuse_allocations", after.allocationCount - firstCapture.allocationCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuCommandIrCapture, MixedRecordGrowthRollbackAndRefillPreserveTheExactPrefix){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    ASSERT_TRUE(capture.beginRecordingAttempt(31u));
    ASSERT_TRUE(AppendMixedRecords(capture, 0u, 257u));
    Graphics::GraphicsBytes original(testArena.arena);
    CopyCommandIrBytes(original, capture.commandBytes());
    VerifyMixedRecords(capture, 257u);

    capture.rollback(129u);
    VerifyMixedRecords(capture, 129u);
    EXPECT_EQ(capture.recordingAttemptGeneration(), 31u);
    const BinaryByteView prefix = capture.commandBytes();
    const usize headerSize = sizeof(Graphics::GpuCommandIrStreamHeader);
    EXPECT_EQ(NWB_MEMCMP(prefix.data() + headerSize, original.data() + headerSize, prefix.size() - headerSize), 0);
    const usize prefixBytes = prefix.size();
    capture.rollback(130u);
    EXPECT_EQ(capture.commandBytes().size(), prefixBytes);
    VerifyMixedRecords(capture, 129u);
    ASSERT_TRUE(AppendMixedRecords(capture, 129u, 257u));
    ASSERT_EQ(capture.commandBytes().size(), original.size());
    EXPECT_EQ(NWB_MEMCMP(capture.commandBytes().data(), original.data(), original.size()), 0);
    VerifyMixedRecords(capture, 257u);
}

TEST(GpuCommandIrCapture, ResetAndRollbackReuseCapacityAcrossRecordingAttempts){
    TestArena testArena;
    const ArenaMemoryStats initial = testArena.arena.memoryStats();
    {
        Graphics::GpuCommandIrCapture capture(testArena.arena);
        ASSERT_TRUE(capture.beginRecordingAttempt(31u));
        ASSERT_TRUE(AppendMixedRecords(capture, 0u, 512u));
        const ArenaMemoryStats warmed = testArena.arena.memoryStats();
        for(u64 attempt = 32u; attempt < 40u; ++attempt){
            capture.rollback(255u);
            ASSERT_TRUE(AppendMixedRecords(capture, 255u, 512u));
            capture.reset();
            EXPECT_EQ(capture.graphGeneration(), 0u);
            EXPECT_EQ(capture.planGeneration(), 0u);
            EXPECT_EQ(capture.recordingAttemptGeneration(), 0u);
            EXPECT_EQ(capture.commandBytes().size(), sizeof(Graphics::GpuCommandIrStreamHeader));
            ASSERT_TRUE(capture.beginRecordingAttempt(attempt));
            ASSERT_TRUE(AppendMixedRecords(capture, 0u, 512u));
            EXPECT_EQ(capture.recordingAttemptGeneration(), attempt);
        }
        const ArenaMemoryStats after = testArena.arena.memoryStats();
        EXPECT_EQ(after.allocationCount, warmed.allocationCount);
        EXPECT_EQ(after.reallocationCount, warmed.reallocationCount);
        EXPECT_EQ(after.deallocationCount, warmed.deallocationCount);
        EXPECT_EQ(after.usedBytes, warmed.usedBytes);
        VerifyMixedRecords(capture, 512u);
        capture.rollback(0u);
        VerifyMixedRecords(capture, 0u);
        EXPECT_EQ(capture.recordingAttemptGeneration(), 0u);
    }
    EXPECT_EQ(testArena.arena.memoryStats().usedBytes, initial.usedBytes);
}

TEST(GpuCommandIrCapture, RejectedAppendsPreserveBytesAndGenerationUntilRollbackCompletes){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    ASSERT_TRUE(capture.beginRecordingAttempt(31u));
    ASSERT_TRUE(AppendMixedRecords(capture, 0u, 129u));
    Graphics::GraphicsBytes original(testArena.arena);
    CopyCommandIrBytes(original, capture.commandBytes());
    const ArenaMemoryStats before = testArena.arena.memoryStats();
    EXPECT_FALSE(capture.beginRecordingAttempt(32u));
    const Graphics::GpuSubmissionPacketId otherPlan{ 0u, s_CommandIrPacket.generation + 1u };
    EXPECT_FALSE(capture.captureClearBuffer(s_CommandIrTask, otherPlan, s_CommandIrQueue, s_CommandIrDestination, 5u));
    EXPECT_FALSE(capture.captureCopyBuffer(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_CommandIrSource, 0u, s_CommandIrDestination, 0u, 0u
    ));
    EXPECT_EQ(testArena.arena.memoryStats().allocationCount, before.allocationCount);
    ASSERT_EQ(capture.commandBytes().size(), original.size());
    EXPECT_EQ(NWB_MEMCMP(capture.commandBytes().data(), original.data(), original.size()), 0);
    VerifyMixedRecords(capture, 129u);
    capture.rollback(0u);
    ASSERT_TRUE(capture.beginRecordingAttempt(32u));
    const Graphics::GpuTaskId otherTask{ 0u, s_CommandIrTask.generation + 1u };
    const Graphics::GpuGraphResourceId otherResource{ 0u, otherTask.generation };
    ASSERT_TRUE(capture.captureClearBuffer(otherTask, otherPlan, s_CommandIrQueue, otherResource, 7u));
    EXPECT_EQ(capture.graphGeneration(), otherTask.generation);
    EXPECT_EQ(capture.planGeneration(), otherPlan.generation);
    EXPECT_EQ(capture.recordingAttemptGeneration(), 32u);
    EXPECT_TRUE(Graphics::ValidateGpuCommandIrStream(capture.commandBytes()).valid());
}

// CPU-only capture/rollback/reset workloads; opt in explicitly after correctness tests pass.
TEST(GpuCommandIrCapture, DISABLED_ScalingBenchmark1024Records){
    BenchmarkCapture(1024u);
}

TEST(GpuCommandIrCapture, DISABLED_ScalingBenchmark4096Records){
    BenchmarkCapture(4096u);
}

TEST(GpuCommandIrCapture, DISABLED_ScalingBenchmark16384Records){
    BenchmarkCapture(16384u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

