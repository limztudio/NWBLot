// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/gpu_task_graph_read_views.h>
#include <tests/common/test_context.h>

#include <core/task/gpu/compiler_internal.h>
#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_analysis_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct TaskGraphAnalysisTestsTag>;
namespace Graphics = Core;

inline constexpr Name s_AnalysisScratchArena("tests/task/gpu/analysis_scratch");


[[nodiscard]] static Graphics::GpuTaskId AddTask(
    Graphics::GpuTaskGraph& graph,
    const usize taskIndex,
    const Graphics::GpuTaskId* const dependencies = nullptr,
    const usize dependencyCount = 0u,
    const Graphics::GpuTaskResourceUse* const resourceUses = nullptr,
    const usize resourceUseCount = 0u
){
    char taskIndexBuffer[32u] = {};
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(DeriveName(Name("tests/task_graph_analysis/task_"), FormatDecimal(taskIndex, taskIndexBuffer)))
        .setMarkerLabel("Analysis Test Task")
        .setDependencies(dependencies, dependencyCount)
        .setResourceUses(resourceUses, resourceUseCount)
    ;
    return graph.addTask(desc);
}

static void ExpectEdgeEqual(const Graphics::GpuTaskDependencyEdge& expected, const Graphics::GpuTaskDependencyEdge& actual){
    EXPECT_EQ(actual.producer, expected.producer);
    EXPECT_EQ(actual.consumer, expected.consumer);
    EXPECT_EQ(actual.resource, expected.resource);
    EXPECT_EQ(actual.resourceVersion, expected.resourceVersion);
    EXPECT_EQ(actual.hazard, expected.hazard);
}

static void ExpectDagAnalysis(
    const Graphics::GpuTaskGraph& graph,
    Graphics::GpuTaskGraphAnalysis& analysis,
    const Vector<Graphics::GpuTaskDependencyEdge, Core::Alloc::ScratchArena>& expectedEdges,
    Core::Alloc::ScratchArena& scratchArena
){
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    ASSERT_TRUE(declarations.valid());
    const Graphics::GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.analyze(declarations, analysis, scratchArena));
    ASSERT_TRUE(analysis.validFor(declarations));
    ASSERT_EQ(analysis.edges().size(), expectedEdges.size());
    for(usize edgeIndex = 0u; edgeIndex < expectedEdges.size(); ++edgeIndex)
        ExpectEdgeEqual(expectedEdges[edgeIndex], analysis.edges()[edgeIndex]);

    const usize taskCount = declarations.taskCount();
    Vector<u8, Core::Alloc::ScratchArena> adjacency(taskCount * taskCount, 0u, scratchArena);
    Vector<u8, Core::Alloc::ScratchArena> reachable(taskCount * taskCount, 0u, scratchArena);
    Vector<u32, Core::Alloc::ScratchArena> indegrees(taskCount, 0u, scratchArena);
    Vector<u8, Core::Alloc::ScratchArena> emitted(taskCount, 0u, scratchArena);
    for(const Graphics::GpuTaskDependencyEdge& edge : expectedEdges){
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

    Graphics::GpuTaskGraphCompilerDetail::GpuTaskSchedulingReachability schedulingReachability(scratchArena);
    ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(declarations, analysis, schedulingReachability));
    for(usize source = 0u; source < taskCount; ++source){
        for(usize destination = 0u; destination < taskCount; ++destination){
            const Graphics::GpuTaskId sourceTask = declarations.taskAt(source).id;
            const Graphics::GpuTaskId destinationTask = declarations.taskAt(destination).id;
            EXPECT_EQ(schedulingReachability.reaches(sourceTask, destinationTask), reachable[source * taskCount + destination] != 0u);
            EXPECT_EQ(schedulingReachability.transitivelyIndependent(sourceTask, destinationTask),
                source != destination && !reachable[source * taskCount + destination] && !reachable[destination * taskCount + source]);
        }
    }

    Vector<usize, Core::Alloc::ScratchArena> reducedEdgeIndices(scratchArena);
    reducedEdgeIndices.reserve(expectedEdges.size());
    for(usize edgeIndex = 0u; edgeIndex < expectedEdges.size(); ++edgeIndex){
        const Graphics::GpuTaskDependencyEdge& edge = expectedEdges[edgeIndex];
        bool redundant = false;
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
        const Graphics::GpuTaskId task = declarations.taskAt(taskIndex).id;
        const Graphics::GpuTaskGraphSchedulingTaskIndexView consumers = analysis.schedulingConsumers(task);
        const Graphics::GpuTaskGraphSchedulingTaskIndexView producers = analysis.schedulingProducers(task);
        usize consumerOffset = 0u;
        usize producerOffset = 0u;
        for(const usize edgeIndex : reducedEdgeIndices){
            const Graphics::GpuTaskDependencyEdge& edge = expectedEdges[edgeIndex];
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


static void DeclareShortcutChain(Graphics::GpuTaskGraph& graph, const usize taskCount, const bool reversed){
    u64 generation = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
        generation = view.generation();
    }
    for(usize declarationIndex = 0u; declarationIndex < taskCount; ++declarationIndex){
        const usize chainIndex = reversed ? taskCount - declarationIndex - 1u : declarationIndex;
        Graphics::GpuTaskId dependencies[2u] = {};
        const usize dependencyCount = Min(chainIndex, LengthOf(dependencies));
        for(usize offset = 0u; offset < dependencyCount; ++offset){
            const usize sourceIndex = chainIndex - offset - 1u;
            dependencies[offset] = {
                .generation = generation,
                .index = static_cast<u32>(reversed ? taskCount - sourceIndex - 1u : sourceIndex),
            };
        }
        ASSERT_TRUE(AddTask(graph, declarationIndex, dependencies, dependencyCount).valid());
    }
}

static void ExpectReducedShortcutChain(
    const Graphics::GpuTaskGraphAnalysis& analysis,
    const usize taskCount,
    const bool reversed
){
    ASSERT_EQ(analysis.edges().size(), taskCount * 2u - 3u);
    ASSERT_EQ(analysis.schedulingEdges().size(), taskCount - 1u);
    for(usize edgeIndex = 0u; edgeIndex + 1u < taskCount; ++edgeIndex){
        const auto& edge = analysis.schedulingEdges()[edgeIndex];
        EXPECT_EQ(edge.producer.index, reversed ? edgeIndex + 1u : edgeIndex);
        EXPECT_EQ(edge.consumer.index, reversed ? edgeIndex : edgeIndex + 1u);
    }
}

static void MeasureAnalysis(const Graphics::GpuTaskGraph& graph, Graphics::GpuTaskGraphAnalysis& analysis){
    const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
    const Graphics::GpuTaskGraphCompiler compiler;
    u64 samples[5u] = {};
    usize scratchBytes = 0u;
    for(usize sample = 0u; sample <= LengthOf(samples); ++sample){
        Core::Alloc::ScratchArena scratch(s_AnalysisScratchArena);
        const Timer begin = TimerNow();
        const bool analyzed = compiler.analyze(view, analysis, scratch);
        const u64 elapsed = DurationInNS<u64>(TimerNow(), begin);
        ASSERT_TRUE(analyzed);
        if(sample != 0u)
            samples[sample - 1u] = elapsed;
        scratchBytes = scratch.memoryStats().peakUsedBytes;
    }
    Sort(samples, samples + LengthOf(samples));
    Tests::RecordUnsignedTestProperty("median_analysis_ns", samples[LengthOf(samples) / 2u]);
    Tests::RecordUnsignedTestProperty("minimum_analysis_ns", samples[0u]);
    Tests::RecordUnsignedTestProperty("scratch_bytes", scratchBytes);
    Tests::RecordUnsignedTestProperty("task_count", view.taskCount());
}

static void BenchmarkShortcutChain(const usize taskCount){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    ASSERT_NO_FATAL_FAILURE(DeclareShortcutChain(graph, taskCount, false));
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_NO_FATAL_FAILURE(MeasureAnalysis(graph, analysis));
    ASSERT_NO_FATAL_FAILURE(ExpectReducedShortcutChain(analysis, taskCount, false));
}


namespace ReadyOrderShape{
    enum Enum : u8{
        ConsumerFirstPairs,
        ProducerFirstPairs,
        ReverseChain,
    };
};

static void DeclareReadyOrderGraph(
    Graphics::GpuTaskGraph& graph,
    const usize taskCount,
    const ReadyOrderShape::Enum shape
){
    u64 generation = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
        generation = view.generation();
    }
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        const usize half = taskCount / 2u;
        const bool hasDependency = shape == ReadyOrderShape::ReverseChain ? taskIndex + 1u < taskCount
            : shape == ReadyOrderShape::ConsumerFirstPairs ? taskIndex < half : taskIndex >= half;
        const usize producerIndex = !hasDependency ? 0u : shape == ReadyOrderShape::ReverseChain ? taskIndex + 1u
            : shape == ReadyOrderShape::ConsumerFirstPairs ? taskIndex + half : taskIndex - half;
        const Graphics::GpuTaskId dependency{ .generation = generation, .index = static_cast<u32>(producerIndex) };
        ASSERT_TRUE(AddTask(graph, taskIndex, hasDependency ? &dependency : nullptr, hasDependency ? 1u : 0u).valid());
    }
}

static void ExpectReadyOrder(
    const Graphics::GpuTaskGraphAnalysis& analysis,
    const usize taskCount,
    const ReadyOrderShape::Enum shape
){
    ASSERT_EQ(analysis.topologicalOrder().size(), taskCount);
    for(usize index = 0u; index < taskCount; ++index){
        const usize expected = shape == ReadyOrderShape::ReverseChain ? taskCount - index - 1u
            : shape == ReadyOrderShape::ConsumerFirstPairs ? (index % 2u == 0u ? taskCount / 2u + index / 2u : index / 2u) : index;
        EXPECT_EQ(analysis.topologicalOrder()[index].index, expected);
    }
}

static void BenchmarkReadyOrder(const usize taskCount, const ReadyOrderShape::Enum shape){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    ASSERT_NO_FATAL_FAILURE(DeclareReadyOrderGraph(graph, taskCount, shape));
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_NO_FATAL_FAILURE(MeasureAnalysis(graph, analysis));
    ASSERT_NO_FATAL_FAILURE(ExpectReadyOrder(analysis, taskCount, shape));
}


TEST(GpuTaskGraphAnalysis, PreservesLowestReadyIdAcrossRepeatedBackwardReleasesAndPartialIndexWords){
    constexpr usize s_Counts[] = { 130u, 8194u };
    constexpr ReadyOrderShape::Enum s_Shapes[] = {
        ReadyOrderShape::ConsumerFirstPairs, ReadyOrderShape::ProducerFirstPairs, ReadyOrderShape::ReverseChain,
    };
    for(const usize count : s_Counts){
        for(const auto shape : s_Shapes){
            SCOPED_TRACE(count);
            SCOPED_TRACE(shape);
            TestArena testArena;
            Graphics::GpuTaskGraph graph(testArena.arena);
            ASSERT_NO_FATAL_FAILURE(DeclareReadyOrderGraph(graph, count, shape));
            Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
            const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
            Core::Alloc::ScratchArena scratch(s_AnalysisScratchArena);
            ASSERT_TRUE(Graphics::GpuTaskGraphCompiler().analyze(view, analysis, scratch));
            ASSERT_NO_FATAL_FAILURE(ExpectReadyOrder(analysis, count, shape));
        }
    }
}

TEST(GpuTaskGraphAnalysis, RetainsCycleDiagnosticsAfterBackwardReleasePrefixExhaustsReadyWork){
    constexpr usize s_PrefixCount = 130u;
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    ASSERT_NO_FATAL_FAILURE(DeclareReadyOrderGraph(graph, s_PrefixCount, ReadyOrderShape::ConsumerFirstPairs));
    u64 generation = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
        generation = view.generation();
    }
    const Graphics::GpuTaskId first{ .generation = generation, .index = s_PrefixCount };
    const Graphics::GpuTaskId second{ .generation = generation, .index = s_PrefixCount + 1u };
    ASSERT_EQ(AddTask(graph, s_PrefixCount, &second, 1u), first);
    ASSERT_EQ(AddTask(graph, s_PrefixCount + 1u, &first, 1u), second);
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
    Core::Alloc::ScratchArena scratch(s_AnalysisScratchArena);
    EXPECT_FALSE(Graphics::GpuTaskGraphCompiler().analyze(view, analysis, scratch));
    EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::Cycle);
    EXPECT_TRUE(analysis.topologicalOrder().empty());
    ASSERT_EQ(analysis.cyclePath().size(), 3u);
    EXPECT_EQ(analysis.cyclePath()[0u], first);
    EXPECT_EQ(analysis.cyclePath()[1u], second);
    EXPECT_EQ(analysis.cyclePath()[2u], first);
}

TEST(GpuTaskGraphAnalysis, DISABLED_ConsumerFirstPairsBenchmark1024Tasks){
    BenchmarkReadyOrder(1024u, ReadyOrderShape::ConsumerFirstPairs);
}

TEST(GpuTaskGraphAnalysis, DISABLED_ConsumerFirstPairsBenchmark4096Tasks){
    BenchmarkReadyOrder(4096u, ReadyOrderShape::ConsumerFirstPairs);
}

TEST(GpuTaskGraphAnalysis, DISABLED_ProducerFirstPairsBenchmark1024Tasks){
    BenchmarkReadyOrder(1024u, ReadyOrderShape::ProducerFirstPairs);
}

TEST(GpuTaskGraphAnalysis, DISABLED_ProducerFirstPairsBenchmark4096Tasks){
    BenchmarkReadyOrder(4096u, ReadyOrderShape::ProducerFirstPairs);
}

TEST(GpuTaskGraphAnalysis, DISABLED_ReverseChainBenchmark1024Tasks){
    BenchmarkReadyOrder(1024u, ReadyOrderShape::ReverseChain);
}

TEST(GpuTaskGraphAnalysis, DISABLED_ReverseChainBenchmark4096Tasks){
    BenchmarkReadyOrder(4096u, ReadyOrderShape::ReverseChain);
}



TEST(GpuTaskGraphAnalysis, ReducesShortcutEdgesAtTheLastDirectConsumerBeforeLongTails){
    constexpr usize s_TaskCount = 257u;
    constexpr bool s_ReversedDeclarations[] = { false, true };
    for(const bool reversed : s_ReversedDeclarations){
        SCOPED_TRACE(reversed);
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        ASSERT_NO_FATAL_FAILURE(DeclareShortcutChain(graph, s_TaskCount, reversed));
        const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
        Core::Alloc::ScratchArena scratch(s_AnalysisScratchArena);
        const Graphics::GpuTaskGraphCompiler compiler;
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(compiler.analyze(view, analysis, scratch));
        ASSERT_NO_FATAL_FAILURE(ExpectReducedShortcutChain(analysis, s_TaskCount, reversed));
    }
}

TEST(GpuTaskGraphAnalysis, DISABLED_ShortcutChainBenchmark1024Tasks){
    BenchmarkShortcutChain(1024u);
}

TEST(GpuTaskGraphAnalysis, DISABLED_ShortcutChainBenchmark4096Tasks){
    BenchmarkShortcutChain(4096u);
}


TEST(GpuTaskGraphAnalysis, MatchesIndependentReferenceForPermutedDagsAndDuplicateDependencies){
    constexpr usize s_TaskCounts[] = { 0u, 1u, 9u, 63u, 64u, 65u, 127u, 128u, 129u, 191u, 192u, 193u };
    constexpr u32 s_Seeds[] = { 17u, 91u };
    for(const usize taskCount : s_TaskCounts){
        for(const u32 seed : s_Seeds){
            SCOPED_TRACE(taskCount);
            SCOPED_TRACE(seed);
            TestArena testArena;
            Core::Alloc::ScratchArena scratchArena(s_AnalysisScratchArena);
            Graphics::GpuTaskGraph graph(testArena.arena);
            u64 generation = 0u;
            {
                const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
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

            Vector<Graphics::GpuTaskId, Core::Alloc::ScratchArena> dependencies(scratchArena);
            dependencies.reserve(taskCount * 2u);
            Vector<Graphics::GpuTaskDependencyEdge, Core::Alloc::ScratchArena> expectedEdges(scratchArena);
            expectedEdges.reserve(taskCount * taskCount / 2u);
            for(usize consumerIndex = 0u; consumerIndex < taskCount; ++consumerIndex){
                dependencies.clear();
                // Reverse producer declaration order deliberately differs from the shuffled topological order.
                for(usize remaining = taskCount; remaining > 0u; --remaining){
                    const usize producerIndex = remaining - 1u;
                    randomState = randomState * 1664525u + 1013904223u;
                    if(ranks[producerIndex] >= ranks[consumerIndex] || (randomState >> 24u) % 5u != 0u)
                        continue;
                    const Graphics::GpuTaskId producer{ .generation = generation, .index = static_cast<u32>(producerIndex) };
                    dependencies.push_back(producer);
                    dependencies.push_back(producer);
                    expectedEdges.push_back(Graphics::GpuTaskDependencyEdge{
                        .producer = producer,
                        .consumer = { .generation = generation, .index = static_cast<u32>(consumerIndex) },
                        .resource = {},
                        .resourceVersion = {},
                        .hazard = Graphics::GpuTaskHazardType::Explicit,
                    });
                }
                const Graphics::GpuTaskId task = AddTask(graph, consumerIndex, dependencies.data(), dependencies.size());
                ASSERT_TRUE(task.valid());
                EXPECT_EQ(task.index, consumerIndex);
                EXPECT_EQ(task.generation, generation);
            }

            Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
            ASSERT_NO_FATAL_FAILURE(ExpectDagAnalysis(graph, analysis, expectedEdges, scratchArena));
            EXPECT_EQ(analysis.explicitEdgeCount(), expectedEdges.size());
            EXPECT_EQ(analysis.inferredEdgeCount(), 0u);
            EXPECT_TRUE(analysis.inferredEdges().empty());
        }
    }
}

TEST(GpuTaskGraphAnalysis, DeduplicatesHazardReasonsAndPreservesExplicitRawEdgePriority){
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(s_AnalysisScratchArena);
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuGraphResourceId resources[3u] = {};
    for(usize resourceIndex = 0u; resourceIndex < LengthOf(resources); ++resourceIndex){
        char resourceIndexBuffer[32u] = {};
        resources[resourceIndex] = graph.importHazardDomain(
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(DeriveName(Name("tests/task_graph_analysis/resource_"), FormatDecimal(resourceIndex, resourceIndexBuffer)))
                .setMarkerLabel("Analysis Hazard Domain")
                .setType(Graphics::GpuGraphResourceType::HazardDomain)
        );
        ASSERT_TRUE(resources[resourceIndex].valid());
    }

    Graphics::GpuTaskId tasks[3u] = {};
    const Graphics::GpuTaskResourceAccess::Enum accesses[3u][3u] = {
        { Graphics::GpuTaskResourceAccess::ReadWrite, Graphics::GpuTaskResourceAccess::Read, Graphics::GpuTaskResourceAccess::Write },
        { Graphics::GpuTaskResourceAccess::ReadWrite, Graphics::GpuTaskResourceAccess::Write, Graphics::GpuTaskResourceAccess::Read },
        { Graphics::GpuTaskResourceAccess::Read, Graphics::GpuTaskResourceAccess::Read, Graphics::GpuTaskResourceAccess::Read },
    };
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
        Graphics::GpuTaskResourceUse uses[6u] = {};
        const usize resourceCount = taskIndex == 1u ? 2u : 3u;
        for(usize resourceIndex = 0u; resourceIndex < resourceCount; ++resourceIndex){
            uses[resourceIndex * 2u] = Graphics::GpuTaskResourceUse{
                .resource = resources[resourceIndex],
                .range = {},
                .requiredState = Graphics::ResourceStates::UnorderedAccess,
                .access = accesses[taskIndex][resourceIndex],
            };
            uses[resourceIndex * 2u + 1u] = uses[resourceIndex * 2u];
        }
        const Graphics::GpuTaskId dependencies[] = { tasks[0u], tasks[0u] };
        tasks[taskIndex] = AddTask(graph, taskIndex, dependencies, taskIndex == 1u ? 2u : 0u, uses, resourceCount * 2u);
        ASSERT_TRUE(tasks[taskIndex].valid());
    }

    Vector<Graphics::GpuTaskDependencyEdge, Core::Alloc::ScratchArena> expectedEdges(scratchArena);
    expectedEdges.reserve(3u);
    expectedEdges.push_back(Graphics::GpuTaskDependencyEdge{
        .producer = tasks[0u],
        .consumer = tasks[1u],
        .resource = {},
        .resourceVersion = {},
        .hazard = Graphics::GpuTaskHazardType::Explicit,
    });
    expectedEdges.push_back(Graphics::GpuTaskDependencyEdge{
        .producer = tasks[1u],
        .consumer = tasks[2u],
        .resource = resources[0u],
        .resourceVersion = {},
        .hazard = Graphics::GpuTaskHazardType::ReadAfterWrite,
    });
    expectedEdges.push_back(Graphics::GpuTaskDependencyEdge{
        .producer = tasks[0u],
        .consumer = tasks[2u],
        .resource = resources[2u],
        .resourceVersion = {},
        .hazard = Graphics::GpuTaskHazardType::ReadAfterWrite,
    });
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_NO_FATAL_FAILURE(ExpectDagAnalysis(graph, analysis, expectedEdges, scratchArena));
    EXPECT_EQ(analysis.explicitEdgeCount(), 1u);
    EXPECT_EQ(analysis.inferredEdgeCount(), 3u);
    EXPECT_EQ(analysis.resourceVersionEdgeCount(), 0u);

    const Graphics::GpuTaskDependencyEdge expectedInferred[] = {
        { tasks[0u], tasks[1u], resources[0u], {}, Graphics::GpuTaskHazardType::ReadAfterWrite },
        { tasks[0u], tasks[1u], resources[0u], {}, Graphics::GpuTaskHazardType::WriteAfterWrite },
        { tasks[0u], tasks[1u], resources[1u], {}, Graphics::GpuTaskHazardType::WriteAfterRead },
        { tasks[1u], tasks[2u], resources[0u], {}, Graphics::GpuTaskHazardType::ReadAfterWrite },
        { tasks[1u], tasks[2u], resources[1u], {}, Graphics::GpuTaskHazardType::ReadAfterWrite },
        { tasks[0u], tasks[2u], resources[2u], {}, Graphics::GpuTaskHazardType::ReadAfterWrite },
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
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskId tasks[s_TaskCount] = {};
    for(usize taskIndex = 0u; taskIndex < s_TaskCount; ++taskIndex){
        tasks[taskIndex] = AddTask(graph, taskIndex);
        ASSERT_TRUE(tasks[taskIndex].valid());
    }
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    const Graphics::GpuTaskGraphCompiler compiler;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        ASSERT_TRUE(compiler.analyze(declarations, analysis, analysisScratchArena));
        ASSERT_TRUE(analysis.schedulingEdges().empty());
    }

    Graphics::GpuTaskGraphCompilerDetail::GpuTaskSchedulingReachability reachability(reachabilityScratchArena);
    EXPECT_FALSE(reachability.reaches(tasks[0u], tasks[s_TaskCount - 1u]));
    EXPECT_FALSE(reachability.transitivelyIndependent(tasks[0u], tasks[s_TaskCount - 1u]));
    const ArenaMemoryStats before = reachabilityScratchArena.memoryStats();
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(declarations, analysis, reachability));
    }
    const ArenaMemoryStats after = reachabilityScratchArena.memoryStats();
    EXPECT_EQ(after.allocationCount, before.allocationCount);
    EXPECT_EQ(after.reallocationCount, before.reallocationCount);
    EXPECT_EQ(after.deallocationCount, before.deallocationCount);
    EXPECT_EQ(after.reservedBytes, before.reservedBytes);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_EQ(after.peakUsedBytes, before.peakUsedBytes);

    for(const Graphics::GpuTaskId& source : tasks){
        for(const Graphics::GpuTaskId& destination : tasks){
            EXPECT_FALSE(reachability.reaches(source, destination));
            EXPECT_EQ(reachability.transitivelyIndependent(source, destination), source != destination);
        }
    }
    const Graphics::GpuTaskId invalidTasks[] = {
        {},
        { .generation = tasks[0u].generation + 1u, .index = tasks[0u].index },
        { .generation = tasks[0u].generation, .index = static_cast<u32>(s_TaskCount) },
    };
    for(const Graphics::GpuTaskId& invalid : invalidTasks){
        EXPECT_FALSE(reachability.reaches(tasks[0u], invalid));
        EXPECT_FALSE(reachability.reaches(invalid, tasks[0u]));
        EXPECT_FALSE(reachability.transitivelyIndependent(tasks[0u], invalid));
        EXPECT_FALSE(reachability.transitivelyIndependent(invalid, tasks[0u]));
    }

    Graphics::GpuTaskGraph dependentGraph(testArena.arena);
    const Graphics::GpuTaskId producer = AddTask(dependentGraph, 0u);
    const Graphics::GpuTaskId consumer = AddTask(dependentGraph, 1u, &producer, 1u);
    const Graphics::GpuTaskId independent = AddTask(dependentGraph, 2u);
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());
    ASSERT_TRUE(independent.valid());
    Graphics::GpuTaskGraphAnalysis dependentAnalysis(testArena.arena);
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(dependentGraph);

        ASSERT_TRUE(compiler.analyze(declarations, dependentAnalysis, analysisScratchArena));
        ASSERT_FALSE(dependentAnalysis.schedulingEdges().empty());
        ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(declarations, dependentAnalysis, reachability));
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
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(declarations, analysis, reachability));
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

