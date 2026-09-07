// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/scratch.h>

#include <global/containers.h>
#include <global/exception.h>
#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scratch_reuse_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Core::Alloc::ScratchArena;

struct AllocationRequest{
    usize alignment;
    usize bytes;
};

struct RepeatedSample{
    u64 elapsed = 0u;
    ArenaMemoryStats memory;
    ArenaMemoryStats heap;
};

template<usize Count>
[[nodiscard]] static bool RunAllocationBatch(ScratchArena& arena, const AllocationRequest (&requests)[Count]){
    u8* allocations[Count] = {};
    usize prepared = 0u;
    bool valid = true;
    for(; prepared < Count; ++prepared){
        const AllocationRequest& request = requests[prepared];
        auto* allocation = static_cast<u8*>(arena.allocate(request.alignment, request.bytes));
        if(!allocation){
            valid = false;
            break;
        }
        allocations[prepared] = allocation;
        valid &= reinterpret_cast<usize>(allocation) % request.alignment == 0u;
        allocation[0u] = static_cast<u8>(prepared + 1u);
        allocation[request.bytes - 1u] = static_cast<u8>(prepared + 17u);
    }
    while(prepared != 0u){
        const usize index = --prepared;
        const AllocationRequest& request = requests[index];
        u8* allocation = allocations[index];
        valid &= allocation[0u] == static_cast<u8>(index + 1u);
        valid &= allocation[request.bytes - 1u] == static_cast<u8>(index + 17u);
        arena.deallocate(allocation, request.alignment, request.bytes);
    }
    return valid;
}

// Public container operations model collecting and sorting transient work while a caller retains earlier storage.
[[nodiscard]] static bool RunScopedCollection(ScratchArena& arena, const usize count){
    Vector<u64, ScratchArena> keys(arena);
    keys.resize(count);
    for(usize index = 0u; index < count; ++index)
        keys[index] = static_cast<u64>(count - index - 1u);
    Vector<u64, ScratchArena> payload(arena);
    payload.resize(count * 3u);
    for(usize index = 0u; index < payload.size(); ++index)
        payload[index] = static_cast<u64>(index * 7u + 3u);
    {
        Vector<usize, ScratchArena> ordinals(arena);
        ordinals.resize(count);
        for(usize index = 0u; index < count; ++index)
            ordinals[index] = index;
        Sort(keys.begin(), keys.end());
        AString<ScratchArena> path(arena);
        path.reserve(96u);
        path += "project/fixtures/transient_collection/";
        path += "resolved_asset_name_with_a_nontrivial_path_length";
        if(path.size() <= 32u)
            return false;
        for(usize index = 0u; index < count; ++index){
            if(keys[index] != ordinals[index] || payload[index * 3u] != static_cast<u64>(index * 21u + 3u))
                return false;
        }
    }
    return true;
}

static void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

static void RecordSeries(const Array<RepeatedSample, 5u>& samples){
    constexpr Array<usize, 5u> s_CallCounts{ 1u, 2u, 4u, 8u, 16u };
    constexpr Array<NotNull<const char*>, 5u> s_UsedKeys{
        MakeNotNull("scratch_used_after_1"), MakeNotNull("scratch_used_after_2"), MakeNotNull("scratch_used_after_4"),
        MakeNotNull("scratch_used_after_8"), MakeNotNull("scratch_used_after_16"),
    };
    constexpr Array<NotNull<const char*>, 5u> s_ReservedKeys{
        MakeNotNull("scratch_reserved_after_1"), MakeNotNull("scratch_reserved_after_2"), MakeNotNull("scratch_reserved_after_4"),
        MakeNotNull("scratch_reserved_after_8"), MakeNotNull("scratch_reserved_after_16"),
    };
    for(usize index = 0u; index < samples.size(); ++index){
        RecordUnsignedProperty(s_UsedKeys[index], samples[index].memory.usedBytes);
        RecordUnsignedProperty(s_ReservedKeys[index], samples[index].memory.reservedBytes);
    }
    RecordUnsignedProperty(MakeNotNull("scratch_reuse_ns"), samples.back().elapsed);
    RecordUnsignedProperty(MakeNotNull("scratch_reuse_calls"), s_CallCounts.back());
    RecordUnsignedProperty(MakeNotNull("scratch_reuse_peak_bytes"), samples.back().memory.peakUsedBytes);
    RecordUnsignedProperty(MakeNotNull("scratch_heap_allocations_after_first"),
        samples.back().heap.allocationCount - samples.front().heap.allocationCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ScratchArenaReuse, ReverseFreesCrossChunkBoundariesAndReuseAllEmptyChunks){
    ScratchArena arena(Name("tests/scratch_reuse/cross_chunk"), 256u);
    constexpr AllocationRequest s_Requests[]{ { 8u, 192u }, { 8u, 128u }, { 8u, 768u } };
    ASSERT_TRUE(RunAllocationBatch(arena, s_Requests));
    const ArenaMemoryStats warm = arena.memoryStats();
    EXPECT_EQ(warm.usedBytes, 0u);
    EXPECT_EQ(warm.allocationCount, 3u);
    EXPECT_EQ(warm.deallocationCount, 3u);
    EXPECT_EQ(warm.peakUsedBytes, 1088u);
    for(usize iteration = 0u; iteration < 32u; ++iteration){
        ASSERT_TRUE(RunAllocationBatch(arena, s_Requests));
        const ArenaMemoryStats current = arena.memoryStats();
        EXPECT_EQ(current.usedBytes, 0u);
        EXPECT_EQ(current.reservedBytes, warm.reservedBytes);
        EXPECT_EQ(current.allocationCount, (iteration + 2u) * 3u);
        EXPECT_EQ(current.deallocationCount, current.allocationCount);
    }
}

TEST(ScratchArenaReuse, AlignmentBucketsKeepIndependentLifoStacksAndCallerSentinels){
    ScratchArena arena(Name("tests/scratch_reuse/alignments"), 256u);
    constexpr usize s_Alignments[]{ 1u, 8u, 64u, 256u };
    u8* sentinels[LengthOf(s_Alignments)] = {};
    for(usize index = 0u; index < LengthOf(s_Alignments); ++index){
        sentinels[index] = static_cast<u8*>(arena.allocate(s_Alignments[index], 64u));
        ASSERT_NE(sentinels[index], nullptr);
        sentinels[index][0u] = static_cast<u8>(index + 71u);
        sentinels[index][63u] = static_cast<u8>(index + 83u);
    }
    const ArenaMemoryStats retained = arena.memoryStats();
    constexpr AllocationRequest s_Requests[]{
        { 1u, 256u }, { 8u, 256u }, { 64u, 256u }, { 256u, 256u },
        { 1u, 1536u }, { 8u, 1536u }, { 64u, 1536u }, { 256u, 1536u },
    };
    for(usize iteration = 0u; iteration < 16u; ++iteration){
        ASSERT_TRUE(RunAllocationBatch(arena, s_Requests));
        EXPECT_EQ(arena.memoryStats().usedBytes, retained.usedBytes);
        for(usize index = 0u; index < LengthOf(s_Alignments); ++index){
            EXPECT_EQ(sentinels[index][0u], static_cast<u8>(index + 71u));
            EXPECT_EQ(sentinels[index][63u], static_cast<u8>(index + 83u));
        }
    }
    // The alignment buckets are independent, so global reverse order is not required here.
    for(usize index = 0u; index < LengthOf(s_Alignments); ++index)
        arena.deallocate(sentinels[index], s_Alignments[index], 64u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(arena.memoryStats().allocationCount, arena.memoryStats().deallocationCount);
}

TEST(ScratchArenaReuse, OutOfOrderFreeRemainsANoopUntilThatAllocationBecomesTop){
    ScratchArena arena(Name("tests/scratch_reuse/out_of_order"), 256u);
    auto* first = static_cast<u8*>(arena.allocate(8u, 96u));
    auto* second = static_cast<u8*>(arena.allocate(8u, 96u));
    auto* third = static_cast<u8*>(arena.allocate(8u, 192u));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(third, nullptr);
    first[0u] = 17u;
    first[95u] = 29u;
    const ArenaMemoryStats before = arena.memoryStats();
    arena.deallocate(first, 8u, 96u);
    EXPECT_EQ(arena.memoryStats().usedBytes, before.usedBytes);
    EXPECT_EQ(arena.memoryStats().deallocationCount, before.deallocationCount);
    arena.deallocate(third, 8u, 192u);
    arena.deallocate(second, 8u, 96u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 96u);
    EXPECT_EQ(first[0u], 17u);
    EXPECT_EQ(first[95u], 29u);
    arena.deallocate(first, 8u, 96u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(arena.memoryStats().deallocationCount, 3u);
}

TEST(ScratchArenaReuse, RelocatedAndZeroSizedTopReallocationsExposeThePreviousLiveChunk){
    ScratchArena arena(Name("tests/scratch_reuse/relocation"), 256u);
    auto* sentinel = static_cast<u8*>(arena.allocate(8u, 64u));
    auto* allocation = static_cast<u8*>(arena.allocate(8u, 128u));
    ASSERT_NE(sentinel, nullptr);
    ASSERT_NE(allocation, nullptr);
    sentinel[0u] = 41u;
    for(usize index = 0u; index < 128u; ++index)
        allocation[index] = static_cast<u8>(index);
    auto* grown = static_cast<u8*>(arena.reallocate(allocation, 8u, 1024u));
    ASSERT_NE(grown, nullptr);
    EXPECT_NE(grown, allocation);
    for(usize index = 0u; index < 128u; ++index)
        EXPECT_EQ(grown[index], static_cast<u8>(index));
    EXPECT_EQ(arena.memoryStats().usedBytes, 1088u);
    EXPECT_EQ(arena.memoryStats().allocationCount, 3u);
    EXPECT_EQ(arena.memoryStats().deallocationCount, 1u);
    EXPECT_EQ(arena.reallocate(grown, 8u, 256u), grown);
    EXPECT_EQ(arena.memoryStats().reallocationCount, 1u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 320u);
    // Scratch's existing zero-size top resize returns its former address; that address is no longer live.
    EXPECT_EQ(arena.reallocate(grown, 8u, 0u), grown);
    EXPECT_EQ(arena.memoryStats().usedBytes, 64u);
    EXPECT_EQ(sentinel[0u], 41u);
    arena.deallocate(sentinel, 8u, 64u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(arena.memoryStats().deallocationCount, 3u);

    auto* sole = static_cast<u8*>(arena.allocate(8u, 128u));
    ASSERT_NE(sole, nullptr);
    sole[0u] = 97u;
    auto* moved = static_cast<u8*>(arena.reallocate(sole, 8u, 8192u));
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(moved[0u], 97u);
    arena.deallocate(moved, 8u, 8192u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
    constexpr AllocationRequest s_AfterRelocation[]{ { 8u, 128u }, { 8u, 8192u } };
    ASSERT_TRUE(RunAllocationBatch(arena, s_AfterRelocation));
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}

TEST(ScratchArenaReuse, ZeroByteOperationsRemainValidBeforeAndAfterCachingTheLastChunk){
    ScratchArena arena(Name("tests/scratch_reuse/zero_bytes"), 256u);
    EXPECT_EQ(arena.allocate<u8>(0u), nullptr);
    EXPECT_EQ(arena.memoryStats().reservedBytes, 0u);
    void* zero = arena.allocate(8u, 0u);
    ASSERT_NE(zero, nullptr);
    arena.deallocate(zero, 8u, 0u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(arena.memoryStats().allocationCount, 0u);
    auto* allocation = static_cast<u8*>(arena.allocate(8u, 128u));
    ASSERT_NE(allocation, nullptr);
    allocation[0u] = 43u;
    void* zeroAtTop = arena.allocate(8u, 0u);
    ASSERT_NE(zeroAtTop, nullptr);
    arena.deallocate(zeroAtTop, 8u, 0u);
    EXPECT_EQ(allocation[0u], 43u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 128u);
    arena.deallocate(allocation, 8u, 128u);
    arena.deallocate(zeroAtTop, 8u, 0u);
    const ArenaMemoryStats cached = arena.memoryStats();
    EXPECT_EQ(cached.usedBytes, 0u);
    void* reusedZero = arena.allocate(8u, 0u);
    ASSERT_NE(reusedZero, nullptr);
    arena.deallocate(reusedZero, 8u, 0u);
    EXPECT_EQ(arena.memoryStats().reservedBytes, cached.reservedBytes);
    EXPECT_EQ(arena.memoryStats().allocationCount, cached.allocationCount);
    EXPECT_EQ(arena.memoryStats().deallocationCount, cached.deallocationCount);
    void* resized = arena.allocate(8u, 64u);
    ASSERT_NE(resized, nullptr);
    EXPECT_EQ(arena.reallocate(resized, 8u, 0u), resized);
    arena.deallocate(resized, 8u, 0u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(arena.memoryStats().allocationCount, 2u);
    EXPECT_EQ(arena.memoryStats().deallocationCount, 2u);
}

TEST(ScratchArenaReuse, RelocationReusesCachedBackingAndUnlinksAnEmptiedFormerTop){
    ScratchArena arena(Name("tests/scratch_reuse/cached_relocation"), 256u);
    auto* sentinel = static_cast<u8*>(arena.allocate(8u, 64u));
    auto* allocation = static_cast<u8*>(arena.allocate(8u, 128u));
    void* cached = arena.allocate(8u, 1024u);
    ASSERT_NE(sentinel, nullptr);
    ASSERT_NE(allocation, nullptr);
    ASSERT_NE(cached, nullptr);
    sentinel[0u] = 79u;
    for(usize index = 0u; index < 128u; ++index)
        allocation[index] = static_cast<u8>(index + 11u);
    arena.deallocate(cached, 8u, 1024u);
    const ArenaMemoryStats beforeHeap = HeapBackingMemoryStats();
    auto* relocated = static_cast<u8*>(arena.reallocate(allocation, 8u, 1024u));
    const ArenaMemoryStats afterHeap = HeapBackingMemoryStats();
    ASSERT_NE(relocated, nullptr);
    EXPECT_EQ(relocated, cached);
    EXPECT_EQ(afterHeap.allocationCount, beforeHeap.allocationCount);
    EXPECT_EQ(afterHeap.deallocationCount, beforeHeap.deallocationCount);
    for(usize index = 0u; index < 128u; ++index)
        EXPECT_EQ(relocated[index], static_cast<u8>(index + 11u));
    arena.deallocate(relocated, 8u, 1024u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 64u);
    EXPECT_EQ(sentinel[0u], 79u);
    arena.deallocate(sentinel, 8u, 64u);

    // The first reused chunk now contains only this block; relocation must remove it beneath its replacement.
    allocation = static_cast<u8*>(arena.allocate(8u, 128u));
    ASSERT_NE(allocation, nullptr);
    allocation[0u] = 101u;
    relocated = static_cast<u8*>(arena.reallocate(allocation, 8u, 1024u));
    ASSERT_NE(relocated, nullptr);
    EXPECT_EQ(relocated, cached);
    EXPECT_EQ(relocated[0u], 101u);
    arena.deallocate(relocated, 8u, 1024u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
    constexpr AllocationRequest s_Requests[]{ { 8u, 128u }, { 8u, 1024u } };
    ASSERT_TRUE(RunAllocationBatch(arena, s_Requests));
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(arena.memoryStats().allocationCount, arena.memoryStats().deallocationCount);
}

TEST(ScratchArenaReuse, FailedRelocationPreservesLiveAllocationsCachedChunksAndCounters){
    ScratchArena arena(Name("tests/scratch_reuse/failed_relocation"), 256u);
    auto* sentinel = static_cast<u8*>(arena.allocate(1u, 64u));
    auto* allocation = static_cast<u8*>(arena.allocate(1u, 128u));
    void* cached = arena.allocate(1u, 1536u);
    ASSERT_NE(sentinel, nullptr);
    ASSERT_NE(allocation, nullptr);
    ASSERT_NE(cached, nullptr);
    sentinel[0u] = 131u;
    allocation[0u] = 139u;
    allocation[127u] = 149u;
    arena.deallocate(cached, 1u, 1536u);
    const ArenaMemoryStats before = arena.memoryStats();
    EXPECT_EQ(arena.reallocate(allocation, 1u, Limit<usize>::s_Max), nullptr);
    const ArenaMemoryStats after = arena.memoryStats();
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_EQ(after.reservedBytes, before.reservedBytes);
    EXPECT_EQ(after.peakUsedBytes, before.peakUsedBytes);
    EXPECT_EQ(after.allocationCount, before.allocationCount);
    EXPECT_EQ(after.reallocationCount, before.reallocationCount);
    EXPECT_EQ(after.deallocationCount, before.deallocationCount);
    EXPECT_EQ(sentinel[0u], 131u);
    EXPECT_EQ(allocation[0u], 139u);
    EXPECT_EQ(allocation[127u], 149u);
    void* reused = arena.allocate(1u, 1536u);
    ASSERT_NE(reused, nullptr);
    EXPECT_EQ(reused, cached);
    arena.deallocate(reused, 1u, 1536u);
    arena.deallocate(allocation, 1u, 128u);
    arena.deallocate(sentinel, 1u, 64u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}

TEST(ScratchArenaReuse, LargeSmallLargeRequestsDoNotRecreateUndersizedCachedChunks){
    ScratchArena arena(Name("tests/scratch_reuse/mixed_sizes"), 256u);
    constexpr AllocationRequest s_SmallFirst[]{ { 8u, 64u }, { 8u, 8192u }, { 8u, 96u }, { 8u, 512u } };
    constexpr AllocationRequest s_LargeFirst[]{ { 8u, 8192u }, { 8u, 96u }, { 8u, 64u }, { 8u, 8192u } };
    for(usize iteration = 0u; iteration < 3u; ++iteration){
        ASSERT_TRUE(RunAllocationBatch(arena, s_SmallFirst));
        ASSERT_TRUE(RunAllocationBatch(arena, s_LargeFirst));
    }
    const ArenaMemoryStats warm = arena.memoryStats();
    const ArenaMemoryStats beforeHeap = HeapBackingMemoryStats();
    bool valid = true;
    for(usize iteration = 0u; iteration < 64u; ++iteration){
        valid &= RunAllocationBatch(arena, s_SmallFirst);
        valid &= RunAllocationBatch(arena, s_LargeFirst);
    }
    const ArenaMemoryStats afterHeap = HeapBackingMemoryStats();
    EXPECT_TRUE(valid);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(arena.memoryStats().reservedBytes, warm.reservedBytes);
    EXPECT_EQ(afterHeap.allocationCount, beforeHeap.allocationCount);
    EXPECT_EQ(afterHeap.deallocationCount, beforeHeap.deallocationCount);
}

TEST(ScratchArenaReuse, NestedContainerDestructionAndCallerUnwindPreserveEarlierStorage){
    ScratchArena arena(Name("tests/scratch_reuse/nested_containers"), 256u);
    Vector<u64, ScratchArena> caller(arena);
    caller.assign(48u, 0x12345678u);
    const ArenaMemoryStats retained = arena.memoryStats();
    ASSERT_TRUE(RunScopedCollection(arena, 1024u));
    const ArenaMemoryStats warm = arena.memoryStats();
    EXPECT_EQ(warm.usedBytes, retained.usedBytes);
    for(usize iteration = 0u; iteration < 16u; ++iteration){
        ASSERT_TRUE(RunScopedCollection(arena, 1024u));
        EXPECT_EQ(arena.memoryStats().usedBytes, retained.usedBytes);
        EXPECT_EQ(arena.memoryStats().reservedBytes, warm.reservedBytes);
    }
    EXPECT_THROW(([&](){
        Vector<u64, ScratchArena> outer(arena);
        outer.resize(2048u, 23u);
        Vector<u64, ScratchArena> inner(arena);
        inner.resize(4096u, 47u);
        throw RuntimeException("scratch nested scope unwind");
    })(), RuntimeException);
    EXPECT_EQ(arena.memoryStats().usedBytes, retained.usedBytes);
    for(const u64 value : caller)
        EXPECT_EQ(value, 0x12345678u);
}

TEST(ScratchArenaReuse, OwnerTelemetryRetainsHistoryAndSeparatesReservedBackingFromUsage){
    constexpr Name s_Owner("tests/scratch_reuse/owner_lifecycle");
    ArenaMemoryStats before;
    const ArenaMemoryOwnerRecord* record = FirstArenaMemoryOwnerRecord();
    while(record){
        ArenaMemoryOwnerSnapshot owner;
        record = ReadArenaMemoryOwnerRecord(*record, owner);
        if(owner.ownerName == s_Owner && owner.source == ArenaMemorySource::Arena){
            before = owner.stats;
            break;
        }
    }
    ArenaMemoryStats live;
    {
        ScratchArena arena(s_Owner, 256u);
        constexpr AllocationRequest s_Requests[]{ { 8u, 192u }, { 8u, 128u }, { 8u, 768u } };
        ASSERT_TRUE(RunAllocationBatch(arena, s_Requests));
        live = arena.memoryStats();
        EXPECT_EQ(live.usedBytes, 0u);
        EXPECT_GT(live.reservedBytes, 0u);
        bool found = false;
        record = FirstArenaMemoryOwnerRecord();
        while(record){
            ArenaMemoryOwnerSnapshot owner;
            record = ReadArenaMemoryOwnerRecord(*record, owner);
            if(owner.ownerName != s_Owner || owner.source != ArenaMemorySource::Arena)
                continue;
            found = true;
            EXPECT_EQ(owner.stats.usedBytes, 0u);
            EXPECT_EQ(owner.stats.reservedBytes, live.reservedBytes);
            EXPECT_EQ(owner.stats.allocationCount, before.allocationCount + live.allocationCount);
            EXPECT_EQ(owner.stats.deallocationCount, before.deallocationCount + live.deallocationCount);
            break;
        }
        EXPECT_TRUE(found);
    }
    bool found = false;
    record = FirstArenaMemoryOwnerRecord();
    while(record){
        ArenaMemoryOwnerSnapshot owner;
        record = ReadArenaMemoryOwnerRecord(*record, owner);
        if(owner.ownerName != s_Owner || owner.source != ArenaMemorySource::Arena)
            continue;
        found = true;
        EXPECT_EQ(owner.stats.usedBytes, 0u);
        EXPECT_EQ(owner.stats.reservedBytes, 0u);
        EXPECT_EQ(owner.stats.allocationCount, before.allocationCount + live.allocationCount);
        EXPECT_EQ(owner.stats.deallocationCount, before.deallocationCount + live.deallocationCount);
        EXPECT_EQ(owner.stats.peakUsedBytes, Max(before.peakUsedBytes, live.peakUsedBytes));
        break;
    }
    EXPECT_TRUE(found);
}

TEST(ScratchArenaReuseBenchmark, DISABLED_RepeatedScopedCollection4096){
    ScratchArena arena(Name("tests/scratch_reuse/collection_series"), 256u);
    Vector<u64, ScratchArena> caller(arena);
    caller.assign(32u, 0x76543210u);
    Array<RepeatedSample, 5u> samples{};
    constexpr Array<usize, 5u> s_SampleCalls{ 1u, 2u, 4u, 8u, 16u };
    usize sampleIndex = 0u;
    u64 elapsed = 0u;
    bool valid = true;
    for(usize call = 1u; call <= s_SampleCalls.back(); ++call){
        const Timer begin = TimerNow();
        valid &= RunScopedCollection(arena, 4096u);
        elapsed += DurationInNS<u64>(TimerNow(), begin);
        if(call == s_SampleCalls[sampleIndex]){
            samples[sampleIndex] = RepeatedSample{ elapsed, arena.memoryStats(), HeapBackingMemoryStats() };
            ++sampleIndex;
        }
    }
    EXPECT_TRUE(valid);
    for(const u64 value : caller)
        EXPECT_EQ(value, 0x76543210u);
    RecordSeries(samples);
    RecordUnsignedProperty(MakeNotNull("scratch_collection_count"), 4096u);
}

TEST(ScratchArenaReuseBenchmark, DISABLED_RepeatedMixedAlignmentBatches){
    ScratchArena arena(Name("tests/scratch_reuse/alignment_series"), 256u);
    constexpr AllocationRequest s_Requests[]{
        { 1u, 192u }, { 8u, 192u }, { 64u, 192u }, { 256u, 256u },
        { 1u, 512u }, { 8u, 512u }, { 64u, 512u }, { 256u, 512u },
        { 1u, 4096u }, { 8u, 4096u }, { 64u, 4096u }, { 256u, 4096u },
    };
    Array<RepeatedSample, 5u> samples{};
    constexpr Array<usize, 5u> s_SampleCalls{ 1u, 2u, 4u, 8u, 16u };
    usize sampleIndex = 0u;
    u64 elapsed = 0u;
    bool valid = true;
    for(usize call = 1u; call <= s_SampleCalls.back(); ++call){
        const Timer begin = TimerNow();
        for(usize batch = 0u; batch < 128u; ++batch)
            valid &= RunAllocationBatch(arena, s_Requests);
        elapsed += DurationInNS<u64>(TimerNow(), begin);
        if(call == s_SampleCalls[sampleIndex]){
            samples[sampleIndex] = RepeatedSample{ elapsed, arena.memoryStats(), HeapBackingMemoryStats() };
            ++sampleIndex;
        }
    }
    EXPECT_TRUE(valid);
    RecordSeries(samples);
    RecordUnsignedProperty(MakeNotNull("scratch_batch_allocations"), LengthOf(s_Requests) * 128u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

