// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;
constexpr u32 s_ThirdElementIndex = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_buffer_range_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


[[nodiscard]] static Graphics::GpuTaskResourceUse BufferUse(
    const Graphics::GpuGraphResourceId resource,
    const u64 offset,
    const u64 size,
    const Graphics::ResourceStates::Mask state,
    const Graphics::GpuTaskResourceAccess::Enum access = Graphics::GpuTaskResourceAccess::Write
){
    return Graphics::GpuTaskResourceUse{
        .resource = resource,
        .range = Graphics::GpuTaskResourceRange{ .bufferRange = Graphics::BufferRange(offset, size) },
        .requiredState = state,
        .access = access,
    };
}

[[nodiscard]] static Graphics::GpuTaskId AddRangeTask(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const Graphics::GpuTaskResourceUse* const uses,
    const usize useCount,
    const bool compute = false
){
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel("Buffer Range Task")
        .setResourceUses(uses, useCount)
    ;
    return graph.addTask(desc, Graphics::GpuTaskCommandRequirements{ compute ? Graphics::GpuQueueCapability::Compute : Graphics::GpuQueueCapability::Graphics });
}

[[nodiscard]] static const Graphics::GpuCompiledBarrier* FindRangeBarrier(
    const Graphics::GpuCompiledBarrier* const barriers,
    const u32 count,
    const Graphics::BufferRange& range,
    const Graphics::GpuCompiledBarrierType::Enum type
){
    for(u32 index = 0u; index < count; ++index){
        if(barriers[index].type == type && barriers[index].range.bufferRange == range)
            return &barriers[index];
    }
    return nullptr;
}


static void BenchmarkPendingEpilogueGrouping(const usize taskCount){
    constexpr usize s_Repetitions = 8u;
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Core::Alloc::ScratchArena inputScratchArena(Name("tests/buffer_range/epilogue_inputs"));
    Vector<Graphics::GpuGraphResourceId, Core::Alloc::ScratchArena> resources(inputScratchArena);
    Vector<Graphics::GpuTaskId, Core::Alloc::ScratchArena> tasks(inputScratchArena);
    resources.reserve(taskCount);
    tasks.reserve(taskCount);
    for(usize index = 0u; index < taskCount; ++index){
        char text[32u];
        const Graphics::GpuGraphResourceId resource = graph.importResource(
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(DeriveName(Name("tests/buffer_range/epilogue_buffer"), FormatDecimal(index, text)))
                .setMarkerLabel("Epilogue Grouping Buffer")
                .setType(Graphics::GpuGraphResourceType::Buffer)
                .setInitialState(Graphics::ResourceStates::Common)
                .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
        );
        ASSERT_TRUE(resource.valid());
        resources.push_back(resource);
    }
    for(usize index = 0u; index < taskCount; ++index){
        char text[32u];
        const usize resourceIndex = taskCount - index - 1u;
        const Graphics::GpuTaskResourceUse use = BufferUse(
            resources[resourceIndex], resourceIndex * 16u, 16u, Graphics::ResourceStates::CopyDest
        );
        const Graphics::GpuTaskId task = AddRangeTask(
            graph, DeriveName(Name("tests/buffer_range/epilogue_writer"), FormatDecimal(index, text)), &use, 1u
        );
        ASSERT_TRUE(task.valid());
        tasks.push_back(task);
    }
    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    const Graphics::GpuTaskGraphCompiler compiler;
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    Graphics::GpuTaskGraphCompileOptions options;
    options.allowMetadataOnlyTasks = true;
    ASSERT_TRUE(compiler.compile(declarations, analysis, topology, assignments, compiledGraph, scratchArena, options));
    f64 resourceStatePlanningSeconds = 0.0;
    const Timer begin = TimerNow();
    for(usize repetition = 0u; repetition < s_Repetitions; ++repetition){
        ASSERT_TRUE(compiler.compile(declarations, analysis, topology, assignments, compiledGraph, scratchArena, options));
        const Graphics::GpuCompiledGraph::ReadView plan(compiledGraph);
        resourceStatePlanningSeconds += plan.compileStatistics().resourceStatePlanningSeconds;
    }
    const u64 elapsedNanoseconds = DurationInNS<u64>(TimerNow(), begin);
    const Graphics::GpuCompiledGraph::ReadView plan(compiledGraph);
    for(usize index = 0u; index < tasks.size(); ++index){
        const Graphics::GpuCompiledTaskView view = plan.findTask(tasks[index]);
        ASSERT_TRUE(view.valid());
        ASSERT_EQ(view.plan->epilogueBarrierCount, 1u);
        EXPECT_EQ(view.plan->epilogueBarrierOffset, index);
        EXPECT_EQ(view.epilogueBarriers[0u].resource, resources[taskCount - index - 1u]);
        EXPECT_EQ(view.epilogueBarriers[0u].range.bufferRange, Graphics::BufferRange((taskCount - index - 1u) * 16u, 16u));
        EXPECT_EQ(view.epilogueBarriers[0u].type, Graphics::GpuCompiledBarrierType::BufferStateExport);
    }
    char text[32u];
    const AStringView elapsed = FormatDecimal(elapsedNanoseconds, text);
    text[elapsed.size()] = '\0';
    testing::Test::RecordProperty("elapsed_ns", text);
    const u64 planningNanoseconds = static_cast<u64>(resourceStatePlanningSeconds * 1'000'000'000.0);
    const AStringView planningElapsed = FormatDecimal(planningNanoseconds, text);
    text[planningElapsed.size()] = '\0';
    testing::Test::RecordProperty("resource_state_planning_ns", text);
    testing::Test::RecordProperty("repetitions", static_cast<i32>(s_Repetitions));
    testing::Test::RecordProperty("task_count", static_cast<i32>(taskCount));
    testing::Test::RecordProperty("pending_epilogue_count", static_cast<i32>(taskCount));
    testing::Test::RecordProperty("scratch_peak_bytes", static_cast<i32>(scratchArena.memoryStats().peakUsedBytes));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphBufferRange, IndependentIntervalsKeepSeparateInitialStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId buffer = AddBufferMetadata(
        graph,
        Name("tests/buffer_range/independent"),
        "Independent Bytes"
    );
    const Graphics::GpuTaskResourceUse firstUse = BufferUse(buffer, 0u, 32u, Graphics::ResourceStates::CopyDest);
    const Graphics::GpuTaskResourceUse tailUse = BufferUse(
        buffer,
        32u,
        Graphics::BufferRange::s_AllBytes,
        Graphics::ResourceStates::UnorderedAccess
    );
    const Graphics::GpuTaskId first = AddRangeTask(graph, Name("tests/buffer_range/first"), &firstUse, 1u);
    const Graphics::GpuTaskId tail = AddRangeTask(graph, Name("tests/buffer_range/tail"), &tailUse, 1u);
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(tail.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(graph, analysis, topology, assignments, compiledGraph));
    EXPECT_EQ(FindEdge(analysis, first, tail), nullptr);

    const Graphics::GpuCompiledGraph::ReadView plan(compiledGraph);
    const Graphics::GpuCompiledTaskView tailView = plan.findTask(tail);
    ASSERT_TRUE(tailView.valid());
    EXPECT_EQ(tailView.plan->prologueStateSeedCount, 0u);
    ASSERT_EQ(tailView.plan->prologueBarrierCount, 1u);
    EXPECT_EQ(tailView.prologueBarriers[0u].range.bufferRange, tailUse.range.bufferRange);
    EXPECT_EQ(tailView.prologueBarriers[0u].before, Graphics::ResourceStates::Common);
    EXPECT_TRUE(tailView.prologueBarriers[0u].isGraphInitialState);
}

TEST(GpuTaskGraphBufferRange, WideConsumerCollectsOlderPrefixAndTailAfterPartialOverwrite){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId buffer = AddBufferMetadata(graph, Name("tests/buffer_range/fan_in"), "Byte Fan In");
    const Graphics::GpuTaskResourceUse wholeUse = BufferUse(buffer, 0u, 128u, Graphics::ResourceStates::CopyDest);
    const Graphics::GpuTaskResourceUse middleUse = BufferUse(buffer, 32u, 32u, Graphics::ResourceStates::UnorderedAccess);
    const Graphics::GpuTaskResourceUse readUse = BufferUse(
        buffer,
        0u,
        128u,
        Graphics::ResourceStates::ShaderResource,
        Graphics::GpuTaskResourceAccess::Read
    );
    const Graphics::GpuTaskId whole = AddRangeTask(graph, Name("tests/buffer_range/whole"), &wholeUse, 1u);
    const Graphics::GpuTaskId middle = AddRangeTask(graph, Name("tests/buffer_range/middle"), &middleUse, 1u);
    const Graphics::GpuTaskId reader = AddRangeTask(graph, Name("tests/buffer_range/reader"), &readUse, 1u);
    ASSERT_TRUE(whole.valid());
    ASSERT_TRUE(middle.valid());
    ASSERT_TRUE(reader.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(graph, analysis, topology, assignments, compiledGraph));
    EXPECT_NE(FindEdge(analysis, whole, reader), nullptr);
    EXPECT_NE(FindEdge(analysis, middle, reader), nullptr);

    const Graphics::GpuCompiledGraph::ReadView plan(compiledGraph);
    const Graphics::GpuCompiledTaskView readerView = plan.findTask(reader);
    ASSERT_TRUE(readerView.valid());
    ASSERT_EQ(readerView.plan->prologueBarrierCount, 3u);
    ASSERT_EQ(readerView.plan->prologueStateSeedCount, 3u);
    const Graphics::BufferRange ranges[] = { { 0u, 32u }, { 64u, 64u }, { 32u, 32u } };
    for(usize index = 0u; index < LengthOf(ranges); ++index){
        const Graphics::GpuCompiledBarrier* const barrier = FindRangeBarrier(
            readerView.prologueBarriers,
            readerView.plan->prologueBarrierCount,
            ranges[index],
            Graphics::GpuCompiledBarrierType::BufferTransition
        );
        ASSERT_NE(barrier, nullptr);
        EXPECT_EQ(
            barrier->before,
            index == s_ExpectedDualCount ? Graphics::ResourceStates::UnorderedAccess : Graphics::ResourceStates::CopyDest
        );
        EXPECT_EQ(barrier->after, Graphics::ResourceStates::ShaderResource);
        const Graphics::GpuSubmissionPacketId producerPacket = plan.findTask(index == s_ExpectedDualCount ? middle : whole).plan->packet;
        bool foundSeed = false;
        for(u32 seedIndex = 0u; seedIndex < readerView.plan->prologueStateSeedCount; ++seedIndex){
            const Graphics::GpuPacketStateSeed& seed = readerView.prologueStateSeeds[seedIndex];
            foundSeed = foundSeed || (seed.range.bufferRange == ranges[index] && seed.sourcePacket == producerPacket);
        }
        EXPECT_TRUE(foundSeed);
    }
}

TEST(GpuTaskGraphBufferRange, SameTaskOverlappingDeclarationsSeedOnlyNewBytes){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId buffer = AddBufferMetadata(graph, Name("tests/buffer_range/local"), "Local Byte Ranges");
    const Graphics::GpuTaskResourceUse uses[] = {
        BufferUse(buffer, 32u, 32u, Graphics::ResourceStates::CopyDest),
        BufferUse(buffer, 0u, 128u, Graphics::ResourceStates::UnorderedAccess),
    };
    const Graphics::GpuTaskId task = AddRangeTask(graph, Name("tests/buffer_range/local_writer"), uses, LengthOf(uses));
    const Graphics::GpuTaskResourceUse readUse = BufferUse(
        buffer,
        0u,
        128u,
        Graphics::ResourceStates::ShaderResource,
        Graphics::GpuTaskResourceAccess::Read
    );
    const Graphics::GpuTaskId reader = AddRangeTask(graph, Name("tests/buffer_range/local_reader"), &readUse, 1u);
    ASSERT_TRUE(task.valid());
    ASSERT_TRUE(reader.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView plan(compiledGraph);
    const Graphics::GpuCompiledTaskView taskView = plan.findTask(task);
    ASSERT_TRUE(taskView.valid());
    EXPECT_EQ(taskView.plan->prologueStateSeedCount, 0u);
    ASSERT_EQ(taskView.plan->prologueBarrierCount, 3u);
    const Graphics::BufferRange ranges[] = { { 32u, 32u }, { 0u, 32u }, { 64u, 64u } };
    for(usize index = 0u; index < LengthOf(ranges); ++index){
        const Graphics::GpuCompiledBarrier* const barrier = FindRangeBarrier(
            taskView.prologueBarriers,
            taskView.plan->prologueBarrierCount,
            ranges[index],
            Graphics::GpuCompiledBarrierType::BufferTransition
        );
        ASSERT_NE(barrier, nullptr);
        EXPECT_EQ(barrier->before, Graphics::ResourceStates::Common);
        EXPECT_EQ(barrier->after, index == 0u ? Graphics::ResourceStates::CopyDest : Graphics::ResourceStates::UnorderedAccess);
    }
    const Graphics::GpuCompiledTaskView readerView = plan.findTask(reader);
    ASSERT_TRUE(readerView.valid());
    ASSERT_EQ(readerView.plan->prologueBarrierCount, 1u);
    EXPECT_EQ(readerView.prologueBarriers[0u].range.bufferRange, readUse.range.bufferRange);
    EXPECT_EQ(readerView.prologueBarriers[0u].before, Graphics::ResourceStates::UnorderedAccess);
}

TEST(GpuTaskGraphBufferRange, ExportsEveryTerminalIntervalIncludingSymbolicTail){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue(), DedicatedTransferQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    const Graphics::GpuGraphResourceId buffer = graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/buffer_range/export"))
            .setMarkerLabel("Byte Range Exports")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::Common)
            .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(queues[s_ThirdElementIndex].id)
    );
    const Graphics::GpuTaskResourceUse wholeUse = BufferUse(
        buffer,
        0u,
        Graphics::BufferRange::s_AllBytes,
        Graphics::ResourceStates::CopyDest
    );
    const Graphics::GpuTaskResourceUse middleUse = BufferUse(buffer, 32u, 32u, Graphics::ResourceStates::UnorderedAccess);
    const Graphics::GpuTaskId whole = AddRangeTask(graph, Name("tests/buffer_range/export_whole"), &wholeUse, 1u);
    const Graphics::GpuTaskId middle = AddRangeTask(graph, Name("tests/buffer_range/export_middle"), &middleUse, 1u, true);
    ASSERT_TRUE(whole.valid());
    ASSERT_TRUE(middle.valid());
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView plan(compiledGraph);
    const Graphics::GpuCompiledExternalResourceExportView exportView = plan.externalResourceExport(buffer);
    ASSERT_TRUE(exportView.valid());
    ASSERT_EQ(exportView.plan->sourceCount, 3u);
    EXPECT_FALSE(exportView.plan->producerTask.valid());
    const Graphics::BufferRange ranges[] = { { 0u, 32u }, { 64u, Graphics::BufferRange::s_AllBytes }, { 32u, 32u } };
    for(usize index = 0u; index < LengthOf(ranges); ++index){
        const Graphics::GpuTaskId producer = index == s_ExpectedDualCount ? middle : whole;
        const Graphics::GpuCompiledTaskView taskView = plan.findTask(producer);
        ASSERT_TRUE(taskView.valid());
        const Graphics::GpuCompiledBarrier* const stateExport = FindRangeBarrier(
            taskView.epilogueBarriers,
            taskView.plan->epilogueBarrierCount,
            ranges[index],
            Graphics::GpuCompiledBarrierType::BufferStateExport
        );
        ASSERT_NE(stateExport, nullptr);
        EXPECT_EQ(stateExport->after, Graphics::ResourceStates::ShaderResource);
        EXPECT_NE(FindRangeBarrier(
            taskView.epilogueBarriers,
            taskView.plan->epilogueBarrierCount,
            ranges[index],
            Graphics::GpuCompiledBarrierType::BufferOwnershipRelease
        ), nullptr);
        bool foundSource = false;
        for(u32 sourceIndex = 0u; sourceIndex < exportView.plan->sourceCount; ++sourceIndex){
            const Graphics::GpuCompiledExternalResourceExportSource& source = exportView.sources[sourceIndex];
            foundSource = foundSource || (source.producerTask == producer && source.range.bufferRange == ranges[index]);
        }
        EXPECT_TRUE(foundSource);
    }
}

TEST(GpuTaskGraphBufferRange, CrossQueueFanInTransfersOnlyIntersectingBytes){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    const Graphics::GpuGraphResourceId buffer = AddBufferMetadata(
        graph,
        Name("tests/buffer_range/cross_queue"),
        "Cross Queue Bytes"
    );
    const Graphics::GpuTaskResourceUse graphicsUse = BufferUse(buffer, 0u, 64u, Graphics::ResourceStates::CopyDest);
    const Graphics::GpuTaskResourceUse computeUse = BufferUse(buffer, 64u, 64u, Graphics::ResourceStates::UnorderedAccess);
    const Graphics::GpuTaskResourceUse readUse = BufferUse(
        buffer,
        32u,
        64u,
        Graphics::ResourceStates::ShaderResource,
        Graphics::GpuTaskResourceAccess::Read
    );
    const Graphics::GpuTaskId graphics = AddRangeTask(graph, Name("tests/buffer_range/graphics"), &graphicsUse, 1u);
    const Graphics::GpuTaskId compute = AddRangeTask(graph, Name("tests/buffer_range/compute"), &computeUse, 1u, true);
    const Graphics::GpuTaskId reader = AddRangeTask(graph, Name("tests/buffer_range/join"), &readUse, 1u);
    ASSERT_TRUE(graphics.valid());
    ASSERT_TRUE(compute.valid());
    ASSERT_TRUE(reader.valid());
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(graph, analysis, topology, assignments, compiledGraph));
    EXPECT_EQ(FindEdge(analysis, graphics, compute), nullptr);
    const Graphics::GpuCompiledGraph::ReadView plan(compiledGraph);
    const Graphics::GpuCompiledTaskView computeView = plan.findTask(compute);
    const Graphics::GpuCompiledTaskView readerView = plan.findTask(reader);
    ASSERT_TRUE(computeView.valid());
    ASSERT_TRUE(readerView.valid());
    EXPECT_EQ(computeView.plan->queue, queues[1u].id);
    EXPECT_EQ(readerView.plan->prologueStateSeedCount, s_ExpectedDualCount);
    ASSERT_EQ(plan.logicalOwnershipTransferCount(), 1u);
    const Graphics::GpuCompiledOwnershipTransfer* const transfer = plan.logicalOwnershipTransferAt(0u);
    ASSERT_NE(transfer, nullptr);
    EXPECT_EQ(transfer->range.bufferRange, Graphics::BufferRange(64u, 32u));
    EXPECT_EQ(transfer->sourceTask, compute);
    EXPECT_EQ(transfer->destinationTask, reader);
    EXPECT_NE(FindRangeBarrier(
        computeView.epilogueBarriers,
        computeView.plan->epilogueBarrierCount,
        Graphics::BufferRange(64u, 32u),
        Graphics::GpuCompiledBarrierType::BufferOwnershipRelease
    ), nullptr);
    EXPECT_NE(FindRangeBarrier(
        readerView.prologueBarriers,
        readerView.plan->prologueBarrierCount,
        Graphics::BufferRange(64u, 32u),
        Graphics::GpuCompiledBarrierType::BufferOwnershipAcquire
    ), nullptr);
}

TEST(GpuTaskGraphBufferRange, TypedRangesRejectOutOfBoundsAndResolveRemainingBytes){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    Graphics::Buffer* const object = NewMetadataOnlyBuffer(
        testArena.arena,
        context,
        allocator,
        Graphics::BufferDesc().setByteSize(64u).setInitialState(Graphics::ResourceStates::Common)
    );
    ASSERT_NE(object, nullptr);
    const Graphics::BufferHandle buffer(object, Graphics::BufferHandle::deleter_type(&testArena.arena), AdoptRef);
    const Graphics::BufferRange ranges[] = {
        { 0u, 0u },
        { 64u, Graphics::BufferRange::s_AllBytes },
        { 48u, 32u },
        { Limit<u64>::s_Max - 3u, 4u },
        { 32u, Graphics::BufferRange::s_AllBytes },
    };
    for(usize index = 0u; index < LengthOf(ranges); ++index){
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = graph.importBuffer(
            buffer,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/buffer_range/typed"))
                .setMarkerLabel("Typed Byte Range")
                .setType(Graphics::GpuGraphResourceType::Buffer)
                .setInitialState(Graphics::ResourceStates::Common)
        );
        ASSERT_TRUE(resource.valid());
        const Graphics::GpuTaskResourceUse use = BufferUse(
            resource,
            ranges[index].byteOffset,
            ranges[index].byteSize,
            Graphics::ResourceStates::CopyDest
        );
        const Graphics::GpuTaskId task = AddRangeTask(graph, Name("tests/buffer_range/typed_writer"), &use, 1u);
        ASSERT_TRUE(task.valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        if(index + 1u != LengthOf(ranges)){
            EXPECT_FALSE(Analyze(graph, analysis));
            EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceUse);
            continue;
        }
        const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
        const Graphics::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u };
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(CompileWithSeparatedCommandQueues(graph, analysis, topology, assignments, compiledGraph));
        const Graphics::GpuCompiledGraph::ReadView plan(compiledGraph);
        const Graphics::GpuCompiledTaskView taskView = plan.findTask(task);
        ASSERT_TRUE(taskView.valid());
        ASSERT_EQ(taskView.plan->prologueBarrierCount, 1u);
        EXPECT_EQ(taskView.prologueBarriers[0u].range.bufferRange, Graphics::BufferRange(32u, 32u));
    }
}


TEST(GpuTaskGraphBufferRange, GroupsLateOwnershipAndTerminalExportsInTaskAndDiscoveryOrder){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue(), DedicatedTransferQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    const Name identities[] = { Name("tests/buffer_range/grouped_a"), Name("tests/buffer_range/grouped_b") };
    const Graphics::BufferRange ranges[] = { { 16u, 32u }, { 64u, 16u } };
    Array<Graphics::GpuGraphResourceId, s_ExpectedDualCount> resources{};
    Array<Graphics::GpuTaskId, s_ExpectedDualCount> writers{};
    Array<Graphics::GpuTaskId, s_ExpectedDualCount> consumers{};
    for(usize index = 0u; index < resources.size(); ++index){
        resources[index] = graph.importResource(
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(identities[index])
                .setMarkerLabel("Grouped Epilogue Buffer")
                .setType(Graphics::GpuGraphResourceType::Buffer)
                .setInitialState(Graphics::ResourceStates::Common)
                .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
                .setExternalFinalReleaseDestinationQueue(queues[s_ThirdElementIndex].id)
        );
        ASSERT_TRUE(resources[index].valid());
    }
    for(usize offset = 0u; offset < resources.size(); ++offset){
        const usize index = resources.size() - offset - 1u;
        const Graphics::GpuTaskResourceUse use = BufferUse(
            resources[index], ranges[index].byteOffset, ranges[index].byteSize, Graphics::ResourceStates::CopyDest
        );
        writers[index] = AddRangeTask(graph, DeriveName(identities[index], AStringView("writer")), &use, 1u);
        ASSERT_TRUE(writers[index].valid());
    }
    for(usize index = 0u; index < resources.size(); ++index){
        const Graphics::GpuTaskResourceUse use = BufferUse(
            resources[index], ranges[index].byteOffset, ranges[index].byteSize,
            Graphics::ResourceStates::ShaderResource, Graphics::GpuTaskResourceAccess::Read
        );
        consumers[index] = AddRangeTask(graph, DeriveName(identities[index], AStringView("consumer")), &use, 1u, true);
        ASSERT_TRUE(consumers[index].valid());
    }
    const Graphics::GpuTaskId empty = graph.addTask(Graphics::GpuTaskDesc{}
        .setIdentity(Name("tests/buffer_range/grouped_empty"))
        .setMarkerLabel("No Epilogue Task")
    );
    ASSERT_TRUE(empty.valid());
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView plan(compiledGraph);
    for(usize index = 0u; index < resources.size(); ++index){
        SCOPED_TRACE(index);
        const Graphics::GpuCompiledTaskView writer = plan.findTask(writers[index]);
        ASSERT_TRUE(writer.valid());
        ASSERT_EQ(writer.plan->epilogueBarrierCount, 1u);
        EXPECT_EQ(writer.plan->epilogueBarrierOffset, resources.size() - index - 1u);
        EXPECT_EQ(writer.epilogueBarriers[0u].resource, resources[index]);
        EXPECT_EQ(writer.epilogueBarriers[0u].range.bufferRange, ranges[index]);
        EXPECT_EQ(writer.epilogueBarriers[0u].type, Graphics::GpuCompiledBarrierType::BufferOwnershipRelease);
        EXPECT_EQ(writer.epilogueBarriers[0u].sourceQueue, queues[0u].id);
        EXPECT_EQ(writer.epilogueBarriers[0u].destinationQueue, queues[1u].id);
        const Graphics::GpuCompiledTaskView consumer = plan.findTask(consumers[index]);
        ASSERT_TRUE(consumer.valid());
        ASSERT_EQ(consumer.plan->epilogueBarrierCount, s_ExpectedDualCount);
        EXPECT_EQ(consumer.plan->epilogueBarrierOffset, s_ExpectedDualCount + index * s_ExpectedDualCount);
        EXPECT_EQ(consumer.epilogueBarriers[0u].type, Graphics::GpuCompiledBarrierType::BufferStateExport);
        EXPECT_EQ(consumer.epilogueBarriers[1u].type, Graphics::GpuCompiledBarrierType::BufferOwnershipRelease);
        for(usize barrierIndex = 0u; barrierIndex < s_ExpectedDualCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = consumer.epilogueBarriers[barrierIndex];
            EXPECT_EQ(barrier.resource, resources[index]);
            EXPECT_EQ(barrier.range.bufferRange, ranges[index]);
            EXPECT_EQ(barrier.before, Graphics::ResourceStates::ShaderResource);
            EXPECT_EQ(barrier.after, Graphics::ResourceStates::ShaderResource);
            EXPECT_EQ(barrier.sourceQueue, queues[1u].id);
            EXPECT_EQ(barrier.destinationQueue, queues[barrierIndex == 0u ? 1u : s_ThirdElementIndex].id);
        }
    }
    const Graphics::GpuCompiledTaskView emptyView = plan.findTask(empty);
    ASSERT_TRUE(emptyView.valid());
    EXPECT_EQ(emptyView.plan->epilogueBarrierOffset, 6u);
    EXPECT_EQ(emptyView.plan->epilogueBarrierCount, 0u);
    EXPECT_EQ(emptyView.epilogueBarriers, nullptr);
}

TEST(GpuTaskGraphBufferRange, DISABLED_PendingEpilogueGroupingBenchmark1024Tasks){
    BenchmarkPendingEpilogueGrouping(1024u);
}

TEST(GpuTaskGraphBufferRange, DISABLED_PendingEpilogueGroupingBenchmark4096Tasks){
    BenchmarkPendingEpilogueGrouping(4096u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

