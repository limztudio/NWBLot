// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_frontier_packetization_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, FrontierSafePacketizationSplitsBeforeCrossQueueConsumer){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
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

    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc firstDesc;
    firstDesc
        .setIdentity(Name("tests/task_graph/frontier_first"))
        .setMarkerLabel("Frontier First")
        .setQueue(graphicsRequest)
        .setScheduling(firstScheduling)
    ;
    const Graphics::GpuTaskId first = graph.addTask(firstDesc);
    ASSERT_TRUE(first.valid());

    Graphics::GpuTaskSchedulingHint mergedScheduling;
    mergedScheduling.allowPacketMerge = true;
    mergedScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc mergedDesc;
    mergedDesc
        .setIdentity(Name("tests/task_graph/frontier_unrelated_graphics"))
        .setMarkerLabel("Frontier Unrelated Graphics")
        .setQueue(graphicsRequest)
        .setScheduling(mergedScheduling)
        .setDependencies(&first, 1u)
    ;
    const Graphics::GpuTaskId unrelatedGraphics = graph.addTask(mergedDesc);
    ASSERT_TRUE(unrelatedGraphics.valid());

    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/frontier_compute_consumer"))
        .setMarkerLabel("Frontier Compute Consumer")
        .setQueue(computeRequest)
        .setDependencies(&first, 1u)
    ;
    const Graphics::GpuTaskId computeConsumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(computeConsumer.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };

    Graphics::GpuTaskGraphAnalysis explicitMergeAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments explicitMergeAssignments(testArena.arena);
    Graphics::GpuCompiledGraph explicitMergeGraph(testArena.arena);
    ASSERT_TRUE(Compile(
        graph,
        explicitMergeAnalysis,
        topology,
        explicitMergeAssignments,
        explicitMergeGraph
    ));
    {
        const Graphics::GpuCompiledGraph::ReadView explicitMergePlan(explicitMergeGraph);

        ASSERT_EQ(explicitMergePlan.packetCount(), 2u);
        const Graphics::GpuSubmissionPacketId explicitFirstPacket = explicitMergePlan.packetForTask(first);
        const Graphics::GpuSubmissionPacketId explicitUnrelatedPacket = explicitMergePlan.packetForTask(unrelatedGraphics);
        const Graphics::GpuSubmissionPacketId explicitConsumerPacket = explicitMergePlan.packetForTask(computeConsumer);
        ASSERT_TRUE(explicitFirstPacket.valid());
        EXPECT_EQ(explicitFirstPacket, explicitUnrelatedPacket);
        EXPECT_NE(explicitFirstPacket, explicitConsumerPacket);
        const Graphics::GpuCompiledPacketView explicitConsumerView = explicitMergePlan.packet(explicitConsumerPacket);
        ASSERT_TRUE(explicitConsumerView.valid());
        ASSERT_EQ(explicitConsumerView.plan->dependencyCount, 1u);
        EXPECT_EQ(explicitConsumerView.dependencies[0u].producer, explicitFirstPacket);
    }

    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis frontierAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments frontierAssignments(testArena.arena);
    Graphics::GpuCompiledGraph frontierGraph(testArena.arena);
    ASSERT_TRUE(Compile(
        graph,
        frontierAnalysis,
        topology,
        frontierAssignments,
        frontierGraph,
        frontierOptions
    ));
    const Graphics::GpuCompiledGraph::ReadView frontierPlan(frontierGraph);

    ASSERT_EQ(frontierPlan.packetCount(), 3u);
    const Graphics::GpuSubmissionPacketId frontierFirstPacket = frontierPlan.packetForTask(first);
    const Graphics::GpuSubmissionPacketId frontierUnrelatedPacket = frontierPlan.packetForTask(unrelatedGraphics);
    const Graphics::GpuSubmissionPacketId frontierConsumerPacket = frontierPlan.packetForTask(computeConsumer);
    ASSERT_TRUE(frontierFirstPacket.valid());
    EXPECT_NE(frontierFirstPacket, frontierUnrelatedPacket);
    EXPECT_NE(frontierUnrelatedPacket, frontierConsumerPacket);
    EXPECT_NE(
        frontierPlan.packet(frontierFirstPacket).plan->queue,
        frontierPlan.packet(frontierConsumerPacket).plan->queue
    );
    EXPECT_EQ(frontierPlan.packet(frontierFirstPacket).plan->taskCount, 1u);
    EXPECT_EQ(frontierPlan.packet(frontierUnrelatedPacket).plan->taskCount, 1u);
    EXPECT_EQ(
        frontierPlan.packetizationDecisionForTask(unrelatedGraphics),
        Graphics::GpuTaskPacketizationDecision::CrossQueueConsumerFrontier
    );
    const Graphics::GpuCompiledPacketView frontierConsumerView = frontierPlan.packet(frontierConsumerPacket);
    ASSERT_TRUE(frontierConsumerView.valid());
    ASSERT_EQ(frontierConsumerView.plan->dependencyCount, 1u);
    EXPECT_EQ(frontierConsumerView.dependencies[0u].producer, frontierFirstPacket);
}

TEST(GpuTaskGraph, FrontierScoredPacketizationMergesCheapImmediateSuccessor){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };

    Graphics::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    producerScheduling.frontierScoredMergeDomain = Name("tests/task_graph/frontier_scored_cheap_chain");
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_producer"))
        .setMarkerLabel("Frontier Scored Producer")
        .setQueue(graphicsRequest)
        .setScheduling(producerScheduling)
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskSchedulingHint successorScheduling;
    successorScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    successorScheduling.frontierScoredMergeDomain = Name("tests/task_graph/frontier_scored_cheap_chain");
    Graphics::GpuTaskDesc successorDesc;
    successorDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_successor"))
        .setMarkerLabel("Frontier Scored Successor")
        .setQueue(graphicsRequest)
        .setScheduling(successorScheduling)
        .setDependencies(&producer, 1u)
    ;
    const Graphics::GpuTaskId successor = graph.addTask(successorDesc);
    ASSERT_TRUE(successor.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierScored;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 1u);

    const Graphics::GpuSubmissionPacketId producerPacket = compiledPlan.packetForTask(producer);
    const Graphics::GpuSubmissionPacketId successorPacket = compiledPlan.packetForTask(successor);
    ASSERT_TRUE(producerPacket.valid());
    EXPECT_EQ(producerPacket, successorPacket);
    EXPECT_EQ(compiledPlan.packet(producerPacket).plan->taskCount, 2u);
    EXPECT_EQ(
        compiledPlan.packetizationDecisionForTask(producer),
        Graphics::GpuTaskPacketizationDecision::FirstTask
    );
    EXPECT_EQ(
        compiledPlan.packetizationDecisionForTask(successor),
        Graphics::GpuTaskPacketizationDecision::MergedFrontierScored
    );
}

TEST(GpuTaskGraph, FrontierScoredPacketizationMergesLongSerialPacket){
    constexpr usize s_TaskCount = 512u;

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Name mergeDomain("tests/task_graph/frontier_scored_long_chain");
    const Name taskBaseName("tests/task_graph/frontier_scored_long_chain_task_");
    Graphics::GpuTaskId tasks[s_TaskCount] = {};
    char taskIndexBuffer[32u] = {};
    for(usize taskIndex = 0u; taskIndex < s_TaskCount; ++taskIndex){
        Graphics::GpuTaskSchedulingHint scheduling;
        scheduling.cost = taskIndex == 0u
            ? Graphics::GpuTaskCostHint::Medium
            : Graphics::GpuTaskCostHint::Tiny
        ;
        scheduling.frontierScoredMergeDomain = mergeDomain;
        Graphics::GpuTaskDesc taskDesc;
        taskDesc
            .setIdentity(DeriveName(taskBaseName, FormatDecimal(taskIndex, taskIndexBuffer)))
            .setMarkerLabel("Frontier Scored Long Chain Task")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling)
            .setDependencies(taskIndex == 0u ? nullptr : &tasks[taskIndex - 1u], taskIndex == 0u ? 0u : 1u)
        ;
        tasks[taskIndex] = graph.addTask(taskDesc);
        ASSERT_TRUE(tasks[taskIndex].valid());
    }

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierScored;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 1u);

    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(tasks[0u]);
    ASSERT_TRUE(packet.valid());
    ASSERT_EQ(compiledPlan.packet(packet).plan->taskCount, s_TaskCount);
    ASSERT_NE(compiledPlan.packet(packet).tasks, nullptr);
    EXPECT_EQ(
        compiledPlan.packetizationDecisionForTask(tasks[0u]),
        Graphics::GpuTaskPacketizationDecision::FirstTask
    );
    for(usize taskIndex = 0u; taskIndex < s_TaskCount; ++taskIndex){
        EXPECT_EQ(compiledPlan.packet(packet).tasks[taskIndex], tasks[taskIndex]);
        EXPECT_EQ(compiledPlan.packetForTask(tasks[taskIndex]), packet);
        if(taskIndex != 0u){
            EXPECT_EQ(
                compiledPlan.packetizationDecisionForTask(tasks[taskIndex]),
                Graphics::GpuTaskPacketizationDecision::MergedFrontierScored
            );
        }
    }
}

TEST(GpuTaskGraph, FrontierScoredPacketizationRejectsPrecedingBoundaryTask){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Name mergeDomain("tests/task_graph/frontier_scored_preceding_boundary");

    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    firstScheduling.forceSubmissionBoundary = true;
    firstScheduling.frontierScoredMergeDomain = mergeDomain;
    Graphics::GpuTaskDesc firstDesc;
    firstDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_preceding_boundary_first"))
        .setMarkerLabel("Frontier Scored Preceding Boundary First")
        .setQueue(graphicsRequest)
        .setScheduling(firstScheduling)
    ;
    const Graphics::GpuTaskId first = graph.addTask(firstDesc);
    ASSERT_TRUE(first.valid());

    Graphics::GpuTaskSchedulingHint successorScheduling;
    successorScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    successorScheduling.frontierScoredMergeDomain = mergeDomain;
    Graphics::GpuTaskDesc successorDesc;
    successorDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_preceding_boundary_successor"))
        .setMarkerLabel("Frontier Scored Preceding Boundary Successor")
        .setQueue(graphicsRequest)
        .setScheduling(successorScheduling)
        .setDependencies(&first, 1u)
    ;
    const Graphics::GpuTaskId successor = graph.addTask(successorDesc);
    ASSERT_TRUE(successor.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierScored;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 2u);
    EXPECT_NE(compiledPlan.packetForTask(first), compiledPlan.packetForTask(successor));
    EXPECT_EQ(
        compiledPlan.packetizationDecisionForTask(successor),
        Graphics::GpuTaskPacketizationDecision::PrecedingTaskForcesBoundary
    );
}

TEST(GpuTaskGraph, FrontierScoredPacketizationRequiresNonemptyMergeDomain){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };

    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    Graphics::GpuTaskDesc firstDesc;
    firstDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_missing_domain_first"))
        .setMarkerLabel("Frontier Scored Missing Domain First")
        .setQueue(graphicsRequest)
        .setScheduling(firstScheduling)
    ;
    const Graphics::GpuTaskId first = graph.addTask(firstDesc);
    ASSERT_TRUE(first.valid());

    Graphics::GpuTaskSchedulingHint namedDomainScheduling;
    namedDomainScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    namedDomainScheduling.frontierScoredMergeDomain = Name("tests/task_graph/frontier_scored_missing_domain");
    Graphics::GpuTaskDesc namedDomainDesc;
    namedDomainDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_missing_domain_named_successor"))
        .setMarkerLabel("Frontier Scored Missing Domain Named Successor")
        .setQueue(graphicsRequest)
        .setScheduling(namedDomainScheduling)
        .setDependencies(&first, 1u)
    ;
    const Graphics::GpuTaskId namedDomainSuccessor = graph.addTask(namedDomainDesc);
    ASSERT_TRUE(namedDomainSuccessor.valid());

    Graphics::GpuTaskSchedulingHint emptyDomainScheduling;
    emptyDomainScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    Graphics::GpuTaskDesc emptyDomainDesc;
    emptyDomainDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_missing_domain_empty_successor"))
        .setMarkerLabel("Frontier Scored Missing Domain Empty Successor")
        .setQueue(graphicsRequest)
        .setScheduling(emptyDomainScheduling)
        .setDependencies(&namedDomainSuccessor, 1u)
    ;
    const Graphics::GpuTaskId emptyDomainSuccessor = graph.addTask(emptyDomainDesc);
    ASSERT_TRUE(emptyDomainSuccessor.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierScored;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 3u);

    const Graphics::GpuSubmissionPacketId firstPacket = compiledPlan.packetForTask(first);
    const Graphics::GpuSubmissionPacketId namedDomainSuccessorPacket = compiledPlan.packetForTask(namedDomainSuccessor);
    const Graphics::GpuSubmissionPacketId emptyDomainSuccessorPacket = compiledPlan.packetForTask(emptyDomainSuccessor);
    ASSERT_TRUE(firstPacket.valid());
    EXPECT_NE(firstPacket, namedDomainSuccessorPacket);
    EXPECT_NE(namedDomainSuccessorPacket, emptyDomainSuccessorPacket);
    EXPECT_EQ(
        compiledPlan.packetizationDecisionForTask(namedDomainSuccessor),
        Graphics::GpuTaskPacketizationDecision::ScoredMergeDomainMismatch
    );
    EXPECT_EQ(
        compiledPlan.packetizationDecisionForTask(emptyDomainSuccessor),
        Graphics::GpuTaskPacketizationDecision::ScoredMergeDomainMismatch
    );
}

TEST(GpuTaskGraph, FrontierScoredPacketizationRequiresOneDomainAcrossPrecedingPacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };

    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    firstScheduling.frontierScoredMergeDomain = Name("tests/task_graph/frontier_scored_first_domain");
    Graphics::GpuTaskDesc firstDesc;
    firstDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_mismatched_domain_first"))
        .setMarkerLabel("Frontier Scored Mismatched Domain First")
        .setQueue(graphicsRequest)
        .setScheduling(firstScheduling)
    ;
    const Graphics::GpuTaskId first = graph.addTask(firstDesc);
    ASSERT_TRUE(first.valid());

    Graphics::GpuTaskSchedulingHint explicitScheduling;
    explicitScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    explicitScheduling.mergeWithPrevious = true;
    explicitScheduling.frontierScoredMergeDomain = Name("tests/task_graph/frontier_scored_second_domain");
    Graphics::GpuTaskDesc explicitDesc;
    explicitDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_mismatched_domain_explicit"))
        .setMarkerLabel("Frontier Scored Mismatched Domain Explicit")
        .setQueue(graphicsRequest)
        .setScheduling(explicitScheduling)
        .setDependencies(&first, 1u)
    ;
    const Graphics::GpuTaskId explicitTask = graph.addTask(explicitDesc);
    ASSERT_TRUE(explicitTask.valid());

    Graphics::GpuTaskSchedulingHint successorScheduling;
    successorScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    successorScheduling.frontierScoredMergeDomain = Name("tests/task_graph/frontier_scored_second_domain");
    Graphics::GpuTaskDesc successorDesc;
    successorDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_mismatched_domain_successor"))
        .setMarkerLabel("Frontier Scored Mismatched Domain Successor")
        .setQueue(graphicsRequest)
        .setScheduling(successorScheduling)
        .setDependencies(&explicitTask, 1u)
    ;
    const Graphics::GpuTaskId successor = graph.addTask(successorDesc);
    ASSERT_TRUE(successor.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierScored;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 2u);

    const Graphics::GpuSubmissionPacketId firstPacket = compiledPlan.packetForTask(first);
    const Graphics::GpuSubmissionPacketId explicitPacket = compiledPlan.packetForTask(explicitTask);
    const Graphics::GpuSubmissionPacketId successorPacket = compiledPlan.packetForTask(successor);
    ASSERT_TRUE(firstPacket.valid());
    EXPECT_EQ(firstPacket, explicitPacket);
    EXPECT_NE(explicitPacket, successorPacket);
    EXPECT_EQ(
        compiledPlan.packetizationDecisionForTask(explicitTask),
        Graphics::GpuTaskPacketizationDecision::MergedExplicit
    );
    EXPECT_EQ(
        compiledPlan.packetizationDecisionForTask(successor),
        Graphics::GpuTaskPacketizationDecision::ScoredMergeDomainMismatch
    );
}

TEST(GpuTaskGraph, FrontierScoredPacketizationPreservesCrossQueueConsumerFrontier){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
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

    Graphics::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    producerScheduling.frontierScoredMergeDomain = Name("tests/task_graph/frontier_scored_cross_queue");
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_frontier_producer"))
        .setMarkerLabel("Frontier Scored Frontier Producer")
        .setQueue(graphicsRequest)
        .setScheduling(producerScheduling)
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskSchedulingHint successorScheduling;
    successorScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    successorScheduling.frontierScoredMergeDomain = Name("tests/task_graph/frontier_scored_cross_queue");
    Graphics::GpuTaskDesc successorDesc;
    successorDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_frontier_successor"))
        .setMarkerLabel("Frontier Scored Frontier Successor")
        .setQueue(graphicsRequest)
        .setScheduling(successorScheduling)
        .setDependencies(&producer, 1u)
    ;
    const Graphics::GpuTaskId successor = graph.addTask(successorDesc);
    ASSERT_TRUE(successor.valid());

    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_compute_consumer"))
        .setMarkerLabel("Frontier Scored Compute Consumer")
        .setQueue(computeRequest)
        .setDependencies(&producer, 1u)
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(consumer.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierScored;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 3u);

    const Graphics::GpuSubmissionPacketId producerPacket = compiledPlan.packetForTask(producer);
    const Graphics::GpuSubmissionPacketId successorPacket = compiledPlan.packetForTask(successor);
    const Graphics::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(consumer);
    ASSERT_TRUE(producerPacket.valid());
    EXPECT_NE(producerPacket, successorPacket);
    EXPECT_NE(producerPacket, consumerPacket);
    EXPECT_EQ(
        compiledPlan.packetizationDecisionForTask(successor),
        Graphics::GpuTaskPacketizationDecision::CrossQueueConsumerFrontier
    );
    ASSERT_EQ(compiledPlan.packet(consumerPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(consumerPacket).dependencies[0u].producer, producerPacket);
}

TEST(GpuTaskGraph, FrontierScoredPacketizationCarriesTailPhysicalQueueFrontierForward){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Name mergeDomain("tests/task_graph/frontier_scored_tail_frontier");

    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    firstScheduling.frontierScoredMergeDomain = mergeDomain;
    Graphics::GpuTaskDesc firstDesc;
    firstDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_tail_frontier_first"))
        .setMarkerLabel("Frontier Scored Tail Frontier First")
        .setQueue(graphicsRequest)
        .setScheduling(firstScheduling)
    ;
    const Graphics::GpuTaskId first = graph.addTask(firstDesc);
    ASSERT_TRUE(first.valid());

    Graphics::GpuTaskSchedulingHint mergedScheduling;
    mergedScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    mergedScheduling.frontierScoredMergeDomain = mergeDomain;
    Graphics::GpuTaskDesc mergedDesc;
    mergedDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_tail_frontier_merged"))
        .setMarkerLabel("Frontier Scored Tail Frontier Merged")
        .setQueue(graphicsRequest)
        .setScheduling(mergedScheduling)
        .setDependencies(&first, 1u)
    ;
    const Graphics::GpuTaskId merged = graph.addTask(mergedDesc);
    ASSERT_TRUE(merged.valid());

    Graphics::GpuTaskSchedulingHint blockedScheduling;
    blockedScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    blockedScheduling.frontierScoredMergeDomain = mergeDomain;
    Graphics::GpuTaskDesc blockedDesc;
    blockedDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_tail_frontier_blocked"))
        .setMarkerLabel("Frontier Scored Tail Frontier Blocked")
        .setQueue(graphicsRequest)
        .setScheduling(blockedScheduling)
        .setDependencies(&merged, 1u)
    ;
    const Graphics::GpuTaskId blocked = graph.addTask(blockedDesc);
    ASSERT_TRUE(blocked.valid());

    const Name consumerIdentity("tests/task_graph/frontier_scored_tail_frontier_consumer");
    Graphics::GpuTaskSchedulingHint consumerScheduling;
    consumerScheduling.allowSameClassQueueRouting = true;
    consumerScheduling.allowTimingFeedbackRouting = true;
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(consumerIdentity)
        .setMarkerLabel("Frontier Scored Tail Frontier Consumer")
        .setQueue(graphicsRequest)
        .setScheduling(consumerScheduling)
        .setDependencies(&merged, 1u)
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc);
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
    const Graphics::GpuTaskTimingQueueOverride queueOverride{
        .key = Graphics::GpuTaskTimingKey{
            .task = consumerIdentity,
            .queue = Graphics::CommandQueue::Graphics,
        },
        .queue = auxiliaryGraphicsQueue.id,
    };
    Graphics::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierScored;
    options.queueAssignmentOptions.timingQueueOverrides = &queueOverride;
    options.queueAssignmentOptions.timingQueueOverrideCount = 1u;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 3u);

    const Graphics::GpuTaskQueueAssignment* const firstAssignment = assignments.find(first);
    const Graphics::GpuTaskQueueAssignment* const mergedAssignment = assignments.find(merged);
    const Graphics::GpuTaskQueueAssignment* const blockedAssignment = assignments.find(blocked);
    const Graphics::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumer);
    ASSERT_NE(firstAssignment, nullptr);
    ASSERT_NE(mergedAssignment, nullptr);
    ASSERT_NE(blockedAssignment, nullptr);
    ASSERT_NE(consumerAssignment, nullptr);
    EXPECT_EQ(firstAssignment->queue, queues[0u].id);
    EXPECT_EQ(mergedAssignment->queue, queues[0u].id);
    EXPECT_EQ(blockedAssignment->queue, queues[0u].id);
    EXPECT_EQ(consumerAssignment->queue, queues[1u].id);

    const Graphics::GpuSubmissionPacketId firstPacket = compiledPlan.packetForTask(first);
    const Graphics::GpuSubmissionPacketId mergedPacket = compiledPlan.packetForTask(merged);
    const Graphics::GpuSubmissionPacketId blockedPacket = compiledPlan.packetForTask(blocked);
    const Graphics::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(consumer);
    ASSERT_TRUE(firstPacket.valid());
    EXPECT_EQ(firstPacket, mergedPacket);
    EXPECT_NE(firstPacket, blockedPacket);
    EXPECT_NE(blockedPacket, consumerPacket);
    EXPECT_EQ(
        compiledPlan.packetizationDecisionForTask(merged),
        Graphics::GpuTaskPacketizationDecision::MergedFrontierScored
    );
    EXPECT_EQ(
        compiledPlan.packetizationDecisionForTask(blocked),
        Graphics::GpuTaskPacketizationDecision::CrossQueueConsumerFrontier
    );
}

TEST(GpuTaskGraph, FrontierSafePacketizationKeepsFrontierAfterExplicitOverride){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
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

    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc firstDesc;
    firstDesc
        .setIdentity(Name("tests/task_graph/frontier_override_persistence_first"))
        .setMarkerLabel("Frontier Override Persistence First")
        .setQueue(graphicsRequest)
        .setScheduling(firstScheduling)
    ;
    const Graphics::GpuTaskId first = graph.addTask(firstDesc);
    ASSERT_TRUE(first.valid());

    Graphics::GpuTaskSchedulingHint overrideScheduling;
    overrideScheduling.allowPacketMerge = true;
    overrideScheduling.mergeWithPrevious = true;
    overrideScheduling.allowMergeAcrossConsumerFrontier = true;
    Graphics::GpuTaskDesc overrideDesc;
    overrideDesc
        .setIdentity(Name("tests/task_graph/frontier_override_persistence_merged"))
        .setMarkerLabel("Frontier Override Persistence Merged")
        .setQueue(graphicsRequest)
        .setScheduling(overrideScheduling)
        .setDependencies(&first, 1u)
    ;
    const Graphics::GpuTaskId overrideTask = graph.addTask(overrideDesc);
    ASSERT_TRUE(overrideTask.valid());

    Graphics::GpuTaskSchedulingHint blockedScheduling;
    blockedScheduling.allowPacketMerge = true;
    blockedScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc blockedDesc;
    blockedDesc
        .setIdentity(Name("tests/task_graph/frontier_override_persistence_blocked"))
        .setMarkerLabel("Frontier Override Persistence Blocked")
        .setQueue(graphicsRequest)
        .setScheduling(blockedScheduling)
        .setDependencies(&overrideTask, 1u)
    ;
    const Graphics::GpuTaskId blocked = graph.addTask(blockedDesc);
    ASSERT_TRUE(blocked.valid());

    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/frontier_override_persistence_consumer"))
        .setMarkerLabel("Frontier Override Persistence Consumer")
        .setQueue(computeRequest)
        .setDependencies(&first, 1u)
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(consumer.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 3u);

    const Graphics::GpuSubmissionPacketId firstPacket = compiledPlan.packetForTask(first);
    const Graphics::GpuSubmissionPacketId overridePacket = compiledPlan.packetForTask(overrideTask);
    const Graphics::GpuSubmissionPacketId blockedPacket = compiledPlan.packetForTask(blocked);
    const Graphics::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(consumer);
    ASSERT_TRUE(firstPacket.valid());
    EXPECT_EQ(firstPacket, overridePacket);
    EXPECT_NE(firstPacket, blockedPacket);
    EXPECT_NE(blockedPacket, consumerPacket);
    EXPECT_EQ(
        compiledPlan.packetizationDecisionForTask(overrideTask),
        Graphics::GpuTaskPacketizationDecision::MergedExplicit
    );
    EXPECT_EQ(
        compiledPlan.packetizationDecisionForTask(blocked),
        Graphics::GpuTaskPacketizationDecision::CrossQueueConsumerFrontier
    );
}

TEST(GpuTaskGraph, FrontierSafeConsumerFrontierOverrideRequiresExplicitImmediateDependency){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
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
    const Graphics::GpuGraphResourceId orderedHandoff = AddHazardDomain(
        graph,
        Name("tests/task_graph/frontier_override_ordered_handoff"),
        "Frontier Override Ordered Handoff"
    );
    ASSERT_TRUE(orderedHandoff.valid());

    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc firstDesc;
    firstDesc
        .setIdentity(Name("tests/task_graph/frontier_override_first"))
        .setMarkerLabel("Frontier Override First")
        .setQueue(graphicsRequest)
        .setScheduling(firstScheduling)
    ;
    const Graphics::GpuTaskId first = graph.addTask(firstDesc);
    ASSERT_TRUE(first.valid());

    Graphics::GpuTaskSchedulingHint immediateFinalizeScheduling;
    immediateFinalizeScheduling.allowPacketMerge = true;
    immediateFinalizeScheduling.mergeWithPrevious = true;
    immediateFinalizeScheduling.allowMergeAcrossConsumerFrontier = true;
    Graphics::GpuTaskResourceUse immediateFinalizeUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = orderedHandoff,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc immediateFinalizeDesc;
    immediateFinalizeDesc
        .setIdentity(Name("tests/task_graph/frontier_override_immediate_finalize"))
        .setMarkerLabel("Frontier Override Immediate Finalize")
        .setQueue(graphicsRequest)
        .setScheduling(immediateFinalizeScheduling)
        .setDependencies(&first, 1u)
        .setResourceUses(immediateFinalizeUses, LengthOf(immediateFinalizeUses))
    ;
    const Graphics::GpuTaskId immediateFinalize = graph.addTask(immediateFinalizeDesc);
    ASSERT_TRUE(immediateFinalize.valid());

    // The inferred producer edge fixes this task after the finalizer, but its only explicit dependency remains
    // the first task. It must not inherit the finalizer's consumer-frontier override.
    Graphics::GpuTaskSchedulingHint unrelatedScheduling;
    unrelatedScheduling.allowPacketMerge = true;
    unrelatedScheduling.mergeWithPrevious = true;
    unrelatedScheduling.allowMergeAcrossConsumerFrontier = true;
    const Graphics::GpuTaskResourceUse unrelatedUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = orderedHandoff,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc unrelatedDesc;
    unrelatedDesc
        .setIdentity(Name("tests/task_graph/frontier_override_unrelated_successor"))
        .setMarkerLabel("Frontier Override Unrelated Successor")
        .setQueue(graphicsRequest)
        .setScheduling(unrelatedScheduling)
        .setDependencies(&first, 1u)
        .setResourceUses(unrelatedUses, LengthOf(unrelatedUses))
    ;
    const Graphics::GpuTaskId unrelatedSuccessor = graph.addTask(unrelatedDesc);
    ASSERT_TRUE(unrelatedSuccessor.valid());

    // Keep the direct cross-queue edge from the first task, while sequencing the consumer after the attempted
    // unrelated merge so packet order cannot accidentally make this test pass.
    const Graphics::GpuTaskId consumerDependencies[] = { first, unrelatedSuccessor };
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/frontier_override_compute_consumer"))
        .setMarkerLabel("Frontier Override Compute Consumer")
        .setQueue(computeRequest)
        .setDependencies(consumerDependencies, LengthOf(consumerDependencies))
    ;
    const Graphics::GpuTaskId computeConsumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(computeConsumer.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 3u);

    const Graphics::GpuSubmissionPacketId firstPacket = compiledPlan.packetForTask(first);
    const Graphics::GpuSubmissionPacketId immediateFinalizePacket = compiledPlan.packetForTask(immediateFinalize);
    const Graphics::GpuSubmissionPacketId unrelatedPacket = compiledPlan.packetForTask(unrelatedSuccessor);
    const Graphics::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(computeConsumer);
    ASSERT_TRUE(firstPacket.valid());
    EXPECT_EQ(firstPacket, immediateFinalizePacket);
    EXPECT_NE(firstPacket, unrelatedPacket);
    EXPECT_NE(unrelatedPacket, consumerPacket);
    EXPECT_EQ(compiledPlan.packet(firstPacket).plan->taskCount, 2u);
    EXPECT_EQ(compiledPlan.packet(unrelatedPacket).plan->taskCount, 1u);
    EXPECT_TRUE(analysis.hasExplicitEdge(first, computeConsumer));
    ASSERT_EQ(compiledPlan.packet(consumerPacket).plan->dependencyCount, 1u);
    ASSERT_NE(compiledPlan.packet(consumerPacket).dependencies, nullptr);
    EXPECT_EQ(compiledPlan.packet(consumerPacket).dependencies[0u].producer, unrelatedPacket);
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        immediateFinalize,
        unrelatedSuccessor,
        orderedHandoff,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
}

TEST(GpuTaskGraph, FrontierSafePacketizationKeepsMergeWhenLaterTaskOwnsCrossQueueConsumer){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
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

    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc firstDesc;
    firstDesc
        .setIdentity(Name("tests/task_graph/frontier_merge_first"))
        .setMarkerLabel("Frontier Merge First")
        .setQueue(graphicsRequest)
        .setScheduling(firstScheduling)
    ;
    const Graphics::GpuTaskId first = graph.addTask(firstDesc);
    ASSERT_TRUE(first.valid());

    Graphics::GpuTaskSchedulingHint mergedScheduling;
    mergedScheduling.allowPacketMerge = true;
    mergedScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc mergedDesc;
    mergedDesc
        .setIdentity(Name("tests/task_graph/frontier_merge_producer"))
        .setMarkerLabel("Frontier Merge Producer")
        .setQueue(graphicsRequest)
        .setScheduling(mergedScheduling)
        .setDependencies(&first, 1u)
    ;
    const Graphics::GpuTaskId producer = graph.addTask(mergedDesc);
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/frontier_merge_compute_consumer"))
        .setMarkerLabel("Frontier Merge Compute Consumer")
        .setQueue(computeRequest)
        .setDependencies(&producer, 1u)
    ;
    const Graphics::GpuTaskId computeConsumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(computeConsumer.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 2u);

    const Graphics::GpuSubmissionPacketId firstPacket = compiledPlan.packetForTask(first);
    const Graphics::GpuSubmissionPacketId producerPacket = compiledPlan.packetForTask(producer);
    const Graphics::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(computeConsumer);
    ASSERT_TRUE(firstPacket.valid());
    EXPECT_EQ(firstPacket, producerPacket);
    EXPECT_NE(producerPacket, consumerPacket);
    EXPECT_EQ(compiledPlan.packet(firstPacket).plan->taskCount, 2u);
    EXPECT_NE(compiledPlan.packet(firstPacket).plan->queue, compiledPlan.packet(consumerPacket).plan->queue);
    ASSERT_EQ(compiledPlan.packet(consumerPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(consumerPacket).dependencies[0u].producer, producerPacket);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

