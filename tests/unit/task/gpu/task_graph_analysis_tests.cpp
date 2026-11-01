// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_task_graph_read_views.h"

#include <tests/common/test_context.h>

#include <core/task/gpu/compiler_internal.h>

#include <global/text_utils.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_analysis_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct TaskGraphAnalysisTestsTag>;

inline constexpr Name s_AnalysisScratchArena("tests/task/gpu/analysis_scratch");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<Core::GpuTaskId> AddTask(
    Core::GpuTaskGraph& graph,
    const usize taskIndex,
    const Span<const Core::GpuTaskId> dependencies = {},
    const Span<const Core::GpuTaskResourceUse> resourceUses = {}
){
    Array<char, 32u> taskIndexBuffer = {};
    Core::GpuTaskDesc desc;
    const auto resourceNameResult = DeriveName(Name("tests/task_graph_analysis/task_"), FormatDecimal(taskIndex, taskIndexBuffer));
    if(!resourceNameResult)
        return MakeUnexpected(Failure{});
    desc.setIdentity(*resourceNameResult).setMarkerLabel("Analysis Test Task").setDependencies(dependencies).setResourceUses(resourceUses);
    return graph.addTask(desc);
}

static void ExpectEdgeEqual(const Core::GpuTaskDependencyEdge& expected, const Core::GpuTaskDependencyEdge& actual){
    EXPECT_EQ(actual.producer, expected.producer);
    EXPECT_EQ(actual.consumer, expected.consumer);
    EXPECT_EQ(actual.resource, expected.resource);
    EXPECT_EQ(actual.resourceVersion, expected.resourceVersion);
    EXPECT_EQ(actual.hazard, expected.hazard);
}

static void ExpectDagAnalysis(const Core::GpuTaskGraph& graph, const Span<const Core::GpuTaskDependencyEdge> expectedEdges, Core::Alloc::ScratchArena& scratchArena, Core::GpuTaskGraphAnalysis& analysis){
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    ASSERT_TRUE(declarations.valid());
    const Core::GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.analyze(declarations, scratchArena, analysis));
    ASSERT_TRUE(analysis.validFor(declarations));
    ASSERT_EQ(analysis.edges().size(), expectedEdges.size());
    for(usize edgeIndex = 0u; edgeIndex < expectedEdges.size(); ++edgeIndex)
        ExpectEdgeEqual(expectedEdges[edgeIndex], analysis.edges()[edgeIndex]);

    const usize taskCount = declarations.taskCount();
    Vector<u8, Core::Alloc::ScratchArena> adjacency(taskCount * taskCount, 0u, scratchArena);
    Vector<u8, Core::Alloc::ScratchArena> reachable(taskCount * taskCount, 0u, scratchArena);
    Vector<u32, Core::Alloc::ScratchArena> indegrees(taskCount, 0u, scratchArena);
    Vector<u8, Core::Alloc::ScratchArena> emitted(taskCount, 0u, scratchArena);
    for(const Core::GpuTaskDependencyEdge& edge : expectedEdges){
        const usize matrixIndex = edge.producer.index * taskCount + edge.consumer.index;
        ASSERT_EQ(adjacency[matrixIndex], 0u);
        adjacency[matrixIndex] = 1u;
        reachable[matrixIndex] = 1u;
        ++indegrees[edge.consumer.index];
    }

    ASSERT_EQ(analysis.topologicalOrder().size(), taskCount);
    for(usize orderIndex = 0u; orderIndex < taskCount; ++orderIndex){
        usize nextTask = 0u;
        while(nextTask < taskCount && (emitted[nextTask] || indegrees[nextTask] != 0u))
            ++nextTask;
        ASSERT_LT(nextTask, taskCount);
        EXPECT_EQ(analysis.topologicalOrder()[orderIndex], declarations.taskAt(nextTask).id);
        emitted[nextTask] = 1u;
        for(usize consumer = 0u; consumer < taskCount; ++consumer){
            if(adjacency[nextTask * taskCount + consumer])
                --indegrees[consumer];
        }
    }

    // A plain Floyd-Warshall matrix gives an independent reference for strict reachability and reduction.
    for(usize intermediate = 0u; intermediate < taskCount; ++intermediate){
        for(usize producer = 0u; producer < taskCount; ++producer){
            if(!reachable[producer * taskCount + intermediate])
                continue;
            for(usize consumer = 0u; consumer < taskCount; ++consumer){
                if(reachable[intermediate * taskCount + consumer])
                    reachable[producer * taskCount + consumer] = 1u;
            }
        }
    }

    Core::GpuTaskGraphCompilerDetail::GpuTaskSchedulingReachability schedulingReachability(scratchArena);
    ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(declarations, analysis, schedulingReachability));
    for(usize source = 0u; source < taskCount; ++source){
        for(usize destination = 0u; destination < taskCount; ++destination){
            const Core::GpuTaskId sourceTask = declarations.taskAt(source).id;
            const Core::GpuTaskId destinationTask = declarations.taskAt(destination).id;
            EXPECT_EQ(schedulingReachability.reaches(sourceTask, destinationTask), reachable[source * taskCount + destination] != 0u);
            EXPECT_EQ(schedulingReachability.transitivelyIndependent(sourceTask, destinationTask), source != destination && !reachable[source * taskCount + destination] && !reachable[destination * taskCount + source]);
        }
    }

    Vector<usize, Core::Alloc::ScratchArena> reducedEdgeIndices(scratchArena);
    reducedEdgeIndices.reserve(expectedEdges.size());
    for(usize edgeIndex = 0u; edgeIndex < expectedEdges.size(); ++edgeIndex){
        const Core::GpuTaskDependencyEdge& edge = expectedEdges[edgeIndex];
        b8 redundant = false;
        for(usize intermediate = 0u; intermediate < taskCount; ++intermediate){
            if(adjacency[edge.producer.index * taskCount + intermediate] && reachable[intermediate * taskCount + edge.consumer.index]){
                redundant = true;
                break;
            }
        }
        if(!redundant)
            reducedEdgeIndices.push_back(edgeIndex);
    }
    ASSERT_EQ(analysis.schedulingEdges().size(), reducedEdgeIndices.size());
    for(usize edgeIndex = 0u; edgeIndex < reducedEdgeIndices.size(); ++edgeIndex)
        ExpectEdgeEqual(expectedEdges[reducedEdgeIndices[edgeIndex]], analysis.schedulingEdges()[edgeIndex]);

    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        const Core::GpuTaskId task = declarations.taskAt(taskIndex).id;
        const Core::GpuTaskGraphSchedulingTaskIndexView consumers = analysis.schedulingConsumers(task);
        const Core::GpuTaskGraphSchedulingTaskIndexView producers = analysis.schedulingProducers(task);
        usize consumerOffset = 0u;
        usize producerOffset = 0u;
        for(const usize edgeIndex : reducedEdgeIndices){
            const Core::GpuTaskDependencyEdge& edge = expectedEdges[edgeIndex];
            if(edge.producer == task){
                ASSERT_LT(consumerOffset, consumers.taskCount);
                EXPECT_EQ(consumers[consumerOffset], edge.consumer.index);
                ++consumerOffset;
            }
            if(edge.consumer == task){
                ASSERT_LT(producerOffset, producers.taskCount);
                EXPECT_EQ(producers[producerOffset], edge.producer.index);
                ++producerOffset;
            }
        }
        EXPECT_EQ(consumers.taskCount, consumerOffset);
        EXPECT_EQ(producers.taskCount, producerOffset);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void DeclareShortcutChain(Core::GpuTaskGraph& graph, const usize taskCount, const b8 reversed){
    u64 generation = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView view(graph);
        generation = view.generation();
    }
    for(usize declarationIndex = 0u; declarationIndex < taskCount; ++declarationIndex){
        const usize chainIndex = reversed ? taskCount - declarationIndex - 1u : declarationIndex;
        Array<Core::GpuTaskId, 2u> dependencies = {};
        const usize dependencyCount = Min(chainIndex, LengthOf(dependencies));
        for(usize offset = 0u; offset < dependencyCount; ++offset){
            const usize sourceIndex = chainIndex - offset - 1u;
            dependencies[offset] = { .generation = generation, .index = static_cast<u32>(reversed ? taskCount - sourceIndex - 1u : sourceIndex), };
        }
        {
            const auto gpuTaskAdmission = AddTask(graph, declarationIndex, Span(dependencies.data(), dependencyCount));
            ASSERT_TRUE(gpuTaskAdmission);
            ASSERT_TRUE(gpuTaskAdmission->valid());
        }
    }
}

static void ExpectReducedShortcutChain(const Core::GpuTaskGraphAnalysis& analysis, const usize taskCount, const b8 reversed){
    ASSERT_EQ(analysis.edges().size(), taskCount * 2u - 3u);
    ASSERT_EQ(analysis.schedulingEdges().size(), taskCount - 1u);
    for(usize edgeIndex = 0u; edgeIndex + 1u < taskCount; ++edgeIndex){
        const auto& edge = analysis.schedulingEdges()[edgeIndex];
        EXPECT_EQ(edge.producer.index, reversed ? edgeIndex + 1u : edgeIndex);
        EXPECT_EQ(edge.consumer.index, reversed ? edgeIndex : edgeIndex + 1u);
    }
}


namespace ReadyOrderShape{
    enum Enum : u8{
        ConsumerFirstPairs,
        ProducerFirstPairs,
        ReverseChain,
    };
};

static void DeclareReadyOrderGraph(Core::GpuTaskGraph& graph, const usize taskCount, const ReadyOrderShape::Enum shape){
    u64 generation = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView view(graph);
        generation = view.generation();
    }
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        const usize half = taskCount / 2u;
        const b8 hasDependency = shape == ReadyOrderShape::ReverseChain ? taskIndex + 1u < taskCount : shape == ReadyOrderShape::ConsumerFirstPairs ? taskIndex < half : taskIndex >= half;
        const usize producerIndex = !hasDependency ? 0u : shape == ReadyOrderShape::ReverseChain ? taskIndex + 1u : shape == ReadyOrderShape::ConsumerFirstPairs ? taskIndex + half : taskIndex - half;
        const Core::GpuTaskId dependency{ .generation = generation, .index = static_cast<u32>(producerIndex) };
        {
            const auto secondGpuTaskAdmission = AddTask(graph, taskIndex, hasDependency ? Span(&dependency, 1u) : Span<const Core::GpuTaskId>{});
            ASSERT_TRUE(secondGpuTaskAdmission);
            ASSERT_TRUE(secondGpuTaskAdmission->valid());
        }
    }
}

static void ExpectReadyOrder(const Core::GpuTaskGraphAnalysis& analysis, const usize taskCount, const ReadyOrderShape::Enum shape){
    ASSERT_EQ(analysis.topologicalOrder().size(), taskCount);
    for(usize index = 0u; index < taskCount; ++index){
        const usize expected = shape == ReadyOrderShape::ReverseChain ? taskCount - index - 1u : shape == ReadyOrderShape::ConsumerFirstPairs ? (index % 2u == 0u ? taskCount / 2u + index / 2u : index / 2u) : index;
        EXPECT_EQ(analysis.topologicalOrder()[index].index, expected);
    }
}


TEST(GpuTaskGraphAnalysis, PreservesLowestReadyIdAcrossRepeatedBackwardReleasesAndPartialIndexWords){
    constexpr Array<usize, 2> s_Counts = { 130u, 8194u };
    constexpr Array<ReadyOrderShape::Enum, 3> s_Shapes = { ReadyOrderShape::ConsumerFirstPairs, ReadyOrderShape::ProducerFirstPairs, ReadyOrderShape::ReverseChain, };
    for(const usize count : s_Counts){
        for(const auto shape : s_Shapes){
            SCOPED_TRACE(count);
            SCOPED_TRACE(shape);
            TestArena testArena;
            Core::GpuTaskGraph graph(testArena.arena);
            ASSERT_NO_FATAL_FAILURE(DeclareReadyOrderGraph(graph, count, shape));
            Core::GpuTaskGraphAnalysis analysis(testArena.arena);
            const Core::GpuTaskGraph::DeclarationReadView view(graph);
            Core::Alloc::ScratchArena scratch(s_AnalysisScratchArena);
            ASSERT_TRUE(Core::GpuTaskGraphCompiler().analyze(view, scratch, analysis));
            ASSERT_NO_FATAL_FAILURE(ExpectReadyOrder(analysis, count, shape));
        }
    }
}

TEST(GpuTaskGraphAnalysis, RetainsCycleDiagnosticsAfterBackwardReleasePrefixExhaustsReadyWork){
    constexpr usize s_PrefixCount = 130u;
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    ASSERT_NO_FATAL_FAILURE(DeclareReadyOrderGraph(graph, s_PrefixCount, ReadyOrderShape::ConsumerFirstPairs));
    u64 generation = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView view(graph);
        generation = view.generation();
    }
    const Core::GpuTaskId first{ .generation = generation, .index = s_PrefixCount };
    const Core::GpuTaskId second{ .generation = generation, .index = s_PrefixCount + 1u };
    {
        const auto gpuTaskAdmission = AddTask(graph, s_PrefixCount, Span(&second, 1u));
        ASSERT_TRUE(gpuTaskAdmission);
        ASSERT_EQ(*gpuTaskAdmission, first);
    }
    {
        const auto secondGpuTaskAdmission = AddTask(graph, s_PrefixCount + 1u, Span(&first, 1u));
        ASSERT_TRUE(secondGpuTaskAdmission);
        ASSERT_EQ(*secondGpuTaskAdmission, second);
    }
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    const Core::GpuTaskGraph::DeclarationReadView view(graph);
    Core::Alloc::ScratchArena scratch(s_AnalysisScratchArena);
    EXPECT_FALSE(Core::GpuTaskGraphCompiler().analyze(view, scratch, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::Cycle);
    EXPECT_TRUE(analysis.topologicalOrder().empty());
    ASSERT_EQ(analysis.cyclePath().size(), 3u);
    EXPECT_EQ(analysis.cyclePath()[0u], first);
    EXPECT_EQ(analysis.cyclePath()[1u], second);
    EXPECT_EQ(analysis.cyclePath()[2u], first);
}

TEST(GpuTaskGraphAnalysis, ReducesShortcutEdgesAtTheLastDirectConsumerBeforeLongTails){
    constexpr usize s_TaskCount = 257u;
    constexpr Array<b8, 2> s_ReversedDeclarations = { false, true };
    for(const b8 reversed : s_ReversedDeclarations){
        SCOPED_TRACE(reversed);
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        ASSERT_NO_FATAL_FAILURE(DeclareShortcutChain(graph, s_TaskCount, reversed));
        const Core::GpuTaskGraph::DeclarationReadView view(graph);
        Core::Alloc::ScratchArena scratch(s_AnalysisScratchArena);
        const Core::GpuTaskGraphCompiler compiler;
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(compiler.analyze(view, scratch, analysis));
        ASSERT_NO_FATAL_FAILURE(ExpectReducedShortcutChain(analysis, s_TaskCount, reversed));
    }
}

TEST(GpuTaskGraphAnalysis, MatchesIndependentReferenceForPermutedDagsAndDuplicateDependencies){
    constexpr Array<usize, 12> s_TaskCounts = { 0u, 1u, 9u, 63u, 64u, 65u, 127u, 128u, 129u, 191u, 192u, 193u };
    constexpr Array<u32, 2> s_Seeds = { 17u, 91u };
    for(const usize taskCount : s_TaskCounts){
        for(const u32 seed : s_Seeds){
            SCOPED_TRACE(taskCount);
            SCOPED_TRACE(seed);
            TestArena testArena;
            Core::Alloc::ScratchArena scratchArena(s_AnalysisScratchArena);
            Core::GpuTaskGraph graph(testArena.arena);
            u64 generation = 0u;
            {
                const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
                ASSERT_TRUE(declarations.valid());
                generation = declarations.generation();
            }

            Vector<usize, Core::Alloc::ScratchArena> ranks(taskCount, scratchArena);
            for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex)
                ranks[taskIndex] = taskIndex;
            u32 randomState = seed;
            for(usize remaining = taskCount; remaining > 1u; --remaining){
                randomState = randomState * 1664525u + 1013904223u;
                Swap(ranks[remaining - 1u], ranks[randomState % remaining]);
            }

            Vector<Core::GpuTaskId, Core::Alloc::ScratchArena> dependencies(scratchArena);
            dependencies.reserve(taskCount * 2u);
            Vector<Core::GpuTaskDependencyEdge, Core::Alloc::ScratchArena> expectedEdges(scratchArena);
            expectedEdges.reserve(taskCount * taskCount / 2u);
            for(usize consumerIndex = 0u; consumerIndex < taskCount; ++consumerIndex){
                dependencies.clear();
                // Reverse producer declaration order deliberately differs from the shuffled topological order.
                for(usize remaining = taskCount; remaining > 0u; --remaining){
                    const usize producerIndex = remaining - 1u;
                    randomState = randomState * 1664525u + 1013904223u;
                    if(ranks[producerIndex] >= ranks[consumerIndex] || (randomState >> 24u) % 5u != 0u)
                        continue;
                    const Core::GpuTaskId producer{ .generation = generation, .index = static_cast<u32>(producerIndex) };
                    dependencies.push_back(producer);
                    dependencies.push_back(producer);
                    expectedEdges.push_back(Core::GpuTaskDependencyEdge{
                        .producer = producer,
                        .consumer = { .generation = generation, .index = static_cast<u32>(consumerIndex) },
                        .resource = {},
                        .resourceVersion = {},
                        .hazard = Core::GpuTaskHazardType::Explicit,
                    });
                }
                const auto taskResult = AddTask(graph, consumerIndex, dependencies);
                ASSERT_TRUE(taskResult);
                const Core::GpuTaskId task = *taskResult;
                ASSERT_TRUE(task.valid());
                EXPECT_EQ(task.index, consumerIndex);
                EXPECT_EQ(task.generation, generation);
            }

            Core::GpuTaskGraphAnalysis analysis(testArena.arena);
            ASSERT_NO_FATAL_FAILURE(ExpectDagAnalysis(graph, expectedEdges, scratchArena, analysis));
            EXPECT_EQ(analysis.explicitEdgeCount(), expectedEdges.size());
            EXPECT_EQ(analysis.inferredEdgeCount(), 0u);
            EXPECT_TRUE(analysis.inferredEdges().empty());
        }
    }
}

TEST(GpuTaskGraphAnalysis, DeduplicatesHazardReasonsAndPreservesExplicitRawEdgePriority){
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(s_AnalysisScratchArena);
    Core::GpuTaskGraph graph(testArena.arena);
    Array<Core::GpuGraphResourceId, 3u> resources = {};
    for(usize resourceIndex = 0u; resourceIndex < LengthOf(resources); ++resourceIndex){
        Array<char, 32u> resourceIndexBuffer = {};
        const auto resourceNameResult = DeriveName(Name("tests/task_graph_analysis/resource_"), FormatDecimal(resourceIndex, resourceIndexBuffer));
        ASSERT_TRUE(resourceNameResult);
        const auto resourceTaskAdmission = graph.importHazardDomain(Core::GpuGraphResourceDesc{}.setIdentity(*resourceNameResult).setMarkerLabel("Analysis Hazard Domain").setType(Core::GpuGraphResourceType::HazardDomain));
        ASSERT_TRUE(resourceTaskAdmission);
        resources[resourceIndex] = *resourceTaskAdmission;
        ASSERT_TRUE(resources[resourceIndex].valid());
    }

    Array<Core::GpuTaskId, 3u> tasks = {};
    const Array<Array<Core::GpuTaskResourceAccess::Enum, 3u>, 3u> accesses = {{
        { Core::GpuTaskResourceAccess::ReadWrite, Core::GpuTaskResourceAccess::Read, Core::GpuTaskResourceAccess::Write },
        { Core::GpuTaskResourceAccess::ReadWrite, Core::GpuTaskResourceAccess::Write, Core::GpuTaskResourceAccess::Read },
        { Core::GpuTaskResourceAccess::Read, Core::GpuTaskResourceAccess::Read, Core::GpuTaskResourceAccess::Read },
    }};
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
        Array<Core::GpuTaskResourceUse, 6u> uses = {};
        const usize resourceCount = taskIndex == 1u ? 2u : 3u;
        for(usize resourceIndex = 0u; resourceIndex < resourceCount; ++resourceIndex){
            uses[resourceIndex * 2u] = Core::GpuTaskResourceUse{ .resource = resources[resourceIndex], .range = {}, .requiredState = Core::ResourceStates::UnorderedAccess, .access = accesses[taskIndex][resourceIndex], };
            uses[resourceIndex * 2u + 1u] = uses[resourceIndex * 2u];
        }
        const Array<Core::GpuTaskId, 2> dependencies = { tasks[0u], tasks[0u] };
        const auto taskAdmission = AddTask(graph, taskIndex, taskIndex == 1u ? Span(dependencies) : Span<const Core::GpuTaskId>{}, Span(uses.data(), resourceCount * 2u));
        ASSERT_TRUE(taskAdmission);
        tasks[taskIndex] = *taskAdmission;
        ASSERT_TRUE(tasks[taskIndex].valid());
    }

    Vector<Core::GpuTaskDependencyEdge, Core::Alloc::ScratchArena> expectedEdges(scratchArena);
    expectedEdges.reserve(3u);
    expectedEdges.push_back(Core::GpuTaskDependencyEdge{ .producer = tasks[0u], .consumer = tasks[1u], .resource = {}, .resourceVersion = {}, .hazard = Core::GpuTaskHazardType::Explicit, });
    expectedEdges.push_back(Core::GpuTaskDependencyEdge{ .producer = tasks[1u], .consumer = tasks[2u], .resource = resources[0u], .resourceVersion = {}, .hazard = Core::GpuTaskHazardType::ReadAfterWrite, });
    expectedEdges.push_back(Core::GpuTaskDependencyEdge{ .producer = tasks[0u], .consumer = tasks[2u], .resource = resources[2u], .resourceVersion = {}, .hazard = Core::GpuTaskHazardType::ReadAfterWrite, });
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_NO_FATAL_FAILURE(ExpectDagAnalysis(graph, expectedEdges, scratchArena, analysis));
    EXPECT_EQ(analysis.explicitEdgeCount(), 1u);
    EXPECT_EQ(analysis.inferredEdgeCount(), 3u);
    EXPECT_EQ(analysis.resourceVersionEdgeCount(), 0u);

    const Array<Core::GpuTaskDependencyEdge, 6> expectedInferred = {
        Core::GpuTaskDependencyEdge{ tasks[0u], tasks[1u], resources[0u], {}, Core::GpuTaskHazardType::ReadAfterWrite },
        Core::GpuTaskDependencyEdge{ tasks[0u], tasks[1u], resources[0u], {}, Core::GpuTaskHazardType::WriteAfterWrite },
        Core::GpuTaskDependencyEdge{ tasks[0u], tasks[1u], resources[1u], {}, Core::GpuTaskHazardType::WriteAfterRead },
        Core::GpuTaskDependencyEdge{ tasks[1u], tasks[2u], resources[0u], {}, Core::GpuTaskHazardType::ReadAfterWrite },
        Core::GpuTaskDependencyEdge{ tasks[1u], tasks[2u], resources[1u], {}, Core::GpuTaskHazardType::ReadAfterWrite },
        Core::GpuTaskDependencyEdge{ tasks[0u], tasks[2u], resources[2u], {}, Core::GpuTaskHazardType::ReadAfterWrite },
    };
    ASSERT_EQ(analysis.inferredEdges().size(), LengthOf(expectedInferred));
    for(usize edgeIndex = 0u; edgeIndex < LengthOf(expectedInferred); ++edgeIndex)
        ExpectEdgeEqual(expectedInferred[edgeIndex], analysis.inferredEdges()[edgeIndex]);
}


TEST(GpuTaskGraphAnalysis, EmptySchedulingReachabilityAvoidsStorageAndResetsAcrossDependencyGraphs){
    constexpr usize s_TaskCount = 65u;
    TestArena testArena;
    Core::Alloc::ScratchArena analysisScratchArena(s_AnalysisScratchArena);
    Core::Alloc::ScratchArena reachabilityScratchArena(Name("tests/task/gpu/edgefree_reachability"));
    Core::GpuTaskGraph graph(testArena.arena);
    Array<Core::GpuTaskId, s_TaskCount> tasks = {};
    for(usize taskIndex = 0u; taskIndex < s_TaskCount; ++taskIndex){
        const auto taskAdmission = AddTask(graph, taskIndex);
        ASSERT_TRUE(taskAdmission);
        tasks[taskIndex] = *taskAdmission;
        ASSERT_TRUE(tasks[taskIndex].valid());
    }
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    const Core::GpuTaskGraphCompiler compiler;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);

        ASSERT_TRUE(compiler.analyze(declarations, analysisScratchArena, analysis));
        ASSERT_TRUE(analysis.schedulingEdges().empty());
    }

    Core::GpuTaskGraphCompilerDetail::GpuTaskSchedulingReachability reachability(reachabilityScratchArena);
    EXPECT_FALSE(reachability.reaches(tasks[0u], tasks[s_TaskCount - 1u]));
    EXPECT_FALSE(reachability.transitivelyIndependent(tasks[0u], tasks[s_TaskCount - 1u]));
    const ArenaMemoryStats before = reachabilityScratchArena.memoryStats();
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);

        ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(declarations, analysis, reachability));
    }
    const ArenaMemoryStats after = reachabilityScratchArena.memoryStats();
    EXPECT_EQ(after.allocationCount, before.allocationCount);
    EXPECT_EQ(after.reallocationCount, before.reallocationCount);
    EXPECT_EQ(after.deallocationCount, before.deallocationCount);
    EXPECT_EQ(after.reservedBytes, before.reservedBytes);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_EQ(after.peakUsedBytes, before.peakUsedBytes);

    for(const Core::GpuTaskId& source : tasks){
        for(const Core::GpuTaskId& destination : tasks){
            EXPECT_FALSE(reachability.reaches(source, destination));
            EXPECT_EQ(reachability.transitivelyIndependent(source, destination), source != destination);
        }
    }
    const Array<Core::GpuTaskId, 3> invalidTasks = {
        Core::GpuTaskId{},
        Core::GpuTaskId{ .generation = tasks[0u].generation + 1u, .index = tasks[0u].index },
        Core::GpuTaskId{ .generation = tasks[0u].generation, .index = static_cast<u32>(s_TaskCount) },
    };
    for(const Core::GpuTaskId& invalid : invalidTasks){
        EXPECT_FALSE(reachability.reaches(tasks[0u], invalid));
        EXPECT_FALSE(reachability.reaches(invalid, tasks[0u]));
        EXPECT_FALSE(reachability.transitivelyIndependent(tasks[0u], invalid));
        EXPECT_FALSE(reachability.transitivelyIndependent(invalid, tasks[0u]));
    }

    Core::GpuTaskGraph dependentGraph(testArena.arena);
    const auto producerResult = AddTask(dependentGraph, 0u);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;
    const auto consumerResult = AddTask(dependentGraph, 1u, Span(&producer, 1u));
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    const auto independentResult = AddTask(dependentGraph, 2u);
    ASSERT_TRUE(independentResult);
    const Core::GpuTaskId independent = *independentResult;
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());
    ASSERT_TRUE(independent.valid());
    Core::GpuTaskGraphAnalysis dependentAnalysis(testArena.arena);
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(dependentGraph);

        ASSERT_TRUE(compiler.analyze(declarations, analysisScratchArena, dependentAnalysis));
        ASSERT_FALSE(dependentAnalysis.schedulingEdges().empty());
        ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(declarations, dependentAnalysis, reachability));
    }
    EXPECT_TRUE(reachability.reaches(producer, consumer));
    EXPECT_FALSE(reachability.reaches(consumer, producer));
    EXPECT_FALSE(reachability.transitivelyIndependent(producer, consumer));
    EXPECT_TRUE(reachability.transitivelyIndependent(producer, independent));
    EXPECT_GT(reachabilityScratchArena.memoryStats().allocationCount, after.allocationCount);
    EXPECT_FALSE(reachability.reaches(tasks[0u], tasks[s_TaskCount - 1u]));
    EXPECT_FALSE(reachability.transitivelyIndependent(tasks[0u], tasks[s_TaskCount - 1u]));

    const ArenaMemoryStats beforeReuse = reachabilityScratchArena.memoryStats();
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);

        ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(declarations, analysis, reachability));
    }
    EXPECT_EQ(reachabilityScratchArena.memoryStats().allocationCount, beforeReuse.allocationCount);
    EXPECT_FALSE(reachability.reaches(tasks[0u], tasks[s_TaskCount - 1u]));
    EXPECT_TRUE(reachability.transitivelyIndependent(tasks[0u], tasks[s_TaskCount - 1u]));
    EXPECT_FALSE(reachability.transitivelyIndependent(tasks[0u], tasks[0u]));
    EXPECT_FALSE(reachability.reaches(producer, consumer));
    EXPECT_FALSE(reachability.transitivelyIndependent(producer, independent));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

