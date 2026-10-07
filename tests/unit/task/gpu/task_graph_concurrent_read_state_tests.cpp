// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_concurrent_read_state_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


[[nodiscard]] Graphics::GpuTaskId AddStateTask(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const Graphics::GpuTaskResourceUse& use,
    const Graphics::GpuTaskCommandRequirements& commands,
    const Graphics::GpuTaskId* const dependencies = nullptr,
    const usize dependencyCount = 0u,
    const bool mergeWithPrevious = false,
    const bool allowPacketMerge = false
){
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = !allowPacketMerge;
    scheduling.allowPacketMerge = allowPacketMerge;
    scheduling.mergeWithPrevious = mergeWithPrevious;
    Graphics::GpuTaskDesc description;
    description
        .setIdentity(identity)
        .setMarkerLabel("Read State Task")
        .setScheduling(scheduling)
        .setDependencies(dependencies, dependencyCount)
        .setResourceUses(&use, 1u)
    ;
    return graph.addTask(description, commands);
}

[[nodiscard]] bool HasPacketDependency(
    const Graphics::GpuCompiledGraph::ReadView& plan,
    const Graphics::GpuTaskId& consumer,
    const Graphics::GpuTaskId& producer
)noexcept{
    const Graphics::GpuSubmissionPacketId producerPacket = plan.packetForTask(producer);
    const Graphics::GpuCompiledPacketView consumerPacket = plan.packet(plan.packetForTask(consumer));
    if(!producerPacket.valid() || !consumerPacket.valid())
        return false;
    for(u32 index = 0u; index < consumerPacket.plan->dependencyCount; ++index){
        if(consumerPacket.dependencies[index].producer == producerPacket)
            return true;
    }
    return false;
}

[[nodiscard]] bool HasStateSeed(
    const Graphics::GpuCompiledGraph::ReadView& plan,
    const Graphics::GpuTaskId& consumer,
    const Graphics::GpuTaskId& producer
)noexcept{
    const Graphics::GpuSubmissionPacketId producerPacket = plan.packetForTask(producer);
    const Graphics::GpuCompiledTaskView consumerTask = plan.findTask(consumer);
    if(!producerPacket.valid() || !consumerTask.valid())
        return false;
    for(u32 index = 0u; index < consumerTask.plan->prologueStateSeedCount; ++index){
        if(consumerTask.prologueStateSeeds[index].sourcePacket == producerPacket)
            return true;
    }
    return false;
}


void CheckSingleLaneStateChangeScratch(const usize taskCount, usize& scratchPeak, const bool concurrentSharing = false){
    SCOPED_TRACE(taskCount);
    SCOPED_TRACE(concurrentSharing);
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddBufferMetadata(
        graph,
        Name("tests/task_graph/single_lane_read_state_resource"),
        "Single Lane Read State Resource",
        Graphics::ResourceStates::Common,
        concurrentSharing ? Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute : Graphics::ResourceQueueSharing::Exclusive
    );
    ASSERT_TRUE(resource.valid());
    for(usize index = 0u; index < taskCount; ++index){
        char identityText[32u] = {};
        const Name identity = DeriveName(Name("tests/task_graph/single_lane_read_state/"), FormatDecimal(index, identityText));
        const Graphics::GpuTaskResourceUse use{
            .resource = resource,
            .range = {},
            .requiredState = index % 2u == 0u ? Graphics::ResourceStates::ShaderResource : Graphics::ResourceStates::CopySource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        };
        ASSERT_TRUE(AddStateTask(graph, identity, use, GraphicsCommands()).valid());
    }
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{
        .queues = queues,
        .queueCount = concurrentSharing ? LengthOf(queues) : 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    Graphics::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    const Graphics::GpuTaskGraphCompiler compiler;
    Graphics::GpuTaskGraphCompileOptions options;
    options.allowMetadataOnlyTasks = true;
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    ASSERT_TRUE(compiler.compile(declarations, analysis, topology, assignments, compiledGraph, scratchArena, options));
    scratchPeak = scratchArena.memoryStats().peakUsedBytes;
    const Graphics::GpuCompiledGraph::ReadView plan(compiledGraph);
    ASSERT_EQ(plan.packetCount(), taskCount);
    for(usize index = 0u; index < taskCount; ++index){
        const Graphics::GpuCompiledPacketView packet = plan.packet(plan.packetIdAt(index));
        ASSERT_TRUE(packet.valid());
        ASSERT_EQ(packet.plan->dependencyCount, index == 0u ? 0u : 1u);
        if(index != 0u)
            EXPECT_EQ(packet.dependencies[0u].producer, plan.packetIdAt(index - 1u));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraph, KeepsConcurrentReadersIndependentAfterStateEstablishingProducer){
    const Graphics::GpuGraphResourceType::Enum resourceTypes[] = {
        Graphics::GpuGraphResourceType::Texture,
        Graphics::GpuGraphResourceType::Buffer,
        Graphics::GpuGraphResourceType::AccelStruct,
    };
    for(const Graphics::GpuGraphResourceType::Enum resourceType : resourceTypes){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = graph.importResource(
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph/read_fanout_resource"))
                .setMarkerLabel("Read Fanout Resource")
                .setType(resourceType)
                .setInitialState(Graphics::ResourceStates::Common)
                .setQueueSharing(Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute)
        );
        ASSERT_TRUE(resource.valid());
        const bool isAccelStruct = resourceType == Graphics::GpuGraphResourceType::AccelStruct;
        Graphics::GpuTaskResourceUse use{
            .resource = resource,
            .range = {},
            .requiredState = isAccelStruct ? Graphics::ResourceStates::AccelStructWrite : Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        const Graphics::GpuTaskId writer = AddStateTask(graph, Name("tests/task_graph/read_fanout_writer"), use, GraphicsCommands());
        use.requiredState = isAccelStruct ? Graphics::ResourceStates::AccelStructRead : Graphics::ResourceStates::ShaderResource;
        use.access = Graphics::GpuTaskResourceAccess::Read;
        const Graphics::GpuTaskId firstReader = AddStateTask(graph, Name("tests/task_graph/read_fanout_first"), use, GraphicsCommands());
        const Graphics::GpuTaskId computeReader = AddStateTask(graph, Name("tests/task_graph/read_fanout_compute"), use, ComputeCommands());
        const Graphics::GpuTaskId graphicsReader = AddStateTask(graph, Name("tests/task_graph/read_fanout_graphics"), use, GraphicsCommands());
        ASSERT_TRUE(writer.valid());
        ASSERT_TRUE(firstReader.valid());
        ASSERT_TRUE(computeReader.valid());
        ASSERT_TRUE(graphicsReader.valid());

        TwoQueueCompile compilation(testArena);
        ASSERT_TRUE(compilation.compile(graph));
        const Graphics::GpuCompiledGraph::ReadView plan(compilation.compiledGraph);
        EXPECT_TRUE(HasStateSeed(plan, computeReader, firstReader));
        EXPECT_TRUE(HasStateSeed(plan, graphicsReader, firstReader));
        EXPECT_FALSE(HasStateSeed(plan, graphicsReader, computeReader));
        EXPECT_FALSE(HasPacketDependency(plan, graphicsReader, computeReader));
        EXPECT_TRUE(HasPacketDependency(plan, firstReader, writer));
        EXPECT_TRUE(HasPacketDependency(plan, computeReader, firstReader));
        EXPECT_TRUE(HasPacketDependency(plan, graphicsReader, firstReader));
        EXPECT_EQ(plan.logicalOwnershipTransferCount(), 0u);
    }
}

TEST(GpuTaskGraph, KeepsCompatibleTextureReadMasksIndependentWithoutChangingSemanticStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddTextureMetadata(
        graph,
        Name("tests/task_graph/compatible_read_resource"),
        "Compatible Read Resource",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(resource.valid());
    Graphics::GpuTaskResourceUse use{
        .resource = resource,
        .range = {},
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    const Graphics::GpuTaskId firstReader = AddStateTask(graph, Name("tests/task_graph/compatible_read_first"), use, GraphicsCommands());
    const Graphics::GpuTaskId computeReader = AddStateTask(graph, Name("tests/task_graph/compatible_read_compute"), use, ComputeCommands());
    use.requiredState = Graphics::ResourceStates::DepthRead;
    const Graphics::GpuTaskId depthReader = AddStateTask(graph, Name("tests/task_graph/compatible_read_depth"), use, GraphicsCommands());
    ASSERT_TRUE(firstReader.valid());
    ASSERT_TRUE(computeReader.valid());
    ASSERT_TRUE(depthReader.valid());

    TwoQueueCompile compilation(testArena);
    ASSERT_TRUE(compilation.compile(graph));
    const Graphics::GpuCompiledGraph::ReadView plan(compilation.compiledGraph);
    EXPECT_TRUE(HasStateSeed(plan, depthReader, firstReader));
    EXPECT_FALSE(HasPacketDependency(plan, depthReader, computeReader));
    const Graphics::GpuCompiledTaskView depthTask = plan.findTask(depthReader);
    ASSERT_TRUE(depthTask.valid());
    bool hasSemanticTransition = false;
    for(u32 index = 0u; index < depthTask.plan->prologueBarrierCount; ++index){
        const Graphics::GpuCompiledBarrier& barrier = depthTask.prologueBarriers[index];
        hasSemanticTransition = hasSemanticTransition || (
            barrier.resource == resource
            && barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.before == Graphics::ResourceStates::ShaderResource
            && barrier.after == Graphics::ResourceStates::DepthRead
        );
    }
    EXPECT_TRUE(hasSemanticTransition);
}

TEST(GpuTaskGraph, JoinsEveryConcurrentReaderBeforeIncompatibleReadStateChange){
    const Graphics::GpuGraphResourceType::Enum resourceTypes[] = {
        Graphics::GpuGraphResourceType::Texture,
        Graphics::GpuGraphResourceType::Buffer,
    };
    for(const Graphics::GpuGraphResourceType::Enum resourceType : resourceTypes){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = graph.importResource(
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph/read_state_join_resource"))
                .setMarkerLabel("Read State Join Resource")
                .setType(resourceType)
                .setInitialState(Graphics::ResourceStates::Common)
                .setQueueSharing(Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute)
        );
        ASSERT_TRUE(resource.valid());
        Graphics::GpuTaskResourceUse use{
            .resource = resource,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        };
        const Graphics::GpuTaskId firstReader = AddStateTask(graph, Name("tests/task_graph/read_state_join_first"), use, GraphicsCommands());
        const Graphics::GpuTaskId computeReader = AddStateTask(graph, Name("tests/task_graph/read_state_join_compute"), use, ComputeCommands());
        if(resourceType == Graphics::GpuGraphResourceType::Texture)
            use.requiredState = Graphics::ResourceStates::DepthRead;
        const Graphics::GpuTaskId graphicsReader = AddStateTask(graph, Name("tests/task_graph/read_state_join_graphics"), use, GraphicsCommands());
        use.requiredState = Graphics::ResourceStates::CopySource;
        const Graphics::GpuTaskId transitionReader = AddStateTask(graph, Name("tests/task_graph/read_state_join_transition"), use, GraphicsCommands());
        ASSERT_TRUE(firstReader.valid());
        ASSERT_TRUE(computeReader.valid());
        ASSERT_TRUE(graphicsReader.valid());
        ASSERT_TRUE(transitionReader.valid());

        TwoQueueCompile compilation(testArena);
        ASSERT_TRUE(compilation.compile(graph));
        const Graphics::GpuCompiledGraph::ReadView plan(compilation.compiledGraph);
        EXPECT_FALSE(HasPacketDependency(plan, graphicsReader, computeReader));
        EXPECT_TRUE(HasPacketDependency(plan, transitionReader, computeReader));
        EXPECT_TRUE(HasPacketDependency(plan, transitionReader, graphicsReader));
        EXPECT_TRUE(HasStateSeed(plan, transitionReader, graphicsReader));
    }
}

TEST(GpuTaskGraph, JoinsConcurrentReadersBeforeTaskInternalReadStateChange){
    const Graphics::GpuGraphResourceType::Enum resourceTypes[] = {
        Graphics::GpuGraphResourceType::Texture,
        Graphics::GpuGraphResourceType::Buffer,
    };
    for(const Graphics::GpuGraphResourceType::Enum resourceType : resourceTypes){
        for(const bool overlappingLaterUse : { false, true }){
            SCOPED_TRACE(resourceType);
            SCOPED_TRACE(overlappingLaterUse);
            TestArena testArena;
            Graphics::GpuTaskGraph graph(testArena.arena);
            const Graphics::GpuGraphResourceId resource = graph.importResource(
                Graphics::GpuGraphResourceDesc{}
                    .setIdentity(Name("tests/task_graph/internal_read_state_resource"))
                    .setMarkerLabel("Internal Read State Resource")
                    .setType(resourceType)
                    .setInitialState(Graphics::ResourceStates::Common)
                    .setQueueSharing(Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute)
            );
            ASSERT_TRUE(resource.valid());
            Graphics::GpuTaskResourceUse use{
                .resource = resource,
                .range = {},
                .requiredState = Graphics::ResourceStates::ShaderResource,
                .access = Graphics::GpuTaskResourceAccess::Read,
            };
            if(resourceType == Graphics::GpuGraphResourceType::Texture)
                use.range.textureSubresources = Graphics::TextureSubresourceSet(0u, overlappingLaterUse ? 2u : 1u, 0u, 1u);
            else
                use.range.bufferRange = Graphics::BufferRange(0u, overlappingLaterUse ? 128u : 64u);
            const Graphics::GpuTaskId firstReader = AddStateTask(graph, Name("tests/task_graph/internal_read_state_first"), use, GraphicsCommands());
            const Graphics::GpuTaskId computeReader = AddStateTask(graph, Name("tests/task_graph/internal_read_state_compute"), use, ComputeCommands());
            const Graphics::GpuTaskId graphicsReader = AddStateTask(graph, Name("tests/task_graph/internal_read_state_graphics"), use, GraphicsCommands());
            Graphics::GpuTaskResourceUse internalUses[] = { use, use };
            internalUses[1u].requiredState = Graphics::ResourceStates::CopySource;
            if(resourceType == Graphics::GpuGraphResourceType::Texture)
                internalUses[1u].range.textureSubresources = Graphics::TextureSubresourceSet(1u, 1u, 0u, 1u);
            else
                internalUses[1u].range.bufferRange = Graphics::BufferRange(64u, 64u);
            Graphics::GpuTaskSchedulingHint scheduling;
            scheduling.forceSubmissionBoundary = true;
            scheduling.allowPacketMerge = false;
            Graphics::GpuTaskDesc description;
            description
                .setIdentity(Name("tests/task_graph/internal_read_state_transition"))
                .setMarkerLabel("Internal Read State Transition")
                .setScheduling(scheduling)
                .setResourceUses(internalUses, LengthOf(internalUses))
            ;
            const Graphics::GpuTaskId transitionReader = graph.addTask(description, GraphicsCommands());
            ASSERT_TRUE(firstReader.valid());
            ASSERT_TRUE(computeReader.valid());
            ASSERT_TRUE(graphicsReader.valid());
            ASSERT_TRUE(transitionReader.valid());

            TwoQueueCompile compilation(testArena);
            ASSERT_TRUE(compilation.compile(graph));
            const Graphics::GpuCompiledGraph::ReadView plan(compilation.compiledGraph);
            ASSERT_FALSE(HasPacketDependency(plan, graphicsReader, computeReader));
            EXPECT_EQ(HasPacketDependency(plan, transitionReader, computeReader), overlappingLaterUse);
            if(overlappingLaterUse){
                EXPECT_TRUE(HasStateSeed(plan, transitionReader, graphicsReader));
                EXPECT_TRUE(HasPacketDependency(plan, transitionReader, graphicsReader));
            }
            else
                EXPECT_TRUE(HasStateSeed(plan, transitionReader, firstReader));
        }
    }
}

TEST(GpuTaskGraph, PreservesReaderJoinAcrossPartialTaskInternalStateEpoch){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddBufferMetadata(
        graph,
        Name("tests/task_graph/partial_read_epoch_resource"),
        "Partial Read Epoch Resource",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(resource.valid());
    Graphics::GpuTaskResourceUse use{
        .resource = resource,
        .range = { .bufferRange = Graphics::BufferRange(0u, 64u) },
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    const Graphics::GpuTaskId firstReader = AddStateTask(graph, Name("tests/task_graph/partial_read_epoch_first"), use, GraphicsCommands());
    const Graphics::GpuTaskId computeReader = AddStateTask(graph, Name("tests/task_graph/partial_read_epoch_compute"), use, ComputeCommands());
    use.range.bufferRange = Graphics::BufferRange(64u, 64u);
    Graphics::GpuTaskResourceUse internalUses[] = { use, use, use };
    internalUses[1u].requiredState = Graphics::ResourceStates::CopySource;
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc description;
    description
        .setIdentity(Name("tests/task_graph/partial_read_epoch_internal"))
        .setMarkerLabel("Partial Read Epoch Internal")
        .setScheduling(scheduling)
        .setResourceUses(internalUses, LengthOf(internalUses))
    ;
    const Graphics::GpuTaskId internalReader = graph.addTask(description, GraphicsCommands());
    use.range.bufferRange = Graphics::BufferRange(0u, 128u);
    const Graphics::GpuTaskId spanningReader = AddStateTask(graph, Name("tests/task_graph/partial_read_epoch_spanning"), use, GraphicsCommands());
    use.requiredState = Graphics::ResourceStates::CopySource;
    const Graphics::GpuTaskId transitionReader = AddStateTask(graph, Name("tests/task_graph/partial_read_epoch_transition"), use, GraphicsCommands());
    ASSERT_TRUE(firstReader.valid());
    ASSERT_TRUE(computeReader.valid());
    ASSERT_TRUE(internalReader.valid());
    ASSERT_TRUE(spanningReader.valid());
    ASSERT_TRUE(transitionReader.valid());

    TwoQueueCompile compilation(testArena);
    ASSERT_TRUE(compilation.compile(graph));
    const Graphics::GpuCompiledGraph::ReadView plan(compilation.compiledGraph);
    ASSERT_FALSE(HasPacketDependency(plan, internalReader, computeReader));
    ASSERT_FALSE(HasPacketDependency(plan, spanningReader, computeReader));
    // The second-half task's internal transition does not dominate the independent first-half Compute reader.
    EXPECT_TRUE(HasPacketDependency(plan, transitionReader, computeReader));
    EXPECT_TRUE(HasPacketDependency(plan, transitionReader, spanningReader));
}

TEST(GpuTaskGraph, PreservesExplicitDependencyWhenConcurrentReaderUsesEarlierStateSource){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddBufferMetadata(
        graph,
        Name("tests/task_graph/read_explicit_dependency_resource"),
        "Read Explicit Dependency Resource",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(resource.valid());
    const Graphics::GpuTaskResourceUse use{
        .resource = resource,
        .range = {},
        .requiredState = Graphics::ResourceStates::ConstantBuffer,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    const Graphics::GpuTaskId firstReader = AddStateTask(graph, Name("tests/task_graph/read_explicit_dependency_first"), use, GraphicsCommands());
    const Graphics::GpuTaskId computeReader = AddStateTask(graph, Name("tests/task_graph/read_explicit_dependency_compute"), use, ComputeCommands());
    const Graphics::GpuTaskId graphicsReader = AddStateTask(
        graph,
        Name("tests/task_graph/read_explicit_dependency_graphics"),
        use,
        GraphicsCommands(),
        &computeReader,
        1u
    );
    ASSERT_TRUE(firstReader.valid());
    ASSERT_TRUE(computeReader.valid());
    ASSERT_TRUE(graphicsReader.valid());

    TwoQueueCompile compilation(testArena);
    ASSERT_TRUE(compilation.compile(graph));
    const Graphics::GpuCompiledGraph::ReadView plan(compilation.compiledGraph);
    EXPECT_TRUE(HasStateSeed(plan, graphicsReader, firstReader));
    EXPECT_FALSE(HasStateSeed(plan, graphicsReader, computeReader));
    EXPECT_TRUE(HasPacketDependency(plan, graphicsReader, computeReader));
}

TEST(GpuTaskGraph, KeepsPartialReadOrderingWhenSourcePacketChangesAdjacentRange){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddBufferMetadata(
        graph,
        Name("tests/task_graph/read_packet_tail_resource"),
        "Read Packet Tail Resource",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(resource.valid());
    Graphics::GpuTaskResourceUse use{
        .resource = resource,
        .range = { .bufferRange = Graphics::BufferRange(0u, 128u) },
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    const Graphics::GpuTaskId firstReader = AddStateTask(
        graph,
        Name("tests/task_graph/read_packet_tail_first"),
        use,
        GraphicsCommands(),
        nullptr,
        0u,
        false,
        true
    );
    use.range.bufferRange = Graphics::BufferRange(64u, 64u);
    use.requiredState = Graphics::ResourceStates::CopySource;
    const Graphics::GpuTaskId packetTail = AddStateTask(
        graph,
        Name("tests/task_graph/read_packet_tail_changed_range"),
        use,
        GraphicsCommands(),
        &firstReader,
        1u,
        true,
        true
    );
    use.range.bufferRange = Graphics::BufferRange(0u, 64u);
    use.requiredState = Graphics::ResourceStates::ShaderResource;
    const Graphics::GpuTaskId computeReader = AddStateTask(graph, Name("tests/task_graph/read_packet_tail_compute"), use, ComputeCommands());
    const Graphics::GpuTaskId graphicsReader = AddStateTask(graph, Name("tests/task_graph/read_packet_tail_graphics"), use, GraphicsCommands());
    ASSERT_TRUE(firstReader.valid());
    ASSERT_TRUE(packetTail.valid());
    ASSERT_TRUE(computeReader.valid());
    ASSERT_TRUE(graphicsReader.valid());

    TwoQueueCompile compilation(testArena);
    ASSERT_TRUE(compilation.compile(graph));
    const Graphics::GpuCompiledGraph::ReadView plan(compilation.compiledGraph);
    ASSERT_TRUE(plan.tasksSharePacket(firstReader, packetTail));
    EXPECT_TRUE(HasStateSeed(plan, graphicsReader, computeReader));
    EXPECT_FALSE(HasStateSeed(plan, graphicsReader, firstReader));
    EXPECT_TRUE(HasPacketDependency(plan, graphicsReader, computeReader));
}


TEST(GpuTaskGraph, KeepsSingleLaneAlternatingReadStateScratchGrowthLinear){
    usize smallScratchPeak = 0u;
    usize largeScratchPeak = 0u;
    CheckSingleLaneStateChangeScratch(128u, smallScratchPeak);
    CheckSingleLaneStateChangeScratch(512u, largeScratchPeak);
    ASSERT_NE(smallScratchPeak, 0u);
    ASSERT_NE(largeScratchPeak, 0u);
    RecordUnsignedTestProperty("small_read_state_scratch_peak_bytes", smallScratchPeak);
    RecordUnsignedTestProperty("large_read_state_scratch_peak_bytes", largeScratchPeak);
    // Four times as many serial readers must not produce all historical reader pairs or a dense packet closure.
    EXPECT_LE(largeScratchPeak, smallScratchPeak * 6u);
}


TEST(GpuTaskGraph, KeepsConcurrentSingleLaneAlternatingReadStateScratchGrowthLinear){
    usize smallScratchPeak = 0u;
    usize largeScratchPeak = 0u;
    CheckSingleLaneStateChangeScratch(128u, smallScratchPeak, true);
    CheckSingleLaneStateChangeScratch(512u, largeScratchPeak, true);
    ASSERT_NE(smallScratchPeak, 0u);
    ASSERT_NE(largeScratchPeak, 0u);
    RecordUnsignedTestProperty("small_concurrent_read_state_scratch_peak_bytes", smallScratchPeak);
    RecordUnsignedTestProperty("large_concurrent_read_state_scratch_peak_bytes", largeScratchPeak);
    EXPECT_LE(largeScratchPeak, smallScratchPeak * 6u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

