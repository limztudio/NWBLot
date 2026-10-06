// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_lifecycle_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;
constexpr u32 s_ThirdElementIndex = 2u;


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
        completions[s_ThirdElementIndex],
        completions[1u],
        completions[s_ThirdElementIndex],
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
    const Graphics::GpuPhysicalQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const auto& analysisDependencies =
        analysis.externalDependencies()
    ;
    ASSERT_EQ(analysisDependencies.size(), 5u);
    EXPECT_EQ(analysisDependencies[0u].completion, completions[1u]);
    EXPECT_EQ(analysisDependencies[0u].consumer, first);
    EXPECT_EQ(analysisDependencies[1u].completion, completions[0u]);
    EXPECT_EQ(analysisDependencies[1u].consumer, first);
    EXPECT_EQ(analysisDependencies[s_ThirdElementIndex].completion, completions[0u]);
    EXPECT_EQ(analysisDependencies[s_ThirdElementIndex].consumer, second);
    EXPECT_EQ(analysisDependencies[3u].completion, completions[s_ThirdElementIndex]);
    EXPECT_EQ(analysisDependencies[3u].consumer, second);
    EXPECT_EQ(analysisDependencies[4u].completion, completions[1u]);
    EXPECT_EQ(analysisDependencies[4u].consumer, second);

    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(first);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetForTask(second), packet);
    ASSERT_EQ(compiledPlan.packet(packet).plan->taskCount, s_ExpectedDualCount);
    ASSERT_EQ(compiledPlan.packet(packet).plan->externalDependencyCount, 3u);
    const Graphics::GpuExternalCompletionId* const packetDependencies = compiledPlan.packet(packet).externalDependencies;
    ASSERT_NE(packetDependencies, nullptr);
    EXPECT_EQ(packetDependencies[0u], completions[1u]);
    EXPECT_EQ(packetDependencies[1u], completions[0u]);
    EXPECT_EQ(packetDependencies[s_ThirdElementIndex], completions[s_ThirdElementIndex]);
    EXPECT_EQ(compiledPlan.compileStatistics().declaredExternalDependencyCount, analysisDependencies.size());
    EXPECT_EQ(compiledPlan.compileStatistics().packetExternalDependencyCount, 3u);
}

TEST(GpuTaskGraph, CompilesOneTaskPacketsWithDependenciesAndLifecycleBoundaries){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo producerQueue = DedicatedComputeQueue();
    const Graphics::QueueSubmissionToken completionToken{
        .value = 41u,
        .physicalQueueIndex = producerQueue.id.index,
        .deviceGeneration = producerQueue.id.deviceGeneration,
        .queue = producerQueue.queueClass,
    };
    const Graphics::GpuExternalCompletionId completion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/packet_external"))
            .setMarkerLabel("External Completion")
            .setToken(completionToken)
    );
    ASSERT_TRUE(completion.valid());
    Graphics::GpuTaskDesc firstDesc;
    firstDesc
        .setIdentity(Name("tests/task_graph/packet_first"))
        .setMarkerLabel("Packet First")
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
    const Graphics::GpuPhysicalQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    const Graphics::GpuTaskDiagnosticQueueOverride routes[] = {
        { .task = first, .queue = queues[1u].id },
        { .task = second, .queue = queues[0u].id },
        { .task = transfer, .queue = queues[s_ThirdElementIndex].id },
        { .task = recovery, .queue = queues[0u].id },
    };
    Graphics::GpuTaskGraphCompileOptions options;
    options.queueAssignmentOptions.diagnosticQueueOverrides = routes;
    options.queueAssignmentOptions.diagnosticQueueOverrideCount = LengthOf(routes);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));
    {
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);
        const Graphics::GpuCompiledGraph::ReadView& compiledPlan = reads.compiled;

        ASSERT_TRUE(reads.valid());
        ASSERT_EQ(compiledPlan.taskCount(), 4u);
        ASSERT_EQ(compiledPlan.packetCount(), 4u);
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
        EXPECT_EQ(compiledPlan.queueInfoForTask({}), nullptr);
        const Graphics::GpuSubmissionPacketRange firstTwoPacketRange = compiledPlan.packetRange(
            firstPacket,
            secondPacket
        );
        ASSERT_TRUE(firstTwoPacketRange.valid());
        EXPECT_TRUE(compiledPlan.validPacketRange(firstTwoPacketRange));
        EXPECT_EQ(firstTwoPacketRange.first, firstPacket);
        EXPECT_EQ(firstTwoPacketRange.packetCount, s_ExpectedDualCount);
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

    SingleQueueCompile singleQueueCompile(testArena);
    ASSERT_TRUE(singleQueueCompile.compile(graph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(singleQueueCompile.compiledGraph);


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
    ASSERT_EQ(compiledPlan.packet(fourthPacket).plan->dependencyCount, s_ExpectedDualCount);
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
    const Graphics::GpuPhysicalQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuTaskGraphSchedulingTaskIndexView producers = analysis.schedulingProducers(consumer);
    ASSERT_EQ(producers.taskCount, LengthOf(consumerDependencies));
    EXPECT_EQ(producers[0u], third.index);
    EXPECT_EQ(producers[1u], first.index);
    EXPECT_EQ(producers[s_ThirdElementIndex], second.index);

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
    EXPECT_EQ(dependencies[s_ThirdElementIndex].producer, secondPacket);
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

    const Graphics::GpuTaskCommandRequirements graphicsCommands{ Graphics::GpuQueueCapability::Graphics };
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
        .setScheduling(scheduling)
        .setResourceUses(&producerUse, 1u)
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc, graphicsCommands);
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
        .setScheduling(scheduling)
        .setDependencies(consumerDependencies, LengthOf(consumerDependencies))
        .setResourceUses(&consumerUse, 1u)
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc, graphicsCommands);
    ASSERT_TRUE(consumer.valid());

    SingleQueueCompile singleQueueCompile(testArena);
    ASSERT_TRUE(singleQueueCompile.compile(graph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(singleQueueCompile.compiledGraph);


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
    EXPECT_EQ(compiledPlan.compileStatistics().recordingFrontierCount, s_ExpectedDualCount);
    ASSERT_EQ(compiledPlan.packet(consumerPacket).plan->dependencyCount, s_ExpectedDualCount);
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

