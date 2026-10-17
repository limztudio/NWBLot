// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/cpu/scheduler.h>
#include <core/alloc/scratch.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


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
        const auto cpuAdmission5 = scope.submit([&]()noexcept{ ++originalCallbacks; });
        ASSERT_TRUE(cpuAdmission5);
        history[index] = *cpuAdmission5;
        ASSERT_TRUE(history[index].valid());
        EXPECT_EQ(history[index].index, history[0u].index);
        if(index % s_ExpectedDualCount == 0u)
            scope.cancel();
        scope.wait();
    }
    EXPECT_EQ(originalCallbacks, s_Count / s_ExpectedDualCount);
    for(usize index = 0u; index < s_Count; ++index){
        const u32 before = laterCallbacks;
        const auto cpuAdmission6 = scheduler.submit([&]()noexcept{ ++laterCallbacks; }, history[index]);
        ASSERT_TRUE(cpuAdmission6);
        const auto task = *cpuAdmission6;
        ASSERT_TRUE(task.valid());
        scheduler.wait(task);
        EXPECT_EQ(laterCallbacks - before, index % s_ExpectedDualCount);
    }
    EXPECT_EQ(laterCallbacks, s_Count / s_ExpectedDualCount);
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
        const auto cpuAdmission7 = canceled.submit([&]()noexcept{ ++canceledCallbacks; });
        ASSERT_TRUE(cpuAdmission7);
        rejected[index] = *cpuAdmission7;
        const auto cpuAdmission8 = completed.submit([&]()noexcept{ ++completedCallbacks; });
        ASSERT_TRUE(cpuAdmission8);
        accepted[index] = *cpuAdmission8;
        ASSERT_TRUE(rejected[index].valid());
        ASSERT_TRUE(accepted[index].valid());
        EXPECT_EQ(rejected[index].generation, accepted[index].generation);
        EXPECT_NE(rejected[index].index, accepted[index].index);
    }
    canceled.cancel();
    canceled.wait();
    completed.wait();
    for(usize index = 0u; index < s_Count; ++index){
        const auto cpuAdmission9 = scheduler.submit([&]()noexcept{ ++canceledCallbacks; }, rejected[index]);
        ASSERT_TRUE(cpuAdmission9);
        if(cpuAdmission9)
            ASSERT_TRUE(cpuAdmission9->valid());
        const auto cpuAdmission10 = scheduler.submit([&]()noexcept{ ++completedCallbacks; }, accepted[index]);
        ASSERT_TRUE(cpuAdmission10);
        if(cpuAdmission10)
            ASSERT_TRUE(cpuAdmission10->valid());
    }
    scheduler.wait();
    EXPECT_EQ(canceledCallbacks, 0u);
    EXPECT_EQ(completedCallbacks, s_Count * s_ExpectedDualCount);
    EXPECT_EQ(scheduler.statistics().canceledTasks, s_Count * s_ExpectedDualCount);
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
        const auto cpuAdmission11 = canceled.submit([&]()noexcept{ ++canceledCallbacks; });
        ASSERT_TRUE(cpuAdmission11);
        history[index] = *cpuAdmission11;
        ASSERT_TRUE(history[index].valid());
    }
    canceled.cancel();
    canceled.wait();
    EXPECT_TRUE((scheduler.parallelFor(0u, 1u, 1u, [&](usize)noexcept{ ++rangeCallbacks; })));
    EXPECT_TRUE((scheduler.parallelFor(0u, 128u, 1u, [&](usize)noexcept{ ++rangeCallbacks; })));
    CpuTaskScope interrupted(scheduler);
    EXPECT_TRUE((interrupted.parallelFor(0u, 64u, 1u, [&](usize index){
        ++interruptedCallbacks;
        if(index == 0u)
            interrupted.cancel();
    }
    )));
    interrupted.wait();
    EXPECT_GT(interruptedCallbacks, 0u);
    EXPECT_LT(interruptedCallbacks, 64u);
    const u64 canceledBeforeDependents = scheduler.statistics().canceledTasks;
    for(const auto handle : history){
        const auto cpuAdmission12 = scheduler.submit([&]()noexcept{ ++canceledCallbacks; }, handle);
        ASSERT_TRUE(cpuAdmission12);
        if(cpuAdmission12)
            ASSERT_TRUE(cpuAdmission12->valid());
    }
    scheduler.wait();
    EXPECT_EQ(rangeCallbacks, 129u);
    EXPECT_EQ(canceledCallbacks, 0u);
    EXPECT_EQ(scheduler.statistics().canceledTasks, canceledBeforeDependents + s_Count);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

