// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_frontier_packetization_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_SecondMergeDomainPath = "tests/task_graph/frontier_scored_second_domain";
constexpr AStringView s_CrossQueueMergeDomainPath = "tests/task_graph/frontier_scored_cross_queue";

using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, FrontierSafePacketizationSplitsBeforeCrossQueueConsumer){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::GpuTaskCommandRequirements graphicsCommands = GraphicsCommands();
    const Core::GpuTaskCommandRequirements computeCommands = ComputeCommands();

    Core::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.allowPacketMerge = true;
    Core::GpuTaskDesc firstDesc;
    firstDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierFirst)).setMarkerLabel("Frontier First").setScheduling(firstScheduling);
    const auto firstResult = graph.addTask(firstDesc, graphicsCommands);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());

    Core::GpuTaskSchedulingHint mergedScheduling;
    mergedScheduling.allowPacketMerge = true;
    mergedScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc mergedDesc;
    mergedDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierUnrelatedGraphics)).setMarkerLabel("Frontier Unrelated Graphics").setScheduling(mergedScheduling).setDependencies(Span(&first, 1u));
    const auto unrelatedGraphicsResult = graph.addTask(mergedDesc, graphicsCommands);
    ASSERT_TRUE(unrelatedGraphicsResult);
    const Core::GpuTaskId unrelatedGraphics = *unrelatedGraphicsResult;
    ASSERT_TRUE(unrelatedGraphics.valid());

    Core::GpuTaskDesc consumerDesc;
    consumerDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierComputeConsumer)).setMarkerLabel("Frontier Compute Consumer").setDependencies(Span(&first, 1u));
    const auto computeConsumerResult = graph.addTask(consumerDesc, computeCommands);
    ASSERT_TRUE(computeConsumerResult);
    const Core::GpuTaskId computeConsumer = *computeConsumerResult;
    ASSERT_TRUE(computeConsumer.valid());

    const Array<Core::GpuPhysicalQueueInfo, 2> queues = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };

    Core::GpuTaskGraphAnalysis explicitMergeAnalysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments explicitMergeAssignments(testArena.arena);
    Core::GpuCompiledGraph explicitMergeGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(explicitMergeAnalysis, explicitMergeAssignments, explicitMergeGraph, operationScratchArena, graph, topology));
    {
        const Core::GpuCompiledGraph::ReadView explicitMergePlan(explicitMergeGraph);

        ASSERT_EQ(explicitMergePlan.packetCount(), 2u);
        const Core::GpuSubmissionPacketId explicitFirstPacket = explicitMergePlan.packetForTask(first);
        const Core::GpuSubmissionPacketId explicitUnrelatedPacket = explicitMergePlan.packetForTask(unrelatedGraphics);
        const Core::GpuSubmissionPacketId explicitConsumerPacket = explicitMergePlan.packetForTask(computeConsumer);
        ASSERT_TRUE(explicitFirstPacket.valid());
        EXPECT_EQ(explicitFirstPacket, explicitUnrelatedPacket);
        EXPECT_NE(explicitFirstPacket, explicitConsumerPacket);
        const Core::GpuCompiledPacketView explicitConsumerView = explicitMergePlan.packet(explicitConsumerPacket);
        ASSERT_TRUE(explicitConsumerView.valid());
        ASSERT_EQ(explicitConsumerView.plan->dependencyCount, 1u);
        EXPECT_EQ(explicitConsumerView.dependencies[0u].producer, explicitFirstPacket);
    }

    Core::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Core::GpuTaskGraphAnalysis frontierAnalysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments frontierAssignments(testArena.arena);
    Core::GpuCompiledGraph frontierGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(frontierAnalysis, frontierAssignments, frontierGraph, operationScratchArena, graph, topology, frontierOptions));
    const Core::GpuCompiledGraph::ReadView frontierPlan(frontierGraph);

    ASSERT_EQ(frontierPlan.packetCount(), 3u);
    const Core::GpuSubmissionPacketId frontierFirstPacket = frontierPlan.packetForTask(first);
    const Core::GpuSubmissionPacketId frontierUnrelatedPacket = frontierPlan.packetForTask(unrelatedGraphics);
    const Core::GpuSubmissionPacketId frontierConsumerPacket = frontierPlan.packetForTask(computeConsumer);
    ASSERT_TRUE(frontierFirstPacket.valid());
    EXPECT_NE(frontierFirstPacket, frontierUnrelatedPacket);
    EXPECT_NE(frontierUnrelatedPacket, frontierConsumerPacket);
    EXPECT_NE(frontierPlan.packet(frontierFirstPacket).plan->queue, frontierPlan.packet(frontierConsumerPacket).plan->queue);
    EXPECT_EQ(frontierPlan.packet(frontierFirstPacket).plan->taskCount, 1u);
    EXPECT_EQ(frontierPlan.packet(frontierUnrelatedPacket).plan->taskCount, 1u);
    EXPECT_EQ(frontierPlan.packetizationDecisionForTask(unrelatedGraphics), Core::GpuTaskPacketizationDecision::CrossQueueConsumerFrontier);
    const Core::GpuCompiledPacketView frontierConsumerView = frontierPlan.packet(frontierConsumerPacket);
    ASSERT_TRUE(frontierConsumerView.valid());
    ASSERT_EQ(frontierConsumerView.plan->dependencyCount, 1u);
    EXPECT_EQ(frontierConsumerView.dependencies[0u].producer, frontierFirstPacket);
}

TEST(GpuTaskGraph, FrontierScoredPacketizationRejectsPrecedingBoundaryTask){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::GpuTaskCommandRequirements graphicsCommands{ Core::GpuQueueCapability::Graphics };
    const Name mergeDomain = NWB::Tests::TaskGraphTestUtils::s_TaskGraphFrontierScoredPrecedingBoundary;

    Core::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.cost = Core::GpuTaskCostHint::Medium;
    firstScheduling.forceSubmissionBoundary = true;
    firstScheduling.frontierScoredMergeDomain = mergeDomain;
    Core::GpuTaskDesc firstDesc;
    firstDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierScoredPrecedingBoundaryFirst)).setMarkerLabel("Frontier Scored Preceding Boundary First").setScheduling(firstScheduling);
    const auto firstResult = graph.addTask(firstDesc, graphicsCommands);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());

    Core::GpuTaskSchedulingHint successorScheduling;
    successorScheduling.cost = Core::GpuTaskCostHint::Tiny;
    successorScheduling.frontierScoredMergeDomain = mergeDomain;
    Core::GpuTaskDesc successorDesc;
    successorDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierScoredPrecedingBoundarySuccessor)).setMarkerLabel("Frontier Scored Preceding Boundary Successor").setScheduling(successorScheduling).setDependencies(Span(&first, 1u));
    const auto successorResult = graph.addTask(successorDesc, graphicsCommands);
    ASSERT_TRUE(successorResult);
    const Core::GpuTaskId successor = *successorResult;
    ASSERT_TRUE(successor.valid());

    Core::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierScored;
    SingleQueueCompile singleQueueCompile(testArena);
    ASSERT_TRUE(singleQueueCompile.compile(graph, options));
    const Core::GpuCompiledGraph::ReadView compiledPlan(singleQueueCompile.compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 2u);
    EXPECT_NE(compiledPlan.packetForTask(first), compiledPlan.packetForTask(successor));
    EXPECT_EQ(compiledPlan.packetizationDecisionForTask(successor), Core::GpuTaskPacketizationDecision::PrecedingTaskForcesBoundary);
}

TEST(GpuTaskGraph, FrontierScoredPacketizationRequiresNonemptyMergeDomain){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::GpuTaskCommandRequirements graphicsCommands{ Core::GpuQueueCapability::Graphics };

    Core::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.cost = Core::GpuTaskCostHint::Medium;
    Core::GpuTaskDesc firstDesc;
    firstDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierScoredMissingDomainFirst)).setMarkerLabel("Frontier Scored Missing Domain First").setScheduling(firstScheduling);
    const auto firstResult = graph.addTask(firstDesc, graphicsCommands);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());

    Core::GpuTaskSchedulingHint namedDomainScheduling;
    namedDomainScheduling.cost = Core::GpuTaskCostHint::Tiny;
    namedDomainScheduling.frontierScoredMergeDomain = Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierScoredMissingDomain);
    Core::GpuTaskDesc namedDomainDesc;
    namedDomainDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierScoredMissingDomainNamedSuccessor)).setMarkerLabel("Frontier Scored Missing Domain Named Successor").setScheduling(namedDomainScheduling).setDependencies(Span(&first, 1u));
    const auto namedDomainSuccessorResult = graph.addTask(namedDomainDesc, graphicsCommands);
    ASSERT_TRUE(namedDomainSuccessorResult);
    const Core::GpuTaskId namedDomainSuccessor = *namedDomainSuccessorResult;
    ASSERT_TRUE(namedDomainSuccessor.valid());

    Core::GpuTaskSchedulingHint emptyDomainScheduling;
    emptyDomainScheduling.cost = Core::GpuTaskCostHint::Tiny;
    Core::GpuTaskDesc emptyDomainDesc;
    emptyDomainDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuFrontierMissingEmpty)).setMarkerLabel("Frontier Scored Missing Domain Empty Successor").setScheduling(emptyDomainScheduling).setDependencies(Span(&namedDomainSuccessor, 1u));
    const auto emptyDomainSuccessorResult = graph.addTask(emptyDomainDesc, graphicsCommands);
    ASSERT_TRUE(emptyDomainSuccessorResult);
    const Core::GpuTaskId emptyDomainSuccessor = *emptyDomainSuccessorResult;
    ASSERT_TRUE(emptyDomainSuccessor.valid());

    Core::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierScored;
    SingleQueueCompile singleQueueCompile(testArena);
    ASSERT_TRUE(singleQueueCompile.compile(graph, options));
    const Core::GpuCompiledGraph::ReadView compiledPlan(singleQueueCompile.compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 3u);

    const Core::GpuSubmissionPacketId firstPacket = compiledPlan.packetForTask(first);
    const Core::GpuSubmissionPacketId namedDomainSuccessorPacket = compiledPlan.packetForTask(namedDomainSuccessor);
    const Core::GpuSubmissionPacketId emptyDomainSuccessorPacket = compiledPlan.packetForTask(emptyDomainSuccessor);
    ASSERT_TRUE(firstPacket.valid());
    EXPECT_NE(firstPacket, namedDomainSuccessorPacket);
    EXPECT_NE(namedDomainSuccessorPacket, emptyDomainSuccessorPacket);
    EXPECT_EQ(compiledPlan.packetizationDecisionForTask(namedDomainSuccessor), Core::GpuTaskPacketizationDecision::ScoredMergeDomainMismatch);
    EXPECT_EQ(compiledPlan.packetizationDecisionForTask(emptyDomainSuccessor), Core::GpuTaskPacketizationDecision::ScoredMergeDomainMismatch);
}

TEST(GpuTaskGraph, FrontierScoredPacketizationRequiresOneDomainAcrossPrecedingPacket){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::GpuTaskCommandRequirements graphicsCommands{ Core::GpuQueueCapability::Graphics };

    Core::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.cost = Core::GpuTaskCostHint::Medium;
    firstScheduling.frontierScoredMergeDomain = Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierScoredFirstDomain);
    Core::GpuTaskDesc firstDesc;
    firstDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierScoredMismatchedDomainFirst)).setMarkerLabel("Frontier Scored Mismatched Domain First").setScheduling(firstScheduling);
    const auto firstResult = graph.addTask(firstDesc, graphicsCommands);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());

    Core::GpuTaskSchedulingHint explicitScheduling;
    explicitScheduling.cost = Core::GpuTaskCostHint::Tiny;
    explicitScheduling.mergeWithPrevious = true;
    explicitScheduling.frontierScoredMergeDomain = Name(s_SecondMergeDomainPath);
    Core::GpuTaskDesc explicitDesc;
    explicitDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierScoredMismatchedDomainExplicit)).setMarkerLabel("Frontier Scored Mismatched Domain Explicit").setScheduling(explicitScheduling).setDependencies(Span(&first, 1u));
    const auto explicitTaskResult = graph.addTask(explicitDesc, graphicsCommands);
    ASSERT_TRUE(explicitTaskResult);
    const Core::GpuTaskId explicitTask = *explicitTaskResult;
    ASSERT_TRUE(explicitTask.valid());

    Core::GpuTaskSchedulingHint successorScheduling;
    successorScheduling.cost = Core::GpuTaskCostHint::Tiny;
    successorScheduling.frontierScoredMergeDomain = Name(s_SecondMergeDomainPath);
    Core::GpuTaskDesc successorDesc;
    successorDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierScoredMismatchedDomainSuccessor)).setMarkerLabel("Frontier Scored Mismatched Domain Successor").setScheduling(successorScheduling).setDependencies(Span(&explicitTask, 1u));
    const auto successorResult = graph.addTask(successorDesc, graphicsCommands);
    ASSERT_TRUE(successorResult);
    const Core::GpuTaskId successor = *successorResult;
    ASSERT_TRUE(successor.valid());

    Core::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierScored;
    SingleQueueCompile singleQueueCompile(testArena);
    ASSERT_TRUE(singleQueueCompile.compile(graph, options));
    const Core::GpuCompiledGraph::ReadView compiledPlan(singleQueueCompile.compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 2u);

    const Core::GpuSubmissionPacketId firstPacket = compiledPlan.packetForTask(first);
    const Core::GpuSubmissionPacketId explicitPacket = compiledPlan.packetForTask(explicitTask);
    const Core::GpuSubmissionPacketId successorPacket = compiledPlan.packetForTask(successor);
    ASSERT_TRUE(firstPacket.valid());
    EXPECT_EQ(firstPacket, explicitPacket);
    EXPECT_NE(explicitPacket, successorPacket);
    EXPECT_EQ(compiledPlan.packetizationDecisionForTask(explicitTask), Core::GpuTaskPacketizationDecision::MergedExplicit);
    EXPECT_EQ(compiledPlan.packetizationDecisionForTask(successor), Core::GpuTaskPacketizationDecision::ScoredMergeDomainMismatch);
}

TEST(GpuTaskGraph, FrontierScoredPacketizationPreservesCrossQueueConsumerFrontier){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::GpuTaskCommandRequirements graphicsCommands = GraphicsCommands();
    const Core::GpuTaskCommandRequirements computeCommands = ComputeCommands();

    Core::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Core::GpuTaskCostHint::Medium;
    producerScheduling.frontierScoredMergeDomain = Name(s_CrossQueueMergeDomainPath);
    Core::GpuTaskDesc producerDesc;
    producerDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierScoredFrontierProducer)).setMarkerLabel("Frontier Scored Frontier Producer").setScheduling(producerScheduling);
    const auto producerResult = graph.addTask(producerDesc, graphicsCommands);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;
    ASSERT_TRUE(producer.valid());

    Core::GpuTaskSchedulingHint successorScheduling;
    successorScheduling.cost = Core::GpuTaskCostHint::Tiny;
    successorScheduling.frontierScoredMergeDomain = Name(s_CrossQueueMergeDomainPath);
    Core::GpuTaskDesc successorDesc;
    successorDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierScoredFrontierSuccessor)).setMarkerLabel("Frontier Scored Frontier Successor").setScheduling(successorScheduling).setDependencies(Span(&producer, 1u));
    const auto successorResult = graph.addTask(successorDesc, graphicsCommands);
    ASSERT_TRUE(successorResult);
    const Core::GpuTaskId successor = *successorResult;
    ASSERT_TRUE(successor.valid());

    Core::GpuTaskDesc consumerDesc;
    consumerDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierScoredComputeConsumer)).setMarkerLabel("Frontier Scored Compute Consumer").setDependencies(Span(&producer, 1u));
    const auto consumerResult = graph.addTask(consumerDesc, computeCommands);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());

    const Array<Core::GpuPhysicalQueueInfo, 2> queues = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierScored;
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology, options));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 3u);

    const Core::GpuSubmissionPacketId producerPacket = compiledPlan.packetForTask(producer);
    const Core::GpuSubmissionPacketId successorPacket = compiledPlan.packetForTask(successor);
    const Core::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(consumer);
    ASSERT_TRUE(producerPacket.valid());
    EXPECT_NE(producerPacket, successorPacket);
    EXPECT_NE(producerPacket, consumerPacket);
    EXPECT_EQ(compiledPlan.packetizationDecisionForTask(successor), Core::GpuTaskPacketizationDecision::CrossQueueConsumerFrontier);
    ASSERT_EQ(compiledPlan.packet(consumerPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(consumerPacket).dependencies[0u].producer, producerPacket);
}

TEST(GpuTaskGraph, FrontierScoredPacketizationCarriesTailPhysicalQueueFrontierForward){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::GpuTaskCommandRequirements graphicsCommands{ Core::GpuQueueCapability::Graphics };
    const Name mergeDomain = NWB::Tests::TaskGraphTestUtils::s_TaskGraphFrontierScoredTailFrontier;

    Core::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.cost = Core::GpuTaskCostHint::Medium;
    firstScheduling.frontierScoredMergeDomain = mergeDomain;
    Core::GpuTaskDesc firstDesc;
    firstDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierScoredTailFrontierFirst)).setMarkerLabel("Frontier Scored Tail Frontier First").setScheduling(firstScheduling);
    const auto firstResult = graph.addTask(firstDesc, graphicsCommands);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());

    Core::GpuTaskSchedulingHint mergedScheduling;
    mergedScheduling.cost = Core::GpuTaskCostHint::Tiny;
    mergedScheduling.frontierScoredMergeDomain = mergeDomain;
    Core::GpuTaskDesc mergedDesc;
    mergedDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierScoredTailFrontierMerged)).setMarkerLabel("Frontier Scored Tail Frontier Merged").setScheduling(mergedScheduling).setDependencies(Span(&first, 1u));
    const auto mergedResult = graph.addTask(mergedDesc, graphicsCommands);
    ASSERT_TRUE(mergedResult);
    const Core::GpuTaskId merged = *mergedResult;
    ASSERT_TRUE(merged.valid());

    Core::GpuTaskSchedulingHint blockedScheduling;
    blockedScheduling.cost = Core::GpuTaskCostHint::Tiny;
    blockedScheduling.frontierScoredMergeDomain = mergeDomain;
    Core::GpuTaskDesc blockedDesc;
    blockedDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierScoredTailFrontierBlocked)).setMarkerLabel("Frontier Scored Tail Frontier Blocked").setScheduling(blockedScheduling).setDependencies(Span(&merged, 1u));
    const auto blockedResult = graph.addTask(blockedDesc, graphicsCommands);
    ASSERT_TRUE(blockedResult);
    const Core::GpuTaskId blocked = *blockedResult;
    ASSERT_TRUE(blocked.valid());

    const Name consumerIdentity = NWB::Tests::TaskGraphTestUtils::s_TaskGraphFrontierScoredTailFrontierConsumer;
    Core::GpuTaskSchedulingHint consumerScheduling;
    consumerScheduling.allowSameClassQueueRouting = true;
    Core::GpuTaskDesc consumerDesc;
    consumerDesc.setIdentity(consumerIdentity).setMarkerLabel("Frontier Scored Tail Frontier Consumer").setScheduling(consumerScheduling).setDependencies(Span(&merged, 1u));
    const auto consumerResult = graph.addTask(consumerDesc, graphicsCommands);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());

    Core::GpuPhysicalQueueInfo auxiliaryGraphicsQueue = GraphicsQueue(1u);
    auxiliaryGraphicsQueue.queueIndex = 1u;
    const Array<Core::GpuPhysicalQueueInfo, 2> queues = { GraphicsQueue(), auxiliaryGraphicsQueue, };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierScored;
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology, options));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 3u);

    const Core::GpuTaskQueueAssignment* const firstAssignment = assignments.find(first);
    const Core::GpuTaskQueueAssignment* const mergedAssignment = assignments.find(merged);
    const Core::GpuTaskQueueAssignment* const blockedAssignment = assignments.find(blocked);
    const Core::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumer);
    ASSERT_NE(firstAssignment, nullptr);
    ASSERT_NE(mergedAssignment, nullptr);
    ASSERT_NE(blockedAssignment, nullptr);
    ASSERT_NE(consumerAssignment, nullptr);
    EXPECT_EQ(firstAssignment->queue, queues[0u].id);
    EXPECT_EQ(mergedAssignment->queue, queues[0u].id);
    EXPECT_EQ(blockedAssignment->queue, queues[0u].id);
    // Same-class balancing moves the consumer to the idle graphics queue in the same family.
    EXPECT_EQ(consumerAssignment->queue, queues[1u].id);
    EXPECT_TRUE(consumerAssignment->modifiers & Core::GpuTaskQueueAssignmentModifier::SameClassLoadBalance);

    const Core::GpuSubmissionPacketId firstPacket = compiledPlan.packetForTask(first);
    const Core::GpuSubmissionPacketId mergedPacket = compiledPlan.packetForTask(merged);
    const Core::GpuSubmissionPacketId blockedPacket = compiledPlan.packetForTask(blocked);
    const Core::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(consumer);
    ASSERT_TRUE(firstPacket.valid());
    EXPECT_EQ(firstPacket, mergedPacket);
    EXPECT_NE(firstPacket, blockedPacket);
    EXPECT_NE(blockedPacket, consumerPacket);
    EXPECT_EQ(compiledPlan.packetizationDecisionForTask(merged), Core::GpuTaskPacketizationDecision::MergedFrontierScored);
    EXPECT_EQ(compiledPlan.packetizationDecisionForTask(blocked), Core::GpuTaskPacketizationDecision::CrossQueueConsumerFrontier);
}

TEST(GpuTaskGraph, FrontierSafePacketizationKeepsFrontierAfterExplicitOverride){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::GpuTaskCommandRequirements graphicsCommands = GraphicsCommands();
    const Core::GpuTaskCommandRequirements computeCommands = ComputeCommands();

    Core::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.allowPacketMerge = true;
    Core::GpuTaskDesc firstDesc;
    firstDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierOverridePersistenceFirst)).setMarkerLabel("Frontier Override Persistence First").setScheduling(firstScheduling);
    const auto firstResult = graph.addTask(firstDesc, graphicsCommands);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());

    Core::GpuTaskSchedulingHint overrideScheduling;
    overrideScheduling.allowPacketMerge = true;
    overrideScheduling.mergeWithPrevious = true;
    overrideScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc overrideDesc;
    overrideDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierOverridePersistenceMerged)).setMarkerLabel("Frontier Override Persistence Merged").setScheduling(overrideScheduling).setDependencies(Span(&first, 1u));
    const auto overrideTaskResult = graph.addTask(overrideDesc, graphicsCommands);
    ASSERT_TRUE(overrideTaskResult);
    const Core::GpuTaskId overrideTask = *overrideTaskResult;
    ASSERT_TRUE(overrideTask.valid());

    Core::GpuTaskSchedulingHint blockedScheduling;
    blockedScheduling.allowPacketMerge = true;
    blockedScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc blockedDesc;
    blockedDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierOverridePersistenceBlocked)).setMarkerLabel("Frontier Override Persistence Blocked").setScheduling(blockedScheduling).setDependencies(Span(&overrideTask, 1u));
    const auto blockedResult = graph.addTask(blockedDesc, graphicsCommands);
    ASSERT_TRUE(blockedResult);
    const Core::GpuTaskId blocked = *blockedResult;
    ASSERT_TRUE(blocked.valid());

    Core::GpuTaskDesc consumerDesc;
    consumerDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierOverridePersistenceConsumer)).setMarkerLabel("Frontier Override Persistence Consumer").setDependencies(Span(&first, 1u));
    const auto consumerResult = graph.addTask(consumerDesc, computeCommands);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());

    const Array<Core::GpuPhysicalQueueInfo, 2> queues = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology, options));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 3u);

    const Core::GpuSubmissionPacketId firstPacket = compiledPlan.packetForTask(first);
    const Core::GpuSubmissionPacketId overridePacket = compiledPlan.packetForTask(overrideTask);
    const Core::GpuSubmissionPacketId blockedPacket = compiledPlan.packetForTask(blocked);
    const Core::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(consumer);
    ASSERT_TRUE(firstPacket.valid());
    EXPECT_EQ(firstPacket, overridePacket);
    EXPECT_NE(firstPacket, blockedPacket);
    EXPECT_NE(blockedPacket, consumerPacket);
    EXPECT_EQ(compiledPlan.packetizationDecisionForTask(overrideTask), Core::GpuTaskPacketizationDecision::MergedExplicit);
    EXPECT_EQ(compiledPlan.packetizationDecisionForTask(blocked), Core::GpuTaskPacketizationDecision::CrossQueueConsumerFrontier);
}

TEST(GpuTaskGraph, FrontierSafeConsumerFrontierOverrideRequiresExplicitImmediateDependency){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::GpuTaskCommandRequirements graphicsCommands = GraphicsCommands();
    const Core::GpuTaskCommandRequirements computeCommands = ComputeCommands();
    const auto orderedHandoffResult = AddHazardDomain(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierOverrideOrderedHandoff), "Frontier Override Ordered Handoff");
    ASSERT_TRUE(orderedHandoffResult);
    const Core::GpuGraphResourceId orderedHandoff = *orderedHandoffResult;
    ASSERT_TRUE(orderedHandoff.valid());

    Core::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.allowPacketMerge = true;
    Core::GpuTaskDesc firstDesc;
    firstDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierOverrideFirst)).setMarkerLabel("Frontier Override First").setScheduling(firstScheduling);
    const auto firstResult = graph.addTask(firstDesc, graphicsCommands);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());

    Core::GpuTaskSchedulingHint immediateFinalizeScheduling;
    immediateFinalizeScheduling.allowPacketMerge = true;
    immediateFinalizeScheduling.mergeWithPrevious = true;
    immediateFinalizeScheduling.allowMergeAcrossConsumerFrontier = true;
    Array<Core::GpuTaskResourceUse, 1> immediateFinalizeUses = { Core::GpuTaskResourceUse{ .resource = orderedHandoff, .range = {}, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::Write, }, };
    Core::GpuTaskDesc immediateFinalizeDesc;
    immediateFinalizeDesc
        .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierOverrideImmediateFinalize))
        .setMarkerLabel("Frontier Override Immediate Finalize")
        .setScheduling(immediateFinalizeScheduling)
        .setDependencies(Span(&first, 1u))
        .setResourceUses(immediateFinalizeUses)
    ;
    const auto immediateFinalizeResult = graph.addTask(immediateFinalizeDesc, graphicsCommands);
    ASSERT_TRUE(immediateFinalizeResult);
    const Core::GpuTaskId immediateFinalize = *immediateFinalizeResult;
    ASSERT_TRUE(immediateFinalize.valid());

    // Inferred ordering after the finalizer must not inherit the finalizer frontier override.
    Core::GpuTaskSchedulingHint unrelatedScheduling;
    unrelatedScheduling.allowPacketMerge = true;
    unrelatedScheduling.mergeWithPrevious = true;
    unrelatedScheduling.allowMergeAcrossConsumerFrontier = true;
    const Array<Core::GpuTaskResourceUse, 1> unrelatedUses = { Core::GpuTaskResourceUse{ .resource = orderedHandoff, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, }, };
    Core::GpuTaskDesc unrelatedDesc;
    unrelatedDesc
        .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierOverrideUnrelatedSuccessor))
        .setMarkerLabel("Frontier Override Unrelated Successor")
        .setScheduling(unrelatedScheduling)
        .setDependencies(Span(&first, 1u))
        .setResourceUses(unrelatedUses)
    ;
    const auto unrelatedSuccessorResult = graph.addTask(unrelatedDesc, graphicsCommands);
    ASSERT_TRUE(unrelatedSuccessorResult);
    const Core::GpuTaskId unrelatedSuccessor = *unrelatedSuccessorResult;
    ASSERT_TRUE(unrelatedSuccessor.valid());

    // Keep the cross-queue edge while sequencing the consumer after the merge, so packet order cannot fake a pass.
    const Array<Core::GpuTaskId, 2> consumerDependencies = { first, unrelatedSuccessor };
    Core::GpuTaskDesc consumerDesc;
    consumerDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierOverrideComputeConsumer)).setMarkerLabel("Frontier Override Compute Consumer").setDependencies(consumerDependencies);
    const auto computeConsumerResult = graph.addTask(consumerDesc, computeCommands);
    ASSERT_TRUE(computeConsumerResult);
    const Core::GpuTaskId computeConsumer = *computeConsumerResult;
    ASSERT_TRUE(computeConsumer.valid());

    const Array<Core::GpuPhysicalQueueInfo, 2> queues = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology, frontierOptions));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 3u);

    const Core::GpuSubmissionPacketId firstPacket = compiledPlan.packetForTask(first);
    const Core::GpuSubmissionPacketId immediateFinalizePacket = compiledPlan.packetForTask(immediateFinalize);
    const Core::GpuSubmissionPacketId unrelatedPacket = compiledPlan.packetForTask(unrelatedSuccessor);
    const Core::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(computeConsumer);
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
    EXPECT_TRUE(HasInferredHazard(analysis, immediateFinalize, unrelatedSuccessor, orderedHandoff, Core::GpuTaskHazardType::ReadAfterWrite));
}

TEST(GpuTaskGraph, FrontierSafePacketizationKeepsMergeWhenLaterTaskOwnsCrossQueueConsumer){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::GpuTaskCommandRequirements graphicsCommands = GraphicsCommands();
    const Core::GpuTaskCommandRequirements computeCommands = ComputeCommands();

    Core::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.allowPacketMerge = true;
    Core::GpuTaskDesc firstDesc;
    firstDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierMergeFirst)).setMarkerLabel("Frontier Merge First").setScheduling(firstScheduling);
    const auto firstResult = graph.addTask(firstDesc, graphicsCommands);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());

    Core::GpuTaskSchedulingHint mergedScheduling;
    mergedScheduling.allowPacketMerge = true;
    mergedScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc mergedDesc;
    mergedDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierMergeProducer)).setMarkerLabel("Frontier Merge Producer").setScheduling(mergedScheduling).setDependencies(Span(&first, 1u));
    const auto producerResult = graph.addTask(mergedDesc, graphicsCommands);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;
    ASSERT_TRUE(producer.valid());

    Core::GpuTaskDesc consumerDesc;
    consumerDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFrontierMergeComputeConsumer)).setMarkerLabel("Frontier Merge Compute Consumer").setDependencies(Span(&producer, 1u));
    const auto computeConsumerResult = graph.addTask(consumerDesc, computeCommands);
    ASSERT_TRUE(computeConsumerResult);
    const Core::GpuTaskId computeConsumer = *computeConsumerResult;
    ASSERT_TRUE(computeConsumer.valid());

    const Array<Core::GpuPhysicalQueueInfo, 2> queues = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology, frontierOptions));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 2u);

    const Core::GpuSubmissionPacketId firstPacket = compiledPlan.packetForTask(first);
    const Core::GpuSubmissionPacketId producerPacket = compiledPlan.packetForTask(producer);
    const Core::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(computeConsumer);
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

