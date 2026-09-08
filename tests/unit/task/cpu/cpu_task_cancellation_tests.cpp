// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/cpu/scheduler.h>
#include <core/alloc/scratch.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTaskCancellationTests, OldGenerationsKeepDistinctResultsAcrossRepeatedReuse){
    using namespace NWB::Core;
    constexpr usize s_Count = 1024u;
    Alloc::ScratchArena arena("tests/task/cpu/cancellation_generations");
    Vector<CpuTaskHandle, Alloc::ScratchArena> history(s_Count, CpuTaskHandle{}, arena);
    u32 originalCallbacks = 0u;
    u32 laterCallbacks = 0u;
    CpuTaskScheduler scheduler(0u);
    for(usize index = 0u; index < s_Count; ++index){
        CpuTaskScope scope(scheduler);
        history[index] = scope.submit([&](){ ++originalCallbacks; });
        ASSERT_TRUE(history[index].valid());
        EXPECT_EQ(history[index].index, history[0u].index);
        if(index % 2u == 0u)
            scope.cancel();
        scope.wait();
    }
    EXPECT_EQ(originalCallbacks, s_Count / 2u);
    for(usize index = 0u; index < s_Count; ++index){
        const u32 before = laterCallbacks;
        const auto task = scheduler.submit([&](){ ++laterCallbacks; }, history[index]);
        ASSERT_TRUE(task.valid());
        scheduler.wait(task);
        EXPECT_EQ(laterCallbacks - before, index % 2u);
    }
    EXPECT_EQ(laterCallbacks, s_Count / 2u);
    EXPECT_EQ(scheduler.statistics().canceledTasks, s_Count);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}

TEST(CpuTaskCancellationTests, CancellationIsIndexedBySlotAsWellAsGeneration){
    using namespace NWB::Core;
    constexpr usize s_Count = 256u;
    Alloc::ScratchArena arena("tests/task/cpu/cancellation_slots");
    Vector<CpuTaskHandle, Alloc::ScratchArena> rejected(s_Count, CpuTaskHandle{}, arena);
    Vector<CpuTaskHandle, Alloc::ScratchArena> accepted(s_Count, CpuTaskHandle{}, arena);
    u32 canceledCallbacks = 0u;
    u32 completedCallbacks = 0u;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope canceled(scheduler);
    CpuTaskScope completed(scheduler);
    for(usize index = 0u; index < s_Count; ++index){
        rejected[index] = canceled.submit([&](){ ++canceledCallbacks; });
        accepted[index] = completed.submit([&](){ ++completedCallbacks; });
        ASSERT_TRUE(rejected[index].valid());
        ASSERT_TRUE(accepted[index].valid());
        EXPECT_EQ(rejected[index].generation, accepted[index].generation);
        EXPECT_NE(rejected[index].index, accepted[index].index);
    }
    canceled.cancel();
    canceled.wait();
    completed.wait();
    for(usize index = 0u; index < s_Count; ++index){
        ASSERT_TRUE(scheduler.submit([&](){ ++canceledCallbacks; }, rejected[index]).valid());
        ASSERT_TRUE(scheduler.submit([&](){ ++completedCallbacks; }, accepted[index]).valid());
    }
    scheduler.wait();
    EXPECT_EQ(canceledCallbacks, 0u);
    EXPECT_EQ(completedCallbacks, s_Count * 2u);
    EXPECT_EQ(scheduler.statistics().canceledTasks, s_Count * 2u);
}

TEST(CpuTaskCancellationTests, ParallelRangesPreserveHistoryInRecycledSlots){
    using namespace NWB::Core;
    constexpr usize s_Count = 64u;
    Alloc::ScratchArena arena("tests/task/cpu/cancellation_ranges");
    Vector<CpuTaskHandle, Alloc::ScratchArena> history(s_Count, CpuTaskHandle{}, arena);
    u32 canceledCallbacks = 0u;
    u32 rangeCallbacks = 0u;
    u32 interruptedCallbacks = 0u;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope canceled(scheduler);
    for(usize index = 0u; index < s_Count; ++index){
        history[index] = canceled.submit([&](){ ++canceledCallbacks; });
        ASSERT_TRUE(history[index].valid());
    }
    canceled.cancel();
    canceled.wait();
    scheduler.parallelFor(0u, 1u, 1u, [&](usize){ ++rangeCallbacks; });
    scheduler.parallelFor(0u, 128u, 1u, [&](usize){ ++rangeCallbacks; });
    CpuTaskScope interrupted(scheduler);
    interrupted.parallelFor(0u, 64u, 1u, [&](usize index){
        ++interruptedCallbacks;
        if(index == 0u)
            interrupted.cancel();
    });
    interrupted.wait();
    EXPECT_GT(interruptedCallbacks, 0u);
    EXPECT_LT(interruptedCallbacks, 64u);
    const u64 canceledBeforeDependents = scheduler.statistics().canceledTasks;
    for(const auto handle : history)
        ASSERT_TRUE(scheduler.submit([&](){ ++canceledCallbacks; }, handle).valid());
    scheduler.wait();
    EXPECT_EQ(rangeCallbacks, 129u);
    EXPECT_EQ(canceledCallbacks, 0u);
    EXPECT_EQ(scheduler.statistics().canceledTasks, canceledBeforeDependents + s_Count);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

