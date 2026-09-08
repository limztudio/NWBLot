// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/gpu/timing_feedback.h>

#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_timing_history_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

struct TimingInput{
    GpuTaskTimingKey key;
    GpuPhysicalQueueId queue;
};

[[nodiscard]] TimingInput Input(const u32 index, const u16 generation = 1u){
    return {
        .key = {
            .task = Name("tests/timing_history/work"),
            .variant = index,
            .resolutionClass = index % 3u,
            .queue = static_cast<CommandQueue::Enum>(index % CommandQueue::kCount),
        },
        .queue = { static_cast<u16>(index % 7u), generation },
    };
}

TEST(GpuTaskTimingHistory, PreservesEveryRouteKeyFieldAndFullNameIdentity){
    GraphicsArena arena(Name("tests/timing_history/full_identity"));
    GpuTaskTimingHistoryStore store(arena);
    for(u32 index = 100u; index < 164u; ++index){
        const TimingInput input = Input(index);
        ASSERT_TRUE(store.recordNonCommittingSample(input.key, input.queue, 1.0));
    }
    const TimingInput base = Input(0u);
    TimingInput inputs[13u];
    for(TimingInput& input : inputs)
        input = base;
    inputs[1u].key.variant = 1u;
    inputs[2u].key.resolutionClass = 1u;
    inputs[3u].key.queue = CommandQueue::Compute;
    inputs[4u].queue.index = 1u;
    for(u32 lane = 0u; lane < s_NameHashLaneCount; ++lane){
        NameHash hash = base.key.task.hash();
        hash.qwords[lane] ^= 1u;
        inputs[5u + lane].key.task = Name(hash);
    }
    for(usize index = 0u; index < LengthOf(inputs); ++index)
        ASSERT_TRUE(store.recordNonCommittingSample(inputs[index].key, inputs[index].queue, static_cast<f64>(index + 1u)));
    EXPECT_EQ(store.historyCount(), LengthOf(inputs) + 64u);
    GpuTaskTimingHistorySnapshot snapshot(arena);
    store.snapshot(snapshot);
    ASSERT_TRUE(snapshot.valid());
    for(usize index = 0u; index < LengthOf(inputs); ++index){
        const GpuTaskTimingHistory* const history = store.find(inputs[index].key, inputs[index].queue);
        const GpuTaskTimingHistory* const frozen = snapshot.find(inputs[index].key, inputs[index].queue);
        ASSERT_NE(history, nullptr);
        ASSERT_NE(frozen, nullptr);
        EXPECT_DOUBLE_EQ(history->averageSeconds, static_cast<f64>(index + 1u));
        EXPECT_DOUBLE_EQ(frozen->averageSeconds, history->averageSeconds);
        EXPECT_EQ(history->sampleCount, 1u);
        EXPECT_EQ(snapshot.findAssignment(GpuTaskTimingAssignmentKeyFromHistoryKey(inputs[index].key)), nullptr);
    }
    GpuPhysicalQueueId foreignQueue = base.queue;
    ++foreignQueue.deviceGeneration;
    EXPECT_FALSE(store.recordSample(base.key, foreignQueue, 1.0, 1u));
    EXPECT_EQ(store.find(base.key, foreignQueue), nullptr);
    EXPECT_EQ(snapshot.find(base.key, foreignQueue), nullptr);
    EXPECT_EQ(snapshot.find({}, base.queue), nullptr);
    EXPECT_EQ(snapshot.find(base.key, {}), nullptr);
    EXPECT_EQ(store.historyCount(), LengthOf(inputs) + 64u);
}

TEST(GpuTaskTimingHistory, KeepsSemanticAssignmentsAcrossQueueClassChangesAndLateSamples){
    GraphicsArena arena(Name("tests/timing_history/assignment_order"));
    GpuTaskTimingHistoryStore store(arena, 2u);
    for(u32 index = 1u; index < 80u; ++index){
        const TimingInput input = Input(index);
        ASSERT_TRUE(store.recordSample(input.key, input.queue, 1.0, 1u));
    }
    const TimingInput graphics = Input(0u);
    TimingInput compute = graphics;
    compute.key.queue = CommandQueue::Compute;
    compute.queue.index = 8u;
    const GpuTaskTimingAssignmentKey assignmentKey = GpuTaskTimingAssignmentKeyFromHistoryKey(graphics.key);
    ASSERT_TRUE(store.recordSample(graphics.key, graphics.queue, 2.0, 10u));
    ASSERT_TRUE(store.recordSample(compute.key, compute.queue, 4.0, 20u));
    ASSERT_TRUE(store.recordSample(graphics.key, graphics.queue, 6.0, 5u));
    ASSERT_TRUE(store.recordSample(graphics.key, graphics.queue, 10.0, 6u));
    ASSERT_FALSE(store.recordSample(graphics.key, graphics.queue, 99.0, 20u));
    ASSERT_TRUE(store.noteAcceptedAssignment(compute.key, compute.queue, 20u));
    const GpuTaskTimingAssignmentState* state = store.findAssignment(assignmentKey);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->lastAcceptedQueue, compute.queue);
    EXPECT_EQ(state->lastAcceptedFrameIndex, 20u);
    EXPECT_EQ(state->lastSwitchFrameIndex, 20u);
    const GpuTaskTimingHistory* const history = store.find(graphics.key, graphics.queue);
    ASSERT_NE(history, nullptr);
    EXPECT_DOUBLE_EQ(history->averageSeconds, 8.0);
    EXPECT_DOUBLE_EQ(history->minimumSeconds, 6.0);
    EXPECT_DOUBLE_EQ(history->maximumSeconds, 10.0);
    EXPECT_EQ(history->sampleCount, 2u);
    ASSERT_TRUE(store.recordNonCommittingSample(graphics.key, graphics.queue, 14.0));
    EXPECT_EQ(store.findAssignment(assignmentKey)->lastAcceptedQueue, compute.queue);
    ASSERT_TRUE(store.noteAcceptedAssignment(graphics.key, graphics.queue, 30u));
    state = store.findAssignment(assignmentKey);
    EXPECT_EQ(state->lastAcceptedQueue, graphics.queue);
    EXPECT_EQ(state->lastSwitchFrameIndex, 30u);
    EXPECT_EQ(store.historyCount(), 81u);
}

TEST(GpuTaskTimingHistory, SnapshotSurvivesStoreGrowthResetAndReplacementInAnotherArena){
    GraphicsArena storeArena(Name("tests/timing_history/store_owner"));
    GraphicsArena snapshotArena(Name("tests/timing_history/snapshot_owner"));
    GpuTaskTimingHistorySnapshot snapshot(snapshotArena);
    const TimingInput first = Input(0u);
    {
        GpuTaskTimingHistoryStore store(storeArena, 3u);
        for(u32 index = 0u; index < 512u; ++index){
            const TimingInput input = Input((index * 73u) % 512u);
            ASSERT_TRUE(store.recordSample(input.key, input.queue, static_cast<f64>(input.key.variant + 1u), 10u));
        }
        store.snapshot(snapshot);
        ASSERT_TRUE(snapshot.valid());
        const TimingInput absent = Input(4096u);
        EXPECT_EQ(store.find(absent.key, absent.queue), nullptr);
        EXPECT_EQ(store.findAssignment(GpuTaskTimingAssignmentKeyFromHistoryKey(absent.key)), nullptr);
        EXPECT_EQ(snapshot.find(absent.key, absent.queue), nullptr);
        EXPECT_EQ(snapshot.findAssignment(GpuTaskTimingAssignmentKeyFromHistoryKey(absent.key)), nullptr);
        store.resetForDeviceGeneration(1u);
        EXPECT_EQ(store.historyCount(), 512u);
        for(u32 index = 0u; index < 512u; ++index){
            const TimingInput input = Input(index);
            const GpuTaskTimingHistory* const history = snapshot.find(input.key, input.queue);
            ASSERT_NE(history, nullptr);
            EXPECT_DOUBLE_EQ(history->averageSeconds, static_cast<f64>(index + 1u));
            const GpuTaskTimingAssignmentState* const state = snapshot.findAssignment(GpuTaskTimingAssignmentKeyFromHistoryKey(input.key));
            ASSERT_NE(state, nullptr);
            EXPECT_EQ(state->lastAcceptedFrameIndex, 10u);
        }
        ASSERT_TRUE(store.recordSample(first.key, first.queue, 9.0, 20u));
        for(u32 index = 512u; index < 1024u; ++index){
            const TimingInput input = Input(index);
            ASSERT_TRUE(store.recordSample(input.key, input.queue, 1.0, 20u));
        }
        EXPECT_DOUBLE_EQ(snapshot.find(first.key, first.queue)->averageSeconds, 1.0);
        EXPECT_EQ(snapshot.findAssignment(GpuTaskTimingAssignmentKeyFromHistoryKey(first.key))->lastAcceptedFrameIndex, 10u);
        const TimingInput newer = Input(600u);
        EXPECT_EQ(snapshot.find(newer.key, newer.queue), nullptr);
        store.resetForDeviceGeneration(2u);
        EXPECT_EQ(store.historyCount(), 0u);
        EXPECT_EQ(store.find(first.key, first.queue), nullptr);
        EXPECT_EQ(store.findAssignment(GpuTaskTimingAssignmentKeyFromHistoryKey(first.key)), nullptr);
    }
    EXPECT_EQ(storeArena.memoryStats().usedBytes, 0u);
    ASSERT_NE(snapshot.find(first.key, first.queue), nullptr);
    EXPECT_DOUBLE_EQ(snapshot.find(first.key, first.queue)->averageSeconds, 1.0);
    GpuTaskTimingHistoryStore replacement(storeArena);
    replacement.reset(snapshot);
    EXPECT_FALSE(snapshot.valid());
    EXPECT_EQ(snapshot.find(first.key, first.queue), nullptr);
    replacement.resetForDeviceGeneration(2u);
    const TimingInput newGeneration = Input(0u, 2u);
    ASSERT_TRUE(replacement.recordSample(newGeneration.key, newGeneration.queue, 7.0, 1u));
    replacement.snapshot(snapshot);
    EXPECT_EQ(snapshot.deviceGeneration(), 2u);
    EXPECT_EQ(snapshot.find(first.key, first.queue), nullptr);
    ASSERT_NE(snapshot.find(newGeneration.key, newGeneration.queue), nullptr);
    EXPECT_DOUBLE_EQ(snapshot.find(newGeneration.key, newGeneration.queue)->averageSeconds, 7.0);
    replacement.resetForDeviceGeneration(0u);
    replacement.snapshot(snapshot);
    EXPECT_FALSE(snapshot.valid());
    EXPECT_EQ(snapshot.findAssignment(GpuTaskTimingAssignmentKeyFromHistoryKey(first.key)), nullptr);
}

TEST(GpuTaskTimingHistory, RejectedInputDoesNotCreateHistoryOrAssignmentState){
    GraphicsArena arena(Name("tests/timing_history/rejections"));
    GpuTaskTimingHistoryStore store(arena);
    const TimingInput input = Input(0u);
    EXPECT_FALSE(store.recordSample({}, input.queue, 1.0, 1u));
    EXPECT_FALSE(store.recordSample(input.key, {}, 1.0, 1u));
    for(const f64 duration : { 0.0, -1.0, Limit<f64>::s_Max }){
        EXPECT_FALSE(store.recordSample(input.key, input.queue, duration, 1u));
        EXPECT_FALSE(store.recordNonCommittingSample(input.key, input.queue, duration));
    }
    EXPECT_EQ(store.deviceGeneration(), 0u);
    EXPECT_EQ(store.historyCount(), 0u);
    EXPECT_EQ(store.findAssignment(GpuTaskTimingAssignmentKeyFromHistoryKey(input.key)), nullptr);
    GpuTaskTimingHistorySnapshot snapshot(arena);
    store.snapshot(snapshot);
    EXPECT_FALSE(snapshot.valid());
    store.resetForDeviceGeneration(1u);
    store.snapshot(snapshot);
    EXPECT_TRUE(snapshot.valid());
    EXPECT_EQ(snapshot.find(input.key, input.queue), nullptr);
}

TEST(GpuTaskTimingHistory, PromotesHistoryAndAssignmentCollectionsIndependentlyAndReusesResetStorage){
    GraphicsArena arena(Name("tests/timing_history/independent_collections"));
    GpuTaskTimingHistoryStore historyStore(arena);
    GpuTaskTimingHistoryStore assignmentStore(arena);
    GpuTaskTimingHistorySnapshot snapshot(arena);
    for(u32 index = 0u; index < 80u; ++index){
        const TimingInput input = Input(index);
        ASSERT_TRUE(historyStore.recordNonCommittingSample(input.key, input.queue, 1.0));
        ASSERT_TRUE(assignmentStore.noteAcceptedAssignment(input.key, input.queue, 10u));
    }
    const TimingInput first = Input(0u);
    const GpuTaskTimingAssignmentKey assignmentKey = GpuTaskTimingAssignmentKeyFromHistoryKey(first.key);
    historyStore.snapshot(snapshot);
    ASSERT_NE(snapshot.find(first.key, first.queue), nullptr);
    EXPECT_EQ(snapshot.findAssignment(assignmentKey), nullptr);
    assignmentStore.snapshot(snapshot);
    EXPECT_EQ(snapshot.find(first.key, first.queue), nullptr);
    ASSERT_NE(snapshot.findAssignment(assignmentKey), nullptr);
    EXPECT_EQ(assignmentStore.historyCount(), 0u);
    ASSERT_TRUE(assignmentStore.recordNonCommittingSample(first.key, first.queue, 3.0));
    assignmentStore.snapshot(snapshot);
    ASSERT_NE(snapshot.find(first.key, first.queue), nullptr);
    EXPECT_DOUBLE_EQ(snapshot.find(first.key, first.queue)->averageSeconds, 3.0);
    EXPECT_EQ(snapshot.findAssignment(assignmentKey)->lastAcceptedFrameIndex, 10u);
    historyStore.reset();
    ASSERT_TRUE(historyStore.recordSample(first.key, first.queue, 7.0, 30u));
    historyStore.snapshot(snapshot);
    ASSERT_NE(snapshot.find(first.key, first.queue), nullptr);
    EXPECT_DOUBLE_EQ(snapshot.find(first.key, first.queue)->averageSeconds, 7.0);
    EXPECT_EQ(snapshot.findAssignment(assignmentKey)->lastAcceptedFrameIndex, 30u);
    const TimingInput stale = Input(79u);
    EXPECT_EQ(snapshot.find(stale.key, stale.queue), nullptr);
    EXPECT_EQ(snapshot.findAssignment(GpuTaskTimingAssignmentKeyFromHistoryKey(stale.key)), nullptr);
}

TEST(GpuTaskTimingHistory, SmallRepeatedUpdatesAndSnapshotsReuseTheirAllocations){
    GraphicsArena arena(Name("tests/timing_history/small_reuse"));
    GpuTaskTimingHistoryStore store(arena, 4u);
    GpuTaskTimingHistorySnapshot snapshot(arena);
    for(u32 index = 0u; index < 8u; ++index){
        const TimingInput input = Input(index);
        ASSERT_TRUE(store.recordSample(input.key, input.queue, 1.0, 1u));
    }
    store.snapshot(snapshot);
    const ArenaMemoryStats before = arena.memoryStats();
    for(u64 frame = 2u; frame < 16u; ++frame){
        for(u32 index = 0u; index < 8u; ++index){
            const TimingInput input = Input(index);
            ASSERT_TRUE(store.recordSample(input.key, input.queue, 2.0, frame));
        }
        store.snapshot(snapshot);
    }
    const ArenaMemoryStats after = arena.memoryStats();
    EXPECT_EQ(after.allocationCount, before.allocationCount);
    EXPECT_EQ(after.deallocationCount, before.deallocationCount);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_EQ(after.reservedBytes, before.reservedBytes);
}

static void BenchmarkHistory(const u32 keyCount, const u32 repeatCount){
    GraphicsArena inputArena(Name("tests/timing_history/benchmark_inputs"));
    GraphicsArena storeArena(Name("tests/timing_history/benchmark_store"));
    GraphicsArena snapshotArena(Name("tests/timing_history/benchmark_snapshot"));
    GraphicsVector<TimingInput> inputs(inputArena);
    inputs.reserve(keyCount);
    for(u32 index = 0u; index < keyCount; ++index)
        inputs.push_back(Input((index * 73u) % keyCount));
    GpuTaskTimingHistoryStore store(storeArena, 4u);
    GpuTaskTimingHistorySnapshot snapshot(snapshotArena);
    bool accepted = true;
    const Timer recordBegin = TimerNow();
    for(const TimingInput& input : inputs)
        accepted = store.recordSample(input.key, input.queue, 1.0, 1u) && accepted;
    const u64 recordNanoseconds = DurationInNS<u64>(TimerNow(), recordBegin);
    const Timer updateBegin = TimerNow();
    for(u32 repeat = 0u; repeat < repeatCount; ++repeat){
        for(const TimingInput& input : inputs)
            accepted = store.recordSample(input.key, input.queue, 2.0, repeat + 2u) && accepted;
    }
    const u64 updateNanoseconds = DurationInNS<u64>(TimerNow(), updateBegin);
    const Timer snapshotBegin = TimerNow();
    store.snapshot(snapshot);
    const u64 snapshotNanoseconds = DurationInNS<u64>(TimerNow(), snapshotBegin);
    const Timer snapshotRepeatBegin = TimerNow();
    for(u32 repeat = 0u; repeat < repeatCount; ++repeat)
        store.snapshot(snapshot);
    const u64 snapshotRepeatNanoseconds = DurationInNS<u64>(TimerNow(), snapshotRepeatBegin);
    u64 storeSamples = 0u;
    u64 storeFrames = 0u;
    const Timer storeLookupBegin = TimerNow();
    for(u32 repeat = 0u; repeat < repeatCount; ++repeat){
        for(const TimingInput& input : inputs){
            const GpuTaskTimingHistory* const history = store.find(input.key, input.queue);
            const GpuTaskTimingAssignmentState* const state = store.findAssignment(GpuTaskTimingAssignmentKeyFromHistoryKey(input.key));
            if(history)
                storeSamples += history->sampleCount;
            if(state)
                storeFrames += state->lastAcceptedFrameIndex;
        }
    }
    const u64 storeLookupNanoseconds = DurationInNS<u64>(TimerNow(), storeLookupBegin);
    u64 snapshotSamples = 0u;
    u64 snapshotFrames = 0u;
    const Timer snapshotLookupBegin = TimerNow();
    for(u32 repeat = 0u; repeat < repeatCount; ++repeat){
        for(const TimingInput& input : inputs){
            const GpuTaskTimingHistory* const history = snapshot.find(input.key, input.queue);
            const GpuTaskTimingAssignmentState* const state = snapshot.findAssignment(GpuTaskTimingAssignmentKeyFromHistoryKey(input.key));
            if(history)
                snapshotSamples += history->sampleCount;
            if(state)
                snapshotFrames += state->lastAcceptedFrameIndex;
        }
    }
    const u64 snapshotLookupNanoseconds = DurationInNS<u64>(TimerNow(), snapshotLookupBegin);
    EXPECT_TRUE(accepted);
    EXPECT_EQ(store.historyCount(), keyCount);
    EXPECT_EQ(storeSamples, static_cast<u64>(keyCount) * repeatCount * Min(4u, repeatCount + 1u));
    EXPECT_EQ(storeFrames, static_cast<u64>(keyCount) * repeatCount * (repeatCount + 1u));
    EXPECT_EQ(snapshotSamples, storeSamples);
    EXPECT_EQ(snapshotFrames, storeFrames);
    char recordText[32u] = {};
    char updateText[32u] = {};
    char snapshotText[32u] = {};
    char snapshotRepeatText[32u] = {};
    char storeLookupText[32u] = {};
    char snapshotLookupText[32u] = {};
    char repeatText[32u] = {};
    char storeBytesText[32u] = {};
    char snapshotBytesText[32u] = {};
    testing::Test::RecordProperty("history_record_ns", FormatDecimal(recordNanoseconds, recordText).data());
    testing::Test::RecordProperty("history_update_ns", FormatDecimal(updateNanoseconds, updateText).data());
    testing::Test::RecordProperty("history_snapshot_ns", FormatDecimal(snapshotNanoseconds, snapshotText).data());
    testing::Test::RecordProperty("history_snapshot_repeat_ns", FormatDecimal(snapshotRepeatNanoseconds, snapshotRepeatText).data());
    testing::Test::RecordProperty("history_store_lookup_ns", FormatDecimal(storeLookupNanoseconds, storeLookupText).data());
    testing::Test::RecordProperty("history_snapshot_lookup_ns", FormatDecimal(snapshotLookupNanoseconds, snapshotLookupText).data());
    testing::Test::RecordProperty("history_repeat_count", FormatDecimal(repeatCount, repeatText).data());
    testing::Test::RecordProperty("history_store_used_bytes", FormatDecimal(storeArena.memoryStats().usedBytes, storeBytesText).data());
    testing::Test::RecordProperty("history_snapshot_used_bytes", FormatDecimal(snapshotArena.memoryStats().usedBytes, snapshotBytesText).data());
}

TEST(GpuTaskTimingHistory, DISABLED_BenchmarkSmallHistory){
    BenchmarkHistory(8u, 256u);
}

TEST(GpuTaskTimingHistory, DISABLED_BenchmarkMediumHistory){
    BenchmarkHistory(512u, 4u);
}

TEST(GpuTaskTimingHistory, DISABLED_BenchmarkLargeHistory){
    BenchmarkHistory(4096u, 4u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

