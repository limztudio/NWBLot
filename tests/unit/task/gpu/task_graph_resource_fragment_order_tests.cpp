// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <core/task/gpu/compiler_internal.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_resource_fragment_order_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;
using Graphics::GpuTaskGraphCompilerDetail::TrackedCompiledResourceState;
using Graphics::GpuTaskGraphCompilerDetail::TrackedResourceStateFragment;

using TrackedStates = Vector<TrackedCompiledResourceState, Core::Alloc::ScratchArena>;
using StateFragments = Vector<TrackedResourceStateFragment, Core::Alloc::ScratchArena>;
using RequestedRanges = Vector<Graphics::GpuTaskResourceRange, Core::Alloc::ScratchArena>;

struct ExpectedBufferFragment{
    usize stateIndex;
    u64 offset;
    u64 size;
};

constexpr Graphics::GpuGraphResourceId s_Buffer{ 0u, 1u };
constexpr Graphics::GpuGraphResourceId s_OtherBuffer{ 1u, 1u };
constexpr usize s_InitialState = Limit<usize>::s_Max;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Graphics::GpuTaskResourceRange BufferRange(const u64 offset, const u64 size){
    return Graphics::GpuTaskResourceRange{ .bufferRange = Graphics::BufferRange(offset, size) };
}

void AppendBufferState(TrackedStates& states, const Graphics::GpuGraphResourceId resource, const u64 offset, const u64 size){
    states.push_back(TrackedCompiledResourceState{
        .resource = resource,
        .range = BufferRange(offset, size),
        .state = Graphics::ResourceStates::CopyDest,
        .access = Graphics::GpuTaskResourceAccess::Write,
        .task = Graphics::GpuTaskId{ static_cast<u32>(states.size()), 1u },
        .queue = Graphics::GpuPhysicalQueueId{ 0u, 1u },
    });
}

template<usize Count>
void ExpectBufferFragments(
    const StateFragments& fragments,
    const TrackedStates& states,
    const ExpectedBufferFragment (&expected)[Count]){
    ASSERT_EQ(fragments.size(), Count);
    for(usize index = 0u; index < Count; ++index){
        SCOPED_TRACE(index);
        EXPECT_EQ(fragments[index].stateIndex, expected[index].stateIndex);
        EXPECT_EQ(fragments[index].range.bufferRange, Graphics::BufferRange(expected[index].offset, expected[index].size));
        if(expected[index].stateIndex == s_InitialState)
            EXPECT_EQ(fragments[index].state, nullptr);
        else{
            ASSERT_LT(expected[index].stateIndex, states.size());
            EXPECT_EQ(fragments[index].state, &states[expected[index].stateIndex]);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphResourceFragments, LatestPartialOverwritesKeepProducerAndFragmentOrderBeforeInitialGaps){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TrackedStates states(scratchArena);
    states.reserve(5u);
    AppendBufferState(states, s_Buffer, 16u, 96u);
    AppendBufferState(states, s_OtherBuffer, 0u, 128u);
    AppendBufferState(states, s_Buffer, 32u, 16u);
    AppendBufferState(states, s_Buffer, 64u, 16u);
    AppendBufferState(states, s_OtherBuffer, 16u, 16u);
    RequestedRanges requested(scratchArena);
    requested.push_back(BufferRange(0u, 128u));
    StateFragments fragments(scratchArena);
    const Graphics::GpuTaskGraphResourceView resource{ .id = s_Buffer, .type = Graphics::GpuGraphResourceType::Buffer };

    ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, resource, requested, scratchArena, fragments));
    const ExpectedBufferFragment expected[] = {
        { 0u, 16u, 16u },
        { 0u, 48u, 16u },
        { 0u, 80u, 32u },
        { 2u, 32u, 16u },
        { 3u, 64u, 16u },
        { s_InitialState, 0u, 16u },
        { s_InitialState, 112u, 16u },
    };
    ExpectBufferFragments(fragments, states, expected);

    ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::CollectTerminalResourceStateFragments(states, resource, scratchArena, fragments));
    const ExpectedBufferFragment terminal[] = {
        { 0u, 16u, 16u },
        { 0u, 48u, 16u },
        { 0u, 80u, 32u },
        { 2u, 32u, 16u },
        { 3u, 64u, 16u },
    };
    ExpectBufferFragments(fragments, states, terminal);
}

TEST(GpuTaskGraphResourceFragments, SparseProducerIndicesRetainUnsortedRequestOrderWithinEachProducer){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TrackedStates states(scratchArena);
    states.reserve(130u);
    AppendBufferState(states, s_Buffer, 16u, 96u);
    for(usize index = 1u; index < 129u; ++index)
        AppendBufferState(states, s_OtherBuffer, index * 16u, 16u);
    AppendBufferState(states, s_Buffer, 120u, 8u);
    RequestedRanges requested(scratchArena);
    requested.reserve(6u);
    requested.push_back(BufferRange(80u, 8u));
    requested.push_back(BufferRange(16u, 8u));
    requested.push_back(BufferRange(120u, 8u));
    requested.push_back(BufferRange(48u, 8u));
    requested.push_back(BufferRange(144u, 8u));
    requested.push_back(BufferRange(0u, 8u));
    StateFragments fragments(scratchArena);
    const Graphics::GpuTaskGraphResourceView resource{ .id = s_Buffer, .type = Graphics::GpuGraphResourceType::Buffer };

    ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, resource, requested, scratchArena, fragments));
    const ExpectedBufferFragment expected[] = {
        { 0u, 80u, 8u },
        { 0u, 16u, 8u },
        { 0u, 48u, 8u },
        { 129u, 120u, 8u },
        { s_InitialState, 144u, 8u },
        { s_InitialState, 0u, 8u },
    };
    ExpectBufferFragments(fragments, states, expected);
}

TEST(GpuTaskGraphResourceFragments, EmptyAndUntrackedRequestsClearPriorOutputAndPreserveInitialOrder){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TrackedStates states(scratchArena);
    RequestedRanges requested(scratchArena);
    requested.reserve(2u);
    requested.push_back(BufferRange(64u, 8u));
    requested.push_back(BufferRange(0u, 8u));
    StateFragments fragments(scratchArena);
    const Graphics::GpuTaskGraphResourceView resource{ .id = s_Buffer, .type = Graphics::GpuGraphResourceType::Buffer };

    ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, resource, requested, scratchArena, fragments));
    const ExpectedBufferFragment expected[] = {
        { s_InitialState, 64u, 8u },
        { s_InitialState, 0u, 8u },
    };
    ExpectBufferFragments(fragments, states, expected);
    AppendBufferState(states, s_OtherBuffer, 0u, 128u);
    ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, resource, requested, scratchArena, fragments));
    ExpectBufferFragments(fragments, states, expected);
    requested.clear();
    ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, resource, requested, scratchArena, fragments));
    EXPECT_TRUE(fragments.empty());
    ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::CollectTerminalResourceStateFragments(states, resource, scratchArena, fragments));
    EXPECT_TRUE(fragments.empty());
}

TEST(GpuTaskGraphResourceFragments, SymbolicBufferTailRemainsAfterFiniteRemainderWithinOldestProducer){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TrackedStates states(scratchArena);
    states.reserve(2u);
    AppendBufferState(states, s_Buffer, 0u, Graphics::BufferRange::AllBytes);
    AppendBufferState(states, s_Buffer, 16u, 16u);
    RequestedRanges requested(scratchArena);
    requested.push_back(BufferRange(0u, Graphics::BufferRange::AllBytes));
    StateFragments fragments(scratchArena);
    const Graphics::GpuTaskGraphResourceView resource{ .id = s_Buffer, .type = Graphics::GpuGraphResourceType::Buffer };
    const ExpectedBufferFragment expected[] = {
        { 0u, 0u, 16u },
        { 0u, 32u, Graphics::BufferRange::AllBytes },
        { 1u, 16u, 16u },
    };

    ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, resource, requested, scratchArena, fragments));
    ExpectBufferFragments(fragments, states, expected);
    ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::CollectTerminalResourceStateFragments(states, resource, scratchArena, fragments));
    ExpectBufferFragments(fragments, states, expected);
}

TEST(GpuTaskGraphResourceFragments, TextureInteriorOverwritePreservesFourRemaindersInDiscoveryOrder){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TrackedStates states(scratchArena);
    states.reserve(2u);
    const Graphics::GpuTaskResourceRange whole{ .textureSubresources = Graphics::TextureSubresourceSet(0u, 6u, 0u, 5u) };
    const Graphics::GpuTaskResourceRange center{ .textureSubresources = Graphics::TextureSubresourceSet(2u, 2u, 1u, 3u) };
    states.push_back(TrackedCompiledResourceState{
        .resource = s_Buffer,
        .range = whole,
        .state = Graphics::ResourceStates::CopyDest,
        .access = Graphics::GpuTaskResourceAccess::Write,
        .task = Graphics::GpuTaskId{ 0u, 1u },
        .queue = Graphics::GpuPhysicalQueueId{ 0u, 1u },
    });
    states.push_back(TrackedCompiledResourceState{
        .resource = s_Buffer,
        .range = center,
        .state = Graphics::ResourceStates::CopyDest,
        .access = Graphics::GpuTaskResourceAccess::Write,
        .task = Graphics::GpuTaskId{ 1u, 1u },
        .queue = Graphics::GpuPhysicalQueueId{ 0u, 1u },
    });
    RequestedRanges requested(scratchArena);
    requested.push_back(whole);
    StateFragments fragments(scratchArena);
    const Graphics::GpuTaskGraphResourceView resource{ .id = s_Buffer, .type = Graphics::GpuGraphResourceType::Texture };
    const Graphics::TextureSubresourceSet expected[] = {
        Graphics::TextureSubresourceSet(0u, 2u, 0u, 5u),
        Graphics::TextureSubresourceSet(4u, 2u, 0u, 5u),
        Graphics::TextureSubresourceSet(2u, 2u, 0u, 1u),
        Graphics::TextureSubresourceSet(2u, 2u, 4u, 1u),
        center.textureSubresources,
    };

    for(usize terminal = 0u; terminal < 2u; ++terminal){
        SCOPED_TRACE(terminal);
        const bool collected = terminal == 0u
            ? Graphics::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, resource, requested, scratchArena, fragments)
            : Graphics::GpuTaskGraphCompilerDetail::CollectTerminalResourceStateFragments(states, resource, scratchArena, fragments)
        ;
        ASSERT_TRUE(collected);
        ASSERT_EQ(fragments.size(), LengthOf(expected));
        for(usize index = 0u; index < LengthOf(expected); ++index){
            SCOPED_TRACE(index);
            const usize stateIndex = index < 4u ? 0u : 1u;
            EXPECT_EQ(fragments[index].stateIndex, stateIndex);
            EXPECT_EQ(fragments[index].state, &states[stateIndex]);
            EXPECT_EQ(fragments[index].range.textureSubresources, expected[index]);
        }
    }
}

TEST(GpuTaskGraphResourceFragments, SixtyFourBuffersPreserveExactTerminalExportSourceOrderAcrossPartialWrites){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    constexpr usize s_BufferCount = 64u;
    Graphics::GpuGraphResourceId buffers[s_BufferCount];
    Graphics::GpuTaskResourceUse wholeUses[s_BufferCount];
    Graphics::GpuTaskResourceUse cutUses[s_BufferCount * 2u];
    Graphics::GpuTaskResourceUse readUses[s_BufferCount];
    for(usize index = 0u; index < s_BufferCount; ++index){
        char text[32u] = {};
        const Name identity = DeriveName(Name("tests/fragment_order/buffer/"), FormatDecimal(index, text));
        buffers[index] = graph.importResource(Graphics::GpuGraphResourceDesc{}
            .setIdentity(identity)
            .setMarkerLabel("Fragment Order Buffer")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::Common)
            .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(queues[1u].id)
        );
        ASSERT_TRUE(buffers[index].valid());
        wholeUses[index] = Graphics::GpuTaskResourceUse{
            .resource = buffers[index],
            .range = BufferRange(0u, 128u),
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        cutUses[index * 2u] = Graphics::GpuTaskResourceUse{
            .resource = buffers[index],
            .range = BufferRange(32u, 16u),
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        cutUses[index * 2u + 1u] = cutUses[index * 2u];
        cutUses[index * 2u + 1u].range = BufferRange(64u, 16u);
        readUses[index] = Graphics::GpuTaskResourceUse{
            .resource = buffers[index],
            .range = BufferRange(48u, 16u),
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        };
    }
    const Graphics::GpuQueueRequest queue{
        .requiredCapabilities = Graphics::GpuQueueCapability::Graphics,
        .preferredQueue = Graphics::GpuQueuePreference::Graphics,
        .allowFallback = false,
        .compilerMayOverridePreference = false,
    };
    Graphics::GpuTaskDesc wholeDesc;
    wholeDesc
        .setIdentity(Name("tests/fragment_order/whole"))
        .setMarkerLabel("Whole Writer")
        .setResourceUses(wholeUses, LengthOf(wholeUses))
        .setQueue(queue)
    ;
    const Graphics::GpuTaskId whole = graph.addTask(wholeDesc);
    ASSERT_TRUE(whole.valid());
    Graphics::GpuTaskDesc cutsDesc;
    cutsDesc
        .setIdentity(Name("tests/fragment_order/cuts"))
        .setMarkerLabel("Partial Writers")
        .setDependencies(&whole, 1u)
        .setResourceUses(cutUses, LengthOf(cutUses))
        .setQueue(queue)
    ;
    const Graphics::GpuTaskId cuts = graph.addTask(cutsDesc);
    ASSERT_TRUE(cuts.valid());
    Graphics::GpuTaskDesc readerDesc;
    readerDesc
        .setIdentity(Name("tests/fragment_order/reader"))
        .setMarkerLabel("Gap Reader")
        .setDependencies(&cuts, 1u)
        .setResourceUses(readUses, LengthOf(readUses))
        .setQueue(queue)
    ;
    const Graphics::GpuTaskId reader = graph.addTask(readerDesc);
    ASSERT_TRUE(reader.valid());
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView plan(compiledGraph);
    const Graphics::GpuTaskId expectedTasks[] = { whole, whole, cuts, cuts, reader };
    const Graphics::BufferRange expectedRanges[] = {
        Graphics::BufferRange(0u, 32u),
        Graphics::BufferRange(80u, 48u),
        Graphics::BufferRange(32u, 16u),
        Graphics::BufferRange(64u, 16u),
        Graphics::BufferRange(48u, 16u),
    };
    for(usize index = 0u; index < s_BufferCount; ++index){
        SCOPED_TRACE(index);
        const Graphics::GpuCompiledExternalResourceExportView exported = plan.externalResourceExport(buffers[index]);
        ASSERT_NE(exported.plan, nullptr);
        ASSERT_NE(exported.sources, nullptr);
        EXPECT_EQ(exported.plan->finalState, Graphics::ResourceStates::ShaderResource);
        EXPECT_EQ(exported.plan->destinationQueue, queues[1u].id);
        ASSERT_EQ(exported.plan->sourceCount, LengthOf(expectedTasks));
        for(usize sourceIndex = 0u; sourceIndex < LengthOf(expectedTasks); ++sourceIndex){
            SCOPED_TRACE(sourceIndex);
            EXPECT_EQ(exported.sources[sourceIndex].producerTask, expectedTasks[sourceIndex]);
            EXPECT_EQ(exported.sources[sourceIndex].sourceQueue, queues[0u].id);
            EXPECT_EQ(exported.sources[sourceIndex].range.bufferRange, expectedRanges[sourceIndex]);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

