// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>

#include <core/graphics/rhi/queue_sharing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_queue_ownership_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, PlansExclusiveOwnershipHandoffToDedicatedTransfer){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto pairResult = AddTransferOwnershipPair(graph, Core::ResourceQueueSharing::Exclusive);
    ASSERT_TRUE(pairResult);
    const TransferOwnershipPair pair = *pairResult;
    ASSERT_TRUE(pair.texture.valid());
    ASSERT_TRUE(pair.producer.valid());
    ASSERT_TRUE(pair.consumer.valid());

    const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), DedicatedTransferQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    {
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        const Core::GpuCompiledTaskView compiledProducerView = compiledPlan.findTask(pair.producer);
        const Core::GpuCompiledTaskView compiledConsumerView = compiledPlan.findTask(pair.consumer);
        const Core::GpuCompiledTask* const compiledProducer = compiledProducerView.plan;
        const Core::GpuCompiledTask* const compiledConsumer = compiledConsumerView.plan;
        ASSERT_NE(compiledProducer, nullptr);
        ASSERT_NE(compiledConsumer, nullptr);
        const Core::GpuPhysicalQueueInfo* const consumerQueue = compiledPlan.queueInfo(compiledConsumer->queue);
        ASSERT_NE(consumerQueue, nullptr);
        EXPECT_EQ(consumerQueue->queueClass, Core::CommandQueue::Transfer);
        ASSERT_EQ(compiledProducer->epilogueBarrierCount, 1u);
        ASSERT_EQ(compiledConsumer->prologueBarrierCount, 2u);
        ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);

        const Core::GpuCompiledBarrier* const release = compiledProducerView.epilogueBarriers;
        const Core::GpuCompiledBarrier* const acquire = compiledConsumerView.prologueBarriers;
        const Core::GpuPacketStateSeed* const stateSeed = compiledConsumerView.prologueStateSeeds;
        ASSERT_NE(release, nullptr);
        ASSERT_NE(acquire, nullptr);
        ASSERT_NE(stateSeed, nullptr);
        EXPECT_EQ(release[0u].type, Core::GpuCompiledBarrierType::TextureOwnershipRelease);
        EXPECT_EQ(release[0u].resource, pair.texture);
        EXPECT_EQ(release[0u].before, Core::ResourceStates::CopySource);
        EXPECT_EQ(release[0u].after, Core::ResourceStates::CopySource);
        EXPECT_EQ(release[0u].sourceQueue, compiledProducer->queue);
        EXPECT_EQ(release[0u].destinationQueue, compiledConsumer->queue);
        EXPECT_FALSE(release[0u].forceMemoryDependency);
        EXPECT_EQ(acquire[0u].type, Core::GpuCompiledBarrierType::TextureOwnershipAcquire);
        EXPECT_EQ(acquire[0u].resource, pair.texture);
        EXPECT_EQ(acquire[0u].sourceQueue, compiledProducer->queue);
        EXPECT_EQ(acquire[0u].destinationQueue, compiledConsumer->queue);
        EXPECT_FALSE(acquire[0u].isInitialOwnerHandoff);
        EXPECT_FALSE(acquire[0u].forceMemoryDependency);
        EXPECT_EQ(acquire[1u].type, Core::GpuCompiledBarrierType::TextureTransition);
        EXPECT_EQ(acquire[1u].resource, pair.texture);
        EXPECT_EQ(acquire[1u].before, Core::ResourceStates::CopySource);
        EXPECT_EQ(acquire[1u].after, Core::ResourceStates::CopySource);
        EXPECT_EQ(acquire[1u].sourceQueue, compiledProducer->queue);
        EXPECT_EQ(acquire[1u].destinationQueue, compiledConsumer->queue);
        EXPECT_FALSE(acquire[1u].isGraphInitialState);
        EXPECT_FALSE(acquire[1u].isInitialOwnerHandoff);
        EXPECT_TRUE(acquire[1u].forceMemoryDependency);
        EXPECT_EQ(stateSeed[0u].resource, pair.texture);
        EXPECT_EQ(stateSeed[0u].sourcePacket, compiledProducer->packet);

        const Core::GpuTaskGraphPhysicalQueueCompileStatistics producerQueueCompileStatistics = compiledPlan.physicalQueueCompileStatistics(compiledProducer->queue);
        const Core::GpuTaskGraphPhysicalQueueCompileStatistics consumerQueueCompileStatistics = compiledPlan.physicalQueueCompileStatistics(compiledConsumer->queue);
        ASSERT_TRUE(producerQueueCompileStatistics.valid());
        ASSERT_TRUE(consumerQueueCompileStatistics.valid());
        EXPECT_EQ(producerQueueCompileStatistics.ownershipReleaseBarrierCount, 1u);
        EXPECT_EQ(producerQueueCompileStatistics.ownershipAcquireBarrierCount, 0u);
        EXPECT_EQ(consumerQueueCompileStatistics.ownershipReleaseBarrierCount, 0u);
        EXPECT_EQ(consumerQueueCompileStatistics.ownershipAcquireBarrierCount, 1u);

        ASSERT_EQ(compiledPlan.logicalOwnershipTransferCount(), 1u);
        const Core::GpuCompiledOwnershipTransfer* const ownershipTransfers = compiledPlan.logicalOwnershipTransfers();
        ASSERT_NE(ownershipTransfers, nullptr);
        EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), ownershipTransfers);
        EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(1u), nullptr);
        const Core::GpuCompiledOwnershipTransfer& ownershipTransfer = ownershipTransfers[0u];
        EXPECT_TRUE(ownershipTransfer.valid());
        EXPECT_EQ(ownershipTransfer.resource, pair.texture);
        EXPECT_EQ(ownershipTransfer.resourceIdentity, Name("tests/task_graph/transfer_ownership_texture"));
        EXPECT_EQ(ownershipTransfer.range.textureSubresources, Core::s_AllSubresources);
        EXPECT_EQ(ownershipTransfer.sourceTask, pair.producer);
        EXPECT_EQ(ownershipTransfer.destinationTask, pair.consumer);
        EXPECT_EQ(ownershipTransfer.sourcePacket, compiledProducer->packet);
        EXPECT_EQ(ownershipTransfer.destinationPacket, compiledConsumer->packet);
        EXPECT_EQ(ownershipTransfer.sourceQueue, compiledProducer->queue);
        EXPECT_EQ(ownershipTransfer.destinationQueue, compiledConsumer->queue);
        EXPECT_EQ(ownershipTransfer.sourceQueueFamilyIndex, queues[0u].familyIndex);
        EXPECT_EQ(ownershipTransfer.destinationQueueFamilyIndex, queues[1u].familyIndex);
        EXPECT_EQ(ownershipTransfer.declaredQueueSharing, Core::ResourceQueueSharing::Exclusive);
        EXPECT_EQ(ownershipTransfer.resourceType, Core::GpuGraphResourceType::Texture);
        EXPECT_EQ(ownershipTransfer.route, Core::GpuOwnershipTransferRoute::Internal);
        EXPECT_TRUE(ownershipTransfer.concurrentSharingCouldAvoid);
        const Core::GpuTaskGraphCompileStatistics ownershipStatistics = compiledPlan.compileStatistics();
        ASSERT_TRUE(ownershipStatistics.valid());
        EXPECT_EQ(ownershipStatistics.logicalOwnershipTransferCount, 1u);
        EXPECT_EQ(ownershipStatistics.logicalOwnershipTransferSignatureCount, 1u);
        EXPECT_EQ(ownershipStatistics.repeatedOwnershipTransferSignatureCount, 0u);
        EXPECT_EQ(ownershipStatistics.concurrentSharingCouldAvoidTransferCount, 1u);
        EXPECT_EQ(ownershipStatistics.concurrentSharingAdviceResourceCount, 0u);
        EXPECT_EQ(ownershipStatistics.logicalOwnershipTransferCountByRoute[Core::GpuOwnershipTransferRoute::Internal], 1u);

        const Core::GpuCompiledPacketView consumerPacketView = compiledPlan.packet(compiledConsumer->packet);
        ASSERT_TRUE(consumerPacketView.valid());
        ASSERT_EQ(consumerPacketView.plan->dependencyCount, 1u);
        EXPECT_EQ(consumerPacketView.dependencies[0u].producer, compiledProducer->packet);
    }

    compiledGraph.reset();
    {
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        EXPECT_EQ(compiledPlan.logicalOwnershipTransferCount(), 0u);
        EXPECT_EQ(compiledPlan.logicalOwnershipTransfers(), nullptr);
        EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), nullptr);
    }

    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    {
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        ASSERT_EQ(compiledPlan.logicalOwnershipTransferCount(), 1u);
    }
    const Core::GpuPhysicalQueueTopology invalidTopology{};
    EXPECT_FALSE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, invalidTopology));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    EXPECT_FALSE(compiledPlan.valid());
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferCount(), 0u);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransfers(), nullptr);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), nullptr);
}

TEST(GpuTaskGraph, ReportsOwnershipRangesWithoutReleaseAcquireDuplicates){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto textureResult = AddTextureMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphMultiRangeOwnershipTexture), "Multi-Range Ownership Texture", Core::ResourceStates::Common, Core::ResourceQueueSharing::Exclusive);
    ASSERT_TRUE(textureResult);
    const Core::GpuGraphResourceId texture = *textureResult;
    ASSERT_TRUE(texture.valid());

    const Core::GpuTaskResourceRange firstRange{ .textureSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u), };
    const Core::GpuTaskResourceRange secondRange{ .textureSubresources = Core::TextureSubresourceSet(2u, 1u, 0u, 1u), };
    const Array<Core::GpuTaskResourceUse, 2u> producerUses = {
        Core::GpuTaskResourceUse{ .resource = texture, .range = firstRange, .requiredState = Core::ResourceStates::CopySource, .access = Core::GpuTaskResourceAccess::Write, },
        Core::GpuTaskResourceUse{ .resource = texture, .range = secondRange, .requiredState = Core::ResourceStates::CopySource, .access = Core::GpuTaskResourceAccess::Write, },
    };
    const Array<Core::GpuTaskResourceUse, 2u> consumerUses = {
        Core::GpuTaskResourceUse{ .resource = texture, .range = firstRange, .requiredState = Core::ResourceStates::CopySource, .access = Core::GpuTaskResourceAccess::Read, },
        Core::GpuTaskResourceUse{ .resource = texture, .range = secondRange, .requiredState = Core::ResourceStates::CopySource, .access = Core::GpuTaskResourceAccess::Read, },
    };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc producerDesc;
    producerDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphMultiRangeOwnershipProducer)).setMarkerLabel("Multi-Range Ownership Producer").setScheduling(scheduling).setResourceUses(producerUses);
    Core::GpuTaskDesc consumerDesc;
    consumerDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphMultiRangeOwnershipConsumer)).setMarkerLabel("Multi-Range Ownership Consumer").setScheduling(scheduling).setResourceUses(consumerUses);
    const auto producerResult = graph.addTask(producerDesc, Core::GpuTaskCommandRequirements{ Core::GpuQueueCapability::Graphics });
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;
    const auto consumerResult = graph.addTask(consumerDesc, Core::GpuTaskCommandRequirements{ Core::GpuQueueCapability::Compute });
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());

    const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Core::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
    const Core::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumer).plan;
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    EXPECT_EQ(compiledProducer->queue, queues[0u].id);
    EXPECT_EQ(compiledConsumer->queue, queues[1u].id);
    const Core::GpuTaskGraphCompileStatistics& statistics = compiledPlan.compileStatistics();
    ASSERT_TRUE(statistics.valid());
    EXPECT_EQ(statistics.ownershipReleaseBarrierCount, 2u);
    EXPECT_EQ(statistics.ownershipAcquireBarrierCount, 2u);
    EXPECT_EQ(statistics.logicalOwnershipTransferCount, 2u);
    EXPECT_EQ(statistics.logicalOwnershipTransferSignatureCount, 1u);
    EXPECT_EQ(statistics.repeatedOwnershipTransferSignatureCount, 0u);
    EXPECT_EQ(statistics.concurrentSharingCouldAvoidTransferCount, 2u);
    EXPECT_EQ(statistics.concurrentSharingAdviceResourceCount, 0u);

    const Core::GpuTaskGraphPhysicalQueueCompileStatistics producerStatistics = compiledPlan.physicalQueueCompileStatistics(queues[0u].id);
    const Core::GpuTaskGraphPhysicalQueueCompileStatistics consumerStatistics = compiledPlan.physicalQueueCompileStatistics(queues[1u].id);
    ASSERT_TRUE(producerStatistics.valid());
    ASSERT_TRUE(consumerStatistics.valid());
    EXPECT_EQ(producerStatistics.ownershipReleaseBarrierCount, 2u);
    EXPECT_EQ(producerStatistics.ownershipAcquireBarrierCount, 0u);
    EXPECT_EQ(producerStatistics.outgoingLogicalOwnershipTransferCount, 2u);
    EXPECT_EQ(producerStatistics.incomingLogicalOwnershipTransferCount, 0u);
    EXPECT_EQ(producerStatistics.outgoingLogicalOwnershipTransferSignatureCount, 1u);
    EXPECT_EQ(producerStatistics.outgoingRepeatedOwnershipTransferSignatureCount, 0u);
    EXPECT_EQ(producerStatistics.concurrentSharingAdviceResourceCount, 0u);
    EXPECT_EQ(consumerStatistics.ownershipReleaseBarrierCount, 0u);
    EXPECT_EQ(consumerStatistics.ownershipAcquireBarrierCount, 2u);
    EXPECT_EQ(consumerStatistics.outgoingLogicalOwnershipTransferCount, 0u);
    EXPECT_EQ(consumerStatistics.incomingLogicalOwnershipTransferCount, 2u);
    EXPECT_EQ(consumerStatistics.incomingLogicalOwnershipTransferSignatureCount, 1u);
    EXPECT_EQ(consumerStatistics.incomingRepeatedOwnershipTransferSignatureCount, 0u);
    EXPECT_EQ(consumerStatistics.concurrentSharingAdviceResourceCount, 0u);

    ASSERT_EQ(compiledPlan.logicalOwnershipTransferCount(), 2u);
    const Core::GpuCompiledOwnershipTransfer* const ownershipTransfers = compiledPlan.logicalOwnershipTransfers();
    ASSERT_NE(ownershipTransfers, nullptr);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), ownershipTransfers);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(1u), ownershipTransfers + 1u);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(2u), nullptr);
    usize firstRangeCount = 0u;
    usize secondRangeCount = 0u;
    for(usize transferIndex = 0u; transferIndex < compiledPlan.logicalOwnershipTransferCount(); ++transferIndex){
        const Core::GpuCompiledOwnershipTransfer& transfer = ownershipTransfers[transferIndex];
        EXPECT_TRUE(transfer.valid());
        EXPECT_EQ(transfer.resource, texture);
        EXPECT_EQ(transfer.resourceIdentity, Name("tests/task_graph/multi_range_ownership_texture"));
        EXPECT_EQ(transfer.sourceTask, producer);
        EXPECT_EQ(transfer.destinationTask, consumer);
        EXPECT_EQ(transfer.sourcePacket, compiledProducer->packet);
        EXPECT_EQ(transfer.destinationPacket, compiledConsumer->packet);
        EXPECT_EQ(transfer.sourceQueue, queues[0u].id);
        EXPECT_EQ(transfer.destinationQueue, queues[1u].id);
        EXPECT_EQ(transfer.sourceQueueFamilyIndex, queues[0u].familyIndex);
        EXPECT_EQ(transfer.destinationQueueFamilyIndex, queues[1u].familyIndex);
        EXPECT_EQ(transfer.declaredQueueSharing, Core::ResourceQueueSharing::Exclusive);
        EXPECT_EQ(transfer.resourceType, Core::GpuGraphResourceType::Texture);
        EXPECT_EQ(transfer.route, Core::GpuOwnershipTransferRoute::Internal);
        EXPECT_TRUE(transfer.concurrentSharingCouldAvoid);
        if(transfer.range.textureSubresources == firstRange.textureSubresources)
            ++firstRangeCount;
        if(transfer.range.textureSubresources == secondRange.textureSubresources)
            ++secondRangeCount;
    }
    EXPECT_EQ(firstRangeCount, 1u);
    EXPECT_EQ(secondRangeCount, 1u);
}

TEST(GpuTaskGraph, UsesDeclaredTripleQueueSharingForDedicatedTransfer){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto pairResult = AddTransferOwnershipPair(graph, Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer);
    ASSERT_TRUE(pairResult);
    const TransferOwnershipPair pair = *pairResult;
    ASSERT_TRUE(pair.texture.valid());
    ASSERT_TRUE(pair.producer.valid());
    ASSERT_TRUE(pair.consumer.valid());

    ThreeQueueCompile threeQueueCompile(testArena);
    ASSERT_TRUE(threeQueueCompile.compile(graph));
    const Core::GpuCompiledGraph::ReadView compiledPlan(threeQueueCompile.compiledGraph);


    const Core::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(pair.producer).plan;
    const Core::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(pair.consumer).plan;
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    EXPECT_EQ(compiledProducer->epilogueBarrierCount, 0u);
    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);
    const Core::GpuCompiledBarrier* const dependency = compiledPlan.findTask(pair.consumer).prologueBarriers;
    const Core::GpuPacketStateSeed* const stateSeed = compiledPlan.findTask(pair.consumer).prologueStateSeeds;
    ASSERT_NE(dependency, nullptr);
    ASSERT_NE(stateSeed, nullptr);
    EXPECT_EQ(dependency[0u].type, Core::GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(dependency[0u].resource, pair.texture);
    EXPECT_EQ(dependency[0u].before, Core::ResourceStates::CopySource);
    EXPECT_EQ(dependency[0u].after, Core::ResourceStates::CopySource);
    EXPECT_TRUE(dependency[0u].forceMemoryDependency);
    EXPECT_EQ(stateSeed[0u].resource, pair.texture);
    EXPECT_EQ(stateSeed[0u].sourcePacket, compiledProducer->packet);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferCount(), 0u);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransfers(), nullptr);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), nullptr);
    const Core::GpuTaskGraphCompileStatistics& statistics = compiledPlan.compileStatistics();
    ASSERT_TRUE(statistics.valid());
    EXPECT_EQ(statistics.ownershipReleaseBarrierCount, 0u);
    EXPECT_EQ(statistics.ownershipAcquireBarrierCount, 0u);
    EXPECT_EQ(statistics.logicalOwnershipTransferCount, 0u);
    EXPECT_EQ(statistics.logicalOwnershipTransferSignatureCount, 0u);
    EXPECT_EQ(statistics.concurrentSharingCouldAvoidTransferCount, 0u);
    EXPECT_EQ(statistics.concurrentSharingAdviceResourceCount, 0u);
}

TEST(GpuTaskGraph, OmitsOwnershipTelemetryForSameFamilyAndSamePhysicalRoutes){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto pairResult = AddTransferOwnershipPair(graph, Core::ResourceQueueSharing::Exclusive);
    ASSERT_TRUE(pairResult);
    const TransferOwnershipPair pair = *pairResult;
    ASSERT_TRUE(pair.texture.valid());
    ASSERT_TRUE(pair.producer.valid());
    ASSERT_TRUE(pair.consumer.valid());

    {
        const Core::GpuPhysicalQueueInfo queue = GraphicsQueue();
        const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Core::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        const Core::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(pair.producer).plan;
        const Core::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(pair.consumer).plan;
        ASSERT_NE(compiledProducer, nullptr);
        ASSERT_NE(compiledConsumer, nullptr);
        EXPECT_EQ(compiledProducer->queue, queue.id);
        EXPECT_EQ(compiledConsumer->queue, queue.id);
        EXPECT_EQ(compiledPlan.logicalOwnershipTransferCount(), 0u);
        EXPECT_EQ(compiledPlan.logicalOwnershipTransfers(), nullptr);
        EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), nullptr);
        EXPECT_EQ(compiledPlan.compileStatistics().ownershipReleaseBarrierCount, 0u);
        EXPECT_EQ(compiledPlan.compileStatistics().ownershipAcquireBarrierCount, 0u);
    }

    {
        Core::GpuPhysicalQueueInfo sameFamilyTransferQueue = DedicatedTransferQueue();
        sameFamilyTransferQueue.familyIndex = GraphicsQueue().familyIndex;
        sameFamilyTransferQueue.queueIndex = 1u;
        const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), sameFamilyTransferQueue, };
        const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Core::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        const Core::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(pair.producer).plan;
        const Core::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(pair.consumer).plan;
        ASSERT_NE(compiledProducer, nullptr);
        ASSERT_NE(compiledConsumer, nullptr);
        EXPECT_EQ(compiledProducer->queue, queues[0u].id);
        EXPECT_EQ(compiledConsumer->queue, queues[1u].id);
        EXPECT_NE(compiledProducer->queue, compiledConsumer->queue);
        EXPECT_EQ(queues[0u].familyIndex, queues[1u].familyIndex);
        EXPECT_EQ(compiledPlan.logicalOwnershipTransferCount(), 0u);
        EXPECT_EQ(compiledPlan.logicalOwnershipTransfers(), nullptr);
        EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), nullptr);
        EXPECT_EQ(compiledPlan.compileStatistics().ownershipReleaseBarrierCount, 0u);
        EXPECT_EQ(compiledPlan.compileStatistics().ownershipAcquireBarrierCount, 0u);
    }
}

TEST(GpuTaskGraph, LogicalSharingRequiresTwoSelectedFamiliesAcrossEveryMaskAndTopologyOrder){
    const Array<Core::ResourceQueueSharing::Mask, 8u> sharingMasks = {
        Core::ResourceQueueSharing::Exclusive,
        Core::ResourceQueueSharing::Graphics,
        Core::ResourceQueueSharing::AsyncCompute,
        Core::ResourceQueueSharing::Transfer,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
        Core::ResourceQueueSharing::GraphicsAndTransfer,
        Core::ResourceQueueSharing::AsyncComputeAndTransfer,
        Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer,
    };
    const Array<Core::CommandQueue::Enum, 4u> splitClasses = { Core::CommandQueue::kCount, Core::CommandQueue::Graphics, Core::CommandQueue::Compute, Core::CommandQueue::Transfer, };
    for(const Core::ResourceQueueSharing::Mask sharing : sharingMasks){
        SCOPED_TRACE(static_cast<u32>(sharing));
        for(const Core::CommandQueue::Enum splitClass : splitClasses){
            SCOPED_TRACE(static_cast<u32>(splitClass));
            Array<Core::GpuPhysicalQueueInfo, 6u> queues = { GraphicsQueue(0u), DedicatedComputeQueue(1u), DedicatedTransferQueue(2u), GraphicsQueue(3u), DedicatedComputeQueue(4u), DedicatedTransferQueue(5u), };
            for(usize queueIndex = 0u; queueIndex < LengthOf(queues); ++queueIndex){
                queues[queueIndex].familyIndex = queueIndex >= 3u && queues[queueIndex].queueClass == splitClass ? 1u : 0u;
                queues[queueIndex].queueIndex = static_cast<u32>(queueIndex);
            }
            for(const b8 reversed : { false, true }){
                SCOPED_TRACE(reversed);
                Array<Core::GpuPhysicalQueueInfo, LengthOf(queues)> orderedQueues;
                for(usize queueIndex = 0u; queueIndex < LengthOf(queues); ++queueIndex)
                    orderedQueues[queueIndex] = queues[reversed ? LengthOf(queues) - 1u - queueIndex : queueIndex];
                const Core::GpuPhysicalQueueTopology topology{ .queues = orderedQueues.data(), .queueCount = LengthOf(orderedQueues) };
                ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::IsValidQueueTopology(topology));
                Core::GpuTaskGraphResourceView resource{};
                resource.type = Core::GpuGraphResourceType::Buffer;
                resource.queueSharing = sharing;
                const b8 expectedConcurrent = Core::ResourceQueueSharing::IncludesQueueClass(sharing, splitClass);
                EXPECT_EQ(Core::GpuTaskGraphCompilerDetail::ResourceUsesConcurrentQueueSharing(resource, topology), expectedConcurrent);
            }
        }
    }
}

TEST(GpuTaskGraph, AcceptsDedicatedTransferClassOnAConcurrentlySharedComputeFamily){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto pairResult = AddTransferOwnershipPair(graph, Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
    ASSERT_TRUE(pairResult);
    const TransferOwnershipPair pair = *pairResult;
    ASSERT_TRUE(pair.texture.valid());
    ASSERT_TRUE(pair.producer.valid());
    ASSERT_TRUE(pair.consumer.valid());

    Core::GpuPhysicalQueueInfo transferOnComputeFamily = DedicatedTransferQueue();
    transferOnComputeFamily.familyIndex = DedicatedComputeQueue().familyIndex;
    transferOnComputeFamily.queueIndex = 1u;
    const Array<Core::GpuPhysicalQueueInfo, 3u> queues = { GraphicsQueue(), DedicatedComputeQueue(), transferOnComputeFamily, };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    const Core::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(pair.consumer);
    const Core::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(pair.producer).plan;
    const Core::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(pair.consumer).plan;
    ASSERT_NE(consumerAssignment, nullptr);
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    EXPECT_EQ(consumerAssignment->queue, transferOnComputeFamily.id);
    EXPECT_EQ(compiledProducer->epilogueBarrierCount, 0u);
    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 1u);
    const Core::GpuCompiledBarrier* const dependency = compiledPlan.findTask(pair.consumer).prologueBarriers;
    ASSERT_NE(dependency, nullptr);
    EXPECT_EQ(dependency[0u].type, Core::GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(dependency[0u].resource, pair.texture);
    EXPECT_EQ(dependency[0u].before, Core::ResourceStates::CopySource);
    EXPECT_EQ(dependency[0u].after, Core::ResourceStates::CopySource);
    EXPECT_TRUE(dependency[0u].forceMemoryDependency);
}

TEST(GpuTaskGraph, RejectsDedicatedTransferUseOutsideConcurrentSharingContract){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto pairResult = AddTransferOwnershipPair(graph, Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
    ASSERT_TRUE(pairResult);
    const TransferOwnershipPair pair = *pairResult;
    ASSERT_TRUE(pair.texture.valid());
    ASSERT_TRUE(pair.producer.valid());
    ASSERT_TRUE(pair.consumer.valid());

    const Array<Core::GpuPhysicalQueueInfo, 3u> queues = { GraphicsQueue(), DedicatedComputeQueue(), DedicatedTransferQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    EXPECT_FALSE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    EXPECT_EQ(assignments.diagnostic().status, Core::GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue);
    EXPECT_EQ(assignments.diagnostic().task, pair.consumer);
    EXPECT_FALSE(compiledPlan.valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

