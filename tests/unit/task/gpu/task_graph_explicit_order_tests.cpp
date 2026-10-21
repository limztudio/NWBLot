// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_explicit_order_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_DeepCycleTaskLabel = "Deep Cycle Task";

using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, UsesTheFullExplicitOrderToOrientInferredHazards){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto firstResourceResult = AddHazardDomain(graph, Name("tests/task_graph/explicit_first_resource"), "Explicit First Resource");
    ASSERT_TRUE(firstResourceResult);
    const Graphics::GpuGraphResourceId firstResource = *firstResourceResult;
    const auto secondResourceResult = AddHazardDomain(graph, Name("tests/task_graph/explicit_second_resource"), "Explicit Second Resource");
    ASSERT_TRUE(secondResourceResult);
    const Graphics::GpuGraphResourceId secondResource = *secondResourceResult;
    ASSERT_TRUE(firstResource.valid());
    ASSERT_TRUE(secondResource.valid());

    const Graphics::GpuTaskResourceUse firstUses[] = { Graphics::GpuTaskResourceUse{ .resource = firstResource, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write, }, };
    const Graphics::GpuTaskResourceUse secondUses[] = {
        Graphics::GpuTaskResourceUse{ .resource = firstResource, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read, },
        Graphics::GpuTaskResourceUse{ .resource = secondResource, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write, },
    };
    const Graphics::GpuTaskResourceUse thirdUses[] = { Graphics::GpuTaskResourceUse{ .resource = secondResource, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read, }, };

    // The explicit third -> first edge has to order both inferred resource hazards.
    // Pair-local declaration order would instead infer first -> second -> third and invent a cycle with this valid explicit edge.
    u64 graphGeneration = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        graphGeneration = declarations.generation();
    }
    const Graphics::GpuTaskId futureThird{ .generation = graphGeneration, .index = s_ExpectedDualCount };
    const auto firstResult = AddTask(graph, Name("tests/task_graph/explicit_first"), "Explicit First", &futureThird, 1u, firstUses, LengthOf(firstUses));
    ASSERT_TRUE(firstResult);
    const Graphics::GpuTaskId first = *firstResult;
    const auto secondResult = AddTask(graph, Name("tests/task_graph/explicit_second"), "Explicit Second", nullptr, 0u, secondUses, LengthOf(secondUses));
    ASSERT_TRUE(secondResult);
    const Graphics::GpuTaskId second = *secondResult;
    const auto thirdResult = AddTask(graph, Name("tests/task_graph/explicit_third"), "Explicit Third", nullptr, 0u, thirdUses, LengthOf(thirdUses));
    ASSERT_TRUE(thirdResult);
    const Graphics::GpuTaskId third = *thirdResult;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    ASSERT_TRUE(third.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
    EXPECT_NE(FindEdge(analysis, third, first), nullptr);
    EXPECT_NE(FindEdge(analysis, second, first), nullptr);
    EXPECT_NE(FindEdge(analysis, second, third), nullptr);
    ASSERT_EQ(analysis.topologicalOrder().size(), 3u);
    EXPECT_EQ(analysis.topologicalOrder()[0], second);
    EXPECT_EQ(analysis.topologicalOrder()[1], third);
    EXPECT_EQ(analysis.topologicalOrder()[2], first);

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(operationScratchArena, graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 3u);

    const Graphics::GpuTaskId topologicalTasks[] = { second, third, first };
    for(usize taskIndex = 0u; taskIndex < LengthOf(topologicalTasks); ++taskIndex){
        const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetIdAt(taskIndex);
        ASSERT_TRUE(packet.valid());
        ASSERT_EQ(compiledPlan.packet(packet).plan->taskCount, 1u);
        ASSERT_NE(compiledPlan.packet(packet).tasks, nullptr);
        EXPECT_EQ(compiledPlan.packet(packet).tasks[0u], topologicalTasks[taskIndex]);
    }

    const Graphics::GpuTaskId lookupOrder[] = { first, second, third, third, second, first };
    for(const Graphics::GpuTaskId task : lookupOrder){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->task, task);
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        ASSERT_NE(compiledTask, nullptr);
        EXPECT_EQ(compiledTask->task, task);
        EXPECT_EQ(compiledPlan.packetForTask(task), compiledTask->packet);
        ASSERT_NE(compiledPlan.packet(compiledTask->packet).tasks, nullptr);
        EXPECT_EQ(compiledPlan.packet(compiledTask->packet).tasks[0u], task);
    }
}

TEST(GpuTaskGraph, RejectsExplicitCyclesAndExportsExternalMetadata){
    Core::Alloc::ScratchArena telemetryScratchArena(s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    u64 graphGeneration = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        graphGeneration = declarations.generation();
    }
    const Graphics::GpuTaskId futureSecond{ .generation = graphGeneration, .index = 1u };
    const auto firstResult = AddTask(graph, Name("tests/task_graph/cycle_first"), "Cycle First", &futureSecond, 1u);
    ASSERT_TRUE(firstResult);
    const Graphics::GpuTaskId first = *firstResult;
    const auto secondResult = AddTask(graph, Name("tests/task_graph/cycle_second"), "Cycle Second", &first, 1u);
    ASSERT_TRUE(secondResult);
    const Graphics::GpuTaskId second = *secondResult;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());

    Graphics::GpuTaskGraphAnalysis cycleAnalysis(testArena.arena);
    EXPECT_FALSE(Analyze(telemetryScratchArena, graph, cycleAnalysis));
    EXPECT_EQ(cycleAnalysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::Cycle);
    ASSERT_GE(cycleAnalysis.cyclePath().size(), 3u);
    EXPECT_EQ(cycleAnalysis.cyclePath().front(), cycleAnalysis.cyclePath().back());
    ASSERT_EQ(cycleAnalysis.cycleEdges().size(), cycleAnalysis.cyclePath().size() - 1u);
    EXPECT_EQ(cycleAnalysis.cycleEdges()[0].hazard, Graphics::GpuTaskHazardType::Explicit);
    EXPECT_EQ(cycleAnalysis.diagnostic().task, cycleAnalysis.cycleEdges()[0].consumer);
    EXPECT_EQ(cycleAnalysis.diagnostic().relatedTask, cycleAnalysis.cycleEdges()[0].producer);

    graph.reset();
    const auto completionResult = graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}.setIdentity(Name("tests/task_graph/external")).setMarkerLabel("Prior Frame"));
    ASSERT_TRUE(completionResult);
    const Graphics::GpuExternalCompletionId completion = *completionResult;
    ASSERT_TRUE(completion.valid());
    const auto taskResult = [&](){
        Graphics::GpuTaskDesc desc;
        desc.setIdentity(Name("tests/task_graph/external_consumer")).setMarkerLabel("External Consumer").setExternalDependencies(&completion, 1u);
        return graph.addTask(desc);
    }();
    ASSERT_TRUE(taskResult);
    const Graphics::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(telemetryScratchArena, graph, analysis));

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingEdges(testArena.arena);
    Telemetry::FrameGraphBuilder builder(telemetryScratchArena, nodes, edges, pendingEdges);
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_TRUE(declarations.appendFrameGraphTelemetry(builder, analysis, telemetryScratchArena));
    }

    {
        const auto gpuAdmissionAt610 = graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}.setIdentity(Name("tests/task_graph/late_external")).setMarkerLabel("Late External"));
        EXPECT_TRUE(gpuAdmissionAt610);
        if(gpuAdmissionAt610)
            EXPECT_TRUE(gpuAdmissionAt610->valid());
    }
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_FALSE(declarations.appendFrameGraphTelemetry(builder, analysis, telemetryScratchArena));
    }
}

TEST(GpuTaskGraph, RejectsDeepExplicitCyclesWithoutCallStackGrowth){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    constexpr usize s_TaskCount = 8192u;

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    u64 graphGeneration = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        graphGeneration = declarations.generation();
    }
    const Name taskBaseName("tests/task_graph/deep_cycle_task_");
    char taskIndexBuffer[32u] = {};
    const Graphics::GpuTaskId futureLast{ .generation = graphGeneration, .index = static_cast<u32>(s_TaskCount - 1u), };
    const auto derivedNameAt750Col44 = DeriveName(taskBaseName, FormatDecimal(0u, taskIndexBuffer));
    ASSERT_TRUE(derivedNameAt750Col44);
    const auto firstResult = AddTask(graph, *derivedNameAt750Col44, s_DeepCycleTaskLabel, &futureLast, 1u);
    ASSERT_TRUE(firstResult);
    const Graphics::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());

    Graphics::GpuTaskId previous = first;
    for(usize taskIndex = 1u; taskIndex < s_TaskCount; ++taskIndex){
        const auto derivedNameAt757Col50 = DeriveName(taskBaseName, FormatDecimal(taskIndex, taskIndexBuffer));
        ASSERT_TRUE(derivedNameAt757Col50);
        const auto currentResult = AddTask(graph, *derivedNameAt757Col50, s_DeepCycleTaskLabel, &previous, 1u);
        ASSERT_TRUE(currentResult);
        const Graphics::GpuTaskId current = *currentResult;
        ASSERT_TRUE(current.valid());
        previous = current;
    }

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(operationScratchArena, graph, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::Cycle);
    EXPECT_TRUE(analysis.topologicalOrder().empty());
    ASSERT_EQ(analysis.cyclePath().size(), s_TaskCount + 1u);
    ASSERT_EQ(analysis.cycleEdges().size(), s_TaskCount);
    for(usize taskIndex = 0u; taskIndex < s_TaskCount; ++taskIndex){
        const Graphics::GpuTaskId expectedTask{ .generation = graphGeneration, .index = static_cast<u32>(taskIndex) };
        EXPECT_EQ(analysis.cyclePath()[taskIndex], expectedTask);
        EXPECT_EQ(analysis.cycleEdges()[taskIndex].producer, analysis.cyclePath()[taskIndex]);
        EXPECT_EQ(analysis.cycleEdges()[taskIndex].consumer, analysis.cyclePath()[taskIndex + 1u]);
        EXPECT_EQ(analysis.cycleEdges()[taskIndex].hazard, Graphics::GpuTaskHazardType::Explicit);
        EXPECT_FALSE(analysis.cycleEdges()[taskIndex].resource.valid());
        EXPECT_FALSE(analysis.cycleEdges()[taskIndex].resourceVersion.valid());
    }
    EXPECT_EQ(analysis.cyclePath().back(), first);
    EXPECT_EQ(analysis.diagnostic().task, analysis.cycleEdges()[0u].consumer);
    EXPECT_EQ(analysis.diagnostic().relatedTask, analysis.cycleEdges()[0u].producer);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

