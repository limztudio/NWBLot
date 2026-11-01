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
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    const auto graphicsResult = AddTaskWithCommands(Name("tests/task_graph/automatic_diagnostics_graphics"), "Automatic Diagnostics Graphics", GraphicsCommands(), scheduling, {}, {}, graph);
    ASSERT_TRUE(graphicsResult);
    const Core::GpuTaskId graphics = *graphicsResult;
    ASSERT_TRUE(graphics.valid());
    const auto firstComputeResult = AddTaskWithCommands(Name("tests/task_graph/automatic_diagnostics_first_compute"), "Automatic Diagnostics First Compute", ComputeCommands(), scheduling, {}, {}, graph);
    ASSERT_TRUE(firstComputeResult);
    const Core::GpuTaskId firstCompute = *firstComputeResult;
    ASSERT_TRUE(firstCompute.valid());
    scheduling.mergeWithPrevious = true;
    const auto secondComputeResult = AddTaskWithCommands(Name("tests/task_graph/automatic_diagnostics_second_compute"), "Automatic Diagnostics Second Compute", ComputeCommands(), scheduling, {}, {}, graph);
    ASSERT_TRUE(secondComputeResult);
    const Core::GpuTaskId secondCompute = *secondComputeResult;
    ASSERT_TRUE(secondCompute.valid());
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    ASSERT_TRUE(analysis.schedulingEdges().empty());

    const Core::GpuPhysicalQueueInfo graphicsQueue = GraphicsQueue();
    const Core::GpuPhysicalQueueInfo computeQueue = DedicatedComputeQueue();
    const Array<Array<Core::GpuPhysicalQueueInfo, 2u>, 2> queueOrders = {{
        { graphicsQueue, computeQueue },
        { computeQueue, graphicsQueue },
    }};
    const Core::GpuTaskQueueLoad queueLoad{ .queue = graphicsQueue.id, .estimatedCost = 13u };
    Core::GpuTaskGraphQueueAssignmentOptions options;
    options.queueLoads = &queueLoad;
    options.queueLoadCount = 1u;
    const Array<Core::GpuTaskId, 3> tasks = { graphics, firstCompute, secondCompute };
    for(const auto& queues : queueOrders){
        const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues) };
        Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, options));
        for(const Core::GpuTaskId task : tasks){
            const Core::GpuTaskQueueAssignment* const assignment = assignments.find(task);
            ASSERT_NE(assignment, nullptr);
            const b8 requiresGraphics = task == graphics;
            const Core::GpuPhysicalQueueInfo& expectedQueue = requiresGraphics ? graphicsQueue : computeQueue;
            EXPECT_EQ(assignment->queue, expectedQueue.id);
        }
    }
}


TEST(GpuTaskGraph, ExtendsMergedGroupAfterLegalityWitnessFallsBackToGraphics){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    const auto firstResult = AddTaskWithCommands(Name("tests/task_graph/automatic_witness_compute"), "Automatic Witness Compute", ComputeCommands(), scheduling, {}, {}, graph);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());
    scheduling.mergeWithPrevious = true;
    const Core::GpuTaskCommandRequirements primaryGraphicsCommands{ .requiredCapabilities = Core::GpuQueueCapability::Graphics, .requiresPrimaryGraphicsQueue = true, };
    const auto graphicsResult = AddTaskWithCommands(Name("tests/task_graph/automatic_witness_graphics"), "Automatic Witness Graphics", primaryGraphicsCommands, scheduling, {}, Span(&first, 1u), graph);
    ASSERT_TRUE(graphicsResult);
    const Core::GpuTaskId graphics = *graphicsResult;
    ASSERT_TRUE(graphics.valid());
    const auto lastResult = AddTaskWithCommands(Name("tests/task_graph/automatic_witness_final_compute"), "Automatic Witness Final Compute", ComputeCommands(), scheduling, {}, Span(&graphics, 1u), graph);
    ASSERT_TRUE(lastResult);
    const Core::GpuTaskId last = *lastResult;
    ASSERT_TRUE(last.valid());
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    const Core::GpuPhysicalQueueInfo graphicsQueue = GraphicsQueue(7u);
    const Core::GpuPhysicalQueueInfo computeQueue = DedicatedComputeQueue(0u);
    const Array<Array<Core::GpuPhysicalQueueInfo, 2u>, 2> queueOrders = {{
        { computeQueue, graphicsQueue },
        { graphicsQueue, computeQueue },
    }};
    const Array<Core::GpuTaskId, 3> tasks = { first, graphics, last };
    for(const auto& queues : queueOrders){
        const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues) };
        Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology));
        for(const Core::GpuTaskId task : tasks){
            const Core::GpuTaskQueueAssignment* const assignment = assignments.find(task);
            ASSERT_NE(assignment, nullptr);
            EXPECT_EQ(assignment->queue, graphicsQueue.id);
        }
    }
}

TEST(GpuTaskGraph, SplitsMergedGroupForDisjointExternalQueueContracts){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Array<Core::GpuPhysicalQueueInfo, 2> queues = { GraphicsQueue(), DedicatedComputeQueue() };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    Core::GpuTaskCommandRequirements commands = ComputeCommands();
    commands.externalQueue = queues[1u].id;
    const auto firstResult = AddTaskWithCommands(Name("tests/task_graph/automatic_witness_external_compute"), "Automatic Witness External Compute", commands, scheduling, {}, {}, graph);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());
    scheduling.mergeWithPrevious = true;
    commands.externalQueue = queues[0u].id;
    const auto secondResult = AddTaskWithCommands(Name("tests/task_graph/automatic_witness_external_graphics"), "Automatic Witness External Graphics", commands, scheduling, {}, Span(&first, 1u), graph);
    ASSERT_TRUE(secondResult);
    const Core::GpuTaskId second = *secondResult;
    ASSERT_TRUE(second.valid());
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues) };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology));
    const Core::GpuTaskQueueAssignment* const firstAssignment = assignments.find(first);
    const Core::GpuTaskQueueAssignment* const secondAssignment = assignments.find(second);
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

