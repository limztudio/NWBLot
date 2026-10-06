// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;
constexpr u32 s_ThirdElementIndex = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_queue_routing_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, RoutesOptedInWorkAcrossSameClassPhysicalQueues){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId buffer = AddBufferMetadata(
        graph,
        Name("tests/task_graph/same_class_buffer"),
        "Same Class Buffer"
    );
    ASSERT_TRUE(buffer.valid());

    Graphics::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;

    Graphics::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Graphics::GpuTaskCostHint::Large;
    producerScheduling.allowSameClassQueueRouting = true;
    const Graphics::GpuTaskResourceUse producerUse{
        .resource = buffer,
        .range = {},
        .requiredState = Graphics::ResourceStates::CopyDest,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/same_class_producer"))
        .setMarkerLabel("Same Class Producer")
        .setScheduling(producerScheduling)
        .setResourceUses(&producerUse, 1u)
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc, graphicsCommands);
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskSchedulingHint consumerScheduling;
    consumerScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    consumerScheduling.allowSameClassQueueRouting = true;
    const Graphics::GpuTaskId consumerDependencies[] = { producer };
    const Graphics::GpuTaskResourceUse consumerUse{
        .resource = buffer,
        .range = {},
        .requiredState = Graphics::ResourceStates::CopySource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/same_class_consumer"))
        .setMarkerLabel("Same Class Consumer")
        .setScheduling(consumerScheduling)
        .setDependencies(consumerDependencies, LengthOf(consumerDependencies))
        .setResourceUses(&consumerUse, 1u)
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc, graphicsCommands);
    ASSERT_TRUE(consumer.valid());

    Graphics::GpuPhysicalQueueInfo secondaryGraphicsQueue = GraphicsQueue(1u);
    secondaryGraphicsQueue.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        secondaryGraphicsQueue,
    };
    const Graphics::GpuPhysicalQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    const Graphics::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
    const Graphics::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumer);
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(consumerAssignment, nullptr);
    EXPECT_EQ(producerAssignment->queue, queues[0u].id);
    EXPECT_EQ(consumerAssignment->queue, queues[1u].id);

    const Graphics::GpuSubmissionPacketId producerPacket = compiledPlan.packetForTask(producer);
    const Graphics::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(consumer);
    const Graphics::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
    const Graphics::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumer).plan;
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
    EXPECT_EQ(
        compiledPlan.findTask(consumer).prologueBarriers[0u].type,
        Graphics::GpuCompiledBarrierType::BufferTransition
    );

}

TEST(GpuTaskGraph, RoutesSameClassWorkAroundExternalQueueLoad){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskCommandRequirements commands;
    commands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.allowSameClassQueueRouting = true;
    const Graphics::GpuTaskId task = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/external_load_same_class"),
        "External Load Same Class",
        commands,
        scheduling
    );
    ASSERT_TRUE(task.valid());

    Graphics::GpuPhysicalQueueInfo secondaryGraphicsQueue = GraphicsQueue(1u);
    secondaryGraphicsQueue.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        secondaryGraphicsQueue,
    };
    const Graphics::GpuPhysicalQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    const Graphics::GpuTaskQueueLoad queueLoads[]{
        {
            .queue = queues[0u].id,
            .estimatedCost = 32u,
        },
    };
    Graphics::GpuTaskGraphQueueAssignmentOptions options;
    options.queueLoads = queueLoads;
    options.queueLoadCount = LengthOf(queueLoads);
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, options));
    const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queue, queues[1u].id);
}


TEST(GpuTaskGraph, PreservesLatestDirectDependencyRouteAcrossIncomingAdjacencyOrder){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;

    Graphics::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Graphics::GpuTaskCostHint::Large;
    producerScheduling.allowSameClassQueueRouting = true;
    const Graphics::GpuTaskId first = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/direct_affinity_order_first"),
        "Direct Affinity Order First",
        graphicsCommands,
        producerScheduling
    );
    const Graphics::GpuTaskId second = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/direct_affinity_order_second"),
        "Direct Affinity Order Second",
        graphicsCommands,
        producerScheduling
    );
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());

    Graphics::GpuTaskSchedulingHint consumerScheduling;
    consumerScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    consumerScheduling.allowSameClassQueueRouting = true;
    consumerScheduling.preserveSameClassQueueWithDirectDependency = true;
    const Graphics::GpuTaskId consumerDependencies[] = { second, first };
    const Graphics::GpuTaskId consumer = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/direct_affinity_order_consumer"),
        "Direct Affinity Order Consumer",
        graphicsCommands,
        consumerScheduling,
        {},
        consumerDependencies,
        LengthOf(consumerDependencies)
    );
    ASSERT_TRUE(consumer.valid());

    Graphics::GpuPhysicalQueueInfo auxiliaryGraphicsQueue = GraphicsQueue(1u);
    auxiliaryGraphicsQueue.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        auxiliaryGraphicsQueue,
    };
    const Graphics::GpuPhysicalQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.topologicalOrder().size(), 3u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], first);
    EXPECT_EQ(analysis.topologicalOrder()[1u], second);
    EXPECT_EQ(analysis.topologicalOrder()[s_ThirdElementIndex], consumer);
    const Graphics::GpuTaskGraphSchedulingTaskIndexView producers = analysis.schedulingProducers(consumer);
    ASSERT_EQ(producers.taskCount, s_ExpectedDualCount);
    EXPECT_EQ(producers[0u], second.index);
    EXPECT_EQ(producers[1u], first.index);

    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments));
    const Graphics::GpuTaskQueueAssignment* const firstAssignment = assignments.find(first);
    const Graphics::GpuTaskQueueAssignment* const secondAssignment = assignments.find(second);
    const Graphics::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumer);
    ASSERT_NE(firstAssignment, nullptr);
    ASSERT_NE(secondAssignment, nullptr);
    ASSERT_NE(consumerAssignment, nullptr);
    EXPECT_EQ(firstAssignment->queue, queues[0u].id);
    EXPECT_EQ(secondAssignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_EQ(consumerAssignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_TRUE(consumerAssignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::DirectDependencyAffinity);
}

TEST(GpuTaskGraph, RetainsSameFamilyRoutingWithoutCrossFamilyOptIn){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;

    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.cost = Graphics::GpuTaskCostHint::Large;
    firstScheduling.allowSameClassQueueRouting = true;
    Graphics::GpuTaskSchedulingHint secondScheduling;
    secondScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    secondScheduling.allowSameClassQueueRouting = true;
    const Graphics::GpuTaskId first = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/cross_family_not_opted_first"),
        "Cross Family Not Opted First",
        graphicsCommands,
        firstScheduling
    );
    const Graphics::GpuTaskId second = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/cross_family_not_opted_second"),
        "Cross Family Not Opted Second",
        graphicsCommands,
        secondScheduling
    );
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());

    Graphics::GpuPhysicalQueueInfo secondaryGraphicsQueue = GraphicsQueue(1u);
    secondaryGraphicsQueue.familyIndex = 3u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        secondaryGraphicsQueue,
    };
    const Graphics::GpuPhysicalQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments));

    const Graphics::GpuTaskQueueAssignment* const firstAssignment = assignments.find(first);
    const Graphics::GpuTaskQueueAssignment* const secondAssignment = assignments.find(second);
    ASSERT_NE(firstAssignment, nullptr);
    ASSERT_NE(secondAssignment, nullptr);
    EXPECT_EQ(firstAssignment->queue, queues[0u].id);
    EXPECT_EQ(secondAssignment->queue, queues[0u].id);
}

TEST(GpuTaskGraph, RoutesCrossFamilySameClassWorkWithExclusiveOwnershipHandoffs){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId buffer = AddBufferMetadata(
        graph,
        Name("tests/task_graph/cross_family_same_class_buffer"),
        "Cross Family Same Class Buffer"
    );
    ASSERT_TRUE(buffer.valid());

    Graphics::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;

    Graphics::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Graphics::GpuTaskCostHint::Large;
    producerScheduling.allowSameClassQueueRouting = true;
    producerScheduling.allowCrossFamilySameClassQueueRouting = true;
    const Graphics::GpuTaskResourceUse producerUse{
        .resource = buffer,
        .range = {},
        .requiredState = Graphics::ResourceStates::CopyDest,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/cross_family_same_class_producer"))
        .setMarkerLabel("Cross Family Same Class Producer")
        .setScheduling(producerScheduling)
        .setResourceUses(&producerUse, 1u)
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc, graphicsCommands);
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskSchedulingHint consumerScheduling;
    consumerScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    consumerScheduling.allowSameClassQueueRouting = true;
    consumerScheduling.allowCrossFamilySameClassQueueRouting = true;
    const Graphics::GpuTaskId consumerDependencies[] = { producer };
    const Graphics::GpuTaskResourceUse consumerUse{
        .resource = buffer,
        .range = {},
        .requiredState = Graphics::ResourceStates::CopySource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/cross_family_same_class_consumer"))
        .setMarkerLabel("Cross Family Same Class Consumer")
        .setScheduling(consumerScheduling)
        .setDependencies(consumerDependencies, LengthOf(consumerDependencies))
        .setResourceUses(&consumerUse, 1u)
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc, graphicsCommands);
    ASSERT_TRUE(consumer.valid());

    Graphics::GpuPhysicalQueueInfo secondaryGraphicsQueue = GraphicsQueue(1u);
    secondaryGraphicsQueue.familyIndex = 3u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        secondaryGraphicsQueue,
    };
    const Graphics::GpuPhysicalQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    const Graphics::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
    const Graphics::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumer);
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(consumerAssignment, nullptr);
    EXPECT_EQ(producerAssignment->queue, queues[0u].id);
    EXPECT_EQ(consumerAssignment->queue, queues[1u].id);

    const Graphics::GpuSubmissionPacketId producerPacket = compiledPlan.packetForTask(producer);
    const Graphics::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(consumer);
    const Graphics::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
    const Graphics::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumer).plan;
    ASSERT_TRUE(producerPacket.valid());
    ASSERT_TRUE(consumerPacket.valid());
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    EXPECT_NE(compiledPlan.packet(producerPacket).plan->queue, compiledPlan.packet(consumerPacket).plan->queue);
    ASSERT_EQ(compiledPlan.packet(consumerPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(consumerPacket).dependencies[0u].producer, producerPacket);
    ASSERT_EQ(compiledProducer->epilogueBarrierCount, 1u);
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);
    ASSERT_EQ(compiledConsumer->prologueBarrierCount, s_ExpectedDualCount);

    const Graphics::GpuCompiledBarrier& release = compiledPlan.findTask(producer).epilogueBarriers[0u];
    EXPECT_EQ(release.type, Graphics::GpuCompiledBarrierType::BufferOwnershipRelease);
    EXPECT_EQ(release.resource, buffer);
    EXPECT_EQ(release.sourceQueue, queues[0u].id);
    EXPECT_EQ(release.destinationQueue, queues[1u].id);

    const Graphics::GpuCompiledBarrier* const acquireAndTransition = compiledPlan.findTask(consumer).prologueBarriers;
    ASSERT_NE(acquireAndTransition, nullptr);
    EXPECT_EQ(acquireAndTransition[0u].type, Graphics::GpuCompiledBarrierType::BufferOwnershipAcquire);
    EXPECT_EQ(acquireAndTransition[0u].resource, buffer);
    EXPECT_EQ(acquireAndTransition[0u].sourceQueue, queues[0u].id);
    EXPECT_EQ(acquireAndTransition[0u].destinationQueue, queues[1u].id);
    EXPECT_EQ(acquireAndTransition[1u].type, Graphics::GpuCompiledBarrierType::BufferTransition);

}

TEST(GpuTaskGraph, RoutesCrossFamilySameClassConcurrentGraphicsResourceWithoutOwnershipHandoff){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId buffer = AddBufferMetadata(
        graph,
        Name("tests/task_graph/cross_family_same_class_concurrent_buffer"),
        "Cross Family Same Class Concurrent Buffer",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(buffer.valid());

    Graphics::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;

    Graphics::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Graphics::GpuTaskCostHint::Large;
    producerScheduling.allowSameClassQueueRouting = true;
    producerScheduling.allowCrossFamilySameClassQueueRouting = true;
    const Graphics::GpuTaskResourceUse producerUse{
        .resource = buffer,
        .range = {},
        .requiredState = Graphics::ResourceStates::CopyDest,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/cross_family_same_class_concurrent_producer"))
        .setMarkerLabel("Cross Family Same Class Concurrent Producer")
        .setScheduling(producerScheduling)
        .setResourceUses(&producerUse, 1u)
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc, graphicsCommands);
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskSchedulingHint consumerScheduling;
    consumerScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    consumerScheduling.allowSameClassQueueRouting = true;
    consumerScheduling.allowCrossFamilySameClassQueueRouting = true;
    const Graphics::GpuTaskId consumerDependencies[] = { producer };
    const Graphics::GpuTaskResourceUse consumerUse{
        .resource = buffer,
        .range = {},
        .requiredState = Graphics::ResourceStates::CopySource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/cross_family_same_class_concurrent_consumer"))
        .setMarkerLabel("Cross Family Same Class Concurrent Consumer")
        .setScheduling(consumerScheduling)
        .setDependencies(consumerDependencies, LengthOf(consumerDependencies))
        .setResourceUses(&consumerUse, 1u)
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc, graphicsCommands);
    ASSERT_TRUE(consumer.valid());

    Graphics::GpuPhysicalQueueInfo auxiliaryGraphicsQueue = GraphicsQueue(1u);
    auxiliaryGraphicsQueue.familyIndex = 3u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        auxiliaryGraphicsQueue,
    };
    const Graphics::GpuPhysicalQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    const Graphics::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
    const Graphics::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumer);
    const Graphics::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
    const Graphics::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumer).plan;
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(consumerAssignment, nullptr);
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    EXPECT_EQ(producerAssignment->queue, queues[0u].id);
    EXPECT_EQ(consumerAssignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_EQ(compiledProducer->epilogueBarrierCount, 0u);
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);
    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 1u);
    EXPECT_EQ(
        compiledPlan.findTask(consumer).prologueBarriers[0u].type,
        Graphics::GpuCompiledBarrierType::BufferTransition
    );
}

TEST(GpuTaskGraph, RoutesCrossFamilySameClassComputeAndTransferWorkWithOwnershipHandoffs){
    const auto runCase = [](
        const Graphics::GpuQueueCapability::Mask capabilities,
        const Graphics::GpuPhysicalQueueInfo& primaryQueue,
        const Graphics::GpuPhysicalQueueInfo& auxiliaryQueue,
        const Name& resourceIdentity,
        const Name& producerIdentity,
        const Name& consumerIdentity
    ){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId buffer = AddBufferMetadata(
            graph,
            resourceIdentity,
            "Cross Family Same Class Buffer"
        );
        ASSERT_TRUE(buffer.valid());

        Graphics::GpuTaskCommandRequirements commands;
        commands.requiredCapabilities = capabilities;

        Graphics::GpuTaskSchedulingHint producerScheduling;
        producerScheduling.cost = Graphics::GpuTaskCostHint::Large;
        producerScheduling.allowSameClassQueueRouting = true;
        producerScheduling.allowCrossFamilySameClassQueueRouting = true;
        const Graphics::GpuTaskResourceUse producerUse{
            .resource = buffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        Graphics::GpuTaskDesc producerDesc;
        producerDesc
            .setIdentity(producerIdentity)
            .setMarkerLabel("Cross Family Same Class Producer")
            .setScheduling(producerScheduling)
            .setResourceUses(&producerUse, 1u)
        ;
        const Graphics::GpuTaskId producer = graph.addTask(producerDesc, commands);
        ASSERT_TRUE(producer.valid());

        Graphics::GpuTaskSchedulingHint consumerScheduling;
        consumerScheduling.cost = Graphics::GpuTaskCostHint::Medium;
        consumerScheduling.allowSameClassQueueRouting = true;
        consumerScheduling.allowCrossFamilySameClassQueueRouting = true;
        const Graphics::GpuTaskId consumerDependencies[] = { producer };
        const Graphics::GpuTaskResourceUse consumerUse{
            .resource = buffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopySource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        };
        Graphics::GpuTaskDesc consumerDesc;
        consumerDesc
            .setIdentity(consumerIdentity)
            .setMarkerLabel("Cross Family Same Class Consumer")
            .setScheduling(consumerScheduling)
            .setDependencies(consumerDependencies, LengthOf(consumerDependencies))
            .setResourceUses(&consumerUse, 1u)
        ;
        const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc, commands);
        ASSERT_TRUE(consumer.valid());

        const Graphics::GpuPhysicalQueueInfo queues[] = { primaryQueue, auxiliaryQueue };
        const Graphics::GpuPhysicalQueueTopology topology{
            .queues = queues,
            .queueCount = LengthOf(queues),
        };
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        const Graphics::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
        const Graphics::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumer);
        const Graphics::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
        const Graphics::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumer).plan;
        ASSERT_NE(producerAssignment, nullptr);
        ASSERT_NE(consumerAssignment, nullptr);
        ASSERT_NE(compiledProducer, nullptr);
        ASSERT_NE(compiledConsumer, nullptr);
        EXPECT_EQ(producerAssignment->queue, primaryQueue.id);
        EXPECT_EQ(consumerAssignment->queue, auxiliaryQueue.id);
        EXPECT_EQ(compiledProducer->queue, primaryQueue.id);
        EXPECT_EQ(compiledConsumer->queue, auxiliaryQueue.id);
        ASSERT_EQ(compiledProducer->epilogueBarrierCount, 1u);
        ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);
        ASSERT_EQ(compiledConsumer->prologueBarrierCount, s_ExpectedDualCount);

        const Graphics::GpuCompiledBarrier* const release = compiledPlan.findTask(producer).epilogueBarriers;
        const Graphics::GpuCompiledBarrier* const acquireAndTransition = compiledPlan.findTask(consumer).prologueBarriers;
        ASSERT_NE(release, nullptr);
        ASSERT_NE(acquireAndTransition, nullptr);
        EXPECT_EQ(release[0u].type, Graphics::GpuCompiledBarrierType::BufferOwnershipRelease);
        EXPECT_EQ(release[0u].resource, buffer);
        EXPECT_EQ(release[0u].sourceQueue, primaryQueue.id);
        EXPECT_EQ(release[0u].destinationQueue, auxiliaryQueue.id);
        EXPECT_EQ(acquireAndTransition[0u].type, Graphics::GpuCompiledBarrierType::BufferOwnershipAcquire);
        EXPECT_EQ(acquireAndTransition[0u].resource, buffer);
        EXPECT_EQ(acquireAndTransition[0u].sourceQueue, primaryQueue.id);
        EXPECT_EQ(acquireAndTransition[0u].destinationQueue, auxiliaryQueue.id);
        EXPECT_EQ(acquireAndTransition[1u].type, Graphics::GpuCompiledBarrierType::BufferTransition);
    };

    Graphics::GpuPhysicalQueueInfo auxiliaryCompute = DedicatedComputeQueue(3u);
    auxiliaryCompute.familyIndex = 3u;
    runCase(
        Graphics::GpuQueueCapability::Compute,
        DedicatedComputeQueue(),
        auxiliaryCompute,
        Name("tests/task_graph/cross_family_same_class_compute_buffer"),
        Name("tests/task_graph/cross_family_same_class_compute_producer"),
        Name("tests/task_graph/cross_family_same_class_compute_consumer")
    );

    Graphics::GpuPhysicalQueueInfo auxiliaryTransfer = DedicatedTransferQueue(4u);
    auxiliaryTransfer.familyIndex = 4u;
    runCase(
        Graphics::GpuQueueCapability::Transfer,
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

