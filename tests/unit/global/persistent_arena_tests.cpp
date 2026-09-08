// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/general.h>
#include <core/alloc/persistent.h>

#include <global/arena_c_allocator.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_persistent_arena_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Core::Alloc::PersistentArena;


void ExpectSameStats(const ArenaMemoryStats& actual, const ArenaMemoryStats& expected){
    EXPECT_EQ(actual.reservedBytes, expected.reservedBytes);
    EXPECT_EQ(actual.usedBytes, expected.usedBytes);
    EXPECT_EQ(actual.peakUsedBytes, expected.peakUsedBytes);
    EXPECT_EQ(actual.allocationCount, expected.allocationCount);
    EXPECT_EQ(actual.reallocationCount, expected.reallocationCount);
    EXPECT_EQ(actual.deallocationCount, expected.deallocationCount);
}

void FillPattern(u8* const bytes, const usize byteCount){
    for(usize index = 0u; index < byteCount; ++index)
        bytes[index] = static_cast<u8>(index * 17u + 3u);
}

void ExpectPattern(const u8* const bytes, const usize byteCount){
    for(usize index = 0u; index < byteCount; ++index)
        EXPECT_EQ(bytes[index], static_cast<u8>(index * 17u + 3u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(PersistentArenaTests, StructureAlignedSizeFitsExactPayloadAndReusesTheFixedPool){
    constexpr usize s_PayloadBytes = 4096u;
    const auto structureAlignedSize = static_cast<usize(*)(usize)>(&PersistentArena::StructureAlignedSize);
    PersistentArena arena(
        Name("tests/persistent_arena/exact_payload"),
        structureAlignedSize(s_PayloadBytes)
    );

    const ArenaMemoryStats initial = arena.memoryStats();
    auto* const first = static_cast<u8*>(arena.allocate(1u, s_PayloadBytes));
    ASSERT_NE(first, nullptr);
    FillPattern(first, s_PayloadBytes);
    const ArenaMemoryStats allocated = arena.memoryStats();
    EXPECT_EQ(allocated.reservedBytes, initial.reservedBytes);
    EXPECT_GT(allocated.usedBytes, 0u);
    EXPECT_EQ(allocated.allocationCount, initial.allocationCount + 1u);

    EXPECT_EQ(arena.allocate(1u, 1u), nullptr);
    ExpectSameStats(arena.memoryStats(), allocated);

    arena.deallocate(first, 1u, s_PayloadBytes);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
    auto* const reused = static_cast<u8*>(arena.allocate(1u, s_PayloadBytes));
    ASSERT_NE(reused, nullptr);
    FillPattern(reused, s_PayloadBytes);
    ExpectPattern(reused, s_PayloadBytes);
    arena.deallocate(reused, 1u, s_PayloadBytes);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}


TEST(PersistentArenaTests, StructureAlignedSizeFitsAnExactOverAlignedPayload){
    constexpr usize s_Alignment = 4096u;
    PersistentArena arena(
        Name("tests/persistent_arena/exact_overaligned_payload"),
        PersistentArena::StructureAlignedSize(1u, s_Alignment)
    );

    auto* const allocation = static_cast<u8*>(arena.allocate(s_Alignment, 1u));
    ASSERT_NE(allocation, nullptr);
    EXPECT_EQ(reinterpret_cast<usize>(allocation) % s_Alignment, 0u);
    arena.deallocate(allocation, s_Alignment, 1u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}


TEST(PersistentArenaTests, StructureAlignedSizeFitsMultipleExactTypedArrayBackings){
    struct SlotRecord{
        u64 serial = 0u;
        usize markerHash = 0u;
        u32 marker = 0u;
    };

    constexpr usize s_QueueCount = 9u;
    constexpr usize s_SlotsPerQueue = 256u;
    constexpr usize s_SlotCount = s_QueueCount * s_SlotsPerQueue;
    const usize slotRecordBytes = s_SlotCount * sizeof(SlotRecord);
    const usize nextSerialBytes = s_QueueCount * sizeof(u64);
    const usize poolBytes =
        PersistentArena::StructureAlignedSize(slotRecordBytes, alignof(SlotRecord))
        + PersistentArena::StructureAlignedSize(nextSerialBytes, alignof(u64))
    ;
    PersistentArena arena(Name("tests/persistent_arena/fixed_array_backings"), poolBytes);

    {
        auto slotRecords = NWB::Core::MakePersistentUnique<SlotRecord[]>(arena, s_SlotCount);
        auto nextSerials = NWB::Core::MakePersistentUnique<u64[]>(arena, s_QueueCount);

        ASSERT_TRUE(slotRecords);
        ASSERT_TRUE(nextSerials);
        slotRecords[0u].marker = 7u;
        nextSerials[s_QueueCount - 1u] = 11u;
        EXPECT_EQ(slotRecords[0u].marker, 7u);
        EXPECT_EQ(nextSerials[s_QueueCount - 1u], 11u);
    }

    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}


TEST(PersistentArenaTests, ReusesAdjacentFreeBlocksForGrowthAndCoalescing){
    PersistentArena arena(
        Name("tests/persistent_arena/coalescing"),
        PersistentArena::StructureAlignedSize(4096u)
    );

    auto* const growth = static_cast<u8*>(arena.allocate(1u, 512u));
    auto* const neighbor = static_cast<u8*>(arena.allocate(1u, 1024u));
    ASSERT_NE(growth, nullptr);
    ASSERT_NE(neighbor, nullptr);
    FillPattern(growth, 512u);
    arena.deallocate(neighbor, 1u, 1024u);

    auto* const grown = static_cast<u8*>(arena.reallocate(growth, 1u, 1024u));
    ASSERT_EQ(grown, growth);
    ExpectPattern(grown, 512u);
    arena.deallocate(grown, 1u, 1024u);

    auto* const first = static_cast<u8*>(arena.allocate(1u, 1024u));
    auto* const second = static_cast<u8*>(arena.allocate(1u, 1024u));
    auto* const blocker = static_cast<u8*>(arena.allocate(1u, 1024u));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(blocker, nullptr);
    EXPECT_EQ(arena.allocate(1u, 1536u), nullptr);

    arena.deallocate(first, 1u, 1024u);
    arena.deallocate(second, 1u, 1024u);
    auto* const combined = static_cast<u8*>(arena.allocate(1u, 1536u));
    ASSERT_NE(combined, nullptr);
    arena.deallocate(combined, 1u, 1536u);
    arena.deallocate(blocker, 1u, 1024u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}


TEST(PersistentArenaTests, ReallocationGrowsAcrossMultipleDeferredAdjacentBlocks){
    constexpr usize s_BlockBytes = 512u;
    PersistentArena arena(
        Name("tests/persistent_arena/deferred_reallocation_growth"),
        3u * PersistentArena::StructureAlignedSize(s_BlockBytes)
    );

    auto* const first = static_cast<u8*>(arena.allocate(1u, s_BlockBytes));
    auto* const second = static_cast<u8*>(arena.allocate(1u, s_BlockBytes));
    auto* const third = static_cast<u8*>(arena.allocate(1u, s_BlockBytes));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(third, nullptr);
    FillPattern(first, s_BlockBytes);

    arena.deallocate(second, 1u, s_BlockBytes);
    const ArenaMemoryStats beforeFailedGrowth = arena.memoryStats();
    EXPECT_EQ(arena.reallocate(first, 1u, 1600u), nullptr);
    ExpectPattern(first, s_BlockBytes);
    ExpectSameStats(arena.memoryStats(), beforeFailedGrowth);

    arena.deallocate(third, 1u, s_BlockBytes);

    auto* const grown = static_cast<u8*>(arena.reallocate(first, 1u, 1600u));
    ASSERT_EQ(grown, first);
    ExpectPattern(grown, s_BlockBytes);
    arena.deallocate(grown, 1u, 1600u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}


TEST(PersistentArenaTests, FailedAlignedReallocationRetainsPayloadAndStats){
    PersistentArena arena(
        Name("tests/persistent_arena/failed_reallocation"),
        PersistentArena::StructureAlignedSize(2048u)
    );

    auto* const initial = static_cast<u8*>(arena.allocate(1u, 1024u));
    auto* const blocker = static_cast<u8*>(arena.allocate(1u, 800u));
    ASSERT_NE(initial, nullptr);
    ASSERT_NE(blocker, nullptr);
    FillPattern(initial, 1024u);
    const ArenaMemoryStats beforeFailure = arena.memoryStats();

    EXPECT_EQ(arena.reallocate(initial, 256u, 1536u), nullptr);
    ExpectPattern(initial, 1024u);
    ExpectSameStats(arena.memoryStats(), beforeFailure);

    arena.deallocate(initial, 1u, 1024u);
    arena.deallocate(blocker, 1u, 800u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}


TEST(PersistentArenaTests, ReallocationMaintainsRequestedAlignmentAcrossShrinkAndGrowth){
    constexpr usize s_HighAlignment = 4096u;
    PersistentArena arena(
        Name("tests/persistent_arena/reallocation_alignment"),
        PersistentArena::StructureAlignedSize(4096u, s_HighAlignment)
    );

    auto* const initial = static_cast<u8*>(arena.allocate(1u, 512u));
    ASSERT_NE(initial, nullptr);
    FillPattern(initial, 512u);

    auto* const shrunk = static_cast<u8*>(arena.reallocate(initial, 256u, 256u));
    ASSERT_NE(shrunk, nullptr);
    EXPECT_EQ(reinterpret_cast<usize>(shrunk) % 256u, 0u);
    ExpectPattern(shrunk, 256u);

    auto* const grown = static_cast<u8*>(arena.reallocate(shrunk, s_HighAlignment, 1024u));
    ASSERT_NE(grown, nullptr);
    EXPECT_EQ(reinterpret_cast<usize>(grown) % s_HighAlignment, 0u);
    ExpectPattern(grown, 256u);
    arena.deallocate(grown, s_HighAlignment, 1024u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}


TEST(PersistentArenaTests, ReallocationRetainsAHighAlignedBlockForLowerAlignment){
    constexpr usize s_HighAlignment = 4096u;
    PersistentArena arena(
        Name("tests/persistent_arena/reallocation_lower_alignment"),
        PersistentArena::StructureAlignedSize(1u, s_HighAlignment)
    );

    auto* const allocation = static_cast<u8*>(arena.allocate(s_HighAlignment, 1u));
    ASSERT_NE(allocation, nullptr);
    allocation[0u] = 93u;

    auto* const shrunk = static_cast<u8*>(arena.reallocate(allocation, 1u, 1u));
    ASSERT_EQ(shrunk, allocation);
    EXPECT_EQ(shrunk[0u], 93u);
    arena.deallocate(shrunk, 1u, 1u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}


TEST(PersistentArenaTests, NullAndZeroReallocationFollowAllocationAccounting){
    PersistentArena arena(
        Name("tests/persistent_arena/null_zero_reallocation"),
        PersistentArena::StructureAlignedSize(1024u)
    );

    const ArenaMemoryStats before = arena.memoryStats();
    EXPECT_EQ(arena.reallocate(nullptr, 64u, 0u), nullptr);
    ExpectSameStats(arena.memoryStats(), before);

    auto* const allocation = static_cast<u8*>(arena.reallocate(nullptr, 64u, 33u));
    ASSERT_NE(allocation, nullptr);
    const ArenaMemoryStats allocated = arena.memoryStats();
    EXPECT_EQ(allocated.allocationCount, before.allocationCount + 1u);
    EXPECT_EQ(allocated.reallocationCount, before.reallocationCount);
    EXPECT_EQ(allocated.deallocationCount, before.deallocationCount);

    EXPECT_EQ(arena.reallocate(allocation, 64u, 0u), nullptr);
    const ArenaMemoryStats released = arena.memoryStats();
    EXPECT_EQ(released.reservedBytes, before.reservedBytes);
    EXPECT_EQ(released.usedBytes, 0u);
    EXPECT_EQ(released.allocationCount, allocated.allocationCount);
    EXPECT_EQ(released.reallocationCount, allocated.reallocationCount);
    EXPECT_EQ(released.deallocationCount, allocated.deallocationCount + 1u);
}


TEST(PersistentArenaTests, CAllocatorAdapterRetainsMaxAlignmentAndPrefix){
    PersistentArena arena(
        Name("tests/persistent_arena/c_allocator"),
        PersistentArena::StructureAlignedSize(1024u)
    );

    auto* const zeroAllocation = static_cast<u8*>(AllocateArenaCMemory(arena, 0u));
    ASSERT_NE(zeroAllocation, nullptr);
    EXPECT_EQ(reinterpret_cast<usize>(zeroAllocation) % alignof(MaxAlign), 0u);
    zeroAllocation[0u] = 47u;

    auto* const grown = static_cast<u8*>(ReallocateArenaCMemory(arena, zeroAllocation, 37u));
    ASSERT_NE(grown, nullptr);
    EXPECT_EQ(reinterpret_cast<usize>(grown) % alignof(MaxAlign), 0u);
    EXPECT_EQ(grown[0u], 47u);
    DeallocateArenaCMemory(arena, grown);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

