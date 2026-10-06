// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/gpu/task_graph.h>

#include <global/text_utils.h>

#include <tests/common/test_context.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_import_index_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;

struct GraphStamp{
    u64 generation;
    u64 revision;
    usize resources;
    usize sets;
};

[[nodiscard]] static Core::GpuGraphResourceDesc ResourceDescription(const Name& identity, Core::GpuGraphResourceType::Enum type){
    return Core::GpuGraphResourceDesc{}
        .setIdentity(identity)
        .setMarkerLabel("Graph Import Index Resource")
        .setType(type)
        .setInitialState(Core::ResourceStates::Unknown)
    ;
}

[[nodiscard]] static Name IndexedName(const Name& prefix, const usize index){
    char text[32u] = {};
    return DeriveName(prefix, FormatDecimal(index, text));
}

[[nodiscard]] static GraphStamp ReadStamp(const Core::GpuTaskGraph& graph){
    const Core::GpuTaskGraph::DeclarationReadView view(graph);
    EXPECT_TRUE(view.valid());
    return GraphStamp{ view.generation(), view.declarationRevision(), view.resourceCount(), view.resourceSetCount() };
}

static void ExpectUnchanged(const Core::GpuTaskGraph& graph, const GraphStamp& before){
    const GraphStamp after = ReadStamp(graph);
    EXPECT_EQ(after.generation, before.generation);
    EXPECT_EQ(after.revision, before.revision);
    EXPECT_EQ(after.resources, before.resources);
    EXPECT_EQ(after.sets, before.sets);
}

struct ImportContext{
    Core::Alloc::GlobalArena inputArena{ Name("tests/graph_import_index/inputs") };
    Core::Alloc::GlobalArena graphArena{ Name("tests/graph_import_index/graph") };
    Core::GpuTaskGraph graph{ graphArena };
    Vector<Core::GpuGraphResourceDesc, Core::Alloc::GlobalArena> genericDescriptions{ inputArena };
    Vector<Core::GpuGraphResourceSetDesc, Core::Alloc::GlobalArena> setDescriptions{ inputArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::GlobalArena> resources{ inputArena };
    Vector<Core::GpuGraphResourceSetId, Core::Alloc::GlobalArena> sets{ inputArena };

    void prepare(const usize count){
        genericDescriptions.resize(count);
        setDescriptions.resize(count);
        resources.resize(count);
        sets.resize(count);
        for(usize index = 0u; index < count; ++index){
            genericDescriptions[index] = ResourceDescription(
                IndexedName(Name("tests/graph_import_index/generic"), index), Core::GpuGraphResourceType::HazardDomain
            );
            setDescriptions[index]
                .setIdentity(IndexedName(Name("tests/graph_import_index/set"), index))
                .setMarkerLabel("Graph Import Index Set")
                .setMembers(&resources[index], 1u)
            ;
        }
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(TaskGraphImportIndex, EveryNameHashLaneParticipatesAndResourceSetIdentityHasItsOwnNamespace){
    ImportContext context;
    constexpr usize s_Count = 64u;
    context.resources.resize(s_Count);
    context.sets.resize(s_Count);
    Vector<Name, Core::Alloc::GlobalArena> identities(context.inputArena);
    identities.reserve(s_Count);
    for(usize index = 0u; index < s_Count; ++index){
        NameHash hash = ComputeNameHash("tests/graph_import_index/full_identity");
        hash.qwords[index % s_NameHashLaneCount] ^= static_cast<u64>(index + 1u);
        identities.emplace_back(hash);
        const auto description = ResourceDescription(identities.back(), Core::GpuGraphResourceType::HazardDomain);
        context.resources[index] = context.graph.importResource(description);
        ASSERT_TRUE(context.resources[index].valid());
        EXPECT_EQ(context.resources[index].index, index);
        context.sets[index] = context.graph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(identities.back())
                .setMarkerLabel("Binary Identity Set")
                .setMembers(&context.resources[index], 1u)
        );
        ASSERT_TRUE(context.sets[index].valid());
        EXPECT_EQ(context.sets[index].index, index);
    }
    const GraphStamp before = ReadStamp(context.graph);
    for(usize remaining = s_Count; remaining != 0u; --remaining){
        const usize index = remaining - 1u;
        const Name equalIdentity(identities[index].identityHash());
        EXPECT_EQ(context.graph.importResource(ResourceDescription(equalIdentity, Core::GpuGraphResourceType::HazardDomain)), context.resources[index]);
        EXPECT_EQ(context.graph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(equalIdentity)
                .setMarkerLabel("Later Binary Identity Label")
                .setMembers(&context.resources[index], 1u)
        ), context.sets[index]);
    }
    ASSERT_NO_FATAL_FAILURE(ExpectUnchanged(context.graph, before));
    EXPECT_EQ(before.resources, s_Count);
    EXPECT_EQ(before.sets, s_Count);
}


TEST(TaskGraphImportIndex, ManySetsReuseOnlyTheirOriginalOrderedMembersAndRetainFirstLabels){
    ImportContext context;
    constexpr usize s_Count = 96u;
    context.prepare(s_Count);
    for(usize index = 0u; index < s_Count; ++index){
        context.resources[index] = context.graph.importResource(context.genericDescriptions[index]);
        ASSERT_TRUE(context.resources[index].valid());
        context.sets[index] = context.graph.importResourceSet(context.setDescriptions[index]);
        ASSERT_TRUE(context.sets[index].valid());
    }
    const GraphStamp before = ReadStamp(context.graph);
    for(usize index = 0u; index < s_Count; ++index){
        const usize selected = (index * 73u) % s_Count;
        auto description = context.setDescriptions[selected];
        description.markerLabel = "Different Later Set Label";
        EXPECT_EQ(context.graph.importResourceSet(description), context.sets[selected]);
        description.setMembers(&context.resources[(selected + 1u) % s_Count], 1u);
        EXPECT_FALSE(context.graph.importResourceSet(description).valid());
    }
    ASSERT_NO_FATAL_FAILURE(ExpectUnchanged(context.graph, before));
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    for(usize index = 0u; index < s_Count; ++index){
        const auto set = view.resourceSetAt(context.sets[index].index);
        EXPECT_EQ(set.identity, context.setDescriptions[index].identity);
        EXPECT_EQ(set.markerLabel, "Graph Import Index Set");
        ASSERT_EQ(set.memberCount, 1u);
        EXPECT_EQ(set.members[0u], context.resources[index]);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

