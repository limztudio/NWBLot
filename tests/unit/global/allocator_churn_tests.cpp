// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/general.h>

#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_allocator_churn_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static u64 VerifyAllocationChurn(NWB::Core::Alloc::GlobalArena& arena, const usize alignment){
    constexpr usize s_BatchCount = 2048u;
    constexpr usize s_BatchSize = 64u;
    constexpr usize s_AllocationBytes = 128u;
    u8* allocations[s_BatchSize] = {};
    bool allocationFailed = false;
    bool contentsPreserved = true;
    bool alignmentPreserved = true;

    const Timer begin = TimerNow();
    for(usize batch = 0u; batch < s_BatchCount; ++batch){
        usize preparedCount = 0u;
        for(usize slot = 0u; slot < s_BatchSize; ++slot){
            auto* const allocation = static_cast<u8*>(arena.allocate(alignment, s_AllocationBytes));
            if(!allocation){
                allocationFailed = true;
                break;
            }
            allocations[slot] = allocation;
            ++preparedCount;
            alignmentPreserved &= reinterpret_cast<usize>(allocation) % alignment == 0u;
            allocation[0u] = static_cast<u8>(slot);
            allocation[s_AllocationBytes - 1u] = static_cast<u8>(batch);
        }

        while(preparedCount != 0u){
            const usize slot = --preparedCount;
            u8* const allocation = allocations[slot];
            contentsPreserved &= allocation[0u] == static_cast<u8>(slot);
            contentsPreserved &= allocation[s_AllocationBytes - 1u] == static_cast<u8>(batch);
            arena.deallocate(allocation, alignment, s_AllocationBytes);
        }
        if(allocationFailed)
            break;
    }
    const u64 elapsedNanoseconds = DurationInNS<u64>(TimerNow(), begin);

    EXPECT_FALSE(allocationFailed);
    EXPECT_TRUE(contentsPreserved);
    EXPECT_TRUE(alignmentPreserved);
    const ArenaMemoryStats stats = arena.memoryStats();
    EXPECT_EQ(stats.allocationCount, s_BatchCount * s_BatchSize);
    EXPECT_EQ(stats.deallocationCount, stats.allocationCount);
    EXPECT_EQ(stats.reallocationCount, 0u);
    EXPECT_EQ(stats.usedBytes, 0u);
    EXPECT_EQ(stats.reservedBytes, 0u);
    EXPECT_EQ(stats.peakUsedBytes, s_BatchSize * Alignment(alignment, s_AllocationBytes));
    return elapsedNanoseconds;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GlobalArenaTests, UnalignedAllocationChurnPreservesPayloadAndAccounting){
    NWB::Core::Alloc::GlobalArena arena("GlobalArenaTests.UnalignedAllocationChurn");
    const u64 elapsedNanoseconds = __hidden_allocator_churn_tests::VerifyAllocationChurn(arena, 1u);
    char durationText[32u] = {};
    RecordProperty("allocation_churn_ns", FormatDecimal(elapsedNanoseconds, durationText).data());
}

TEST(GlobalArenaTests, AlignedAllocationChurnPreservesPayloadAndAccounting){
    NWB::Core::Alloc::GlobalArena arena("GlobalArenaTests.AlignedAllocationChurn");
    const u64 elapsedNanoseconds = __hidden_allocator_churn_tests::VerifyAllocationChurn(arena, 256u);
    char durationText[32u] = {};
    RecordProperty("allocation_churn_ns", FormatDecimal(elapsedNanoseconds, durationText).data());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

