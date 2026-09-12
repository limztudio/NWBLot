// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_lifecycle_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_packet_compilation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, DeduplicatesMergedPacketExternalDependenciesInTaskOrder){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuExternalCompletionId completions[] = {
        graph.importExternalCompletion(
            Graphics::GpuExternalCompletionDesc{}
                .setIdentity(Name("tests/task_graph/merged_external_a"))
                .setMarkerLabel("Merged External A")
        ),
        graph.importExternalCompletion(
            Graphics::GpuExternalCompletionDesc{}
                .setIdentity(Name("tests/task_graph/merged_external_b"))
                .setMarkerLabel("Merged External B")
        ),
        graph.importExternalCompletion(
            Graphics::GpuExternalCompletionDesc{}
                .setIdentity(Name("tests/task_graph/merged_external_c"))
                .setMarkerLabel("Merged External C")
        ),
    };
    for(const Graphics::GpuExternalCompletionId completion : completions)
        ASSERT_TRUE(completion.valid());

    const Graphics::GpuExternalCompletionId firstExternalDependencies[] = {
        completions[1u],
        completions[0u],
        completions[1u],
    };
    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc firstDesc;
    firstDesc
        .setIdentity(Name("tests/task_graph/merged_external_first"))
        .setMarkerLabel("Merged External First")
        .setScheduling(firstScheduling)
        .setExternalDependencies(firstExternalDependencies, LengthOf(firstExternalDependencies))
    ;
    const Graphics::GpuTaskId first = graph.addTask(firstDesc);
    ASSERT_TRUE(first.valid());

    const Graphics::GpuExternalCompletionId secondExternalDependencies[] = {
        completions[0u],
        completions[2u],
        completions[1u],
        completions[2u],
    };
    Graphics::GpuTaskSchedulingHint secondScheduling;
    secondScheduling.allowPacketMerge = true;
    secondScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc secondDesc;
    secondDesc
        .setIdentity(Name("tests/task_graph/merged_external_second"))
        .setMarkerLabel("Merged External Second")
        .setScheduling(secondScheduling)
        .setDependencies(&first, 1u)
        .setExternalDependencies(secondExternalDependencies, LengthOf(secondExternalDependencies))
    ;
    const Graphics::GpuTaskId second = graph.addTask(secondDesc);
    ASSERT_TRUE(second.valid());

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


    const auto& analysisDependencies =
        analysis.externalDependencies()
    ;
    ASSERT_EQ(analysisDependencies.size(), 5u);
    EXPECT_EQ(analysisDependencies[0u].completion, completions[1u]);
    EXPECT_EQ(analysisDependencies[0u].consumer, first);
    EXPECT_EQ(analysisDependencies[1u].completion, completions[0u]);
    EXPECT_EQ(analysisDependencies[1u].consumer, first);
    EXPECT_EQ(analysisDependencies[2u].completion, completions[0u]);
    EXPECT_EQ(analysisDependencies[2u].consumer, second);
    EXPECT_EQ(analysisDependencies[3u].completion, completions[2u]);
    EXPECT_EQ(analysisDependencies[3u].consumer, second);
    EXPECT_EQ(analysisDependencies[4u].completion, completions[1u]);
    EXPECT_EQ(analysisDependencies[4u].consumer, second);

    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(first);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetForTask(second), packet);
    ASSERT_EQ(compiledPlan.packet(packet).plan->taskCount, 2u);
    ASSERT_EQ(compiledPlan.packet(packet).plan->externalDependencyCount, 3u);
    const Graphics::GpuExternalCompletionId* const packetDependencies = compiledPlan.packet(packet).externalDependencies;
    ASSERT_NE(packetDependencies, nullptr);
    EXPECT_EQ(packetDependencies[0u], completions[1u]);
    EXPECT_EQ(packetDependencies[1u], completions[0u]);
    EXPECT_EQ(packetDependencies[2u], completions[2u]);
    EXPECT_EQ(compiledPlan.compileStatistics().declaredExternalDependencyCount, analysisDependencies.size());
    EXPECT_EQ(compiledPlan.compileStatistics().packetExternalDependencyCount, 3u);
}

TEST(GpuTaskGraph, CompilesOneTaskPacketsWithDependenciesAndLifecycleBoundaries){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuExternalCompletionId completion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/packet_external"))
            .setMarkerLabel("External Completion")
    );
    ASSERT_TRUE(completion.valid());
    Graphics::GpuTaskDesc firstDesc;
    firstDesc
        .setIdentity(Name("tests/task_graph/packet_first"))
        .setMarkerLabel("Packet First")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Compute,
            Graphics::GpuQueuePreference::Compute,
            true,
            true,
        })
    ;
    const Graphics::GpuTaskId first = graph.addTask<PacketLifecycleTask>(
        firstDesc,
        PacketLifecycleTask::Payload{}
    );
    ASSERT_TRUE(first.valid());

    Graphics::GpuTaskDesc secondDesc;
    secondDesc
        .setIdentity(Name("tests/task_graph/packet_second"))
        .setMarkerLabel("Packet Second")
        .setDependencies(&first, 1u)
        .setExternalDependencies(&completion, 1u)
    ;
    const Graphics::GpuTaskId second = graph.addTask<PacketLifecycleTask>(
        secondDesc,
        PacketLifecycleTask::Payload{}
    );
    ASSERT_TRUE(second.valid());

    Graphics::GpuTaskDesc transferDesc;
    transferDesc
        .setIdentity(Name("tests/task_graph/packet_transfer"))
        .setMarkerLabel("Packet Transfer")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            false,
            false,
        })
    ;
    const Graphics::GpuTaskId transfer = graph.addTask<PacketLifecycleTask>(
        transferDesc,
        PacketLifecycleTask::Payload{}
    );
    ASSERT_TRUE(transfer.valid());

    const Graphics::GpuGraphResourceId recoveryDomain = AddHazardDomain(
        graph,
        Name("tests/task_graph/packet_recovery_domain"),
        "Frame Recovery Timing"
    );
    ASSERT_TRUE(recoveryDomain.valid());
    const Graphics::GpuTaskResourceUse recoveryUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = recoveryDomain,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskSchedulingHint recoveryScheduling;
    recoveryScheduling.forceSubmissionBoundary = true;
    recoveryScheduling.allowPacketMerge = false;
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    recoveryScheduling.isRecoverySubmission = true;
    Graphics::GpuTaskDesc recoveryDesc;
    recoveryDesc
        .setIdentity(Name("tests/task_graph/packet_recovery"))
        .setMarkerLabel("Frame Recovery")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(recoveryScheduling)
        .setResourceUses(recoveryUses, LengthOf(recoveryUses))
    ;
    const Graphics::GpuTaskId recovery = graph.addTask<PacketLifecycleTask>(
        recoveryDesc,
        PacketLifecycleTask::Payload{}
    );
    ASSERT_TRUE(recovery.valid());

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
    {
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);
        const Graphics::GpuTaskGraph::DeclarationReadView& declarations = reads.declarations;
        const Graphics::GpuCompiledGraph::ReadView& compiledPlan = reads.compiled;

        ASSERT_TRUE(reads.valid());
        ASSERT_EQ(compiledPlan.taskCount(), 4u);
        ASSERT_EQ(compiledPlan.packetCount(), 4u);
        const Graphics::GpuPhysicalQueueTopology compiledQueueTopology = compiledPlan.queueTopology();
        ASSERT_NE(compiledQueueTopology.queues, nullptr);
        ASSERT_EQ(compiledQueueTopology.queueCount, LengthOf(queues));
        EXPECT_EQ(compiledQueueTopology.queues[0u].id, queues[0u].id);
        EXPECT_EQ(compiledQueueTopology.queues[1u].id, queues[1u].id);
        EXPECT_EQ(compiledQueueTopology.queues[2u].id, queues[2u].id);

        const Graphics::GpuTaskGraphCompileStatistics compileStatistics = compiledPlan.compileStatistics();
        ASSERT_TRUE(compileStatistics.valid());
        EXPECT_EQ(compileStatistics.graphGeneration, compiledPlan.generation());
        EXPECT_EQ(compileStatistics.planGeneration, compiledPlan.planGeneration());
        EXPECT_EQ(compileStatistics.deviceGeneration, compiledPlan.deviceGeneration());
        EXPECT_EQ(compileStatistics.taskCount, compiledPlan.taskCount());
        EXPECT_EQ(compileStatistics.resourceCount, declarations.resourceCount());
        EXPECT_EQ(compileStatistics.resourceUseCount, 1u);
        EXPECT_EQ(compileStatistics.explicitDependencyCount, 1u);
        EXPECT_EQ(compileStatistics.inferredDependencyCount, 0u);
        EXPECT_EQ(compileStatistics.declaredExternalDependencyCount, 1u);
        EXPECT_EQ(compileStatistics.initialOwnershipExternalDependencyCount, 0u);
        EXPECT_EQ(compileStatistics.externalDependencyCount, 1u);
        EXPECT_EQ(compileStatistics.packetCount, compiledPlan.packetCount());
        EXPECT_EQ(compileStatistics.packetDependencyCount, 1u);
        EXPECT_EQ(compileStatistics.packetExternalDependencyCount, 1u);
        EXPECT_EQ(compileStatistics.crossQueuePacketDependencyCount, 1u);
        EXPECT_EQ(compileStatistics.crossFamilyPacketDependencyCount, 1u);
        EXPECT_EQ(compileStatistics.mergedTaskCount, 0u);
        EXPECT_EQ(compileStatistics.recordingFrontierCount, 1u);
        EXPECT_EQ(
            compileStatistics.taskCountByQueueClass[Graphics::CommandQueue::Graphics],
            2u
        );
        EXPECT_EQ(
            compileStatistics.taskCountByQueueClass[Graphics::CommandQueue::Compute],
            1u
        );
        EXPECT_EQ(
            compileStatistics.taskCountByQueueClass[Graphics::CommandQueue::Transfer],
            1u
        );
        EXPECT_EQ(
            compileStatistics.packetCountByQueueClass[Graphics::CommandQueue::Graphics],
            2u
        );
        EXPECT_EQ(
            compileStatistics.packetCountByQueueClass[Graphics::CommandQueue::Compute],
            1u
        );
        EXPECT_EQ(
            compileStatistics.packetCountByQueueClass[Graphics::CommandQueue::Transfer],
            1u
        );
        usize packetizationDecisionCount = 0u;
        for(const usize count : compileStatistics.packetizationDecisionCounts)
            packetizationDecisionCount += count;
        EXPECT_EQ(packetizationDecisionCount, compiledPlan.taskCount());
        EXPECT_GE(compileStatistics.analysisSeconds, 0.0);
        EXPECT_GE(compileStatistics.validationSeconds, 0.0);
        EXPECT_GE(compileStatistics.dependencyAnalysisSeconds, 0.0);
        EXPECT_GE(compileStatistics.hazardAnalysisSeconds, 0.0);
        EXPECT_GE(compileStatistics.topologicalOrderSeconds, 0.0);
        EXPECT_LE(
            compileStatistics.validationSeconds
                + compileStatistics.dependencyAnalysisSeconds
                + compileStatistics.hazardAnalysisSeconds
                + compileStatistics.topologicalOrderSeconds,
            compileStatistics.analysisSeconds + 1.0e-9
        );
        EXPECT_GE(compileStatistics.queueAssignmentSeconds, 0.0);
        EXPECT_GE(compileStatistics.planningSeconds, 0.0);
        EXPECT_GE(compileStatistics.packetizationSeconds, 0.0);
        EXPECT_GE(compileStatistics.resourceStatePlanningSeconds, 0.0);
        EXPECT_GE(compileStatistics.packetDependencyPlanningSeconds, 0.0);
        EXPECT_GE(compileStatistics.totalSeconds, 0.0);

        const Graphics::GpuSubmissionPacketId firstPacket = compiledPlan.packetForTask(first);
        const Graphics::GpuSubmissionPacketId secondPacket = compiledPlan.packetForTask(second);
        const Graphics::GpuSubmissionPacketId transferPacket = compiledPlan.packetForTask(transfer);
        const Graphics::GpuSubmissionPacketId recoveryPacket = compiledPlan.packetForTask(recovery);
        ASSERT_TRUE(firstPacket.valid());
        ASSERT_TRUE(secondPacket.valid());
        ASSERT_TRUE(transferPacket.valid());
        ASSERT_TRUE(recoveryPacket.valid());
        EXPECT_NE(firstPacket, secondPacket);
        EXPECT_NE(recoveryPacket, secondPacket);
        EXPECT_TRUE(compiledPlan.tasksSharePacket(first, first));
        EXPECT_FALSE(compiledPlan.tasksSharePacket(first, second));
        EXPECT_FALSE(compiledPlan.tasksSharePacket(first, {}));
        EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(first, second));
        EXPECT_FALSE(compiledPlan.taskPrecedesOrSharesPacket(second, first));
        EXPECT_FALSE(compiledPlan.taskPrecedesOrSharesPacket(first, {}));
        EXPECT_FALSE(compiledPlan.taskPrecedesInSamePacket(first, second));
        EXPECT_FALSE(compiledPlan.taskPrecedesInSamePacket(first, first));
        EXPECT_FALSE(compiledPlan.tasksFormContiguousPacketSequence(nullptr, 0u));
        EXPECT_FALSE(compiledPlan.taskJoinsAcceptedQueueFrontier(first));
        EXPECT_TRUE(compiledPlan.taskJoinsAcceptedQueueFrontier(recovery));
        EXPECT_FALSE(compiledPlan.taskJoinsAcceptedQueueFrontier({}));
        const Graphics::GpuPhysicalQueueInfo* const firstTaskQueue = compiledPlan.queueInfoForTask(first);
        const Graphics::GpuPhysicalQueueInfo* const recoveryTaskQueue = compiledPlan.queueInfoForTask(recovery);
        ASSERT_NE(firstTaskQueue, nullptr);
        ASSERT_NE(recoveryTaskQueue, nullptr);
        EXPECT_EQ(firstTaskQueue->id, compiledPlan.packet(firstPacket).plan->queue);
        EXPECT_EQ(recoveryTaskQueue->id, compiledPlan.packet(recoveryPacket).plan->queue);
        EXPECT_EQ(compiledPlan.queueInfoForTask({}), nullptr);
        const Graphics::GpuSubmissionPacketRange firstTwoPacketRange = compiledPlan.packetRange(
            firstPacket,
            secondPacket
        );
        ASSERT_TRUE(firstTwoPacketRange.valid());
        EXPECT_TRUE(compiledPlan.validPacketRange(firstTwoPacketRange));
        EXPECT_EQ(firstTwoPacketRange.first, firstPacket);
        EXPECT_EQ(firstTwoPacketRange.packetCount, 2u);
        const Graphics::GpuSubmissionPacketRange firstTwoTaskRange = compiledPlan.packetRangeForTasks(first, second);
        ASSERT_TRUE(firstTwoTaskRange.valid());
        EXPECT_EQ(firstTwoTaskRange.first, firstPacket);
        EXPECT_EQ(firstTwoTaskRange.packetCount, firstTwoPacketRange.packetCount);
        const Graphics::GpuSubmissionPacketRange fullPacketRange = compiledPlan.allPacketRange();
        ASSERT_TRUE(fullPacketRange.valid());
        EXPECT_TRUE(compiledPlan.validPacketRange(fullPacketRange));
        EXPECT_EQ(fullPacketRange.first, firstPacket);
        EXPECT_EQ(fullPacketRange.packetCount, compiledPlan.packetCount());
        EXPECT_FALSE(compiledPlan.packetRange(secondPacket, firstPacket).valid());
        EXPECT_FALSE(compiledPlan.packetRangeForTasks(second, first).valid());
        EXPECT_FALSE(compiledPlan.packetRangeForTasks(first, {}).valid());
        EXPECT_FALSE(compiledPlan.validPacketRange(Graphics::GpuSubmissionPacketRange{
            .first = firstPacket,
            .packetCount = compiledPlan.packetCount() + 1u,
        }));
        const Graphics::GpuCompiledPacketView secondPacketView = compiledPlan.packet(secondPacket);
        ASSERT_TRUE(secondPacketView.valid());
        ASSERT_EQ(secondPacketView.plan->taskCount, 1u);
        ASSERT_EQ(secondPacketView.plan->dependencyCount, 1u);
        ASSERT_EQ(secondPacketView.plan->externalDependencyCount, 1u);
        EXPECT_EQ(secondPacketView.dependencies[0u].producer, firstPacket);
        EXPECT_EQ(secondPacketView.externalDependencies[0u], completion);
        const Graphics::GpuCompiledPacketView recoveryPacketView = compiledPlan.packet(recoveryPacket);
        ASSERT_TRUE(recoveryPacketView.valid());
        EXPECT_EQ(recoveryPacketView.plan->dependencyCount, 0u);
        EXPECT_EQ(recoveryPacketView.plan->externalDependencyCount, 0u);
        EXPECT_TRUE(recoveryPacketView.plan->joinsAcceptedQueueFrontier);
        EXPECT_TRUE(recoveryPacketView.plan->isRecoverySubmission);

        const Graphics::GpuPhysicalQueueId firstQueue = compiledPlan.packet(firstPacket).plan->queue;
        const Graphics::QueueSubmissionToken firstToken{
            .value = 41u,
            .queue = Graphics::CommandQueue::Compute,
            .physicalQueueIndex = firstQueue.index,
            .deviceGeneration = firstQueue.deviceGeneration,
        };
        const Graphics::GpuTaskGraphExternalCompletionToken externalCompletionToken{
            .completion = completion,
            .token = firstToken,
        };
        EXPECT_TRUE(externalCompletionToken.validFor(compiledGraph, compiledPlan));
        const Graphics::GpuTaskGraphExternalCompletionToken otherQueueCompletionToken{
            .completion = completion,
            .token = Graphics::QueueSubmissionToken{
                .value = 40u,
                .queue = Graphics::CommandQueue::Transfer,
                .physicalQueueIndex = 3u,
                .deviceGeneration = compiledPlan.deviceGeneration(),
            },
        };
        EXPECT_TRUE(otherQueueCompletionToken.validFor(compiledGraph, compiledPlan));
        Graphics::GpuTaskGraphExternalCompletionToken staleExternalCompletionToken = externalCompletionToken;
        staleExternalCompletionToken.token.deviceGeneration = firstQueue.deviceGeneration == Limit<u16>::s_Max
            ? 1u
            : static_cast<u16>(firstQueue.deviceGeneration + 1u)
        ;
        EXPECT_FALSE(staleExternalCompletionToken.validFor(compiledGraph, compiledPlan));
    }

    compiledGraph.reset();
    const Graphics::GpuCompiledGraph::ReadView resetPlan(compiledGraph);
    const Graphics::GpuPhysicalQueueTopology resetQueueTopology = resetPlan.queueTopology();
    EXPECT_EQ(resetQueueTopology.queues, nullptr);
    EXPECT_EQ(resetQueueTopology.queueCount, 0u);
}

TEST(GpuTaskGraph, KeepsExplicitPacketDependenciesOutOfRecordingReadyFrontiers){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskId first = AddTask(
        graph,
        Name("tests/task_graph/recording_frontier_first"),
        "Recording Frontier First"
    );
    const Graphics::GpuTaskId second = AddTask(
        graph,
        Name("tests/task_graph/recording_frontier_second"),
        "Recording Frontier Second"
    );
    const Graphics::GpuTaskId thirdDependencies[] = { first };
    const Graphics::GpuTaskId third = AddTask(
        graph,
        Name("tests/task_graph/recording_frontier_third"),
        "Recording Frontier Third",
        thirdDependencies,
        LengthOf(thirdDependencies)
    );
    const Graphics::GpuTaskId fourthDependencies[] = { second, third };
    const Graphics::GpuTaskId fourth = AddTask(
        graph,
        Name("tests/task_graph/recording_frontier_fourth"),
        "Recording Frontier Fourth",
        fourthDependencies,
        LengthOf(fourthDependencies)
    );
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    ASSERT_TRUE(third.valid());
    ASSERT_TRUE(fourth.valid());

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


    const Graphics::GpuSubmissionPacketId firstPacket = compiledPlan.packetForTask(first);
    const Graphics::GpuSubmissionPacketId secondPacket = compiledPlan.packetForTask(second);
    const Graphics::GpuSubmissionPacketId thirdPacket = compiledPlan.packetForTask(third);
    const Graphics::GpuSubmissionPacketId fourthPacket = compiledPlan.packetForTask(fourth);
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_TRUE(thirdPacket.valid());
    ASSERT_TRUE(fourthPacket.valid());
    EXPECT_EQ(compiledPlan.packet(firstPacket).plan->recordingFrontier, 0u);
    EXPECT_EQ(compiledPlan.packet(secondPacket).plan->recordingFrontier, 0u);
    EXPECT_EQ(compiledPlan.packet(thirdPacket).plan->recordingFrontier, 0u);
    EXPECT_EQ(compiledPlan.packet(fourthPacket).plan->recordingFrontier, 0u);
    EXPECT_EQ(compiledPlan.compileStatistics().recordingFrontierCount, 1u);
    ASSERT_EQ(compiledPlan.packet(thirdPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(thirdPacket).dependencies[0u].producer, firstPacket);
    ASSERT_EQ(compiledPlan.packet(fourthPacket).plan->dependencyCount, 2u);
    bool hasSecondProducer = false;
    bool hasThirdProducer = false;
    for(const Graphics::GpuPacketDependency& dependency : {
        compiledPlan.packet(fourthPacket).dependencies[0u],
        compiledPlan.packet(fourthPacket).dependencies[1u],
    }){
        hasSecondProducer = hasSecondProducer || dependency.producer == secondPacket;
        hasThirdProducer = hasThirdProducer || dependency.producer == thirdPacket;
    }
    EXPECT_TRUE(hasSecondProducer);
    EXPECT_TRUE(hasThirdProducer);
}

TEST(GpuTaskGraph, PlansSchedulingPacketDependenciesInStableIncomingOrder){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskId first = AddTask(
        graph,
        Name("tests/task_graph/stable_packet_dependency_first"),
        "Stable Packet Dependency First"
    );
    const Graphics::GpuTaskId second = AddTask(
        graph,
        Name("tests/task_graph/stable_packet_dependency_second"),
        "Stable Packet Dependency Second"
    );
    const Graphics::GpuTaskId third = AddTask(
        graph,
        Name("tests/task_graph/stable_packet_dependency_third"),
        "Stable Packet Dependency Third"
    );
    const Graphics::GpuTaskId consumerDependencies[] = { third, first, second };
    const Graphics::GpuTaskId consumer = AddTask(
        graph,
        Name("tests/task_graph/stable_packet_dependency_consumer"),
        "Stable Packet Dependency Consumer",
        consumerDependencies,
        LengthOf(consumerDependencies)
    );
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    ASSERT_TRUE(third.valid());
    ASSERT_TRUE(consumer.valid());

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


    const Graphics::GpuTaskGraphSchedulingTaskIndexView producers = analysis.schedulingProducers(consumer);
    ASSERT_EQ(producers.taskCount, LengthOf(consumerDependencies));
    EXPECT_EQ(producers[0u], third.index);
    EXPECT_EQ(producers[1u], first.index);
    EXPECT_EQ(producers[2u], second.index);

    const Graphics::GpuSubmissionPacketId firstPacket = compiledPlan.packetForTask(first);
    const Graphics::GpuSubmissionPacketId secondPacket = compiledPlan.packetForTask(second);
    const Graphics::GpuSubmissionPacketId thirdPacket = compiledPlan.packetForTask(third);
    const Graphics::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(consumer);
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_TRUE(thirdPacket.valid());
    ASSERT_TRUE(consumerPacket.valid());
    ASSERT_EQ(compiledPlan.packet(consumerPacket).plan->dependencyCount, LengthOf(consumerDependencies));
    const Graphics::GpuPacketDependency* const dependencies = compiledPlan.packet(consumerPacket).dependencies;
    ASSERT_NE(dependencies, nullptr);
    EXPECT_EQ(dependencies[0u].producer, thirdPacket);
    EXPECT_EQ(dependencies[1u].producer, firstPacket);
    EXPECT_EQ(dependencies[2u].producer, secondPacket);
}

TEST(GpuTaskGraph, DerivesRecordingReadyFrontiersFromStateSeedProducers){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId buffer = AddBufferMetadata(
        graph,
        Name("tests/task_graph/recording_frontier_state_seed_buffer"),
        "Recording Frontier State Seed Buffer"
    );
    ASSERT_TRUE(buffer.valid());
    const Graphics::GpuTaskId orderingProducer = AddTask(
        graph,
        Name("tests/task_graph/recording_frontier_ordering_producer"),
        "Recording Frontier Ordering Producer"
    );
    ASSERT_TRUE(orderingProducer.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.allowParallelRecording = true;

    const Graphics::GpuTaskResourceUse producerUse{
        .resource = buffer,
        .range = {},
        .requiredState = Graphics::ResourceStates::CopyDest,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/recording_frontier_state_seed_producer"))
        .setMarkerLabel("Recording Frontier State Seed Producer")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setResourceUses(&producerUse, 1u)
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
    ASSERT_TRUE(producer.valid());

    const Graphics::GpuTaskId consumerDependencies[] = { producer, orderingProducer };
    const Graphics::GpuTaskResourceUse consumerUse{
        .resource = buffer,
        .range = {},
        .requiredState = Graphics::ResourceStates::CopySource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/recording_frontier_state_seed_consumer"))
        .setMarkerLabel("Recording Frontier State Seed Consumer")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(consumerDependencies, LengthOf(consumerDependencies))
        .setResourceUses(&consumerUse, 1u)
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(consumer.valid());

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


    const Graphics::GpuSubmissionPacketId orderingProducerPacket = compiledPlan.packetForTask(orderingProducer);
    const Graphics::GpuSubmissionPacketId producerPacket = compiledPlan.packetForTask(producer);
    const Graphics::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(consumer);
    const Graphics::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumer).plan;
    ASSERT_TRUE(orderingProducerPacket.valid());
    ASSERT_TRUE(producerPacket.valid());
    ASSERT_TRUE(consumerPacket.valid());
    ASSERT_NE(producerPacket, consumerPacket);
    ASSERT_NE(compiledConsumer, nullptr);
    EXPECT_EQ(compiledPlan.packet(producerPacket).plan->recordingFrontier, 0u);
    EXPECT_EQ(compiledPlan.packet(consumerPacket).plan->recordingFrontier, 1u);
    EXPECT_EQ(compiledPlan.compileStatistics().recordingFrontierCount, 2u);
    ASSERT_EQ(compiledPlan.packet(consumerPacket).plan->dependencyCount, 2u);
    const Graphics::GpuPacketDependency* const dependencies = compiledPlan.packet(consumerPacket).dependencies;
    ASSERT_NE(dependencies, nullptr);
    // The state seed projects producerPacket a second time after both scheduling dependencies. Deduplication retains
    // the original scheduling order rather than moving that producer to the end.
    EXPECT_EQ(dependencies[0u].producer, producerPacket);
    EXPECT_EQ(dependencies[1u].producer, orderingProducerPacket);
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);

    const Graphics::GpuPacketStateSeed* const stateSeeds = compiledPlan.findTask(consumer).prologueStateSeeds;
    ASSERT_NE(stateSeeds, nullptr);
    EXPECT_EQ(stateSeeds[0u].resource, buffer);
    EXPECT_EQ(stateSeeds[0u].sourcePacket, producerPacket);
}

TEST(GpuTaskGraph, MergesExplicitCompatibleSuccessorIntoOnePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuTaskSchedulingHint prefixScheduling;
    prefixScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/merged_prefix"))
        .setMarkerLabel("Merged Prefix")
        .setScheduling(prefixScheduling)
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    Graphics::GpuTaskSchedulingHint suffixScheduling;
    suffixScheduling.allowPacketMerge = true;
    suffixScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc suffixDesc;
    suffixDesc
        .setIdentity(Name("tests/task_graph/merged_suffix"))
        .setMarkerLabel("Merged Suffix")
        .setScheduling(suffixScheduling)
        .setDependencies(&prefix, 1u)
    ;
    const Graphics::GpuTaskId suffix = graph.addTask(suffixDesc);
    ASSERT_TRUE(suffix.valid());

    Graphics::GpuTaskSchedulingHint finalSuffixScheduling;
    finalSuffixScheduling.allowPacketMerge = true;
    finalSuffixScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc finalSuffixDesc;
    finalSuffixDesc
        .setIdentity(Name("tests/task_graph/merged_final_suffix"))
        .setMarkerLabel("Merged Final Suffix")
        .setScheduling(finalSuffixScheduling)
        .setDependencies(&suffix, 1u)
    ;
    const Graphics::GpuTaskId finalSuffix = graph.addTask(finalSuffixDesc);
    ASSERT_TRUE(finalSuffix.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_TRUE(compiledPlan.validFor(declarations));
    ASSERT_EQ(compiledPlan.packetCount(), 1u);

    const Graphics::GpuSubmissionPacketId prefixPacket = compiledPlan.packetForTask(prefix);
    const Graphics::GpuSubmissionPacketId suffixPacket = compiledPlan.packetForTask(suffix);
    const Graphics::GpuSubmissionPacketId finalSuffixPacket = compiledPlan.packetForTask(finalSuffix);
    ASSERT_TRUE(prefixPacket.valid());
    EXPECT_EQ(prefixPacket, suffixPacket);
    EXPECT_EQ(prefixPacket, finalSuffixPacket);
    const Graphics::GpuSubmissionPacket& packet = *compiledPlan.packet(prefixPacket).plan;
    ASSERT_EQ(packet.taskCount, 3u);
    ASSERT_NE(compiledPlan.packet(prefixPacket).tasks, nullptr);
    EXPECT_EQ(compiledPlan.packet(prefixPacket).tasks[0u], prefix);
    EXPECT_EQ(compiledPlan.packet(prefixPacket).tasks[1u], suffix);
    EXPECT_EQ(compiledPlan.packet(prefixPacket).tasks[2u], finalSuffix);
    EXPECT_EQ(packet.dependencyCount, 0u);
    EXPECT_EQ(
        compiledPlan.packetizationDecisionForTask(prefix),
        Graphics::GpuTaskPacketizationDecision::FirstTask
    );
    EXPECT_EQ(
        compiledPlan.packetizationDecisionForTask(suffix),
        Graphics::GpuTaskPacketizationDecision::MergedExplicit
    );
    EXPECT_EQ(
        compiledPlan.packetizationDecisionForTask(finalSuffix),
        Graphics::GpuTaskPacketizationDecision::MergedExplicit
    );

    const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics queueCompileStatistics =
        compiledPlan.physicalQueueCompileStatistics(queues[0u].id)
    ;
    ASSERT_TRUE(queueCompileStatistics.valid());
    EXPECT_EQ(queueCompileStatistics.queue, queues[0u].id);
    EXPECT_EQ(queueCompileStatistics.taskCount, 3u);
    EXPECT_EQ(queueCompileStatistics.packetCount, 1u);
    EXPECT_EQ(queueCompileStatistics.mergedTaskCount, 2u);
    EXPECT_EQ(queueCompileStatistics.prologueBarrierCount, 0u);
    EXPECT_EQ(queueCompileStatistics.epilogueBarrierCount, 0u);
}

TEST(GpuTaskGraph, MergesGraphicsComputeUavProducerIntoGraphicsVertexBufferConsumer){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId generatedVertexBuffer = AddBufferMetadata(
        graph,
        Name("tests/task_graph/generated_vertex_buffer"),
        "Generated Vertex Buffer",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(generatedVertexBuffer.valid());

    const Graphics::GpuQueueRequest graphicsComputeQueue{
        QueueCapabilities(
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueueCapability::Compute
        ),
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest graphicsRasterQueue{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Graphics::GpuTaskCostHint::Small;
    producerScheduling.overlapPreferred = false;
    producerScheduling.avoidQueueCrossing = true;
    producerScheduling.forceSubmissionBoundary = false;
    producerScheduling.allowPacketMerge = true;
    producerScheduling.mergeWithPrevious = false;
    const Graphics::GpuTaskResourceUse producerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = generatedVertexBuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId producer = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/generated_vertex_producer"))
            .setMarkerLabel("Generate Vertex Buffer")
            .setQueue(graphicsComputeQueue)
            .setScheduling(producerScheduling)
            .setResourceUses(producerUses, LengthOf(producerUses))
    );
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskSchedulingHint rasterScheduling = producerScheduling;
    rasterScheduling.mergeWithPrevious = true;
    rasterScheduling.allowMergeAcrossConsumerFrontier = true;
    const Graphics::GpuTaskResourceUse rasterUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = generatedVertexBuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::VertexBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskId raster = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/generated_vertex_raster"))
            .setMarkerLabel("Raster Generated Vertex Buffer")
            .setQueue(graphicsRasterQueue)
            .setScheduling(rasterScheduling)
            .setDependencies(&producer, 1u)
            .setResourceUses(rasterUses, LengthOf(rasterUses))
    );
    ASSERT_TRUE(raster.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    EXPECT_TRUE(analysis.hasExplicitEdge(producer, raster));
    EXPECT_TRUE(analysis.hasInferredEdge(producer, raster));
    const Graphics::GpuTaskDependencyEdge* const dependency = FindEdge(analysis, producer, raster);
    ASSERT_NE(dependency, nullptr);
    EXPECT_EQ(dependency->hazard, Graphics::GpuTaskHazardType::Explicit);
    EXPECT_FALSE(dependency->resource.valid());
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        raster,
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_EQ(analysis.topologicalOrder().size(), 2u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], producer);
    EXPECT_EQ(analysis.topologicalOrder()[1u], raster);

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
    const Graphics::GpuTaskQueueAssignment* const rasterAssignment = assignments.find(raster);
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(rasterAssignment, nullptr);
    EXPECT_EQ(producerAssignment->queue, queue.id);
    EXPECT_EQ(rasterAssignment->queue, queue.id);
    const Graphics::GpuSubmissionPacketId producerPacket = compiledPlan.packetForTask(producer);
    const Graphics::GpuSubmissionPacketId rasterPacket = compiledPlan.packetForTask(raster);
    ASSERT_TRUE(producerPacket.valid());
    ASSERT_TRUE(rasterPacket.valid());
    EXPECT_EQ(compiledPlan.packetCount(), 1u);
    EXPECT_EQ(producerPacket, rasterPacket);
    const Graphics::GpuSubmissionPacket& packet = *compiledPlan.packet(producerPacket).plan;
    EXPECT_EQ(packet.queue, queue.id);
    EXPECT_EQ(packet.dependencyCount, 0u);
    ASSERT_EQ(packet.taskCount, 2u);
    ASSERT_NE(compiledPlan.packet(producerPacket).tasks, nullptr);
    EXPECT_EQ(compiledPlan.packet(producerPacket).tasks[0u], producer);
    EXPECT_EQ(compiledPlan.packet(producerPacket).tasks[1u], raster);

    const Graphics::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
    const Graphics::GpuCompiledTask* const compiledRaster = compiledPlan.findTask(raster).plan;
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledRaster, nullptr);
    ASSERT_EQ(compiledProducer->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledRaster->prologueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const producerBarrier = compiledPlan.findTask(producer).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const rasterBarrier = compiledPlan.findTask(raster).prologueBarriers;
    ASSERT_NE(producerBarrier, nullptr);
    ASSERT_NE(rasterBarrier, nullptr);
    EXPECT_EQ(producerBarrier[0u].type, Graphics::GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(producerBarrier[0u].resource, generatedVertexBuffer);
    EXPECT_EQ(producerBarrier[0u].before, Graphics::ResourceStates::Common);
    EXPECT_EQ(producerBarrier[0u].after, Graphics::ResourceStates::UnorderedAccess);
    EXPECT_EQ(rasterBarrier[0u].type, Graphics::GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(rasterBarrier[0u].resource, generatedVertexBuffer);
    EXPECT_EQ(rasterBarrier[0u].before, Graphics::ResourceStates::UnorderedAccess);
    EXPECT_EQ(rasterBarrier[0u].after, Graphics::ResourceStates::VertexBuffer);
    EXPECT_EQ(rasterBarrier[0u].sourceQueue, queue.id);
    EXPECT_EQ(rasterBarrier[0u].destinationQueue, queue.id);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

