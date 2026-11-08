// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_concurrent_read_state_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


[[nodiscard]] Expected<Core::GpuTaskId> AddStateTask(
    Core::GpuTaskGraph& graph,
    const Name& identity,
    const Core::GpuTaskResourceUse& use,
    const Core::GpuTaskCommandRequirements& commands,
    const Span<const Core::GpuTaskId> dependencies = {},
    const b8 mergeWithPrevious = false,
    const b8 allowPacketMerge = false
){
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = !allowPacketMerge;
    scheduling.allowPacketMerge = allowPacketMerge;
    scheduling.mergeWithPrevious = mergeWithPrevious;
    Core::GpuTaskDesc description;
    description.setIdentity(identity).setMarkerLabel("Read State Task").setScheduling(scheduling).setDependencies(dependencies).setResourceUses(Span(&use, 1u));
    return graph.addTask(description, commands);
}

[[nodiscard]] b8 HasPacketDependency(const Core::GpuCompiledGraph::ReadView& plan, const Core::GpuTaskId& consumer, const Core::GpuTaskId& producer)noexcept{
    const Core::GpuSubmissionPacketId producerPacket = plan.packetForTask(producer);
    const Core::GpuCompiledPacketView consumerPacket = plan.packet(plan.packetForTask(consumer));
    if(!producerPacket.valid() || !consumerPacket.valid())
        return false;
    for(u32 index = 0u; index < consumerPacket.plan->dependencyCount; ++index){
        if(consumerPacket.dependencies[index].producer == producerPacket)
            return true;
    }
    return false;
}

[[nodiscard]] b8 HasStateSeed(const Core::GpuCompiledGraph::ReadView& plan, const Core::GpuTaskId& consumer, const Core::GpuTaskId& producer)noexcept{
    const Core::GpuSubmissionPacketId producerPacket = plan.packetForTask(producer);
    const Core::GpuCompiledTaskView consumerTask = plan.findTask(consumer);
    if(!producerPacket.valid() || !consumerTask.valid())
        return false;
    for(u32 index = 0u; index < consumerTask.plan->prologueStateSeedCount; ++index){
        if(consumerTask.prologueStateSeeds[index].sourcePacket == producerPacket)
            return true;
    }
    return false;
}


void CheckSingleLaneStateChangeScratch(usize& scratchPeak, const usize taskCount, const b8 concurrentSharing = false){
    SCOPED_TRACE(taskCount);
    SCOPED_TRACE(concurrentSharing);
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBufferMetadata(
        graph,
        Name(TaskGraphTestUtils::s_ConcurrentReadStateSingleLaneReadStateResource),
        "Single Lane Read State Resource",
        Core::ResourceStates::Common,
        concurrentSharing ? Core::ResourceQueueSharing::GraphicsAndAsyncCompute : Core::ResourceQueueSharing::Exclusive
    );
    ASSERT_TRUE(resourceResult);
    const Core::GpuGraphResourceId resource = *resourceResult;
    ASSERT_TRUE(resource.valid());
    for(usize index = 0u; index < taskCount; ++index){
        const auto identityNameResult = IndexedName(Name(TaskGraphTestUtils::s_ConcurrentReadStateSingleLaneReadState), index);
        ASSERT_TRUE(identityNameResult);
        const Name identity = *identityNameResult;
        const Core::GpuTaskResourceUse use{ .resource = resource, .range = {}, .requiredState = index % 2u == 0u ? Core::ResourceStates::ShaderResource : Core::ResourceStates::CopySource, .access = Core::GpuTaskResourceAccess::Read, };
        {
            const auto gpuTaskAdmission = AddStateTask(graph, identity, use, GraphicsCommands());
            ASSERT_TRUE(gpuTaskAdmission);
            ASSERT_TRUE(gpuTaskAdmission->valid());
        }
    }
    const Array<Core::GpuPhysicalQueueInfo, 2> queues = { GraphicsQueue(), DedicatedComputeQueue() };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = concurrentSharing ? LengthOf(queues) : 1u, };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    const Core::GpuTaskGraphCompiler compiler;
    Core::GpuTaskGraphCompileOptions options;
    options.allowMetadataOnlyTasks = true;
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    ASSERT_TRUE(compiler.compile(declarations, topology, options, scratchArena, analysis, assignments, compiledGraph));
    scratchPeak = scratchArena.memoryStats().peakUsedBytes;
    const Core::GpuCompiledGraph::ReadView plan(compiledGraph);
    ASSERT_EQ(plan.packetCount(), taskCount);
    for(usize index = 0u; index < taskCount; ++index){
        const Core::GpuCompiledPacketView packet = plan.packet(plan.packetIdAt(index));
        ASSERT_TRUE(packet.valid());
        ASSERT_EQ(packet.plan->dependencyCount, index == 0u ? 0u : 1u);
        if(index != 0u)
            EXPECT_EQ(packet.dependencies[0u].producer, plan.packetIdAt(index - 1u));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraph, KeepsConcurrentReadersIndependentAfterStateEstablishingProducer){
    const Array<Core::GpuGraphResourceType::Enum, 3> resourceTypes = { Core::GpuGraphResourceType::Texture, Core::GpuGraphResourceType::Buffer, Core::GpuGraphResourceType::AccelStruct, };
    for(const Core::GpuGraphResourceType::Enum resourceType : resourceTypes){
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = graph.importResource(
            Core::GpuGraphResourceDesc{}
                .setIdentity(Name(TaskGraphTestUtils::s_ConcurrentReadStateReadFanoutResource))
                .setMarkerLabel("Read Fanout Resource")
                .setType(resourceType)
                .setInitialState(Core::ResourceStates::Common)
                .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        );
        ASSERT_TRUE(resourceResult);
        const Core::GpuGraphResourceId resource = *resourceResult;
        ASSERT_TRUE(resource.valid());
        const b8 isAccelStruct = resourceType == Core::GpuGraphResourceType::AccelStruct;
        Core::GpuTaskResourceUse use{ .resource = resource, .range = {}, .requiredState = isAccelStruct ? Core::ResourceStates::AccelStructWrite : Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write, };
        const auto writerResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateReadFanoutWriter), use, GraphicsCommands());
        ASSERT_TRUE(writerResult);
        const Core::GpuTaskId writer = *writerResult;
        use.requiredState = isAccelStruct ? Core::ResourceStates::AccelStructRead : Core::ResourceStates::ShaderResource;
        use.access = Core::GpuTaskResourceAccess::Read;
        const auto firstReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateReadFanoutFirst), use, GraphicsCommands());
        ASSERT_TRUE(firstReaderResult);
        const Core::GpuTaskId firstReader = *firstReaderResult;
        const auto computeReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateReadFanoutCompute), use, ComputeCommands());
        ASSERT_TRUE(computeReaderResult);
        const Core::GpuTaskId computeReader = *computeReaderResult;
        const auto graphicsReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateReadFanoutGraphics), use, GraphicsCommands());
        ASSERT_TRUE(graphicsReaderResult);
        const Core::GpuTaskId graphicsReader = *graphicsReaderResult;
        ASSERT_TRUE(writer.valid());
        ASSERT_TRUE(firstReader.valid());
        ASSERT_TRUE(computeReader.valid());
        ASSERT_TRUE(graphicsReader.valid());

        TwoQueueCompile compilation(testArena);
        ASSERT_TRUE(compilation.compile(graph));
        const Core::GpuCompiledGraph::ReadView plan(compilation.compiledGraph);
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
    Core::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddTextureMetadata(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateCompatibleReadResource), "Compatible Read Resource", Core::ResourceStates::Common, Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
    ASSERT_TRUE(resourceResult);
    const Core::GpuGraphResourceId resource = *resourceResult;
    ASSERT_TRUE(resource.valid());
    Core::GpuTaskResourceUse use{ .resource = resource, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    const auto firstReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateCompatibleReadFirst), use, GraphicsCommands());
    ASSERT_TRUE(firstReaderResult);
    const Core::GpuTaskId firstReader = *firstReaderResult;
    const auto computeReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateCompatibleReadCompute), use, ComputeCommands());
    ASSERT_TRUE(computeReaderResult);
    const Core::GpuTaskId computeReader = *computeReaderResult;
    use.requiredState = Core::ResourceStates::DepthRead;
    const auto depthReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateCompatibleReadDepth), use, GraphicsCommands());
    ASSERT_TRUE(depthReaderResult);
    const Core::GpuTaskId depthReader = *depthReaderResult;
    ASSERT_TRUE(firstReader.valid());
    ASSERT_TRUE(computeReader.valid());
    ASSERT_TRUE(depthReader.valid());

    TwoQueueCompile compilation(testArena);
    ASSERT_TRUE(compilation.compile(graph));
    const Core::GpuCompiledGraph::ReadView plan(compilation.compiledGraph);
    EXPECT_TRUE(HasStateSeed(plan, depthReader, firstReader));
    EXPECT_FALSE(HasPacketDependency(plan, depthReader, computeReader));
    const Core::GpuCompiledTaskView depthTask = plan.findTask(depthReader);
    ASSERT_TRUE(depthTask.valid());
    b8 hasSemanticTransition = false;
    for(u32 index = 0u; index < depthTask.plan->prologueBarrierCount; ++index){
        const Core::GpuCompiledBarrier& barrier = depthTask.prologueBarriers[index];
        hasSemanticTransition = hasSemanticTransition || (
            barrier.resource == resource
            && barrier.type == Core::GpuCompiledBarrierType::TextureTransition
            && barrier.before == Core::ResourceStates::ShaderResource
            && barrier.after == Core::ResourceStates::DepthRead
        );
    }
    EXPECT_TRUE(hasSemanticTransition);
}

TEST(GpuTaskGraph, JoinsEveryConcurrentReaderBeforeIncompatibleReadStateChange){
    const Array<Core::GpuGraphResourceType::Enum, 2> resourceTypes = { Core::GpuGraphResourceType::Texture, Core::GpuGraphResourceType::Buffer, };
    for(const Core::GpuGraphResourceType::Enum resourceType : resourceTypes){
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = graph.importResource(
            Core::GpuGraphResourceDesc{}
                .setIdentity(Name(TaskGraphTestUtils::s_ConcurrentReadStateReadStateJoinResource))
                .setMarkerLabel("Read State Join Resource")
                .setType(resourceType)
                .setInitialState(Core::ResourceStates::Common)
                .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        );
        ASSERT_TRUE(resourceResult);
        const Core::GpuGraphResourceId resource = *resourceResult;
        ASSERT_TRUE(resource.valid());
        Core::GpuTaskResourceUse use{ .resource = resource, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
        const auto firstReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateReadStateJoinFirst), use, GraphicsCommands());
        ASSERT_TRUE(firstReaderResult);
        const Core::GpuTaskId firstReader = *firstReaderResult;
        const auto computeReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateReadStateJoinCompute), use, ComputeCommands());
        ASSERT_TRUE(computeReaderResult);
        const Core::GpuTaskId computeReader = *computeReaderResult;
        if(resourceType == Core::GpuGraphResourceType::Texture)
            use.requiredState = Core::ResourceStates::DepthRead;
        const auto graphicsReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateReadStateJoinGraphics), use, GraphicsCommands());
        ASSERT_TRUE(graphicsReaderResult);
        const Core::GpuTaskId graphicsReader = *graphicsReaderResult;
        use.requiredState = Core::ResourceStates::CopySource;
        const auto transitionReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateReadStateJoinTransition), use, GraphicsCommands());
        ASSERT_TRUE(transitionReaderResult);
        const Core::GpuTaskId transitionReader = *transitionReaderResult;
        ASSERT_TRUE(firstReader.valid());
        ASSERT_TRUE(computeReader.valid());
        ASSERT_TRUE(graphicsReader.valid());
        ASSERT_TRUE(transitionReader.valid());

        TwoQueueCompile compilation(testArena);
        ASSERT_TRUE(compilation.compile(graph));
        const Core::GpuCompiledGraph::ReadView plan(compilation.compiledGraph);
        EXPECT_FALSE(HasPacketDependency(plan, graphicsReader, computeReader));
        EXPECT_TRUE(HasPacketDependency(plan, transitionReader, computeReader));
        EXPECT_TRUE(HasPacketDependency(plan, transitionReader, graphicsReader));
        EXPECT_TRUE(HasStateSeed(plan, transitionReader, graphicsReader));
    }
}

TEST(GpuTaskGraph, JoinsConcurrentReadersBeforeTaskInternalReadStateChange){
    const Array<Core::GpuGraphResourceType::Enum, 2> resourceTypes = { Core::GpuGraphResourceType::Texture, Core::GpuGraphResourceType::Buffer, };
    for(const Core::GpuGraphResourceType::Enum resourceType : resourceTypes){
        for(const b8 overlappingLaterUse : { false, true }){
            SCOPED_TRACE(resourceType);
            SCOPED_TRACE(overlappingLaterUse);
            TestArena testArena;
            Core::GpuTaskGraph graph(testArena.arena);
            const auto resourceResult = graph.importResource(
                Core::GpuGraphResourceDesc{}
                    .setIdentity(Name(TaskGraphTestUtils::s_ConcurrentReadStateInternalReadStateResource))
                    .setMarkerLabel("Internal Read State Resource")
                    .setType(resourceType)
                    .setInitialState(Core::ResourceStates::Common)
                    .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            );
            ASSERT_TRUE(resourceResult);
            const Core::GpuGraphResourceId resource = *resourceResult;
            ASSERT_TRUE(resource.valid());
            Core::GpuTaskResourceUse use{ .resource = resource, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
            if(resourceType == Core::GpuGraphResourceType::Texture)
                use.range.textureSubresources = Core::TextureSubresourceSet(0u, overlappingLaterUse ? 2u : 1u, 0u, 1u);
            else
                use.range.bufferRange = Core::BufferRange(0u, overlappingLaterUse ? 128u : 64u);
            const auto firstReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateInternalReadStateFirst), use, GraphicsCommands());
            ASSERT_TRUE(firstReaderResult);
            const Core::GpuTaskId firstReader = *firstReaderResult;
            const auto computeReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateInternalReadStateCompute), use, ComputeCommands());
            ASSERT_TRUE(computeReaderResult);
            const Core::GpuTaskId computeReader = *computeReaderResult;
            const auto graphicsReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateInternalReadStateGraphics), use, GraphicsCommands());
            ASSERT_TRUE(graphicsReaderResult);
            const Core::GpuTaskId graphicsReader = *graphicsReaderResult;
            Array<Core::GpuTaskResourceUse, 2> internalUses = { use, use };
            internalUses[1u].requiredState = Core::ResourceStates::CopySource;
            if(resourceType == Core::GpuGraphResourceType::Texture)
                internalUses[1u].range.textureSubresources = Core::TextureSubresourceSet(1u, 1u, 0u, 1u);
            else
                internalUses[1u].range.bufferRange = Core::BufferRange(64u, 64u);
            Core::GpuTaskSchedulingHint scheduling;
            scheduling.forceSubmissionBoundary = true;
            scheduling.allowPacketMerge = false;
            Core::GpuTaskDesc description;
            description.setIdentity(Name(TaskGraphTestUtils::s_ConcurrentReadStateInternalReadStateTransition)).setMarkerLabel("Internal Read State Transition").setScheduling(scheduling).setResourceUses(internalUses);
            const auto transitionReaderResult = graph.addTask(description, GraphicsCommands());
            ASSERT_TRUE(transitionReaderResult);
            const Core::GpuTaskId transitionReader = *transitionReaderResult;
            ASSERT_TRUE(firstReader.valid());
            ASSERT_TRUE(computeReader.valid());
            ASSERT_TRUE(graphicsReader.valid());
            ASSERT_TRUE(transitionReader.valid());

            TwoQueueCompile compilation(testArena);
            ASSERT_TRUE(compilation.compile(graph));
            const Core::GpuCompiledGraph::ReadView plan(compilation.compiledGraph);
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
    Core::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBufferMetadata(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStatePartialReadEpochResource), "Partial Read Epoch Resource", Core::ResourceStates::Common, Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
    ASSERT_TRUE(resourceResult);
    const Core::GpuGraphResourceId resource = *resourceResult;
    ASSERT_TRUE(resource.valid());
    Core::GpuTaskResourceUse use{ .resource = resource, .range = { .bufferRange = Core::BufferRange(0u, 64u) }, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    const auto firstReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStatePartialReadEpochFirst), use, GraphicsCommands());
    ASSERT_TRUE(firstReaderResult);
    const Core::GpuTaskId firstReader = *firstReaderResult;
    const auto computeReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStatePartialReadEpochCompute), use, ComputeCommands());
    ASSERT_TRUE(computeReaderResult);
    const Core::GpuTaskId computeReader = *computeReaderResult;
    use.range.bufferRange = Core::BufferRange(64u, 64u);
    Array<Core::GpuTaskResourceUse, 3> internalUses = { use, use, use };
    internalUses[1u].requiredState = Core::ResourceStates::CopySource;
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc description;
    description.setIdentity(Name(TaskGraphTestUtils::s_ConcurrentReadStatePartialReadEpochInternal)).setMarkerLabel("Partial Read Epoch Internal").setScheduling(scheduling).setResourceUses(internalUses);
    const auto internalReaderResult = graph.addTask(description, GraphicsCommands());
    ASSERT_TRUE(internalReaderResult);
    const Core::GpuTaskId internalReader = *internalReaderResult;
    use.range.bufferRange = Core::BufferRange(0u, 128u);
    const auto spanningReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStatePartialReadEpochSpanning), use, GraphicsCommands());
    ASSERT_TRUE(spanningReaderResult);
    const Core::GpuTaskId spanningReader = *spanningReaderResult;
    use.requiredState = Core::ResourceStates::CopySource;
    const auto transitionReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStatePartialReadEpochTransition), use, GraphicsCommands());
    ASSERT_TRUE(transitionReaderResult);
    const Core::GpuTaskId transitionReader = *transitionReaderResult;
    ASSERT_TRUE(firstReader.valid());
    ASSERT_TRUE(computeReader.valid());
    ASSERT_TRUE(internalReader.valid());
    ASSERT_TRUE(spanningReader.valid());
    ASSERT_TRUE(transitionReader.valid());

    TwoQueueCompile compilation(testArena);
    ASSERT_TRUE(compilation.compile(graph));
    const Core::GpuCompiledGraph::ReadView plan(compilation.compiledGraph);
    ASSERT_FALSE(HasPacketDependency(plan, internalReader, computeReader));
    ASSERT_FALSE(HasPacketDependency(plan, spanningReader, computeReader));
    // The second-half task's internal transition does not dominate the independent first-half Compute reader.
    EXPECT_TRUE(HasPacketDependency(plan, transitionReader, computeReader));
    EXPECT_TRUE(HasPacketDependency(plan, transitionReader, spanningReader));
}

TEST(GpuTaskGraph, PreservesExplicitDependencyWhenConcurrentReaderUsesEarlierStateSource){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBufferMetadata(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateReadExplicitDependencyResource), "Read Explicit Dependency Resource", Core::ResourceStates::Common, Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
    ASSERT_TRUE(resourceResult);
    const Core::GpuGraphResourceId resource = *resourceResult;
    ASSERT_TRUE(resource.valid());
    const Core::GpuTaskResourceUse use{ .resource = resource, .range = {}, .requiredState = Core::ResourceStates::ConstantBuffer, .access = Core::GpuTaskResourceAccess::Read, };
    const auto firstReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateReadExplicitDependencyFirst), use, GraphicsCommands());
    ASSERT_TRUE(firstReaderResult);
    const Core::GpuTaskId firstReader = *firstReaderResult;
    const auto computeReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateReadExplicitDependencyCompute), use, ComputeCommands());
    ASSERT_TRUE(computeReaderResult);
    const Core::GpuTaskId computeReader = *computeReaderResult;
    const auto graphicsReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateReadExplicitDependencyGraphics), use, GraphicsCommands(), Span(&computeReader, 1u));
    ASSERT_TRUE(graphicsReaderResult);
    const Core::GpuTaskId graphicsReader = *graphicsReaderResult;
    ASSERT_TRUE(firstReader.valid());
    ASSERT_TRUE(computeReader.valid());
    ASSERT_TRUE(graphicsReader.valid());

    TwoQueueCompile compilation(testArena);
    ASSERT_TRUE(compilation.compile(graph));
    const Core::GpuCompiledGraph::ReadView plan(compilation.compiledGraph);
    EXPECT_TRUE(HasStateSeed(plan, graphicsReader, firstReader));
    EXPECT_FALSE(HasStateSeed(plan, graphicsReader, computeReader));
    EXPECT_TRUE(HasPacketDependency(plan, graphicsReader, computeReader));
}

TEST(GpuTaskGraph, KeepsPartialReadOrderingWhenSourcePacketChangesAdjacentRange){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBufferMetadata(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateReadPacketTailResource), "Read Packet Tail Resource", Core::ResourceStates::Common, Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
    ASSERT_TRUE(resourceResult);
    const Core::GpuGraphResourceId resource = *resourceResult;
    ASSERT_TRUE(resource.valid());
    Core::GpuTaskResourceUse use{ .resource = resource, .range = { .bufferRange = Core::BufferRange(0u, 128u) }, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    const auto firstReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateReadPacketTailFirst), use, GraphicsCommands(), {}, false, true);
    ASSERT_TRUE(firstReaderResult);
    const Core::GpuTaskId firstReader = *firstReaderResult;
    use.range.bufferRange = Core::BufferRange(64u, 64u);
    use.requiredState = Core::ResourceStates::CopySource;
    const auto packetTailResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateReadPacketTailChangedRange), use, GraphicsCommands(), Span(&firstReader, 1u), true, true);
    ASSERT_TRUE(packetTailResult);
    const Core::GpuTaskId packetTail = *packetTailResult;
    use.range.bufferRange = Core::BufferRange(0u, 64u);
    use.requiredState = Core::ResourceStates::ShaderResource;
    const auto computeReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateReadPacketTailCompute), use, ComputeCommands());
    ASSERT_TRUE(computeReaderResult);
    const Core::GpuTaskId computeReader = *computeReaderResult;
    const auto graphicsReaderResult = AddStateTask(graph, Name(TaskGraphTestUtils::s_ConcurrentReadStateReadPacketTailGraphics), use, GraphicsCommands());
    ASSERT_TRUE(graphicsReaderResult);
    const Core::GpuTaskId graphicsReader = *graphicsReaderResult;
    ASSERT_TRUE(firstReader.valid());
    ASSERT_TRUE(packetTail.valid());
    ASSERT_TRUE(computeReader.valid());
    ASSERT_TRUE(graphicsReader.valid());

    TwoQueueCompile compilation(testArena);
    ASSERT_TRUE(compilation.compile(graph));
    const Core::GpuCompiledGraph::ReadView plan(compilation.compiledGraph);
    ASSERT_TRUE(plan.tasksSharePacket(firstReader, packetTail));
    EXPECT_TRUE(HasStateSeed(plan, graphicsReader, computeReader));
    EXPECT_FALSE(HasStateSeed(plan, graphicsReader, firstReader));
    EXPECT_TRUE(HasPacketDependency(plan, graphicsReader, computeReader));
}


TEST(GpuTaskGraph, KeepsSingleLaneAlternatingReadStateScratchGrowthLinear){
    usize smallScratchPeak = 0u;
    usize largeScratchPeak = 0u;
    ASSERT_NO_FATAL_FAILURE(CheckSingleLaneStateChangeScratch(smallScratchPeak, 128u));
    ASSERT_NO_FATAL_FAILURE(CheckSingleLaneStateChangeScratch(largeScratchPeak, 512u));
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
    ASSERT_NO_FATAL_FAILURE(CheckSingleLaneStateChangeScratch(smallScratchPeak, 128u, true));
    ASSERT_NO_FATAL_FAILURE(CheckSingleLaneStateChangeScratch(largeScratchPeak, 512u, true));
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

