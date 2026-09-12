// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

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
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const TransferOwnershipPair pair = AddTransferOwnershipPair(graph, Graphics::ResourceQueueSharing::Exclusive);
    ASSERT_TRUE(pair.texture.valid());
    ASSERT_TRUE(pair.producer.valid());
    ASSERT_TRUE(pair.consumer.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedTransferQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        const Graphics::GpuCompiledTaskView compiledProducerView = compiledPlan.findTask(pair.producer);
        const Graphics::GpuCompiledTaskView compiledConsumerView = compiledPlan.findTask(pair.consumer);
        const Graphics::GpuCompiledTask* const compiledProducer = compiledProducerView.plan;
        const Graphics::GpuCompiledTask* const compiledConsumer = compiledConsumerView.plan;
        ASSERT_NE(compiledProducer, nullptr);
        ASSERT_NE(compiledConsumer, nullptr);
        const Graphics::GpuPhysicalQueueInfo* const consumerQueue = compiledPlan.queueInfo(compiledConsumer->queue);
        ASSERT_NE(consumerQueue, nullptr);
        EXPECT_EQ(consumerQueue->queueClass, Graphics::CommandQueue::Transfer);
        ASSERT_EQ(compiledProducer->epilogueBarrierCount, 1u);
        ASSERT_EQ(compiledConsumer->prologueBarrierCount, 2u);
        ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);

        const Graphics::GpuCompiledBarrier* const release = compiledProducerView.epilogueBarriers;
        const Graphics::GpuCompiledBarrier* const acquire = compiledConsumerView.prologueBarriers;
        const Graphics::GpuPacketStateSeed* const stateSeed = compiledConsumerView.prologueStateSeeds;
        ASSERT_NE(release, nullptr);
        ASSERT_NE(acquire, nullptr);
        ASSERT_NE(stateSeed, nullptr);
        EXPECT_EQ(release[0u].type, Graphics::GpuCompiledBarrierType::TextureOwnershipRelease);
        EXPECT_EQ(release[0u].resource, pair.texture);
        EXPECT_EQ(release[0u].before, Graphics::ResourceStates::CopySource);
        EXPECT_EQ(release[0u].after, Graphics::ResourceStates::CopySource);
        EXPECT_EQ(release[0u].sourceQueue, compiledProducer->queue);
        EXPECT_EQ(release[0u].destinationQueue, compiledConsumer->queue);
        EXPECT_FALSE(release[0u].forceMemoryDependency);
        EXPECT_EQ(acquire[0u].type, Graphics::GpuCompiledBarrierType::TextureOwnershipAcquire);
        EXPECT_EQ(acquire[0u].resource, pair.texture);
        EXPECT_EQ(acquire[0u].sourceQueue, compiledProducer->queue);
        EXPECT_EQ(acquire[0u].destinationQueue, compiledConsumer->queue);
        EXPECT_FALSE(acquire[0u].isInitialOwnerHandoff);
        EXPECT_FALSE(acquire[0u].forceMemoryDependency);
        EXPECT_EQ(acquire[1u].type, Graphics::GpuCompiledBarrierType::TextureTransition);
        EXPECT_EQ(acquire[1u].resource, pair.texture);
        EXPECT_EQ(acquire[1u].before, Graphics::ResourceStates::CopySource);
        EXPECT_EQ(acquire[1u].after, Graphics::ResourceStates::CopySource);
        EXPECT_EQ(acquire[1u].sourceQueue, compiledProducer->queue);
        EXPECT_EQ(acquire[1u].destinationQueue, compiledConsumer->queue);
        EXPECT_FALSE(acquire[1u].isGraphInitialState);
        EXPECT_FALSE(acquire[1u].isInitialOwnerHandoff);
        EXPECT_TRUE(acquire[1u].forceMemoryDependency);
        EXPECT_EQ(stateSeed[0u].resource, pair.texture);
        EXPECT_EQ(stateSeed[0u].sourcePacket, compiledProducer->packet);

        const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics producerQueueCompileStatistics =
            compiledPlan.physicalQueueCompileStatistics(compiledProducer->queue)
        ;
        const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics consumerQueueCompileStatistics =
            compiledPlan.physicalQueueCompileStatistics(compiledConsumer->queue)
        ;
        ASSERT_TRUE(producerQueueCompileStatistics.valid());
        ASSERT_TRUE(consumerQueueCompileStatistics.valid());
        EXPECT_EQ(producerQueueCompileStatistics.ownershipReleaseBarrierCount, 1u);
        EXPECT_EQ(producerQueueCompileStatistics.ownershipAcquireBarrierCount, 0u);
        EXPECT_EQ(consumerQueueCompileStatistics.ownershipReleaseBarrierCount, 0u);
        EXPECT_EQ(consumerQueueCompileStatistics.ownershipAcquireBarrierCount, 1u);

        ASSERT_EQ(compiledPlan.logicalOwnershipTransferCount(), 1u);
        const Graphics::GpuCompiledOwnershipTransfer* const ownershipTransfers =
            compiledPlan.logicalOwnershipTransfers()
        ;
        ASSERT_NE(ownershipTransfers, nullptr);
        EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), ownershipTransfers);
        EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(1u), nullptr);
        const Graphics::GpuCompiledOwnershipTransfer& ownershipTransfer = ownershipTransfers[0u];
        EXPECT_TRUE(ownershipTransfer.valid());
        EXPECT_EQ(ownershipTransfer.resource, pair.texture);
        EXPECT_EQ(ownershipTransfer.resourceIdentity, Name("tests/task_graph/transfer_ownership_texture"));
        EXPECT_EQ(ownershipTransfer.range.textureSubresources, Graphics::s_AllSubresources);
        EXPECT_EQ(ownershipTransfer.sourceTask, pair.producer);
        EXPECT_EQ(ownershipTransfer.destinationTask, pair.consumer);
        EXPECT_EQ(ownershipTransfer.sourcePacket, compiledProducer->packet);
        EXPECT_EQ(ownershipTransfer.destinationPacket, compiledConsumer->packet);
        EXPECT_EQ(ownershipTransfer.sourceQueue, compiledProducer->queue);
        EXPECT_EQ(ownershipTransfer.destinationQueue, compiledConsumer->queue);
        EXPECT_EQ(ownershipTransfer.sourceQueueFamilyIndex, queues[0u].familyIndex);
        EXPECT_EQ(ownershipTransfer.destinationQueueFamilyIndex, queues[1u].familyIndex);
        EXPECT_EQ(ownershipTransfer.declaredQueueSharing, Graphics::ResourceQueueSharing::Exclusive);
        EXPECT_EQ(ownershipTransfer.resourceType, Graphics::GpuGraphResourceType::Texture);
        EXPECT_EQ(ownershipTransfer.route, Graphics::GpuOwnershipTransferRoute::Internal);
        EXPECT_TRUE(ownershipTransfer.concurrentSharingCouldAvoid);
        Graphics::GpuCompiledOwnershipTransfer nonAdvisoryOwnershipTransfer = ownershipTransfer;
        nonAdvisoryOwnershipTransfer.concurrentSharingCouldAvoid = false;
        EXPECT_TRUE(nonAdvisoryOwnershipTransfer.valid());

        const Graphics::GpuTaskGraphCompileStatistics ownershipStatistics = compiledPlan.compileStatistics();
        ASSERT_TRUE(ownershipStatistics.valid());
        EXPECT_EQ(ownershipStatistics.logicalOwnershipTransferCount, 1u);
        EXPECT_EQ(ownershipStatistics.logicalOwnershipTransferSignatureCount, 1u);
        EXPECT_EQ(ownershipStatistics.repeatedOwnershipTransferSignatureCount, 0u);
        EXPECT_EQ(ownershipStatistics.concurrentSharingCouldAvoidTransferCount, 1u);
        EXPECT_EQ(ownershipStatistics.concurrentSharingAdviceResourceCount, 0u);
        EXPECT_EQ(
            ownershipStatistics.logicalOwnershipTransferCountByRoute[Graphics::GpuOwnershipTransferRoute::Internal],
            1u
        );
        EXPECT_EQ(
            ownershipStatistics.logicalOwnershipTransferCountByRoute[
                Graphics::GpuOwnershipTransferRoute::ExternalImport
            ],
            0u
        );
        EXPECT_EQ(
            ownershipStatistics.logicalOwnershipTransferCountByRoute[
                Graphics::GpuOwnershipTransferRoute::ExternalExport
            ],
            0u
        );

        const Graphics::GpuCompiledPacketView consumerPacketView = compiledPlan.packet(compiledConsumer->packet);
        ASSERT_TRUE(consumerPacketView.valid());
        ASSERT_EQ(consumerPacketView.plan->dependencyCount, 1u);
        EXPECT_EQ(consumerPacketView.dependencies[0u].producer, compiledProducer->packet);
    }

    compiledGraph.reset();
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        EXPECT_EQ(compiledPlan.logicalOwnershipTransferCount(), 0u);
        EXPECT_EQ(compiledPlan.logicalOwnershipTransfers(), nullptr);
        EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), nullptr);
    }

    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        ASSERT_EQ(compiledPlan.logicalOwnershipTransferCount(), 1u);
    }
    const Graphics::GpuTaskGraphQueueTopology invalidTopology{};
    EXPECT_FALSE(Compile(graph, analysis, invalidTopology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    EXPECT_FALSE(compiledPlan.valid());
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferCount(), 0u);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransfers(), nullptr);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), nullptr);
}

TEST(GpuTaskGraph, ReportsOwnershipRangesWithoutReleaseAcquireDuplicates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId texture = AddTextureMetadata(
        graph,
        Name("tests/task_graph/multi_range_ownership_texture"),
        "Multi-Range Ownership Texture",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Exclusive
    );
    ASSERT_TRUE(texture.valid());

    const Graphics::GpuTaskResourceRange firstRange{
        .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
    };
    const Graphics::GpuTaskResourceRange secondRange{
        .textureSubresources = Graphics::TextureSubresourceSet(2u, 1u, 0u, 1u),
    };
    const Graphics::GpuTaskResourceUse producerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = firstRange,
            .requiredState = Graphics::ResourceStates::CopySource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = secondRange,
            .requiredState = Graphics::ResourceStates::CopySource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse consumerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = firstRange,
            .requiredState = Graphics::ResourceStates::CopySource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = secondRange,
            .requiredState = Graphics::ResourceStates::CopySource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/multi_range_ownership_producer"))
        .setMarkerLabel("Multi-Range Ownership Producer")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(scheduling)
        .setResourceUses(producerUses, LengthOf(producerUses))
    ;
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/multi_range_ownership_consumer"))
        .setMarkerLabel("Multi-Range Ownership Consumer")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Compute,
            Graphics::GpuQueuePreference::Compute,
            false,
            false,
        })
        .setScheduling(scheduling)
        .setResourceUses(consumerUses, LengthOf(consumerUses))
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());

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


    const Graphics::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
    const Graphics::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumer).plan;
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    EXPECT_EQ(compiledProducer->queue, queues[0u].id);
    EXPECT_EQ(compiledConsumer->queue, queues[1u].id);
    const Graphics::GpuTaskGraphCompileStatistics& statistics = compiledPlan.compileStatistics();
    ASSERT_TRUE(statistics.valid());
    EXPECT_EQ(statistics.ownershipReleaseBarrierCount, 2u);
    EXPECT_EQ(statistics.ownershipAcquireBarrierCount, 2u);
    EXPECT_EQ(statistics.logicalOwnershipTransferCount, 2u);
    EXPECT_EQ(statistics.logicalOwnershipTransferSignatureCount, 1u);
    EXPECT_EQ(statistics.repeatedOwnershipTransferSignatureCount, 0u);
    EXPECT_EQ(statistics.concurrentSharingCouldAvoidTransferCount, 2u);
    EXPECT_EQ(statistics.concurrentSharingAdviceResourceCount, 0u);

    const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics producerStatistics =
        compiledPlan.physicalQueueCompileStatistics(queues[0u].id)
    ;
    const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics consumerStatistics =
        compiledPlan.physicalQueueCompileStatistics(queues[1u].id)
    ;
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
    const Graphics::GpuCompiledOwnershipTransfer* const ownershipTransfers =
        compiledPlan.logicalOwnershipTransfers()
    ;
    ASSERT_NE(ownershipTransfers, nullptr);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), ownershipTransfers);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(1u), ownershipTransfers + 1u);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(2u), nullptr);
    usize firstRangeCount = 0u;
    usize secondRangeCount = 0u;
    for(usize transferIndex = 0u; transferIndex < compiledPlan.logicalOwnershipTransferCount(); ++transferIndex){
        const Graphics::GpuCompiledOwnershipTransfer& transfer = ownershipTransfers[transferIndex];
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
        EXPECT_EQ(transfer.declaredQueueSharing, Graphics::ResourceQueueSharing::Exclusive);
        EXPECT_EQ(transfer.resourceType, Graphics::GpuGraphResourceType::Texture);
        EXPECT_EQ(transfer.route, Graphics::GpuOwnershipTransferRoute::Internal);
        EXPECT_TRUE(transfer.concurrentSharingCouldAvoid);
        if(transfer.range.textureSubresources == firstRange.textureSubresources)
            ++firstRangeCount;
        if(transfer.range.textureSubresources == secondRange.textureSubresources)
            ++secondRangeCount;
    }
    EXPECT_EQ(firstRangeCount, 1u);
    EXPECT_EQ(secondRangeCount, 1u);
}

TEST(GpuTaskGraph, AdvisesConcurrentSharingForRepeatedExclusiveOwnershipMoves){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId texture = AddTextureMetadata(
        graph,
        Name("tests/task_graph/repeated_ownership_texture"),
        "Repeated Ownership Texture",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Exclusive
    );
    ASSERT_TRUE(texture.valid());

    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
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
    const auto addTask = [&graph, &scheduling, texture](
        const Name& identity,
        const AStringView label,
        const Graphics::GpuQueueRequest& queue,
        const Graphics::GpuTaskResourceAccess::Enum access
    ){
        const Graphics::GpuTaskResourceUse use{
            .resource = texture,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopySource,
            .access = access,
        };
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(queue)
            .setScheduling(scheduling)
            .setResourceUses(&use, 1u)
        ;
        return graph.addTask(desc);
    };
    const Graphics::GpuTaskId firstGraphics = addTask(
        Name("tests/task_graph/repeated_ownership_first_graphics"),
        "Repeated Ownership First Graphics",
        graphicsRequest,
        Graphics::GpuTaskResourceAccess::Write
    );
    const Graphics::GpuTaskId compute = addTask(
        Name("tests/task_graph/repeated_ownership_compute"),
        "Repeated Ownership Compute",
        computeRequest,
        Graphics::GpuTaskResourceAccess::Write
    );
    const Graphics::GpuTaskId secondGraphics = addTask(
        Name("tests/task_graph/repeated_ownership_second_graphics"),
        "Repeated Ownership Second Graphics",
        graphicsRequest,
        Graphics::GpuTaskResourceAccess::Read
    );
    ASSERT_TRUE(firstGraphics.valid());
    ASSERT_TRUE(compute.valid());
    ASSERT_TRUE(secondGraphics.valid());

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


    const Graphics::GpuCompiledTask* const compiledFirstGraphics = compiledPlan.findTask(firstGraphics).plan;
    const Graphics::GpuCompiledTask* const compiledCompute = compiledPlan.findTask(compute).plan;
    const Graphics::GpuCompiledTask* const compiledSecondGraphics = compiledPlan.findTask(secondGraphics).plan;
    ASSERT_NE(compiledFirstGraphics, nullptr);
    ASSERT_NE(compiledCompute, nullptr);
    ASSERT_NE(compiledSecondGraphics, nullptr);
    EXPECT_EQ(compiledFirstGraphics->queue, queues[0u].id);
    EXPECT_EQ(compiledCompute->queue, queues[1u].id);
    EXPECT_EQ(compiledSecondGraphics->queue, queues[0u].id);

    ASSERT_EQ(compiledPlan.logicalOwnershipTransferCount(), 2u);
    const Graphics::GpuCompiledOwnershipTransfer* const ownershipTransfers =
        compiledPlan.logicalOwnershipTransfers()
    ;
    ASSERT_NE(ownershipTransfers, nullptr);
    const Graphics::GpuCompiledOwnershipTransfer* firstMove = nullptr;
    const Graphics::GpuCompiledOwnershipTransfer* secondMove = nullptr;
    for(usize transferIndex = 0u; transferIndex < compiledPlan.logicalOwnershipTransferCount(); ++transferIndex){
        const Graphics::GpuCompiledOwnershipTransfer& transfer = ownershipTransfers[transferIndex];
        EXPECT_TRUE(transfer.valid());
        EXPECT_EQ(transfer.resource, texture);
        EXPECT_EQ(transfer.resourceIdentity, Name("tests/task_graph/repeated_ownership_texture"));
        EXPECT_EQ(transfer.range.textureSubresources, Graphics::s_AllSubresources);
        EXPECT_EQ(transfer.declaredQueueSharing, Graphics::ResourceQueueSharing::Exclusive);
        EXPECT_EQ(transfer.resourceType, Graphics::GpuGraphResourceType::Texture);
        EXPECT_EQ(transfer.route, Graphics::GpuOwnershipTransferRoute::Internal);
        EXPECT_TRUE(transfer.concurrentSharingCouldAvoid);
        if(transfer.sourceTask == firstGraphics && transfer.destinationTask == compute)
            firstMove = &transfer;
        if(transfer.sourceTask == compute && transfer.destinationTask == secondGraphics)
            secondMove = &transfer;
    }
    ASSERT_NE(firstMove, nullptr);
    ASSERT_NE(secondMove, nullptr);
    EXPECT_EQ(firstMove->sourcePacket, compiledFirstGraphics->packet);
    EXPECT_EQ(firstMove->destinationPacket, compiledCompute->packet);
    EXPECT_EQ(firstMove->sourceQueue, queues[0u].id);
    EXPECT_EQ(firstMove->destinationQueue, queues[1u].id);
    EXPECT_EQ(firstMove->sourceQueueFamilyIndex, queues[0u].familyIndex);
    EXPECT_EQ(firstMove->destinationQueueFamilyIndex, queues[1u].familyIndex);
    EXPECT_EQ(secondMove->sourcePacket, compiledCompute->packet);
    EXPECT_EQ(secondMove->destinationPacket, compiledSecondGraphics->packet);
    EXPECT_EQ(secondMove->sourceQueue, queues[1u].id);
    EXPECT_EQ(secondMove->destinationQueue, queues[0u].id);
    EXPECT_EQ(secondMove->sourceQueueFamilyIndex, queues[1u].familyIndex);
    EXPECT_EQ(secondMove->destinationQueueFamilyIndex, queues[0u].familyIndex);

    const Graphics::GpuTaskGraphCompileStatistics& statistics = compiledPlan.compileStatistics();
    ASSERT_TRUE(statistics.valid());
    EXPECT_EQ(statistics.ownershipReleaseBarrierCount, 2u);
    EXPECT_EQ(statistics.ownershipAcquireBarrierCount, 2u);
    EXPECT_EQ(statistics.logicalOwnershipTransferCount, 2u);
    EXPECT_EQ(statistics.logicalOwnershipTransferSignatureCount, 2u);
    EXPECT_EQ(statistics.repeatedOwnershipTransferSignatureCount, 1u);
    EXPECT_EQ(statistics.concurrentSharingCouldAvoidTransferCount, 2u);
    EXPECT_EQ(statistics.concurrentSharingAdviceResourceCount, 1u);
    EXPECT_EQ(
        statistics.logicalOwnershipTransferCountByRoute[Graphics::GpuOwnershipTransferRoute::Internal],
        2u
    );

    const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics graphicsStatistics =
        compiledPlan.physicalQueueCompileStatistics(queues[0u].id)
    ;
    const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics computeStatistics =
        compiledPlan.physicalQueueCompileStatistics(queues[1u].id)
    ;
    ASSERT_TRUE(graphicsStatistics.valid());
    ASSERT_TRUE(computeStatistics.valid());
    EXPECT_EQ(graphicsStatistics.outgoingLogicalOwnershipTransferCount, 1u);
    EXPECT_EQ(graphicsStatistics.incomingLogicalOwnershipTransferCount, 1u);
    EXPECT_EQ(graphicsStatistics.outgoingLogicalOwnershipTransferSignatureCount, 1u);
    EXPECT_EQ(graphicsStatistics.incomingLogicalOwnershipTransferSignatureCount, 1u);
    EXPECT_EQ(graphicsStatistics.outgoingRepeatedOwnershipTransferSignatureCount, 0u);
    EXPECT_EQ(graphicsStatistics.incomingRepeatedOwnershipTransferSignatureCount, 1u);
    EXPECT_EQ(graphicsStatistics.concurrentSharingAdviceResourceCount, 1u);
    EXPECT_EQ(computeStatistics.outgoingLogicalOwnershipTransferCount, 1u);
    EXPECT_EQ(computeStatistics.incomingLogicalOwnershipTransferCount, 1u);
    EXPECT_EQ(computeStatistics.outgoingLogicalOwnershipTransferSignatureCount, 1u);
    EXPECT_EQ(computeStatistics.incomingLogicalOwnershipTransferSignatureCount, 1u);
    EXPECT_EQ(computeStatistics.outgoingRepeatedOwnershipTransferSignatureCount, 1u);
    EXPECT_EQ(computeStatistics.incomingRepeatedOwnershipTransferSignatureCount, 0u);
    EXPECT_EQ(computeStatistics.concurrentSharingAdviceResourceCount, 1u);
}

TEST(GpuTaskGraph, UsesDeclaredTripleQueueSharingForDedicatedTransfer){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const TransferOwnershipPair pair = AddTransferOwnershipPair(
        graph,
        Graphics::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer
    );
    ASSERT_TRUE(pair.texture.valid());
    ASSERT_TRUE(pair.producer.valid());
    ASSERT_TRUE(pair.consumer.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
        DedicatedTransferQueue(),
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


    const Graphics::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(pair.producer).plan;
    const Graphics::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(pair.consumer).plan;
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    EXPECT_EQ(compiledProducer->epilogueBarrierCount, 0u);
    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);
    const Graphics::GpuCompiledBarrier* const dependency = compiledPlan.findTask(pair.consumer).prologueBarriers;
    const Graphics::GpuPacketStateSeed* const stateSeed = compiledPlan.findTask(pair.consumer).prologueStateSeeds;
    ASSERT_NE(dependency, nullptr);
    ASSERT_NE(stateSeed, nullptr);
    EXPECT_EQ(dependency[0u].type, Graphics::GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(dependency[0u].resource, pair.texture);
    EXPECT_EQ(dependency[0u].before, Graphics::ResourceStates::CopySource);
    EXPECT_EQ(dependency[0u].after, Graphics::ResourceStates::CopySource);
    EXPECT_TRUE(dependency[0u].forceMemoryDependency);
    EXPECT_EQ(stateSeed[0u].resource, pair.texture);
    EXPECT_EQ(stateSeed[0u].sourcePacket, compiledProducer->packet);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferCount(), 0u);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransfers(), nullptr);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), nullptr);
    const Graphics::GpuTaskGraphCompileStatistics& statistics = compiledPlan.compileStatistics();
    ASSERT_TRUE(statistics.valid());
    EXPECT_EQ(statistics.ownershipReleaseBarrierCount, 0u);
    EXPECT_EQ(statistics.ownershipAcquireBarrierCount, 0u);
    EXPECT_EQ(statistics.logicalOwnershipTransferCount, 0u);
    EXPECT_EQ(statistics.logicalOwnershipTransferSignatureCount, 0u);
    EXPECT_EQ(statistics.concurrentSharingCouldAvoidTransferCount, 0u);
    EXPECT_EQ(statistics.concurrentSharingAdviceResourceCount, 0u);
}

TEST(GpuTaskGraph, OmitsOwnershipTelemetryForSameFamilyAndSamePhysicalRoutes){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const TransferOwnershipPair pair = AddTransferOwnershipPair(graph, Graphics::ResourceQueueSharing::Exclusive);
    ASSERT_TRUE(pair.texture.valid());
    ASSERT_TRUE(pair.producer.valid());
    ASSERT_TRUE(pair.consumer.valid());

    {
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
        const Graphics::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(pair.producer).plan;
        const Graphics::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(pair.consumer).plan;
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
        Graphics::GpuPhysicalQueueInfo sameFamilyTransferQueue = DedicatedTransferQueue();
        sameFamilyTransferQueue.familyIndex = GraphicsQueue().familyIndex;
        sameFamilyTransferQueue.queueIndex = 1u;
        const Graphics::GpuPhysicalQueueInfo queues[] = {
            GraphicsQueue(),
            sameFamilyTransferQueue,
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
        const Graphics::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(pair.producer).plan;
        const Graphics::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(pair.consumer).plan;
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

TEST(GpuTaskGraph, AcceptsDedicatedTransferClassOnAConcurrentlySharedComputeFamily){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const TransferOwnershipPair pair = AddTransferOwnershipPair(
        graph,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false
    );
    ASSERT_TRUE(pair.texture.valid());
    ASSERT_TRUE(pair.producer.valid());
    ASSERT_TRUE(pair.consumer.valid());

    Graphics::GpuPhysicalQueueInfo transferOnComputeFamily = DedicatedTransferQueue();
    transferOnComputeFamily.familyIndex = DedicatedComputeQueue().familyIndex;
    transferOnComputeFamily.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
        transferOnComputeFamily,
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


    const Graphics::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(pair.consumer);
    const Graphics::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(pair.producer).plan;
    const Graphics::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(pair.consumer).plan;
    ASSERT_NE(consumerAssignment, nullptr);
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    EXPECT_EQ(consumerAssignment->queue, transferOnComputeFamily.id);
    EXPECT_EQ(consumerAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::DedicatedTransfer);
    EXPECT_EQ(compiledProducer->epilogueBarrierCount, 0u);
    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const dependency = compiledPlan.findTask(pair.consumer).prologueBarriers;
    ASSERT_NE(dependency, nullptr);
    EXPECT_EQ(dependency[0u].type, Graphics::GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(dependency[0u].resource, pair.texture);
    EXPECT_EQ(dependency[0u].before, Graphics::ResourceStates::CopySource);
    EXPECT_EQ(dependency[0u].after, Graphics::ResourceStates::CopySource);
    EXPECT_TRUE(dependency[0u].forceMemoryDependency);
}

TEST(GpuTaskGraph, RejectsDedicatedTransferUseOutsideConcurrentSharingContract){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const TransferOwnershipPair pair = AddTransferOwnershipPair(
        graph,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false
    );
    ASSERT_TRUE(pair.texture.valid());
    ASSERT_TRUE(pair.producer.valid());
    ASSERT_TRUE(pair.consumer.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
        DedicatedTransferQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    EXPECT_FALSE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    EXPECT_EQ(
        assignments.diagnostic().status,
        Graphics::GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue
    );
    EXPECT_FALSE(compiledPlan.valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

