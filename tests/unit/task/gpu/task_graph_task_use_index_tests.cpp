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


namespace __hidden_task_graph_task_use_index_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;
using Core::GpuTaskGraphCompilerDetail::TaskResourceUseIndex;
using Core::GpuTaskGraphCompilerDetail::CollectResourceFirstUseRangesWithinTask;
using Ranges = Vector<Core::GpuTaskResourceRange, Core::Alloc::ScratchArena>;

constexpr usize s_NoUse = Limit<usize>::s_Max;
constexpr Core::GpuGraphResourceId s_A{ .generation = 1u, .index = 0u };
constexpr Core::GpuGraphResourceId s_B{ .generation = 1u, .index = 1u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Core::GpuTaskResourceUse BufferUse(const Core::GpuGraphResourceId resource, const u64 offset, const u64 size, const Core::ResourceStates::Mask state = Core::ResourceStates::CopyDest)noexcept{
    return Core::GpuTaskResourceUse{ .resource = resource, .range = { .bufferRange = Core::BufferRange(offset, size) }, .requiredState = state, .access = Core::GpuTaskResourceAccess::Write, };
}

[[nodiscard]] Core::GpuTaskGraphTaskView TaskView(const Span<const Core::GpuTaskResourceUse> uses, const u64 generation = 1u, const u32 taskIndex = 0u)noexcept{
    Core::GpuTaskGraphTaskView view{};
    view.id = { .generation = generation, .index = taskIndex };
    view.resourceUses = uses.data();
    view.resourceUseCount = uses.size();
    return view;
}

void ExpectBufferRanges(const Span<const Core::GpuTaskResourceRange> ranges, const Span<const Core::BufferRange> expected){
    ASSERT_EQ(ranges.size(), expected.size());
    for(usize index = 0u; index < expected.size(); ++index){
        SCOPED_TRACE(index);
        EXPECT_EQ(ranges[index].bufferRange, expected[index]);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskUseIndex, RebuildResetsOnlyThePreviousTaskAndRejectsDetachedViews){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    const Array<Core::GpuTaskResourceUse, 2u> first = { BufferUse(s_A, 0u, 16u), BufferUse(s_A, 16u, 16u) };
    const Array<Core::GpuTaskResourceUse, 1u> second = { BufferUse(s_B, 0u, 8u) };
    const auto a = TaskView(first);
    const auto b = TaskView(second, 1u, 1u);
    TaskResourceUseIndex index(3u, 1u, LengthOf(first), scratchArena);
    ASSERT_TRUE(index.build(a));
    EXPECT_EQ(index.first(Core::GpuGraphResourceId{ .generation = 1u, .index = 2u }), s_NoUse);
    EXPECT_EQ(index.next(LengthOf(first)), s_NoUse);
    ASSERT_TRUE(index.build(b));
    EXPECT_FALSE(index.validFor(a));
    EXPECT_EQ(index.first(s_A), s_NoUse);
    EXPECT_EQ(index.first(s_B), 0u);
    EXPECT_EQ(index.next(0u), s_NoUse);
    Core::GpuTaskGraphTaskView detached = b;
    detached.id.index = 2u;
    EXPECT_FALSE(index.validFor(detached));
    detached = b;
    detached.resourceUses = first.data();
    EXPECT_FALSE(index.validFor(detached));
    detached = b;
    detached.resourceUseCount = 0u;
    EXPECT_FALSE(index.validFor(detached));
    Core::GpuTaskGraphTaskView empty{};
    empty.id = { .generation = 1u, .index = 2u };
    ASSERT_TRUE(index.build(empty));
    EXPECT_TRUE(index.validFor(empty));
    EXPECT_EQ(index.first(s_B), s_NoUse);
    ASSERT_TRUE(index.build(a));
    EXPECT_EQ(index.next(0u), 1u);
}

TEST(GpuTaskUseIndex, InvalidGenerationIdentityAndCapacityNeverPublishAUsableIndex){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    Array<Core::GpuTaskResourceUse, 2u> uses = { BufferUse(s_A, 0u, 16u), BufferUse(s_B, 0u, 8u) };
    const auto task = TaskView(uses);
    TaskResourceUseIndex index(2u, 1u, LengthOf(uses), scratchArena);
    for(
        const Core::GpuGraphResourceId invalid : {
            Core::GpuGraphResourceId{}, Core::GpuGraphResourceId{ .generation = 2u, .index = 0u }, Core::GpuGraphResourceId{ .generation = 1u, .index = 2u },
        }
    ){
        uses[0u].resource = invalid;
        EXPECT_FALSE(index.build(task));
        EXPECT_FALSE(index.validFor(task));
        EXPECT_EQ(index.first(s_B), s_NoUse);
        uses[0u].resource = s_A;
        ASSERT_TRUE(index.build(task));
    }
    Core::GpuTaskGraphTaskView invalid = task;
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
    Core::GpuTaskGraph graph(testArena.arena);
    const auto bufferResult = AddBufferMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskUseFirst), "First Use Buffer");
    ASSERT_TRUE(bufferResult);
    const auto buffer = *bufferResult;
    ASSERT_TRUE(buffer.valid());
    const Array<Core::GpuTaskResourceUse, 1u> uses = { BufferUse(buffer, 32u, Core::BufferRange::s_AllBytes) };
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    const auto task = TaskView(uses, declarations.generation());
    const auto resource = declarations.resourceAt(buffer.index);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TaskResourceUseIndex index(declarations.resourceCount(), declarations.generation(), 1u, scratchArena);
    ASSERT_TRUE(index.build(task));
    Ranges ranges(scratchArena);
    ranges.reserve(1u);
    ASSERT_TRUE(CollectResourceFirstUseRangesWithinTask(declarations, task, index, 0u, resource, uses[0u].range, scratchArena, ranges));
    const Array<Core::BufferRange, 1u> expected = { Core::BufferRange{ 32u, Core::BufferRange::s_AllBytes } };
    ExpectBufferRanges(ranges, expected);
    EXPECT_FALSE(CollectResourceFirstUseRangesWithinTask(declarations, task, index, 0u, resource, BufferUse(buffer, 0u, 0u).range, scratchArena, ranges));
    EXPECT_TRUE(ranges.empty());
    EXPECT_FALSE(CollectResourceFirstUseRangesWithinTask(declarations, task, index, 1u, resource, uses[0u].range, scratchArena, ranges));
}

TEST(GpuTaskUseIndex, PartialAndDisjointEarlierUsesRetainSubtractionOrderAndUnknownStateCoverage){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto bufferResult = AddBufferMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskUsePartial), "Partial Use Buffer");
    ASSERT_TRUE(bufferResult);
    const auto buffer = *bufferResult;
    const auto otherResult = AddBufferMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskUseOther), "Other Use Buffer");
    ASSERT_TRUE(otherResult);
    const auto other = *otherResult;
    ASSERT_TRUE(buffer.valid());
    ASSERT_TRUE(other.valid());
    const Array<Core::GpuTaskResourceUse, 6u> uses = {
        BufferUse(buffer, 64u, 16u), BufferUse(other, 0u, 0u),
        BufferUse(buffer, 32u, 16u, Core::ResourceStates::Unknown),
        BufferUse(buffer, 160u, 16u), BufferUse(buffer, 0u, 128u), BufferUse(buffer, 0u, 0u),
    };
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    const auto task = TaskView(uses, declarations.generation());
    const auto resource = declarations.resourceAt(buffer.index);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TaskResourceUseIndex index(declarations.resourceCount(), declarations.generation(), LengthOf(uses), scratchArena);
    ASSERT_TRUE(index.build(task));
    Ranges ranges(scratchArena);
    ranges.reserve(3u);
    ASSERT_TRUE(CollectResourceFirstUseRangesWithinTask(declarations, task, index, 4u, resource, uses[4u].range, scratchArena, ranges));
    const Array<Core::BufferRange, 3u> expected = { Core::BufferRange{ 0u, 32u }, Core::BufferRange{ 48u, 16u }, Core::BufferRange{ 80u, 48u } };
    ExpectBufferRanges(ranges, expected);
}

TEST(GpuTaskUseIndex, CompleteCoverageStopsBeforeLaterInvalidRangeButSelectedInvalidPrefixStillFails){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto bufferResult = AddBufferMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskUseCovered), "Covered Buffer");
    ASSERT_TRUE(bufferResult);
    const auto buffer = *bufferResult;
    ASSERT_TRUE(buffer.valid());
    Array<Core::GpuTaskResourceUse, 3u> uses = { BufferUse(buffer, 0u, 64u), BufferUse(buffer, 0u, 0u), BufferUse(buffer, 16u, 16u), };
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
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
    Core::GpuTaskGraph graph(testArena.arena);
    const auto bufferResult = AddBufferMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskUseSymbolic), "Symbolic Buffer");
    ASSERT_TRUE(bufferResult);
    const auto buffer = *bufferResult;
    ASSERT_TRUE(buffer.valid());
    const Array<Core::GpuTaskResourceUse, 2u> uses = { BufferUse(buffer, 16u, 16u), BufferUse(buffer, 0u, Core::BufferRange::s_AllBytes) };
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    const auto task = TaskView(uses, declarations.generation());
    const auto resource = declarations.resourceAt(buffer.index);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TaskResourceUseIndex index(declarations.resourceCount(), declarations.generation(), LengthOf(uses), scratchArena);
    ASSERT_TRUE(index.build(task));
    Ranges ranges(scratchArena);
    ranges.reserve(2u);
    ASSERT_TRUE(CollectResourceFirstUseRangesWithinTask(declarations, task, index, 1u, resource, uses[1u].range, scratchArena, ranges));
    const Array<Core::BufferRange, 2u> expected = { Core::BufferRange{ 0u, 16u }, Core::BufferRange{ 32u, Core::BufferRange::s_AllBytes } };
    ExpectBufferRanges(ranges, expected);
}

TEST(GpuTaskUseIndex, TextureRectangleKeepsMipThenSliceRemainderOrder){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto textureResult = AddTextureMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskUseRectangle), "Texture Rectangle");
    ASSERT_TRUE(textureResult);
    const auto texture = *textureResult;
    ASSERT_TRUE(texture.valid());
    const Array<Core::GpuTaskResourceUse, 2u> uses = {
        Core::GpuTaskResourceUse{ .resource = texture, .range = { .textureSubresources = Core::TextureSubresourceSet(1u, 1u, 1u, 1u) } },
        Core::GpuTaskResourceUse{ .resource = texture, .range = { .textureSubresources = Core::TextureSubresourceSet(0u, 4u, 0u, 3u) } },
    };
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    const auto task = TaskView(uses, declarations.generation());
    const auto resource = declarations.resourceAt(texture.index);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    TaskResourceUseIndex index(declarations.resourceCount(), declarations.generation(), LengthOf(uses), scratchArena);
    ASSERT_TRUE(index.build(task));
    Ranges ranges(scratchArena);
    ranges.reserve(4u);
    ASSERT_TRUE(CollectResourceFirstUseRangesWithinTask(declarations, task, index, 1u, resource, uses[1u].range, scratchArena, ranges));
    const Array<Core::TextureSubresourceSet, 4u> expected = { Core::TextureSubresourceSet{ 0u, 1u, 0u, 3u }, Core::TextureSubresourceSet{ 2u, 2u, 0u, 3u }, Core::TextureSubresourceSet{ 1u, 1u, 0u, 1u }, Core::TextureSubresourceSet{ 1u, 1u, 2u, 1u }, };
    ASSERT_EQ(ranges.size(), LengthOf(expected));
    for(usize index = 0u; index < LengthOf(expected); ++index)
        EXPECT_EQ(ranges[index].textureSubresources, expected[index]);
}

TEST(GpuTaskUseIndex, CompiledResourceSetExpansionPreservesEveryInitialByteRange){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto aResult = AddBufferMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskUseExpandedA), "Expanded A", Core::ResourceStates::Common);
    ASSERT_TRUE(aResult);
    const auto a = *aResult;
    const auto bResult = AddBufferMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskUseExpandedB), "Expanded B", Core::ResourceStates::Common);
    ASSERT_TRUE(bResult);
    const auto b = *bResult;
    ASSERT_TRUE(a.valid());
    ASSERT_TRUE(b.valid());
    const Array<Core::GpuGraphResourceId, 2u> members = { a, b };
    const auto setResult = graph.importResourceSet(Core::GpuGraphResourceSetDesc{}.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskUseExpandedSet)).setMarkerLabel("Expanded Set").setMembers(members));
    ASSERT_TRUE(setResult);
    const auto set = *setResult;
    ASSERT_TRUE(set.valid());
    const Array<Core::GpuTaskResourceUse, 2u> direct = { BufferUse(a, 16u, 16u), BufferUse(b, 0u, 8u) };
    const Core::GpuTaskResourceSetUse expanded{
        .resourceSet = set,
        .range = { .bufferRange = Core::BufferRange(0u, Core::BufferRange::s_AllBytes) },
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskDesc desc;
    desc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskUseExpandedTask)).setMarkerLabel("Expanded Task").setResourceUses(direct).setResourceSetUses(Span(&expanded, 1u));
    const auto taskResult = graph.addTask(desc, Core::GpuTaskCommandRequirements{ Core::GpuQueueCapability::Graphics });
    ASSERT_TRUE(taskResult);
    const auto task = *taskResult;
    ASSERT_TRUE(task.valid());
    const Array<Core::GpuPhysicalQueueInfo, 1u> queues = { GraphicsQueue() };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues) };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView plan(compiledGraph);
    const auto view = plan.findTask(task);
    ASSERT_NE(view.plan, nullptr);
    ASSERT_EQ(view.plan->prologueBarrierCount, 5u);
    ASSERT_NE(view.prologueBarriers, nullptr);
    const Array<Core::GpuGraphResourceId, 5u> expectedResources = { a, b, a, a, b };
    const Array<Core::BufferRange, 5u> expectedRanges = {
        Core::BufferRange{ 16u, 16u },
        Core::BufferRange{ 0u, 8u },
        Core::BufferRange{ 0u, 16u },
        Core::BufferRange{ 32u, Core::BufferRange::s_AllBytes },
        Core::BufferRange{ 8u, Core::BufferRange::s_AllBytes },
    };
    for(usize index = 0u; index < LengthOf(expectedRanges); ++index){
        SCOPED_TRACE(index);
        const auto& barrier = view.prologueBarriers[index];
        EXPECT_EQ(barrier.resource, expectedResources[index]);
        EXPECT_EQ(barrier.range.bufferRange, expectedRanges[index]);
        EXPECT_EQ(barrier.before, Core::ResourceStates::Common);
        EXPECT_EQ(barrier.after, index < 2u ? Core::ResourceStates::CopyDest : Core::ResourceStates::ShaderResource);
        EXPECT_TRUE(barrier.isGraphInitialState);
    }
}

TEST(GpuTaskUseIndex, RepeatedAccelerationStructureUsesKeepOneInitialTransitionAndLastStateExport){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto accelerationResult = graph.importResource(Core::GpuGraphResourceDesc{}
        .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskUseAcceleration))
        .setMarkerLabel("Repeated Acceleration")
        .setType(Core::GpuGraphResourceType::AccelStruct)
        .setInitialState(Core::ResourceStates::Common)
        .setExternalFinalState(Core::ResourceStates::AccelStructRead)
    );
    ASSERT_TRUE(accelerationResult);
    const auto acceleration = *accelerationResult;
    const auto otherResult = AddBufferMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskUseAccelerationOther), "Unrelated Buffer", Core::ResourceStates::Common);
    ASSERT_TRUE(otherResult);
    const auto other = *otherResult;
    ASSERT_TRUE(acceleration.valid());
    ASSERT_TRUE(other.valid());
    const Array<Core::GpuTaskResourceUse, 4u> uses = {
        Core::GpuTaskResourceUse{ .resource = acceleration, .range = {}, .requiredState = Core::ResourceStates::AccelStructWrite, .access = Core::GpuTaskResourceAccess::Write },
        BufferUse(other, 0u, 16u),
        Core::GpuTaskResourceUse{ .resource = acceleration, .range = {}, .requiredState = Core::ResourceStates::AccelStructRead, .access = Core::GpuTaskResourceAccess::Read },
        Core::GpuTaskResourceUse{ .resource = acceleration, .range = {}, .requiredState = Core::ResourceStates::AccelStructWrite, .access = Core::GpuTaskResourceAccess::Write },
    };
    const auto taskResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskUseAccelerationTask), "Repeated Acceleration Task", {}, Span(uses), {}, {}, {}, graph);
    ASSERT_TRUE(taskResult);
    const auto task = *taskResult;
    ASSERT_TRUE(task.valid());
    const Array<Core::GpuPhysicalQueueInfo, 1u> queues = { GraphicsQueue() };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues) };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView plan(compiledGraph);
    const auto view = plan.findTask(task);
    ASSERT_NE(view.plan, nullptr);
    ASSERT_EQ(view.plan->prologueBarrierCount, 2u);
    ASSERT_NE(view.prologueBarriers, nullptr);
    EXPECT_EQ(view.prologueBarriers[0u].resource, acceleration);
    EXPECT_EQ(view.prologueBarriers[0u].before, Core::ResourceStates::Common);
    EXPECT_EQ(view.prologueBarriers[0u].after, Core::ResourceStates::AccelStructWrite);
    EXPECT_EQ(view.prologueBarriers[1u].resource, other);
    ASSERT_EQ(view.plan->epilogueBarrierCount, 1u);
    ASSERT_NE(view.epilogueBarriers, nullptr);
    EXPECT_EQ(view.epilogueBarriers[0u].resource, acceleration);
    EXPECT_EQ(view.epilogueBarriers[0u].before, Core::ResourceStates::AccelStructWrite);
    EXPECT_EQ(view.epilogueBarriers[0u].after, Core::ResourceStates::AccelStructRead);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

