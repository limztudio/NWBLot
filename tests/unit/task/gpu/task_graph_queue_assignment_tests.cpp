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
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskCommandRequirements mixedCommands{ QueueCapabilities(Graphics::GpuQueueCapability::Graphics, Graphics::GpuQueueCapability::Compute) };
    const Graphics::GpuTaskId task = AddTaskWithCommands(graph, Name("tests/task_graph/mixed_commands"), "Mixed Commands", mixedCommands);
    ASSERT_TRUE(task.valid());
    const Graphics::GpuPhysicalQueueInfo queues[] = { DedicatedTransferQueue(), DedicatedComputeQueue(), GraphicsQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments));
    ASSERT_NE(assignments.find(task), nullptr);
    EXPECT_EQ(assignments.find(task)->queue, queues[s_ThirdElementIndex].id);
    const Graphics::GpuPhysicalQueueInfo incompatibleQueues[] = { DedicatedComputeQueue(), DedicatedTransferQueue() };
    const Graphics::GpuPhysicalQueueTopology incompatibleTopology{ .queues = incompatibleQueues, .queueCount = LengthOf(incompatibleQueues) };
    EXPECT_FALSE(Assign(graph, analysis, incompatibleTopology, assignments));
    EXPECT_EQ(assignments.diagnostic().status, Graphics::GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue);
    EXPECT_EQ(assignments.diagnostic().task, task);
}

TEST(GpuTaskGraph, AutomaticallyRunsComputeOnGraphicsWhenNoComputeQueueExists){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskId task = AddTaskWithCommands(graph, Name("tests/task_graph/compute_on_graphics"), "Compute On Graphics", ComputeCommands());
    ASSERT_TRUE(task.valid());
    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments));
    ASSERT_NE(assignments.find(task), nullptr);
    EXPECT_EQ(assignments.find(task)->queue, queue.id);
    EXPECT_EQ(assignments.find(task)->reason, Graphics::GpuTaskQueueAssignmentReason::Scored);
}

TEST(GpuTaskGraph, RetainsTinyAndNonOverlappingAutomaticWorkOnGraphics){
    const auto runCase = [](const bool tiny){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::GpuTaskSchedulingHint scheduling;
        scheduling.cost = tiny ? Graphics::GpuTaskCostHint::Tiny : Graphics::GpuTaskCostHint::Medium;
        scheduling.overlapPreferred = tiny;
        const Graphics::GpuTaskId task = AddTaskWithCommands(graph, Name("tests/task_graph/automatic_small"), "Automatic Small", ComputeCommands(), scheduling);
        ASSERT_TRUE(task.valid());
        const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
        const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        ASSERT_TRUE(Analyze(graph, analysis));
        ASSERT_TRUE(Assign(graph, analysis, topology, assignments));
        ASSERT_NE(assignments.find(task), nullptr);
        EXPECT_EQ(assignments.find(task)->queue, queues[0u].id);
    };
    runCase(true);
    runCase(false);
}

TEST(GpuTaskGraph, CoLocatesMergedMixedCommandsOnAQueueSupportingTheWholeChain){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.allowPacketMerge = true;
    const Graphics::GpuTaskId producer = AddTaskWithCommands(graph, Name("tests/task_graph/automatic_merge_compute"), "Merge Compute", ComputeCommands(), producerScheduling);
    ASSERT_TRUE(producer.valid());
    Graphics::GpuTaskSchedulingHint consumerScheduling = producerScheduling;
    consumerScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskId consumer = AddTaskWithCommands(graph, Name("tests/task_graph/automatic_merge_graphics"), "Merge Graphics", GraphicsCommands(), consumerScheduling, {}, &producer, 1u);
    ASSERT_TRUE(consumer.valid());
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
    ASSERT_NE(assignments.find(producer), nullptr);
    ASSERT_NE(assignments.find(consumer), nullptr);
    EXPECT_EQ(assignments.find(producer)->queue, queues[0u].id);
    EXPECT_EQ(assignments.find(consumer)->queue, queues[0u].id);
    EXPECT_EQ(compiledPlan.packetForTask(producer), compiledPlan.packetForTask(consumer));
}

TEST(GpuTaskGraph, KeepsTaskOwnedPrimaryGraphicsRestrictionDuringAutomaticPlacement){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.allowSameClassQueueRouting = true;
    scheduling.preferNonPrimarySameClassQueue = true;
    const Graphics::GpuTaskCommandRequirements commands{ .requiredCapabilities = Graphics::GpuQueueCapability::Compute, .requiresPrimaryGraphicsQueue = true };
    const Graphics::GpuTaskId task = AddTaskWithCommands(graph, Name("tests/task_graph/primary_graphics_contract"), "Primary Graphics Contract", commands, scheduling);
    ASSERT_TRUE(task.valid());
    Graphics::GpuPhysicalQueueInfo auxiliary = GraphicsQueue(3u);
    auxiliary.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo queues[] = { auxiliary, DedicatedComputeQueue(), GraphicsQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments));
    ASSERT_NE(assignments.find(task), nullptr);
    EXPECT_EQ(assignments.find(task)->queue, GraphicsQueue().id);
    const Graphics::GpuTaskDiagnosticQueueOverride override{ .task = task, .queue = auxiliary.id };
    Graphics::GpuTaskGraphQueueAssignmentOptions options;
    options.diagnosticQueueOverrides = &override;
    options.diagnosticQueueOverrideCount = 1u;
    EXPECT_FALSE(Assign(graph, analysis, topology, assignments, options));
    EXPECT_EQ(
        assignments.diagnostic().status,
        Graphics::GpuTaskGraphQueueAssignmentStatus::InvalidDiagnosticQueueOverride
    );
}

TEST(GpuTaskGraph, RejectsConflictingDiagnosticQueueOverridesForMergedTasks){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.allowPacketMerge = true;
    const Graphics::GpuTaskId first = AddTaskWithCommands(graph, Name("tests/task_graph/override_merge_first"), "Override Merge First", ComputeCommands(), firstScheduling);
    ASSERT_TRUE(first.valid());
    Graphics::GpuTaskSchedulingHint secondScheduling = firstScheduling;
    secondScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskId second = AddTaskWithCommands(graph, Name("tests/task_graph/override_merge_second"), "Override Merge Second", ComputeCommands(), secondScheduling, {}, &first, 1u);
    ASSERT_TRUE(second.valid());
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    const Graphics::GpuTaskDiagnosticQueueOverride overrides[] = {
        { .task = first, .queue = queues[0u].id },
        { .task = second, .queue = queues[1u].id },
    };
    Graphics::GpuTaskGraphQueueAssignmentOptions options;
    options.diagnosticQueueOverrides = overrides;
    options.diagnosticQueueOverrideCount = LengthOf(overrides);
    EXPECT_FALSE(Assign(graph, analysis, topology, assignments, options));
    EXPECT_EQ(
        assignments.diagnostic().status,
        Graphics::GpuTaskGraphQueueAssignmentStatus::InvalidDiagnosticQueueOverride
    );
}

TEST(GpuTaskGraph, AutomaticallyPreservesAnImportedExclusiveOwnerForFirstUse){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuGraphResourceId resource = graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/automatic_owner_buffer"))
            .setMarkerLabel("Automatic Owner Buffer")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::Common)
            .setInitialOwnerQueue(queues[0u].id)
    );
    ASSERT_TRUE(resource.valid());
    const Graphics::GpuTaskResourceUse use{
        .resource = resource,
        .range = {},
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/automatic_owner_compute"))
        .setMarkerLabel("Automatic Owner Compute")
        .setResourceUses(&use, 1u)
    ;
    const Graphics::GpuTaskId task = graph.addTask(desc, ComputeCommands());
    ASSERT_TRUE(task.valid());
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    ASSERT_NE(assignments.find(task), nullptr);
    EXPECT_EQ(assignments.find(task)->queue, queues[0u].id);
}

TEST(GpuTaskGraph, ValidatesDiagnosticQueueOverridesAgainstCommandCapabilities){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskId task = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/diagnostic_queue_override"),
        "Diagnostic Queue Override",
        ComputeCommands()
    );
    ASSERT_TRUE(task.valid());
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(0u, Graphics::GpuQueueCapability::Graphics), DedicatedComputeQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    Graphics::GpuTaskDiagnosticQueueOverride override{ .task = task, .queue = queues[1u].id };
    Graphics::GpuTaskGraphQueueAssignmentOptions options;
    options.diagnosticQueueOverrides = &override;
    options.diagnosticQueueOverrideCount = 1u;
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, options));
    ASSERT_NE(assignments.find(task), nullptr);
    EXPECT_EQ(assignments.find(task)->queue, queues[1u].id);
    EXPECT_TRUE(assignments.find(task)->modifiers & Graphics::GpuTaskQueueAssignmentModifier::DiagnosticQueueOverride);
    override.queue = queues[0u].id;
    EXPECT_FALSE(Assign(graph, analysis, topology, assignments, options));
    EXPECT_EQ(
        assignments.diagnostic().status,
        Graphics::GpuTaskGraphQueueAssignmentStatus::InvalidDiagnosticQueueOverride
    );
    override.queue = { .index = 1u, .deviceGeneration = s_ExpectedDualCount };
    EXPECT_FALSE(Assign(graph, analysis, topology, assignments, options));
    EXPECT_EQ(
        assignments.diagnostic().status,
        Graphics::GpuTaskGraphQueueAssignmentStatus::InvalidDiagnosticQueueOverride
    );
}

TEST(GpuTaskGraph, ChoosesComputePlacementFromOverlapAndExternalQueueLoad){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    Graphics::GpuTaskSchedulingHint graphicsScheduling;
    graphicsScheduling.cost = Graphics::GpuTaskCostHint::Large;
    ASSERT_TRUE(AddTaskWithCommands(
        graph,
        Name("tests/task_graph/external_load_graphics"),
        "External Load Graphics",
        graphicsCommands,
        graphicsScheduling
    ).valid());

    Graphics::GpuTaskCommandRequirements computeCommands;
    computeCommands.requiredCapabilities = Graphics::GpuQueueCapability::Compute;
    const Graphics::GpuTaskId computeTask = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/external_load_compute"),
        "External Load Compute",
        computeCommands
    );
    ASSERT_TRUE(computeTask.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    Graphics::GpuTaskGraphQueueAssignments idleAssignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, topology, idleAssignments));
    const Graphics::GpuTaskQueueAssignment* const idleAssignment = idleAssignments.find(computeTask);
    ASSERT_NE(idleAssignment, nullptr);
    EXPECT_EQ(idleAssignment->queueClass, Graphics::CommandQueue::Compute);

    const Graphics::GpuTaskQueueLoad queueLoads[]{
        {
            .queue = DedicatedComputeQueue().id,
            .estimatedCost = 32u,
        },
    };
    Graphics::GpuTaskGraphQueueAssignmentOptions options;
    options.queueLoads = queueLoads;
    options.queueLoadCount = LengthOf(queueLoads);
    Graphics::GpuTaskGraphQueueAssignments loadedAssignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, topology, loadedAssignments, options));
    const Graphics::GpuTaskQueueAssignment* const loadedAssignment = loadedAssignments.find(computeTask);
    ASSERT_NE(loadedAssignment, nullptr);
    EXPECT_EQ(loadedAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(loadedAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::Scored);
    EXPECT_EQ(loadedAssignment->score.queueLoad, 8);
}

TEST(GpuTaskGraph, ChoosesAutomaticPlacementDeterministicallyAcrossTopologyOrder){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    Graphics::GpuTaskSchedulingHint graphicsScheduling;
    graphicsScheduling.cost = Graphics::GpuTaskCostHint::Large;
    const Graphics::GpuTaskId graphicsTask = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/deterministic_graphics"),
        "Deterministic Graphics",
        graphicsCommands,
        graphicsScheduling
    );

    Graphics::GpuTaskCommandRequirements transferCommands;
    transferCommands.requiredCapabilities = Graphics::GpuQueueCapability::Transfer;
    const Graphics::GpuTaskId transferTask = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/deterministic_transfer"),
        "Deterministic Transfer",
        transferCommands
    );
    ASSERT_TRUE(graphicsTask.valid());
    ASSERT_TRUE(transferTask.valid());

    Graphics::GpuPhysicalQueueInfo auxiliaryGraphics = GraphicsQueue(3u);
    auxiliaryGraphics.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo firstQueues[] = {
        auxiliaryGraphics,
        DedicatedTransferQueue(),
        DedicatedComputeQueue(),
        GraphicsQueue(),
    };
    const Graphics::GpuPhysicalQueueInfo secondQueues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
        auxiliaryGraphics,
        DedicatedTransferQueue(),
    };
    const Graphics::GpuPhysicalQueueTopology firstTopology{
        .queues = firstQueues,
        .queueCount = LengthOf(firstQueues),
    };
    const Graphics::GpuPhysicalQueueTopology secondTopology{
        .queues = secondQueues,
        .queueCount = LengthOf(secondQueues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    Graphics::GpuTaskGraphQueueAssignments firstAssignments(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments secondAssignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, firstTopology, firstAssignments));
    ASSERT_TRUE(Assign(graph, analysis, secondTopology, secondAssignments));

    const Graphics::GpuTaskQueueAssignment* const firstAssignment = firstAssignments.find(transferTask);
    const Graphics::GpuTaskQueueAssignment* const secondAssignment = secondAssignments.find(transferTask);
    ASSERT_NE(firstAssignment, nullptr);
    ASSERT_NE(secondAssignment, nullptr);
    EXPECT_EQ(firstAssignment->queue, DedicatedComputeQueue().id);
    EXPECT_EQ(secondAssignment->queue, firstAssignment->queue);
    EXPECT_NE(firstAssignment->queue, auxiliaryGraphics.id);
    EXPECT_EQ(firstAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::Scored);
    EXPECT_EQ(firstAssignment->initialQueue, firstAssignment->queue);
    EXPECT_EQ(firstAssignment->modifiers, Graphics::GpuTaskQueueAssignmentModifier::None);
    EXPECT_EQ(firstAssignment->score.overlap, 8);
    EXPECT_EQ(firstAssignment->score.queueLoad, 0);
    EXPECT_EQ(firstAssignment->score.incomingCrossings, 0);
    EXPECT_EQ(firstAssignment->score.outgoingCrossings, 0);
    EXPECT_EQ(firstAssignment->score.ownershipTransfers, 0);
}

TEST(GpuTaskGraph, QueueScoreUsesOnlyReducedOutgoingDependencies){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskCommandRequirements computeCommands;
    computeCommands.requiredCapabilities = Graphics::GpuQueueCapability::Compute;
    Graphics::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;

    const Graphics::GpuTaskId producer = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/reduced_queue_score_producer"),
        "Reduced Queue Score Producer",
        computeCommands
    );
    const Graphics::GpuTaskId middle = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/reduced_queue_score_middle"),
        "Reduced Queue Score Middle",
        graphicsCommands,
        {},
        {},
        &producer,
        1u
    );
    const Graphics::GpuTaskId finalDependencies[] = { producer, middle };
    const Graphics::GpuTaskId finalTask = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/reduced_queue_score_final"),
        "Reduced Queue Score Final",
        graphicsCommands,
        {},
        {},
        finalDependencies,
        LengthOf(finalDependencies)
    );
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(middle.valid());
    ASSERT_TRUE(finalTask.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.edges().size(), 3u);
    ASSERT_EQ(analysis.schedulingEdges().size(), s_ExpectedDualCount);
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    const Graphics::GpuTaskDiagnosticQueueOverride route{ .task = producer, .queue = queues[1u].id };
    Graphics::GpuTaskGraphQueueAssignmentOptions options;
    options.diagnosticQueueOverrides = &route;
    options.diagnosticQueueOverrideCount = 1u;
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, options));
    const Graphics::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
    ASSERT_NE(producerAssignment, nullptr);
    EXPECT_EQ(producerAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(producerAssignment->score.outgoingCrossings, 1);
    EXPECT_EQ(producerAssignment->score.incomingCrossings, 0);
    EXPECT_EQ(producerAssignment->score.ownershipTransfers, 0);
}

TEST(GpuTaskGraph, DeduplicatesRawOwnershipScoreAndIgnoresSameFamilyQueueCrossings){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId texture = AddTextureMetadata(
        graph,
        Name("tests/task_graph/ownership_score_texture"),
        "Ownership Score Texture"
    );
    ASSERT_TRUE(texture.valid());
    const Graphics::GpuGraphResourceId secondTexture = AddTextureMetadata(
        graph,
        Name("tests/task_graph/ownership_score_second_texture"),
        "Ownership Score Second Texture"
    );
    ASSERT_TRUE(secondTexture.valid());

    const Graphics::GpuTaskResourceUse producerUse{
        .resource = texture,
        .range = {},
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskResourceUse consumerUse{
        .resource = texture,
        .range = {},
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::ReadWrite,
    };
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
    producerDesc
        .setIdentity(Name("tests/task_graph/ownership_score_producer"))
        .setMarkerLabel("Ownership Score Producer")
        .setResourceUses(producerUses, LengthOf(producerUses))
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc, graphicsCommands);
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/ownership_score_consumer"))
        .setMarkerLabel("Ownership Score Consumer")
        .setResourceUses(consumerUses, LengthOf(consumerUses))
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc, computeCommands);
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());
    Graphics::GpuTaskDesc finalDesc;
    finalDesc
        .setIdentity(Name("tests/task_graph/ownership_score_final_consumer"))
        .setMarkerLabel("Ownership Score Final Consumer")
        .setResourceUses(finalUses, LengthOf(finalUses))
    ;
    const Graphics::GpuTaskId finalConsumer = graph.addTask(finalDesc, graphicsCommands);
    ASSERT_TRUE(finalConsumer.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.inferredEdges().size(), 6u);
    ASSERT_EQ(analysis.schedulingEdges().size(), s_ExpectedDualCount);

    const Graphics::GpuPhysicalQueueInfo separateFamilyQueues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuPhysicalQueueTopology separateFamilyTopology{
        .queues = separateFamilyQueues,
        .queueCount = LengthOf(separateFamilyQueues),
    };
    Graphics::GpuTaskGraphQueueAssignments separateFamilyAssignments(testArena.arena);
    const Graphics::GpuTaskDiagnosticQueueOverride route{ .task = consumer, .queue = separateFamilyQueues[1u].id };
    Graphics::GpuTaskGraphQueueAssignmentOptions options;
    options.diagnosticQueueOverrides = &route;
    options.diagnosticQueueOverrideCount = 1u;
    ASSERT_TRUE(Assign(graph, analysis, separateFamilyTopology, separateFamilyAssignments, options));
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
    sameFamilyCompute.familyIndex = GraphicsQueue().familyIndex;
    sameFamilyCompute.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo sameFamilyQueues[] = {
        GraphicsQueue(),
        sameFamilyCompute,
    };
    const Graphics::GpuPhysicalQueueTopology sameFamilyTopology{
        .queues = sameFamilyQueues,
        .queueCount = LengthOf(sameFamilyQueues),
    };
    Graphics::GpuTaskGraphQueueAssignments sameFamilyAssignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, sameFamilyTopology, sameFamilyAssignments, options));
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

TEST(GpuTaskGraph, UsesFutureConsumerRoutesWhenChoosingAutomaticPlacement){
    const auto runCase = [](const bool addFutureConsumers){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuTaskId producer = AddTaskWithCommands(graph, Name("tests/task_graph/automatic_future_producer"), "Future Producer", GraphicsCommands());
        ASSERT_TRUE(producer.valid());
        const Graphics::GpuTaskId movable = AddTaskWithCommands(graph, Name("tests/task_graph/automatic_future_movable"), "Future Movable", ComputeCommands(), {}, {}, &producer, 1u);
        ASSERT_TRUE(movable.valid());
        const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
        Graphics::GpuTaskDiagnosticQueueOverride routes[3u] = {};
        const Name consumerIdentities[] = { Name("tests/task_graph/future_first"), Name("tests/task_graph/future_second"), Name("tests/task_graph/future_third") };
        if(addFutureConsumers){
            for(usize index = 0u; index < LengthOf(routes); ++index){
                routes[index].task = AddTaskWithCommands(graph, consumerIdentities[index], "Future Consumer", ComputeCommands(), {}, {}, &movable, 1u);
                routes[index].queue = queues[1u].id;
                ASSERT_TRUE(routes[index].task.valid());
            }
        }
        const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        ASSERT_TRUE(Analyze(graph, analysis));
        Graphics::GpuTaskGraphQueueAssignmentOptions options;
        options.diagnosticQueueOverrides = addFutureConsumers ? routes : nullptr;
        options.diagnosticQueueOverrideCount = addFutureConsumers ? LengthOf(routes) : 0u;
        ASSERT_TRUE(Assign(graph, analysis, topology, assignments, options));
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(movable);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queues[addFutureConsumers ? 1u : 0u].id);
        EXPECT_EQ(assignment->score.outgoingCrossings, 0);
        EXPECT_EQ(assignment->score.incomingCrossings, addFutureConsumers ? 1 : 0);
    };
    runCase(false);
    runCase(true);
}

TEST(GpuTaskGraph, RejectsInvalidAndIncompatibleQueueTopologiesDeterministically){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskCommandRequirements computeCommands;
    computeCommands.requiredCapabilities = Graphics::GpuQueueCapability::Compute;
    const Graphics::GpuTaskId task = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/queue_diagnostic"),
        "Queue Diagnostic",
        computeCommands
    );
    ASSERT_TRUE(task.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    EXPECT_FALSE(Assign(graph, analysis, Graphics::GpuPhysicalQueueTopology{}, assignments));
    EXPECT_EQ(
        assignments.diagnostic().status,
        Graphics::GpuTaskGraphQueueAssignmentStatus::InvalidQueueTopology
    );

    const Graphics::GpuPhysicalQueueInfo graphicsOnly[] = {
        GraphicsQueue(0u, Graphics::GpuQueueCapability::Graphics),
    };
    const Graphics::GpuPhysicalQueueTopology graphicsOnlyTopology{
        .queues = graphicsOnly,
        .queueCount = LengthOf(graphicsOnly),
    };
    EXPECT_FALSE(Assign(graph, analysis, graphicsOnlyTopology, assignments));
    EXPECT_EQ(
        assignments.diagnostic().status,
        Graphics::GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue
    );
    EXPECT_EQ(assignments.diagnostic().task, task);
    EXPECT_EQ(assignments.diagnostic().requiredCapabilities, Graphics::GpuQueueCapability::Compute);

    Graphics::GpuPhysicalQueueInfo invalidTransferQueue = DedicatedTransferQueue();
    invalidTransferQueue.capabilities = Graphics::GpuQueueCapability::Compute;
    const Graphics::GpuPhysicalQueueTopology invalidTransferTopology{
        .queues = &invalidTransferQueue,
        .queueCount = 1u,
    };
    EXPECT_FALSE(Assign(graph, analysis, invalidTransferTopology, assignments));
    EXPECT_EQ(
        assignments.diagnostic().status,
        Graphics::GpuTaskGraphQueueAssignmentStatus::InvalidQueueTopology
    );

    Graphics::GpuPhysicalQueueInfo duplicateNativeQueue = GraphicsQueue(3u);
    // Different graph IDs must not alias the same Vulkan family/index transport.
    duplicateNativeQueue.queueIndex = GraphicsQueue().queueIndex;
    const Graphics::GpuPhysicalQueueInfo duplicateNativeQueues[] = {
        GraphicsQueue(),
        duplicateNativeQueue,
    };
    const Graphics::GpuPhysicalQueueTopology duplicateNativeTopology{
        .queues = duplicateNativeQueues,
        .queueCount = LengthOf(duplicateNativeQueues),
    };
    EXPECT_FALSE(Assign(graph, analysis, duplicateNativeTopology, assignments));
    EXPECT_EQ(
        assignments.diagnostic().status,
        Graphics::GpuTaskGraphQueueAssignmentStatus::InvalidQueueTopology
    );

    const Graphics::GpuPhysicalQueueInfo topologyA[] = { DedicatedComputeQueue(), GraphicsQueue() };
    const Graphics::GpuPhysicalQueueInfo topologyB[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuPhysicalQueueTopology firstTopology{
        .queues = topologyA,
        .queueCount = LengthOf(topologyA),
    };
    const Graphics::GpuPhysicalQueueTopology secondTopology{
        .queues = topologyB,
        .queueCount = LengthOf(topologyB),
    };
    Graphics::GpuTaskGraphQueueAssignments firstAssignments(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments secondAssignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, firstTopology, firstAssignments));
    ASSERT_TRUE(Assign(graph, analysis, secondTopology, secondAssignments));
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

