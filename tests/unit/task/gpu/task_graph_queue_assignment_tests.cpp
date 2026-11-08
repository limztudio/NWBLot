// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_queue_assignment_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, AutomaticallyUsesTheOnlyQueueSupportingAllCommandKinds){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::GpuTaskCommandRequirements mixedCommands{ QueueCapabilities(Core::GpuQueueCapability::Graphics, Core::GpuQueueCapability::Compute) };
    const auto taskResult = AddTaskWithCommands(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphMixedCommands), "Mixed Commands", mixedCommands, {}, {}, {}, graph);
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());
    const Array<Core::GpuPhysicalQueueInfo, 3u> queues = { DedicatedTransferQueue(), DedicatedComputeQueue(), GraphicsQueue() };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues) };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology));
    ASSERT_NE(assignments.find(task), nullptr);
    EXPECT_EQ(assignments.find(task)->queue, queues[2u].id);
    const Array<Core::GpuPhysicalQueueInfo, 2u> incompatibleQueues = { DedicatedComputeQueue(), DedicatedTransferQueue() };
    const Core::GpuPhysicalQueueTopology incompatibleTopology{ .queues = incompatibleQueues.data(), .queueCount = LengthOf(incompatibleQueues) };
    EXPECT_FALSE(Assign(assignments, operationScratchArena, graph, analysis, incompatibleTopology));
    EXPECT_EQ(assignments.diagnostic().status, Core::GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue);
    EXPECT_EQ(assignments.diagnostic().task, task);
}


TEST(GpuTaskGraph, KeepsTaskOwnedPrimaryGraphicsRestrictionDuringAutomaticPlacement){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.allowSameClassQueueRouting = true;
    scheduling.preferNonPrimarySameClassQueue = true;
    const Core::GpuTaskCommandRequirements commands{ .requiredCapabilities = Core::GpuQueueCapability::Compute, .requiresPrimaryGraphicsQueue = true };
    const auto taskResult = AddTaskWithCommands(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphPrimaryGraphicsContract), "Primary Graphics Contract", commands, scheduling, {}, {}, graph);
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());
    Core::GpuPhysicalQueueInfo auxiliary = GraphicsQueue(3u);
    auxiliary.queueIndex = 1u;
    const Array<Core::GpuPhysicalQueueInfo, 3u> queues = { auxiliary, DedicatedComputeQueue(), GraphicsQueue() };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues) };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology));
    ASSERT_NE(assignments.find(task), nullptr);
    EXPECT_EQ(assignments.find(task)->queue, GraphicsQueue().id);
}

TEST(GpuTaskGraph, ConditionalRecoveryDoesNotCreateOverlapForSerialNormalWork){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Core::GpuQueueCapability::Graphics;
    Core::GpuTaskSchedulingHint graphicsScheduling;
    graphicsScheduling.cost = Core::GpuTaskCostHint::Large;
    const auto prefixResult = AddTaskWithCommands(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphRecoveryOverlapPrefix), "Recovery Overlap Prefix", graphicsCommands, graphicsScheduling, {}, {}, graph);
    ASSERT_TRUE(prefixResult);
    const Core::GpuTaskId prefix = *prefixResult;
    ASSERT_TRUE(prefix.valid());
    const auto middleResult = AddTaskWithCommands(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphRecoveryOverlapMiddle), "Recovery Overlap Middle", ComputeCommands(), {}, {}, Span(&prefix, 1u), graph);
    ASSERT_TRUE(middleResult);
    const Core::GpuTaskId middle = *middleResult;
    ASSERT_TRUE(middle.valid());
    const auto suffixResult = AddTaskWithCommands(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphRecoveryOverlapSuffix), "Recovery Overlap Suffix", graphicsCommands, graphicsScheduling, {}, Span(&middle, 1u), graph);
    ASSERT_TRUE(suffixResult);
    const Core::GpuTaskId suffix = *suffixResult;
    ASSERT_TRUE(suffix.valid());

    const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), DedicatedComputeQueue() };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues) };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments normalAssignments(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    ASSERT_TRUE(Assign(normalAssignments, operationScratchArena, graph, analysis, topology));
    const Core::GpuTaskQueueAssignment* const normalMiddle = normalAssignments.find(middle);
    ASSERT_NE(normalMiddle, nullptr);
    ASSERT_EQ(normalMiddle->queue, queues[0u].id);

    Core::GpuTaskSchedulingHint recoveryScheduling;
    recoveryScheduling.cost = Core::GpuTaskCostHint::Tiny;
    recoveryScheduling.forceSubmissionBoundary = true;
    recoveryScheduling.allowPacketMerge = false;
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    recoveryScheduling.isRecoverySubmission = true;
    const auto recoveryResult = AddTaskWithCommands(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphRecoveryOverlapFallback), "Recovery Overlap Fallback", graphicsCommands, recoveryScheduling, {}, {}, graph);
    ASSERT_TRUE(recoveryResult);
    const Core::GpuTaskId recovery = *recoveryResult;
    ASSERT_TRUE(recovery.valid());
    Core::GpuTaskGraphQueueAssignments recoveryAssignments(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    ASSERT_TRUE(Assign(recoveryAssignments, operationScratchArena, graph, analysis, topology));
    const Array<Core::GpuTaskId, 3u> normalTasks = { prefix, middle, suffix };
    for(const Core::GpuTaskId task : normalTasks){
        const Core::GpuTaskQueueAssignment* const before = normalAssignments.find(task);
        const Core::GpuTaskQueueAssignment* const after = recoveryAssignments.find(task);
        ASSERT_NE(before, nullptr);
        ASSERT_NE(after, nullptr);
        EXPECT_EQ(after->queue, before->queue);
        EXPECT_EQ(after->score.overlap, 0);
    }
    const Core::GpuTaskQueueAssignment* const recoveryAssignment = recoveryAssignments.find(recovery);
    ASSERT_NE(recoveryAssignment, nullptr);
    EXPECT_EQ(recoveryAssignment->queue, queues[0u].id);
}


TEST(GpuTaskGraph, ChoosesAutomaticPlacementDeterministicallyAcrossTopologyOrder){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);

    Core::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Core::GpuQueueCapability::Graphics;
    Core::GpuTaskSchedulingHint graphicsScheduling;
    graphicsScheduling.cost = Core::GpuTaskCostHint::Large;
    const auto graphicsTaskResult = AddTaskWithCommands(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphDeterministicGraphics), "Deterministic Graphics", graphicsCommands, graphicsScheduling, {}, {}, graph);
    ASSERT_TRUE(graphicsTaskResult);
    const Core::GpuTaskId graphicsTask = *graphicsTaskResult;

    Core::GpuTaskCommandRequirements transferCommands;
    transferCommands.requiredCapabilities = Core::GpuQueueCapability::Transfer;
    const auto transferTaskResult = AddTaskWithCommands(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphDeterministicTransfer), "Deterministic Transfer", transferCommands, {}, {}, {}, graph);
    ASSERT_TRUE(transferTaskResult);
    const Core::GpuTaskId transferTask = *transferTaskResult;
    ASSERT_TRUE(graphicsTask.valid());
    ASSERT_TRUE(transferTask.valid());

    Core::GpuPhysicalQueueInfo auxiliaryGraphics = GraphicsQueue(3u);
    auxiliaryGraphics.queueIndex = 1u;
    const Array<Core::GpuPhysicalQueueInfo, 4u> firstQueues = { auxiliaryGraphics, DedicatedTransferQueue(), DedicatedComputeQueue(), GraphicsQueue(), };
    const Array<Core::GpuPhysicalQueueInfo, 4u> secondQueues = { GraphicsQueue(), DedicatedComputeQueue(), auxiliaryGraphics, DedicatedTransferQueue(), };
    const Core::GpuPhysicalQueueTopology firstTopology{ .queues = firstQueues.data(), .queueCount = LengthOf(firstQueues), };
    const Core::GpuPhysicalQueueTopology secondTopology{ .queues = secondQueues.data(), .queueCount = LengthOf(secondQueues), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    Core::GpuTaskGraphQueueAssignments firstAssignments(testArena.arena);
    Core::GpuTaskGraphQueueAssignments secondAssignments(testArena.arena);
    ASSERT_TRUE(Assign(firstAssignments, operationScratchArena, graph, analysis, firstTopology));
    ASSERT_TRUE(Assign(secondAssignments, operationScratchArena, graph, analysis, secondTopology));

    const Core::GpuTaskQueueAssignment* const firstAssignment = firstAssignments.find(transferTask);
    const Core::GpuTaskQueueAssignment* const secondAssignment = secondAssignments.find(transferTask);
    ASSERT_NE(firstAssignment, nullptr);
    ASSERT_NE(secondAssignment, nullptr);
    EXPECT_EQ(firstAssignment->queue, DedicatedComputeQueue().id);
    EXPECT_EQ(secondAssignment->queue, firstAssignment->queue);
    EXPECT_NE(firstAssignment->queue, auxiliaryGraphics.id);
}

TEST(GpuTaskGraph, QueueScoreUsesOnlyReducedOutgoingDependencies){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuTaskCommandRequirements computeCommands;
    computeCommands.requiredCapabilities = Core::GpuQueueCapability::Compute;
    Core::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Core::GpuQueueCapability::Graphics;

    const auto producerResult = AddTaskWithCommands(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphReducedQueueScoreProducer), "Reduced Queue Score Producer", computeCommands, {}, {}, {}, graph);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;
    const auto middleResult = AddTaskWithCommands(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphReducedQueueScoreMiddle), "Reduced Queue Score Middle", graphicsCommands, {}, {}, Span(&producer, 1u), graph);
    ASSERT_TRUE(middleResult);
    const Core::GpuTaskId middle = *middleResult;
    const Array<Core::GpuTaskId, 2u> finalDependencies = { producer, middle };
    const auto finalTaskResult = AddTaskWithCommands(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphReducedQueueScoreFinal), "Reduced Queue Score Final", graphicsCommands, {}, {}, Span(finalDependencies), graph);
    ASSERT_TRUE(finalTaskResult);
    const Core::GpuTaskId finalTask = *finalTaskResult;
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(middle.valid());
    ASSERT_TRUE(finalTask.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    ASSERT_EQ(analysis.edges().size(), 3u);
    ASSERT_EQ(analysis.schedulingEdges().size(), 2u);
    // The graphics queue has no compute support, so the compute producer must run on the dedicated queue.
    const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(0u, QueueCapabilities(Core::GpuQueueCapability::Graphics, Core::GpuQueueCapability::Transfer)), DedicatedComputeQueue() };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology));
    const Core::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
    ASSERT_NE(producerAssignment, nullptr);
    EXPECT_EQ(producerAssignment->queueClass, Core::CommandQueue::Compute);
    EXPECT_EQ(producerAssignment->score.outgoingCrossings, 1);
}

TEST(GpuTaskGraph, DeduplicatesRawOwnershipScoreAndIgnoresSameFamilyQueueCrossings){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto textureResult = AddTextureMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphOwnershipScoreTexture), "Ownership Score Texture");
    ASSERT_TRUE(textureResult);
    const Core::GpuGraphResourceId texture = *textureResult;
    ASSERT_TRUE(texture.valid());
    const auto secondTextureResult = AddTextureMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphOwnershipScoreSecondTexture), "Ownership Score Second Texture");
    ASSERT_TRUE(secondTextureResult);
    const Core::GpuGraphResourceId secondTexture = *secondTextureResult;
    ASSERT_TRUE(secondTexture.valid());

    const Core::GpuTaskResourceUse producerUse{ .resource = texture, .range = {}, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::Write, };
    const Core::GpuTaskResourceUse consumerUse{ .resource = texture, .range = {}, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::ReadWrite, };
    Array<Core::GpuTaskResourceUse, 2u> producerUses = { producerUse, producerUse };
    producerUses[1u].resource = secondTexture;
    Array<Core::GpuTaskResourceUse, 2u> consumerUses = { consumerUse, consumerUse };
    consumerUses[1u].resource = secondTexture;
    Array<Core::GpuTaskResourceUse, 2u> finalUses = { consumerUses[0u], consumerUses[1u] };
    for(Core::GpuTaskResourceUse& use : finalUses)
        use.access = Core::GpuTaskResourceAccess::Read;
    const Core::GpuTaskCommandRequirements graphicsCommands = GraphicsCommands();
    const Core::GpuTaskCommandRequirements computeCommands = ComputeCommands();
    Core::GpuTaskDesc producerDesc;
    producerDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphOwnershipScoreProducer)).setMarkerLabel("Ownership Score Producer").setResourceUses(producerUses);
    const auto producerResult = graph.addTask(producerDesc, graphicsCommands);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;
    Core::GpuTaskDesc consumerDesc;
    consumerDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphOwnershipScoreConsumer)).setMarkerLabel("Ownership Score Consumer").setResourceUses(consumerUses);
    const auto consumerResult = graph.addTask(consumerDesc, computeCommands);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());
    Core::GpuTaskDesc finalDesc;
    finalDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphOwnershipScoreFinalConsumer)).setMarkerLabel("Ownership Score Final Consumer").setResourceUses(finalUses);
    const auto finalConsumerResult = graph.addTask(finalDesc, graphicsCommands);
    ASSERT_TRUE(finalConsumerResult);
    const Core::GpuTaskId finalConsumer = *finalConsumerResult;
    ASSERT_TRUE(finalConsumer.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    ASSERT_EQ(analysis.inferredEdges().size(), 6u);
    ASSERT_EQ(analysis.schedulingEdges().size(), 2u);

    // The graphics queue has no compute support, so the compute consumer must run on the dedicated queue.
    const Core::GpuPhysicalQueueInfo graphicsQueue = GraphicsQueue(0u, QueueCapabilities(Core::GpuQueueCapability::Graphics, Core::GpuQueueCapability::Transfer));
    const Array<Core::GpuPhysicalQueueInfo, 2u> separateFamilyQueues = { graphicsQueue, DedicatedComputeQueue(), };
    const Core::GpuPhysicalQueueTopology separateFamilyTopology{ .queues = separateFamilyQueues.data(), .queueCount = LengthOf(separateFamilyQueues), };
    Core::GpuTaskGraphQueueAssignments separateFamilyAssignments(testArena.arena);
    ASSERT_TRUE(Assign(separateFamilyAssignments, operationScratchArena, graph, analysis, separateFamilyTopology));
    const Core::GpuTaskQueueAssignment* const separateFamilyAssignment = separateFamilyAssignments.find(consumer);
    ASSERT_NE(separateFamilyAssignment, nullptr);
    EXPECT_EQ(separateFamilyAssignment->score.incomingCrossings, 1);
    EXPECT_EQ(separateFamilyAssignment->score.outgoingCrossings, 1);
    EXPECT_EQ(separateFamilyAssignment->score.ownershipTransfers, 4);
    const Core::GpuTaskQueueAssignment* const producerAssignment = separateFamilyAssignments.find(producer);
    const Core::GpuTaskQueueAssignment* const finalAssignment = separateFamilyAssignments.find(finalConsumer);
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(finalAssignment, nullptr);
    EXPECT_EQ(producerAssignment->score.ownershipTransfers, 2);
    EXPECT_EQ(finalAssignment->score.ownershipTransfers, 2);

    Core::GpuPhysicalQueueInfo sameFamilyCompute = DedicatedComputeQueue();
    sameFamilyCompute.familyIndex = graphicsQueue.familyIndex;
    sameFamilyCompute.queueIndex = 1u;
    const Array<Core::GpuPhysicalQueueInfo, 2u> sameFamilyQueues = { graphicsQueue, sameFamilyCompute, };
    const Core::GpuPhysicalQueueTopology sameFamilyTopology{ .queues = sameFamilyQueues.data(), .queueCount = LengthOf(sameFamilyQueues), };
    Core::GpuTaskGraphQueueAssignments sameFamilyAssignments(testArena.arena);
    ASSERT_TRUE(Assign(sameFamilyAssignments, operationScratchArena, graph, analysis, sameFamilyTopology));
    const Core::GpuTaskQueueAssignment* const sameFamilyAssignment = sameFamilyAssignments.find(consumer);
    ASSERT_NE(sameFamilyAssignment, nullptr);
    EXPECT_EQ(sameFamilyAssignment->score.incomingCrossings, 1);
    EXPECT_EQ(sameFamilyAssignment->score.ownershipTransfers, 0);
    const Core::GpuTaskQueueAssignment* const sameFamilyProducer = sameFamilyAssignments.find(producer);
    const Core::GpuTaskQueueAssignment* const sameFamilyFinal = sameFamilyAssignments.find(finalConsumer);
    ASSERT_NE(sameFamilyProducer, nullptr);
    ASSERT_NE(sameFamilyFinal, nullptr);
    EXPECT_EQ(sameFamilyProducer->score.ownershipTransfers, 0);
    EXPECT_EQ(sameFamilyFinal->score.ownershipTransfers, 0);
}

TEST(GpuTaskGraph, RejectsInvalidAndIncompatibleQueueTopologiesDeterministically){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuTaskCommandRequirements computeCommands;
    computeCommands.requiredCapabilities = Core::GpuQueueCapability::Compute;
    const auto taskResult = AddTaskWithCommands(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphQueueDiagnostic), "Queue Diagnostic", computeCommands, {}, {}, {}, graph);
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    EXPECT_FALSE(Assign(assignments, operationScratchArena, graph, analysis, Core::GpuPhysicalQueueTopology{}));
    EXPECT_EQ(assignments.diagnostic().status, Core::GpuTaskGraphQueueAssignmentStatus::InvalidQueueTopology);

    const Array<Core::GpuPhysicalQueueInfo, 1u> graphicsOnly = { GraphicsQueue(0u, Core::GpuQueueCapability::Graphics), };
    const Core::GpuPhysicalQueueTopology graphicsOnlyTopology{ .queues = graphicsOnly.data(), .queueCount = LengthOf(graphicsOnly), };
    EXPECT_FALSE(Assign(assignments, operationScratchArena, graph, analysis, graphicsOnlyTopology));
    EXPECT_EQ(assignments.diagnostic().status, Core::GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue);
    EXPECT_EQ(assignments.diagnostic().task, task);
    EXPECT_EQ(assignments.diagnostic().requiredCapabilities, Core::GpuQueueCapability::Compute);

    Core::GpuPhysicalQueueInfo invalidTransferQueue = DedicatedTransferQueue();
    invalidTransferQueue.capabilities = Core::GpuQueueCapability::Compute;
    const Core::GpuPhysicalQueueTopology invalidTransferTopology{ .queues = &invalidTransferQueue, .queueCount = 1u, };
    EXPECT_FALSE(Assign(assignments, operationScratchArena, graph, analysis, invalidTransferTopology));
    EXPECT_EQ(assignments.diagnostic().status, Core::GpuTaskGraphQueueAssignmentStatus::InvalidQueueTopology);

    Core::GpuPhysicalQueueInfo duplicateNativeQueue = GraphicsQueue(3u);
    // Different graph IDs must not alias the same physical queue family/index transport.
    duplicateNativeQueue.queueIndex = GraphicsQueue().queueIndex;
    const Array<Core::GpuPhysicalQueueInfo, 2u> duplicateNativeQueues = { GraphicsQueue(), duplicateNativeQueue, };
    const Core::GpuPhysicalQueueTopology duplicateNativeTopology{ .queues = duplicateNativeQueues.data(), .queueCount = LengthOf(duplicateNativeQueues), };
    EXPECT_FALSE(Assign(assignments, operationScratchArena, graph, analysis, duplicateNativeTopology));
    EXPECT_EQ(assignments.diagnostic().status, Core::GpuTaskGraphQueueAssignmentStatus::InvalidQueueTopology);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

