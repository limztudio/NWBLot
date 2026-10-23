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


namespace __hidden_task_graph_queue_assignment_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, AutomaticallyUsesTheOnlyQueueSupportingAllCommandKinds){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskCommandRequirements mixedCommands{ QueueCapabilities(Graphics::GpuQueueCapability::Graphics, Graphics::GpuQueueCapability::Compute) };
    const auto taskResult = AddTaskWithCommands(graph, Name("tests/task_graph/mixed_commands"), "Mixed Commands", mixedCommands);
    ASSERT_TRUE(taskResult);
    const Graphics::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());
    const Graphics::GpuPhysicalQueueInfo queues[] = { DedicatedTransferQueue(), DedicatedComputeQueue(), GraphicsQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology));
    ASSERT_NE(assignments.find(task), nullptr);
    EXPECT_EQ(assignments.find(task)->queue, queues[s_ThirdElementIndex].id);
    const Graphics::GpuPhysicalQueueInfo incompatibleQueues[] = { DedicatedComputeQueue(), DedicatedTransferQueue() };
    const Graphics::GpuPhysicalQueueTopology incompatibleTopology{ .queues = incompatibleQueues, .queueCount = LengthOf(incompatibleQueues) };
    EXPECT_FALSE(Assign(assignments, operationScratchArena, graph, analysis, incompatibleTopology));
    EXPECT_EQ(assignments.diagnostic().status, Graphics::GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue);
    EXPECT_EQ(assignments.diagnostic().task, task);
}


TEST(GpuTaskGraph, KeepsTaskOwnedPrimaryGraphicsRestrictionDuringAutomaticPlacement){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.allowSameClassQueueRouting = true;
    scheduling.preferNonPrimarySameClassQueue = true;
    const Graphics::GpuTaskCommandRequirements commands{ .requiredCapabilities = Graphics::GpuQueueCapability::Compute, .requiresPrimaryGraphicsQueue = true };
    const auto taskResult = AddTaskWithCommands(graph, Name("tests/task_graph/primary_graphics_contract"), "Primary Graphics Contract", commands, scheduling);
    ASSERT_TRUE(taskResult);
    const Graphics::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());
    Graphics::GpuPhysicalQueueInfo auxiliary = GraphicsQueue(3u);
    auxiliary.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo queues[] = { auxiliary, DedicatedComputeQueue(), GraphicsQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology));
    ASSERT_NE(assignments.find(task), nullptr);
    EXPECT_EQ(assignments.find(task)->queue, GraphicsQueue().id);
}

TEST(GpuTaskGraph, ConditionalRecoveryDoesNotCreateOverlapForSerialNormalWork){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    Graphics::GpuTaskSchedulingHint graphicsScheduling;
    graphicsScheduling.cost = Graphics::GpuTaskCostHint::Large;
    const auto prefixResult = AddTaskWithCommands(graph, Name("tests/task_graph/recovery_overlap_prefix"), "Recovery Overlap Prefix", graphicsCommands, graphicsScheduling);
    ASSERT_TRUE(prefixResult);
    const Graphics::GpuTaskId prefix = *prefixResult;
    ASSERT_TRUE(prefix.valid());
    const auto middleResult = AddTaskWithCommands(graph, Name("tests/task_graph/recovery_overlap_middle"), "Recovery Overlap Middle", ComputeCommands(), {}, {}, &prefix, 1u);
    ASSERT_TRUE(middleResult);
    const Graphics::GpuTaskId middle = *middleResult;
    ASSERT_TRUE(middle.valid());
    const auto suffixResult = AddTaskWithCommands(graph, Name("tests/task_graph/recovery_overlap_suffix"), "Recovery Overlap Suffix", graphicsCommands, graphicsScheduling, {}, &middle, 1u);
    ASSERT_TRUE(suffixResult);
    const Graphics::GpuTaskId suffix = *suffixResult;
    ASSERT_TRUE(suffix.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments normalAssignments(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
    ASSERT_TRUE(Assign(normalAssignments, operationScratchArena, graph, analysis, topology));
    const Graphics::GpuTaskQueueAssignment* const normalMiddle = normalAssignments.find(middle);
    ASSERT_NE(normalMiddle, nullptr);
    ASSERT_EQ(normalMiddle->queue, queues[0u].id);

    Graphics::GpuTaskSchedulingHint recoveryScheduling;
    recoveryScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    recoveryScheduling.forceSubmissionBoundary = true;
    recoveryScheduling.allowPacketMerge = false;
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    recoveryScheduling.isRecoverySubmission = true;
    const auto recoveryResult = AddTaskWithCommands(graph, Name("tests/task_graph/recovery_overlap_fallback"), "Recovery Overlap Fallback", graphicsCommands, recoveryScheduling);
    ASSERT_TRUE(recoveryResult);
    const Graphics::GpuTaskId recovery = *recoveryResult;
    ASSERT_TRUE(recovery.valid());
    Graphics::GpuTaskGraphQueueAssignments recoveryAssignments(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
    ASSERT_TRUE(Assign(recoveryAssignments, operationScratchArena, graph, analysis, topology));
    const Graphics::GpuTaskId normalTasks[] = { prefix, middle, suffix };
    for(const Graphics::GpuTaskId task : normalTasks){
        const Graphics::GpuTaskQueueAssignment* const before = normalAssignments.find(task);
        const Graphics::GpuTaskQueueAssignment* const after = recoveryAssignments.find(task);
        ASSERT_NE(before, nullptr);
        ASSERT_NE(after, nullptr);
        EXPECT_EQ(after->queue, before->queue);
        EXPECT_EQ(after->score.overlap, 0);
    }
    const Graphics::GpuTaskQueueAssignment* const recoveryAssignment = recoveryAssignments.find(recovery);
    ASSERT_NE(recoveryAssignment, nullptr);
    EXPECT_EQ(recoveryAssignment->queue, queues[0u].id);
}


TEST(GpuTaskGraph, ChoosesAutomaticPlacementDeterministicallyAcrossTopologyOrder){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    Graphics::GpuTaskSchedulingHint graphicsScheduling;
    graphicsScheduling.cost = Graphics::GpuTaskCostHint::Large;
    const auto graphicsTaskResult = AddTaskWithCommands(graph, Name("tests/task_graph/deterministic_graphics"), "Deterministic Graphics", graphicsCommands, graphicsScheduling);
    ASSERT_TRUE(graphicsTaskResult);
    const Graphics::GpuTaskId graphicsTask = *graphicsTaskResult;

    Graphics::GpuTaskCommandRequirements transferCommands;
    transferCommands.requiredCapabilities = Graphics::GpuQueueCapability::Transfer;
    const auto transferTaskResult = AddTaskWithCommands(graph, Name("tests/task_graph/deterministic_transfer"), "Deterministic Transfer", transferCommands);
    ASSERT_TRUE(transferTaskResult);
    const Graphics::GpuTaskId transferTask = *transferTaskResult;
    ASSERT_TRUE(graphicsTask.valid());
    ASSERT_TRUE(transferTask.valid());

    Graphics::GpuPhysicalQueueInfo auxiliaryGraphics = GraphicsQueue(3u);
    auxiliaryGraphics.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo firstQueues[] = { auxiliaryGraphics, DedicatedTransferQueue(), DedicatedComputeQueue(), GraphicsQueue(), };
    const Graphics::GpuPhysicalQueueInfo secondQueues[] = { GraphicsQueue(), DedicatedComputeQueue(), auxiliaryGraphics, DedicatedTransferQueue(), };
    const Graphics::GpuPhysicalQueueTopology firstTopology{ .queues = firstQueues, .queueCount = LengthOf(firstQueues), };
    const Graphics::GpuPhysicalQueueTopology secondTopology{ .queues = secondQueues, .queueCount = LengthOf(secondQueues), };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
    Graphics::GpuTaskGraphQueueAssignments firstAssignments(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments secondAssignments(testArena.arena);
    ASSERT_TRUE(Assign(firstAssignments, operationScratchArena, graph, analysis, firstTopology));
    ASSERT_TRUE(Assign(secondAssignments, operationScratchArena, graph, analysis, secondTopology));

    const Graphics::GpuTaskQueueAssignment* const firstAssignment = firstAssignments.find(transferTask);
    const Graphics::GpuTaskQueueAssignment* const secondAssignment = secondAssignments.find(transferTask);
    ASSERT_NE(firstAssignment, nullptr);
    ASSERT_NE(secondAssignment, nullptr);
    EXPECT_EQ(firstAssignment->queue, DedicatedComputeQueue().id);
    EXPECT_EQ(secondAssignment->queue, firstAssignment->queue);
    EXPECT_NE(firstAssignment->queue, auxiliaryGraphics.id);
}

TEST(GpuTaskGraph, QueueScoreUsesOnlyReducedOutgoingDependencies){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskCommandRequirements computeCommands;
    computeCommands.requiredCapabilities = Graphics::GpuQueueCapability::Compute;
    Graphics::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;

    const auto producerResult = AddTaskWithCommands(graph, Name("tests/task_graph/reduced_queue_score_producer"), "Reduced Queue Score Producer", computeCommands);
    ASSERT_TRUE(producerResult);
    const Graphics::GpuTaskId producer = *producerResult;
    const auto middleResult = AddTaskWithCommands(graph, Name("tests/task_graph/reduced_queue_score_middle"), "Reduced Queue Score Middle", graphicsCommands, {}, {}, &producer, 1u);
    ASSERT_TRUE(middleResult);
    const Graphics::GpuTaskId middle = *middleResult;
    const Graphics::GpuTaskId finalDependencies[] = { producer, middle };
    const auto finalTaskResult = AddTaskWithCommands(graph, Name("tests/task_graph/reduced_queue_score_final"), "Reduced Queue Score Final", graphicsCommands, {}, {}, finalDependencies, LengthOf(finalDependencies));
    ASSERT_TRUE(finalTaskResult);
    const Graphics::GpuTaskId finalTask = *finalTaskResult;
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(middle.valid());
    ASSERT_TRUE(finalTask.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
    ASSERT_EQ(analysis.edges().size(), 3u);
    ASSERT_EQ(analysis.schedulingEdges().size(), s_ExpectedDualCount);
    // The graphics queue has no compute support, so the compute producer must run on the dedicated queue.
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(0u, QueueCapabilities(Graphics::GpuQueueCapability::Graphics, Graphics::GpuQueueCapability::Transfer)), DedicatedComputeQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues), };
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology));
    const Graphics::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
    ASSERT_NE(producerAssignment, nullptr);
    EXPECT_EQ(producerAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(producerAssignment->score.outgoingCrossings, 1);
}

TEST(GpuTaskGraph, DeduplicatesRawOwnershipScoreAndIgnoresSameFamilyQueueCrossings){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto textureResult = AddTextureMetadata(graph, Name("tests/task_graph/ownership_score_texture"), "Ownership Score Texture");
    ASSERT_TRUE(textureResult);
    const Graphics::GpuGraphResourceId texture = *textureResult;
    ASSERT_TRUE(texture.valid());
    const auto secondTextureResult = AddTextureMetadata(graph, Name("tests/task_graph/ownership_score_second_texture"), "Ownership Score Second Texture");
    ASSERT_TRUE(secondTextureResult);
    const Graphics::GpuGraphResourceId secondTexture = *secondTextureResult;
    ASSERT_TRUE(secondTexture.valid());

    const Graphics::GpuTaskResourceUse producerUse{ .resource = texture, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write, };
    const Graphics::GpuTaskResourceUse consumerUse{ .resource = texture, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite, };
    Graphics::GpuTaskResourceUse producerUses[] = { producerUse, producerUse };
    producerUses[1u].resource = secondTexture;
    Graphics::GpuTaskResourceUse consumerUses[] = { consumerUse, consumerUse };
    consumerUses[1u].resource = secondTexture;
    Graphics::GpuTaskResourceUse finalUses[] = { consumerUses[0u], consumerUses[1u] };
    for(Graphics::GpuTaskResourceUse& use : finalUses)
        use.access = Graphics::GpuTaskResourceAccess::Read;
    const Graphics::GpuTaskCommandRequirements graphicsCommands = GraphicsCommands();
    const Graphics::GpuTaskCommandRequirements computeCommands = ComputeCommands();
    Graphics::GpuTaskDesc producerDesc;
    producerDesc.setIdentity(Name("tests/task_graph/ownership_score_producer")).setMarkerLabel("Ownership Score Producer").setResourceUses(producerUses, LengthOf(producerUses));
    const auto producerResult = graph.addTask(producerDesc, graphicsCommands);
    ASSERT_TRUE(producerResult);
    const Graphics::GpuTaskId producer = *producerResult;
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc.setIdentity(Name("tests/task_graph/ownership_score_consumer")).setMarkerLabel("Ownership Score Consumer").setResourceUses(consumerUses, LengthOf(consumerUses));
    const auto consumerResult = graph.addTask(consumerDesc, computeCommands);
    ASSERT_TRUE(consumerResult);
    const Graphics::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());
    Graphics::GpuTaskDesc finalDesc;
    finalDesc.setIdentity(Name("tests/task_graph/ownership_score_final_consumer")).setMarkerLabel("Ownership Score Final Consumer").setResourceUses(finalUses, LengthOf(finalUses));
    const auto finalConsumerResult = graph.addTask(finalDesc, graphicsCommands);
    ASSERT_TRUE(finalConsumerResult);
    const Graphics::GpuTaskId finalConsumer = *finalConsumerResult;
    ASSERT_TRUE(finalConsumer.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
    ASSERT_EQ(analysis.inferredEdges().size(), 6u);
    ASSERT_EQ(analysis.schedulingEdges().size(), s_ExpectedDualCount);

    // The graphics queue has no compute support, so the compute consumer must run on the dedicated queue.
    const Graphics::GpuPhysicalQueueInfo graphicsQueue = GraphicsQueue(0u, QueueCapabilities(Graphics::GpuQueueCapability::Graphics, Graphics::GpuQueueCapability::Transfer));
    const Graphics::GpuPhysicalQueueInfo separateFamilyQueues[] = { graphicsQueue, DedicatedComputeQueue(), };
    const Graphics::GpuPhysicalQueueTopology separateFamilyTopology{ .queues = separateFamilyQueues, .queueCount = LengthOf(separateFamilyQueues), };
    Graphics::GpuTaskGraphQueueAssignments separateFamilyAssignments(testArena.arena);
    ASSERT_TRUE(Assign(separateFamilyAssignments, operationScratchArena, graph, analysis, separateFamilyTopology));
    const Graphics::GpuTaskQueueAssignment* const separateFamilyAssignment = separateFamilyAssignments.find(consumer);
    ASSERT_NE(separateFamilyAssignment, nullptr);
    EXPECT_EQ(separateFamilyAssignment->score.incomingCrossings, 1);
    EXPECT_EQ(separateFamilyAssignment->score.outgoingCrossings, 1);
    EXPECT_EQ(separateFamilyAssignment->score.ownershipTransfers, 4);
    const Graphics::GpuTaskQueueAssignment* const producerAssignment = separateFamilyAssignments.find(producer);
    const Graphics::GpuTaskQueueAssignment* const finalAssignment = separateFamilyAssignments.find(finalConsumer);
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(finalAssignment, nullptr);
    EXPECT_EQ(producerAssignment->score.ownershipTransfers, 2);
    EXPECT_EQ(finalAssignment->score.ownershipTransfers, 2);

    Graphics::GpuPhysicalQueueInfo sameFamilyCompute = DedicatedComputeQueue();
    sameFamilyCompute.familyIndex = graphicsQueue.familyIndex;
    sameFamilyCompute.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo sameFamilyQueues[] = { graphicsQueue, sameFamilyCompute, };
    const Graphics::GpuPhysicalQueueTopology sameFamilyTopology{ .queues = sameFamilyQueues, .queueCount = LengthOf(sameFamilyQueues), };
    Graphics::GpuTaskGraphQueueAssignments sameFamilyAssignments(testArena.arena);
    ASSERT_TRUE(Assign(sameFamilyAssignments, operationScratchArena, graph, analysis, sameFamilyTopology));
    const Graphics::GpuTaskQueueAssignment* const sameFamilyAssignment = sameFamilyAssignments.find(consumer);
    ASSERT_NE(sameFamilyAssignment, nullptr);
    EXPECT_EQ(sameFamilyAssignment->score.incomingCrossings, 1);
    EXPECT_EQ(sameFamilyAssignment->score.ownershipTransfers, 0);
    const Graphics::GpuTaskQueueAssignment* const sameFamilyProducer = sameFamilyAssignments.find(producer);
    const Graphics::GpuTaskQueueAssignment* const sameFamilyFinal = sameFamilyAssignments.find(finalConsumer);
    ASSERT_NE(sameFamilyProducer, nullptr);
    ASSERT_NE(sameFamilyFinal, nullptr);
    EXPECT_EQ(sameFamilyProducer->score.ownershipTransfers, 0);
    EXPECT_EQ(sameFamilyFinal->score.ownershipTransfers, 0);
}

TEST(GpuTaskGraph, RejectsInvalidAndIncompatibleQueueTopologiesDeterministically){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskCommandRequirements computeCommands;
    computeCommands.requiredCapabilities = Graphics::GpuQueueCapability::Compute;
    const auto taskResult = AddTaskWithCommands(graph, Name("tests/task_graph/queue_diagnostic"), "Queue Diagnostic", computeCommands);
    ASSERT_TRUE(taskResult);
    const Graphics::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    EXPECT_FALSE(Assign(assignments, operationScratchArena, graph, analysis, Graphics::GpuPhysicalQueueTopology{}));
    EXPECT_EQ(assignments.diagnostic().status, Graphics::GpuTaskGraphQueueAssignmentStatus::InvalidQueueTopology);

    const Graphics::GpuPhysicalQueueInfo graphicsOnly[] = { GraphicsQueue(0u, Graphics::GpuQueueCapability::Graphics), };
    const Graphics::GpuPhysicalQueueTopology graphicsOnlyTopology{ .queues = graphicsOnly, .queueCount = LengthOf(graphicsOnly), };
    EXPECT_FALSE(Assign(assignments, operationScratchArena, graph, analysis, graphicsOnlyTopology));
    EXPECT_EQ(assignments.diagnostic().status, Graphics::GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue);
    EXPECT_EQ(assignments.diagnostic().task, task);
    EXPECT_EQ(assignments.diagnostic().requiredCapabilities, Graphics::GpuQueueCapability::Compute);

    Graphics::GpuPhysicalQueueInfo invalidTransferQueue = DedicatedTransferQueue();
    invalidTransferQueue.capabilities = Graphics::GpuQueueCapability::Compute;
    const Graphics::GpuPhysicalQueueTopology invalidTransferTopology{ .queues = &invalidTransferQueue, .queueCount = 1u, };
    EXPECT_FALSE(Assign(assignments, operationScratchArena, graph, analysis, invalidTransferTopology));
    EXPECT_EQ(assignments.diagnostic().status, Graphics::GpuTaskGraphQueueAssignmentStatus::InvalidQueueTopology);

    Graphics::GpuPhysicalQueueInfo duplicateNativeQueue = GraphicsQueue(3u);
    // Different graph IDs must not alias the same physical queue family/index transport.
    duplicateNativeQueue.queueIndex = GraphicsQueue().queueIndex;
    const Graphics::GpuPhysicalQueueInfo duplicateNativeQueues[] = { GraphicsQueue(), duplicateNativeQueue, };
    const Graphics::GpuPhysicalQueueTopology duplicateNativeTopology{ .queues = duplicateNativeQueues, .queueCount = LengthOf(duplicateNativeQueues), };
    EXPECT_FALSE(Assign(assignments, operationScratchArena, graph, analysis, duplicateNativeTopology));
    EXPECT_EQ(assignments.diagnostic().status, Graphics::GpuTaskGraphQueueAssignmentStatus::InvalidQueueTopology);

    const Graphics::GpuPhysicalQueueInfo topologyA[] = { DedicatedComputeQueue(), GraphicsQueue() };
    const Graphics::GpuPhysicalQueueInfo topologyB[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuPhysicalQueueTopology firstTopology{ .queues = topologyA, .queueCount = LengthOf(topologyA), };
    const Graphics::GpuPhysicalQueueTopology secondTopology{ .queues = topologyB, .queueCount = LengthOf(topologyB), };
    Graphics::GpuTaskGraphQueueAssignments firstAssignments(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments secondAssignments(testArena.arena);
    ASSERT_TRUE(Assign(firstAssignments, operationScratchArena, graph, analysis, firstTopology));
    ASSERT_TRUE(Assign(secondAssignments, operationScratchArena, graph, analysis, secondTopology));
    const Graphics::GpuTaskQueueAssignment* const firstAssignment = firstAssignments.find(task);
    const Graphics::GpuTaskQueueAssignment* const secondAssignment = secondAssignments.find(task);
    ASSERT_NE(firstAssignment, nullptr);
    ASSERT_NE(secondAssignment, nullptr);
    EXPECT_EQ(firstAssignment->queue, secondAssignment->queue);
    EXPECT_EQ(firstAssignment->queueClass, secondAssignment->queueClass);
    EXPECT_EQ(firstAssignment->reason, secondAssignment->reason);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

