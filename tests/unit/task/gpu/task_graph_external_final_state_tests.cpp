// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_external_final_state_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, ExportsRequiredImportedResourceFinalStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuGraphResourceDesc textureDesc;
    textureDesc
        .setIdentity(Name("tests/task_graph/external_final_texture"))
        .setMarkerLabel("External Final Texture")
        .setType(Graphics::GpuGraphResourceType::Texture)
        .setInitialState(Graphics::ResourceStates::Common)
        .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
    ;
    const Graphics::GpuGraphResourceId texture = graph.importResource(textureDesc);
    ASSERT_TRUE(texture.valid());

    Graphics::GpuGraphResourceDesc bufferDesc;
    bufferDesc
        .setIdentity(Name("tests/task_graph/external_final_buffer"))
        .setMarkerLabel("External Final Buffer")
        .setType(Graphics::GpuGraphResourceType::Buffer)
        .setInitialState(Graphics::ResourceStates::Common)
        .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
    ;
    const Graphics::GpuGraphResourceId buffer = graph.importResource(bufferDesc);
    ASSERT_TRUE(buffer.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.resourceAt(texture.index).externalFinalState, Graphics::ResourceStates::ShaderResource);
        EXPECT_EQ(declarations.resourceAt(buffer.index).externalFinalState, Graphics::ResourceStates::ShaderResource);
    }

    const Graphics::GpuTaskResourceUse uses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = buffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId task = AddTask(
        graph,
        Name("tests/task_graph/external_final_writer"),
        "External Final Writer",
        nullptr,
        0u,
        uses,
        LengthOf(uses)
    );
    ASSERT_TRUE(task.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
    ASSERT_NE(compiledTask, nullptr);
    ASSERT_EQ(compiledTask->epilogueBarrierCount, 2u);
    const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).epilogueBarriers;
    ASSERT_NE(barriers, nullptr);
    bool exportedTexture = false;
    bool exportedBuffer = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledTask->epilogueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
        exportedTexture = exportedTexture || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureStateExport
            && barrier.resource == texture
            && barrier.before == Graphics::ResourceStates::CopyDest
            && barrier.after == Graphics::ResourceStates::ShaderResource
            && barrier.sourceQueue == compiledTask->queue
            && barrier.destinationQueue == compiledTask->queue
        );
        exportedBuffer = exportedBuffer || (
            barrier.type == Graphics::GpuCompiledBarrierType::BufferStateExport
            && barrier.resource == buffer
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
            && barrier.sourceQueue == compiledTask->queue
            && barrier.destinationQueue == compiledTask->queue
        );
    }
    EXPECT_TRUE(exportedTexture);
    EXPECT_TRUE(exportedBuffer);
}

TEST(GpuTaskGraph, ExportsExclusiveImportedResourceOwnershipToExternalQueue){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };

    const Graphics::GpuGraphResourceId buffer = graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/external_final_release_buffer"))
            .setMarkerLabel("External Final Release Buffer")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::Common)
            .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(queues[1u].id)
    );
    ASSERT_TRUE(buffer.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.resourceAt(buffer.index).externalFinalReleaseDestinationQueue, queues[1u].id);
    }

    const Graphics::GpuTaskResourceUse use{
        .resource = buffer,
        .range = Graphics::GpuTaskResourceRange{ .bufferRange = Graphics::BufferRange(32u, 64u) },
        .requiredState = Graphics::ResourceStates::CopyDest,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskId task = AddTask(
        graph,
        Name("tests/task_graph/external_final_release_writer"),
        "External Final Release Writer",
        nullptr,
        0u,
        &use,
        1u
    );
    ASSERT_TRUE(task.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
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

        ASSERT_EQ(compiledTask->epilogueBarrierCount, 2u);
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

        const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics sourceQueueCompileStatistics =
            compiledPlan.physicalQueueCompileStatistics(queues[0u].id)
        ;
        const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics destinationQueueCompileStatistics =
            compiledPlan.physicalQueueCompileStatistics(queues[1u].id)
        ;
        ASSERT_TRUE(sourceQueueCompileStatistics.valid());
        ASSERT_TRUE(destinationQueueCompileStatistics.valid());
        EXPECT_EQ(sourceQueueCompileStatistics.ownershipReleaseBarrierCount, 1u);
        EXPECT_EQ(sourceQueueCompileStatistics.ownershipAcquireBarrierCount, 0u);
        EXPECT_EQ(destinationQueueCompileStatistics.ownershipReleaseBarrierCount, 0u);
        EXPECT_EQ(destinationQueueCompileStatistics.ownershipAcquireBarrierCount, 0u);

        ASSERT_EQ(compiledPlan.logicalOwnershipTransferCount(), 1u);
        const Graphics::GpuCompiledOwnershipTransfer* const ownershipTransfers =
            compiledPlan.logicalOwnershipTransfers()
        ;
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
        EXPECT_EQ(
            ownershipStatistics.logicalOwnershipTransferCountByRoute[
                Graphics::GpuOwnershipTransferRoute::ExternalExport
            ],
            1u
        );
        EXPECT_EQ(sourceQueueCompileStatistics.outgoingLogicalOwnershipTransferCount, 1u);
        EXPECT_EQ(sourceQueueCompileStatistics.incomingLogicalOwnershipTransferCount, 0u);
        EXPECT_EQ(destinationQueueCompileStatistics.outgoingLogicalOwnershipTransferCount, 0u);
        EXPECT_EQ(destinationQueueCompileStatistics.incomingLogicalOwnershipTransferCount, 1u);
    }

    Graphics::GpuPhysicalQueueInfo sameFamilyQueues[] = { queues[0u], queues[1u] };
    sameFamilyQueues[1u].familyIndex = sameFamilyQueues[0u].familyIndex;
    sameFamilyQueues[1u].queueIndex = 1u;
    const Graphics::GpuTaskGraphQueueTopology sameFamilyTopology{
        .queues = sameFamilyQueues,
        .queueCount = LengthOf(sameFamilyQueues),
    };
    ASSERT_TRUE(Compile(graph, analysis, sameFamilyTopology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    EXPECT_EQ(compiledPlan.logicalOwnershipTransferCount(), 0u);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransfers(), nullptr);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), nullptr);
    EXPECT_EQ(compiledPlan.compileStatistics().logicalOwnershipTransferCount, 0u);
}

TEST(GpuTaskGraph, ExportsExclusiveAccelStructOwnershipToExternalQueue){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };

    const Graphics::GpuGraphResourceId accelStruct = graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/external_final_release_accel_struct"))
            .setMarkerLabel("External Final Release Accel Struct")
            .setType(Graphics::GpuGraphResourceType::AccelStruct)
            .setInitialState(Graphics::ResourceStates::Common)
            .setExternalFinalState(Graphics::ResourceStates::AccelStructRead)
            .setExternalFinalReleaseDestinationQueue(queues[1u].id)
    );
    ASSERT_TRUE(accelStruct.valid());

    const Graphics::GpuTaskResourceUse use{
        .resource = accelStruct,
        .range = {},
        .requiredState = Graphics::ResourceStates::AccelStructWrite,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskId task = AddTask(
        graph,
        Name("tests/task_graph/external_final_release_accel_struct_writer"),
        "External Final Release Accel Struct Writer",
        nullptr,
        0u,
        &use,
        1u
    );
    ASSERT_TRUE(task.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
    ASSERT_NE(compiledTask, nullptr);
    const Graphics::GpuCompiledExternalResourceExport* const exportInfo = compiledPlan.externalResourceExport(accelStruct).plan;
    ASSERT_NE(exportInfo, nullptr);
    EXPECT_EQ(exportInfo->producerTask, task);
    EXPECT_EQ(exportInfo->sourceQueue, queues[0u].id);
    EXPECT_EQ(exportInfo->destinationQueue, queues[1u].id);
    EXPECT_EQ(exportInfo->finalState, Graphics::ResourceStates::AccelStructRead);

    ASSERT_EQ(compiledTask->epilogueBarrierCount, 2u);
    const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).epilogueBarriers;
    ASSERT_NE(barriers, nullptr);
    EXPECT_EQ(barriers[0u].type, Graphics::GpuCompiledBarrierType::AccelStructStateExport);
    EXPECT_EQ(barriers[0u].resource, accelStruct);
    EXPECT_EQ(barriers[0u].before, Graphics::ResourceStates::AccelStructWrite);
    EXPECT_EQ(barriers[0u].after, Graphics::ResourceStates::AccelStructRead);
    EXPECT_EQ(barriers[1u].type, Graphics::GpuCompiledBarrierType::AccelStructOwnershipRelease);
    EXPECT_EQ(barriers[1u].resource, accelStruct);
    EXPECT_EQ(barriers[1u].before, Graphics::ResourceStates::AccelStructRead);
    EXPECT_EQ(barriers[1u].after, Graphics::ResourceStates::AccelStructRead);
    EXPECT_EQ(barriers[1u].sourceQueue, queues[0u].id);
    EXPECT_EQ(barriers[1u].destinationQueue, queues[1u].id);
}

TEST(GpuTaskGraph, ExportsExternalFinalOwnershipWithMultipleTerminalPackets){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    const Graphics::GpuGraphResourceId texture = graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/external_final_multiple_packets"))
            .setMarkerLabel("External Final Multiple Packets")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::Common)
            .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(queues[1u].id)
    );
    ASSERT_TRUE(texture.valid());

    const Graphics::GpuTaskResourceUse graphicsUse{
        .resource = texture,
        .range = Graphics::GpuTaskResourceRange{
            .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
        },
        .requiredState = Graphics::ResourceStates::CopyDest,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskResourceUse computeUse{
        .resource = texture,
        .range = Graphics::GpuTaskResourceRange{
            .textureSubresources = Graphics::TextureSubresourceSet(1u, 1u, 0u, 1u),
        },
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    Graphics::GpuTaskDesc graphicsDesc;
    graphicsDesc
        .setIdentity(Name("tests/task_graph/external_final_multiple_graphics"))
        .setMarkerLabel("External Final Multiple Graphics")
        .setQueue(graphicsRequest)
        .setResourceUses(&graphicsUse, 1u)
    ;
    Graphics::GpuTaskDesc computeDesc;
    computeDesc
        .setIdentity(Name("tests/task_graph/external_final_multiple_compute"))
        .setMarkerLabel("External Final Multiple Compute")
        .setQueue(computeRequest)
        .setResourceUses(&computeUse, 1u)
    ;
    const Graphics::GpuTaskId graphicsTask = graph.addTask(graphicsDesc);
    const Graphics::GpuTaskId computeTask = graph.addTask(computeDesc);
    ASSERT_TRUE(graphicsTask.valid());
    ASSERT_TRUE(computeTask.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
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
    ASSERT_EQ(exportInfo->sourceCount, 2u);
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
    ASSERT_EQ(compiledGraphics->epilogueBarrierCount, 2u);
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
    const Graphics::GpuCompiledOwnershipTransfer* const ownershipTransfers =
        compiledPlan.logicalOwnershipTransfers()
    ;
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
    ASSERT_TRUE(statistics.valid());
    EXPECT_EQ(statistics.logicalOwnershipTransferCount, 1u);
    EXPECT_EQ(statistics.logicalOwnershipTransferSignatureCount, 1u);
    EXPECT_EQ(statistics.repeatedOwnershipTransferSignatureCount, 0u);
    EXPECT_EQ(statistics.concurrentSharingAdviceResourceCount, 0u);
}

TEST(GpuTaskGraph, OrdersExternalFinalTransitionAfterOverlappingConcurrentReaders){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskResourceRange range{
        .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
    };
    const ExternalFinalReadPair pair = AddExternalFinalReadPair(
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
    ASSERT_TRUE(pair.resource.valid());
    ASSERT_TRUE(pair.earlierReader.valid());
    ASSERT_TRUE(pair.finalizingReader.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

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

TEST(GpuTaskGraph, KeepsSameStateExternalExportConcurrentReadersIndependent){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskResourceRange range{
        .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
    };
    const ExternalFinalReadPair pair = AddExternalFinalReadPair(
        graph,
        Graphics::GpuGraphResourceType::Texture,
        range,
        range,
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        false,
        {}
    );
    ASSERT_TRUE(pair.resource.valid());
    ASSERT_TRUE(pair.earlierReader.valid());
    ASSERT_TRUE(pair.finalizingReader.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    EXPECT_EQ(FindEdge(analysis, pair.earlierReader, pair.finalizingReader), nullptr);

    const Graphics::GpuSubmissionPacketId earlierPacket = compiledPlan.packetForTask(pair.earlierReader);
    const Graphics::GpuSubmissionPacketId finalizingPacket = compiledPlan.packetForTask(pair.finalizingReader);
    ASSERT_TRUE(earlierPacket.valid());
    ASSERT_TRUE(finalizingPacket.valid());
    ASSERT_NE(earlierPacket, finalizingPacket);
    EXPECT_EQ(compiledPlan.packet(earlierPacket).plan->dependencyCount, 0u);
    EXPECT_EQ(compiledPlan.packet(finalizingPacket).plan->dependencyCount, 0u);

    const Graphics::GpuCompiledTask* const finalizingTask = compiledPlan.findTask(pair.finalizingReader).plan;
    ASSERT_NE(finalizingTask, nullptr);
    ASSERT_EQ(finalizingTask->epilogueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(pair.finalizingReader).epilogueBarriers;
    ASSERT_NE(barriers, nullptr);
    EXPECT_EQ(barriers[0u].before, Graphics::ResourceStates::ShaderResource);
    EXPECT_EQ(barriers[0u].after, Graphics::ResourceStates::ShaderResource);
}

TEST(GpuTaskGraph, KeepsDisjointExternalFinalTextureFragmentsIndependent){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const ExternalFinalReadPair pair = AddExternalFinalReadPair(
        graph,
        Graphics::GpuGraphResourceType::Texture,
        Graphics::GpuTaskResourceRange{
            .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
        },
        Graphics::GpuTaskResourceRange{
            .textureSubresources = Graphics::TextureSubresourceSet(1u, 1u, 0u, 1u),
        },
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceStates::CopySource,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        false,
        {}
    );
    ASSERT_TRUE(pair.resource.valid());
    ASSERT_TRUE(pair.earlierReader.valid());
    ASSERT_TRUE(pair.finalizingReader.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    EXPECT_EQ(FindEdge(analysis, pair.earlierReader, pair.finalizingReader), nullptr);

    const Graphics::GpuSubmissionPacketId earlierPacket = compiledPlan.packetForTask(pair.earlierReader);
    const Graphics::GpuSubmissionPacketId finalizingPacket = compiledPlan.packetForTask(pair.finalizingReader);
    ASSERT_TRUE(earlierPacket.valid());
    ASSERT_TRUE(finalizingPacket.valid());
    ASSERT_NE(earlierPacket, finalizingPacket);
    EXPECT_EQ(compiledPlan.packet(earlierPacket).plan->dependencyCount, 0u);
    EXPECT_EQ(compiledPlan.packet(finalizingPacket).plan->dependencyCount, 0u);

    const Graphics::GpuCompiledTask* const earlierTask = compiledPlan.findTask(pair.earlierReader).plan;
    const Graphics::GpuCompiledTask* const finalizingTask = compiledPlan.findTask(pair.finalizingReader).plan;
    ASSERT_NE(earlierTask, nullptr);
    ASSERT_NE(finalizingTask, nullptr);
    EXPECT_EQ(earlierTask->epilogueBarrierCount, 1u);
    EXPECT_EQ(finalizingTask->epilogueBarrierCount, 1u);
}

TEST(GpuTaskGraph, ElidesSamePacketExternalFinalTransitionSelfDependency){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskResourceRange range{
        .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
    };
    const ExternalFinalReadPair pair = AddExternalFinalReadPair(
        graph,
        Graphics::GpuGraphResourceType::Texture,
        range,
        range,
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceStates::CopySource,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute,
        true,
        false,
        {}
    );
    ASSERT_TRUE(pair.resource.valid());
    ASSERT_TRUE(pair.earlierReader.valid());
    ASSERT_TRUE(pair.finalizingReader.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    EXPECT_EQ(FindEdge(analysis, pair.earlierReader, pair.finalizingReader), nullptr);

    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(pair.earlierReader);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetForTask(pair.finalizingReader), packet);
    EXPECT_EQ(compiledPlan.packet(packet).plan->dependencyCount, 0u);
    const Graphics::GpuCompiledTask* const finalizingTask = compiledPlan.findTask(pair.finalizingReader).plan;
    ASSERT_NE(finalizingTask, nullptr);
    EXPECT_EQ(finalizingTask->epilogueBarrierCount, 1u);
}

TEST(GpuTaskGraph, OrdersExternalFinalTransitionsForWholeAllocationResources){
    struct WholeAllocationCase{
        Graphics::GpuGraphResourceType::Enum type;
        Graphics::ResourceStates::Mask readState;
        Graphics::ResourceStates::Mask finalState;
    };
    const WholeAllocationCase testCases[] = {
        {
            Graphics::GpuGraphResourceType::Buffer,
            Graphics::ResourceStates::ShaderResource,
            Graphics::ResourceStates::CopySource,
        },
        {
            Graphics::GpuGraphResourceType::AccelStruct,
            Graphics::ResourceStates::AccelStructRead,
            Graphics::ResourceStates::Common,
        },
    };

    for(const WholeAllocationCase& testCase : testCases){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const ExternalFinalReadPair pair = AddExternalFinalReadPair(
            graph,
            testCase.type,
            {},
            {},
            testCase.readState,
            testCase.finalState,
            Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute,
            false,
            false,
            {}
        );
        ASSERT_TRUE(pair.resource.valid());
        ASSERT_TRUE(pair.earlierReader.valid());
        ASSERT_TRUE(pair.finalizingReader.valid());

        const Graphics::GpuPhysicalQueueInfo queues[] = {
            GraphicsQueue(),
            DedicatedComputeQueue(),
        };
        const Graphics::GpuTaskGraphQueueTopology topology{
            .queues = queues,
            .queueCount = LengthOf(queues),
        };
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        EXPECT_EQ(FindEdge(analysis, pair.earlierReader, pair.finalizingReader), nullptr);

        const Graphics::GpuSubmissionPacketId earlierPacket = compiledPlan.packetForTask(pair.earlierReader);
        const Graphics::GpuSubmissionPacketId finalizingPacket = compiledPlan.packetForTask(pair.finalizingReader);
        ASSERT_TRUE(earlierPacket.valid());
        ASSERT_TRUE(finalizingPacket.valid());
        ASSERT_NE(earlierPacket, finalizingPacket);
        const Graphics::GpuSubmissionPacket& finalizingPacketInfo = *compiledPlan.packet(finalizingPacket).plan;
        ASSERT_EQ(finalizingPacketInfo.dependencyCount, 1u);
        const Graphics::GpuPacketDependency* const dependencies = compiledPlan.packet(finalizingPacket).dependencies;
        ASSERT_NE(dependencies, nullptr);
        EXPECT_EQ(dependencies[0u].producer, earlierPacket);
        EXPECT_EQ(dependencies[0u].consumer, finalizingPacket);

        const Graphics::GpuCompiledTask* const finalizingTask = compiledPlan.findTask(pair.finalizingReader).plan;
        ASSERT_NE(finalizingTask, nullptr);
        ASSERT_EQ(finalizingTask->epilogueBarrierCount, 1u);
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(pair.finalizingReader).epilogueBarriers;
        ASSERT_NE(barriers, nullptr);
        EXPECT_EQ(barriers[0u].before, testCase.readState);
        EXPECT_EQ(barriers[0u].after, testCase.finalState);
    }
}

TEST(GpuTaskGraph, ElidesSamePacketReleaseOnlyExternalFinalizationSelfDependency){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskResourceRange range{
        .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
    };
    const ExternalFinalReadPair pair = AddExternalFinalReadPair(
        graph,
        Graphics::GpuGraphResourceType::Texture,
        range,
        range,
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceQueueSharing::Exclusive,
        true,
        false,
        queues[1u].id
    );
    ASSERT_TRUE(pair.resource.valid());
    ASSERT_TRUE(pair.earlierReader.valid());
    ASSERT_TRUE(pair.finalizingReader.valid());

    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(pair.earlierReader);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetForTask(pair.finalizingReader), packet);
    EXPECT_EQ(compiledPlan.packet(packet).plan->dependencyCount, 0u);
    const Graphics::GpuCompiledTask* const finalizingTask = compiledPlan.findTask(pair.finalizingReader).plan;
    ASSERT_NE(finalizingTask, nullptr);
    ASSERT_EQ(finalizingTask->epilogueBarrierCount, 2u);
    const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(pair.finalizingReader).epilogueBarriers;
    ASSERT_NE(barriers, nullptr);
    EXPECT_EQ(barriers[0u].type, Graphics::GpuCompiledBarrierType::TextureStateExport);
    EXPECT_EQ(barriers[0u].before, Graphics::ResourceStates::ShaderResource);
    EXPECT_EQ(barriers[0u].after, Graphics::ResourceStates::ShaderResource);
    EXPECT_EQ(barriers[1u].type, Graphics::GpuCompiledBarrierType::TextureOwnershipRelease);
    EXPECT_EQ(barriers[1u].destinationQueue, queues[1u].id);
}

TEST(GpuTaskGraph, AvoidsTransitiveExternalFinalizationPacketDependencies){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskResourceRange range{
        .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
    };
    const ExternalFinalReadPair pair = AddExternalFinalReadPair(
        graph,
        Graphics::GpuGraphResourceType::Texture,
        range,
        range,
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceStates::CopySource,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        true,
        {}
    );
    ASSERT_TRUE(pair.resource.valid());
    ASSERT_TRUE(pair.earlierReader.valid());
    ASSERT_TRUE(pair.finalizingReader.valid());

    const Graphics::GpuTaskResourceUse terminalUse{
        .resource = pair.resource,
        .range = range,
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
        .hasIndependentStateSource = true,
    };
    Graphics::GpuTaskSchedulingHint terminalScheduling;
    terminalScheduling.forceSubmissionBoundary = true;
    terminalScheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc terminalDesc;
    terminalDesc
        .setIdentity(Name("tests/task_graph/external_final_transitive_terminal_reader"))
        .setMarkerLabel("External Final Transitive Terminal Reader")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(terminalScheduling)
        .setDependencies(&pair.finalizingReader, 1u)
        .setResourceUses(&terminalUse, 1u)
    ;
    const Graphics::GpuTaskId terminalReader = graph.addTask(terminalDesc);
    ASSERT_TRUE(terminalReader.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    EXPECT_NE(FindEdge(analysis, pair.earlierReader, pair.finalizingReader), nullptr);
    EXPECT_NE(FindEdge(analysis, pair.finalizingReader, terminalReader), nullptr);
    EXPECT_EQ(FindEdge(analysis, pair.earlierReader, terminalReader), nullptr);

    const Graphics::GpuSubmissionPacketId earlierPacket = compiledPlan.packetForTask(pair.earlierReader);
    const Graphics::GpuSubmissionPacketId middlePacket = compiledPlan.packetForTask(pair.finalizingReader);
    const Graphics::GpuSubmissionPacketId terminalPacket = compiledPlan.packetForTask(terminalReader);
    ASSERT_TRUE(earlierPacket.valid());
    ASSERT_TRUE(middlePacket.valid());
    ASSERT_TRUE(terminalPacket.valid());
    ASSERT_EQ(compiledPlan.packet(middlePacket).plan->dependencyCount, 1u);
    ASSERT_EQ(compiledPlan.packet(terminalPacket).plan->dependencyCount, 1u);
    const Graphics::GpuPacketDependency* const middleDependencies = compiledPlan.packet(middlePacket).dependencies;
    const Graphics::GpuPacketDependency* const terminalDependencies = compiledPlan.packet(terminalPacket).dependencies;
    ASSERT_NE(middleDependencies, nullptr);
    ASSERT_NE(terminalDependencies, nullptr);
    EXPECT_EQ(middleDependencies[0u].producer, earlierPacket);
    EXPECT_EQ(terminalDependencies[0u].producer, middlePacket);
}

TEST(GpuTaskGraph, OrdersIndependentTerminalFinalizationDependenciesNearestFirst){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuGraphResourceDesc resourceDesc;
    resourceDesc
        .setIdentity(Name("tests/task_graph/ordered_terminal_finalization_resource"))
        .setMarkerLabel("Ordered Terminal Finalization Resource")
        .setType(Graphics::GpuGraphResourceType::Texture)
        .setInitialState(Graphics::ResourceStates::ShaderResource)
        .setExternalFinalState(Graphics::ResourceStates::CopySource)
        .setQueueSharing(Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute)
    ;
    const Graphics::GpuGraphResourceId resource = graph.importResource(resourceDesc);
    ASSERT_TRUE(resource.valid());

    const Graphics::GpuTaskResourceRange range{
        .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
    };
    const Graphics::GpuTaskResourceUse readerUse{
        .resource = resource,
        .range = range,
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
        .hasIndependentStateSource = true,
    };
    Graphics::GpuTaskSchedulingHint readerScheduling;
    readerScheduling.forceSubmissionBoundary = true;
    readerScheduling.allowPacketMerge = false;
    const Graphics::GpuQueueRequest readerQueues[] = {
        {
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueuePreference::Graphics,
            false,
            false,
        },
        {
            Graphics::GpuQueueCapability::Compute,
            Graphics::GpuQueuePreference::Compute,
            false,
            false,
        },
        {
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueuePreference::Graphics,
            false,
            false,
        },
        {
            Graphics::GpuQueueCapability::Compute,
            Graphics::GpuQueuePreference::Compute,
            false,
            false,
        },
    };

    Graphics::GpuTaskId readers[4u] = {};
    const Name readerBaseName("tests/task_graph/ordered_terminal_finalization_reader_");
    char readerIndexBuffer[32u] = {};
    for(usize readerIndex = 0u; readerIndex < LengthOf(readers); ++readerIndex){
        Graphics::GpuTaskDesc readerDesc;
        readerDesc
            .setIdentity(DeriveName(readerBaseName, FormatDecimal(readerIndex, readerIndexBuffer)))
            .setMarkerLabel("Ordered Terminal Finalization Reader")
            .setQueue(readerQueues[readerIndex])
            .setScheduling(readerScheduling)
            .setDependencies(readerIndex == 1u ? &readers[0u] : nullptr, readerIndex == 1u ? 1u : 0u)
            .setResourceUses(&readerUse, 1u)
        ;
        readers[readerIndex] = graph.addTask(readerDesc);
        ASSERT_TRUE(readers[readerIndex].valid());
    }

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    EXPECT_NE(FindEdge(analysis, readers[0u], readers[1u]), nullptr);
    EXPECT_EQ(FindEdge(analysis, readers[0u], readers[3u]), nullptr);
    EXPECT_EQ(FindEdge(analysis, readers[2u], readers[3u]), nullptr);
    ASSERT_EQ(compiledPlan.packetCount(), LengthOf(readers));

    Graphics::GpuSubmissionPacketId packets[4u] = {};
    for(usize readerIndex = 0u; readerIndex < LengthOf(readers); ++readerIndex){
        packets[readerIndex] = compiledPlan.packetForTask(readers[readerIndex]);
        ASSERT_TRUE(packets[readerIndex].valid());
        EXPECT_EQ(packets[readerIndex].index, readerIndex);
        EXPECT_EQ(compiledPlan.packet(packets[readerIndex]).plan->queue, queues[readerIndex % 2u].id);
    }

    ASSERT_EQ(compiledPlan.packet(packets[0u]).plan->dependencyCount, 0u);
    ASSERT_EQ(compiledPlan.packet(packets[1u]).plan->dependencyCount, 1u);
    ASSERT_EQ(compiledPlan.packet(packets[2u]).plan->dependencyCount, 0u);
    ASSERT_EQ(compiledPlan.packet(packets[3u]).plan->dependencyCount, 2u);
    const Graphics::GpuPacketDependency* const secondDependencies = compiledPlan.packet(packets[1u]).dependencies;
    const Graphics::GpuPacketDependency* const terminalDependencies = compiledPlan.packet(packets[3u]).dependencies;
    ASSERT_NE(secondDependencies, nullptr);
    ASSERT_NE(terminalDependencies, nullptr);
    EXPECT_EQ(secondDependencies[0u].producer, packets[0u]);
    EXPECT_EQ(secondDependencies[0u].consumer, packets[1u]);
    EXPECT_EQ(terminalDependencies[0u].producer, packets[2u]);
    EXPECT_EQ(terminalDependencies[0u].consumer, packets[3u]);
    EXPECT_EQ(terminalDependencies[1u].producer, packets[1u]);
    EXPECT_EQ(terminalDependencies[1u].consumer, packets[3u]);

    for(usize readerIndex = 1u; readerIndex < LengthOf(readers); ++readerIndex){
        const Graphics::GpuCompiledTask* const compiledReader = compiledPlan.findTask(readers[readerIndex]).plan;
        ASSERT_NE(compiledReader, nullptr);
        EXPECT_EQ(compiledReader->prologueStateSeedCount, 0u);
    }
    const Graphics::GpuCompiledTask* const terminalReader = compiledPlan.findTask(readers[3u]).plan;
    ASSERT_NE(terminalReader, nullptr);
    ASSERT_EQ(terminalReader->epilogueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const terminalBarriers = compiledPlan.findTask(readers[3u]).epilogueBarriers;
    ASSERT_NE(terminalBarriers, nullptr);
    EXPECT_EQ(terminalBarriers[0u].type, Graphics::GpuCompiledBarrierType::TextureStateExport);
    EXPECT_EQ(terminalBarriers[0u].before, Graphics::ResourceStates::ShaderResource);
    EXPECT_EQ(terminalBarriers[0u].after, Graphics::ResourceStates::CopySource);
}

TEST(GpuTaskGraph, TerminalFinalizationReachabilityCrossesPackedWordBoundaries){
    const usize prefixPacketCounts[] = { 63u, 64u, 127u, 128u };
    const Graphics::GpuTaskResourceRange range{
        .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
    };

    for(const usize prefixPacketCount : prefixPacketCounts){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuQueueRequest prefixQueue{
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueuePreference::Graphics,
            false,
            false,
        };
        Graphics::GpuTaskSchedulingHint prefixScheduling;
        prefixScheduling.forceSubmissionBoundary = true;
        prefixScheduling.allowPacketMerge = false;
        const Name prefixBaseName("tests/task_graph/terminal_packed_reachability_prefix_");
        char taskIndexBuffer[32u] = {};
        for(usize taskIndex = 0u; taskIndex < prefixPacketCount; ++taskIndex){
            const Graphics::GpuTaskId prefixTask = AddTaskWithQueue(
                graph,
                DeriveName(prefixBaseName, FormatDecimal(taskIndex, taskIndexBuffer)),
                "Terminal Packed Reachability Prefix",
                prefixQueue,
                prefixScheduling
            );
            ASSERT_TRUE(prefixTask.valid());
        }

        const ExternalFinalReadPair pair = AddExternalFinalReadPair(
            graph,
            Graphics::GpuGraphResourceType::Texture,
            range,
            range,
            Graphics::ResourceStates::ShaderResource,
            Graphics::ResourceStates::CopySource,
            Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute,
            false,
            true,
            {}
        );
        ASSERT_TRUE(pair.resource.valid());
        ASSERT_TRUE(pair.earlierReader.valid());
        ASSERT_TRUE(pair.finalizingReader.valid());

        const Graphics::GpuTaskResourceUse terminalUse{
            .resource = pair.resource,
            .range = range,
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
            .hasIndependentStateSource = true,
        };
        Graphics::GpuTaskSchedulingHint terminalScheduling;
        terminalScheduling.forceSubmissionBoundary = true;
        terminalScheduling.allowPacketMerge = false;
        Graphics::GpuTaskDesc terminalDesc;
        terminalDesc
            .setIdentity(Name("tests/task_graph/terminal_packed_reachability_reader"))
            .setMarkerLabel("Terminal Packed Reachability Reader")
            .setQueue(Graphics::GpuQueueRequest{
                Graphics::GpuQueueCapability::Graphics,
                Graphics::GpuQueuePreference::Graphics,
                false,
                false,
            })
            .setScheduling(terminalScheduling)
            .setDependencies(&pair.finalizingReader, 1u)
            .setResourceUses(&terminalUse, 1u)
        ;
        const Graphics::GpuTaskId terminalReader = graph.addTask(terminalDesc);
        ASSERT_TRUE(terminalReader.valid());

        const Graphics::GpuPhysicalQueueInfo queues[] = {
            GraphicsQueue(),
            DedicatedComputeQueue(),
        };
        const Graphics::GpuTaskGraphQueueTopology topology{
            .queues = queues,
            .queueCount = LengthOf(queues),
        };
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        EXPECT_EQ(compiledPlan.packetCount(), prefixPacketCount + 3u);

        const Graphics::GpuSubmissionPacketId earlierPacket = compiledPlan.packetForTask(pair.earlierReader);
        const Graphics::GpuSubmissionPacketId middlePacket = compiledPlan.packetForTask(pair.finalizingReader);
        const Graphics::GpuSubmissionPacketId terminalPacket = compiledPlan.packetForTask(terminalReader);
        ASSERT_TRUE(earlierPacket.valid());
        ASSERT_TRUE(middlePacket.valid());
        ASSERT_TRUE(terminalPacket.valid());
        EXPECT_EQ(earlierPacket.index, prefixPacketCount);
        EXPECT_EQ(middlePacket.index, prefixPacketCount + 1u);
        EXPECT_EQ(terminalPacket.index, prefixPacketCount + 2u);
        ASSERT_EQ(compiledPlan.packet(middlePacket).plan->dependencyCount, 1u);
        ASSERT_EQ(compiledPlan.packet(terminalPacket).plan->dependencyCount, 1u);
        const Graphics::GpuPacketDependency* const middleDependencies = compiledPlan.packet(middlePacket).dependencies;
        const Graphics::GpuPacketDependency* const terminalDependencies = compiledPlan.packet(terminalPacket).dependencies;
        ASSERT_NE(middleDependencies, nullptr);
        ASSERT_NE(terminalDependencies, nullptr);
        EXPECT_EQ(middleDependencies[0u].producer, earlierPacket);
        EXPECT_EQ(terminalDependencies[0u].producer, middlePacket);
    }
}

TEST(GpuTaskGraph, ExportsTextureTerminalFragmentsAfterPartialWholeResourceOverwrite){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
        DedicatedTransferQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    const Graphics::GpuGraphResourceId texture = graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/external_final_fragmented_texture"))
            .setMarkerLabel("External Final Fragmented Texture")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::Common)
            .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(queues[2u].id)
    );
    ASSERT_TRUE(texture.valid());

    const Graphics::GpuTaskResourceRange wholeRange{
        .textureSubresources = Graphics::s_AllSubresources,
    };
    const Graphics::GpuTaskResourceRange overwrittenRange{
        .textureSubresources = Graphics::TextureSubresourceSet(
            0u,
            1u,
            0u,
            Graphics::TextureSubresourceSet::AllArraySlices
        ),
    };
    const Graphics::GpuTaskResourceRange terminalTailRange{
        .textureSubresources = Graphics::TextureSubresourceSet(
            1u,
            Graphics::TextureSubresourceSet::AllMipLevels,
            0u,
            Graphics::TextureSubresourceSet::AllArraySlices
        ),
    };
    const Graphics::GpuTaskResourceUse wholeWriterUse{
        .resource = texture,
        .range = wholeRange,
        .requiredState = Graphics::ResourceStates::CopyDest,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskResourceUse partialWriterUse{
        .resource = texture,
        .range = overwrittenRange,
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc wholeWriterDesc;
    wholeWriterDesc
        .setIdentity(Name("tests/task_graph/external_final_fragmented_whole_writer"))
        .setMarkerLabel("External Final Fragmented Whole Writer")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setResourceUses(&wholeWriterUse, 1u)
    ;
    Graphics::GpuTaskDesc partialWriterDesc;
    partialWriterDesc
        .setIdentity(Name("tests/task_graph/external_final_fragmented_partial_writer"))
        .setMarkerLabel("External Final Fragmented Partial Writer")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setResourceUses(&partialWriterUse, 1u)
    ;
    const Graphics::GpuTaskId wholeWriter = graph.addTask(wholeWriterDesc);
    const Graphics::GpuTaskId partialWriter = graph.addTask(partialWriterDesc);
    ASSERT_TRUE(wholeWriter.valid());
    ASSERT_TRUE(partialWriter.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuCompiledTask* const compiledWholeWriter = compiledPlan.findTask(wholeWriter).plan;
    const Graphics::GpuCompiledTask* const compiledPartialWriter = compiledPlan.findTask(partialWriter).plan;
    ASSERT_NE(compiledWholeWriter, nullptr);
    ASSERT_NE(compiledPartialWriter, nullptr);
    EXPECT_EQ(compiledWholeWriter->queue, queues[0u].id);
    EXPECT_EQ(compiledPartialWriter->queue, queues[1u].id);

    const Graphics::GpuCompiledExternalResourceExportView exportView = compiledPlan.externalResourceExport(texture);
    const Graphics::GpuCompiledExternalResourceExport* const exportInfo = exportView.plan;
    ASSERT_NE(exportInfo, nullptr);
    EXPECT_EQ(exportInfo->resource, texture);
    EXPECT_EQ(exportInfo->destinationQueue, queues[2u].id);
    EXPECT_EQ(exportInfo->finalState, Graphics::ResourceStates::ShaderResource);
    EXPECT_FALSE(exportInfo->producerTask.valid());
    EXPECT_FALSE(exportInfo->sourceQueue.valid());
    ASSERT_EQ(exportInfo->sourceCount, 2u);
    const Graphics::GpuCompiledExternalResourceExportSource* const sources = exportView.sources;
    ASSERT_NE(sources, nullptr);
    bool hasWholeWriterTail = false;
    bool hasPartialWriterMipZero = false;
    for(u32 sourceIndex = 0u; sourceIndex < exportInfo->sourceCount; ++sourceIndex){
        const Graphics::GpuCompiledExternalResourceExportSource& source = sources[sourceIndex];
        hasWholeWriterTail = hasWholeWriterTail || (
            source.producerTask == wholeWriter
            && source.sourceQueue == queues[0u].id
            && source.range.textureSubresources == terminalTailRange.textureSubresources
        );
        hasPartialWriterMipZero = hasPartialWriterMipZero || (
            source.producerTask == partialWriter
            && source.sourceQueue == queues[1u].id
            && source.range.textureSubresources == overwrittenRange.textureSubresources
        );
    }
    EXPECT_TRUE(hasWholeWriterTail);
    EXPECT_TRUE(hasPartialWriterMipZero);

    const auto hasStateExport = [&](
        const Graphics::GpuTaskId task,
        const Graphics::TextureSubresourceSet& range,
        const Graphics::ResourceStates::Mask before,
        const Graphics::GpuPhysicalQueueId& queue
    ){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).epilogueBarriers;
        if(!compiledTask || !barriers)
            return false;
        for(u32 barrierIndex = 0u; barrierIndex < compiledTask->epilogueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureStateExport
                && barrier.resource == texture
                && barrier.range.textureSubresources == range
                && barrier.before == before
                && barrier.after == Graphics::ResourceStates::ShaderResource
                && barrier.sourceQueue == queue
                && barrier.destinationQueue == queue
            )
                return true;
        }
        return false;
    };
    const auto hasExternalRelease = [&](
        const Graphics::GpuTaskId task,
        const Graphics::TextureSubresourceSet& range,
        const Graphics::GpuPhysicalQueueId& sourceQueue
    ){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).epilogueBarriers;
        if(!compiledTask || !barriers)
            return false;
        for(u32 barrierIndex = 0u; barrierIndex < compiledTask->epilogueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureOwnershipRelease
                && barrier.resource == texture
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::ShaderResource
                && barrier.after == Graphics::ResourceStates::ShaderResource
                && barrier.sourceQueue == sourceQueue
                && barrier.destinationQueue == queues[2u].id
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasStateExport(
        wholeWriter,
        terminalTailRange.textureSubresources,
        Graphics::ResourceStates::CopyDest,
        queues[0u].id
    ));
    EXPECT_TRUE(hasStateExport(
        partialWriter,
        overwrittenRange.textureSubresources,
        Graphics::ResourceStates::UnorderedAccess,
        queues[1u].id
    ));
    EXPECT_TRUE(hasExternalRelease(
        wholeWriter,
        terminalTailRange.textureSubresources,
        queues[0u].id
    ));
    EXPECT_TRUE(hasExternalRelease(
        partialWriter,
        overwrittenRange.textureSubresources,
        queues[1u].id
    ));
}

TEST(GpuTaskGraph, RejectsUnpublishableExternalFinalStateContracts){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuGraphResourceDesc unsupportedDesc;
    unsupportedDesc
        .setIdentity(Name("tests/task_graph/external_final_hazard_domain"))
        .setMarkerLabel("External Final Hazard Domain")
        .setType(Graphics::GpuGraphResourceType::HazardDomain)
        .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
    ;
    EXPECT_FALSE(graph.importResource(unsupportedDesc).valid());

    Graphics::GpuGraphResourceDesc destinationWithoutFinalState;
    destinationWithoutFinalState
        .setIdentity(Name("tests/task_graph/external_final_release_without_state"))
        .setMarkerLabel("External Final Release Without State")
        .setType(Graphics::GpuGraphResourceType::Buffer)
        .setInitialState(Graphics::ResourceStates::Common)
        .setExternalFinalReleaseDestinationQueue(DedicatedComputeQueue().id)
    ;
    EXPECT_FALSE(graph.importResource(destinationWithoutFinalState).valid());

    Graphics::GpuGraphResourceDesc untouchedDesc;
    untouchedDesc
        .setIdentity(Name("tests/task_graph/external_final_untouched"))
        .setMarkerLabel("External Final Untouched")
        .setType(Graphics::GpuGraphResourceType::Buffer)
        .setInitialState(Graphics::ResourceStates::Common)
        .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
    ;
    EXPECT_TRUE(graph.importResource(untouchedDesc).valid());
    ASSERT_TRUE(AddTask(
        graph,
        Name("tests/task_graph/external_final_unrelated"),
        "External Final Unrelated"
    ).valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    EXPECT_FALSE(Compile(graph, analysis, topology, assignments, compiledGraph));
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

