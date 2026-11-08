// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_external_final_state_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, ExportsExclusiveImportedResourceOwnershipToExternalQueue){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Array<Core::GpuPhysicalQueueInfo, 2> queues = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };

    const auto bufferResult = graph.importResource(
        Core::GpuGraphResourceDesc{}
            .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphExternalFinalReleaseBuffer))
            .setMarkerLabel("External Final Release Buffer")
            .setType(Core::GpuGraphResourceType::Buffer)
            .setInitialState(Core::ResourceStates::Common)
            .setExternalFinalState(Core::ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(queues[1u].id)
    );
    ASSERT_TRUE(bufferResult);
    const Core::GpuGraphResourceId buffer = *bufferResult;
    ASSERT_TRUE(buffer.valid());

    const Core::GpuTaskResourceUse use{ .resource = buffer, .range = Core::GpuTaskResourceRange{ .bufferRange = Core::BufferRange(32u, 64u) }, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write, };
    const auto taskResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphExternalFinalReleaseWriter), "External Final Release Writer", {}, Span(&use, 1u), {}, {}, {}, graph);
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    {
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        const Core::GpuCompiledTaskView compiledTaskView = compiledPlan.findTask(task);
        const Core::GpuCompiledTask* const compiledTask = compiledTaskView.plan;
        ASSERT_NE(compiledTask, nullptr);
        EXPECT_EQ(compiledTask->queue, queues[0u].id);
        const Core::GpuCompiledExternalResourceExportView exportView = compiledPlan.externalResourceExport(buffer);
        const Core::GpuCompiledExternalResourceExport* const exportInfo = exportView.plan;
        ASSERT_NE(exportInfo, nullptr);
        EXPECT_EQ(exportInfo->resource, buffer);
        EXPECT_EQ(exportInfo->producerTask, task);
        EXPECT_EQ(exportInfo->sourceQueue, queues[0u].id);
        EXPECT_EQ(exportInfo->destinationQueue, queues[1u].id);
        EXPECT_EQ(exportInfo->finalState, Core::ResourceStates::ShaderResource);

        ASSERT_EQ(compiledTask->epilogueBarrierCount, 2u);
        const Core::GpuCompiledBarrier* const barriers = compiledTaskView.epilogueBarriers;
        ASSERT_NE(barriers, nullptr);
        EXPECT_EQ(barriers[0u].type, Core::GpuCompiledBarrierType::BufferStateExport);
        EXPECT_EQ(barriers[0u].resource, buffer);
        EXPECT_EQ(barriers[0u].before, Core::ResourceStates::CopyDest);
        EXPECT_EQ(barriers[0u].after, Core::ResourceStates::ShaderResource);
        EXPECT_EQ(barriers[0u].range.bufferRange, use.range.bufferRange);
        EXPECT_EQ(barriers[0u].sourceQueue, queues[0u].id);
        EXPECT_EQ(barriers[0u].destinationQueue, queues[0u].id);
        EXPECT_EQ(barriers[1u].type, Core::GpuCompiledBarrierType::BufferOwnershipRelease);
        EXPECT_EQ(barriers[1u].resource, buffer);
        EXPECT_EQ(barriers[1u].before, Core::ResourceStates::ShaderResource);
        EXPECT_EQ(barriers[1u].after, Core::ResourceStates::ShaderResource);
        EXPECT_EQ(barriers[1u].range.bufferRange, use.range.bufferRange);
        EXPECT_EQ(barriers[1u].sourceQueue, queues[0u].id);
        EXPECT_EQ(barriers[1u].destinationQueue, queues[1u].id);

        const Core::GpuTaskGraphPhysicalQueueCompileStatistics sourceQueueCompileStatistics = compiledPlan.physicalQueueCompileStatistics(queues[0u].id);
        const Core::GpuTaskGraphPhysicalQueueCompileStatistics destinationQueueCompileStatistics = compiledPlan.physicalQueueCompileStatistics(queues[1u].id);
        ASSERT_TRUE(sourceQueueCompileStatistics.valid());
        ASSERT_TRUE(destinationQueueCompileStatistics.valid());
        EXPECT_EQ(sourceQueueCompileStatistics.ownershipReleaseBarrierCount, 1u);
        EXPECT_EQ(sourceQueueCompileStatistics.ownershipAcquireBarrierCount, 0u);
        EXPECT_EQ(destinationQueueCompileStatistics.ownershipReleaseBarrierCount, 0u);
        EXPECT_EQ(destinationQueueCompileStatistics.ownershipAcquireBarrierCount, 0u);

        ASSERT_EQ(compiledPlan.logicalOwnershipTransferCount(), 1u);
        const Core::GpuCompiledOwnershipTransfer* const ownershipTransfers = compiledPlan.logicalOwnershipTransfers();
        ASSERT_NE(ownershipTransfers, nullptr);
        EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), ownershipTransfers);
        EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(1u), nullptr);
        const Core::GpuCompiledOwnershipTransfer& ownershipTransfer = ownershipTransfers[0u];
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
        EXPECT_EQ(ownershipTransfer.declaredQueueSharing, Core::ResourceQueueSharing::Exclusive);
        EXPECT_EQ(ownershipTransfer.resourceType, Core::GpuGraphResourceType::Buffer);
        EXPECT_EQ(ownershipTransfer.route, Core::GpuOwnershipTransferRoute::ExternalExport);
        EXPECT_TRUE(ownershipTransfer.concurrentSharingCouldAvoid);

        const Core::GpuTaskGraphCompileStatistics ownershipStatistics = compiledPlan.compileStatistics();
        ASSERT_TRUE(ownershipStatistics.valid());
        EXPECT_EQ(ownershipStatistics.logicalOwnershipTransferCount, 1u);
        EXPECT_EQ(ownershipStatistics.logicalOwnershipTransferSignatureCount, 1u);
        EXPECT_EQ(ownershipStatistics.repeatedOwnershipTransferSignatureCount, 0u);
        EXPECT_EQ(ownershipStatistics.concurrentSharingCouldAvoidTransferCount, 1u);
        EXPECT_EQ(ownershipStatistics.concurrentSharingAdviceResourceCount, 0u);
        EXPECT_EQ(ownershipStatistics.logicalOwnershipTransferCountByRoute[Core::GpuOwnershipTransferRoute::ExternalExport], 1u);
        EXPECT_EQ(sourceQueueCompileStatistics.outgoingLogicalOwnershipTransferCount, 1u);
        EXPECT_EQ(sourceQueueCompileStatistics.incomingLogicalOwnershipTransferCount, 0u);
        EXPECT_EQ(destinationQueueCompileStatistics.outgoingLogicalOwnershipTransferCount, 0u);
        EXPECT_EQ(destinationQueueCompileStatistics.incomingLogicalOwnershipTransferCount, 1u);
    }

    Array<Core::GpuPhysicalQueueInfo, 2> sameFamilyQueues = { queues[0u], queues[1u] };
    sameFamilyQueues[1u].familyIndex = sameFamilyQueues[0u].familyIndex;
    sameFamilyQueues[1u].queueIndex = 1u;
    const Core::GpuPhysicalQueueTopology sameFamilyTopology{ .queues = sameFamilyQueues.data(), .queueCount = LengthOf(sameFamilyQueues), };
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, sameFamilyTopology));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    EXPECT_EQ(compiledPlan.logicalOwnershipTransferCount(), 0u);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransfers(), nullptr);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), nullptr);
    EXPECT_EQ(compiledPlan.compileStatistics().logicalOwnershipTransferCount, 0u);
}


TEST(GpuTaskGraph, ExportsExternalFinalOwnershipWithMultipleTerminalPackets){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Array<Core::GpuPhysicalQueueInfo, 2> queues = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    const auto textureResult = graph.importResource(
        Core::GpuGraphResourceDesc{}
            .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphExternalFinalMultiplePackets))
            .setMarkerLabel("External Final Multiple Packets")
            .setType(Core::GpuGraphResourceType::Texture)
            .setInitialState(Core::ResourceStates::Common)
            .setExternalFinalState(Core::ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(queues[1u].id)
    );
    ASSERT_TRUE(textureResult);
    const Core::GpuGraphResourceId texture = *textureResult;
    ASSERT_TRUE(texture.valid());

    const Core::GpuTaskResourceUse graphicsUse{
        .resource = texture,
        .range = Core::GpuTaskResourceRange{ .textureSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u), },
        .requiredState = Core::ResourceStates::CopyDest,
        .access = Core::GpuTaskResourceAccess::Write,
    };
    const Core::GpuTaskResourceUse computeUse{
        .resource = texture,
        .range = Core::GpuTaskResourceRange{ .textureSubresources = Core::TextureSubresourceSet(1u, 1u, 0u, 1u), },
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::Write,
    };
    const Core::GpuTaskCommandRequirements graphicsCommands = GraphicsCommands();
    const Core::GpuTaskCommandRequirements computeCommands = ComputeCommands();
    Core::GpuTaskDesc graphicsDesc;
    graphicsDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphExternalFinalMultipleGraphics)).setMarkerLabel("External Final Multiple Graphics").setResourceUses(Span(&graphicsUse, 1u));
    Core::GpuTaskDesc computeDesc;
    computeDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphExternalFinalMultipleCompute)).setMarkerLabel("External Final Multiple Compute").setResourceUses(Span(&computeUse, 1u));
    const auto graphicsTaskResult = graph.addTask(graphicsDesc, graphicsCommands);
    ASSERT_TRUE(graphicsTaskResult);
    const Core::GpuTaskId graphicsTask = *graphicsTaskResult;
    const auto computeTaskResult = graph.addTask(computeDesc, computeCommands);
    ASSERT_TRUE(computeTaskResult);
    const Core::GpuTaskId computeTask = *computeTaskResult;
    ASSERT_TRUE(graphicsTask.valid());
    ASSERT_TRUE(computeTask.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    const Core::GpuCompiledExternalResourceExportView exportView = compiledPlan.externalResourceExport(texture);
    const Core::GpuCompiledExternalResourceExport* const exportInfo = exportView.plan;
    ASSERT_NE(exportInfo, nullptr);
    EXPECT_EQ(exportInfo->resource, texture);
    EXPECT_EQ(exportInfo->destinationQueue, queues[1u].id);
    EXPECT_EQ(exportInfo->finalState, Core::ResourceStates::ShaderResource);
    // No singular semantic producer exists: each mip's terminal range belongs to a different packet and queue.
    EXPECT_FALSE(exportInfo->producerTask.valid());
    EXPECT_FALSE(exportInfo->sourceQueue.valid());
    ASSERT_EQ(exportInfo->sourceCount, 2u);
    const Core::GpuCompiledExternalResourceExportSource* const sources = exportView.sources;
    ASSERT_NE(sources, nullptr);
    EXPECT_EQ(sources[0u].producerTask, graphicsTask);
    EXPECT_EQ(sources[0u].sourceQueue, queues[0u].id);
    EXPECT_EQ(sources[0u].range.textureSubresources, graphicsUse.range.textureSubresources);
    EXPECT_EQ(sources[1u].producerTask, computeTask);
    EXPECT_EQ(sources[1u].sourceQueue, queues[1u].id);
    EXPECT_EQ(sources[1u].range.textureSubresources, computeUse.range.textureSubresources);

    const Core::GpuCompiledTask* const compiledGraphics = compiledPlan.findTask(graphicsTask).plan;
    const Core::GpuCompiledTask* const compiledCompute = compiledPlan.findTask(computeTask).plan;
    ASSERT_NE(compiledGraphics, nullptr);
    ASSERT_NE(compiledCompute, nullptr);
    EXPECT_EQ(compiledGraphics->queue, queues[0u].id);
    EXPECT_EQ(compiledCompute->queue, queues[1u].id);
    ASSERT_EQ(compiledGraphics->epilogueBarrierCount, 2u);
    ASSERT_EQ(compiledCompute->epilogueBarrierCount, 1u);
    const Core::GpuCompiledBarrier* const graphicsBarriers = compiledPlan.findTask(graphicsTask).epilogueBarriers;
    const Core::GpuCompiledBarrier* const computeBarriers = compiledPlan.findTask(computeTask).epilogueBarriers;
    ASSERT_NE(graphicsBarriers, nullptr);
    ASSERT_NE(computeBarriers, nullptr);
    EXPECT_EQ(graphicsBarriers[0u].type, Core::GpuCompiledBarrierType::TextureStateExport);
    EXPECT_EQ(graphicsBarriers[1u].type, Core::GpuCompiledBarrierType::TextureOwnershipRelease);
    EXPECT_EQ(graphicsBarriers[1u].destinationQueue, queues[1u].id);
    EXPECT_EQ(computeBarriers[0u].type, Core::GpuCompiledBarrierType::TextureStateExport);

    ASSERT_EQ(compiledPlan.logicalOwnershipTransferCount(), 1u);
    const Core::GpuCompiledOwnershipTransfer* const ownershipTransfers = compiledPlan.logicalOwnershipTransfers();
    ASSERT_NE(ownershipTransfers, nullptr);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), ownershipTransfers);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(1u), nullptr);
    const Core::GpuCompiledOwnershipTransfer& ownershipTransfer = ownershipTransfers[0u];
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
    EXPECT_EQ(ownershipTransfer.declaredQueueSharing, Core::ResourceQueueSharing::Exclusive);
    EXPECT_EQ(ownershipTransfer.resourceType, Core::GpuGraphResourceType::Texture);
    EXPECT_EQ(ownershipTransfer.route, Core::GpuOwnershipTransferRoute::ExternalExport);
    EXPECT_TRUE(ownershipTransfer.concurrentSharingCouldAvoid);
    const Core::GpuTaskGraphCompileStatistics& statistics = compiledPlan.compileStatistics();
    EXPECT_EQ(statistics.logicalOwnershipTransferCount, 1u);
}

TEST(GpuTaskGraph, OrdersExternalFinalTransitionAfterOverlappingConcurrentReaders){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::GpuTaskResourceRange range{ .textureSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u), };
    const auto pairResult = AddExternalFinalReadPair(
        graph,
        Core::GpuGraphResourceType::Texture,
        range,
        range,
        Core::ResourceStates::ShaderResource,
        Core::ResourceStates::CopySource,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        false,
        {});
    ASSERT_TRUE(pairResult);
    const ExternalFinalReadPair pair = *pairResult;
    ASSERT_TRUE(pair.resource.valid());
    ASSERT_TRUE(pair.earlierReader.valid());
    ASSERT_TRUE(pair.finalizingReader.valid());

    TwoQueueCompile twoQueueCompile(testArena);
    ASSERT_TRUE(twoQueueCompile.compile(graph));
    const Core::GpuCompiledGraph::ReadView compiledPlan(twoQueueCompile.compiledGraph);
    Core::GpuTaskGraphAnalysis& analysis = twoQueueCompile.analysis;

    EXPECT_EQ(FindEdge(analysis, pair.earlierReader, pair.finalizingReader), nullptr);

    const Core::GpuSubmissionPacketId earlierPacket = compiledPlan.packetForTask(pair.earlierReader);
    const Core::GpuSubmissionPacketId finalizingPacket = compiledPlan.packetForTask(pair.finalizingReader);
    ASSERT_TRUE(earlierPacket.valid());
    ASSERT_TRUE(finalizingPacket.valid());
    ASSERT_NE(earlierPacket, finalizingPacket);
    const Core::GpuCompiledTask* const finalizingTask = compiledPlan.findTask(pair.finalizingReader).plan;
    ASSERT_NE(finalizingTask, nullptr);
    EXPECT_EQ(finalizingTask->prologueStateSeedCount, 0u);

    const Core::GpuSubmissionPacket& finalizingPacketInfo = *compiledPlan.packet(finalizingPacket).plan;
    ASSERT_EQ(finalizingPacketInfo.dependencyCount, 1u);
    const Core::GpuPacketDependency* const dependencies = compiledPlan.packet(finalizingPacket).dependencies;
    ASSERT_NE(dependencies, nullptr);
    EXPECT_EQ(dependencies[0u].producer, earlierPacket);
    EXPECT_EQ(dependencies[0u].consumer, finalizingPacket);

    ASSERT_EQ(finalizingTask->epilogueBarrierCount, 1u);
    const Core::GpuCompiledBarrier* const barriers = compiledPlan.findTask(pair.finalizingReader).epilogueBarriers;
    ASSERT_NE(barriers, nullptr);
    EXPECT_EQ(barriers[0u].type, Core::GpuCompiledBarrierType::TextureStateExport);
    EXPECT_EQ(barriers[0u].before, Core::ResourceStates::ShaderResource);
    EXPECT_EQ(barriers[0u].after, Core::ResourceStates::CopySource);
}

TEST(GpuTaskGraph, RejectsUnpublishableExternalFinalStateContracts){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);

    Core::GpuGraphResourceDesc unsupportedDesc;
    unsupportedDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphExternalFinalHazardDomain)).setMarkerLabel("External Final Hazard Domain").setType(Core::GpuGraphResourceType::HazardDomain).setExternalFinalState(Core::ResourceStates::ShaderResource);
    EXPECT_FALSE(graph.importResource(unsupportedDesc).has_value());

    Core::GpuGraphResourceDesc destinationWithoutFinalState;
    destinationWithoutFinalState
        .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphExternalFinalReleaseWithoutState))
        .setMarkerLabel("External Final Release Without State")
        .setType(Core::GpuGraphResourceType::Buffer)
        .setInitialState(Core::ResourceStates::Common)
        .setExternalFinalReleaseDestinationQueue(DedicatedComputeQueue().id)
    ;
    EXPECT_FALSE(graph.importResource(destinationWithoutFinalState).has_value());

    Core::GpuGraphResourceDesc untouchedDesc;
    untouchedDesc
        .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphExternalFinalUntouched))
        .setMarkerLabel("External Final Untouched")
        .setType(Core::GpuGraphResourceType::Buffer)
        .setInitialState(Core::ResourceStates::Common)
        .setExternalFinalState(Core::ResourceStates::ShaderResource)
    ;
    {
        const auto gpuTaskAdmission = graph.importResource(untouchedDesc);
        EXPECT_TRUE(gpuTaskAdmission);
        if(gpuTaskAdmission)
            EXPECT_TRUE(gpuTaskAdmission->valid());
    }
    {
        const auto secondGpuTaskAdmission = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphExternalFinalUnrelated), "External Final Unrelated", {}, {}, {}, {}, {}, graph);
        ASSERT_TRUE(secondGpuTaskAdmission);
        ASSERT_TRUE(secondGpuTaskAdmission->valid());
    }

    const Core::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    EXPECT_FALSE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    EXPECT_FALSE(compiledPlan.valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

