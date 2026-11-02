// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_scheduling_dag_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, ReducesSchedulingDagWithoutLosingRawDependencyDiagnostics){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto handoffResult = AddHazardDomain(graph, Name("tests/task_graph/transitive_reduction_handoff"), "Transitive Reduction Handoff");
    ASSERT_TRUE(handoffResult);
    const Core::GpuGraphResourceId handoff = *handoffResult;
    ASSERT_TRUE(handoff.valid());

    const Core::GpuTaskCommandRequirements graphicsCommands = GraphicsCommands();
    const Core::GpuTaskCommandRequirements computeCommands = ComputeCommands();
    const Name mergeDomain("tests/task_graph/transitive_reduction_merge_domain");

    Core::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.cost = Core::GpuTaskCostHint::Medium;
    firstScheduling.frontierScoredMergeDomain = mergeDomain;
    const Core::GpuTaskResourceUse firstUse{ .resource = handoff, .range = {}, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::Write, };
    Core::GpuTaskDesc firstDesc;
    firstDesc.setIdentity(Name("tests/task_graph/transitive_reduction_first")).setMarkerLabel("Transitive Reduction First").setScheduling(firstScheduling).setResourceUses(Span(&firstUse, 1u));
    const auto firstResult = graph.addTask(firstDesc, graphicsCommands);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());

    Core::GpuTaskSchedulingHint secondScheduling;
    secondScheduling.cost = Core::GpuTaskCostHint::Tiny;
    secondScheduling.frontierScoredMergeDomain = mergeDomain;
    Core::GpuTaskDesc secondDesc;
    secondDesc.setIdentity(Name("tests/task_graph/transitive_reduction_second")).setMarkerLabel("Transitive Reduction Second").setScheduling(secondScheduling).setDependencies(Span(&first, 1u));
    const auto secondResult = graph.addTask(secondDesc, graphicsCommands);
    ASSERT_TRUE(secondResult);
    const Core::GpuTaskId second = *secondResult;
    ASSERT_TRUE(second.valid());

    const Core::GpuTaskResourceUse thirdUse{ .resource = handoff, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    Core::GpuTaskDesc thirdDesc;
    thirdDesc.setIdentity(Name("tests/task_graph/transitive_reduction_third")).setMarkerLabel("Transitive Reduction Third").setDependencies(Span(&second, 1u)).setResourceUses(Span(&thirdUse, 1u));
    const auto thirdResult = graph.addTask(thirdDesc, computeCommands);
    ASSERT_TRUE(thirdResult);
    const Core::GpuTaskId third = *thirdResult;
    ASSERT_TRUE(third.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    ASSERT_EQ(analysis.edges().size(), 3u);
    EXPECT_EQ(analysis.edges()[0u].producer, first);
    EXPECT_EQ(analysis.edges()[0u].consumer, second);
    EXPECT_EQ(analysis.edges()[1u].producer, second);
    EXPECT_EQ(analysis.edges()[1u].consumer, third);
    EXPECT_EQ(analysis.edges()[2u].producer, first);
    EXPECT_EQ(analysis.edges()[2u].consumer, third);
    ASSERT_EQ(analysis.schedulingEdges().size(), 2u);
    EXPECT_EQ(analysis.schedulingEdges()[0u].producer, first);
    EXPECT_EQ(analysis.schedulingEdges()[0u].consumer, second);
    EXPECT_EQ(analysis.schedulingEdges()[1u].producer, second);
    EXPECT_EQ(analysis.schedulingEdges()[1u].consumer, third);
    ASSERT_EQ(analysis.inferredEdges().size(), 1u);
    EXPECT_TRUE(HasInferredHazard(analysis, first, third, handoff, Core::GpuTaskHazardType::ReadAfterWrite));

    const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphAnalysis separateAnalysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments separateAssignments(testArena.arena);
    Core::GpuCompiledGraph separateGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(separateAnalysis, separateAssignments, separateGraph, operationScratchArena, graph, topology));
    {
        const Core::GpuCompiledGraph::ReadView compiledPlan(separateGraph);
        ASSERT_EQ(compiledPlan.packetCount(), 3u);
        const Core::GpuSubmissionPacketId separateFirstPacket = compiledPlan.packetForTask(first);
        const Core::GpuSubmissionPacketId separateSecondPacket = compiledPlan.packetForTask(second);
        const Core::GpuSubmissionPacketId separateThirdPacket = compiledPlan.packetForTask(third);
        ASSERT_TRUE(separateFirstPacket.valid());
        ASSERT_TRUE(separateSecondPacket.valid());
        ASSERT_TRUE(separateThirdPacket.valid());
        EXPECT_EQ(compiledPlan.packet(separateFirstPacket).plan->dependencyCount, 0u);
        ASSERT_EQ(compiledPlan.packet(separateSecondPacket).plan->dependencyCount, 1u);
        EXPECT_EQ(compiledPlan.packet(separateSecondPacket).dependencies[0u].producer, separateFirstPacket);
        ASSERT_EQ(compiledPlan.packet(separateThirdPacket).plan->dependencyCount, 1u);
        EXPECT_EQ(compiledPlan.packet(separateThirdPacket).dependencies[0u].producer, separateSecondPacket);
    }

    Core::GpuTaskGraphCompileOptions scoredOptions;
    scoredOptions.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierScored;
    Core::GpuTaskGraphAnalysis scoredAnalysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments scoredAssignments(testArena.arena);
    Core::GpuCompiledGraph scoredGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(scoredAnalysis, scoredAssignments, scoredGraph, operationScratchArena, graph, topology, scoredOptions));
    const Core::GpuCompiledGraph::ReadView compiledPlan(scoredGraph);
    ASSERT_EQ(compiledPlan.packetCount(), 2u);
    const Core::GpuSubmissionPacketId scoredFirstPacket = compiledPlan.packetForTask(first);
    const Core::GpuSubmissionPacketId scoredSecondPacket = compiledPlan.packetForTask(second);
    const Core::GpuSubmissionPacketId scoredThirdPacket = compiledPlan.packetForTask(third);
    ASSERT_TRUE(scoredFirstPacket.valid());
    ASSERT_TRUE(scoredThirdPacket.valid());
    EXPECT_EQ(scoredFirstPacket, scoredSecondPacket);
    EXPECT_NE(scoredFirstPacket, scoredThirdPacket);
    EXPECT_EQ(compiledPlan.packetizationDecisionForTask(second), Core::GpuTaskPacketizationDecision::MergedFrontierScored);
    ASSERT_EQ(compiledPlan.packet(scoredThirdPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(scoredThirdPacket).dependencies[0u].producer, scoredFirstPacket);
}


TEST(GpuTaskGraph, SchedulingAdjacencyRejectsStaleIdsAndClearsOnReset){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto firstResult = AddTask(Name("tests/task_graph/scheduling_adjacency_first"), "Scheduling Adjacency First", {}, {}, {}, {}, {}, graph);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    const auto secondResult = AddTask(Name("tests/task_graph/scheduling_adjacency_second"), "Scheduling Adjacency Second", Span(&first, 1u), {}, {}, {}, {}, graph);
    ASSERT_TRUE(secondResult);
    const Core::GpuTaskId second = *secondResult;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    const Core::GpuTaskGraphSchedulingTaskIndexView consumers = analysis.schedulingConsumers(first);
    const Core::GpuTaskGraphSchedulingTaskIndexView producers = analysis.schedulingProducers(second);
    ASSERT_EQ(consumers.taskCount, 1u);
    ASSERT_EQ(producers.taskCount, 1u);
    EXPECT_EQ(consumers[0u], second.index);
    EXPECT_EQ(producers[0u], first.index);

    u32 declaredTaskCount = 0u;
    u64 graphGeneration = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);

        declaredTaskCount = static_cast<u32>(declarations.taskCount());
        graphGeneration = declarations.generation();
    }
    const Core::GpuTaskId outOfRangeTask{ .generation = graphGeneration, .index = static_cast<u32>(declaredTaskCount), };
    const Core::GpuTaskId staleTask{ .generation = graphGeneration + 1u, .index = static_cast<u32>(first.index) };
    Core::GpuTaskGraph foreignGraph(testArena.arena);
    const auto foreignTaskResult = AddTask(Name("tests/task_graph/scheduling_adjacency_foreign"), "Scheduling Adjacency Foreign", {}, {}, {}, {}, {}, foreignGraph);
    ASSERT_TRUE(foreignTaskResult);
    const Core::GpuTaskId foreignTask = *foreignTaskResult;
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
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_FALSE(analysis.validFor(declarations));
    }
    EXPECT_TRUE(analysis.schedulingConsumers(first).empty());
    EXPECT_TRUE(analysis.schedulingProducers(second).empty());
}

TEST(GpuTaskGraph, PackedSchedulingReachabilityPreservesStrictClosureAcrossWordBoundaries){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    constexpr usize s_MaxTaskCount = 129u;
    const Array<usize, 7u> taskCounts = { 63u, 64u, 65u, 66u, 127u, 128u, 129u };

    for(const usize taskCount : taskCounts){
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        Array<Core::GpuTaskId, s_MaxTaskCount> tasks = {};
        const Name taskBaseName("tests/task_graph/packed_reachability_task_");
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
            const Core::GpuTaskId* const dependency = taskIndex >= 2u ? &tasks[taskIndex - 2u] : nullptr;
            const auto derivedNameResult = IndexedName(taskBaseName, taskIndex);
            ASSERT_TRUE(derivedNameResult);
            const auto taskAdmission = AddTask(*derivedNameResult, "Packed Reachability Task", dependency ? Span(dependency, 1u) : Span<const Core::GpuTaskId>{}, {}, {}, {}, {}, graph);
            ASSERT_TRUE(taskAdmission);
            tasks[taskIndex] = *taskAdmission;
            ASSERT_TRUE(tasks[taskIndex].valid());
            EXPECT_EQ(tasks[taskIndex].index, taskIndex);
        }

        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
        Core::Alloc::ScratchArena reachabilityScratchArena(Name("tests/task/gpu/packed_reachability_scratch"));
        Core::GpuTaskGraphCompilerDetail::GpuTaskSchedulingReachability reachability(reachabilityScratchArena);
        u64 graphGeneration = 0u;
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(graph);

            ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(declarations, analysis, reachability));
            graphGeneration = declarations.generation();
        }

        const usize lastEvenTask = (taskCount & 1u) != 0u ? taskCount - 1u : taskCount - 2u;
        const usize lastOddTask = (taskCount & 1u) != 0u ? taskCount - 2u : taskCount - 1u;
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
        EXPECT_TRUE(reachability.transitivelyIndependent(tasks[taskCount - 2u], tasks[boundaryConsumer]));
        if(taskCount >= 127u){
            const usize secondWordProducer = taskCount - 63u;
            EXPECT_TRUE(reachability.reaches(tasks[secondWordProducer], tasks[boundaryConsumer]));
            EXPECT_FALSE(reachability.reaches(tasks[boundaryConsumer], tasks[secondWordProducer]));
            EXPECT_TRUE(reachability.transitivelyIndependent(tasks[secondWordProducer - 1u], tasks[boundaryConsumer]));
        }

        const Core::GpuTaskId staleTask{ .generation = graphGeneration + 1u, .index = static_cast<u32>(tasks[0u].index) };
        const Core::GpuTaskId outOfRangeTask{ .generation = graphGeneration, .index = static_cast<u32>(taskCount) };
        EXPECT_FALSE(reachability.reaches(tasks[0u], tasks[0u]));
        EXPECT_FALSE(reachability.transitivelyIndependent(tasks[0u], tasks[0u]));
        EXPECT_FALSE(reachability.reaches(staleTask, tasks[lastEvenTask]));
        EXPECT_FALSE(reachability.transitivelyIndependent(staleTask, tasks[lastOddTask]));
        EXPECT_FALSE(reachability.reaches(outOfRangeTask, tasks[lastEvenTask]));
        EXPECT_FALSE(reachability.transitivelyIndependent(outOfRangeTask, tasks[lastOddTask]));

        if(taskCount == s_MaxTaskCount){
            Core::GpuTaskGraph emptyGraph(testArena.arena);
            Core::GpuTaskGraphAnalysis emptyAnalysis(testArena.arena);
            ASSERT_TRUE(Analyze(emptyGraph, operationScratchArena, emptyAnalysis));
            const Core::GpuTaskGraph::DeclarationReadView declarations(emptyGraph);

            ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(declarations, emptyAnalysis, reachability));
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

