// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_lifecycle_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>


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
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto completionsAdmission0 = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphMergedExternalA)).setMarkerLabel("Merged External A"));
    const auto completionsAdmission1 = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphMergedExternalB)).setMarkerLabel("Merged External B"));
    const auto completionsAdmission2 = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphMergedExternalC)).setMarkerLabel("Merged External C"));
    ASSERT_TRUE(completionsAdmission0);
    ASSERT_TRUE(completionsAdmission1);
    ASSERT_TRUE(completionsAdmission2);
    const Array<Core::GpuExternalCompletionId, 3> completions = {
        *completionsAdmission0,
        *completionsAdmission1,
        *completionsAdmission2,
    };
    for(const Core::GpuExternalCompletionId completion : completions)
        ASSERT_TRUE(completion.valid());

    const Array<Core::GpuExternalCompletionId, 3> firstExternalDependencies = { completions[1u], completions[0u], completions[1u], };
    Core::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.allowPacketMerge = true;
    Core::GpuTaskDesc firstDesc;
    firstDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphMergedExternalFirst)).setMarkerLabel("Merged External First").setScheduling(firstScheduling).setExternalDependencies(firstExternalDependencies);
    const auto firstResult = graph.addTask(firstDesc);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());

    const Array<Core::GpuExternalCompletionId, 4> secondExternalDependencies = { completions[0u], completions[2u], completions[1u], completions[2u], };
    Core::GpuTaskSchedulingHint secondScheduling;
    secondScheduling.allowPacketMerge = true;
    secondScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc secondDesc;
    secondDesc
        .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphMergedExternalSecond))
        .setMarkerLabel("Merged External Second")
        .setScheduling(secondScheduling)
        .setDependencies(Span(&first, 1u))
        .setExternalDependencies(secondExternalDependencies)
    ;
    const auto secondResult = graph.addTask(secondDesc);
    ASSERT_TRUE(secondResult);
    const Core::GpuTaskId second = *secondResult;
    ASSERT_TRUE(second.valid());

    const Core::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const auto& analysisDependencies = analysis.externalDependencies();
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

    const Core::GpuSubmissionPacketId packet = compiledPlan.packetForTask(first);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetForTask(second), packet);
    ASSERT_EQ(compiledPlan.packet(packet).plan->taskCount, 2u);
    ASSERT_EQ(compiledPlan.packet(packet).plan->externalDependencyCount, 3u);
    const Core::GpuExternalCompletionId* const packetDependencies = compiledPlan.packet(packet).externalDependencies;
    ASSERT_NE(packetDependencies, nullptr);
    EXPECT_EQ(packetDependencies[0u], completions[1u]);
    EXPECT_EQ(packetDependencies[1u], completions[0u]);
    EXPECT_EQ(packetDependencies[2u], completions[2u]);
    EXPECT_EQ(compiledPlan.compileStatistics().declaredExternalDependencyCount, analysisDependencies.size());
    EXPECT_EQ(compiledPlan.compileStatistics().packetExternalDependencyCount, 3u);
}

TEST(GpuTaskGraph, CompilesOneTaskPacketsWithDependenciesAndLifecycleBoundaries){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::GpuPhysicalQueueInfo producerQueue = DedicatedComputeQueue();
    const Core::QueueSubmissionToken completionToken{ .value = 41u, .physicalQueueIndex = producerQueue.id.index, .deviceGeneration = producerQueue.id.deviceGeneration, .queue = producerQueue.queueClass, };
    const auto completionResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphPacketExternal)).setMarkerLabel("External Completion").setToken(completionToken));
    ASSERT_TRUE(completionResult);
    const Core::GpuExternalCompletionId completion = *completionResult;
    ASSERT_TRUE(completion.valid());
    Core::GpuTaskDesc firstDesc;
    firstDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphPacketFirst)).setMarkerLabel("Packet First");
    const auto firstResult = graph.addTask<PacketLifecycleTask>(firstDesc, PacketLifecycleTask::Payload{ .requiredCapabilities = Core::GpuQueueCapability::Compute });
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());

    Core::GpuTaskDesc secondDesc;
    secondDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphPacketSecond)).setMarkerLabel("Packet Second").setDependencies(Span(&first, 1u)).setExternalDependencies(Span(&completion, 1u));
    const auto secondResult = graph.addTask<PacketLifecycleTask>(secondDesc, PacketLifecycleTask::Payload{ .requiredCapabilities = Core::GpuQueueCapability::Graphics });
    ASSERT_TRUE(secondResult);
    const Core::GpuTaskId second = *secondResult;
    ASSERT_TRUE(second.valid());

    Core::GpuTaskDesc transferDesc;
    transferDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphPacketTransfer)).setMarkerLabel("Packet Transfer");
    const auto transferResult = graph.addTask<PacketLifecycleTask>(transferDesc, PacketLifecycleTask::Payload{ .requiredCapabilities = Core::GpuQueueCapability::Transfer });
    ASSERT_TRUE(transferResult);
    const Core::GpuTaskId transfer = *transferResult;
    ASSERT_TRUE(transfer.valid());

    const auto recoveryDomainResult = AddHazardDomain(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphPacketRecoveryDomain), "Frame Recovery Timing");
    ASSERT_TRUE(recoveryDomainResult);
    const Core::GpuGraphResourceId recoveryDomain = *recoveryDomainResult;
    ASSERT_TRUE(recoveryDomain.valid());
    const Array<Core::GpuTaskResourceUse, 1> recoveryUses = { Core::GpuTaskResourceUse{ .resource = recoveryDomain, .range = {}, .requiredState = Core::ResourceStates::Common, .access = Core::GpuTaskResourceAccess::ReadWrite, }, };
    Core::GpuTaskSchedulingHint recoveryScheduling;
    recoveryScheduling.forceSubmissionBoundary = true;
    recoveryScheduling.allowPacketMerge = false;
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    recoveryScheduling.isRecoverySubmission = true;
    Core::GpuTaskDesc recoveryDesc;
    recoveryDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphPacketRecovery)).setMarkerLabel("Frame Recovery").setScheduling(recoveryScheduling).setResourceUses(recoveryUses);
    const auto recoveryResult = graph.addTask<PacketLifecycleTask>(recoveryDesc, PacketLifecycleTask::Payload{ .requiredCapabilities = Core::GpuQueueCapability::Graphics });
    ASSERT_TRUE(recoveryResult);
    const Core::GpuTaskId recovery = *recoveryResult;
    ASSERT_TRUE(recovery.valid());

    const Array<Core::GpuPhysicalQueueInfo, 3> queues = { GraphicsQueue(), DedicatedComputeQueue(), DedicatedTransferQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    {
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);
        const Core::GpuCompiledGraph::ReadView& compiledPlan = reads.compiled;

        ASSERT_TRUE(reads.valid());
        ASSERT_EQ(compiledPlan.taskCount(), 4u);
        ASSERT_EQ(compiledPlan.packetCount(), 4u);
        const Core::GpuSubmissionPacketId firstPacket = compiledPlan.packetForTask(first);
        const Core::GpuSubmissionPacketId secondPacket = compiledPlan.packetForTask(second);
        const Core::GpuSubmissionPacketId transferPacket = compiledPlan.packetForTask(transfer);
        const Core::GpuSubmissionPacketId recoveryPacket = compiledPlan.packetForTask(recovery);
        ASSERT_TRUE(firstPacket.valid());
        ASSERT_TRUE(secondPacket.valid());
        ASSERT_TRUE(transferPacket.valid());
        ASSERT_TRUE(recoveryPacket.valid());
        EXPECT_NE(firstPacket, secondPacket);
        EXPECT_NE(recoveryPacket, secondPacket);
        EXPECT_FALSE(compiledPlan.tasksSharePacket(first, second));
        EXPECT_FALSE(compiledPlan.tasksSharePacket(first, {}));
        EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(first, second));
        EXPECT_FALSE(compiledPlan.taskPrecedesOrSharesPacket(second, first));
        EXPECT_FALSE(compiledPlan.taskPrecedesOrSharesPacket(first, {}));
        EXPECT_FALSE(compiledPlan.taskPrecedesInSamePacket(first, second));
        EXPECT_FALSE(compiledPlan.taskPrecedesInSamePacket(first, first));
        EXPECT_FALSE(compiledPlan.tasksFormContiguousPacketSequence({}));
        EXPECT_FALSE(compiledPlan.taskJoinsAcceptedQueueFrontier(first));
        EXPECT_TRUE(compiledPlan.taskJoinsAcceptedQueueFrontier(recovery));
        EXPECT_FALSE(compiledPlan.taskJoinsAcceptedQueueFrontier({}));
        EXPECT_EQ(compiledPlan.queueInfoForTask({}), nullptr);
        const Core::GpuSubmissionPacketRange firstTwoPacketRange = compiledPlan.packetRange(firstPacket, secondPacket);
        ASSERT_TRUE(firstTwoPacketRange.valid());
        EXPECT_TRUE(compiledPlan.validPacketRange(firstTwoPacketRange));
        EXPECT_TRUE(compiledPlan.validPacketRange(compiledPlan.allPacketRange()));
        EXPECT_FALSE(compiledPlan.packetRange(secondPacket, firstPacket).valid());
        EXPECT_FALSE(compiledPlan.packetRangeForTasks(second, first).valid());
        EXPECT_FALSE(compiledPlan.packetRangeForTasks(first, {}).valid());
        EXPECT_FALSE(compiledPlan.validPacketRange(Core::GpuSubmissionPacketRange{ .first = firstPacket, .packetCount = compiledPlan.packetCount() + 1u, }));
        const Core::GpuCompiledPacketView secondPacketView = compiledPlan.packet(secondPacket);
        ASSERT_TRUE(secondPacketView.valid());
        ASSERT_EQ(secondPacketView.plan->taskCount, 1u);
        ASSERT_EQ(secondPacketView.plan->dependencyCount, 1u);
        ASSERT_EQ(secondPacketView.plan->externalDependencyCount, 1u);
        EXPECT_EQ(secondPacketView.dependencies[0u].producer, firstPacket);
        EXPECT_EQ(secondPacketView.externalDependencies[0u], completion);
        const Core::GpuCompiledPacketView recoveryPacketView = compiledPlan.packet(recoveryPacket);
        ASSERT_TRUE(recoveryPacketView.valid());
        EXPECT_EQ(recoveryPacketView.plan->dependencyCount, 0u);
        EXPECT_EQ(recoveryPacketView.plan->externalDependencyCount, 0u);

    }

    compiledGraph.reset();
    const Core::GpuCompiledGraph::ReadView resetPlan(compiledGraph);
    const Core::GpuPhysicalQueueTopology resetQueueTopology = resetPlan.queueTopology();
    EXPECT_EQ(resetQueueTopology.queues, nullptr);
    EXPECT_EQ(resetQueueTopology.queueCount, 0u);
}

TEST(GpuTaskGraph, KeepsExplicitPacketDependenciesOutOfRecordingReadyFrontiers){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto firstResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphRecordingFrontierFirst), "Recording Frontier First", {}, {}, {}, {}, {}, graph);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    const auto secondResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphRecordingFrontierSecond), "Recording Frontier Second", {}, {}, {}, {}, {}, graph);
    ASSERT_TRUE(secondResult);
    const Core::GpuTaskId second = *secondResult;
    const Array<Core::GpuTaskId, 1> thirdDependencies = { first };
    const auto thirdResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphRecordingFrontierThird), "Recording Frontier Third", Span(thirdDependencies), {}, {}, {}, {}, graph);
    ASSERT_TRUE(thirdResult);
    const Core::GpuTaskId third = *thirdResult;
    const Array<Core::GpuTaskId, 2> fourthDependencies = { second, third };
    const auto fourthResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphRecordingFrontierFourth), "Recording Frontier Fourth", Span(fourthDependencies), {}, {}, {}, {}, graph);
    ASSERT_TRUE(fourthResult);
    const Core::GpuTaskId fourth = *fourthResult;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    ASSERT_TRUE(third.valid());
    ASSERT_TRUE(fourth.valid());

    SingleQueueCompile singleQueueCompile(testArena);
    ASSERT_TRUE(singleQueueCompile.compile(graph));
    const Core::GpuCompiledGraph::ReadView compiledPlan(singleQueueCompile.compiledGraph);


    const Core::GpuSubmissionPacketId firstPacket = compiledPlan.packetForTask(first);
    const Core::GpuSubmissionPacketId secondPacket = compiledPlan.packetForTask(second);
    const Core::GpuSubmissionPacketId thirdPacket = compiledPlan.packetForTask(third);
    const Core::GpuSubmissionPacketId fourthPacket = compiledPlan.packetForTask(fourth);
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
    b8 hasSecondProducer = false;
    b8 hasThirdProducer = false;
    for(const Core::GpuPacketDependency& dependency : { compiledPlan.packet(fourthPacket).dependencies[0u], compiledPlan.packet(fourthPacket).dependencies[1u] }){
        hasSecondProducer = hasSecondProducer || dependency.producer == secondPacket;
        hasThirdProducer = hasThirdProducer || dependency.producer == thirdPacket;
    }
    EXPECT_TRUE(hasSecondProducer);
    EXPECT_TRUE(hasThirdProducer);
}

TEST(GpuTaskGraph, PlansSchedulingPacketDependenciesInStableIncomingOrder){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto firstResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphStablePacketDependencyFirst), "Stable Packet Dependency First", {}, {}, {}, {}, {}, graph);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    const auto secondResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphStablePacketDependencySecond), "Stable Packet Dependency Second", {}, {}, {}, {}, {}, graph);
    ASSERT_TRUE(secondResult);
    const Core::GpuTaskId second = *secondResult;
    const auto thirdResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphStablePacketDependencyThird), "Stable Packet Dependency Third", {}, {}, {}, {}, {}, graph);
    ASSERT_TRUE(thirdResult);
    const Core::GpuTaskId third = *thirdResult;
    const Array<Core::GpuTaskId, 3> consumerDependencies = { third, first, second };
    const auto consumerResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphStablePacketDependencyConsumer), "Stable Packet Dependency Consumer", Span(consumerDependencies), {}, {}, {}, {}, graph);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    ASSERT_TRUE(third.valid());
    ASSERT_TRUE(consumer.valid());

    const Core::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Core::GpuTaskGraphSchedulingTaskIndexView producers = analysis.schedulingProducers(consumer);
    ASSERT_EQ(producers.taskCount, LengthOf(consumerDependencies));
    EXPECT_EQ(producers[0u], third.index);
    EXPECT_EQ(producers[1u], first.index);
    EXPECT_EQ(producers[2u], second.index);

    const Core::GpuSubmissionPacketId firstPacket = compiledPlan.packetForTask(first);
    const Core::GpuSubmissionPacketId secondPacket = compiledPlan.packetForTask(second);
    const Core::GpuSubmissionPacketId thirdPacket = compiledPlan.packetForTask(third);
    const Core::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(consumer);
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_TRUE(thirdPacket.valid());
    ASSERT_TRUE(consumerPacket.valid());
    ASSERT_EQ(compiledPlan.packet(consumerPacket).plan->dependencyCount, LengthOf(consumerDependencies));
    const Core::GpuPacketDependency* const dependencies = compiledPlan.packet(consumerPacket).dependencies;
    ASSERT_NE(dependencies, nullptr);
    EXPECT_EQ(dependencies[0u].producer, thirdPacket);
    EXPECT_EQ(dependencies[1u].producer, firstPacket);
    EXPECT_EQ(dependencies[2u].producer, secondPacket);
}

TEST(GpuTaskGraph, DerivesRecordingReadyFrontiersFromStateSeedProducers){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto bufferResult = AddBufferMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphRecordingFrontierStateSeedBuffer), "Recording Frontier State Seed Buffer");
    ASSERT_TRUE(bufferResult);
    const Core::GpuGraphResourceId buffer = *bufferResult;
    ASSERT_TRUE(buffer.valid());
    const auto orderingProducerResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphRecordingFrontierOrderingProducer), "Recording Frontier Ordering Producer", {}, {}, {}, {}, {}, graph);
    ASSERT_TRUE(orderingProducerResult);
    const Core::GpuTaskId orderingProducer = *orderingProducerResult;
    ASSERT_TRUE(orderingProducer.valid());

    const Core::GpuTaskCommandRequirements graphicsCommands{ Core::GpuQueueCapability::Graphics };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.allowParallelRecording = true;

    const Core::GpuTaskResourceUse producerUse{ .resource = buffer, .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write, };
    Core::GpuTaskDesc producerDesc;
    producerDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphRecordingFrontierStateSeedProducer)).setMarkerLabel("Recording Frontier State Seed Producer").setScheduling(scheduling).setResourceUses(Span(&producerUse, 1u));
    const auto producerResult = graph.addTask(producerDesc, graphicsCommands);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;
    ASSERT_TRUE(producer.valid());

    const Array<Core::GpuTaskId, 2> consumerDependencies = { producer, orderingProducer };
    const Core::GpuTaskResourceUse consumerUse{ .resource = buffer, .range = {}, .requiredState = Core::ResourceStates::CopySource, .access = Core::GpuTaskResourceAccess::Read, };
    Core::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphRecordingFrontierStateSeedConsumer))
        .setMarkerLabel("Recording Frontier State Seed Consumer")
        .setScheduling(scheduling)
        .setDependencies(consumerDependencies)
        .setResourceUses(Span(&consumerUse, 1u))
    ;
    const auto consumerResult = graph.addTask(consumerDesc, graphicsCommands);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());

    SingleQueueCompile singleQueueCompile(testArena);
    ASSERT_TRUE(singleQueueCompile.compile(graph));
    const Core::GpuCompiledGraph::ReadView compiledPlan(singleQueueCompile.compiledGraph);


    const Core::GpuSubmissionPacketId orderingProducerPacket = compiledPlan.packetForTask(orderingProducer);
    const Core::GpuSubmissionPacketId producerPacket = compiledPlan.packetForTask(producer);
    const Core::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(consumer);
    const Core::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumer).plan;
    ASSERT_TRUE(orderingProducerPacket.valid());
    ASSERT_TRUE(producerPacket.valid());
    ASSERT_TRUE(consumerPacket.valid());
    ASSERT_NE(producerPacket, consumerPacket);
    ASSERT_NE(compiledConsumer, nullptr);
    EXPECT_EQ(compiledPlan.packet(producerPacket).plan->recordingFrontier, 0u);
    EXPECT_EQ(compiledPlan.packet(consumerPacket).plan->recordingFrontier, 1u);
    EXPECT_EQ(compiledPlan.compileStatistics().recordingFrontierCount, 2u);
    ASSERT_EQ(compiledPlan.packet(consumerPacket).plan->dependencyCount, 2u);
    const Core::GpuPacketDependency* const dependencies = compiledPlan.packet(consumerPacket).dependencies;
    ASSERT_NE(dependencies, nullptr);
    // Deduplicating a state-seed dependency preserves the original scheduling order.
    EXPECT_EQ(dependencies[0u].producer, producerPacket);
    EXPECT_EQ(dependencies[1u].producer, orderingProducerPacket);
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);

    const Core::GpuPacketStateSeed* const stateSeeds = compiledPlan.findTask(consumer).prologueStateSeeds;
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

