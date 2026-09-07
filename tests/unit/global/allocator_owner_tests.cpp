// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/module.h>

#include <global/arena_memory.h>
#include <global/thread.h>
#include <global/timer.h>
#include <global/text_utils.h>

#include <cerrno>
#include <gtest/gtest.h>
#include <tbb/scalable_allocator.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_allocator_owner_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core::Alloc;

[[nodiscard]] ArenaMemoryStats OwnerStats(const Name& name, const ArenaMemorySource::Enum source = ArenaMemorySource::Arena){
    const ArenaMemoryOwnerRecord* record = FirstArenaMemoryOwnerRecord();
    while(record){
        ArenaMemoryOwnerSnapshot snapshot;
        record = ReadArenaMemoryOwnerRecord(*record, snapshot);
        if(snapshot.ownerName == name && snapshot.source == source)
            return snapshot.stats;
    }
    return {};
}

TEST(AllocationOwners, GlobalArenaChargesUsableBytesAcrossOddSizeReallocations){
    constexpr Name s_Owner("tests/allocation_owners/odd_reallocation");
    GlobalArena arena(s_Owner);
    auto* pointer = static_cast<u8*>(arena.allocate(1u, 13u));
    ASSERT_NE(pointer, nullptr);
    for(usize index = 0u; index < 13u; ++index)
        pointer[index] = static_cast<u8>(index + 1u);
    EXPECT_EQ(arena.memoryStats().usedBytes, CoreMsize(pointer));
    EXPECT_EQ(arena.memoryStats().reservedBytes, CoreMsize(pointer));

    auto* replacement = static_cast<u8*>(arena.reallocate(pointer, 1u, 21u));
    ASSERT_NE(replacement, nullptr);
    pointer = replacement;
    for(usize index = 0u; index < 13u; ++index)
        EXPECT_EQ(pointer[index], static_cast<u8>(index + 1u));
    EXPECT_EQ(arena.memoryStats().usedBytes, CoreMsize(pointer));
    EXPECT_EQ(OwnerStats(s_Owner).usedBytes, CoreMsize(pointer));

    replacement = static_cast<u8*>(arena.reallocate(pointer, 1u, 7u));
    ASSERT_NE(replacement, nullptr);
    pointer = replacement;
    EXPECT_EQ(arena.memoryStats().usedBytes, CoreMsize(pointer));
    const ArenaMemoryStats beforeFailure = arena.memoryStats();
    // Match the backend's caller-visible error contract. The Windows dbg executable and allocator DLL
    // can use distinct CRT errno domains even though the backend rejects invalid alignment with EINVAL.
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
    EXPECT_EQ(arena.memoryStats().reallocationCount, 2u);
    EXPECT_EQ(arena.memoryStats().deallocationCount, 1u);
}

TEST(AllocationOwners, ObjectAndZeroSizeReallocationReleaseChargedBytes){
    struct OddObject{
        u8 bytes[13u] = {};
    };
    constexpr Name s_Owner("tests/allocation_owners/object_and_zero");
    GlobalArena arena(s_Owner);
    auto* object = static_cast<OddObject*>(arena.allocate(alignof(OddObject), sizeof(OddObject)));
    ASSERT_NE(object, nullptr);
    EXPECT_EQ(arena.memoryStats().usedBytes, CoreMsize(object));
    arena.deallocateObject(object);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);

    void* allocation = arena.allocate(64u, 13u);
    ASSERT_NE(allocation, nullptr);
    EXPECT_EQ(arena.reallocate(allocation, 64u, 0u), nullptr);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(arena.memoryStats().reservedBytes, 0u);
    EXPECT_EQ(arena.memoryStats().allocationCount, 2u);
    EXPECT_EQ(arena.memoryStats().deallocationCount, 2u);
    EXPECT_EQ(OwnerStats(s_Owner).usedBytes, 0u);
}

TEST(AllocationOwners, SharedNameAggregatesBytesAndRetainsIndividualHighwater){
    constexpr Name s_Owner("tests/allocation_owners/shared_name");
    const ArenaMemoryStats before = OwnerStats(s_Owner);
    u64 combinedBytes = 0u;
    u64 largestArenaBytes = 0u;
    {
        GlobalArena first(s_Owner);
        GlobalArena second("TESTS/ALLOCATION_OWNERS/SHARED_NAME");
        void* firstAllocation = first.allocate(1u, 13u);
        void* secondAllocation = second.allocate(1u, 21u);
        ASSERT_NE(firstAllocation, nullptr);
        ASSERT_NE(secondAllocation, nullptr);
        combinedBytes = CoreMsize(firstAllocation) + CoreMsize(secondAllocation);
        largestArenaBytes = Max(CoreMsize(firstAllocation), CoreMsize(secondAllocation));
        EXPECT_EQ(OwnerStats(s_Owner).usedBytes, before.usedBytes + combinedBytes);
        EXPECT_EQ(first.memoryStats().allocationCount, 1u);
        EXPECT_EQ(second.memoryStats().allocationCount, 1u);
        first.deallocate(firstAllocation, 1u, 13u);
        EXPECT_EQ(OwnerStats(s_Owner).usedBytes, before.usedBytes + CoreMsize(secondAllocation));
        second.deallocate(secondAllocation, 1u, 21u);
    }
    const ArenaMemoryStats after = OwnerStats(s_Owner);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_EQ(after.reservedBytes, before.reservedBytes);
    EXPECT_EQ(after.allocationCount - before.allocationCount, 2u);
    EXPECT_EQ(after.deallocationCount - before.deallocationCount, 2u);
    EXPECT_EQ(after.peakUsedBytes, Max(before.peakUsedBytes, largestArenaBytes));
}

TEST(AllocationOwners, TransientScratchBulkReleaseRetainsChurnWithoutAccumulatingPeak){
    constexpr Name s_Owner("tests/allocation_owners/transient_scratch");
    const ArenaMemoryStats before = OwnerStats(s_Owner);
    for(usize pass = 0u; pass < 3u; ++pass){
        ScratchArena arena(s_Owner);
        void* first = arena.allocate(16u, 208u);
        void* second = arena.allocate(16u, 64u);
        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);
        arena.deallocate(first, 16u, 208u);
        EXPECT_EQ(arena.memoryStats().usedBytes, 272u);
        EXPECT_EQ(OwnerStats(s_Owner).usedBytes, before.usedBytes + 272u);
    }
    const ArenaMemoryStats after = OwnerStats(s_Owner);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_EQ(after.reservedBytes, before.reservedBytes);
    EXPECT_EQ(after.allocationCount - before.allocationCount, 6u);
    EXPECT_EQ(after.deallocationCount - before.deallocationCount, 6u);
    EXPECT_EQ(after.peakUsedBytes, Max(before.peakUsedBytes, u64{ 272u }));
}

TEST(AllocationOwners, PersistentPoolBulkReleaseRetainsReallocationHistory){
    constexpr Name s_Owner("tests/allocation_owners/transient_persistent");
    const ArenaMemoryStats before = OwnerStats(s_Owner);
    {
        PersistentArena arena(s_Owner, 65536u);
        void* allocation = arena.allocate(1u, 13u);
        ASSERT_NE(allocation, nullptr);
        allocation = arena.reallocate(allocation, 1u, 67u);
        ASSERT_NE(allocation, nullptr);
        EXPECT_EQ(OwnerStats(s_Owner).reservedBytes, before.reservedBytes + 65536u);
        EXPECT_EQ(OwnerStats(s_Owner).usedBytes, before.usedBytes + arena.memoryStats().usedBytes);
    }
    const ArenaMemoryStats after = OwnerStats(s_Owner);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_EQ(after.reservedBytes, before.reservedBytes);
    EXPECT_EQ(after.allocationCount - before.allocationCount, 1u);
    EXPECT_EQ(after.reallocationCount - before.reallocationCount, 1u);
    EXPECT_EQ(after.deallocationCount - before.deallocationCount, 1u);
}

TEST(AllocationOwners, SharedNamePreservesPoolReservationAlongsideDirectHeapUsage){
    constexpr Name s_Owner("tests/allocation_owners/mixed_reservation");
    const ArenaMemoryStats before = OwnerStats(s_Owner);
    {
        PersistentArena pool(s_Owner, 65536u);
        GlobalArena direct(s_Owner);
        void* allocation = direct.allocate(1u, 13u);
        ASSERT_NE(allocation, nullptr);
        EXPECT_EQ(OwnerStats(s_Owner).usedBytes, before.usedBytes + CoreMsize(allocation));
        EXPECT_EQ(OwnerStats(s_Owner).reservedBytes, before.reservedBytes + 65536u + CoreMsize(allocation));
        void* replacement = direct.reallocate(allocation, 1u, 21u);
        ASSERT_NE(replacement, nullptr);
        allocation = replacement;
        EXPECT_EQ(direct.memoryStats().reservedBytes, CoreMsize(allocation));
        EXPECT_EQ(OwnerStats(s_Owner).reservedBytes, before.reservedBytes + 65536u + CoreMsize(allocation));
        direct.deallocate(allocation, 1u, 21u);
        EXPECT_EQ(OwnerStats(s_Owner).usedBytes, before.usedBytes);
        EXPECT_EQ(OwnerStats(s_Owner).reservedBytes, before.reservedBytes + 65536u);
    }
    EXPECT_EQ(OwnerStats(s_Owner).usedBytes, before.usedBytes);
    EXPECT_EQ(OwnerStats(s_Owner).reservedBytes, before.reservedBytes);
}

TEST(AllocationOwners, ResetPreservesRetiredCountersAndIndividualPeakExactlyOnce){
    constexpr Name s_Owner("tests/allocation_owners/reset_history");
    const ArenaMemoryStats before = OwnerStats(s_Owner);
    {
        ArenaMemoryTracker tracker(s_Owner);
        tracker.reset(512u);
        tracker.recordAllocation(64u);
        tracker.recordAllocation(128u);
        tracker.reset(256u);
        const ArenaMemoryStats resetOwner = OwnerStats(s_Owner);
        EXPECT_EQ(tracker.snapshot().allocationCount, 0u);
        EXPECT_EQ(tracker.snapshot().peakUsedBytes, 0u);
        EXPECT_EQ(resetOwner.reservedBytes, before.reservedBytes + 256u);
        EXPECT_EQ(resetOwner.usedBytes, before.usedBytes);
        EXPECT_EQ(resetOwner.allocationCount - before.allocationCount, 2u);
        EXPECT_EQ(resetOwner.deallocationCount - before.deallocationCount, 2u);
        EXPECT_EQ(resetOwner.peakUsedBytes, Max(before.peakUsedBytes, u64{ 192u }));
        tracker.recordAllocation(16u);
        tracker.reset();
        tracker.reset();
    }
    const ArenaMemoryStats after = OwnerStats(s_Owner);
    EXPECT_EQ(after.reservedBytes, before.reservedBytes);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_EQ(after.allocationCount - before.allocationCount, 3u);
    EXPECT_EQ(after.deallocationCount - before.deallocationCount, 3u);
    EXPECT_EQ(after.peakUsedBytes, Max(before.peakUsedBytes, u64{ 192u }));
}

TEST(AllocationOwners, RetiredGlobalArenaPreservesUnreleasedOwnership){
    constexpr Name s_Owner("tests/allocation_owners/retired_outstanding");
    const ArenaMemoryStats before = OwnerStats(s_Owner);
    void* allocation = nullptr;
    {
        GlobalArena arena(s_Owner);
        allocation = arena.allocate(1u, 13u);
        ASSERT_NE(allocation, nullptr);
    }
    const u64 bytes = CoreMsize(allocation);
    const ArenaMemoryStats retired = OwnerStats(s_Owner);
    CoreFree(allocation);
    EXPECT_EQ(retired.usedBytes, before.usedBytes + bytes);
    EXPECT_EQ(retired.reservedBytes, before.reservedBytes + bytes);
    EXPECT_EQ(retired.allocationCount - before.allocationCount, 1u);
    EXPECT_EQ(retired.deallocationCount, before.deallocationCount);
    EXPECT_EQ(retired.peakUsedBytes, Max(before.peakUsedBytes, bytes));
}

TEST(AllocationOwners, BackingCountsRawAlignedAllocationsAndPreservesFailedReallocation){
    const ArenaMemoryStats before = HeapBackingMemoryStats();
    auto* allocation = static_cast<u8*>(CoreAllocAligned(13u, 64u));
    const u64 bytes = allocation ? CoreMsize(allocation) : 0u;
    const ArenaMemoryStats allocated = HeapBackingMemoryStats();
    ASSERT_NE(allocation, nullptr);
    allocation[0u] = 37u;
    errno = ERANGE;
    void* backendInvalidZero = scalable_aligned_realloc(allocation, 0u, 3u);
    const i32 backendInvalidZeroError = errno;
    errno = ERANGE;
    void* backendInvalidAlignment = scalable_aligned_realloc(allocation, 21u, 0u);
    const i32 backendInvalidAlignmentError = errno;
    errno = ERANGE;
    void* invalidZero = CoreReallocAligned(allocation, 0u, 3u);
    const i32 invalidZeroError = errno;
    errno = ERANGE;
    void* invalidAlignment = CoreReallocAligned(allocation, 21u, 0u);
    const i32 invalidAlignmentError = errno;
    const ArenaMemoryStats failed = HeapBackingMemoryStats();
    EXPECT_EQ(invalidZero, nullptr);
    EXPECT_EQ(invalidZero, backendInvalidZero);
    EXPECT_EQ(invalidZeroError, backendInvalidZeroError);
    EXPECT_EQ(invalidAlignment, nullptr);
    EXPECT_EQ(invalidAlignment, backendInvalidAlignment);
    EXPECT_EQ(invalidAlignmentError, backendInvalidAlignmentError);
    EXPECT_EQ(allocation[0u], 37u);
    EXPECT_EQ(allocated.usedBytes - before.usedBytes, bytes);
    EXPECT_EQ(allocated.reservedBytes, allocated.usedBytes);
    EXPECT_EQ(failed.reservedBytes, failed.usedBytes);
    EXPECT_EQ(allocated.allocationCount - before.allocationCount, 1u);
    EXPECT_EQ(failed.usedBytes, allocated.usedBytes);
    EXPECT_EQ(failed.reallocationCount, allocated.reallocationCount);
    EXPECT_EQ(failed.deallocationCount, allocated.deallocationCount);
    const ArenaMemoryStats beforeFree = HeapBackingMemoryStats();
    CoreFreeSizeAligned(allocation, 13u);
    const ArenaMemoryStats freed = HeapBackingMemoryStats();
    EXPECT_EQ(beforeFree.usedBytes - freed.usedBytes, bytes);
    EXPECT_EQ(freed.reservedBytes, freed.usedBytes);
    EXPECT_EQ(freed.deallocationCount - beforeFree.deallocationCount, 1u);
}

TEST(AllocationOwners, RawReallocationMaintainsBackingReservationAndCounts){
    const ArenaMemoryStats before = HeapBackingMemoryStats();
    auto* allocation = static_cast<u8*>(CoreRealloc(nullptr, 13u));
    ASSERT_NE(allocation, nullptr);
    allocation[0u] = 29u;
    const u64 originalBytes = CoreMsize(allocation);
    const ArenaMemoryStats allocated = HeapBackingMemoryStats();
    auto* replacement = static_cast<u8*>(CoreRealloc(allocation, 71u));
    ASSERT_NE(replacement, nullptr);
    allocation = replacement;
    const u64 grownBytes = CoreMsize(allocation);
    const ArenaMemoryStats grown = HeapBackingMemoryStats();
    EXPECT_EQ(allocation[0u], 29u);
    const ArenaMemoryStats beforeRelease = HeapBackingMemoryStats();
    void* released = CoreRealloc(allocation, 0u);
    const ArenaMemoryStats afterRelease = HeapBackingMemoryStats();
    EXPECT_EQ(released, nullptr);
    EXPECT_EQ(allocated.usedBytes - before.usedBytes, originalBytes);
    EXPECT_EQ(allocated.allocationCount - before.allocationCount, 1u);
    EXPECT_EQ(grown.usedBytes - allocated.usedBytes, grownBytes - originalBytes);
    EXPECT_EQ(grown.reallocationCount - allocated.reallocationCount, 1u);
    EXPECT_EQ(beforeRelease.usedBytes - afterRelease.usedBytes, grownBytes);
    EXPECT_EQ(afterRelease.deallocationCount - beforeRelease.deallocationCount, 1u);
    EXPECT_EQ(allocated.reservedBytes, allocated.usedBytes);
    EXPECT_EQ(grown.reservedBytes, grown.usedBytes);
    EXPECT_EQ(afterRelease.reservedBytes, afterRelease.usedBytes);
}

TEST(AllocationOwners, BackingDomainCannotCollideWithAnArenaName){
    constexpr Name s_Owner("core/alloc/heap_backing");
    GlobalArena arena(s_Owner);
    void* allocation = arena.allocate(1u, 13u);
    ASSERT_NE(allocation, nullptr);
    const ArenaMemoryStats arenaStats = OwnerStats(s_Owner, ArenaMemorySource::Arena);
    const ArenaMemoryStats backingStats = OwnerStats(s_Owner, ArenaMemorySource::HeapBacking);
    EXPECT_EQ(arenaStats.usedBytes, CoreMsize(allocation));
    EXPECT_GE(backingStats.usedBytes, arenaStats.usedBytes);
    EXPECT_GT(backingStats.allocationCount, arenaStats.allocationCount);
    arena.deallocate(allocation, 1u, 13u);
}

TEST(AllocationOwners, ConcurrentSharedOwnerChurnPreservesTotals){
    constexpr Name s_Owner("tests/allocation_owners/concurrent");
    constexpr u32 s_ThreadCount = 4u;
    constexpr u32 s_AllocationCount = 8192u;
    const ArenaMemoryStats before = OwnerStats(s_Owner);
    Latch startGate(s_ThreadCount + 1u);
    Atomic<bool> failed{ false };
    Thread workers[s_ThreadCount];
    for(Thread& worker : workers){
        worker = Thread([&startGate, &failed, &s_Owner](){
            GlobalArena arena(s_Owner);
            startGate.arrive_and_wait();
            for(u32 index = 0u; index < s_AllocationCount; ++index){
                void* allocation = arena.allocate(1u, 128u);
                if(!allocation){
                    failed.store(true, MemoryOrder::relaxed);
                    return;
                }
                arena.deallocate(allocation, 1u, 128u);
            }
        });
    }
    const Timer begin = TimerNow();
    startGate.arrive_and_wait();
    for(Thread& worker : workers)
        worker.join();
    const u64 elapsed = DurationInNS<u64>(TimerNow(), begin);
    const ArenaMemoryStats after = OwnerStats(s_Owner);
    EXPECT_FALSE(failed.load(MemoryOrder::relaxed));
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_EQ(after.reservedBytes, before.reservedBytes);
    EXPECT_EQ(after.allocationCount - before.allocationCount, s_ThreadCount * s_AllocationCount);
    EXPECT_EQ(after.deallocationCount - before.deallocationCount, s_ThreadCount * s_AllocationCount);
    EXPECT_EQ(after.peakUsedBytes, Max(before.peakUsedBytes, u64{ 128u }));
    char durationText[32u] = {};
    RecordProperty("shared_owner_churn_ns", FormatDecimal(elapsed, durationText).data());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

