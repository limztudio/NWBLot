// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/module.h>

#include <global/arena_memory.h>
#include <global/sync.h>
#include <global/thread.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_allocator_owner_concurrency_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core::Alloc;

[[nodiscard]] ArenaMemoryStats OwnerStats(const Name& name){
    const ArenaMemoryOwnerRecord* record = FirstArenaMemoryOwnerRecord();
    while(record){
        ArenaMemoryOwnerSnapshot snapshot;
        record = ReadArenaMemoryOwnerRecord(*record, snapshot);
        if(snapshot.ownerName == name && snapshot.source == ArenaMemorySource::Arena)
            return snapshot.stats;
    }
    return {};
}

void WaitForFlag(const Atomic<bool>& flag){
    AtomicBackOff backoff;
    while(!flag.load(MemoryOrder::acquire))
        backoff.pause();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AllocationOwnerConcurrency, CapturesLiveAndRetiringArenasWithoutLosingHistory){
    static constexpr Name s_Owner("tests/allocation_owners/concurrent_lifetimes");
    constexpr usize s_ThreadCount = 4u;
    constexpr usize s_Iterations = 256u;
    constexpr u64 s_Allocations = s_ThreadCount * s_Iterations * 5u;
    const usize poolBytes = PersistentArena::StructureAlignedSize(4096u);
    const ArenaMemoryStats before = OwnerStats(s_Owner);
    Atomic<bool> failed{ false };
    Atomic<bool> releaseFirstAllocations{ false };
    Atomic<usize> completed{ 0u };
    Latch firstAllocationsReady(s_ThreadCount);
    Thread workers[s_ThreadCount];

    for(Thread& worker : workers){
        worker = Thread([&failed, &releaseFirstAllocations, &completed, &firstAllocationsReady, poolBytes](){
            for(usize iteration = 0u; iteration < s_Iterations; ++iteration){
                {
                    GlobalArena arena(s_Owner);
                    auto* allocation = static_cast<u8*>(arena.allocate(1u, 128u));
                    if(allocation){
                        allocation[0u] = 73u;
                        auto* const replacement = static_cast<u8*>(arena.reallocate(allocation, 1u, 256u));
                        if(replacement)
                            allocation = replacement;
                        else
                            failed.store(true, MemoryOrder::relaxed);
                        if(allocation[0u] != 73u)
                            failed.store(true, MemoryOrder::relaxed);
                    }
                    else
                        failed.store(true, MemoryOrder::relaxed);

                    if(iteration == 0u){
                        firstAllocationsReady.count_down();
                        WaitForFlag(releaseFirstAllocations);
                    }
                    arena.deallocate(allocation, 1u, 256u);
                }
                {
                    ScratchArena arena(s_Owner);
                    void* const first = arena.allocate(8u, 64u);
                    void* const second = arena.allocate(8u, 128u);
                    if(!first || !second)
                        failed.store(true, MemoryOrder::relaxed);
                    if(first)
                        arena.deallocate(first, 8u, 64u);
                    // The out-of-order free retains the first allocation; destruction releases both together.
                }
                {
                    PersistentArena arena(s_Owner, poolBytes);
                    void* const first = arena.allocate(8u, 64u);
                    void* const second = arena.allocate(8u, 128u);
                    if(!first || !second)
                        failed.store(true, MemoryOrder::relaxed);
                    if(first)
                        arena.deallocate(first, 8u, 64u);
                    // One explicit free and one bulk free must contribute exactly two retired deallocations.
                }
            }
            completed.fetch_add(1u, MemoryOrder::release);
        });
    }

    firstAllocationsReady.wait();
    const ArenaMemoryStats simultaneouslyLive = OwnerStats(s_Owner);
    releaseFirstAllocations.store(true, MemoryOrder::release);
    ArenaMemoryStats previous = simultaneouslyLive;
    bool snapshotsConsistent = true;
    do{
        const ArenaMemoryStats current = OwnerStats(s_Owner);
        snapshotsConsistent &= current.allocationCount >= previous.allocationCount;
        snapshotsConsistent &= current.reallocationCount >= previous.reallocationCount;
        snapshotsConsistent &= current.deallocationCount >= previous.deallocationCount;
        snapshotsConsistent &= current.allocationCount - before.allocationCount <= s_Allocations;
        snapshotsConsistent &= current.deallocationCount - before.deallocationCount <= s_Allocations;
        snapshotsConsistent &= current.usedBytes <= s_ThreadCount * 256u;
        snapshotsConsistent &= current.reservedBytes <= s_ThreadCount * poolBytes;
        snapshotsConsistent &= current.peakUsedBytes <= Max<u64>(before.peakUsedBytes, 256u);
        previous = current;
        YieldThread();
    }while(completed.load(MemoryOrder::acquire) != s_ThreadCount);
    for(Thread& worker : workers)
        worker.join();

    const ArenaMemoryStats after = OwnerStats(s_Owner);
    EXPECT_FALSE(failed.load(MemoryOrder::relaxed));
    EXPECT_TRUE(snapshotsConsistent);
    EXPECT_EQ(simultaneouslyLive.usedBytes, s_ThreadCount * 256u);
    EXPECT_EQ(simultaneouslyLive.reservedBytes, simultaneouslyLive.usedBytes);
    EXPECT_EQ(simultaneouslyLive.peakUsedBytes, Max<u64>(before.peakUsedBytes, 256u));
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_EQ(after.reservedBytes, before.reservedBytes);
    EXPECT_EQ(after.allocationCount - before.allocationCount, s_Allocations);
    EXPECT_EQ(after.deallocationCount - before.deallocationCount, s_Allocations);
    EXPECT_EQ(after.reallocationCount - before.reallocationCount, s_ThreadCount * s_Iterations);
    EXPECT_EQ(after.peakUsedBytes, Max<u64>(before.peakUsedBytes, 256u));
}

TEST(AllocationOwnerConcurrency, CrossThreadRawReallocationAndFreeKeepSettledBackingTotals){
    constexpr usize s_AllocationCount = 64u;
    u8* allocations[s_AllocationCount] = {};
    u64 allocatedBytes = 0u;
    Atomic<bool> failed{ false };
    Atomic<bool> producerReady{ false };
    Atomic<bool> consumerReady{ false };
    Atomic<bool> startProducer{ false };
    Atomic<bool> startConsumer{ false };
    Atomic<bool> produced{ false };
    Atomic<bool> consumed{ false };
    Atomic<bool> releaseWorkers{ false };

    Thread producer([&](){
        void* const warmup = CoreAlloc(1u);
        CoreFree(warmup);
        producerReady.store(true, MemoryOrder::release);
        WaitForFlag(startProducer);
        for(usize index = 0u; index < s_AllocationCount; ++index){
            auto* const allocation = static_cast<u8*>(CoreAlloc(13u));
            allocations[index] = allocation;
            if(!allocation){
                failed.store(true, MemoryOrder::relaxed);
                continue;
            }
            allocatedBytes += CoreMsize(allocation);
            allocation[0u] = static_cast<u8>(index);
            allocation[12u] = static_cast<u8>(index + 1u);
        }
        produced.store(true, MemoryOrder::release);
        WaitForFlag(releaseWorkers);
    });
    Thread consumer([&](){
        void* const warmup = CoreAlloc(1u);
        CoreFree(warmup);
        consumerReady.store(true, MemoryOrder::release);
        WaitForFlag(startConsumer);
        for(usize index = 0u; index < s_AllocationCount; ++index){
            u8* allocation = allocations[index];
            if(!allocation)
                continue;
            auto* const replacement = static_cast<u8*>(CoreRealloc(allocation, 71u));
            if(replacement)
                allocation = replacement;
            else
                failed.store(true, MemoryOrder::relaxed);
            if(allocation[0u] != static_cast<u8>(index) || allocation[12u] != static_cast<u8>(index + 1u))
                failed.store(true, MemoryOrder::relaxed);
            CoreFree(allocation);
        }
        consumed.store(true, MemoryOrder::release);
        WaitForFlag(releaseWorkers);
    });

    // Initialize each shard before baseline capture and keep both thread closures alive until final capture.
    // The interval contains only the planned raw allocation/reallocation/free operations, with no test assertions.
    WaitForFlag(producerReady);
    WaitForFlag(consumerReady);
    const ArenaMemoryStats before = HeapBackingMemoryStats();
    startProducer.store(true, MemoryOrder::release);
    WaitForFlag(produced);
    const ArenaMemoryStats held = HeapBackingMemoryStats();
    startConsumer.store(true, MemoryOrder::release);
    WaitForFlag(consumed);
    const ArenaMemoryStats settled = HeapBackingMemoryStats();
    releaseWorkers.store(true, MemoryOrder::release);
    producer.join();
    consumer.join();
    const ArenaMemoryStats retired = HeapBackingMemoryStats();

    EXPECT_FALSE(failed.load(MemoryOrder::relaxed));
    EXPECT_EQ(held.usedBytes - before.usedBytes, allocatedBytes);
    EXPECT_EQ(held.reservedBytes, held.usedBytes);
    EXPECT_GE(held.peakUsedBytes, held.usedBytes);
    EXPECT_EQ(held.allocationCount - before.allocationCount, s_AllocationCount);
    EXPECT_EQ(settled.usedBytes, before.usedBytes);
    EXPECT_EQ(settled.reservedBytes, settled.usedBytes);
    EXPECT_EQ(settled.allocationCount - before.allocationCount, s_AllocationCount);
    EXPECT_EQ(settled.reallocationCount - before.reallocationCount, s_AllocationCount);
    EXPECT_EQ(settled.deallocationCount - before.deallocationCount, s_AllocationCount);
    EXPECT_GE(settled.peakUsedBytes, held.peakUsedBytes);
    EXPECT_GE(retired.allocationCount, settled.allocationCount);
    EXPECT_GE(retired.reallocationCount, settled.reallocationCount);
    EXPECT_GE(retired.deallocationCount, settled.deallocationCount);
    EXPECT_GE(retired.peakUsedBytes, settled.peakUsedBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

