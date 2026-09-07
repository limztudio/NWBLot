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

    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;

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
        .setQueue(graphicsRequest)
        .setScheduling(producerScheduling)
        .setResourceUses(&producerUse, 1u)
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
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
        .setQueue(graphicsRequest)
        .setScheduling(consumerScheduling)
        .setDependencies(consumerDependencies, LengthOf(consumerDependencies))
        .setResourceUses(&consumerUse, 1u)
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(consumer.valid());

    Graphics::GpuPhysicalQueueInfo secondaryGraphicsQueue = GraphicsQueue(1u);
    secondaryGraphicsQueue.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        secondaryGraphicsQueue,
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


    const Graphics::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
    const Graphics::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumer);
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(consumerAssignment, nullptr);
    EXPECT_EQ(producerAssignment->queue, queues[0u].id);
    EXPECT_EQ(consumerAssignment->queue, queues[1u].id);
    EXPECT_EQ(consumerAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_TRUE(consumerAssignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::SameClassLoadBalance);

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

    const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics primaryQueueCompileStatistics =
        compiledPlan.physicalQueueCompileStatistics(queues[0u].id)
    ;
    const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics secondaryQueueCompileStatistics =
        compiledPlan.physicalQueueCompileStatistics(queues[1u].id)
    ;
    ASSERT_TRUE(primaryQueueCompileStatistics.valid());
    ASSERT_TRUE(secondaryQueueCompileStatistics.valid());
    EXPECT_EQ(primaryQueueCompileStatistics.queue, queues[0u].id);
    EXPECT_EQ(secondaryQueueCompileStatistics.queue, queues[1u].id);
    EXPECT_EQ(primaryQueueCompileStatistics.queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(secondaryQueueCompileStatistics.queueClass, Graphics::CommandQueue::Graphics);
    const Graphics::GpuTaskGraphCompileStatistics& compileStatistics = compiledPlan.compileStatistics();
    EXPECT_EQ(
        primaryQueueCompileStatistics.taskCount + secondaryQueueCompileStatistics.taskCount,
        compileStatistics.taskCount
    );
    EXPECT_EQ(
        primaryQueueCompileStatistics.taskCount + secondaryQueueCompileStatistics.taskCount,
        compileStatistics.taskCountByQueueClass[Graphics::CommandQueue::Graphics]
    );
    EXPECT_EQ(
        primaryQueueCompileStatistics.packetCount + secondaryQueueCompileStatistics.packetCount,
        compileStatistics.packetCount
    );
    EXPECT_EQ(
        primaryQueueCompileStatistics.packetCount + secondaryQueueCompileStatistics.packetCount,
        compileStatistics.packetCountByQueueClass[Graphics::CommandQueue::Graphics]
    );
    EXPECT_EQ(
        primaryQueueCompileStatistics.mergedTaskCount + secondaryQueueCompileStatistics.mergedTaskCount,
        compileStatistics.mergedTaskCount
    );
    EXPECT_EQ(
        primaryQueueCompileStatistics.prologueBarrierCount + secondaryQueueCompileStatistics.prologueBarrierCount,
        compileStatistics.prologueBarrierCount
    );
    EXPECT_EQ(
        primaryQueueCompileStatistics.epilogueBarrierCount + secondaryQueueCompileStatistics.epilogueBarrierCount,
        compileStatistics.epilogueBarrierCount
    );
}

TEST(GpuTaskGraph, BalancesAcrossAllRegisteredSameClassPhysicalQueues){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;

    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Large;
    scheduling.allowSameClassQueueRouting = true;
    const Graphics::GpuTaskId tasks[] = {
        AddTaskWithQueue(
            graph,
            Name("tests/task_graph/multi_auxiliary_same_class_first"),
            "Multi Auxiliary Same Class First",
            graphicsRequest,
            scheduling
        ),
        AddTaskWithQueue(
            graph,
            Name("tests/task_graph/multi_auxiliary_same_class_second"),
            "Multi Auxiliary Same Class Second",
            graphicsRequest,
            scheduling
        ),
        AddTaskWithQueue(
            graph,
            Name("tests/task_graph/multi_auxiliary_same_class_third"),
            "Multi Auxiliary Same Class Third",
            graphicsRequest,
            scheduling
        ),
        AddTaskWithQueue(
            graph,
            Name("tests/task_graph/multi_auxiliary_same_class_fourth"),
            "Multi Auxiliary Same Class Fourth",
            graphicsRequest,
            scheduling
        ),
    };
    for(const Graphics::GpuTaskId task : tasks)
        ASSERT_TRUE(task.valid());

    Graphics::GpuPhysicalQueueInfo firstAuxiliary = GraphicsQueue(1u);
    firstAuxiliary.queueIndex = 1u;
    Graphics::GpuPhysicalQueueInfo secondAuxiliary = GraphicsQueue(2u);
    secondAuxiliary.queueIndex = 2u;
    Graphics::GpuPhysicalQueueInfo thirdAuxiliary = GraphicsQueue(3u);
    thirdAuxiliary.queueIndex = 3u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        firstAuxiliary,
        secondAuxiliary,
        thirdAuxiliary,
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(tasks[taskIndex]);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queues[taskIndex].id);
        EXPECT_EQ(assignment->reason, Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics);
        EXPECT_EQ(
            static_cast<bool>(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::SameClassLoadBalance),
            taskIndex != 0u
        );
    }
}

TEST(GpuTaskGraph, BalancesAcrossAllRegisteredDedicatedSameClassPhysicalQueues){
    const auto runCase = [](
        const Graphics::GpuQueueCapability::Mask requiredCapabilities,
        const Graphics::GpuQueuePreference::Enum preference,
        const Graphics::GpuPhysicalQueueInfo& primary,
        const Graphics::GpuPhysicalQueueInfo& firstAuxiliary,
        const Graphics::GpuPhysicalQueueInfo& secondAuxiliary,
        const Graphics::GpuPhysicalQueueInfo& thirdAuxiliary,
        const Graphics::GpuTaskQueueAssignmentReason::Enum primaryReason
    ){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);

        Graphics::GpuQueueRequest queueRequest;
        queueRequest.requiredCapabilities = requiredCapabilities;
        queueRequest.preferredQueue = preference;
        queueRequest.allowFallback = false;
        queueRequest.compilerMayOverridePreference = false;

        Graphics::GpuTaskSchedulingHint scheduling;
        scheduling.cost = Graphics::GpuTaskCostHint::Large;
        scheduling.allowSameClassQueueRouting = true;
        const Graphics::GpuTaskId tasks[] = {
            AddTaskWithQueue(graph, Name("tests/task_graph/multi_auxiliary_dedicated_first"), "Multi Auxiliary Dedicated First", queueRequest, scheduling),
            AddTaskWithQueue(graph, Name("tests/task_graph/multi_auxiliary_dedicated_second"), "Multi Auxiliary Dedicated Second", queueRequest, scheduling),
            AddTaskWithQueue(graph, Name("tests/task_graph/multi_auxiliary_dedicated_third"), "Multi Auxiliary Dedicated Third", queueRequest, scheduling),
            AddTaskWithQueue(graph, Name("tests/task_graph/multi_auxiliary_dedicated_fourth"), "Multi Auxiliary Dedicated Fourth", queueRequest, scheduling),
        };
        for(const Graphics::GpuTaskId task : tasks)
            ASSERT_TRUE(task.valid());

        const Graphics::GpuPhysicalQueueInfo queues[] = {
            primary,
            firstAuxiliary,
            secondAuxiliary,
            thirdAuxiliary,
        };
        const Graphics::GpuTaskGraphQueueTopology topology{
            .queues = queues,
            .queueCount = LengthOf(queues),
        };
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

        for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
            const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(tasks[taskIndex]);
            ASSERT_NE(assignment, nullptr);
            EXPECT_EQ(assignment->queue, queues[taskIndex].id);
            EXPECT_EQ(assignment->reason, primaryReason);
            EXPECT_EQ(
                static_cast<bool>(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::SameClassLoadBalance),
                taskIndex != 0u
            );
        }
    };

    Graphics::GpuPhysicalQueueInfo firstComputeAuxiliary = DedicatedComputeQueue(2u);
    firstComputeAuxiliary.queueIndex = 1u;
    Graphics::GpuPhysicalQueueInfo secondComputeAuxiliary = DedicatedComputeQueue(3u);
    secondComputeAuxiliary.queueIndex = 2u;
    Graphics::GpuPhysicalQueueInfo thirdComputeAuxiliary = DedicatedComputeQueue(4u);
    thirdComputeAuxiliary.queueIndex = 3u;
    runCase(
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        DedicatedComputeQueue(),
        firstComputeAuxiliary,
        secondComputeAuxiliary,
        thirdComputeAuxiliary,
        Graphics::GpuTaskQueueAssignmentReason::DedicatedCompute
    );

    Graphics::GpuPhysicalQueueInfo firstTransferAuxiliary = DedicatedTransferQueue(3u);
    firstTransferAuxiliary.queueIndex = 1u;
    Graphics::GpuPhysicalQueueInfo secondTransferAuxiliary = DedicatedTransferQueue(4u);
    secondTransferAuxiliary.queueIndex = 2u;
    Graphics::GpuPhysicalQueueInfo thirdTransferAuxiliary = DedicatedTransferQueue(5u);
    thirdTransferAuxiliary.queueIndex = 3u;
    runCase(
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Transfer,
        DedicatedTransferQueue(),
        firstTransferAuxiliary,
        secondTransferAuxiliary,
        thirdTransferAuxiliary,
        Graphics::GpuTaskQueueAssignmentReason::DedicatedTransfer
    );
}

TEST(GpuTaskGraph, RoutesIsolatedOffloadToAuxiliaryAndReturnsPrimaryBridge){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Transfer;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;

    Graphics::GpuTaskSchedulingHint uploadScheduling;
    uploadScheduling.cost = Graphics::GpuTaskCostHint::Large;
    uploadScheduling.allowSameClassQueueRouting = true;
    uploadScheduling.preferNonPrimarySameClassQueue = true;
    uploadScheduling.allowCrossFamilySameClassQueueRouting = true;
    Graphics::GpuTaskDesc uploadDesc;
    uploadDesc
        .setIdentity(Name("tests/task_graph/isolated_same_class_offload"))
        .setMarkerLabel("Isolated Same Class Offload")
        .setQueue(graphicsRequest)
        .setScheduling(uploadScheduling)
    ;
    const Graphics::GpuTaskId upload = graph.addTask(uploadDesc);
    ASSERT_TRUE(upload.valid());

    Graphics::GpuTaskSchedulingHint bridgeScheduling;
    bridgeScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    bridgeScheduling.overlapPreferred = false;
    bridgeScheduling.forceSubmissionBoundary = true;
    bridgeScheduling.allowPacketMerge = false;
    const Graphics::GpuTaskId bridgeDependencies[] = { upload };
    Graphics::GpuTaskDesc bridgeDesc;
    bridgeDesc
        .setIdentity(Name("tests/task_graph/isolated_same_class_primary_bridge"))
        .setMarkerLabel("Isolated Same Class Primary Bridge")
        .setQueue(graphicsRequest)
        .setScheduling(bridgeScheduling)
        .setDependencies(bridgeDependencies, LengthOf(bridgeDependencies))
    ;
    const Graphics::GpuTaskId bridge = graph.addTask(bridgeDesc);
    ASSERT_TRUE(bridge.valid());

    Graphics::GpuPhysicalQueueInfo auxiliaryGraphicsQueue = GraphicsQueue(1u);
    auxiliaryGraphicsQueue.familyIndex = 3u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        auxiliaryGraphicsQueue,
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments));

    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    const Graphics::GpuTaskQueueAssignment* const uploadAssignment = assignments.find(upload);
    const Graphics::GpuTaskQueueAssignment* const bridgeAssignment = assignments.find(bridge);
    ASSERT_NE(uploadAssignment, nullptr);
    ASSERT_NE(bridgeAssignment, nullptr);
    EXPECT_EQ(uploadAssignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_EQ(uploadAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::PreferredQueue);
    EXPECT_TRUE(uploadAssignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::SameClassLoadBalance);
    EXPECT_TRUE(uploadAssignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::NonPrimaryPreference);
    EXPECT_EQ(bridgeAssignment->queue, queues[0u].id);

    const Graphics::GpuSubmissionPacketId uploadPacket = compiledPlan.packetForTask(upload);
    const Graphics::GpuSubmissionPacketId bridgePacket = compiledPlan.packetForTask(bridge);
    ASSERT_TRUE(uploadPacket.valid());
    ASSERT_TRUE(bridgePacket.valid());
    ASSERT_EQ(compiledPlan.packet(bridgePacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(bridgePacket).dependencies[0u].producer, uploadPacket);
}

TEST(GpuTaskGraph, PreservesAuxiliarySameClassQueueAcrossSerialOffloadChain){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Transfer;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;

    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.cost = Graphics::GpuTaskCostHint::Large;
    firstScheduling.allowSameClassQueueRouting = true;
    firstScheduling.preferNonPrimarySameClassQueue = true;
    firstScheduling.allowCrossFamilySameClassQueueRouting = true;
    firstScheduling.forceSubmissionBoundary = true;
    firstScheduling.allowPacketMerge = false;
    const Graphics::GpuTaskId first = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/serial_same_class_offload_first"),
        "Serial Same Class Offload First",
        graphicsRequest,
        firstScheduling
    );
    ASSERT_TRUE(first.valid());

    Graphics::GpuTaskSchedulingHint secondScheduling = firstScheduling;
    secondScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    secondScheduling.preferNonPrimarySameClassQueue = false;
    secondScheduling.preserveSameClassQueueWithDirectDependency = true;
    const Graphics::GpuTaskId secondDependencies[] = { first };
    Graphics::GpuTaskDesc secondDesc;
    secondDesc
        .setIdentity(Name("tests/task_graph/serial_same_class_offload_second"))
        .setMarkerLabel("Serial Same Class Offload Second")
        .setQueue(graphicsRequest)
        .setScheduling(secondScheduling)
        .setDependencies(secondDependencies, LengthOf(secondDependencies))
    ;
    const Graphics::GpuTaskId second = graph.addTask(secondDesc);
    ASSERT_TRUE(second.valid());

    Graphics::GpuTaskSchedulingHint bridgeScheduling;
    bridgeScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    bridgeScheduling.overlapPreferred = false;
    bridgeScheduling.forceSubmissionBoundary = true;
    bridgeScheduling.allowPacketMerge = false;
    const Graphics::GpuTaskId bridgeDependencies[] = { second };
    Graphics::GpuTaskDesc bridgeDesc;
    bridgeDesc
        .setIdentity(Name("tests/task_graph/serial_same_class_primary_bridge"))
        .setMarkerLabel("Serial Same Class Primary Bridge")
        .setQueue(graphicsRequest)
        .setScheduling(bridgeScheduling)
        .setDependencies(bridgeDependencies, LengthOf(bridgeDependencies))
    ;
    const Graphics::GpuTaskId bridge = graph.addTask(bridgeDesc);
    ASSERT_TRUE(bridge.valid());

    Graphics::GpuPhysicalQueueInfo auxiliaryGraphicsQueue = GraphicsQueue(1u);
    auxiliaryGraphicsQueue.familyIndex = 3u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        auxiliaryGraphicsQueue,
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


    const Graphics::GpuTaskQueueAssignment* const firstAssignment = assignments.find(first);
    const Graphics::GpuTaskQueueAssignment* const secondAssignment = assignments.find(second);
    const Graphics::GpuTaskQueueAssignment* const bridgeAssignment = assignments.find(bridge);
    ASSERT_NE(firstAssignment, nullptr);
    ASSERT_NE(secondAssignment, nullptr);
    ASSERT_NE(bridgeAssignment, nullptr);
    EXPECT_EQ(firstAssignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_EQ(secondAssignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_EQ(secondAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::PreferredQueue);
    EXPECT_TRUE(secondAssignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::DirectDependencyAffinity);
    EXPECT_EQ(bridgeAssignment->queue, queues[0u].id);

    const Graphics::GpuSubmissionPacketId firstPacket = compiledPlan.packetForTask(first);
    const Graphics::GpuSubmissionPacketId secondPacket = compiledPlan.packetForTask(second);
    const Graphics::GpuSubmissionPacketId bridgePacket = compiledPlan.packetForTask(bridge);
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_TRUE(bridgePacket.valid());
    ASSERT_EQ(compiledPlan.packet(secondPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(secondPacket).dependencies[0u].producer, firstPacket);
    ASSERT_EQ(compiledPlan.packet(bridgePacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(bridgePacket).dependencies[0u].producer, secondPacket);
}

TEST(GpuTaskGraph, PreservesLatestDirectDependencyRouteAcrossIncomingAdjacencyOrder){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;

    Graphics::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Graphics::GpuTaskCostHint::Large;
    producerScheduling.allowSameClassQueueRouting = true;
    const Graphics::GpuTaskId first = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/direct_affinity_order_first"),
        "Direct Affinity Order First",
        graphicsRequest,
        producerScheduling
    );
    const Graphics::GpuTaskId second = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/direct_affinity_order_second"),
        "Direct Affinity Order Second",
        graphicsRequest,
        producerScheduling
    );
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());

    Graphics::GpuTaskSchedulingHint consumerScheduling;
    consumerScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    consumerScheduling.allowSameClassQueueRouting = true;
    consumerScheduling.preserveSameClassQueueWithDirectDependency = true;
    const Graphics::GpuTaskId consumerDependencies[] = { second, first };
    const Graphics::GpuTaskId consumer = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/direct_affinity_order_consumer"),
        "Direct Affinity Order Consumer",
        graphicsRequest,
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
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.topologicalOrder().size(), 3u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], first);
    EXPECT_EQ(analysis.topologicalOrder()[1u], second);
    EXPECT_EQ(analysis.topologicalOrder()[2u], consumer);
    const Graphics::GpuTaskGraphSchedulingTaskIndexView producers = analysis.schedulingProducers(consumer);
    ASSERT_EQ(producers.taskCount, 2u);
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

    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;

    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.cost = Graphics::GpuTaskCostHint::Large;
    firstScheduling.allowSameClassQueueRouting = true;
    Graphics::GpuTaskSchedulingHint secondScheduling;
    secondScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    secondScheduling.allowSameClassQueueRouting = true;
    const Graphics::GpuTaskId first = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/cross_family_not_opted_first"),
        "Cross Family Not Opted First",
        graphicsRequest,
        firstScheduling
    );
    const Graphics::GpuTaskId second = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/cross_family_not_opted_second"),
        "Cross Family Not Opted Second",
        graphicsRequest,
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
    const Graphics::GpuTaskGraphQueueTopology topology{
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

    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;

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
        .setQueue(graphicsRequest)
        .setScheduling(producerScheduling)
        .setResourceUses(&producerUse, 1u)
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
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
        .setQueue(graphicsRequest)
        .setScheduling(consumerScheduling)
        .setDependencies(consumerDependencies, LengthOf(consumerDependencies))
        .setResourceUses(&consumerUse, 1u)
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(consumer.valid());

    Graphics::GpuPhysicalQueueInfo secondaryGraphicsQueue = GraphicsQueue(1u);
    secondaryGraphicsQueue.familyIndex = 3u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        secondaryGraphicsQueue,
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


    const Graphics::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
    const Graphics::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumer);
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(consumerAssignment, nullptr);
    EXPECT_EQ(producerAssignment->queue, queues[0u].id);
    EXPECT_EQ(consumerAssignment->queue, queues[1u].id);
    EXPECT_EQ(consumerAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_TRUE(consumerAssignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::SameClassLoadBalance);

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
    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 2u);

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

    const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics producerQueueCompileStatistics =
        compiledPlan.physicalQueueCompileStatistics(queues[0u].id)
    ;
    const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics consumerQueueCompileStatistics =
        compiledPlan.physicalQueueCompileStatistics(queues[1u].id)
    ;
    ASSERT_TRUE(producerQueueCompileStatistics.valid());
    ASSERT_TRUE(consumerQueueCompileStatistics.valid());
    EXPECT_EQ(producerQueueCompileStatistics.taskCount, 1u);
    EXPECT_EQ(consumerQueueCompileStatistics.taskCount, 1u);
    EXPECT_EQ(producerQueueCompileStatistics.prologueBarrierCount, 1u);
    EXPECT_EQ(producerQueueCompileStatistics.epilogueBarrierCount, 1u);
    EXPECT_EQ(producerQueueCompileStatistics.ownershipReleaseBarrierCount, 1u);
    EXPECT_EQ(producerQueueCompileStatistics.ownershipAcquireBarrierCount, 0u);
    EXPECT_EQ(consumerQueueCompileStatistics.prologueBarrierCount, 2u);
    EXPECT_EQ(consumerQueueCompileStatistics.epilogueBarrierCount, 0u);
    EXPECT_EQ(consumerQueueCompileStatistics.ownershipReleaseBarrierCount, 0u);
    EXPECT_EQ(consumerQueueCompileStatistics.ownershipAcquireBarrierCount, 1u);
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

    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;

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
        .setQueue(graphicsRequest)
        .setScheduling(producerScheduling)
        .setResourceUses(&producerUse, 1u)
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
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
        .setQueue(graphicsRequest)
        .setScheduling(consumerScheduling)
        .setDependencies(consumerDependencies, LengthOf(consumerDependencies))
        .setResourceUses(&consumerUse, 1u)
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(consumer.valid());

    Graphics::GpuPhysicalQueueInfo auxiliaryGraphicsQueue = GraphicsQueue(1u);
    auxiliaryGraphicsQueue.familyIndex = 3u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        auxiliaryGraphicsQueue,
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
    EXPECT_EQ(consumerAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_TRUE(consumerAssignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::SameClassLoadBalance);
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
        const Graphics::GpuQueuePreference::Enum preference,
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

        Graphics::GpuQueueRequest queueRequest;
        queueRequest.requiredCapabilities = capabilities;
        queueRequest.preferredQueue = preference;
        queueRequest.allowFallback = false;
        queueRequest.compilerMayOverridePreference = false;

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
            .setQueue(queueRequest)
            .setScheduling(producerScheduling)
            .setResourceUses(&producerUse, 1u)
        ;
        const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
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
            .setQueue(queueRequest)
            .setScheduling(consumerScheduling)
            .setDependencies(consumerDependencies, LengthOf(consumerDependencies))
            .setResourceUses(&consumerUse, 1u)
        ;
        const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc);
        ASSERT_TRUE(consumer.valid());

        const Graphics::GpuPhysicalQueueInfo queues[] = { primaryQueue, auxiliaryQueue };
        const Graphics::GpuTaskGraphQueueTopology topology{
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
        EXPECT_EQ(
            consumerAssignment->reason,
            preference == Graphics::GpuQueuePreference::Compute
                ? Graphics::GpuTaskQueueAssignmentReason::DedicatedCompute
                : Graphics::GpuTaskQueueAssignmentReason::DedicatedTransfer
        );
        EXPECT_TRUE(consumerAssignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::SameClassLoadBalance);
        EXPECT_EQ(compiledProducer->queue, primaryQueue.id);
        EXPECT_EQ(compiledConsumer->queue, auxiliaryQueue.id);
        ASSERT_EQ(compiledProducer->epilogueBarrierCount, 1u);
        ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);
        ASSERT_EQ(compiledConsumer->prologueBarrierCount, 2u);

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
        Graphics::GpuQueuePreference::Compute,
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
        Graphics::GpuQueuePreference::Transfer,
        DedicatedTransferQueue(),
        auxiliaryTransfer,
        Name("tests/task_graph/cross_family_same_class_transfer_buffer"),
        Name("tests/task_graph/cross_family_same_class_transfer_producer"),
        Name("tests/task_graph/cross_family_same_class_transfer_consumer")
    );
}

TEST(GpuTaskGraph, RoutesAccelStructAcrossQueueFamiliesWithOwnershipAndStateSeed){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId accelStruct = AddAccelStructMetadata(
        graph,
        Name("tests/task_graph/cross_family_accel_struct"),
        "Cross Family Accel Struct"
    );
    ASSERT_TRUE(accelStruct.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
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
    const Graphics::GpuTaskResourceUse producerUse{
        .resource = accelStruct,
        .range = {},
        .requiredState = Graphics::ResourceStates::AccelStructWrite,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/cross_family_accel_struct_producer"))
        .setMarkerLabel("Cross Family Accel Struct Producer")
        .setQueue(graphicsRequest)
        .setResourceUses(&producerUse, 1u)
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
    ASSERT_TRUE(producer.valid());

    const Graphics::GpuTaskId consumerDependencies[] = { producer };
    const Graphics::GpuTaskResourceUse consumerUse{
        .resource = accelStruct,
        .range = {},
        .requiredState = Graphics::ResourceStates::AccelStructRead,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/cross_family_accel_struct_consumer"))
        .setMarkerLabel("Cross Family Accel Struct Consumer")
        .setQueue(computeRequest)
        .setDependencies(consumerDependencies, LengthOf(consumerDependencies))
        .setResourceUses(&consumerUse, 1u)
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(consumer.valid());

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
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);
    const Graphics::GpuPacketStateSeed* const seeds = compiledPlan.findTask(consumer).prologueStateSeeds;
    ASSERT_NE(seeds, nullptr);
    EXPECT_EQ(seeds[0u].resource, accelStruct);
    EXPECT_EQ(seeds[0u].sourcePacket, compiledProducer->packet);

    ASSERT_EQ(compiledProducer->epilogueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const release = compiledPlan.findTask(producer).epilogueBarriers;
    ASSERT_NE(release, nullptr);
    EXPECT_EQ(release[0u].type, Graphics::GpuCompiledBarrierType::AccelStructOwnershipRelease);
    EXPECT_EQ(release[0u].resource, accelStruct);
    EXPECT_EQ(release[0u].sourceQueue, queues[0u].id);
    EXPECT_EQ(release[0u].destinationQueue, queues[1u].id);

    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 2u);
    const Graphics::GpuCompiledBarrier* const acquireAndTransition = compiledPlan.findTask(consumer).prologueBarriers;
    ASSERT_NE(acquireAndTransition, nullptr);
    EXPECT_EQ(acquireAndTransition[0u].type, Graphics::GpuCompiledBarrierType::AccelStructOwnershipAcquire);
    EXPECT_EQ(acquireAndTransition[0u].resource, accelStruct);
    EXPECT_EQ(acquireAndTransition[0u].sourceQueue, queues[0u].id);
    EXPECT_EQ(acquireAndTransition[0u].destinationQueue, queues[1u].id);
    EXPECT_EQ(acquireAndTransition[1u].type, Graphics::GpuCompiledBarrierType::AccelStructTransition);
    EXPECT_EQ(acquireAndTransition[1u].before, Graphics::ResourceStates::AccelStructWrite);
    EXPECT_EQ(acquireAndTransition[1u].after, Graphics::ResourceStates::AccelStructRead);

    ASSERT_EQ(compiledPlan.logicalOwnershipTransferCount(), 1u);
    const Graphics::GpuCompiledOwnershipTransfer* const ownershipTransfers =
        compiledPlan.logicalOwnershipTransfers()
    ;
    ASSERT_NE(ownershipTransfers, nullptr);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(0u), ownershipTransfers);
    EXPECT_EQ(compiledPlan.logicalOwnershipTransferAt(1u), nullptr);
    const Graphics::GpuCompiledOwnershipTransfer& ownershipTransfer = ownershipTransfers[0u];
    EXPECT_TRUE(ownershipTransfer.valid());
    EXPECT_EQ(ownershipTransfer.resource, accelStruct);
    EXPECT_EQ(ownershipTransfer.resourceIdentity, Name("tests/task_graph/cross_family_accel_struct"));
    EXPECT_EQ(ownershipTransfer.range.textureSubresources, Graphics::s_AllSubresources);
    EXPECT_EQ(ownershipTransfer.range.bufferRange, Graphics::s_EntireBuffer);
    EXPECT_EQ(ownershipTransfer.sourceTask, producer);
    EXPECT_EQ(ownershipTransfer.destinationTask, consumer);
    EXPECT_EQ(ownershipTransfer.sourcePacket, compiledProducer->packet);
    EXPECT_EQ(ownershipTransfer.destinationPacket, compiledConsumer->packet);
    EXPECT_EQ(ownershipTransfer.sourceQueue, queues[0u].id);
    EXPECT_EQ(ownershipTransfer.destinationQueue, queues[1u].id);
    EXPECT_EQ(ownershipTransfer.sourceQueueFamilyIndex, queues[0u].familyIndex);
    EXPECT_EQ(ownershipTransfer.destinationQueueFamilyIndex, queues[1u].familyIndex);
    EXPECT_EQ(ownershipTransfer.declaredQueueSharing, Graphics::ResourceQueueSharing::Exclusive);
    EXPECT_EQ(ownershipTransfer.resourceType, Graphics::GpuGraphResourceType::AccelStruct);
    EXPECT_EQ(ownershipTransfer.route, Graphics::GpuOwnershipTransferRoute::Internal);
    EXPECT_TRUE(ownershipTransfer.concurrentSharingCouldAvoid);

    const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics producerQueueCompileStatistics =
        compiledPlan.physicalQueueCompileStatistics(queues[0u].id)
    ;
    const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics consumerQueueCompileStatistics =
        compiledPlan.physicalQueueCompileStatistics(queues[1u].id)
    ;
    ASSERT_TRUE(producerQueueCompileStatistics.valid());
    ASSERT_TRUE(consumerQueueCompileStatistics.valid());
    EXPECT_EQ(producerQueueCompileStatistics.ownershipReleaseBarrierCount, 1u);
    EXPECT_EQ(producerQueueCompileStatistics.ownershipAcquireBarrierCount, 0u);
    EXPECT_EQ(consumerQueueCompileStatistics.ownershipReleaseBarrierCount, 0u);
    EXPECT_EQ(consumerQueueCompileStatistics.ownershipAcquireBarrierCount, 1u);
    EXPECT_EQ(producerQueueCompileStatistics.outgoingLogicalOwnershipTransferCount, 1u);
    EXPECT_EQ(producerQueueCompileStatistics.incomingLogicalOwnershipTransferCount, 0u);
    EXPECT_EQ(consumerQueueCompileStatistics.outgoingLogicalOwnershipTransferCount, 0u);
    EXPECT_EQ(consumerQueueCompileStatistics.incomingLogicalOwnershipTransferCount, 1u);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

