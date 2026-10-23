// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_external_final_state_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, ExportsExclusiveImportedResourceOwnershipToExternalQueue){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues), };

    const auto bufferResult = graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/external_final_release_buffer"))
            .setMarkerLabel("External Final Release Buffer")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::Common)
            .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(queues[1u].id)
    );
    ASSERT_TRUE(bufferResult);
    const Graphics::GpuGraphResourceId buffer = *bufferResult;
    ASSERT_TRUE(buffer.valid());

    const Graphics::GpuTaskResourceUse use{ .resource = buffer, .range = Graphics::GpuTaskResourceRange{ .bufferRange = Graphics::BufferRange(32u, 64u) }, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write, };
    const auto taskResult = AddTask(graph, Name("tests/task_graph/external_final_release_writer"), "External Final Release Writer", nullptr, 0u, &use, 1u);
    ASSERT_TRUE(taskResult);
    const Graphics::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        const Graphics::GpuCompiledTaskView compiledTaskView = compiledPlan.findTask(task);
        const Graphics::GpuCompiledTask* const compiledTask = compiledTaskView.plan;
        ASSERT_NE(compiledTask, nullptr);
        EXPECT_EQ(compiledTask->queue, queues[0u].id);
        const Graphics::GpuCompiledExternalResourceExportView exportView = compiledPlan.externalResourceExport(buffer);
        const Graphics::GpuCompiledExternalResourceExport* const exportInfo = exportView.plan;
        ASSERT_NE(exportInfo, nullptr);
        EXPECT_EQ(exportInfo->resource, buffer);
        EXPECT_EQ(exportInfo->producerTask, task);
        EXPECT_EQ(exportInfo->sourceQueue, queues[0u].id);
        EXPECT_EQ(exportInfo->destinationQueue, queues[1u].id);
        EXPECT_EQ(exportInfo->finalState, Graphics::ResourceStates::ShaderResource);

        ASSERT_EQ(compiledTask->epilogueBarrierCount, s_ExpectedDualCount);
        const Graphics::GpuCompiledBarrier* const barriers = compiledTaskView.epilogueBarriers;
        ASSERT_NE(barriers, nullptr);
        EXPECT_EQ(barriers[0u].type, Graphics::GpuCompiledBarrierType::BufferStateExport);
        EXPECT_EQ(barriers[0u].resource, buffer);
        EXPECT_EQ(barriers[0u].before, Graphics::ResourceStates::CopyDest);
        EXPECT_EQ(barriers[0u].after, Graphics::ResourceStates::ShaderResource);
        EXPECT_EQ(barriers[0u].range.bufferRange, use.range.bufferRange);
        EXPECT_EQ(barriers[0u].sourceQueue, queues[0u].id);
        EXPECT_EQ(barriers[0u].destinationQueue, queues[0u].id);
        EXPECT_EQ(barriers[1u].type, Graphics::GpuCompiledBarrierType::BufferOwnershipRelease);
        EXPECT_EQ(barriers[1u].resource, buffer);
        EXPECT_EQ(barriers[1u].before, Graphics::ResourceStates::ShaderResource);
        EXPECT_EQ(barriers[1u].after, Graphics::ResourceStates::ShaderResource);
        EXPECT_EQ(barriers[1u].range.bufferRange, use.range.bufferRange);
        EXPECT_EQ(barriers[1u].sourceQueue, queues[0u].id);
        EXPECT_EQ(barriers[1u].destinationQueue, queues[1u].id);

        const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics sourceQueueCompileStatistics = compiledPlan.physicalQueueCompileStatistics(queues[0u].id);
        const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics destinationQueueCompileStatistics = compiledPlan.physicalQueueCompileStatistics(queues[1u].id);
        ASSERT_TRUE(sourceQueueCompileStatistics.valid());
        ASSERT_TRUE(destinationQueueCompileStatistics.valid());
        EXPECT_EQ(sourceQueueCompileStatistics.ownershipReleaseBarrierCount, 1u);
        EXPECT_EQ(sourceQueueCompileStatistics.ownershipAcquireBarrierCount, 0u);
        EXPECT_EQ(destinationQueueCompileStatistics.ownershipReleaseBarrierCount, 0u);
        EXPECT_EQ(destinationQueueCompileStatistics.ownershipAcquireBarrierCount, 0u);

        ASSERT_EQ(compiledPlan.logicalOwnershipTransferCount(), 1u);
        const Graphics::GpuCompiledOwnershipTransfer* const ownershipTransfers = compiledPlan.logicalOwnershipTransfers();
        ASSERT_NE(ownershipTransfers, nullptr);
        EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), ownershipTransfers);
        EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(1u), nullptr);
        const Graphics::GpuCompiledOwnershipTransfer& ownershipTransfer = ownershipTransfers[0u];
        EXPECT_TRUE(ownershipTransfer.valid());
        EXPECT_EQ(ownershipTransfer.resource, buffer);
        EXPECT_EQ(ownershipTransfer.resourceIdentity, Name("tests/task_graph/external_final_release_buffer"));
        EXPECT_EQ(ownershipTransfer.range.bufferRange, use.range.bufferRange);
        EXPECT_EQ(ownershipTransfer.sourceTask, task);
        EXPECT_FALSE(ownershipTransfer.destinationTask.valid());
        EXPECT_EQ(ownershipTransfer.sourcePacket, compiledTask->packet);
        EXPECT_FALSE(ownershipTransfer.destinationPacket.valid());
        EXPECT_EQ(ownershipTransfer.sourceQueue, queues[0u].id);
        EXPECT_EQ(ownershipTransfer.destinationQueue, queues[1u].id);
        EXPECT_EQ(ownershipTransfer.sourceQueueFamilyIndex, queues[0u].familyIndex);
        EXPECT_EQ(ownershipTransfer.destinationQueueFamilyIndex, queues[1u].familyIndex);
        EXPECT_EQ(ownershipTransfer.declaredQueueSharing, Graphics::ResourceQueueSharing::Exclusive);
        EXPECT_EQ(ownershipTransfer.resourceType, Graphics::GpuGraphResourceType::Buffer);
        EXPECT_EQ(ownershipTransfer.route, Graphics::GpuOwnershipTransferRoute::ExternalExport);
        EXPECT_TRUE(ownershipTransfer.concurrentSharingCouldAvoid);

        const Graphics::GpuTaskGraphCompileStatistics ownershipStatistics = compiledPlan.compileStatistics();
        ASSERT_TRUE(ownershipStatistics.valid());
        EXPECT_EQ(ownershipStatistics.logicalOwnershipTransferCount, 1u);
        EXPECT_EQ(ownershipStatistics.logicalOwnershipTransferSignatureCount, 1u);
        EXPECT_EQ(ownershipStatistics.repeatedOwnershipTransferSignatureCount, 0u);
        EXPECT_EQ(ownershipStatistics.concurrentSharingCouldAvoidTransferCount, 1u);
        EXPECT_EQ(ownershipStatistics.concurrentSharingAdviceResourceCount, 0u);
        EXPECT_EQ(ownershipStatistics.logicalOwnershipTransferCountByRoute[Graphics::GpuOwnershipTransferRoute::ExternalExport], 1u);
        EXPECT_EQ(sourceQueueCompileStatistics.outgoingLogicalOwnershipTransferCount, 1u);
        EXPECT_EQ(sourceQueueCompileStatistics.incomingLogicalOwnershipTransferCount, 0u);
        EXPECT_EQ(destinationQueueCompileStatistics.outgoingLogicalOwnershipTransferCount, 0u);
        EXPECT_EQ(destinationQueueCompileStatistics.incomingLogicalOwnershipTransferCount, 1u);
    }

    Graphics::GpuPhysicalQueueInfo sameFamilyQueues[] = { queues[0u], queues[1u] };
    sameFamilyQueues[1u].familyIndex = sameFamilyQueues[0u].familyIndex;
    sameFamilyQueues[1u].queueIndex = 1u;
    const Graphics::GpuPhysicalQueueTopology sameFamilyTopology{ .queues = sameFamilyQueues, .queueCount = LengthOf(sameFamilyQueues), };
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, sameFamilyTopology));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    EXPECT_EQ(compiledPlan.logicalOwnershipTransferCount(), 0u);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransfers(), nullptr);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), nullptr);
    EXPECT_EQ(compiledPlan.compileStatistics().logicalOwnershipTransferCount, 0u);
}


TEST(GpuTaskGraph, ExportsExternalFinalOwnershipWithMultipleTerminalPackets){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues), };
    const auto textureResult = graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/external_final_multiple_packets"))
            .setMarkerLabel("External Final Multiple Packets")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::Common)
            .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(queues[1u].id)
    );
    ASSERT_TRUE(textureResult);
    const Graphics::GpuGraphResourceId texture = *textureResult;
    ASSERT_TRUE(texture.valid());

    const Graphics::GpuTaskResourceUse graphicsUse{
        .resource = texture,
        .range = Graphics::GpuTaskResourceRange{ .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u), },
        .requiredState = Graphics::ResourceStates::CopyDest,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskResourceUse computeUse{
        .resource = texture,
        .range = Graphics::GpuTaskResourceRange{ .textureSubresources = Graphics::TextureSubresourceSet(1u, 1u, 0u, 1u), },
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskCommandRequirements graphicsCommands = GraphicsCommands();
    const Graphics::GpuTaskCommandRequirements computeCommands = ComputeCommands();
    Graphics::GpuTaskDesc graphicsDesc;
    graphicsDesc.setIdentity(Name("tests/task_graph/external_final_multiple_graphics")).setMarkerLabel("External Final Multiple Graphics").setResourceUses(&graphicsUse, 1u);
    Graphics::GpuTaskDesc computeDesc;
    computeDesc.setIdentity(Name("tests/task_graph/external_final_multiple_compute")).setMarkerLabel("External Final Multiple Compute").setResourceUses(&computeUse, 1u);
    const auto graphicsTaskResult = graph.addTask(graphicsDesc, graphicsCommands);
    ASSERT_TRUE(graphicsTaskResult);
    const Graphics::GpuTaskId graphicsTask = *graphicsTaskResult;
    const auto computeTaskResult = graph.addTask(computeDesc, computeCommands);
    ASSERT_TRUE(computeTaskResult);
    const Graphics::GpuTaskId computeTask = *computeTaskResult;
    ASSERT_TRUE(graphicsTask.valid());
    ASSERT_TRUE(computeTask.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    const Graphics::GpuCompiledExternalResourceExportView exportView = compiledPlan.externalResourceExport(texture);
    const Graphics::GpuCompiledExternalResourceExport* const exportInfo = exportView.plan;
    ASSERT_NE(exportInfo, nullptr);
    EXPECT_EQ(exportInfo->resource, texture);
    EXPECT_EQ(exportInfo->destinationQueue, queues[1u].id);
    EXPECT_EQ(exportInfo->finalState, Graphics::ResourceStates::ShaderResource);
    // No singular semantic producer exists: each mip's terminal range belongs to a different packet and queue.
    EXPECT_FALSE(exportInfo->producerTask.valid());
    EXPECT_FALSE(exportInfo->sourceQueue.valid());
    ASSERT_EQ(exportInfo->sourceCount, s_ExpectedDualCount);
    const Graphics::GpuCompiledExternalResourceExportSource* const sources = exportView.sources;
    ASSERT_NE(sources, nullptr);
    EXPECT_EQ(sources[0u].producerTask, graphicsTask);
    EXPECT_EQ(sources[0u].sourceQueue, queues[0u].id);
    EXPECT_EQ(sources[0u].range.textureSubresources, graphicsUse.range.textureSubresources);
    EXPECT_EQ(sources[1u].producerTask, computeTask);
    EXPECT_EQ(sources[1u].sourceQueue, queues[1u].id);
    EXPECT_EQ(sources[1u].range.textureSubresources, computeUse.range.textureSubresources);

    const Graphics::GpuCompiledTask* const compiledGraphics = compiledPlan.findTask(graphicsTask).plan;
    const Graphics::GpuCompiledTask* const compiledCompute = compiledPlan.findTask(computeTask).plan;
    ASSERT_NE(compiledGraphics, nullptr);
    ASSERT_NE(compiledCompute, nullptr);
    EXPECT_EQ(compiledGraphics->queue, queues[0u].id);
    EXPECT_EQ(compiledCompute->queue, queues[1u].id);
    ASSERT_EQ(compiledGraphics->epilogueBarrierCount, s_ExpectedDualCount);
    ASSERT_EQ(compiledCompute->epilogueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const graphicsBarriers = compiledPlan.findTask(graphicsTask).epilogueBarriers;
    const Graphics::GpuCompiledBarrier* const computeBarriers = compiledPlan.findTask(computeTask).epilogueBarriers;
    ASSERT_NE(graphicsBarriers, nullptr);
    ASSERT_NE(computeBarriers, nullptr);
    EXPECT_EQ(graphicsBarriers[0u].type, Graphics::GpuCompiledBarrierType::TextureStateExport);
    EXPECT_EQ(graphicsBarriers[1u].type, Graphics::GpuCompiledBarrierType::TextureOwnershipRelease);
    EXPECT_EQ(graphicsBarriers[1u].destinationQueue, queues[1u].id);
    EXPECT_EQ(computeBarriers[0u].type, Graphics::GpuCompiledBarrierType::TextureStateExport);

    ASSERT_EQ(compiledPlan.logicalOwnershipTransferCount(), 1u);
    const Graphics::GpuCompiledOwnershipTransfer* const ownershipTransfers = compiledPlan.logicalOwnershipTransfers();
    ASSERT_NE(ownershipTransfers, nullptr);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), ownershipTransfers);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(1u), nullptr);
    const Graphics::GpuCompiledOwnershipTransfer& ownershipTransfer = ownershipTransfers[0u];
    EXPECT_TRUE(ownershipTransfer.valid());
    EXPECT_EQ(ownershipTransfer.resource, texture);
    EXPECT_EQ(ownershipTransfer.resourceIdentity, Name("tests/task_graph/external_final_multiple_packets"));
    EXPECT_EQ(ownershipTransfer.range.textureSubresources, graphicsUse.range.textureSubresources);
    EXPECT_NE(ownershipTransfer.range.textureSubresources, computeUse.range.textureSubresources);
    EXPECT_EQ(ownershipTransfer.sourceTask, graphicsTask);
    EXPECT_FALSE(ownershipTransfer.destinationTask.valid());
    EXPECT_EQ(ownershipTransfer.sourcePacket, compiledGraphics->packet);
    EXPECT_FALSE(ownershipTransfer.destinationPacket.valid());
    EXPECT_EQ(ownershipTransfer.sourceQueue, queues[0u].id);
    EXPECT_EQ(ownershipTransfer.destinationQueue, queues[1u].id);
    EXPECT_EQ(ownershipTransfer.sourceQueueFamilyIndex, queues[0u].familyIndex);
    EXPECT_EQ(ownershipTransfer.destinationQueueFamilyIndex, queues[1u].familyIndex);
    EXPECT_EQ(ownershipTransfer.declaredQueueSharing, Graphics::ResourceQueueSharing::Exclusive);
    EXPECT_EQ(ownershipTransfer.resourceType, Graphics::GpuGraphResourceType::Texture);
    EXPECT_EQ(ownershipTransfer.route, Graphics::GpuOwnershipTransferRoute::ExternalExport);
    EXPECT_TRUE(ownershipTransfer.concurrentSharingCouldAvoid);
    const Graphics::GpuTaskGraphCompileStatistics& statistics = compiledPlan.compileStatistics();
    EXPECT_EQ(statistics.logicalOwnershipTransferCount, 1u);
}

TEST(GpuTaskGraph, OrdersExternalFinalTransitionAfterOverlappingConcurrentReaders){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskResourceRange range{ .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u), };
    const auto pairResult = AddExternalFinalReadPair(
        graph,
        Graphics::GpuGraphResourceType::Texture,
        range,
        range,
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceStates::CopySource,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        false,
        {}
    );
    ASSERT_TRUE(pairResult);
    const ExternalFinalReadPair pair = *pairResult;
    ASSERT_TRUE(pair.resource.valid());
    ASSERT_TRUE(pair.earlierReader.valid());
    ASSERT_TRUE(pair.finalizingReader.valid());

    TwoQueueCompile twoQueueCompile(testArena);
    ASSERT_TRUE(twoQueueCompile.compile(graph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(twoQueueCompile.compiledGraph);
    Graphics::GpuTaskGraphAnalysis& analysis = twoQueueCompile.analysis;

    EXPECT_EQ(FindEdge(analysis, pair.earlierReader, pair.finalizingReader), nullptr);

    const Graphics::GpuSubmissionPacketId earlierPacket = compiledPlan.packetForTask(pair.earlierReader);
    const Graphics::GpuSubmissionPacketId finalizingPacket = compiledPlan.packetForTask(pair.finalizingReader);
    ASSERT_TRUE(earlierPacket.valid());
    ASSERT_TRUE(finalizingPacket.valid());
    ASSERT_NE(earlierPacket, finalizingPacket);
    const Graphics::GpuCompiledTask* const finalizingTask = compiledPlan.findTask(pair.finalizingReader).plan;
    ASSERT_NE(finalizingTask, nullptr);
    EXPECT_EQ(finalizingTask->prologueStateSeedCount, 0u);

    const Graphics::GpuSubmissionPacket& finalizingPacketInfo = *compiledPlan.packet(finalizingPacket).plan;
    ASSERT_EQ(finalizingPacketInfo.dependencyCount, 1u);
    const Graphics::GpuPacketDependency* const dependencies = compiledPlan.packet(finalizingPacket).dependencies;
    ASSERT_NE(dependencies, nullptr);
    EXPECT_EQ(dependencies[0u].producer, earlierPacket);
    EXPECT_EQ(dependencies[0u].consumer, finalizingPacket);

    ASSERT_EQ(finalizingTask->epilogueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(pair.finalizingReader).epilogueBarriers;
    ASSERT_NE(barriers, nullptr);
    EXPECT_EQ(barriers[0u].type, Graphics::GpuCompiledBarrierType::TextureStateExport);
    EXPECT_EQ(barriers[0u].before, Graphics::ResourceStates::ShaderResource);
    EXPECT_EQ(barriers[0u].after, Graphics::ResourceStates::CopySource);
}

TEST(GpuTaskGraph, RejectsUnpublishableExternalFinalStateContracts){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuGraphResourceDesc unsupportedDesc;
    unsupportedDesc.setIdentity(Name("tests/task_graph/external_final_hazard_domain")).setMarkerLabel("External Final Hazard Domain").setType(Graphics::GpuGraphResourceType::HazardDomain).setExternalFinalState(Graphics::ResourceStates::ShaderResource);
    EXPECT_FALSE(graph.importResource(unsupportedDesc).has_value());

    Graphics::GpuGraphResourceDesc destinationWithoutFinalState;
    destinationWithoutFinalState
        .setIdentity(Name("tests/task_graph/external_final_release_without_state"))
        .setMarkerLabel("External Final Release Without State")
        .setType(Graphics::GpuGraphResourceType::Buffer)
        .setInitialState(Graphics::ResourceStates::Common)
        .setExternalFinalReleaseDestinationQueue(DedicatedComputeQueue().id)
    ;
    EXPECT_FALSE(graph.importResource(destinationWithoutFinalState).has_value());

    Graphics::GpuGraphResourceDesc untouchedDesc;
    untouchedDesc
        .setIdentity(Name("tests/task_graph/external_final_untouched"))
        .setMarkerLabel("External Final Untouched")
        .setType(Graphics::GpuGraphResourceType::Buffer)
        .setInitialState(Graphics::ResourceStates::Common)
        .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
    ;
    {
        const auto gpuTaskAdmission = graph.importResource(untouchedDesc);
        EXPECT_TRUE(gpuTaskAdmission);
        if(gpuTaskAdmission)
            EXPECT_TRUE(gpuTaskAdmission->valid());
    }
    {
        const auto secondGpuTaskAdmission = AddTask(graph, Name("tests/task_graph/external_final_unrelated"), "External Final Unrelated");
        ASSERT_TRUE(secondGpuTaskAdmission);
        ASSERT_TRUE(secondGpuTaskAdmission->valid());
    }

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    EXPECT_FALSE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    EXPECT_FALSE(compiledPlan.valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

