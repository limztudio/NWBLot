// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/gpu/task_graph.h>

#include <global/arena_memory.h>
#include <global/text_utils.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_resource_set_memory_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ResourceIds = Vector<Core::GpuGraphResourceId, Core::Alloc::GlobalArena>;

static constexpr Name s_ScratchOwner("core/task/gpu/declaration_scratch");


[[nodiscard]] static ArenaMemoryStats ScratchOwnerStats(){
    const ArenaMemoryOwnerRecord* record = FirstArenaMemoryOwnerRecord();
    while(record){
        ArenaMemoryOwnerSnapshot snapshot;
        record = ReadArenaMemoryOwnerRecord(*record, snapshot);
        if(snapshot.source == ArenaMemorySource::Arena && snapshot.ownerName == s_ScratchOwner)
            return snapshot.stats;
    }
    return {};
}

[[nodiscard]] static bool FillResources(Core::GpuTaskGraph& graph, ResourceIds& members, const usize count){
    members.clear();
    members.reserve(count);
    for(usize index = 0u; index < count; ++index){
        char text[32u] = {};
        const Name identity = DeriveName(Name("tests/resource_set_memory/resource/"), FormatDecimal(index, text));
        const Core::GpuGraphResourceId resource = graph.importResource(Core::GpuGraphResourceDesc{}
            .setIdentity(identity)
            .setMarkerLabel("Resource Set Memory Input")
            .setType(Core::GpuGraphResourceType::Buffer)
        );
        if(!resource.valid())
            return false;
        members.push_back(resource);
    }
    return true;
}

[[nodiscard]] static Core::GpuGraphResourceSetDesc SetDescription(
    const Name& identity,
    const ResourceIds& members,
    const usize count){
    return Core::GpuGraphResourceSetDesc{}
        .setIdentity(identity)
        .setMarkerLabel("Resource Set Memory Members")
        .setMembers(members.data(), count)
    ;
}

static void ExpectDeclarationState(const Core::GpuTaskGraph& graph, const u64 revision, const usize setCount){
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    ASSERT_TRUE(declarations.valid());
    EXPECT_EQ(declarations.declarationRevision(), revision);
    EXPECT_EQ(declarations.resourceSetCount(), setCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ResourceSetMemory, SmallSetsAndBlockedMutationLeaveTheScratchOwnerUnallocated){
    const ArenaMemoryStats before = ScratchOwnerStats();
    {
        Core::Alloc::GlobalArena arena(Name("tests/resource_set_memory/small"));
        Core::GpuTaskGraph graph(arena);
        ResourceIds members(arena);
        ASSERT_TRUE(FillResources(graph, members, 64u));
        Core::GpuGraphResourceSetId lastSet;
        for(const usize count : { 0u, 1u, 8u, 32u }){
            char text[32u] = {};
            const Name identity = DeriveName(Name("tests/resource_set_memory/small_set/"), FormatDecimal(count, text));
            lastSet = graph.importResourceSet(SetDescription(identity, members, count));
            ASSERT_TRUE(lastSet.valid());
        }
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
            ASSERT_TRUE(declarations.valid());
            const u64 revision = declarations.declarationRevision();
            const Core::GpuTaskGraphResourceSetView last = declarations.resourceSetAt(lastSet.index);
            EXPECT_FALSE(graph.importResourceSet(SetDescription(
                Name("tests/resource_set_memory/blocked_large"), members, members.size()
            )).valid());
            EXPECT_FALSE(graph.importResourceSet(SetDescription(last.identity, members, last.memberCount)).valid());
            EXPECT_EQ(declarations.declarationRevision(), revision);
        }
        const ArenaMemoryStats after = ScratchOwnerStats();
        EXPECT_EQ(after.usedBytes, before.usedBytes);
        EXPECT_EQ(after.reservedBytes, before.reservedBytes);
        EXPECT_EQ(after.allocationCount, before.allocationCount);
        EXPECT_EQ(after.reallocationCount, before.reallocationCount);
        EXPECT_EQ(after.deallocationCount, before.deallocationCount);
    }
    EXPECT_EQ(ScratchOwnerStats().usedBytes, before.usedBytes);
    EXPECT_EQ(ScratchOwnerStats().reservedBytes, before.reservedBytes);
}

TEST(ResourceSetMemory, LargeSmallLargeScopesReclaimUsageAndReuseBackingAcrossGraphReset){
    const ArenaMemoryStats before = ScratchOwnerStats();
    ArenaMemoryStats warm;
    {
        Core::Alloc::GlobalArena arena(Name("tests/resource_set_memory/repeated"));
        Core::GpuTaskGraph graph(arena);
        ResourceIds members(arena);
        for(usize cycle = 0u; cycle < 8u; ++cycle){
            if(cycle != 0u)
                graph.reset();
            ASSERT_TRUE(FillResources(graph, members, 4096u));
            const ArenaMemoryStats heapBefore = HeapBackingMemoryStats();
            usize phase = 0u;
            for(const usize count : { 4096u, 33u, 4096u }){
                char text[32u] = {};
                const Name identity = DeriveName(Name("tests/resource_set_memory/phase/"), FormatDecimal(phase, text));
                const Core::GpuGraphResourceSetId set = graph.importResourceSet(SetDescription(identity, members, count));
                ASSERT_TRUE(set.valid());
                EXPECT_EQ(ScratchOwnerStats().usedBytes, before.usedBytes);
                const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
                ASSERT_TRUE(declarations.valid());
                const Core::GpuTaskGraphResourceSetView stored = declarations.resourceSetAt(set.index);
                ASSERT_EQ(stored.memberCount, count);
                EXPECT_EQ(stored.members[0u], members[0u]);
                EXPECT_EQ(stored.members[count - 1u], members[count - 1u]);
                ++phase;
            }
            const ArenaMemoryStats heapAfter = HeapBackingMemoryStats();
            const ArenaMemoryStats after = ScratchOwnerStats();
            if(cycle == 0u){
                warm = after;
                EXPECT_GT(warm.reservedBytes, before.reservedBytes);
                EXPECT_GT(warm.allocationCount, before.allocationCount);
            }
            else{
                EXPECT_EQ(after.reservedBytes, warm.reservedBytes);
                EXPECT_EQ(heapAfter.allocationCount, heapBefore.allocationCount);
                EXPECT_EQ(heapAfter.reallocationCount, heapBefore.reallocationCount);
                EXPECT_EQ(heapAfter.deallocationCount, heapBefore.deallocationCount);
            }
            EXPECT_EQ(after.usedBytes, before.usedBytes);
        }
        graph.reset();
        const ArenaMemoryStats afterReset = ScratchOwnerStats();
        EXPECT_EQ(afterReset.usedBytes, before.usedBytes);
        EXPECT_EQ(afterReset.reservedBytes, warm.reservedBytes);
    }
    const ArenaMemoryStats afterDestruction = ScratchOwnerStats();
    EXPECT_EQ(afterDestruction.usedBytes, before.usedBytes);
    EXPECT_EQ(afterDestruction.reservedBytes, before.reservedBytes);
    EXPECT_GT(afterDestruction.allocationCount, before.allocationCount);
    EXPECT_EQ(
        afterDestruction.allocationCount - before.allocationCount,
        afterDestruction.deallocationCount - before.deallocationCount
    );
}

TEST(ResourceSetMemory, LateRejectedMembersRetireTemporaryStorageBeforeTheNextDeclaration){
    const ArenaMemoryStats before = ScratchOwnerStats();
    {
        Core::Alloc::GlobalArena arena(Name("tests/resource_set_memory/rejection"));
        Core::GpuTaskGraph graph(arena);
        ResourceIds members(arena);
        ASSERT_TRUE(FillResources(graph, members, 1024u));
        const Core::GpuGraphResourceId saved = members.back();
        const Core::GpuGraphResourceSetDesc rejected = SetDescription(
            Name("tests/resource_set_memory/rejected"), members, members.size()
        );
        u64 warmReserved = 0u;
        for(usize cycle = 0u; cycle < 16u; ++cycle){
            u64 revision = 0u;
            {
                const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
                ASSERT_TRUE(declarations.valid());
                revision = declarations.declarationRevision();
            }
            members.back() = members.front();
            EXPECT_FALSE(graph.importResourceSet(rejected).valid());
            ASSERT_NO_FATAL_FAILURE(ExpectDeclarationState(graph, revision, cycle));
            EXPECT_EQ(ScratchOwnerStats().usedBytes, before.usedBytes);
            members.back() = saved;
            ++members.back().generation;
            EXPECT_FALSE(graph.importResourceSet(rejected).valid());
            ASSERT_NO_FATAL_FAILURE(ExpectDeclarationState(graph, revision, cycle));
            EXPECT_EQ(ScratchOwnerStats().usedBytes, before.usedBytes);
            members.back() = saved;
            members.back().index = static_cast<u32>(members.size());
            EXPECT_FALSE(graph.importResourceSet(rejected).valid());
            ASSERT_NO_FATAL_FAILURE(ExpectDeclarationState(graph, revision, cycle));
            EXPECT_EQ(ScratchOwnerStats().usedBytes, before.usedBytes);
            members.back() = saved;
            char text[32u] = {};
            const Name acceptedIdentity = DeriveName(Name("tests/resource_set_memory/accepted/"), FormatDecimal(cycle, text));
            EXPECT_TRUE(graph.importResourceSet(SetDescription(acceptedIdentity, members, members.size())).valid());
            const ArenaMemoryStats after = ScratchOwnerStats();
            EXPECT_EQ(after.usedBytes, before.usedBytes);
            if(cycle == 0u){
                warmReserved = after.reservedBytes;
                EXPECT_GT(warmReserved, before.reservedBytes);
            }
            else
                EXPECT_EQ(after.reservedBytes, warmReserved);
        }
    }
    const ArenaMemoryStats after = ScratchOwnerStats();
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_EQ(after.reservedBytes, before.reservedBytes);
    EXPECT_GT(after.allocationCount, before.allocationCount);
    EXPECT_EQ(after.allocationCount - before.allocationCount, after.deallocationCount - before.deallocationCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

