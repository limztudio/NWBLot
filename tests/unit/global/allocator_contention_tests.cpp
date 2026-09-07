// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/general.h>

#include <global/text_utils.h>
#include <global/thread.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_allocator_contention_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct WorkerResult{
    ArenaMemoryStats stats;
    bool allocationFailed = false;
    bool contentsPreserved = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GlobalArenaTests, SharedOwnerAllocationChurnPreservesPayloadAndAccounting){
    constexpr usize s_ThreadCount = 4u;
    constexpr usize s_IterationsPerThread = 32768u;
    constexpr usize s_AllocationBytes = 128u;
    static constexpr Name s_OwnerName("tests/global/shared_owner_allocation_churn");
    WorkerResult results[s_ThreadCount];
    Thread workers[s_ThreadCount];
    Latch workersReady(s_ThreadCount);
    Latch start(1u);

    for(usize threadIndex = 0u; threadIndex < s_ThreadCount; ++threadIndex){
        workers[threadIndex] = Thread([&results, &workersReady, &start, threadIndex](){
            NWB::Core::Alloc::GlobalArena arena(s_OwnerName);
            bool allocationFailed = false;
            bool contentsPreserved = true;
            workersReady.count_down();
            start.wait();

            for(usize iteration = 0u; iteration < s_IterationsPerThread; ++iteration){
                auto* const allocation = static_cast<u8*>(arena.allocate(1u, s_AllocationBytes));
                if(!allocation){
                    allocationFailed = true;
                    break;
                }
                // Volatile payload accesses retain the cross-thread allocator correctness check in opt/fin builds.
                volatile u8* const payload = allocation;
                const u8 firstByte = static_cast<u8>(threadIndex);
                const u8 lastByte = static_cast<u8>(iteration);
                payload[0u] = firstByte;
                payload[s_AllocationBytes - 1u] = lastByte;
                contentsPreserved &= payload[0u] == firstByte;
                contentsPreserved &= payload[s_AllocationBytes - 1u] == lastByte;
                arena.deallocate(allocation, 1u, s_AllocationBytes);
            }

            results[threadIndex] = WorkerResult{
                .stats = arena.memoryStats(),
                .allocationFailed = allocationFailed,
                .contentsPreserved = contentsPreserved,
            };
        });
    }

    // Every arena and thread exists before timing begins. The timed interval contains simultaneous churn and join.
    workersReady.wait();
    const Timer begin = TimerNow();
    start.count_down();
    for(Thread& worker : workers)
        worker.join();
    const u64 elapsedNanoseconds = DurationInNS<u64>(TimerNow(), begin);

    for(const WorkerResult& result : results){
        EXPECT_FALSE(result.allocationFailed);
        EXPECT_TRUE(result.contentsPreserved);
        EXPECT_EQ(result.stats.allocationCount, s_IterationsPerThread);
        EXPECT_EQ(result.stats.deallocationCount, s_IterationsPerThread);
        EXPECT_EQ(result.stats.reallocationCount, 0u);
        EXPECT_EQ(result.stats.usedBytes, 0u);
        EXPECT_EQ(result.stats.reservedBytes, 0u);
        EXPECT_GE(result.stats.peakUsedBytes, s_AllocationBytes);
    }
    char durationText[32u] = {};
    RecordProperty("shared_owner_churn_ns", FormatDecimal(elapsedNanoseconds, durationText).data());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

