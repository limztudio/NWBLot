// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_queue_routing_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_CrossFamilyBufferLabel = "Cross Family Same Class Buffer";
constexpr AStringView s_CrossFamilyProducerLabel = "Cross Family Same Class Producer";
constexpr AStringView s_CrossFamilyConsumerLabel = "Cross Family Same Class Consumer";

using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, RoutesOptedInWorkAcrossSameClassPhysicalQueues){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto bufferResult = AddBufferMetadata(graph, Name("tests/task_graph/same_class_buffer"), "Same Class Buffer");
    ASSERT_TRUE(bufferResult);
    const Core::GpuGraphResourceId buffer = *bufferResult;
    ASSERT_TRUE(buffer.valid());

    Core::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Core::GpuQueueCapability::Graphics;

    Core::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Core::GpuTaskCostHint::Large;
    producerScheduling.allowSameClassQueueRouting = true;
    const Core::GpuTaskResourceUse producerUse{ .resource = buffer, .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write, };
    Core::GpuTaskDesc producerDesc;
    producerDesc.setIdentity(Name("tests/task_graph/same_class_producer")).setMarkerLabel("Same Class Producer").setScheduling(producerScheduling).setResourceUses(Span(&producerUse, 1u));
    const auto producerResult = graph.addTask(producerDesc, graphicsCommands);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;
    ASSERT_TRUE(producer.valid());

    Core::GpuTaskSchedulingHint consumerScheduling;
    consumerScheduling.cost = Core::GpuTaskCostHint::Medium;
    consumerScheduling.allowSameClassQueueRouting = true;
    const Array<Core::GpuTaskId, 1u> consumerDependencies = { producer };
    const Core::GpuTaskResourceUse consumerUse{ .resource = buffer, .range = {}, .requiredState = Core::ResourceStates::CopySource, .access = Core::GpuTaskResourceAccess::Read, };
    Core::GpuTaskDesc consumerDesc;
    consumerDesc.setIdentity(Name("tests/task_graph/same_class_consumer")).setMarkerLabel("Same Class Consumer").setScheduling(consumerScheduling).setDependencies(consumerDependencies).setResourceUses(Span(&consumerUse, 1u));
    const auto consumerResult = graph.addTask(consumerDesc, graphicsCommands);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());

    Core::GpuPhysicalQueueInfo secondaryGraphicsQueue = GraphicsQueue(1u);
    secondaryGraphicsQueue.queueIndex = 1u;
    const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), secondaryGraphicsQueue, };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    const Core::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
    const Core::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumer);
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(consumerAssignment, nullptr);
    EXPECT_EQ(producerAssignment->queue, queues[0u].id);
    EXPECT_EQ(consumerAssignment->queue, queues[1u].id);

    const Core::GpuSubmissionPacketId producerPacket = compiledPlan.packetForTask(producer);
    const Core::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(consumer);
    const Core::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
    const Core::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumer).plan;
    ASSERT_TRUE(producerPacket.valid());
    ASSERT_TRUE(consumerPacket.valid());
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    EXPECT_NE(compiledPlan.packet(producerPacket).plan->queue, compiledPlan.packet(consumerPacket).plan->queue);
    ASSERT_EQ(compiledPlan.packet(consumerPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(consumerPacket).dependencies[0u].producer, producerPacket);
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);
    EXPECT_EQ(compiledProducer->epilogueBarrierCount, 0u);
    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 1u);
    EXPECT_EQ(compiledPlan.findTask(consumer).prologueBarriers[0u].type, Core::GpuCompiledBarrierType::BufferTransition);

}

TEST(GpuTaskGraph, PreservesLatestDirectDependencyRouteAcrossIncomingAdjacencyOrder){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);

    Core::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Core::GpuQueueCapability::Graphics;

    Core::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Core::GpuTaskCostHint::Large;
    producerScheduling.allowSameClassQueueRouting = true;
    const auto firstResult = AddTaskWithCommands(Name("tests/task_graph/direct_affinity_order_first"), "Direct Affinity Order First", graphicsCommands, producerScheduling, {}, {}, graph);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    const auto secondResult = AddTaskWithCommands(Name("tests/task_graph/direct_affinity_order_second"), "Direct Affinity Order Second", graphicsCommands, producerScheduling, {}, {}, graph);
    ASSERT_TRUE(secondResult);
    const Core::GpuTaskId second = *secondResult;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());

    Core::GpuTaskSchedulingHint consumerScheduling;
    consumerScheduling.cost = Core::GpuTaskCostHint::Tiny;
    consumerScheduling.allowSameClassQueueRouting = true;
    consumerScheduling.preserveSameClassQueueWithDirectDependency = true;
    const Array<Core::GpuTaskId, 2u> consumerDependencies = { second, first };
    const auto consumerResult = AddTaskWithCommands(Name("tests/task_graph/direct_affinity_order_consumer"), "Direct Affinity Order Consumer", graphicsCommands, consumerScheduling, {}, Span(consumerDependencies), graph);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());

    Core::GpuPhysicalQueueInfo auxiliaryGraphicsQueue = GraphicsQueue(1u);
    auxiliaryGraphicsQueue.queueIndex = 1u;
    const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), auxiliaryGraphicsQueue, };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    ASSERT_EQ(analysis.topologicalOrder().size(), 3u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], first);
    EXPECT_EQ(analysis.topologicalOrder()[1u], second);
    EXPECT_EQ(analysis.topologicalOrder()[2u], consumer);
    const Core::GpuTaskGraphSchedulingTaskIndexView producers = analysis.schedulingProducers(consumer);
    ASSERT_EQ(producers.taskCount, 2u);
    EXPECT_EQ(producers[0u], second.index);
    EXPECT_EQ(producers[1u], first.index);

    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology));
    const Core::GpuTaskQueueAssignment* const firstAssignment = assignments.find(first);
    const Core::GpuTaskQueueAssignment* const secondAssignment = assignments.find(second);
    const Core::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumer);
    ASSERT_NE(firstAssignment, nullptr);
    ASSERT_NE(secondAssignment, nullptr);
    ASSERT_NE(consumerAssignment, nullptr);
    EXPECT_EQ(firstAssignment->queue, queues[0u].id);
    EXPECT_EQ(secondAssignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_EQ(consumerAssignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_TRUE(consumerAssignment->modifiers & Core::GpuTaskQueueAssignmentModifier::DirectDependencyAffinity);
}

TEST(GpuTaskGraph, RetainsSameFamilyRoutingWithoutCrossFamilyOptIn){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);

    Core::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Core::GpuQueueCapability::Graphics;

    Core::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.cost = Core::GpuTaskCostHint::Large;
    firstScheduling.allowSameClassQueueRouting = true;
    Core::GpuTaskSchedulingHint secondScheduling;
    secondScheduling.cost = Core::GpuTaskCostHint::Medium;
    secondScheduling.allowSameClassQueueRouting = true;
    const auto firstResult = AddTaskWithCommands(Name("tests/task_graph/cross_family_not_opted_first"), "Cross Family Not Opted First", graphicsCommands, firstScheduling, {}, {}, graph);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    const auto secondResult = AddTaskWithCommands(Name("tests/task_graph/cross_family_not_opted_second"), "Cross Family Not Opted Second", graphicsCommands, secondScheduling, {}, {}, graph);
    ASSERT_TRUE(secondResult);
    const Core::GpuTaskId second = *secondResult;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());

    Core::GpuPhysicalQueueInfo secondaryGraphicsQueue = GraphicsQueue(1u);
    secondaryGraphicsQueue.familyIndex = 3u;
    const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), secondaryGraphicsQueue, };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology));

    const Core::GpuTaskQueueAssignment* const firstAssignment = assignments.find(first);
    const Core::GpuTaskQueueAssignment* const secondAssignment = assignments.find(second);
    ASSERT_NE(firstAssignment, nullptr);
    ASSERT_NE(secondAssignment, nullptr);
    EXPECT_EQ(firstAssignment->queue, queues[0u].id);
    EXPECT_EQ(secondAssignment->queue, queues[0u].id);
}

TEST(GpuTaskGraph, RoutesCrossFamilySameClassConcurrentGraphicsResourceWithoutOwnershipHandoff){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto bufferResult = AddBufferMetadata(graph, Name("tests/task_graph/cross_family_same_class_concurrent_buffer"), "Cross Family Same Class Concurrent Buffer", Core::ResourceStates::Common, Core::ResourceQueueSharing::Graphics);
    ASSERT_TRUE(bufferResult);
    const Core::GpuGraphResourceId buffer = *bufferResult;
    ASSERT_TRUE(buffer.valid());

    Core::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Core::GpuQueueCapability::Graphics;

    Core::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Core::GpuTaskCostHint::Large;
    producerScheduling.allowSameClassQueueRouting = true;
    producerScheduling.allowCrossFamilySameClassQueueRouting = true;
    const Core::GpuTaskResourceUse producerUse{ .resource = buffer, .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write, };
    Core::GpuTaskDesc producerDesc;
    producerDesc.setIdentity(Name("tests/task_graph/cross_family_same_class_concurrent_producer")).setMarkerLabel("Cross Family Same Class Concurrent Producer").setScheduling(producerScheduling).setResourceUses(Span(&producerUse, 1u));
    const auto producerResult = graph.addTask(producerDesc, graphicsCommands);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;
    ASSERT_TRUE(producer.valid());

    Core::GpuTaskSchedulingHint consumerScheduling;
    consumerScheduling.cost = Core::GpuTaskCostHint::Medium;
    consumerScheduling.allowSameClassQueueRouting = true;
    consumerScheduling.allowCrossFamilySameClassQueueRouting = true;
    const Array<Core::GpuTaskId, 1u> consumerDependencies = { producer };
    const Core::GpuTaskResourceUse consumerUse{ .resource = buffer, .range = {}, .requiredState = Core::ResourceStates::CopySource, .access = Core::GpuTaskResourceAccess::Read, };
    Core::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/cross_family_same_class_concurrent_consumer"))
        .setMarkerLabel("Cross Family Same Class Concurrent Consumer")
        .setScheduling(consumerScheduling)
        .setDependencies(consumerDependencies)
        .setResourceUses(Span(&consumerUse, 1u))
    ;
    const auto consumerResult = graph.addTask(consumerDesc, graphicsCommands);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());

    Core::GpuPhysicalQueueInfo auxiliaryGraphicsQueue = GraphicsQueue(1u);
    auxiliaryGraphicsQueue.familyIndex = 3u;
    const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), auxiliaryGraphicsQueue, };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    const Core::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
    const Core::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumer);
    const Core::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
    const Core::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumer).plan;
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(consumerAssignment, nullptr);
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    EXPECT_EQ(producerAssignment->queue, queues[0u].id);
    EXPECT_EQ(consumerAssignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_EQ(compiledProducer->epilogueBarrierCount, 0u);
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);
    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 1u);
    EXPECT_EQ(compiledPlan.findTask(consumer).prologueBarriers[0u].type, Core::GpuCompiledBarrierType::BufferTransition);
}

TEST(GpuTaskGraph, RoutesCrossFamilySameClassComputeAndTransferWorkWithOwnershipHandoffs){
    const auto runCase = [](
        const Core::GpuQueueCapability::Mask capabilities,
        const Core::GpuPhysicalQueueInfo& primaryQueue,
        const Core::GpuPhysicalQueueInfo& auxiliaryQueue,
        const Name& resourceIdentity,
        const Name& producerIdentity,
        const Name& consumerIdentity
    ){
        Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const auto bufferResult = AddBufferMetadata(graph, resourceIdentity, s_CrossFamilyBufferLabel);
        ASSERT_TRUE(bufferResult);
        const Core::GpuGraphResourceId buffer = *bufferResult;
        ASSERT_TRUE(buffer.valid());

        Core::GpuTaskCommandRequirements commands;
        commands.requiredCapabilities = capabilities;

        Core::GpuTaskSchedulingHint producerScheduling;
        producerScheduling.cost = Core::GpuTaskCostHint::Large;
        producerScheduling.allowSameClassQueueRouting = true;
        producerScheduling.allowCrossFamilySameClassQueueRouting = true;
        const Core::GpuTaskResourceUse producerUse{ .resource = buffer, .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write, };
        Core::GpuTaskDesc producerDesc;
        producerDesc.setIdentity(producerIdentity).setMarkerLabel(s_CrossFamilyProducerLabel).setScheduling(producerScheduling).setResourceUses(Span(&producerUse, 1u));
        const auto producerResult = graph.addTask(producerDesc, commands);
        ASSERT_TRUE(producerResult);
        const Core::GpuTaskId producer = *producerResult;
        ASSERT_TRUE(producer.valid());

        Core::GpuTaskSchedulingHint consumerScheduling;
        consumerScheduling.cost = Core::GpuTaskCostHint::Medium;
        consumerScheduling.allowSameClassQueueRouting = true;
        consumerScheduling.allowCrossFamilySameClassQueueRouting = true;
        const Array<Core::GpuTaskId, 1u> consumerDependencies = { producer };
        const Core::GpuTaskResourceUse consumerUse{ .resource = buffer, .range = {}, .requiredState = Core::ResourceStates::CopySource, .access = Core::GpuTaskResourceAccess::Read, };
        Core::GpuTaskDesc consumerDesc;
        consumerDesc.setIdentity(consumerIdentity).setMarkerLabel(s_CrossFamilyConsumerLabel).setScheduling(consumerScheduling).setDependencies(consumerDependencies).setResourceUses(Span(&consumerUse, 1u));
        const auto consumerResult = graph.addTask(consumerDesc, commands);
        ASSERT_TRUE(consumerResult);
        const Core::GpuTaskId consumer = *consumerResult;
        ASSERT_TRUE(consumer.valid());

        const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { primaryQueue, auxiliaryQueue };
        const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Core::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        const Core::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
        const Core::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumer);
        const Core::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
        const Core::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumer).plan;
        ASSERT_NE(producerAssignment, nullptr);
        ASSERT_NE(consumerAssignment, nullptr);
        ASSERT_NE(compiledProducer, nullptr);
        ASSERT_NE(compiledConsumer, nullptr);
        EXPECT_EQ(producerAssignment->queue, primaryQueue.id);
        EXPECT_EQ(consumerAssignment->queue, auxiliaryQueue.id);
        const Core::GpuSubmissionPacketId producerPacket = compiledPlan.packetForTask(producer);
        const Core::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(consumer);
        ASSERT_TRUE(producerPacket.valid());
        ASSERT_TRUE(consumerPacket.valid());
        EXPECT_NE(compiledPlan.packet(producerPacket).plan->queue, compiledPlan.packet(consumerPacket).plan->queue);
        ASSERT_EQ(compiledPlan.packet(consumerPacket).plan->dependencyCount, 1u);
        EXPECT_EQ(compiledPlan.packet(consumerPacket).dependencies[0u].producer, producerPacket);
        EXPECT_EQ(compiledProducer->queue, primaryQueue.id);
        EXPECT_EQ(compiledConsumer->queue, auxiliaryQueue.id);
        ASSERT_EQ(compiledProducer->epilogueBarrierCount, 1u);
        ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);
        ASSERT_EQ(compiledConsumer->prologueBarrierCount, 2u);

        const Core::GpuCompiledBarrier* const release = compiledPlan.findTask(producer).epilogueBarriers;
        const Core::GpuCompiledBarrier* const acquireAndTransition = compiledPlan.findTask(consumer).prologueBarriers;
        ASSERT_NE(release, nullptr);
        ASSERT_NE(acquireAndTransition, nullptr);
        EXPECT_EQ(release[0u].type, Core::GpuCompiledBarrierType::BufferOwnershipRelease);
        EXPECT_EQ(release[0u].resource, buffer);
        EXPECT_EQ(release[0u].sourceQueue, primaryQueue.id);
        EXPECT_EQ(release[0u].destinationQueue, auxiliaryQueue.id);
        EXPECT_EQ(acquireAndTransition[0u].type, Core::GpuCompiledBarrierType::BufferOwnershipAcquire);
        EXPECT_EQ(acquireAndTransition[0u].resource, buffer);
        EXPECT_EQ(acquireAndTransition[0u].sourceQueue, primaryQueue.id);
        EXPECT_EQ(acquireAndTransition[0u].destinationQueue, auxiliaryQueue.id);
        EXPECT_EQ(acquireAndTransition[1u].type, Core::GpuCompiledBarrierType::BufferTransition);
    };

    Core::GpuPhysicalQueueInfo auxiliaryGraphics = GraphicsQueue(1u);
    auxiliaryGraphics.familyIndex = 3u;
    runCase(
        Core::GpuQueueCapability::Graphics,
        GraphicsQueue(),
        auxiliaryGraphics,
        Name("tests/task_graph/cross_family_same_class_buffer"),
        Name("tests/task_graph/cross_family_same_class_producer"),
        Name("tests/task_graph/cross_family_same_class_consumer")
    );

    Core::GpuPhysicalQueueInfo auxiliaryCompute = DedicatedComputeQueue(3u);
    auxiliaryCompute.familyIndex = 3u;
    runCase(
        Core::GpuQueueCapability::Compute,
        DedicatedComputeQueue(),
        auxiliaryCompute,
        Name("tests/task_graph/cross_family_same_class_compute_buffer"),
        Name("tests/task_graph/cross_family_same_class_compute_producer"),
        Name("tests/task_graph/cross_family_same_class_compute_consumer")
    );

    Core::GpuPhysicalQueueInfo auxiliaryTransfer = DedicatedTransferQueue(4u);
    auxiliaryTransfer.familyIndex = 4u;
    runCase(
        Core::GpuQueueCapability::Transfer,
        DedicatedTransferQueue(),
        auxiliaryTransfer,
        Name("tests/task_graph/cross_family_same_class_transfer_buffer"),
        Name("tests/task_graph/cross_family_same_class_transfer_producer"),
        Name("tests/task_graph/cross_family_same_class_transfer_consumer")
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

