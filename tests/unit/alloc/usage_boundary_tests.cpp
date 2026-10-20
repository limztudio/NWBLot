// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/arena_owner_fixture.h>

#include <core/alloc/general.h>

#include <global/arena_memory.h>
#include <global/limit.h>
#include <global/text_utils.h>

#include <cerrno>
#include <gtest/gtest.h>
#include <tbb/scalable_allocator.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_allocator_usage_boundary_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core::Alloc;
using ArenaOwnerTestSupport::OwnerStats;

inline constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AllocatorUsageBoundaries, GlobalArenaChargesUsableBytesAcrossOddSizeReallocations){
    constexpr u32 s_OddBytes = 13u;
    constexpr Name s_Owner("tests/allocation_owners/odd_reallocation");
    GlobalArena arena(s_Owner);
    auto* pointer = static_cast<u8*>(arena.allocate(1u, s_OddBytes));
    ASSERT_NE(pointer, nullptr);
    for(usize index = 0u; index < s_OddBytes; ++index)
        pointer[index] = static_cast<u8>(index + 1u);
    EXPECT_EQ(arena.memoryStats().usedBytes, CoreMsize(pointer));
    EXPECT_EQ(arena.memoryStats().reservedBytes, CoreMsize(pointer));

    auto* replacement = static_cast<u8*>(arena.reallocate(pointer, 1u, 21u));
    ASSERT_NE(replacement, nullptr);
    pointer = replacement;
    for(usize index = 0u; index < s_OddBytes; ++index)
        EXPECT_EQ(pointer[index], static_cast<u8>(index + 1u));
    EXPECT_EQ(arena.memoryStats().usedBytes, CoreMsize(pointer));
    EXPECT_EQ(OwnerStats(s_Owner).usedBytes, CoreMsize(pointer));

    replacement = static_cast<u8*>(arena.reallocate(pointer, 1u, 7u));
    ASSERT_NE(replacement, nullptr);
    pointer = replacement;
    EXPECT_EQ(arena.memoryStats().usedBytes, CoreMsize(pointer));
    const ArenaMemoryStats beforeFailure = arena.memoryStats();
    // Match the backend's caller-visible error contract.
    // The Windows dbg executable and allocator DLL can use distinct CRT errno domains even though the backend rejects invalid alignment with EINVAL.
    errno = ERANGE;
    void* backendInvalidAlignment = scalable_aligned_realloc(pointer, 0u, 3u);
    const i32 backendInvalidAlignmentError = errno;
    errno = ERANGE;
    void* invalidAlignment = arena.reallocate(pointer, 3u, 0u);
    const i32 invalidAlignmentError = errno;
    EXPECT_EQ(invalidAlignment, nullptr);
    EXPECT_EQ(invalidAlignment, backendInvalidAlignment);
    EXPECT_EQ(invalidAlignmentError, backendInvalidAlignmentError);
    EXPECT_EQ(arena.reallocate(pointer, 1u, Limit<usize>::s_Max), nullptr);
    EXPECT_EQ(arena.memoryStats().usedBytes, beforeFailure.usedBytes);
    EXPECT_EQ(arena.memoryStats().reservedBytes, beforeFailure.reservedBytes);
    EXPECT_EQ(arena.memoryStats().reallocationCount, beforeFailure.reallocationCount);
    EXPECT_EQ(arena.memoryStats().deallocationCount, beforeFailure.deallocationCount);
    EXPECT_EQ(pointer[0u], 1u);
    arena.deallocate(pointer, 1u, 7u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(arena.memoryStats().reservedBytes, 0u);
    EXPECT_EQ(OwnerStats(s_Owner).usedBytes, 0u);
    EXPECT_EQ(OwnerStats(s_Owner).reservedBytes, 0u);
    EXPECT_EQ(arena.memoryStats().allocationCount, 1u);
    EXPECT_EQ(arena.memoryStats().reallocationCount, s_ExpectedDualCount);
    EXPECT_EQ(arena.memoryStats().deallocationCount, 1u);
}

TEST(AllocatorUsageBoundaries, ZeroSizeReallocationReleasesChargedBytes){
    constexpr Name s_Owner("tests/allocation_owners/object_and_zero");
    GlobalArena arena(s_Owner);
    void* allocation = arena.allocate(64u, 13u);
    ASSERT_NE(allocation, nullptr);
    EXPECT_EQ(arena.reallocate(allocation, 64u, 0u), nullptr);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(arena.memoryStats().reservedBytes, 0u);
    EXPECT_EQ(arena.memoryStats().allocationCount, 1u);
    EXPECT_EQ(arena.memoryStats().deallocationCount, 1u);
    EXPECT_EQ(OwnerStats(s_Owner).usedBytes, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

