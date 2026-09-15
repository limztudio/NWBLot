// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <core/task/gpu/compiler_internal.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_task_use_index_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;
using Graphics::GpuTaskGraphCompilerDetail::TaskResourceUseIndex;
using Graphics::GpuTaskGraphCompilerDetail::CollectResourceFirstUseRangesWithinTask;
using Ranges = Vector<Graphics::GpuTaskResourceRange, Core::Alloc::ScratchArena>;

constexpr usize s_NoUse = Limit<usize>::s_Max;
constexpr Graphics::GpuGraphResourceId s_A{ .generation = 1u, .index = 0u };
constexpr Graphics::GpuGraphResourceId s_B{ .generation = 1u, .index = 1u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Graphics::GpuTaskResourceUse BufferUse(
    const Graphics::GpuGraphResourceId resource,
    const u64 offset,
    const u64 size,
    const Graphics::ResourceStates::Mask state = Graphics::ResourceStates::CopyDest){
    return Graphics::GpuTaskResourceUse{
        .resource = resource,
        .range = { .bufferRange = Graphics::BufferRange(offset, size) },
        .requiredState = state,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
}

template<usize Count>
[[nodiscard]] Graphics::GpuTaskGraphTaskView TaskView(
    const Graphics::GpuTaskResourceUse (&uses)[Count],
    const u64 generation = 1u,
    const u32 taskIndex = 0u){
    Graphics::GpuTaskGraphTaskView view{};
    view.id = { .generation = generation, .index = taskIndex };
    view.resourceUses = uses;
    view.resourceUseCount = Count;
    return view;
}

template<usize Count>
void ExpectBufferRanges(const Ranges& ranges, const Graphics::BufferRange (&expected)[Count]){
    ASSERT_EQ(ranges.size(), Count);
    for(usize index = 0u; index < Count; ++index){
        SCOPED_TRACE(index);
        EXPECT_EQ(ranges[index].bufferRange, expected[index]);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskUseIndex, ChainsKeepAscendingDeclarationIndicesIncludingUnknownStates){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    const Graphics::GpuTaskResourceUse uses[] = {
        BufferUse(s_A, 64u, 16u), BufferUse(s_B, 0u, 16u),
        BufferUse(s_A, 32u, 16u, Graphics::ResourceStates::Unknown),
        BufferUse(s_B, 16u, 16u), BufferUse(s_A, 0u, 128u),
    };
    const Graphics::GpuTaskGraphTaskView task = TaskView(uses);
    TaskResourceUseIndex index(3u, 1u, LengthOf(uses), scratchArena);
    ASSERT_TRUE(index.build(task));
    ASSERT_TRUE(index.validFor(task));
    EXPECT_EQ(index.first(s_A), 0u);
    EXPECT_EQ(index.next(0u), 2u);
    EXPECT_EQ(index.next(2u), 4u);
    EXPECT_EQ(index.next(4u), s_NoUse);
    EXPECT_EQ(index.first(s_B), 1u);
    EXPECT_EQ(index.next(1u), 3u);
    EXPECT_EQ(index.next(3u), s_NoUse);
    EXPECT_EQ(index.first(Graphics::GpuGraphResourceId{ .generation = 1u, .index = 2u }), s_NoUse);
    EXPECT_EQ(index.next(LengthOf(uses)), s_NoUse);
}

TEST(GpuTaskUseIndex, RebuildResetsOnlyThePreviousTaskAndRejectsDetachedViews){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    const Graphics::GpuTaskResourceUse first[] = { BufferUse(s_A, 0u, 16u), BufferUse(s_A, 16u, 16u) };
    const Graphics::GpuTaskResourceUse second[] = { BufferUse(s_B, 0u, 8u) };
    const auto a = TaskView(first);
    const auto b = TaskView(second, 1u, 1u);
    TaskResourceUseIndex index(3u, 1u, LengthOf(first), scratchArena);
    ASSERT_TRUE(index.build(a));
    ASSERT_TRUE(index.build(b));
    EXPECT_FALSE(index.validFor(a));
    EXPECT_EQ(index.first(s_A), s_NoUse);
    EXPECT_EQ(index.first(s_B), 0u);
    EXPECT_EQ(index.next(0u), s_NoUse);
    Graphics::GpuTaskGraphTaskView detached = b;
    detached.id.index = 2u;
    EXPECT_FALSE(index.validFor(detached));
    detached = b;
    detached.resourceUses = first;
    EXPECT_FALSE(index.validFor(detached));
    detached = b;
    detached.resourceUseCount = 0u;
    EXPECT_FALSE(index.validFor(detached));
    Graphics::GpuTaskGraphTaskView empty{};
    empty.id = { .generation = 1u, .index = 2u };
    ASSERT_TRUE(index.build(empty));
    EXPECT_TRUE(index.validFor(empty));
    EXPECT_EQ(index.first(s_B), s_NoUse);
    ASSERT_TRUE(index.build(a));
    EXPECT_EQ(index.next(0u), 1u);
}

TEST(GpuTaskUseIndex, InvalidGenerationIdentityAndCapacityNeverPublishAUsableIndex){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    Graphics::GpuTaskResourceUse uses[] = { BufferUse(s_A, 0u, 16u), BufferUse(s_B, 0u, 8u) };
    const auto task = TaskView(uses);
    TaskResourceUseIndex index(2u, 1u, LengthOf(uses), scratchArena);
    for(
        const Graphics::GpuGraphResourceId invalid : {
            Graphics::GpuGraphResourceId{}, Graphics::GpuGraphResourceId{ .generation = 2u, .index = 0u }, Graphics::GpuGraphResourceId{ .generation = 1u, .index = 2u },
        }
    ){
        uses[0u].resource = invalid;
        EXPECT_FALSE(index.build(task));
        EXPECT_FALSE(index.validFor(task));
        EXPECT_EQ(index.first(s_B), s_NoUse);
        uses[0u].resource = s_A;
        ASSERT_TRUE(index.build(task));
    }
    Graphics::GpuTaskGraphTaskView invalid = task;
    invalid.id.generation = 2u;
    EXPECT_FALSE(index.build(invalid));
    invalid = task;
    invalid.id = {};
    EXPECT_FALSE(index.build(invalid));
    invalid = task;
    invalid.resourceUses = nullptr;
    EXPECT_FALSE(index.build(invalid));
    invalid = task;
    invalid.resourceUseCount = LengthOf(uses) + 1u;
    EXPECT_FALSE(index.build(invalid));
    ASSERT_TRUE(index.build(task));
    EXPECT_EQ(index.first(s_A), 0u);
    EXPECT_EQ(index.first(s_B), 1u);
}

TEST(GpuTaskUseIndex, FirstOccurrenceReturnsNormalizedRangeAndStillRejectsEmptyRequest){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto buffer = AddBufferMetadata(graph, Name("tests/task_use/first"), "First Use Buffer");
    ASSERT_TRUE(buffer.valid());
    const Graphics::GpuTaskResourceUse uses[] = { BufferUse(buffer, 32u, Graphics::BufferRange::AllBytes) };
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    const auto task = TaskView(uses, declarations.generation());
    const auto resource = declarations.resourceAt(buffer.index);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TaskResourceUseIndex index(declarations.resourceCount(), declarations.generation(), 1u, scratchArena);
    ASSERT_TRUE(index.build(task));
    Ranges ranges(scratchArena);
    ranges.reserve(1u);
    ASSERT_TRUE(CollectResourceFirstUseRangesWithinTask(declarations, task, index, 0u, resource, uses[0u].range, scratchArena, ranges));
    const Graphics::BufferRange expected[] = { { 32u, Graphics::BufferRange::AllBytes } };
    ExpectBufferRanges(ranges, expected);
    EXPECT_FALSE(CollectResourceFirstUseRangesWithinTask(declarations, task, index, 0u, resource, BufferUse(buffer, 0u, 0u).range, scratchArena, ranges));
    EXPECT_TRUE(ranges.empty());
    EXPECT_FALSE(CollectResourceFirstUseRangesWithinTask(declarations, task, index, 1u, resource, uses[0u].range, scratchArena, ranges));
}

TEST(GpuTaskUseIndex, PartialAndDisjointEarlierUsesRetainSubtractionOrderAndUnknownStateCoverage){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto buffer = AddBufferMetadata(graph, Name("tests/task_use/partial"), "Partial Use Buffer");
    const auto other = AddBufferMetadata(graph, Name("tests/task_use/other"), "Other Use Buffer");
    ASSERT_TRUE(buffer.valid());
    ASSERT_TRUE(other.valid());
    const Graphics::GpuTaskResourceUse uses[] = {
        BufferUse(buffer, 64u, 16u), BufferUse(other, 0u, 0u),
        BufferUse(buffer, 32u, 16u, Graphics::ResourceStates::Unknown),
        BufferUse(buffer, 160u, 16u), BufferUse(buffer, 0u, 128u), BufferUse(buffer, 0u, 0u),
    };
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    const auto task = TaskView(uses, declarations.generation());
    const auto resource = declarations.resourceAt(buffer.index);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TaskResourceUseIndex index(declarations.resourceCount(), declarations.generation(), LengthOf(uses), scratchArena);
    ASSERT_TRUE(index.build(task));
    Ranges ranges(scratchArena);
    ranges.reserve(3u);
    ASSERT_TRUE(CollectResourceFirstUseRangesWithinTask(declarations, task, index, 4u, resource, uses[4u].range, scratchArena, ranges));
    const Graphics::BufferRange expected[] = { { 0u, 32u }, { 48u, 16u }, { 80u, 48u } };
    ExpectBufferRanges(ranges, expected);
}

TEST(GpuTaskUseIndex, CompleteCoverageStopsBeforeLaterInvalidRangeButSelectedInvalidPrefixStillFails){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto buffer = AddBufferMetadata(graph, Name("tests/task_use/covered"), "Covered Buffer");
    ASSERT_TRUE(buffer.valid());
    Graphics::GpuTaskResourceUse uses[] = {
        BufferUse(buffer, 0u, 64u), BufferUse(buffer, 0u, 0u), BufferUse(buffer, 16u, 16u),
    };
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    const auto task = TaskView(uses, declarations.generation());
    const auto resource = declarations.resourceAt(buffer.index);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TaskResourceUseIndex index(declarations.resourceCount(), declarations.generation(), LengthOf(uses), scratchArena);
    ASSERT_TRUE(index.build(task));
    Ranges ranges(scratchArena);
    ranges.reserve(2u);
    ASSERT_TRUE(CollectResourceFirstUseRangesWithinTask(declarations, task, index, 2u, resource, uses[2u].range, scratchArena, ranges));
    EXPECT_TRUE(ranges.empty());
    uses[0u].range = BufferUse(buffer, 0u, 8u).range;
    ASSERT_TRUE(index.build(task));
    EXPECT_FALSE(CollectResourceFirstUseRangesWithinTask(declarations, task, index, 2u, resource, uses[2u].range, scratchArena, ranges));
}

TEST(GpuTaskUseIndex, SymbolicBufferTailSurvivesFinitePreviousUse){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto buffer = AddBufferMetadata(graph, Name("tests/task_use/symbolic"), "Symbolic Buffer");
    ASSERT_TRUE(buffer.valid());
    const Graphics::GpuTaskResourceUse uses[] = { BufferUse(buffer, 16u, 16u), BufferUse(buffer, 0u, Graphics::BufferRange::AllBytes) };
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    const auto task = TaskView(uses, declarations.generation());
    const auto resource = declarations.resourceAt(buffer.index);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TaskResourceUseIndex index(declarations.resourceCount(), declarations.generation(), LengthOf(uses), scratchArena);
    ASSERT_TRUE(index.build(task));
    Ranges ranges(scratchArena);
    ranges.reserve(2u);
    ASSERT_TRUE(CollectResourceFirstUseRangesWithinTask(declarations, task, index, 1u, resource, uses[1u].range, scratchArena, ranges));
    const Graphics::BufferRange expected[] = { { 0u, 16u }, { 32u, Graphics::BufferRange::AllBytes } };
    ExpectBufferRanges(ranges, expected);
}

TEST(GpuTaskUseIndex, TextureRectangleKeepsMipThenSliceRemainderOrder){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto texture = AddTextureMetadata(graph, Name("tests/task_use/rectangle"), "Texture Rectangle");
    ASSERT_TRUE(texture.valid());
    const Graphics::GpuTaskResourceUse uses[] = {
        { .resource = texture, .range = { .textureSubresources = Graphics::TextureSubresourceSet(1u, 1u, 1u, 1u) } },
        { .resource = texture, .range = { .textureSubresources = Graphics::TextureSubresourceSet(0u, 4u, 0u, 3u) } },
    };
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    const auto task = TaskView(uses, declarations.generation());
    const auto resource = declarations.resourceAt(texture.index);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TaskResourceUseIndex index(declarations.resourceCount(), declarations.generation(), LengthOf(uses), scratchArena);
    ASSERT_TRUE(index.build(task));
    Ranges ranges(scratchArena);
    ranges.reserve(4u);
    ASSERT_TRUE(CollectResourceFirstUseRangesWithinTask(declarations, task, index, 1u, resource, uses[1u].range, scratchArena, ranges));
    const Graphics::TextureSubresourceSet expected[] = {
        { 0u, 1u, 0u, 3u }, { 2u, 2u, 0u, 3u }, { 1u, 1u, 0u, 1u }, { 1u, 1u, 2u, 1u },
    };
    ASSERT_EQ(ranges.size(), LengthOf(expected));
    for(usize index = 0u; index < LengthOf(expected); ++index)
        EXPECT_EQ(ranges[index].textureSubresources, expected[index]);
}

TEST(GpuTaskUseIndex, CompiledResourceSetExpansionPreservesEveryInitialByteRange){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto a = AddBufferMetadata(graph, Name("tests/task_use/expanded_a"), "Expanded A", Graphics::ResourceStates::Common);
    const auto b = AddBufferMetadata(graph, Name("tests/task_use/expanded_b"), "Expanded B", Graphics::ResourceStates::Common);
    ASSERT_TRUE(a.valid());
    ASSERT_TRUE(b.valid());
    const Graphics::GpuGraphResourceId members[] = { a, b };
    const auto set = graph.importResourceSet(Graphics::GpuGraphResourceSetDesc{}
        .setIdentity(Name("tests/task_use/expanded_set"))
        .setMarkerLabel("Expanded Set")
        .setMembers(members, LengthOf(members))
    );
    ASSERT_TRUE(set.valid());
    const Graphics::GpuTaskResourceUse direct[] = { BufferUse(a, 16u, 16u), BufferUse(b, 0u, 8u) };
    const Graphics::GpuTaskResourceSetUse expanded{
        .resourceSet = set,
        .range = { .bufferRange = Graphics::BufferRange(0u, Graphics::BufferRange::AllBytes) },
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_use/expanded_task"))
        .setMarkerLabel("Expanded Task")
        .setResourceUses(direct, LengthOf(direct))
        .setResourceSetUses(&expanded, 1u)
        .setQueue(Graphics::GpuQueueRequest{ Graphics::GpuQueueCapability::Graphics, Graphics::GpuQueuePreference::Graphics, false, false })
    ;
    const auto task = graph.addTask(desc);
    ASSERT_TRUE(task.valid());
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView plan(compiledGraph);
    const auto view = plan.findTask(task);
    ASSERT_NE(view.plan, nullptr);
    ASSERT_EQ(view.plan->prologueBarrierCount, 5u);
    ASSERT_NE(view.prologueBarriers, nullptr);
    const Graphics::GpuGraphResourceId expectedResources[] = { a, b, a, a, b };
    const Graphics::BufferRange expectedRanges[] = {
        { 16u, 16u }, { 0u, 8u }, { 0u, 16u }, { 32u, Graphics::BufferRange::AllBytes }, { 8u, Graphics::BufferRange::AllBytes },
    };
    for(usize index = 0u; index < LengthOf(expectedRanges); ++index){
        SCOPED_TRACE(index);
        const auto& barrier = view.prologueBarriers[index];
        EXPECT_EQ(barrier.resource, expectedResources[index]);
        EXPECT_EQ(barrier.range.bufferRange, expectedRanges[index]);
        EXPECT_EQ(barrier.before, Graphics::ResourceStates::Common);
        EXPECT_EQ(barrier.after, index < 2u ? Graphics::ResourceStates::CopyDest : Graphics::ResourceStates::ShaderResource);
        EXPECT_TRUE(barrier.isGraphInitialState);
    }
}

TEST(GpuTaskUseIndex, RepeatedAccelerationStructureUsesKeepOneInitialTransitionAndLastStateExport){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto acceleration = graph.importResource(Graphics::GpuGraphResourceDesc{}
        .setIdentity(Name("tests/task_use/acceleration"))
        .setMarkerLabel("Repeated Acceleration")
        .setType(Graphics::GpuGraphResourceType::AccelStruct)
        .setInitialState(Graphics::ResourceStates::Common)
        .setExternalFinalState(Graphics::ResourceStates::AccelStructRead)
    );
    const auto other = AddBufferMetadata(graph, Name("tests/task_use/acceleration_other"), "Unrelated Buffer", Graphics::ResourceStates::Common);
    ASSERT_TRUE(acceleration.valid());
    ASSERT_TRUE(other.valid());
    const Graphics::GpuTaskResourceUse uses[] = {
        { .resource = acceleration, .range = {}, .requiredState = Graphics::ResourceStates::AccelStructWrite, .access = Graphics::GpuTaskResourceAccess::Write },
        BufferUse(other, 0u, 16u),
        { .resource = acceleration, .range = {}, .requiredState = Graphics::ResourceStates::AccelStructRead, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = acceleration, .range = {}, .requiredState = Graphics::ResourceStates::AccelStructWrite, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    const auto task = AddTask(graph, Name("tests/task_use/acceleration_task"), "Repeated Acceleration Task", nullptr, 0u, uses, LengthOf(uses));
    ASSERT_TRUE(task.valid());
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView plan(compiledGraph);
    const auto view = plan.findTask(task);
    ASSERT_NE(view.plan, nullptr);
    ASSERT_EQ(view.plan->prologueBarrierCount, 2u);
    ASSERT_NE(view.prologueBarriers, nullptr);
    EXPECT_EQ(view.prologueBarriers[0u].resource, acceleration);
    EXPECT_EQ(view.prologueBarriers[0u].before, Graphics::ResourceStates::Common);
    EXPECT_EQ(view.prologueBarriers[0u].after, Graphics::ResourceStates::AccelStructWrite);
    EXPECT_EQ(view.prologueBarriers[1u].resource, other);
    ASSERT_EQ(view.plan->epilogueBarrierCount, 1u);
    ASSERT_NE(view.epilogueBarriers, nullptr);
    EXPECT_EQ(view.epilogueBarriers[0u].resource, acceleration);
    EXPECT_EQ(view.epilogueBarriers[0u].before, Graphics::ResourceStates::AccelStructWrite);
    EXPECT_EQ(view.epilogueBarriers[0u].after, Graphics::ResourceStates::AccelStructRead);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

