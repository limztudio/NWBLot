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


namespace __hidden_task_graph_scheduling_dag_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, ReducesSchedulingDagWithoutLosingRawDependencyDiagnostics){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto handoffResult = AddHazardDomain(graph, Name("tests/task_graph/transitive_reduction_handoff"), "Transitive Reduction Handoff");
    ASSERT_TRUE(handoffResult);
    const Graphics::GpuGraphResourceId handoff = *handoffResult;
    ASSERT_TRUE(handoff.valid());

    const Graphics::GpuTaskCommandRequirements graphicsCommands = GraphicsCommands();
    const Graphics::GpuTaskCommandRequirements computeCommands = ComputeCommands();
    const Name mergeDomain("tests/task_graph/transitive_reduction_merge_domain");

    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    firstScheduling.frontierScoredMergeDomain = mergeDomain;
    const Graphics::GpuTaskResourceUse firstUse{ .resource = handoff, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write, };
    Graphics::GpuTaskDesc firstDesc;
    firstDesc.setIdentity(Name("tests/task_graph/transitive_reduction_first")).setMarkerLabel("Transitive Reduction First").setScheduling(firstScheduling).setResourceUses(&firstUse, 1u);
    const auto firstResult = graph.addTask(firstDesc, graphicsCommands);
    ASSERT_TRUE(firstResult);
    const Graphics::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());

    Graphics::GpuTaskSchedulingHint secondScheduling;
    secondScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    secondScheduling.frontierScoredMergeDomain = mergeDomain;
    Graphics::GpuTaskDesc secondDesc;
    secondDesc.setIdentity(Name("tests/task_graph/transitive_reduction_second")).setMarkerLabel("Transitive Reduction Second").setScheduling(secondScheduling).setDependencies(&first, 1u);
    const auto secondResult = graph.addTask(secondDesc, graphicsCommands);
    ASSERT_TRUE(secondResult);
    const Graphics::GpuTaskId second = *secondResult;
    ASSERT_TRUE(second.valid());

    const Graphics::GpuTaskResourceUse thirdUse{ .resource = handoff, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read, };
    Graphics::GpuTaskDesc thirdDesc;
    thirdDesc.setIdentity(Name("tests/task_graph/transitive_reduction_third")).setMarkerLabel("Transitive Reduction Third").setDependencies(&second, 1u).setResourceUses(&thirdUse, 1u);
    const auto thirdResult = graph.addTask(thirdDesc, computeCommands);
    ASSERT_TRUE(thirdResult);
    const Graphics::GpuTaskId third = *thirdResult;
    ASSERT_TRUE(third.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
    ASSERT_EQ(analysis.edges().size(), 3u);
    EXPECT_EQ(analysis.edges()[0u].producer, first);
    EXPECT_EQ(analysis.edges()[0u].consumer, second);
    EXPECT_EQ(analysis.edges()[1u].producer, second);
    EXPECT_EQ(analysis.edges()[1u].consumer, third);
    EXPECT_EQ(analysis.edges()[s_ThirdElementIndex].producer, first);
    EXPECT_EQ(analysis.edges()[s_ThirdElementIndex].consumer, third);
    ASSERT_EQ(analysis.schedulingEdges().size(), s_ExpectedDualCount);
    EXPECT_EQ(analysis.schedulingEdges()[0u].producer, first);
    EXPECT_EQ(analysis.schedulingEdges()[0u].consumer, second);
    EXPECT_EQ(analysis.schedulingEdges()[1u].producer, second);
    EXPECT_EQ(analysis.schedulingEdges()[1u].consumer, third);
    ASSERT_EQ(analysis.inferredEdges().size(), 1u);
    EXPECT_TRUE(HasInferredHazard(analysis, first, third, handoff, Graphics::GpuTaskHazardType::ReadAfterWrite));

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues), };
    Graphics::GpuTaskGraphAnalysis separateAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments separateAssignments(testArena.arena);
    Graphics::GpuCompiledGraph separateGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(operationScratchArena, graph, separateAnalysis, topology, separateAssignments, separateGraph));
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(separateGraph);
        ASSERT_EQ(compiledPlan.packetCount(), 3u);
        const Graphics::GpuSubmissionPacketId separateFirstPacket = compiledPlan.packetForTask(first);
        const Graphics::GpuSubmissionPacketId separateSecondPacket = compiledPlan.packetForTask(second);
        const Graphics::GpuSubmissionPacketId separateThirdPacket = compiledPlan.packetForTask(third);
        ASSERT_TRUE(separateFirstPacket.valid());
        ASSERT_TRUE(separateSecondPacket.valid());
        ASSERT_TRUE(separateThirdPacket.valid());
        EXPECT_EQ(compiledPlan.packet(separateFirstPacket).plan->dependencyCount, 0u);
        ASSERT_EQ(compiledPlan.packet(separateSecondPacket).plan->dependencyCount, 1u);
        EXPECT_EQ(compiledPlan.packet(separateSecondPacket).dependencies[0u].producer, separateFirstPacket);
        ASSERT_EQ(compiledPlan.packet(separateThirdPacket).plan->dependencyCount, 1u);
        EXPECT_EQ(compiledPlan.packet(separateThirdPacket).dependencies[0u].producer, separateSecondPacket);
    }

    Graphics::GpuTaskGraphCompileOptions scoredOptions;
    scoredOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierScored;
    Graphics::GpuTaskGraphAnalysis scoredAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments scoredAssignments(testArena.arena);
    Graphics::GpuCompiledGraph scoredGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(operationScratchArena, graph, scoredAnalysis, topology, scoredAssignments, scoredGraph, scoredOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(scoredGraph);
    ASSERT_EQ(compiledPlan.packetCount(), s_ExpectedDualCount);
    const Graphics::GpuSubmissionPacketId scoredFirstPacket = compiledPlan.packetForTask(first);
    const Graphics::GpuSubmissionPacketId scoredSecondPacket = compiledPlan.packetForTask(second);
    const Graphics::GpuSubmissionPacketId scoredThirdPacket = compiledPlan.packetForTask(third);
    ASSERT_TRUE(scoredFirstPacket.valid());
    ASSERT_TRUE(scoredThirdPacket.valid());
    EXPECT_EQ(scoredFirstPacket, scoredSecondPacket);
    EXPECT_NE(scoredFirstPacket, scoredThirdPacket);
    EXPECT_EQ(compiledPlan.packetizationDecisionForTask(second), Graphics::GpuTaskPacketizationDecision::MergedFrontierScored);
    ASSERT_EQ(compiledPlan.packet(scoredThirdPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(scoredThirdPacket).dependencies[0u].producer, scoredFirstPacket);
}


TEST(GpuTaskGraph, SchedulingAdjacencyRejectsStaleIdsAndClearsOnReset){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto firstResult = AddTask(graph, Name("tests/task_graph/scheduling_adjacency_first"), "Scheduling Adjacency First");
    ASSERT_TRUE(firstResult);
    const Graphics::GpuTaskId first = *firstResult;
    const auto secondResult = AddTask(graph, Name("tests/task_graph/scheduling_adjacency_second"), "Scheduling Adjacency Second", &first, 1u);
    ASSERT_TRUE(secondResult);
    const Graphics::GpuTaskId second = *secondResult;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
    const Graphics::GpuTaskGraphSchedulingTaskIndexView consumers = analysis.schedulingConsumers(first);
    const Graphics::GpuTaskGraphSchedulingTaskIndexView producers = analysis.schedulingProducers(second);
    ASSERT_EQ(consumers.taskCount, 1u);
    ASSERT_EQ(producers.taskCount, 1u);
    EXPECT_EQ(consumers[0u], second.index);
    EXPECT_EQ(producers[0u], first.index);

    u32 declaredTaskCount = 0u;
    u64 graphGeneration = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        declaredTaskCount = static_cast<u32>(declarations.taskCount());
        graphGeneration = declarations.generation();
    }
    const Graphics::GpuTaskId outOfRangeTask{ .generation = graphGeneration, .index = static_cast<u32>(declaredTaskCount), };
    const Graphics::GpuTaskId staleTask{ .generation = graphGeneration + 1u, .index = static_cast<u32>(first.index) };
    Graphics::GpuTaskGraph foreignGraph(testArena.arena);
    const auto foreignTaskResult = AddTask(foreignGraph, Name("tests/task_graph/scheduling_adjacency_foreign"), "Scheduling Adjacency Foreign");
    ASSERT_TRUE(foreignTaskResult);
    const Graphics::GpuTaskId foreignTask = *foreignTaskResult;
    ASSERT_TRUE(foreignTask.valid());
    EXPECT_TRUE(analysis.schedulingConsumers({}).empty());
    EXPECT_TRUE(analysis.schedulingConsumers(outOfRangeTask).empty());
    EXPECT_TRUE(analysis.schedulingConsumers(staleTask).empty());
    EXPECT_TRUE(analysis.schedulingConsumers(foreignTask).empty());
    EXPECT_TRUE(analysis.schedulingProducers({}).empty());
    EXPECT_TRUE(analysis.schedulingProducers(outOfRangeTask).empty());
    EXPECT_TRUE(analysis.schedulingProducers(staleTask).empty());
    EXPECT_TRUE(analysis.schedulingProducers(foreignTask).empty());

    analysis.reset();
    EXPECT_FALSE(analysis.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_FALSE(analysis.validFor(declarations));
    }
    EXPECT_TRUE(analysis.schedulingConsumers(first).empty());
    EXPECT_TRUE(analysis.schedulingProducers(second).empty());
}

TEST(GpuTaskGraph, PackedSchedulingReachabilityPreservesStrictClosureAcrossWordBoundaries){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    constexpr usize s_MaxTaskCount = 129u;
    const usize taskCounts[] = { 63u, 64u, 65u, 66u, 127u, 128u, 129u };

    for(const usize taskCount : taskCounts){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::GpuTaskId tasks[s_MaxTaskCount] = {};
        const Name taskBaseName("tests/task_graph/packed_reachability_task_");
        char taskIndexBuffer[32u] = {};
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
            const Graphics::GpuTaskId* const dependency = taskIndex >= s_ExpectedDualCount ? &tasks[taskIndex - s_ExpectedDualCount] : nullptr;
            const auto derivedNameAt488Col58 = DeriveName(taskBaseName, FormatDecimal(taskIndex, taskIndexBuffer));
            ASSERT_TRUE(derivedNameAt488Col58);
            const auto gpuAdmissionAt403 = AddTask(graph, *derivedNameAt488Col58, "Packed Reachability Task", dependency, dependency ? 1u : 0u);
            ASSERT_TRUE(gpuAdmissionAt403);
            tasks[taskIndex] = *gpuAdmissionAt403;
            ASSERT_TRUE(tasks[taskIndex].valid());
            EXPECT_EQ(tasks[taskIndex].index, taskIndex);
        }

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
        Core::Alloc::ScratchArena reachabilityScratchArena(Name("tests/task/gpu/packed_reachability_scratch"));
        Graphics::GpuTaskGraphCompilerDetail::GpuTaskSchedulingReachability reachability(reachabilityScratchArena);
        u64 graphGeneration = 0u;
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

            ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(declarations, analysis, reachability));
            graphGeneration = declarations.generation();
        }

        const usize lastEvenTask = (taskCount & 1u) != 0u ? taskCount - 1u : taskCount - s_ExpectedDualCount;
        const usize lastOddTask = (taskCount & 1u) != 0u ? taskCount - s_ExpectedDualCount : taskCount - 1u;
        EXPECT_TRUE(reachability.reaches(tasks[0u], tasks[lastEvenTask]));
        EXPECT_FALSE(reachability.reaches(tasks[lastEvenTask], tasks[0u]));
        EXPECT_TRUE(reachability.reaches(tasks[1u], tasks[lastOddTask]));
        EXPECT_FALSE(reachability.reaches(tasks[lastOddTask], tasks[1u]));
        EXPECT_FALSE(reachability.transitivelyIndependent(tasks[0u], tasks[lastEvenTask]));
        EXPECT_FALSE(reachability.transitivelyIndependent(tasks[1u], tasks[lastOddTask]));
        EXPECT_TRUE(reachability.transitivelyIndependent(tasks[0u], tasks[lastOddTask]));
        EXPECT_TRUE(reachability.transitivelyIndependent(tasks[1u], tasks[lastEvenTask]));

        const usize boundaryProducer = taskCount - 3u;
        const usize boundaryConsumer = taskCount - 1u;
        EXPECT_TRUE(reachability.reaches(tasks[boundaryProducer], tasks[boundaryConsumer]));
        EXPECT_FALSE(reachability.reaches(tasks[boundaryConsumer], tasks[boundaryProducer]));
        EXPECT_TRUE(reachability.transitivelyIndependent(tasks[taskCount - s_ExpectedDualCount], tasks[boundaryConsumer]));
        if(taskCount >= 127u){
            const usize secondWordProducer = taskCount - 63u;
            EXPECT_TRUE(reachability.reaches(tasks[secondWordProducer], tasks[boundaryConsumer]));
            EXPECT_FALSE(reachability.reaches(tasks[boundaryConsumer], tasks[secondWordProducer]));
            EXPECT_TRUE(reachability.transitivelyIndependent(tasks[secondWordProducer - 1u], tasks[boundaryConsumer]));
        }

        const Graphics::GpuTaskId staleTask{ .generation = graphGeneration + 1u, .index = static_cast<u32>(tasks[0u].index) };
        const Graphics::GpuTaskId outOfRangeTask{ .generation = graphGeneration, .index = static_cast<u32>(taskCount) };
        EXPECT_FALSE(reachability.reaches(tasks[0u], tasks[0u]));
        EXPECT_FALSE(reachability.transitivelyIndependent(tasks[0u], tasks[0u]));
        EXPECT_FALSE(reachability.reaches(staleTask, tasks[lastEvenTask]));
        EXPECT_FALSE(reachability.transitivelyIndependent(staleTask, tasks[lastOddTask]));
        EXPECT_FALSE(reachability.reaches(outOfRangeTask, tasks[lastEvenTask]));
        EXPECT_FALSE(reachability.transitivelyIndependent(outOfRangeTask, tasks[lastOddTask]));

        if(taskCount == s_MaxTaskCount){
            Graphics::GpuTaskGraph emptyGraph(testArena.arena);
            Graphics::GpuTaskGraphAnalysis emptyAnalysis(testArena.arena);
            ASSERT_TRUE(Analyze(operationScratchArena, emptyGraph, emptyAnalysis));
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(emptyGraph);

            ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(declarations, emptyAnalysis, reachability));
            EXPECT_FALSE(reachability.reaches(tasks[0u], tasks[lastEvenTask]));
            EXPECT_FALSE(reachability.transitivelyIndependent(tasks[0u], tasks[lastOddTask]));
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

