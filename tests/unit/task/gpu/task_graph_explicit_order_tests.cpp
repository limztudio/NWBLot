// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_explicit_order_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_DeepCycleTaskLabel = "Deep Cycle Task";

using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, UsesTheFullExplicitOrderToOrientInferredHazards){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto firstResourceResult = AddHazardDomain(graph, Name("tests/task_graph/explicit_first_resource"), "Explicit First Resource");
    ASSERT_TRUE(firstResourceResult);
    const Core::GpuGraphResourceId firstResource = *firstResourceResult;
    const auto secondResourceResult = AddHazardDomain(graph, Name("tests/task_graph/explicit_second_resource"), "Explicit Second Resource");
    ASSERT_TRUE(secondResourceResult);
    const Core::GpuGraphResourceId secondResource = *secondResourceResult;
    ASSERT_TRUE(firstResource.valid());
    ASSERT_TRUE(secondResource.valid());

    const Array<Core::GpuTaskResourceUse, 1> firstUses = { Core::GpuTaskResourceUse{ .resource = firstResource, .range = {}, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::Write, }, };
    const Array<Core::GpuTaskResourceUse, 2> secondUses = {
        Core::GpuTaskResourceUse{ .resource = firstResource, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, },
        Core::GpuTaskResourceUse{ .resource = secondResource, .range = {}, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::Write, },
    };
    const Array<Core::GpuTaskResourceUse, 1> thirdUses = { Core::GpuTaskResourceUse{ .resource = secondResource, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, }, };

    // Explicit third-to-first ordering reverses declaration order without creating a cycle.
    u64 graphGeneration = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);

        graphGeneration = declarations.generation();
    }
    const Core::GpuTaskId futureThird{ .generation = graphGeneration, .index = 2u };
    const auto firstResult = AddTask(Name("tests/task_graph/explicit_first"), "Explicit First", Span(&futureThird, 1u), Span(firstUses), {}, {}, {}, graph);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    const auto secondResult = AddTask(Name("tests/task_graph/explicit_second"), "Explicit Second", {}, Span(secondUses), {}, {}, {}, graph);
    ASSERT_TRUE(secondResult);
    const Core::GpuTaskId second = *secondResult;
    const auto thirdResult = AddTask(Name("tests/task_graph/explicit_third"), "Explicit Third", {}, Span(thirdUses), {}, {}, {}, graph);
    ASSERT_TRUE(thirdResult);
    const Core::GpuTaskId third = *thirdResult;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    ASSERT_TRUE(third.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    EXPECT_NE(FindEdge(analysis, third, first), nullptr);
    EXPECT_NE(FindEdge(analysis, second, first), nullptr);
    EXPECT_NE(FindEdge(analysis, second, third), nullptr);
    ASSERT_EQ(analysis.topologicalOrder().size(), 3u);
    EXPECT_EQ(analysis.topologicalOrder()[0], second);
    EXPECT_EQ(analysis.topologicalOrder()[1], third);
    EXPECT_EQ(analysis.topologicalOrder()[2], first);

    const Core::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 3u);

    const Array<Core::GpuTaskId, 3> topologicalTasks = { second, third, first };
    for(usize taskIndex = 0u; taskIndex < LengthOf(topologicalTasks); ++taskIndex){
        const Core::GpuSubmissionPacketId packet = compiledPlan.packetIdAt(taskIndex);
        ASSERT_TRUE(packet.valid());
        ASSERT_EQ(compiledPlan.packet(packet).plan->taskCount, 1u);
        ASSERT_NE(compiledPlan.packet(packet).tasks, nullptr);
        EXPECT_EQ(compiledPlan.packet(packet).tasks[0u], topologicalTasks[taskIndex]);
    }

    const Array<Core::GpuTaskId, 6> lookupOrder = { first, second, third, third, second, first };
    for(const Core::GpuTaskId task : lookupOrder){
        const Core::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->task, task);
        const Core::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
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
    Core::GpuTaskGraph graph(testArena.arena);
    u64 graphGeneration = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        graphGeneration = declarations.generation();
    }
    const Core::GpuTaskId futureSecond{ .generation = graphGeneration, .index = 1u };
    const auto firstResult = AddTask(Name("tests/task_graph/cycle_first"), "Cycle First", Span(&futureSecond, 1u), {}, {}, {}, {}, graph);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    const auto secondResult = AddTask(Name("tests/task_graph/cycle_second"), "Cycle Second", Span(&first, 1u), {}, {}, {}, {}, graph);
    ASSERT_TRUE(secondResult);
    const Core::GpuTaskId second = *secondResult;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());

    Core::GpuTaskGraphAnalysis cycleAnalysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, telemetryScratchArena, cycleAnalysis));
    EXPECT_EQ(cycleAnalysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::Cycle);
    ASSERT_GE(cycleAnalysis.cyclePath().size(), 3u);
    EXPECT_EQ(cycleAnalysis.cyclePath().front(), cycleAnalysis.cyclePath().back());
    ASSERT_EQ(cycleAnalysis.cycleEdges().size(), cycleAnalysis.cyclePath().size() - 1u);
    EXPECT_EQ(cycleAnalysis.cycleEdges()[0].hazard, Core::GpuTaskHazardType::Explicit);
    EXPECT_EQ(cycleAnalysis.diagnostic().task, cycleAnalysis.cycleEdges()[0].consumer);
    EXPECT_EQ(cycleAnalysis.diagnostic().relatedTask, cycleAnalysis.cycleEdges()[0].producer);

    graph.reset();
    const auto completionResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name("tests/task_graph/external")).setMarkerLabel("Prior Frame"));
    ASSERT_TRUE(completionResult);
    const Core::GpuExternalCompletionId completion = *completionResult;
    ASSERT_TRUE(completion.valid());
    const auto taskResult = [&](){
        Core::GpuTaskDesc desc;
        desc.setIdentity(Name("tests/task_graph/external_consumer")).setMarkerLabel("External Consumer").setExternalDependencies(Span(&completion, 1u));
        return graph.addTask(desc);
    }();
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, telemetryScratchArena, analysis));

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingEdges(testArena.arena);
    Telemetry::FrameGraphBuilder builder(telemetryScratchArena, nodes, edges, pendingEdges);
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_TRUE(declarations.appendFrameGraphTelemetry(analysis, telemetryScratchArena, builder));
    }

    {
        const auto gpuTaskAdmission = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name("tests/task_graph/late_external")).setMarkerLabel("Late External"));
        EXPECT_TRUE(gpuTaskAdmission);
        if(gpuTaskAdmission)
            EXPECT_TRUE(gpuTaskAdmission->valid());
    }
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_FALSE(declarations.appendFrameGraphTelemetry(analysis, telemetryScratchArena, builder));
    }
}

TEST(GpuTaskGraph, RejectsDeepExplicitCyclesWithoutCallStackGrowth){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    constexpr usize s_TaskCount = 8192u;

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    u64 graphGeneration = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        graphGeneration = declarations.generation();
    }
    const Name taskBaseName("tests/task_graph/deep_cycle_task_");
    Array<char, 32u> taskIndexBuffer = {};
    const Core::GpuTaskId futureLast{ .generation = graphGeneration, .index = static_cast<u32>(s_TaskCount - 1u), };
    const auto derivedNameResult = DeriveName(taskBaseName, FormatDecimal(0u, taskIndexBuffer));
    ASSERT_TRUE(derivedNameResult);
    const auto firstResult = AddTask(*derivedNameResult, s_DeepCycleTaskLabel, Span(&futureLast, 1u), {}, {}, {}, {}, graph);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());

    Core::GpuTaskId previous = first;
    for(usize taskIndex = 1u; taskIndex < s_TaskCount; ++taskIndex){
        const auto secondDerivedNameResult = DeriveName(taskBaseName, FormatDecimal(taskIndex, taskIndexBuffer));
        ASSERT_TRUE(secondDerivedNameResult);
        const auto currentResult = AddTask(*secondDerivedNameResult, s_DeepCycleTaskLabel, Span(&previous, 1u), {}, {}, {}, {}, graph);
        ASSERT_TRUE(currentResult);
        const Core::GpuTaskId current = *currentResult;
        ASSERT_TRUE(current.valid());
        previous = current;
    }

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, operationScratchArena, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::Cycle);
    EXPECT_TRUE(analysis.topologicalOrder().empty());
    ASSERT_EQ(analysis.cyclePath().size(), s_TaskCount + 1u);
    ASSERT_EQ(analysis.cycleEdges().size(), s_TaskCount);
    for(usize taskIndex = 0u; taskIndex < s_TaskCount; ++taskIndex){
        const Core::GpuTaskId expectedTask{ .generation = graphGeneration, .index = static_cast<u32>(taskIndex) };
        EXPECT_EQ(analysis.cyclePath()[taskIndex], expectedTask);
        EXPECT_EQ(analysis.cycleEdges()[taskIndex].producer, analysis.cyclePath()[taskIndex]);
        EXPECT_EQ(analysis.cycleEdges()[taskIndex].consumer, analysis.cyclePath()[taskIndex + 1u]);
        EXPECT_EQ(analysis.cycleEdges()[taskIndex].hazard, Core::GpuTaskHazardType::Explicit);
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

