// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/module.h>

#include <global/basic_string.h>
#include <global/container/adaptor.h>
#include <global/containers.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_allocator_propagation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Core::Alloc::GlobalArena;

[[nodiscard]] GlobalArena& DefaultOwner(){
    static GlobalArena s_Arena(Name("tests/allocator_propagation/default"));
    return s_Arena;
}

template<typename T>
using DefaultAllocator = ContainerDetail::DefaultArenaAllocator<T, GlobalArena, DefaultOwner>;

template<typename AllocatorT>
void VerifyAllocatorAssignment(){
    static_assert(AllocatorT::propagate_on_container_move_assignment::value);
    GlobalArena sourceArena(Name("tests/allocator_propagation/direct_source"));
    GlobalArena targetArena(Name("tests/allocator_propagation/direct_target"));
    AllocatorT source(sourceArena);
    AllocatorT target(targetArena);
    EXPECT_NE(source, target);
    target = source;
    EXPECT_EQ(target.arenaPtr(), &sourceArena);
    EXPECT_EQ(target, source);
    AllocatorT replacement(targetArena);
    target = Move(replacement);
    EXPECT_EQ(target.arenaPtr(), &targetArena);
    EXPECT_EQ(target, replacement);
    AllocatorT& sameAllocator = target;
    target = sameAllocator;
    EXPECT_EQ(target.arenaPtr(), &targetArena);
    using Rebound = typename AllocatorT::template rebind<u64>::other;
    Rebound rebound(sourceArena);
    rebound = Rebound(target);
    EXPECT_EQ(rebound.arenaPtr(), &targetArena);
}

template<typename VectorT>
void VerifyVectorMove(const bool foreignArena, const usize targetSize = 1u){
    GlobalArena sourceArena(Name("tests/allocator_propagation/vector_source"));
    GlobalArena targetArena(Name("tests/allocator_propagation/vector_target"));
    {
        VectorT target{ typename VectorT::allocator_type(foreignArena ? targetArena : sourceArena) };
        for(usize index = 0u; index < targetSize; ++index)
            target.push_back(99u);
        {
            VectorT source{ typename VectorT::allocator_type(sourceArena) };
            for(u32 index = 0u; index < 128u; ++index)
                source.push_back(index);
            const u32* const firstElement = &source[0u];
            target = Move(source);
            EXPECT_EQ(target.get_allocator().arenaPtr(), &sourceArena);
            ASSERT_EQ(target.size(), 128u);
            EXPECT_EQ(&target[0u], firstElement);
            EXPECT_TRUE(source.empty());
            source.push_back(7u);
            EXPECT_EQ(source[0u], 7u);
        }
        for(u32 index = 0u; index < 128u; ++index)
            EXPECT_EQ(target[index], index);
        target.push_back(128u);
        EXPECT_EQ(target[128u], 128u);
    }
    EXPECT_EQ(sourceArena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(sourceArena.memoryStats().reservedBytes, 0u);
    EXPECT_EQ(targetArena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(targetArena.memoryStats().reservedBytes, 0u);
    EXPECT_EQ(sourceArena.memoryStats().allocationCount, sourceArena.memoryStats().deallocationCount);
    EXPECT_EQ(targetArena.memoryStats().allocationCount, targetArena.memoryStats().deallocationCount);
}

template<typename StringT>
void VerifyStringMove(const bool foreignArena, const bool shortSource){
    GlobalArena sourceArena(Name("tests/allocator_propagation/string_source"));
    GlobalArena targetArena(Name("tests/allocator_propagation/string_target"));
    {
        StringT target{ typename StringT::allocator_type(foreignArena ? targetArena : sourceArena) };
        target.assign(96u, 'z');
        {
            StringT source{ typename StringT::allocator_type(sourceArena) };
            source.assign(shortSource ? 3u : 192u, 'a');
            const char* const sourceStorage = source.data();
            target = Move(source);
            EXPECT_EQ(target.get_allocator().arenaPtr(), &sourceArena);
            EXPECT_EQ(target.size(), shortSource ? 3u : 192u);
            if(!shortSource)
                EXPECT_EQ(target.data(), sourceStorage);
            EXPECT_TRUE(source.empty());
            source.assign(48u, 'b');
        }
        for(const char character : target)
            EXPECT_EQ(character, 'a');
        target.append(257u, 'c');
        EXPECT_EQ(target.back(), 'c');
    }
    EXPECT_EQ(sourceArena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(targetArena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(sourceArena.memoryStats().allocationCount, sourceArena.memoryStats().deallocationCount);
    EXPECT_EQ(targetArena.memoryStats().allocationCount, targetArena.memoryStats().deallocationCount);
}


TEST(AllocatorPropagation, DirectAssignmentMatchesAdvertisedMoveContract){
    VerifyAllocatorAssignment<ContainerDetail::ArenaAllocator<u32, GlobalArena>>();
    VerifyAllocatorAssignment<ContainerDetail::ArenaCacheAlignedAllocator<u32, GlobalArena>>();
    VerifyAllocatorAssignment<DefaultAllocator<u32>>();
}

TEST(AllocatorPropagation, PlainAllocatorCanAcquireAndReleaseAnOptionalArenaBinding){
    GlobalArena arena(Name("tests/allocator_propagation/optional_binding"));
    ContainerDetail::ArenaAllocator<u32, GlobalArena> plain;
    ContainerDetail::ArenaAllocator<u32, GlobalArena> bound(arena);
    EXPECT_EQ(plain.arenaPtr(), nullptr);
    plain = bound;
    EXPECT_EQ(plain.arenaPtr(), &arena);
    bound = ContainerDetail::ArenaAllocator<u32, GlobalArena>{};
    EXPECT_EQ(bound.arenaPtr(), nullptr);
    EXPECT_EQ(plain.arenaPtr(), &arena);
}

TEST(AllocatorPropagation, VectorMoveTransfersStorageAndItsDeallocationOwner){
    for(const bool foreignArena : { false, true }){
        VerifyVectorMove<Vector<u32, GlobalArena>>(foreignArena);
        VerifyVectorMove<DefaultVector<u32, GlobalArena, DefaultOwner>>(foreignArena);
    }
}

TEST(AllocatorPropagation, CacheAlignedVectorMoveTransfersStorageAndItsDeallocationOwner){
    for(const bool foreignArena : { false, true }){
        for(const usize targetSize : { 0u, 1u, 65u })
            VerifyVectorMove<ParallelVector<u32, GlobalArena>>(foreignArena, targetSize);
    }
}

TEST(AllocatorPropagation, CacheAlignedVectorMoveConstructionPreservesExplicitArenaOwnership){
    for(const bool explicitAllocator : { false, true }){
        for(const bool foreignArena : { false, true }){
            GlobalArena sourceArena(Name("tests/allocator_propagation/parallel_construct_source"));
            GlobalArena targetArena(Name("tests/allocator_propagation/parallel_construct_target"));
            using Values = ParallelVector<u32, GlobalArena>;
            {
                Values source{ sourceArena };
                for(u32 index = 0u; index < 128u; ++index)
                    source.push_back(index);
                const u32* const firstElement = &source[0u];
                GlobalArena& destinationArena = explicitAllocator && foreignArena ? targetArena : sourceArena;
                Values target = explicitAllocator ? Values(Move(source), destinationArena) : Values(Move(source));
                EXPECT_EQ(target.get_allocator().arenaPtr(), &destinationArena);
                ASSERT_EQ(target.size(), 128u);
                EXPECT_EQ(&target[0u] == firstElement, &destinationArena == &sourceArena);
                for(u32 index = 0u; index < 128u; ++index)
                    EXPECT_EQ(target[index], index);
                source.clear();
                source.push_back(7u);
                EXPECT_EQ(source[0u], 7u);
                target.push_back(128u);
                EXPECT_EQ(target[128u], 128u);
            }
            EXPECT_EQ(sourceArena.memoryStats().usedBytes, 0u);
            EXPECT_EQ(sourceArena.memoryStats().reservedBytes, 0u);
            EXPECT_EQ(targetArena.memoryStats().usedBytes, 0u);
            EXPECT_EQ(targetArena.memoryStats().reservedBytes, 0u);
            EXPECT_EQ(sourceArena.memoryStats().allocationCount, sourceArena.memoryStats().deallocationCount);
            EXPECT_EQ(targetArena.memoryStats().allocationCount, targetArena.memoryStats().deallocationCount);
        }
    }
}

TEST(AllocatorPropagation, StringMovePropagatesArenaForHeapAndSmallStringStorage){
    for(const bool foreignArena : { false, true }){
        for(const bool shortSource : { false, true }){
            VerifyStringMove<AString<GlobalArena>>(foreignArena, shortSource);
            VerifyStringMove<DefaultAString<GlobalArena, DefaultOwner>>(foreignArena, shortSource);
        }
    }
}

TEST(AllocatorPropagation, OrderedMapMoveTransfersNodesAndTheirDeallocationOwner){
    for(const bool foreignArena : { false, true }){
        GlobalArena sourceArena(Name("tests/allocator_propagation/map_source"));
        GlobalArena targetArena(Name("tests/allocator_propagation/map_target"));
        {
            Map<u32, u32, GlobalArena> target{ foreignArena ? targetArena : sourceArena };
            target.emplace(900u, 900u);
            {
                Map<u32, u32, GlobalArena> source{ sourceArena };
                for(u32 key = 0u; key < 64u; ++key)
                    source.emplace(key, key * 3u);
                const u32* const sourceValue = &source.find(0u)->second;
                target = Move(source);
                EXPECT_EQ(target.get_allocator().arenaPtr(), &sourceArena);
                ASSERT_EQ(target.size(), 64u);
                EXPECT_EQ(&target.find(0u)->second, sourceValue);
                EXPECT_TRUE(source.empty());
                source.emplace(7u, 8u);
            }
            for(u32 key = 0u; key < 64u; ++key)
                EXPECT_EQ(target.find(key)->second, key * 3u);
            target.emplace(65u, 195u);
        }
        EXPECT_EQ(sourceArena.memoryStats().usedBytes, 0u);
        EXPECT_EQ(targetArena.memoryStats().usedBytes, 0u);
        EXPECT_EQ(sourceArena.memoryStats().allocationCount, sourceArena.memoryStats().deallocationCount);
        EXPECT_EQ(targetArena.memoryStats().allocationCount, targetArena.memoryStats().deallocationCount);
    }
}

TEST(AllocatorPropagation, SameArenaSwapsAndHashMapMovesKeepStorageOwners){
    GlobalArena arena(Name("tests/allocator_propagation/same_arena_swaps"));
    {
        Vector<u32, GlobalArena> first{ arena };
        Vector<u32, GlobalArena> second{ arena };
        first.assign(64u, 1u);
        second.assign(96u, 2u);
        const u32* const firstStorage = first.data();
        const u32* const secondStorage = second.data();
        ASSERT_EQ(first.get_allocator(), second.get_allocator());
        first.swap(second);
        EXPECT_EQ(first.data(), secondStorage);
        EXPECT_EQ(second.data(), firstStorage);
        AString<GlobalArena> firstText{ arena };
        AString<GlobalArena> secondText{ arena };
        firstText.assign(96u, 'a');
        secondText.assign(64u, 'b');
        firstText.swap(secondText);
        EXPECT_EQ(firstText[0u], 'b');
        EXPECT_EQ(secondText[0u], 'a');
        HashMap<u32, u32, GlobalArena> firstMap{ arena };
        HashMap<u32, u32, GlobalArena> secondMap{ arena };
        firstMap.emplace(1u, 11u);
        secondMap.emplace(2u, 22u);
        firstMap.swap(secondMap);
        EXPECT_EQ(firstMap.at(2u), 22u);
        secondMap = Move(firstMap);
        EXPECT_EQ(secondMap.at(2u), 22u);
        EXPECT_EQ(secondMap.get_allocator().arenaPtr(), &arena);
    }
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(arena.memoryStats().allocationCount, arena.memoryStats().deallocationCount);
}

TEST(AllocatorPropagation, DefaultProviderAllocatorMovesAwayFromItsInitialOwner){
    GlobalArena sourceArena(Name("tests/allocator_propagation/default_source"));
    const ArenaMemoryStats defaultBefore = DefaultOwner().memoryStats();
    {
        DefaultVector<u32, GlobalArena, DefaultOwner> target;
        EXPECT_EQ(target.get_allocator().arenaPtr(), &DefaultOwner());
        target.assign(32u, 9u);
        DefaultVector<u32, GlobalArena, DefaultOwner> source{ sourceArena };
        source.assign(64u, 8u);
        target = Move(source);
        EXPECT_EQ(target.get_allocator().arenaPtr(), &sourceArena);
        EXPECT_EQ(target.size(), 64u);
        EXPECT_EQ(target.back(), 8u);
        DefaultAllocator<u32> laterDefault;
        EXPECT_EQ(laterDefault.arenaPtr(), &DefaultOwner());
    }
    EXPECT_EQ(sourceArena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(DefaultOwner().memoryStats().usedBytes, defaultBefore.usedBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

