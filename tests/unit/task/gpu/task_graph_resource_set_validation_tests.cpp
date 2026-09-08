// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/gpu/task_graph.h>

#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_resource_set_validation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ResourceSetContext{
    Core::Alloc::GlobalArena graphArena{ Name("tests/resource_set_validation/graph") };
    Core::GpuTaskGraph graph{ graphArena };
    Core::Alloc::GlobalArena inputArena{ Name("tests/resource_set_validation/input") };
    Vector<Core::GpuGraphResourceId, Core::Alloc::GlobalArena> members{ inputArena };
    Core::GpuGraphResourceSetId result;
    bool ready = false;

    explicit ResourceSetContext(const usize count){
        ready = fill(count);
    }

    [[nodiscard]] bool fill(const usize count){
        members.clear();
        members.reserve(count);
        for(usize index = 0u; index < count; ++index){
            char text[32u] = {};
            const Name identity = DeriveName(Name("tests/resource_set_validation/resource/"), FormatDecimal(index, text));
            const Core::GpuGraphResourceId resource = graph.importResource(Core::GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel("Resource Set Validation Input")
                .setType(Core::GpuGraphResourceType::Buffer)
            );
            if(!resource.valid())
                return false;
            members.push_back(resource);
        }
        for(usize index = 0u; index < count / 2u; ++index)
            Swap(members[index], members[count - index - 1u]);
        return true;
    }

    [[nodiscard]] Core::GpuGraphResourceSetDesc description(const Name& identity)const{
        return Core::GpuGraphResourceSetDesc{}
            .setIdentity(identity)
            .setMarkerLabel("Resource Set Validation Members")
            .setMembers(members.data(), members.size())
        ;
    }
};

static void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

static void ExpectUnchangedDeclaration(const ResourceSetContext& context, const u64 revision, const usize setCount){
    const Core::GpuTaskGraph::DeclarationReadView declarations(context.graph);
    ASSERT_TRUE(declarations.valid());
    EXPECT_EQ(declarations.declarationRevision(), revision);
    EXPECT_EQ(declarations.resourceSetCount(), setCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ResourceSetValidation, CopiesOrderedMembersAndReusesOnlyTheSameOrderedIdentity){
    ResourceSetContext context(64u);
    ASSERT_TRUE(context.ready);
    constexpr Name s_Identity("tests/resource_set_validation/ordered");
    Core::GpuGraphResourceSetDesc desc = context.description(s_Identity);
    const Core::GpuGraphResourceSetId set = context.graph.importResourceSet(desc);
    ASSERT_TRUE(set.valid());
    u64 revision = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(context.graph);
        ASSERT_TRUE(declarations.valid());
        revision = declarations.declarationRevision();
        const Core::GpuTaskGraphResourceSetView stored = declarations.resourceSetAt(set.index);
        ASSERT_EQ(stored.memberCount, context.members.size());
        for(usize index = 0u; index < stored.memberCount; ++index)
            EXPECT_EQ(stored.members[index], context.members[index]);
    }
    desc.markerLabel = "Replacement Label Does Not Replace Stored Metadata";
    EXPECT_EQ(context.graph.importResourceSet(desc), set);
    ExpectUnchangedDeclaration(context, revision, 1u);
    Swap(context.members.front(), context.members.back());
    EXPECT_FALSE(context.graph.importResourceSet(desc).valid());
    ExpectUnchangedDeclaration(context, revision, 1u);
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(context.graph);
        const Core::GpuTaskGraphResourceSetView stored = declarations.resourceSetAt(set.index);
        EXPECT_EQ(stored.members[0u], context.members.back());
        EXPECT_EQ(stored.members[stored.memberCount - 1u], context.members.front());
        EXPECT_EQ(stored.markerLabel, AStringView("Resource Set Validation Members"));
    }
    Swap(context.members.front(), context.members.back());
    --desc.memberCount;
    EXPECT_FALSE(context.graph.importResourceSet(desc).valid());
    ExpectUnchangedDeclaration(context, revision, 1u);
    desc = context.description(Name("tests/resource_set_validation/second_ordered"));
    EXPECT_TRUE(context.graph.importResourceSet(desc).valid());
    const Core::GpuTaskGraph::DeclarationReadView declarations(context.graph);
    EXPECT_EQ(declarations.resourceSetCount(), 2u);
}

TEST(ResourceSetValidation, EarlyAndLateDuplicatesOrInvalidIdsDoNotPublishPartialSets){
    for(const usize count : { 2u, 8u, 32u, 33u, 1024u }){
        ResourceSetContext context(count);
        ASSERT_TRUE(context.ready);
        const Core::GpuGraphResourceSetDesc desc = context.description(Name("tests/resource_set_validation/rejected"));
        u64 revision = 0u;
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(context.graph);
            revision = declarations.declarationRevision();
        }
        for(const usize index : Array<usize, 2u>{ 1u, count - 1u }){
            const Core::GpuGraphResourceId saved = context.members[index];
            context.members[index] = context.members[0u];
            EXPECT_FALSE(context.graph.importResourceSet(desc).valid());
            ExpectUnchangedDeclaration(context, revision, 0u);
            context.members[index] = saved;
        }
        for(const usize index : Array<usize, 2u>{ 0u, count - 1u }){
            const Core::GpuGraphResourceId saved = context.members[index];
            context.members[index] = {};
            EXPECT_FALSE(context.graph.importResourceSet(desc).valid());
            ExpectUnchangedDeclaration(context, revision, 0u);
            context.members[index] = saved;
            context.members[index].generation = saved.generation + 1u;
            EXPECT_FALSE(context.graph.importResourceSet(desc).valid());
            ExpectUnchangedDeclaration(context, revision, 0u);
            context.members[index] = saved;
            context.members[index].index = static_cast<u32>(count);
            EXPECT_FALSE(context.graph.importResourceSet(desc).valid());
            ExpectUnchangedDeclaration(context, revision, 0u);
            context.members[index] = saved;
        }
        EXPECT_TRUE(context.graph.importResourceSet(desc).valid());
        const Core::GpuTaskGraph::DeclarationReadView declarations(context.graph);
        EXPECT_EQ(declarations.resourceSetCount(), 1u);
    }
}

TEST(ResourceSetValidation, EmptySetsRemainValidAndMalformedDescriptorsRemainUnpublished){
    ResourceSetContext context(1u);
    ASSERT_TRUE(context.ready);
    Core::GpuGraphResourceSetDesc desc = context.description(Name("tests/resource_set_validation/empty"));
    desc.setMembers(nullptr, 0u);
    const Core::GpuGraphResourceSetId empty = context.graph.importResourceSet(desc);
    ASSERT_TRUE(empty.valid());
    EXPECT_EQ(context.graph.importResourceSet(desc), empty);
    u64 revision = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(context.graph);
        revision = declarations.declarationRevision();
        EXPECT_EQ(declarations.resourceSetAt(empty.index).memberCount, 0u);
    }
    desc.identity = Name("tests/resource_set_validation/bad_shape");
    desc.setMembers(nullptr, 1u);
    EXPECT_FALSE(context.graph.importResourceSet(desc).valid());
    desc = context.description(NAME_NONE);
    EXPECT_FALSE(context.graph.importResourceSet(desc).valid());
    desc = context.description(Name("tests/resource_set_validation/bad_label"));
    desc.markerLabel = {};
    EXPECT_FALSE(context.graph.importResourceSet(desc).valid());
    desc = context.description(Name("tests/resource_set_validation/too_many"));
    desc.memberCount = static_cast<usize>(Limit<u32>::s_Max) + 1u;
    EXPECT_FALSE(context.graph.importResourceSet(desc).valid());
    ExpectUnchangedDeclaration(context, revision, 1u);
}

TEST(ResourceSetValidation, ResetRejectsOldGenerationsAndAcceptsFreshIdsAtTheSameIndices){
    ResourceSetContext context(64u);
    ASSERT_TRUE(context.ready);
    constexpr Name s_Identity("tests/resource_set_validation/reset");
    const Core::GpuGraphResourceSetId previousSet = context.graph.importResourceSet(context.description(s_Identity));
    ASSERT_TRUE(previousSet.valid());
    const Core::GpuGraphResourceId previousMember = context.members.back();
    context.graph.reset();
    ASSERT_TRUE(context.fill(64u));
    const Core::GpuGraphResourceId freshMember = context.members.back();
    EXPECT_EQ(freshMember.index, previousMember.index);
    EXPECT_NE(freshMember.generation, previousMember.generation);
    context.members.back() = previousMember;
    EXPECT_FALSE(context.graph.importResourceSet(context.description(s_Identity)).valid());
    context.members.back() = freshMember;
    const Core::GpuGraphResourceSetId freshSet = context.graph.importResourceSet(context.description(s_Identity));
    ASSERT_TRUE(freshSet.valid());
    const Core::GpuTaskGraph::DeclarationReadView declarations(context.graph);
    EXPECT_FALSE(declarations.validResourceSet(previousSet));
    EXPECT_TRUE(declarations.validResourceSet(freshSet));
    EXPECT_EQ(declarations.resourceSetCount(), 1u);
}

TEST(ResourceSetValidation, LiveReadViewsRejectBothNewAndIdempotentResourceSetMutation){
    ResourceSetContext context(64u);
    ASSERT_TRUE(context.ready);
    const Core::GpuGraphResourceSetDesc first = context.description(Name("tests/resource_set_validation/read_first"));
    const Core::GpuGraphResourceSetId set = context.graph.importResourceSet(first);
    ASSERT_TRUE(set.valid());
    const Core::GpuGraphResourceSetDesc second = context.description(Name("tests/resource_set_validation/read_second"));
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(context.graph);
        ASSERT_TRUE(declarations.valid());
        const u64 revision = declarations.declarationRevision();
        const Core::GpuTaskGraphResourceSetView stored = declarations.resourceSetAt(set.index);
        Core::GpuGraphResourceSetDesc borrowed = second;
        borrowed.setMembers(stored.members, stored.memberCount);
        EXPECT_FALSE(context.graph.importResourceSet(borrowed).valid());
        EXPECT_FALSE(context.graph.importResourceSet(first).valid());
        EXPECT_EQ(declarations.declarationRevision(), revision);
        EXPECT_EQ(declarations.resourceSetCount(), 1u);
        EXPECT_EQ(stored.members[0u], context.members[0u]);
    }
    EXPECT_TRUE(context.graph.importResourceSet(second).valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void BenchmarkResourceSets(const usize memberCount, const usize contextCount){
    Core::Alloc::GlobalArena ownerArena(Name("tests/resource_set_validation/benchmark_owners"));
    List<ResourceSetContext, Core::Alloc::GlobalArena> contexts(ownerArena);
    for(usize index = 0u; index < contextCount; ++index){
        contexts.emplace_back(memberCount);
        ASSERT_TRUE(contexts.back().ready);
    }
    constexpr Name s_Identity("tests/resource_set_validation/benchmark_set");
    const ArenaMemoryStats beforeFirst = HeapBackingMemoryStats();
    const Timer firstBegin = TimerNow();
    for(auto& context : contexts)
        context.result = context.graph.importResourceSet(context.description(s_Identity));
    const u64 firstElapsed = DurationInNS<u64>(TimerNow(), firstBegin);
    const ArenaMemoryStats afterFirst = HeapBackingMemoryStats();
    for(auto& context : contexts){
        ASSERT_TRUE(context.result.valid());
        const Core::GpuTaskGraph::DeclarationReadView declarations(context.graph);
        ASSERT_TRUE(declarations.valid());
        ASSERT_EQ(declarations.resourceSetCount(), 1u);
        const Core::GpuTaskGraphResourceSetView set = declarations.resourceSetAt(context.result.index);
        ASSERT_EQ(set.memberCount, memberCount);
        for(usize index = 0u; index < memberCount; ++index)
            EXPECT_EQ(set.members[index], context.members[index]);
    }
    constexpr usize s_ReuseCount = 64u;
    const ArenaMemoryStats beforeReuse = HeapBackingMemoryStats();
    bool reused = true;
    const Timer reuseBegin = TimerNow();
    for(usize iteration = 0u; iteration < s_ReuseCount; ++iteration){
        for(auto& context : contexts)
            reused &= context.graph.importResourceSet(context.description(s_Identity)) == context.result;
    }
    const u64 reuseElapsed = DurationInNS<u64>(TimerNow(), reuseBegin);
    const ArenaMemoryStats afterReuse = HeapBackingMemoryStats();
    EXPECT_TRUE(reused);
    EXPECT_EQ(afterReuse.allocationCount, beforeReuse.allocationCount);
    RecordUnsignedProperty(MakeNotNull("resource_set_member_count"), memberCount);
    RecordUnsignedProperty(MakeNotNull("resource_set_first_import_count"), contextCount);
    RecordUnsignedProperty(MakeNotNull("resource_set_first_import_ns"), firstElapsed);
    RecordUnsignedProperty(MakeNotNull("resource_set_first_heap_allocations"), afterFirst.allocationCount - beforeFirst.allocationCount);
    RecordUnsignedProperty(MakeNotNull("resource_set_first_heap_used_bytes"), afterFirst.usedBytes - beforeFirst.usedBytes);
    RecordUnsignedProperty(MakeNotNull("resource_set_first_heap_reserved_bytes"), afterFirst.reservedBytes - beforeFirst.reservedBytes);
    RecordUnsignedProperty(MakeNotNull("resource_set_reuse_count"), contextCount * s_ReuseCount);
    RecordUnsignedProperty(MakeNotNull("resource_set_reuse_ns"), reuseElapsed);
    RecordUnsignedProperty(MakeNotNull("resource_set_reuse_heap_allocations"), afterReuse.allocationCount - beforeReuse.allocationCount);
}

TEST(ResourceSetValidationBenchmark, DISABLED_FirstAndReused1){
    BenchmarkResourceSets(1u, 64u);
}

TEST(ResourceSetValidationBenchmark, DISABLED_FirstAndReused8){
    BenchmarkResourceSets(8u, 64u);
}

TEST(ResourceSetValidationBenchmark, DISABLED_FirstAndReused32){
    BenchmarkResourceSets(32u, 32u);
}

TEST(ResourceSetValidationBenchmark, DISABLED_FirstAndReused1024){
    BenchmarkResourceSets(1024u, 4u);
}

TEST(ResourceSetValidationBenchmark, DISABLED_FirstAndReused4096){
    BenchmarkResourceSets(4096u, 2u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

