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


TEST(GpuTaskGraph, PreservesDiagnosticsForSingleLegalClassAndIndependentMergeGroup){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Large;
    const Graphics::GpuTaskId graphics = AddTaskWithCommands(graph, Name("tests/task_graph/automatic_diagnostics_graphics"), "Automatic Diagnostics Graphics", GraphicsCommands(), scheduling);
    ASSERT_TRUE(graphics.valid());
    const Graphics::GpuTaskId firstCompute = AddTaskWithCommands(graph, Name("tests/task_graph/automatic_diagnostics_first_compute"), "Automatic Diagnostics First Compute", ComputeCommands(), scheduling);
    ASSERT_TRUE(firstCompute.valid());
    scheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskId secondCompute = AddTaskWithCommands(graph, Name("tests/task_graph/automatic_diagnostics_second_compute"), "Automatic Diagnostics Second Compute", ComputeCommands(), scheduling);
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
            EXPECT_EQ(assignment->queue, expectedQueue.id);
        }
    }
}


TEST(GpuTaskGraph, ExtendsMergedGroupAfterLegalityWitnessFallsBackToGraphics){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Large;
    const Graphics::GpuTaskId first = AddTaskWithCommands(graph, Name("tests/task_graph/automatic_witness_compute"), "Automatic Witness Compute", ComputeCommands(), scheduling);
    ASSERT_TRUE(first.valid());
    scheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskCommandRequirements primaryGraphicsCommands{ .requiredCapabilities = Graphics::GpuQueueCapability::Graphics, .requiresPrimaryGraphicsQueue = true, };
    const Graphics::GpuTaskId graphics = AddTaskWithCommands(graph, Name("tests/task_graph/automatic_witness_graphics"), "Automatic Witness Graphics", primaryGraphicsCommands, scheduling, {}, &first, 1u);
    ASSERT_TRUE(graphics.valid());
    const Graphics::GpuTaskId last = AddTaskWithCommands(graph, Name("tests/task_graph/automatic_witness_final_compute"), "Automatic Witness Final Compute", ComputeCommands(), scheduling, {}, &graphics, 1u);
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
    const Graphics::GpuTaskId first = AddTaskWithCommands(graph, Name("tests/task_graph/automatic_witness_external_compute"), "Automatic Witness External Compute", commands, scheduling);
    ASSERT_TRUE(first.valid());
    scheduling.mergeWithPrevious = true;
    commands.externalQueue = queues[0u].id;
    const Graphics::GpuTaskId second = AddTaskWithCommands(graph, Name("tests/task_graph/automatic_witness_external_graphics"), "Automatic Witness External Graphics", commands, scheduling, {}, &first, 1u);
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
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

