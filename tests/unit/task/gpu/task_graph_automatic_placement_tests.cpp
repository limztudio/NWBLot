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


void ExpectComputeStagePlacement(
    const Graphics::GpuTaskCostHint::Enum stageCost,
    const bool independentGraphics,
    const u64 computeQueueLoad,
    const Graphics::CommandQueue::Enum expectedQueueClass){
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
    stageScheduling.cost = stageCost;
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

    Graphics::GpuTaskId independent;
    if(independentGraphics){
        independent = AddTaskWithCommands(
            graph,
            Name("tests/task_graph/automatic_stage_independent_graphics"),
            "Automatic Stage Independent Graphics",
            GraphicsCommands(),
            graphicsScheduling
        );
        ASSERT_TRUE(independent.valid());
    }

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
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
    if(independentGraphics){
        ASSERT_NE(assignments.find(independent), nullptr);
        EXPECT_EQ(assignments.find(independent)->queue, queues[0u].id);
    }

    const Graphics::GpuTaskQueueAssignment* const stageAssignment = assignments.find(stage);
    ASSERT_NE(stageAssignment, nullptr);
    EXPECT_EQ(stageAssignment->queueClass, expectedQueueClass);
    EXPECT_EQ(stageAssignment->queue, queues[expectedQueueClass == Graphics::CommandQueue::Compute ? 1u : 0u].id);
    EXPECT_EQ(stageAssignment->score.preference, 0);
    EXPECT_EQ(stageAssignment->score.ownershipTransfers, 0);
    EXPECT_EQ(stageAssignment->modifiers, Graphics::GpuTaskQueueAssignmentModifier::None);
    const bool routedCompute = expectedQueueClass == Graphics::CommandQueue::Compute;
    EXPECT_EQ(stageAssignment->score.incomingCrossings, routedCompute ? 1 : 0);
    EXPECT_EQ(stageAssignment->score.outgoingCrossings, routedCompute ? 1 : 0);
    EXPECT_EQ(stageAssignment->score.overlap, routedCompute ? 8 : 0);
    EXPECT_EQ(
        stageAssignment->reason,
        stageCost == Graphics::GpuTaskCostHint::Tiny
            ? Graphics::GpuTaskQueueAssignmentReason::ConservativeAny
            : Graphics::GpuTaskQueueAssignmentReason::ScoredAny
    );
}


// Independent Graphics work can overlap the Compute stage even though its producer and consumer use Graphics.
TEST(GpuTaskGraph, AutomaticallyOverlapsComputeBetweenGraphicsProducerAndConsumer){
    ExpectComputeStagePlacement(Graphics::GpuTaskCostHint::Large, true, 0u, Graphics::CommandQueue::Compute);
}

TEST(GpuTaskGraph, KeepsComputeBetweenGraphicsDependenciesLocalWithoutIndependentWork){
    ExpectComputeStagePlacement(Graphics::GpuTaskCostHint::Large, false, 0u, Graphics::CommandQueue::Graphics);
}

TEST(GpuTaskGraph, AvoidsBusyComputeQueueForStageBetweenGraphicsDependencies){
    ExpectComputeStagePlacement(Graphics::GpuTaskCostHint::Large, true, 64u, Graphics::CommandQueue::Graphics);
}

TEST(GpuTaskGraph, KeepsTinyComputeBetweenGraphicsDependenciesLocal){
    ExpectComputeStagePlacement(Graphics::GpuTaskCostHint::Tiny, true, 0u, Graphics::CommandQueue::Graphics);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

