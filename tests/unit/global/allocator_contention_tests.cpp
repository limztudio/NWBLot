// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/general.h>
#include <core/alloc/persistent.h>

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

struct PersistentArenaWorkerResult{
    bool allocationFailed = false;
    bool contentsPreserved = true;
    bool alignmentPreserved = true;
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


TEST(PersistentArenaTests, SharedArenaConcurrentAllocationCrossThreadFreeAndAccounting){
    constexpr usize s_ThreadCount = 4u;
    constexpr usize s_IterationsPerThread = 4096u;
    constexpr usize s_HandoffAlignment = 256u;
    constexpr usize s_HandoffBytes = 256u;
    constexpr usize s_PoolPayloadBytes = 512u * 1024u;
    const usize poolBytes = NWB::Core::Alloc::PersistentArena::StructureAlignedSize(s_PoolPayloadBytes);
    static constexpr Name s_OwnerName("tests/persistent_arena/shared_cross_thread_churn");
    NWB::Core::Alloc::PersistentArena arena(s_OwnerName, poolBytes);
    u8* handoffs[s_ThreadCount] = {};
    PersistentArenaWorkerResult results[s_ThreadCount];
    Thread workers[s_ThreadCount];
    Latch workersReady(s_ThreadCount);
    Latch start(1u);
    Latch handoffsReady(s_ThreadCount);
    Latch releaseHandoffs(1u);

    for(usize threadIndex = 0u; threadIndex < s_ThreadCount; ++threadIndex){
        workers[threadIndex] = Thread([&arena, &handoffs, &results, &workersReady, &start, &handoffsReady, &releaseHandoffs, threadIndex](){
            bool allocationFailed = false;
            bool contentsPreserved = true;
            bool alignmentPreserved = true;
            workersReady.count_down();
            start.wait();

            auto* const handoff = static_cast<u8*>(arena.allocate(s_HandoffAlignment, s_HandoffBytes));
            if(!handoff)
                allocationFailed = true;
            else{
                volatile u8* const payload = handoff;
                payload[0u] = static_cast<u8>(threadIndex);
                payload[s_HandoffBytes - 1u] = static_cast<u8>(threadIndex + 1u);
                alignmentPreserved &= reinterpret_cast<usize>(handoff) % s_HandoffAlignment == 0u;
            }
            handoffs[threadIndex] = handoff;
            handoffsReady.count_down();
            releaseHandoffs.wait();

            auto* const handedOff = handoffs[(threadIndex + 1u) % s_ThreadCount];
            if(!handedOff)
                allocationFailed = true;
            else{
                volatile u8* const payload = handedOff;
                contentsPreserved &= payload[0u] == static_cast<u8>((threadIndex + 1u) % s_ThreadCount);
                contentsPreserved &= payload[s_HandoffBytes - 1u] == static_cast<u8>((threadIndex + 1u) % s_ThreadCount + 1u);
                arena.deallocate(handedOff, s_HandoffAlignment, s_HandoffBytes);
            }

            for(usize iteration = 0u; iteration < s_IterationsPerThread; ++iteration){
                const usize alignment = (iteration + threadIndex) % 2u == 0u ? 1u : s_HandoffAlignment;
                auto* const allocation = static_cast<u8*>(arena.allocate(alignment, 113u));
                if(!allocation){
                    allocationFailed = true;
                    break;
                }
                volatile u8* const payload = allocation;
                const u8 firstByte = static_cast<u8>(threadIndex + iteration);
                const u8 lastByte = static_cast<u8>(threadIndex + iteration + 1u);
                payload[0u] = firstByte;
                payload[112u] = lastByte;
                alignmentPreserved &= reinterpret_cast<usize>(allocation) % alignment == 0u;
                YieldThread();
                contentsPreserved &= payload[0u] == firstByte;
                contentsPreserved &= payload[112u] == lastByte;
                arena.deallocate(allocation, alignment, 113u);
            }

            results[threadIndex] = PersistentArenaWorkerResult{
                .allocationFailed = allocationFailed,
                .contentsPreserved = contentsPreserved,
                .alignmentPreserved = alignmentPreserved,
            };
        });
    }

    workersReady.wait();
    start.count_down();
    handoffsReady.wait();
    const ArenaMemoryStats live = arena.memoryStats();
    releaseHandoffs.count_down();
    for(Thread& worker : workers)
        worker.join();

    const ArenaMemoryStats settled = arena.memoryStats();
    constexpr u64 s_ExpectedOperations = s_ThreadCount * (s_IterationsPerThread + 1u);
    for(const PersistentArenaWorkerResult& result : results){
        EXPECT_FALSE(result.allocationFailed);
        EXPECT_TRUE(result.contentsPreserved);
        EXPECT_TRUE(result.alignmentPreserved);
    }
    EXPECT_EQ(live.reservedBytes, poolBytes);
    EXPECT_EQ(live.allocationCount, s_ThreadCount);
    EXPECT_EQ(live.reallocationCount, 0u);
    EXPECT_EQ(live.deallocationCount, 0u);
    EXPECT_GT(live.usedBytes, 0u);
    EXPECT_EQ(live.peakUsedBytes, live.usedBytes);
    EXPECT_EQ(settled.reservedBytes, poolBytes);
    EXPECT_EQ(settled.usedBytes, 0u);
    EXPECT_EQ(settled.allocationCount, s_ExpectedOperations);
    EXPECT_EQ(settled.reallocationCount, 0u);
    EXPECT_EQ(settled.deallocationCount, s_ExpectedOperations);
    EXPECT_GE(settled.peakUsedBytes, live.usedBytes);
    EXPECT_LE(settled.peakUsedBytes, poolBytes);
}


TEST(PersistentArenaTests, SharedArenaConcurrentReallocationPreservesPayloadAndAccounting){
    constexpr usize s_ThreadCount = 4u;
    constexpr usize s_IterationsPerThread = 4096u;
    constexpr usize s_InitialBytes = 113u;
    constexpr usize s_ReplacementBytes = 389u;
    constexpr usize s_ReplacementAlignment = 256u;
    constexpr usize s_PoolPayloadBytes = 512u * 1024u;
    const usize poolBytes = NWB::Core::Alloc::PersistentArena::StructureAlignedSize(s_PoolPayloadBytes);
    static constexpr Name s_OwnerName("tests/persistent_arena/shared_reallocation_churn");
    NWB::Core::Alloc::PersistentArena arena(s_OwnerName, poolBytes);
    PersistentArenaWorkerResult results[s_ThreadCount];
    Thread workers[s_ThreadCount];
    Latch workersReady(s_ThreadCount);
    Latch start(1u);

    for(usize threadIndex = 0u; threadIndex < s_ThreadCount; ++threadIndex){
        workers[threadIndex] = Thread([&arena, &results, &workersReady, &start, threadIndex](){
            bool allocationFailed = false;
            bool contentsPreserved = true;
            bool alignmentPreserved = true;
            workersReady.count_down();
            start.wait();

            for(usize iteration = 0u; iteration < s_IterationsPerThread; ++iteration){
                auto* allocation = static_cast<u8*>(arena.allocate(1u, s_InitialBytes));
                if(!allocation){
                    allocationFailed = true;
                    break;
                }
                volatile u8* const initialPayload = allocation;
                const u8 firstByte = static_cast<u8>(threadIndex + iteration);
                const u8 lastByte = static_cast<u8>(threadIndex + iteration + 1u);
                initialPayload[0u] = firstByte;
                initialPayload[s_InitialBytes - 1u] = lastByte;
                YieldThread();

                auto* const replacement = static_cast<u8*>(arena.reallocate(allocation, s_ReplacementAlignment, s_ReplacementBytes));
                if(!replacement){
                    allocationFailed = true;
                    arena.deallocate(allocation, 1u, s_InitialBytes);
                    break;
                }
                allocation = replacement;
                volatile u8* const replacementPayload = allocation;
                contentsPreserved &= replacementPayload[0u] == firstByte;
                contentsPreserved &= replacementPayload[s_InitialBytes - 1u] == lastByte;
                alignmentPreserved &= reinterpret_cast<usize>(allocation) % s_ReplacementAlignment == 0u;
                arena.deallocate(allocation, s_ReplacementAlignment, s_ReplacementBytes);
            }

            results[threadIndex] = PersistentArenaWorkerResult{
                .allocationFailed = allocationFailed,
                .contentsPreserved = contentsPreserved,
                .alignmentPreserved = alignmentPreserved,
            };
        });
    }

    workersReady.wait();
    start.count_down();
    for(Thread& worker : workers)
        worker.join();

    const ArenaMemoryStats settled = arena.memoryStats();
    constexpr u64 s_ExpectedOperations = s_ThreadCount * s_IterationsPerThread;
    for(const PersistentArenaWorkerResult& result : results){
        EXPECT_FALSE(result.allocationFailed);
        EXPECT_TRUE(result.contentsPreserved);
        EXPECT_TRUE(result.alignmentPreserved);
    }
    EXPECT_EQ(settled.reservedBytes, poolBytes);
    EXPECT_EQ(settled.usedBytes, 0u);
    EXPECT_EQ(settled.allocationCount, s_ExpectedOperations);
    EXPECT_EQ(settled.reallocationCount, s_ExpectedOperations);
    EXPECT_EQ(settled.deallocationCount, s_ExpectedOperations);
    EXPECT_LE(settled.peakUsedBytes, poolBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

