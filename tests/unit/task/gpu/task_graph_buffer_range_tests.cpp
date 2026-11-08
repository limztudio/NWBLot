// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_buffer_range_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


[[nodiscard]] static Core::GpuTaskResourceUse BufferUse(
    const Core::GpuGraphResourceId resource,
    const u64 offset,
    const u64 size,
    const Core::ResourceStates::Mask state,
    const Core::GpuTaskResourceAccess::Enum access = Core::GpuTaskResourceAccess::Write
)noexcept{
    return Core::GpuTaskResourceUse{ .resource = resource, .range = Core::GpuTaskResourceRange{ .bufferRange = Core::BufferRange(offset, size) }, .requiredState = state, .access = access, };
}

[[nodiscard]] static Expected<Core::GpuTaskId> AddRangeTask(Core::GpuTaskGraph& graph, const Name& identity, const Span<const Core::GpuTaskResourceUse> uses, const b8 compute = false){
    Core::GpuTaskDesc desc;
    desc.setIdentity(identity).setMarkerLabel("Buffer Range Task").setResourceUses(uses);
    return graph.addTask(desc, Core::GpuTaskCommandRequirements{ compute ? Core::GpuQueueCapability::Compute : Core::GpuQueueCapability::Graphics });
}

[[nodiscard]] static const Core::GpuCompiledBarrier* FindRangeBarrier(const Span<const Core::GpuCompiledBarrier> barriers, const Core::BufferRange& range, const Core::GpuCompiledBarrierType::Enum type)noexcept{
    for(const Core::GpuCompiledBarrier& barrier : barriers){
        if(barrier.type == type && barrier.range.bufferRange == range)
            return &barrier;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphBufferRange, IndependentIntervalsKeepSeparateInitialStates){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto bufferResult = AddBufferMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseBufferRangeIndependent), "Independent Bytes");
    ASSERT_TRUE(bufferResult);
    const Core::GpuGraphResourceId buffer = *bufferResult;
    const Core::GpuTaskResourceUse firstUse = BufferUse(buffer, 0u, 32u, Core::ResourceStates::CopyDest);
    const Core::GpuTaskResourceUse tailUse = BufferUse(buffer, 32u, Core::BufferRange::s_AllBytes, Core::ResourceStates::UnorderedAccess);
    const auto firstResult = AddRangeTask(graph, Name(TaskGraphTestUtils::s_GpuCaseBufferRangeFirst), Span(&firstUse, 1u));
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    const auto tailResult = AddRangeTask(graph, Name(TaskGraphTestUtils::s_GpuCaseBufferRangeTail), Span(&tailUse, 1u));
    ASSERT_TRUE(tailResult);
    const Core::GpuTaskId tail = *tailResult;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(tail.valid());

    const Core::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    EXPECT_EQ(FindEdge(analysis, first, tail), nullptr);

    const Core::GpuCompiledGraph::ReadView plan(compiledGraph);
    const Core::GpuCompiledTaskView tailView = plan.findTask(tail);
    ASSERT_TRUE(tailView.valid());
    EXPECT_EQ(tailView.plan->prologueStateSeedCount, 0u);
    ASSERT_EQ(tailView.plan->prologueBarrierCount, 1u);
    EXPECT_EQ(tailView.prologueBarriers[0u].range.bufferRange, tailUse.range.bufferRange);
    EXPECT_EQ(tailView.prologueBarriers[0u].before, Core::ResourceStates::Common);
    EXPECT_TRUE(tailView.prologueBarriers[0u].isGraphInitialState);
}

TEST(GpuTaskGraphBufferRange, WideConsumerCollectsOlderPrefixAndTailAfterPartialOverwrite){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto bufferResult = AddBufferMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseBufferRangeFanIn), "Byte Fan In");
    ASSERT_TRUE(bufferResult);
    const Core::GpuGraphResourceId buffer = *bufferResult;
    const Core::GpuTaskResourceUse wholeUse = BufferUse(buffer, 0u, 128u, Core::ResourceStates::CopyDest);
    const Core::GpuTaskResourceUse middleUse = BufferUse(buffer, 32u, 32u, Core::ResourceStates::UnorderedAccess);
    const Core::GpuTaskResourceUse readUse = BufferUse(buffer, 0u, 128u, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read);
    const auto wholeResult = AddRangeTask(graph, Name(TaskGraphTestUtils::s_GpuCaseBufferRangeWhole), Span(&wholeUse, 1u));
    ASSERT_TRUE(wholeResult);
    const Core::GpuTaskId whole = *wholeResult;
    const auto middleResult = AddRangeTask(graph, Name(TaskGraphTestUtils::s_GpuCaseBufferRangeMiddle), Span(&middleUse, 1u));
    ASSERT_TRUE(middleResult);
    const Core::GpuTaskId middle = *middleResult;
    const auto readerResult = AddRangeTask(graph, Name(TaskGraphTestUtils::s_GpuCaseBufferRangeReader), Span(&readUse, 1u));
    ASSERT_TRUE(readerResult);
    const Core::GpuTaskId reader = *readerResult;
    ASSERT_TRUE(whole.valid());
    ASSERT_TRUE(middle.valid());
    ASSERT_TRUE(reader.valid());

    const Core::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    EXPECT_NE(FindEdge(analysis, whole, reader), nullptr);
    EXPECT_NE(FindEdge(analysis, middle, reader), nullptr);

    const Core::GpuCompiledGraph::ReadView plan(compiledGraph);
    const Core::GpuCompiledTaskView readerView = plan.findTask(reader);
    ASSERT_TRUE(readerView.valid());
    ASSERT_EQ(readerView.plan->prologueBarrierCount, 3u);
    ASSERT_EQ(readerView.plan->prologueStateSeedCount, 3u);
    const Array<Core::BufferRange, 3> ranges = { Core::BufferRange{ 0u, 32u }, Core::BufferRange{ 64u, 64u }, Core::BufferRange{ 32u, 32u } };
    for(usize index = 0u; index < LengthOf(ranges); ++index){
        const Core::GpuCompiledBarrier* const barrier = FindRangeBarrier(Span(readerView.prologueBarriers, readerView.plan->prologueBarrierCount), ranges[index], Core::GpuCompiledBarrierType::BufferTransition);
        ASSERT_NE(barrier, nullptr);
        EXPECT_EQ(barrier->before, index == 2u ? Core::ResourceStates::UnorderedAccess : Core::ResourceStates::CopyDest);
        EXPECT_EQ(barrier->after, Core::ResourceStates::ShaderResource);
        const Core::GpuSubmissionPacketId producerPacket = plan.findTask(index == 2u ? middle : whole).plan->packet;
        b8 foundSeed = false;
        for(u32 seedIndex = 0u; seedIndex < readerView.plan->prologueStateSeedCount; ++seedIndex){
            const Core::GpuPacketStateSeed& seed = readerView.prologueStateSeeds[seedIndex];
            foundSeed = foundSeed || (seed.range.bufferRange == ranges[index] && seed.sourcePacket == producerPacket);
        }
        EXPECT_TRUE(foundSeed);
    }
}

TEST(GpuTaskGraphBufferRange, SameTaskOverlappingDeclarationsSeedOnlyNewBytes){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto bufferResult = AddBufferMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseBufferRangeLocal), "Local Byte Ranges");
    ASSERT_TRUE(bufferResult);
    const Core::GpuGraphResourceId buffer = *bufferResult;
    const Array<Core::GpuTaskResourceUse, 2> uses = { BufferUse(buffer, 32u, 32u, Core::ResourceStates::CopyDest), BufferUse(buffer, 0u, 128u, Core::ResourceStates::UnorderedAccess), };
    const auto taskResult = AddRangeTask(graph, Name(TaskGraphTestUtils::s_GpuCaseBufferRangeLocalWriter), uses);
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    const Core::GpuTaskResourceUse readUse = BufferUse(buffer, 0u, 128u, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read);
    const auto readerResult = AddRangeTask(graph, Name(TaskGraphTestUtils::s_GpuCaseBufferRangeLocalReader), Span(&readUse, 1u));
    ASSERT_TRUE(readerResult);
    const Core::GpuTaskId reader = *readerResult;
    ASSERT_TRUE(task.valid());
    ASSERT_TRUE(reader.valid());

    const Core::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView plan(compiledGraph);
    const Core::GpuCompiledTaskView taskView = plan.findTask(task);
    ASSERT_TRUE(taskView.valid());
    EXPECT_EQ(taskView.plan->prologueStateSeedCount, 0u);
    ASSERT_EQ(taskView.plan->prologueBarrierCount, 3u);
    const Array<Core::BufferRange, 3> ranges = { Core::BufferRange{ 32u, 32u }, Core::BufferRange{ 0u, 32u }, Core::BufferRange{ 64u, 64u } };
    for(usize index = 0u; index < LengthOf(ranges); ++index){
        const Core::GpuCompiledBarrier* const barrier = FindRangeBarrier(Span(taskView.prologueBarriers, taskView.plan->prologueBarrierCount), ranges[index], Core::GpuCompiledBarrierType::BufferTransition);
        ASSERT_NE(barrier, nullptr);
        EXPECT_EQ(barrier->before, Core::ResourceStates::Common);
        EXPECT_EQ(barrier->after, index == 0u ? Core::ResourceStates::CopyDest : Core::ResourceStates::UnorderedAccess);
    }
    const Core::GpuCompiledTaskView readerView = plan.findTask(reader);
    ASSERT_TRUE(readerView.valid());
    ASSERT_EQ(readerView.plan->prologueBarrierCount, 1u);
    EXPECT_EQ(readerView.prologueBarriers[0u].range.bufferRange, readUse.range.bufferRange);
    EXPECT_EQ(readerView.prologueBarriers[0u].before, Core::ResourceStates::UnorderedAccess);
}

TEST(GpuTaskGraphBufferRange, ExportsEveryTerminalIntervalIncludingSymbolicTail){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Array<Core::GpuPhysicalQueueInfo, 3> queues = { GraphicsQueue(), DedicatedComputeQueue(), DedicatedTransferQueue() };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues) };
    const auto bufferResult = graph.importResource(
        Core::GpuGraphResourceDesc{}
            .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseBufferRangeExport))
            .setMarkerLabel("Byte Range Exports")
            .setType(Core::GpuGraphResourceType::Buffer)
            .setInitialState(Core::ResourceStates::Common)
            .setExternalFinalState(Core::ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(queues[2u].id)
    );
    ASSERT_TRUE(bufferResult);
    const Core::GpuGraphResourceId buffer = *bufferResult;
    const Core::GpuTaskResourceUse wholeUse = BufferUse(buffer, 0u, Core::BufferRange::s_AllBytes, Core::ResourceStates::CopyDest);
    const Core::GpuTaskResourceUse middleUse = BufferUse(buffer, 32u, 32u, Core::ResourceStates::UnorderedAccess);
    const auto wholeResult = AddRangeTask(graph, Name(TaskGraphTestUtils::s_GpuCaseBufferRangeExportWhole), Span(&wholeUse, 1u));
    ASSERT_TRUE(wholeResult);
    const Core::GpuTaskId whole = *wholeResult;
    const auto middleResult = AddRangeTask(graph, Name(TaskGraphTestUtils::s_GpuCaseBufferRangeExportMiddle), Span(&middleUse, 1u), true);
    ASSERT_TRUE(middleResult);
    const Core::GpuTaskId middle = *middleResult;
    ASSERT_TRUE(whole.valid());
    ASSERT_TRUE(middle.valid());
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView plan(compiledGraph);
    const Core::GpuCompiledExternalResourceExportView exportView = plan.externalResourceExport(buffer);
    ASSERT_TRUE(exportView.valid());
    ASSERT_EQ(exportView.plan->sourceCount, 3u);
    EXPECT_FALSE(exportView.plan->producerTask.valid());
    const Array<Core::BufferRange, 3> ranges = { Core::BufferRange{ 0u, 32u }, Core::BufferRange{ 64u, Core::BufferRange::s_AllBytes }, Core::BufferRange{ 32u, 32u } };
    for(usize index = 0u; index < LengthOf(ranges); ++index){
        const Core::GpuTaskId producer = index == 2u ? middle : whole;
        const Core::GpuCompiledTaskView taskView = plan.findTask(producer);
        ASSERT_TRUE(taskView.valid());
        const Core::GpuCompiledBarrier* const stateExport = FindRangeBarrier(Span(taskView.epilogueBarriers, taskView.plan->epilogueBarrierCount), ranges[index], Core::GpuCompiledBarrierType::BufferStateExport);
        ASSERT_NE(stateExport, nullptr);
        EXPECT_EQ(stateExport->after, Core::ResourceStates::ShaderResource);
        EXPECT_NE(FindRangeBarrier(Span(taskView.epilogueBarriers, taskView.plan->epilogueBarrierCount), ranges[index], Core::GpuCompiledBarrierType::BufferOwnershipRelease), nullptr);
        b8 foundSource = false;
        for(u32 sourceIndex = 0u; sourceIndex < exportView.plan->sourceCount; ++sourceIndex){
            const Core::GpuCompiledExternalResourceExportSource& source = exportView.sources[sourceIndex];
            foundSource = foundSource || (source.producerTask == producer && source.range.bufferRange == ranges[index]);
        }
        EXPECT_TRUE(foundSource);
    }
}

TEST(GpuTaskGraphBufferRange, CrossQueueFanInTransfersOnlyIntersectingBytes){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Array<Core::GpuPhysicalQueueInfo, 2> queues = { GraphicsQueue(), DedicatedComputeQueue() };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues) };
    const auto bufferResult = AddBufferMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseBufferRangeCrossQueue), "Cross Queue Bytes");
    ASSERT_TRUE(bufferResult);
    const Core::GpuGraphResourceId buffer = *bufferResult;
    const Core::GpuTaskResourceUse graphicsUse = BufferUse(buffer, 0u, 64u, Core::ResourceStates::CopyDest);
    const Core::GpuTaskResourceUse computeUse = BufferUse(buffer, 64u, 64u, Core::ResourceStates::UnorderedAccess);
    const Core::GpuTaskResourceUse readUse = BufferUse(buffer, 32u, 64u, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read);
    const auto graphicsResult = AddRangeTask(graph, Name(TaskGraphTestUtils::s_GpuCaseBufferRangeGraphics), Span(&graphicsUse, 1u));
    ASSERT_TRUE(graphicsResult);
    const Core::GpuTaskId graphics = *graphicsResult;
    const auto computeResult = AddRangeTask(graph, Name(TaskGraphTestUtils::s_GpuCaseBufferRangeCompute), Span(&computeUse, 1u), true);
    ASSERT_TRUE(computeResult);
    const Core::GpuTaskId compute = *computeResult;
    const auto readerResult = AddRangeTask(graph, Name(TaskGraphTestUtils::s_GpuCaseBufferRangeJoin), Span(&readUse, 1u));
    ASSERT_TRUE(readerResult);
    const Core::GpuTaskId reader = *readerResult;
    ASSERT_TRUE(graphics.valid());
    ASSERT_TRUE(compute.valid());
    ASSERT_TRUE(reader.valid());
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    EXPECT_EQ(FindEdge(analysis, graphics, compute), nullptr);
    const Core::GpuCompiledGraph::ReadView plan(compiledGraph);
    const Core::GpuCompiledTaskView computeView = plan.findTask(compute);
    const Core::GpuCompiledTaskView readerView = plan.findTask(reader);
    ASSERT_TRUE(computeView.valid());
    ASSERT_TRUE(readerView.valid());
    EXPECT_EQ(computeView.plan->queue, queues[1u].id);
    EXPECT_EQ(readerView.plan->prologueStateSeedCount, 2u);
    ASSERT_EQ(plan.logicalOwnershipTransferCount(), 1u);
    const Core::GpuCompiledOwnershipTransfer* const transfer = plan.logicalOwnershipTransferAt(0u);
    ASSERT_NE(transfer, nullptr);
    EXPECT_EQ(transfer->range.bufferRange, Core::BufferRange(64u, 32u));
    EXPECT_EQ(transfer->sourceTask, compute);
    EXPECT_EQ(transfer->destinationTask, reader);
    EXPECT_NE(FindRangeBarrier(Span(computeView.epilogueBarriers, computeView.plan->epilogueBarrierCount), Core::BufferRange(64u, 32u), Core::GpuCompiledBarrierType::BufferOwnershipRelease), nullptr);
    EXPECT_NE(FindRangeBarrier(Span(readerView.prologueBarriers, readerView.plan->prologueBarrierCount), Core::BufferRange(64u, 32u), Core::GpuCompiledBarrierType::BufferOwnershipAcquire), nullptr);
}


TEST(GpuTaskGraphBufferRange, GroupsLateOwnershipAndTerminalExportsInTaskAndDiscoveryOrder){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Array<Core::GpuPhysicalQueueInfo, 3> queues = { GraphicsQueue(), DedicatedComputeQueue(), DedicatedTransferQueue() };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues) };
    const Array<Name, 2> identities = { Name(TaskGraphTestUtils::s_GpuCaseBufferRangeGroupedA), Name(TaskGraphTestUtils::s_GpuCaseBufferRangeGroupedB) };
    const Array<Core::BufferRange, 2> ranges = { Core::BufferRange{ 16u, 32u }, Core::BufferRange{ 64u, 16u } };
    Array<Core::GpuGraphResourceId, 2u> resources{};
    Array<Core::GpuTaskId, 2u> writers{};
    Array<Core::GpuTaskId, 2u> consumers{};
    for(usize index = 0u; index < resources.size(); ++index){
        const auto gpuTaskAdmission = graph.importResource(
            Core::GpuGraphResourceDesc{}
                .setIdentity(identities[index])
                .setMarkerLabel("Grouped Epilogue Buffer")
                .setType(Core::GpuGraphResourceType::Buffer)
                .setInitialState(Core::ResourceStates::Common)
                .setExternalFinalState(Core::ResourceStates::ShaderResource)
                .setExternalFinalReleaseDestinationQueue(queues[2u].id)
        );
        ASSERT_TRUE(gpuTaskAdmission);
        resources[index] = *gpuTaskAdmission;
        ASSERT_TRUE(resources[index].valid());
    }
    for(usize offset = 0u; offset < resources.size(); ++offset){
        const usize index = resources.size() - offset - 1u;
        const Core::GpuTaskResourceUse use = BufferUse(resources[index], ranges[index].byteOffset, ranges[index].byteSize, Core::ResourceStates::CopyDest);
        const auto derivedNameResult = DeriveName(identities[index], AStringView("writer"));
        ASSERT_TRUE(derivedNameResult);
        const auto writerTaskAdmission = AddRangeTask(graph, *derivedNameResult, Span(&use, 1u));
        ASSERT_TRUE(writerTaskAdmission);
        writers[index] = *writerTaskAdmission;
        ASSERT_TRUE(writers[index].valid());
    }
    for(usize index = 0u; index < resources.size(); ++index){
        const Core::GpuTaskResourceUse use = BufferUse(resources[index], ranges[index].byteOffset, ranges[index].byteSize, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read);
        const auto secondDerivedNameResult = DeriveName(identities[index], AStringView("consumer"));
        ASSERT_TRUE(secondDerivedNameResult);
        const auto consumerTaskAdmission = AddRangeTask(graph, *secondDerivedNameResult, Span(&use, 1u), true);
        ASSERT_TRUE(consumerTaskAdmission);
        consumers[index] = *consumerTaskAdmission;
        ASSERT_TRUE(consumers[index].valid());
    }
    const auto emptyResult = graph.addTask(Core::GpuTaskDesc{}.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseBufferRangeGroupedEmpty)).setMarkerLabel("No Epilogue Task"));
    ASSERT_TRUE(emptyResult);
    const Core::GpuTaskId empty = *emptyResult;
    ASSERT_TRUE(empty.valid());
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView plan(compiledGraph);
    for(usize index = 0u; index < resources.size(); ++index){
        SCOPED_TRACE(index);
        const Core::GpuCompiledTaskView writer = plan.findTask(writers[index]);
        ASSERT_TRUE(writer.valid());
        ASSERT_EQ(writer.plan->epilogueBarrierCount, 1u);
        EXPECT_EQ(writer.plan->epilogueBarrierOffset, resources.size() - index - 1u);
        EXPECT_EQ(writer.epilogueBarriers[0u].resource, resources[index]);
        EXPECT_EQ(writer.epilogueBarriers[0u].range.bufferRange, ranges[index]);
        EXPECT_EQ(writer.epilogueBarriers[0u].type, Core::GpuCompiledBarrierType::BufferOwnershipRelease);
        EXPECT_EQ(writer.epilogueBarriers[0u].sourceQueue, queues[0u].id);
        EXPECT_EQ(writer.epilogueBarriers[0u].destinationQueue, queues[1u].id);
        const Core::GpuCompiledTaskView consumer = plan.findTask(consumers[index]);
        ASSERT_TRUE(consumer.valid());
        ASSERT_EQ(consumer.plan->epilogueBarrierCount, 2u);
        EXPECT_EQ(consumer.plan->epilogueBarrierOffset, 2u + index * 2u);
        EXPECT_EQ(consumer.epilogueBarriers[0u].type, Core::GpuCompiledBarrierType::BufferStateExport);
        EXPECT_EQ(consumer.epilogueBarriers[1u].type, Core::GpuCompiledBarrierType::BufferOwnershipRelease);
        for(usize barrierIndex = 0u; barrierIndex < 2u; ++barrierIndex){
            const Core::GpuCompiledBarrier& barrier = consumer.epilogueBarriers[barrierIndex];
            EXPECT_EQ(barrier.resource, resources[index]);
            EXPECT_EQ(barrier.range.bufferRange, ranges[index]);
            EXPECT_EQ(barrier.before, Core::ResourceStates::ShaderResource);
            EXPECT_EQ(barrier.after, Core::ResourceStates::ShaderResource);
            EXPECT_EQ(barrier.sourceQueue, queues[1u].id);
            EXPECT_EQ(barrier.destinationQueue, queues[barrierIndex == 0u ? 1u : 2u].id);
        }
    }
    const Core::GpuCompiledTaskView emptyView = plan.findTask(empty);
    ASSERT_TRUE(emptyView.valid());
    EXPECT_EQ(emptyView.plan->epilogueBarrierOffset, 6u);
    EXPECT_EQ(emptyView.plan->epilogueBarrierCount, 0u);
    EXPECT_EQ(emptyView.epilogueBarriers, nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

