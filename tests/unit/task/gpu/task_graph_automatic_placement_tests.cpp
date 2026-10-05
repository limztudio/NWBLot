// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_automatic_placement_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


void ExpectComputeStagePlacement(const u64 computeQueueLoad, const Graphics::CommandQueue::Enum expectedQueueClass){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskSchedulingHint graphicsScheduling;
    graphicsScheduling.cost = Graphics::GpuTaskCostHint::Large;
    const Graphics::GpuTaskId producer = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/automatic_stage_graphics_producer"),
        "Automatic Stage Graphics Producer",
        GraphicsCommands(),
        graphicsScheduling
    );
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskSchedulingHint stageScheduling;
    stageScheduling.cost = Graphics::GpuTaskCostHint::Large;
    const Graphics::GpuTaskId stage = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/automatic_stage_compute"),
        "Automatic Compute Stage",
        ComputeCommands(),
        stageScheduling,
        {},
        &producer,
        1u
    );
    ASSERT_TRUE(stage.valid());
    const Graphics::GpuTaskId consumer = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/automatic_stage_graphics_consumer"),
        "Automatic Stage Graphics Consumer",
        GraphicsCommands(),
        graphicsScheduling,
        {},
        &stage,
        1u
    );
    ASSERT_TRUE(consumer.valid());

    const Graphics::GpuTaskId independent = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/automatic_stage_independent_graphics"),
        "Automatic Stage Independent Graphics",
        GraphicsCommands(),
        graphicsScheduling
    );
    ASSERT_TRUE(independent.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    const Graphics::GpuTaskQueueLoad queueLoad{ .queue = queues[1u].id, .estimatedCost = computeQueueLoad };
    Graphics::GpuTaskGraphQueueAssignmentOptions options;
    if(computeQueueLoad != 0u){
        options.queueLoads = &queueLoad;
        options.queueLoadCount = 1u;
    }
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, options));
    ASSERT_NE(assignments.find(producer), nullptr);
    ASSERT_NE(assignments.find(consumer), nullptr);
    EXPECT_EQ(assignments.find(producer)->queue, queues[0u].id);
    EXPECT_EQ(assignments.find(consumer)->queue, queues[0u].id);
    ASSERT_NE(assignments.find(independent), nullptr);
    EXPECT_EQ(assignments.find(independent)->queue, queues[0u].id);

    const Graphics::GpuTaskQueueAssignment* const stageAssignment = assignments.find(stage);
    ASSERT_NE(stageAssignment, nullptr);
    EXPECT_EQ(stageAssignment->queueClass, expectedQueueClass);
    EXPECT_EQ(stageAssignment->queue, queues[expectedQueueClass == Graphics::CommandQueue::Compute ? 1u : 0u].id);
    EXPECT_EQ(stageAssignment->score.ownershipTransfers, 0);
    EXPECT_EQ(stageAssignment->modifiers, Graphics::GpuTaskQueueAssignmentModifier::None);
    const bool routedCompute = expectedQueueClass == Graphics::CommandQueue::Compute;
    EXPECT_EQ(stageAssignment->score.incomingCrossings, routedCompute ? 1 : 0);
    EXPECT_EQ(stageAssignment->score.outgoingCrossings, routedCompute ? 1 : 0);
    EXPECT_EQ(stageAssignment->score.overlap, routedCompute ? 8 : 0);
    EXPECT_EQ(stageAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::Scored);
}


// Independent Graphics work can overlap the Compute stage even though its producer and consumer use Graphics.
TEST(GpuTaskGraph, AutomaticallyOverlapsComputeBetweenGraphicsProducerAndConsumer){
    ExpectComputeStagePlacement(0u, Graphics::CommandQueue::Compute);
}

TEST(GpuTaskGraph, AvoidsBusyComputeQueueForStageBetweenGraphicsDependencies){
    ExpectComputeStagePlacement(64u, Graphics::CommandQueue::Graphics);
}

TEST(GpuTaskGraph, PreservesDiagnosticsForSingleLegalClassAndIndependentMergeGroup){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Large;
    const Graphics::GpuTaskId graphics = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/automatic_diagnostics_graphics"),
        "Automatic Diagnostics Graphics",
        GraphicsCommands(),
        scheduling
    );
    ASSERT_TRUE(graphics.valid());
    const Graphics::GpuTaskId firstCompute = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/automatic_diagnostics_first_compute"),
        "Automatic Diagnostics First Compute",
        ComputeCommands(),
        scheduling
    );
    ASSERT_TRUE(firstCompute.valid());
    scheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskId secondCompute = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/automatic_diagnostics_second_compute"),
        "Automatic Diagnostics Second Compute",
        ComputeCommands(),
        scheduling
    );
    ASSERT_TRUE(secondCompute.valid());
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_TRUE(analysis.schedulingEdges().empty());

    const Graphics::GpuPhysicalQueueInfo graphicsQueue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueInfo computeQueue = DedicatedComputeQueue();
    const Graphics::GpuPhysicalQueueInfo queueOrders[][2u] = {
        { graphicsQueue, computeQueue },
        { computeQueue, graphicsQueue },
    };
    const Graphics::GpuTaskQueueLoad queueLoad{ .queue = graphicsQueue.id, .estimatedCost = 13u };
    Graphics::GpuTaskGraphQueueAssignmentOptions options;
    options.queueLoads = &queueLoad;
    options.queueLoadCount = 1u;
    const Graphics::GpuTaskId tasks[] = { graphics, firstCompute, secondCompute };
    for(const auto& queues : queueOrders){
        const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        ASSERT_TRUE(Assign(graph, analysis, topology, assignments, options));
        for(const Graphics::GpuTaskId task : tasks){
            const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
            ASSERT_NE(assignment, nullptr);
            const bool requiresGraphics = task == graphics;
            const Graphics::GpuPhysicalQueueInfo& expectedQueue = requiresGraphics ? graphicsQueue : computeQueue;
            EXPECT_EQ(assignment->initialQueue, expectedQueue.id);
            EXPECT_EQ(assignment->queue, expectedQueue.id);
            EXPECT_EQ(assignment->queueClass, expectedQueue.queueClass);
            EXPECT_EQ(
                assignment->reason,
                requiresGraphics
                    ? Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics
                    : Graphics::GpuTaskQueueAssignmentReason::Scored
            );
            EXPECT_EQ(assignment->modifiers, Graphics::GpuTaskQueueAssignmentModifier::None);
            EXPECT_EQ(assignment->score.overlap, requiresGraphics ? 16 : 8);
            EXPECT_EQ(assignment->score.queueLoad, requiresGraphics ? 13 : 8);
            EXPECT_EQ(assignment->score.incomingCrossings, 0);
            EXPECT_EQ(assignment->score.outgoingCrossings, 0);
            EXPECT_EQ(assignment->score.ownershipTransfers, 0);
            EXPECT_EQ(assignment->score.total(), requiresGraphics ? 3 : 0);
        }
    }
}


TEST(GpuTaskGraph, ExtendsMergedGroupAfterLegalityWitnessFallsBackToGraphics){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Large;
    const Graphics::GpuTaskId first = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/automatic_witness_compute"),
        "Automatic Witness Compute",
        ComputeCommands(),
        scheduling
    );
    ASSERT_TRUE(first.valid());
    scheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskCommandRequirements primaryGraphicsCommands{
        .requiredCapabilities = Graphics::GpuQueueCapability::Graphics,
        .requiresPrimaryGraphicsQueue = true,
    };
    const Graphics::GpuTaskId graphics = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/automatic_witness_graphics"),
        "Automatic Witness Graphics",
        primaryGraphicsCommands,
        scheduling,
        {},
        &first,
        1u
    );
    ASSERT_TRUE(graphics.valid());
    const Graphics::GpuTaskId last = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/automatic_witness_final_compute"),
        "Automatic Witness Final Compute",
        ComputeCommands(),
        scheduling,
        {},
        &graphics,
        1u
    );
    ASSERT_TRUE(last.valid());
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    const Graphics::GpuPhysicalQueueInfo graphicsQueue = GraphicsQueue(7u);
    const Graphics::GpuPhysicalQueueInfo computeQueue = DedicatedComputeQueue(0u);
    const Graphics::GpuPhysicalQueueInfo queueOrders[][2u] = {
        { computeQueue, graphicsQueue },
        { graphicsQueue, computeQueue },
    };
    const Graphics::GpuTaskId tasks[] = { first, graphics, last };
    for(const auto& queues : queueOrders){
        const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        ASSERT_TRUE(Assign(graph, analysis, topology, assignments));
        for(const Graphics::GpuTaskId task : tasks){
            const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
            ASSERT_NE(assignment, nullptr);
            EXPECT_EQ(assignment->queue, graphicsQueue.id);
            EXPECT_EQ(assignment->initialQueue, graphicsQueue.id);
            EXPECT_EQ(assignment->reason, Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics);
            EXPECT_EQ(assignment->modifiers, Graphics::GpuTaskQueueAssignmentModifier::None);
            EXPECT_EQ(assignment->score.queueLoad, 16);
            EXPECT_EQ(assignment->score.overlap, 0);
            EXPECT_EQ(assignment->score.incomingCrossings, 0);
            EXPECT_EQ(assignment->score.outgoingCrossings, 0);
        }
    }
}

TEST(GpuTaskGraph, SplitsMergedGroupForDisjointExternalQueueContracts){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Large;
    Graphics::GpuTaskCommandRequirements commands = ComputeCommands();
    commands.externalQueue = queues[1u].id;
    const Graphics::GpuTaskId first = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/automatic_witness_external_compute"),
        "Automatic Witness External Compute",
        commands,
        scheduling
    );
    ASSERT_TRUE(first.valid());
    scheduling.mergeWithPrevious = true;
    commands.externalQueue = queues[0u].id;
    const Graphics::GpuTaskId second = AddTaskWithCommands(
        graph,
        Name("tests/task_graph/automatic_witness_external_graphics"),
        "Automatic Witness External Graphics",
        commands,
        scheduling,
        {},
        &first,
        1u
    );
    ASSERT_TRUE(second.valid());
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments));
    const Graphics::GpuTaskQueueAssignment* const firstAssignment = assignments.find(first);
    const Graphics::GpuTaskQueueAssignment* const secondAssignment = assignments.find(second);
    ASSERT_NE(firstAssignment, nullptr);
    ASSERT_NE(secondAssignment, nullptr);
    EXPECT_EQ(firstAssignment->queue, queues[1u].id);
    EXPECT_EQ(secondAssignment->queue, queues[0u].id);
    EXPECT_EQ(firstAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::Scored);
    EXPECT_EQ(secondAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::Scored);
    EXPECT_EQ(firstAssignment->modifiers, Graphics::GpuTaskQueueAssignmentModifier::None);
    EXPECT_EQ(secondAssignment->modifiers, Graphics::GpuTaskQueueAssignmentModifier::None);
    EXPECT_EQ(firstAssignment->score.outgoingCrossings, 1);
    EXPECT_EQ(secondAssignment->score.incomingCrossings, 1);
    EXPECT_EQ(firstAssignment->score.overlap, 0);
    EXPECT_EQ(secondAssignment->score.overlap, 0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

