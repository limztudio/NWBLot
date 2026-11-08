// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>

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
using Core::GpuTaskGraphCompilerDetail::TrackedCompiledResourceState;
using Core::GpuTaskGraphCompilerDetail::TrackedResourceStateFragment;
using Core::GpuTaskGraphCompilerDetail::TrackedResourceStateHistory;

using TrackedStates = Vector<TrackedCompiledResourceState, Core::Alloc::ScratchArena>;
using StateFragments = Vector<TrackedResourceStateFragment, Core::Alloc::ScratchArena>;
using RequestedRanges = Vector<Core::GpuTaskResourceRange, Core::Alloc::ScratchArena>;

using ExpectedBufferFragment = TaskGraphTestUtils::ExpectedBufferFragment;

constexpr Core::GpuGraphResourceId s_Buffer{ .generation = 1u, .index = 0u };
constexpr Core::GpuGraphResourceId s_OtherBuffer{ .generation = 1u, .index = 1u };
constexpr usize s_InitialState = Limit<usize>::s_Max;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Core::GpuTaskGraphResourceView ResourceView(const Core::GpuGraphResourceId id, const Core::GpuGraphResourceType::Enum type)noexcept{
    Core::GpuTaskGraphResourceView view{};
    view.id = id;
    view.type = type;
    return view;
}

[[nodiscard]] Core::GpuTaskResourceRange BufferRange(const u64 offset, const u64 size)noexcept{
    return Core::GpuTaskResourceRange{ .bufferRange = Core::BufferRange(offset, size) };
}

void AppendBufferState(TrackedStates& states, TrackedResourceStateHistory& history, const Core::GpuGraphResourceId resource, const u64 offset, const u64 size){
    ASSERT_TRUE(history.append(TrackedCompiledResourceState{
        .resource = resource,
        .range = BufferRange(offset, size),
        .task = Core::GpuTaskId{ .generation = 1u, .index = static_cast<u32>(states.size()) },
        .state = Core::ResourceStates::CopyDest,
        .access = Core::GpuTaskResourceAccess::Write,
        .queue = Core::GpuPhysicalQueueId{ .index = 0u, .deviceGeneration = 1u },
    }));
}

void ExpectBufferFragments(const Span<const TrackedResourceStateFragment> fragments, const Span<const TrackedCompiledResourceState> states, const Span<const ExpectedBufferFragment> expected){
    ASSERT_EQ(fragments.size(), expected.size());
    for(usize index = 0u; index < expected.size(); ++index){
        SCOPED_TRACE(index);
        EXPECT_EQ(fragments[index].stateIndex, expected[index].stateIndex);
        EXPECT_EQ(fragments[index].range.bufferRange, Core::BufferRange(expected[index].offset, expected[index].size));
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
    TrackedResourceStateHistory history(states, 2u, 1u, scratchArena);
    AppendBufferState(states, history, s_Buffer, 16u, 96u);
    AppendBufferState(states, history, s_OtherBuffer, 0u, 128u);
    AppendBufferState(states, history, s_Buffer, 32u, 16u);
    AppendBufferState(states, history, s_Buffer, 64u, 16u);
    AppendBufferState(states, history, s_OtherBuffer, 16u, 16u);
    RequestedRanges requested(scratchArena);
    requested.push_back(BufferRange(0u, 128u));
    StateFragments fragments(scratchArena);
    const Core::GpuTaskGraphResourceView resource = ResourceView(s_Buffer, Core::GpuGraphResourceType::Buffer);

    ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, history, resource, requested, scratchArena, fragments));
    const Array<ExpectedBufferFragment, 7u> expected = {
        ExpectedBufferFragment{ .stateIndex = 0u, .offset = 16u, .size = 16u },
        ExpectedBufferFragment{ .stateIndex = 0u, .offset = 48u, .size = 16u },
        ExpectedBufferFragment{ .stateIndex = 0u, .offset = 80u, .size = 32u },
        ExpectedBufferFragment{ .stateIndex = 2u, .offset = 32u, .size = 16u },
        ExpectedBufferFragment{ .stateIndex = 3u, .offset = 64u, .size = 16u },
        ExpectedBufferFragment{ .stateIndex = s_InitialState, .offset = 0u, .size = 16u },
        ExpectedBufferFragment{ .stateIndex = s_InitialState, .offset = 112u, .size = 16u },
    };
    ExpectBufferFragments(fragments, states, expected);

    ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::CollectTerminalResourceStateFragments(states, history, resource, scratchArena, fragments));
    const Array<ExpectedBufferFragment, 5u> terminal = {
        ExpectedBufferFragment{ .stateIndex = 0u, .offset = 16u, .size = 16u },
        ExpectedBufferFragment{ .stateIndex = 0u, .offset = 48u, .size = 16u },
        ExpectedBufferFragment{ .stateIndex = 0u, .offset = 80u, .size = 32u },
        ExpectedBufferFragment{ .stateIndex = 2u, .offset = 32u, .size = 16u },
        ExpectedBufferFragment{ .stateIndex = 3u, .offset = 64u, .size = 16u },
    };
    ExpectBufferFragments(fragments, states, terminal);
}

TEST(GpuTaskGraphResourceFragments, SparseProducerIndicesRetainUnsortedRequestOrderWithinEachProducer){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TrackedStates states(scratchArena);
    states.reserve(130u);
    TrackedResourceStateHistory history(states, 2u, 1u, scratchArena);
    AppendBufferState(states, history, s_Buffer, 16u, 96u);
    for(usize index = 1u; index < 129u; ++index)
        AppendBufferState(states, history, s_OtherBuffer, index * 16u, 16u);
    AppendBufferState(states, history, s_Buffer, 120u, 8u);
    RequestedRanges requested(scratchArena);
    requested.reserve(6u);
    requested.push_back(BufferRange(80u, 8u));
    requested.push_back(BufferRange(16u, 8u));
    requested.push_back(BufferRange(120u, 8u));
    requested.push_back(BufferRange(48u, 8u));
    requested.push_back(BufferRange(144u, 8u));
    requested.push_back(BufferRange(0u, 8u));
    StateFragments fragments(scratchArena);
    const Core::GpuTaskGraphResourceView resource = ResourceView(s_Buffer, Core::GpuGraphResourceType::Buffer);

    ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, history, resource, requested, scratchArena, fragments));
    const Array<ExpectedBufferFragment, 6u> expected = {
        ExpectedBufferFragment{ .stateIndex = 0u, .offset = 80u, .size = 8u },
        ExpectedBufferFragment{ .stateIndex = 0u, .offset = 16u, .size = 8u },
        ExpectedBufferFragment{ .stateIndex = 0u, .offset = 48u, .size = 8u },
        ExpectedBufferFragment{ .stateIndex = 129u, .offset = 120u, .size = 8u },
        ExpectedBufferFragment{ .stateIndex = s_InitialState, .offset = 144u, .size = 8u },
        ExpectedBufferFragment{ .stateIndex = s_InitialState, .offset = 0u, .size = 8u },
    };
    ExpectBufferFragments(fragments, states, expected);
}

TEST(GpuTaskGraphResourceFragments, EmptyAndUntrackedRequestsClearPriorOutputAndPreserveInitialOrder){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TrackedStates states(scratchArena);
    TrackedResourceStateHistory history(states, 2u, 1u, scratchArena);
    RequestedRanges requested(scratchArena);
    requested.reserve(2u);
    requested.push_back(BufferRange(64u, 8u));
    requested.push_back(BufferRange(0u, 8u));
    StateFragments fragments(scratchArena);
    const Core::GpuTaskGraphResourceView resource = ResourceView(s_Buffer, Core::GpuGraphResourceType::Buffer);

    ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, history, resource, requested, scratchArena, fragments));
    const Array<ExpectedBufferFragment, 2u> expected = {
        ExpectedBufferFragment{ .stateIndex = s_InitialState, .offset = 64u, .size = 8u },
        ExpectedBufferFragment{ .stateIndex = s_InitialState, .offset = 0u, .size = 8u },
    };
    ExpectBufferFragments(fragments, states, expected);
    AppendBufferState(states, history, s_OtherBuffer, 0u, 128u);
    ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, history, resource, requested, scratchArena, fragments));
    ExpectBufferFragments(fragments, states, expected);
    requested.clear();
    ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, history, resource, requested, scratchArena, fragments));
    EXPECT_TRUE(fragments.empty());
    ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::CollectTerminalResourceStateFragments(states, history, resource, scratchArena, fragments));
    EXPECT_TRUE(fragments.empty());
}

TEST(GpuTaskGraphResourceFragments, SymbolicBufferTailRemainsAfterFiniteRemainderWithinOldestProducer){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TrackedStates states(scratchArena);
    states.reserve(2u);
    TrackedResourceStateHistory history(states, 2u, 1u, scratchArena);
    AppendBufferState(states, history, s_Buffer, 0u, Core::BufferRange::s_AllBytes);
    AppendBufferState(states, history, s_Buffer, 16u, 16u);
    RequestedRanges requested(scratchArena);
    requested.push_back(BufferRange(0u, Core::BufferRange::s_AllBytes));
    StateFragments fragments(scratchArena);
    const Core::GpuTaskGraphResourceView resource = ResourceView(s_Buffer, Core::GpuGraphResourceType::Buffer);
    const Array<ExpectedBufferFragment, 3u> expected = {
        ExpectedBufferFragment{ .stateIndex = 0u, .offset = 0u, .size = 16u },
        ExpectedBufferFragment{ .stateIndex = 0u, .offset = 32u, .size = Core::BufferRange::s_AllBytes },
        ExpectedBufferFragment{ .stateIndex = 1u, .offset = 16u, .size = 16u },
    };

    ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, history, resource, requested, scratchArena, fragments));
    ExpectBufferFragments(fragments, states, expected);
    ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::CollectTerminalResourceStateFragments(states, history, resource, scratchArena, fragments));
    ExpectBufferFragments(fragments, states, expected);
}

TEST(GpuTaskGraphResourceFragments, TextureInteriorOverwritePreservesFourRemaindersInDiscoveryOrder){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TrackedStates states(scratchArena);
    states.reserve(2u);
    TrackedResourceStateHistory history(states, 2u, 1u, scratchArena);
    const Core::GpuTaskResourceRange whole{ .textureSubresources = Core::TextureSubresourceSet(0u, 6u, 0u, 5u) };
    const Core::GpuTaskResourceRange center{ .textureSubresources = Core::TextureSubresourceSet(2u, 2u, 1u, 3u) };
    ASSERT_TRUE(history.append(TrackedCompiledResourceState{
        .resource = s_Buffer,
        .range = whole,
        .task = Core::GpuTaskId{ .generation = 1u, .index = 0u },
        .state = Core::ResourceStates::CopyDest,
        .access = Core::GpuTaskResourceAccess::Write,
        .queue = Core::GpuPhysicalQueueId{ .index = 0u, .deviceGeneration = 1u },
    }));
    ASSERT_TRUE(history.append(TrackedCompiledResourceState{
        .resource = s_Buffer,
        .range = center,
        .task = Core::GpuTaskId{ .generation = 1u, .index = 1u },
        .state = Core::ResourceStates::CopyDest,
        .access = Core::GpuTaskResourceAccess::Write,
        .queue = Core::GpuPhysicalQueueId{ .index = 0u, .deviceGeneration = 1u },
    }));
    RequestedRanges requested(scratchArena);
    requested.push_back(whole);
    StateFragments fragments(scratchArena);
    const Core::GpuTaskGraphResourceView resource = ResourceView(s_Buffer, Core::GpuGraphResourceType::Texture);
    const Array<Core::TextureSubresourceSet, 5u> expected = {
        Core::TextureSubresourceSet(0u, 2u, 0u, 5u),
        Core::TextureSubresourceSet(4u, 2u, 0u, 5u),
        Core::TextureSubresourceSet(2u, 2u, 0u, 1u),
        Core::TextureSubresourceSet(2u, 2u, 4u, 1u),
        center.textureSubresources,
    };

    for(usize terminal = 0u; terminal < 2u; ++terminal){
        SCOPED_TRACE(terminal);
        const b8 collected = terminal == 0u
            ? Core::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, history, resource, requested, scratchArena, fragments)
            : Core::GpuTaskGraphCompilerDetail::CollectTerminalResourceStateFragments(states, history, resource, scratchArena, fragments)
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

TEST(GpuTaskGraphResourceFragments, IndexedHistoryKeepsGlobalOrderAcrossIncrementalGrowthAndUnrelatedStates){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TrackedStates states(scratchArena);
    states.reserve(1u);
    TrackedResourceStateHistory history(states, 2u, 1u, scratchArena);
    AppendBufferState(states, history, s_Buffer, 0u, 128u);
    RequestedRanges requested(scratchArena);
    requested.push_back(BufferRange(0u, 128u));
    StateFragments fragments(scratchArena);
    const Core::GpuTaskGraphResourceView resource = ResourceView(s_Buffer, Core::GpuGraphResourceType::Buffer);
    ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, history, resource, requested, scratchArena, fragments));
    const Array<ExpectedBufferFragment, 1u> before = { ExpectedBufferFragment{ .stateIndex = 0u, .offset = 0u, .size = 128u } };
    ExpectBufferFragments(fragments, states, before);

    for(usize index = 0u; index < 257u; ++index)
        AppendBufferState(states, history, s_OtherBuffer, index * 16u, 16u);
    AppendBufferState(states, history, s_Buffer, 32u, 16u);
    states.reserve(states.capacity() + 1024u);
    AppendBufferState(states, history, s_OtherBuffer, 0u, 8u);
    AppendBufferState(states, history, s_Buffer, 80u, 16u);
    ASSERT_TRUE(history.validFor(states));
    const Array<ExpectedBufferFragment, 5u> after = {
        ExpectedBufferFragment{ .stateIndex = 0u, .offset = 0u, .size = 32u },
        ExpectedBufferFragment{ .stateIndex = 0u, .offset = 48u, .size = 32u },
        ExpectedBufferFragment{ .stateIndex = 0u, .offset = 96u, .size = 32u },
        ExpectedBufferFragment{ .stateIndex = 258u, .offset = 32u, .size = 16u },
        ExpectedBufferFragment{ .stateIndex = 260u, .offset = 80u, .size = 16u },
    };
    ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, history, resource, requested, scratchArena, fragments));
    ExpectBufferFragments(fragments, states, after);
    ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::CollectTerminalResourceStateFragments(states, history, resource, scratchArena, fragments));
    ExpectBufferFragments(fragments, states, after);
}

TEST(GpuTaskGraphResourceFragments, IndexedHistoryRejectsForeignGenerationAndInvalidResourcesWithoutPublication){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TrackedStates states(scratchArena);
    states.reserve(2u);
    TrackedResourceStateHistory history(states, 2u, 1u, scratchArena);
    TrackedCompiledResourceState state{
        .resource = s_Buffer,
        .range = BufferRange(0u, 16u),
        .task = Core::GpuTaskId{ .generation = 1u, .index = 0u },
        .state = Core::ResourceStates::CopyDest,
        .access = Core::GpuTaskResourceAccess::Write,
        .queue = Core::GpuPhysicalQueueId{ .index = 0u, .deviceGeneration = 1u },
    };
    ASSERT_TRUE(history.append(state));
    for(
        const Core::GpuGraphResourceId invalid : {
            Core::GpuGraphResourceId{ .generation = 2u, .index = 0u },
            Core::GpuGraphResourceId{ .generation = 1u, .index = 2u },
            Core::GpuGraphResourceId{},
        }
    ){
        state.resource = invalid;
        EXPECT_FALSE(history.append(state));
        EXPECT_EQ(states.size(), 1u);
        EXPECT_EQ(history.first(s_Buffer), 0u);
        EXPECT_EQ(history.last(s_Buffer), 0u);
        EXPECT_EQ(history.next(0u), s_InitialState);
    }
    RequestedRanges requested(scratchArena);
    requested.push_back(BufferRange(0u, 16u));
    StateFragments fragments(scratchArena);
    const Core::GpuTaskGraphResourceView foreign = ResourceView({ 0u, 2u }, Core::GpuGraphResourceType::Buffer);
    EXPECT_FALSE(Core::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, history, foreign, requested, scratchArena, fragments));
    EXPECT_FALSE(Core::GpuTaskGraphCompilerDetail::CollectTerminalResourceStateFragments(states, history, foreign, scratchArena, fragments));
}

TEST(GpuTaskGraphResourceFragments, IndexedHistoryRejectsMismatchedVectorAndBypassedAppend){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TrackedStates states(scratchArena);
    TrackedStates otherStates(scratchArena);
    states.reserve(2u);
    otherStates.reserve(1u);
    TrackedResourceStateHistory history(states, 2u, 1u, scratchArena);
    AppendBufferState(states, history, s_Buffer, 0u, 16u);
    otherStates.push_back(states.front());
    EXPECT_FALSE(history.validFor(otherStates));
    RequestedRanges requested(scratchArena);
    requested.push_back(BufferRange(0u, 16u));
    StateFragments fragments(scratchArena);
    const Core::GpuTaskGraphResourceView resource = ResourceView(s_Buffer, Core::GpuGraphResourceType::Buffer);
    EXPECT_FALSE(Core::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(otherStates, history, resource, requested, scratchArena, fragments));
    EXPECT_FALSE(Core::GpuTaskGraphCompilerDetail::CollectTerminalResourceStateFragments(otherStates, history, resource, scratchArena, fragments));

    states.push_back(states.front());
    EXPECT_FALSE(history.validFor(states));
    EXPECT_FALSE(history.append(states.front()));
    EXPECT_EQ(states.size(), 2u);
    EXPECT_FALSE(Core::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, history, resource, requested, scratchArena, fragments));
    EXPECT_FALSE(Core::GpuTaskGraphCompilerDetail::CollectTerminalResourceStateFragments(states, history, resource, scratchArena, fragments));
}

TEST(GpuTaskGraphResourceFragments, IndexedHistorySkipsUnrelatedInvalidRangesButStillRejectsSelectedInvalidRange){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TrackedStates states(scratchArena);
    states.reserve(2u);
    TrackedResourceStateHistory history(states, 2u, 1u, scratchArena);
    AppendBufferState(states, history, s_Buffer, 16u, 16u);
    AppendBufferState(states, history, s_OtherBuffer, 0u, 0u);
    RequestedRanges requested(scratchArena);
    requested.push_back(BufferRange(0u, 64u));
    StateFragments fragments(scratchArena);
    const Core::GpuTaskGraphResourceView resource = ResourceView(s_Buffer, Core::GpuGraphResourceType::Buffer);
    const Array<ExpectedBufferFragment, 3u> expected = {
        ExpectedBufferFragment{ .stateIndex = 0u, .offset = 16u, .size = 16u },
        ExpectedBufferFragment{ .stateIndex = s_InitialState, .offset = 0u, .size = 16u },
        ExpectedBufferFragment{ .stateIndex = s_InitialState, .offset = 32u, .size = 32u },
    };
    ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, history, resource, requested, scratchArena, fragments));
    ExpectBufferFragments(fragments, states, expected);
    const Array<ExpectedBufferFragment, 1u> terminal = { ExpectedBufferFragment{ .stateIndex = 0u, .offset = 16u, .size = 16u } };
    ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::CollectTerminalResourceStateFragments(states, history, resource, scratchArena, fragments));
    ExpectBufferFragments(fragments, states, terminal);

    const Core::GpuTaskGraphResourceView other = ResourceView(s_OtherBuffer, Core::GpuGraphResourceType::Buffer);
    EXPECT_FALSE(Core::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, history, other, requested, scratchArena, fragments));
    EXPECT_FALSE(Core::GpuTaskGraphCompilerDetail::CollectTerminalResourceStateFragments(states, history, other, scratchArena, fragments));
}

TEST(GpuTaskGraphResourceFragments, LatestCoveredRangeNormalizesInactiveDimensionsAndPreservesPriorOutputOnFailure){
    const Array<Core::GpuGraphResourceType::Enum, 2u> types = { Core::GpuGraphResourceType::Buffer, Core::GpuGraphResourceType::Texture, };
    for(const Core::GpuGraphResourceType::Enum type : types){
        SCOPED_TRACE(type);
        Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
        TrackedStates states(scratchArena);
        states.reserve(3u);
        TrackedResourceStateHistory history(states, 2u, 1u, scratchArena);
        TrackedCompiledResourceState state{
            .resource = s_Buffer,
            .range = {},
            .task = Core::GpuTaskId{ .generation = 1u, .index = 0u },
            .state = Core::ResourceStates::CopyDest,
            .access = Core::GpuTaskResourceAccess::Write,
            .queue = Core::GpuPhysicalQueueId{ .index = 0u, .deviceGeneration = 1u },
        };
        ASSERT_TRUE(history.append(state));
        state.range.bufferRange = Core::BufferRange(16u, Core::BufferRange::s_AllBytes);
        state.range.textureSubresources = Core::TextureSubresourceSet(1u, Core::TextureSubresourceSet::s_AllMipLevels, 2u, Core::TextureSubresourceSet::s_AllArraySlices);
        state.task.index = 1u;
        ASSERT_TRUE(history.append(state));
        state.resource = s_OtherBuffer;
        state.task.index = 2u;
        ASSERT_TRUE(history.append(state));
        RequestedRanges requested(scratchArena);
        requested.reserve(1u);
        requested.push_back(Core::GpuTaskResourceRange{
            .textureSubresources = Core::TextureSubresourceSet(
                3u,
                type == Core::GpuGraphResourceType::Texture ? Core::TextureSubresourceSet::s_AllMipLevels : 1u,
                4u,
                type == Core::GpuGraphResourceType::Texture ? Core::TextureSubresourceSet::s_AllArraySlices : 1u
            ),
            .bufferRange = Core::BufferRange(32u, type == Core::GpuGraphResourceType::Buffer ? Core::BufferRange::s_AllBytes : 8u),
        });
        StateFragments fragments(scratchArena);
        fragments.reserve(2u);
        fragments.push_back(TrackedResourceStateFragment{ .range = {}, .state = &states[0u], .stateIndex = 0u });
        fragments.push_back(fragments.front());
        const Core::GpuTaskGraphResourceView resource = ResourceView(s_Buffer, type);
        ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, history, resource, requested, scratchArena, fragments));
        ASSERT_EQ(fragments.size(), 1u);
        EXPECT_EQ(fragments.front().state, &states[1u]);
        EXPECT_EQ(fragments.front().stateIndex, 1u);
        const Core::GpuTaskResourceRange expected{
            .textureSubresources = type == Core::GpuGraphResourceType::Texture
                ? requested.front().textureSubresources : Core::GpuTaskResourceRange{}.textureSubresources,
            .bufferRange = type == Core::GpuGraphResourceType::Buffer
                ? requested.front().bufferRange : Core::GpuTaskResourceRange{}.bufferRange,
        };
        EXPECT_EQ(fragments.front().range.textureSubresources, expected.textureSubresources);
        EXPECT_EQ(fragments.front().range.bufferRange, expected.bufferRange);
        const Core::GpuTaskResourceRange validRequestedRange = requested.front();
        if(type == Core::GpuGraphResourceType::Buffer)
            requested.front().bufferRange = Core::BufferRange(0u, 0u);
        else
            requested.front().textureSubresources = Core::TextureSubresourceSet(0u, 0u, 0u, 1u);
        EXPECT_FALSE(Core::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, history, resource, requested, scratchArena, fragments));
        requested.front() = validRequestedRange;
        states[1u].range = requested.front();
        if(type == Core::GpuGraphResourceType::Buffer)
            states[1u].range.bufferRange = Core::BufferRange(0u, 0u);
        else
            states[1u].range.textureSubresources = Core::TextureSubresourceSet(0u, 0u, 0u, 1u);
        EXPECT_FALSE(Core::GpuTaskGraphCompilerDetail::CollectLatestResourceStateFragments(states, history, resource, requested, scratchArena, fragments));
        ASSERT_EQ(fragments.size(), 1u);
        EXPECT_EQ(fragments.front().state, &states[1u]);
        EXPECT_EQ(fragments.front().stateIndex, 1u);
        EXPECT_EQ(fragments.front().range.textureSubresources, expected.textureSubresources);
        EXPECT_EQ(fragments.front().range.bufferRange, expected.bufferRange);
    }
}

TEST(GpuTaskGraphResourceFragments, SixtyFourBuffersPreserveExactTerminalExportSourceOrderAcrossPartialWrites){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), DedicatedComputeQueue() };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues) };
    constexpr usize s_BufferCount = 64u;
    Array<Core::GpuGraphResourceId, s_BufferCount> buffers;
    Array<Core::GpuTaskResourceUse, s_BufferCount> wholeUses;
    Array<Core::GpuTaskResourceUse, s_BufferCount * 2u> cutUses;
    Array<Core::GpuTaskResourceUse, s_BufferCount> readUses;
    for(usize index = 0u; index < s_BufferCount; ++index){
        const auto identityNameResult = IndexedName(Name(TaskGraphTestUtils::s_GpuCaseFragmentOrderBuffer), index);
        ASSERT_TRUE(identityNameResult);
        const Name identity = *identityNameResult;
        const auto gpuTaskAdmission = graph.importResource(Core::GpuGraphResourceDesc{}
            .setIdentity(identity)
            .setMarkerLabel("Fragment Order Buffer")
            .setType(Core::GpuGraphResourceType::Buffer)
            .setInitialState(Core::ResourceStates::Common)
            .setExternalFinalState(Core::ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(queues[1u].id)
        );
        ASSERT_TRUE(gpuTaskAdmission);
        buffers[index] = *gpuTaskAdmission;
        ASSERT_TRUE(buffers[index].valid());
        wholeUses[index] = Core::GpuTaskResourceUse{ .resource = buffers[index], .range = BufferRange(0u, 128u), .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write, };
        cutUses[index * 2u] = Core::GpuTaskResourceUse{ .resource = buffers[index], .range = BufferRange(32u, 16u), .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::Write, };
        cutUses[index * 2u + 1u] = cutUses[index * 2u];
        cutUses[index * 2u + 1u].range = BufferRange(64u, 16u);
        readUses[index] = Core::GpuTaskResourceUse{ .resource = buffers[index], .range = BufferRange(48u, 16u), .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    }
    const Core::GpuTaskCommandRequirements commands{ .requiredCapabilities = Core::GpuQueueCapability::Graphics };
    Core::GpuTaskDesc wholeDesc;
    wholeDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseFragmentOrderWhole)).setMarkerLabel("Whole Writer").setResourceUses(wholeUses);
    const auto wholeResult = graph.addTask(wholeDesc, commands);
    ASSERT_TRUE(wholeResult);
    const Core::GpuTaskId whole = *wholeResult;
    ASSERT_TRUE(whole.valid());
    Core::GpuTaskDesc cutsDesc;
    cutsDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseFragmentOrderCuts)).setMarkerLabel("Partial Writers").setDependencies(Span(&whole, 1u)).setResourceUses(cutUses);
    const auto cutsResult = graph.addTask(cutsDesc, commands);
    ASSERT_TRUE(cutsResult);
    const Core::GpuTaskId cuts = *cutsResult;
    ASSERT_TRUE(cuts.valid());
    Core::GpuTaskDesc readerDesc;
    readerDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseFragmentOrderReader)).setMarkerLabel("Gap Reader").setDependencies(Span(&cuts, 1u)).setResourceUses(readUses);
    const auto readerResult = graph.addTask(readerDesc, commands);
    ASSERT_TRUE(readerResult);
    const Core::GpuTaskId reader = *readerResult;
    ASSERT_TRUE(reader.valid());
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView plan(compiledGraph);
    const Array<Core::GpuTaskId, 5u> expectedTasks = { whole, whole, cuts, cuts, reader };
    const Array<Core::BufferRange, 5u> expectedRanges = {
        Core::BufferRange(0u, 32u),
        Core::BufferRange(80u, 48u),
        Core::BufferRange(32u, 16u),
        Core::BufferRange(64u, 16u),
        Core::BufferRange(48u, 16u),
    };
    for(usize index = 0u; index < s_BufferCount; ++index){
        SCOPED_TRACE(index);
        const Core::GpuCompiledExternalResourceExportView exported = plan.externalResourceExport(buffers[index]);
        ASSERT_NE(exported.plan, nullptr);
        ASSERT_NE(exported.sources, nullptr);
        EXPECT_EQ(exported.plan->finalState, Core::ResourceStates::ShaderResource);
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

