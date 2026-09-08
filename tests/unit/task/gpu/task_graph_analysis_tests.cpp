// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/gpu_task_graph_read_views.h>
#include <tests/common/test_context.h>

#include <core/graphics/task_graph/compiler.h>
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
namespace Graphics = Core;

inline constexpr Name s_AnalysisScratchArena("tests/graphics/task_graph_analysis_scratch");


[[nodiscard]] static Graphics::GpuTaskId AddTask(
    Graphics::GpuTaskGraph& graph,
    const usize taskIndex,
    const Graphics::GpuTaskId* const dependencies = nullptr,
    const usize dependencyCount = 0u,
    const Graphics::GpuTaskResourceUse* const resourceUses = nullptr,
    const usize resourceUseCount = 0u){
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
    Core::Alloc::ScratchArena& scratchArena){
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


TEST(GpuTaskGraphAnalysis, MatchesIndependentReferenceForPermutedDagsAndDuplicateDependencies){
    const usize taskCounts[] = { 0u, 1u, 9u, 63u, 64u, 65u, 127u, 128u, 129u };
    const u32 seeds[] = { 17u, 91u };
    for(const usize taskCount : taskCounts){
        for(const u32 seed : seeds){
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
                    const Graphics::GpuTaskId producer{ static_cast<u32>(producerIndex), generation };
                    dependencies.push_back(producer);
                    dependencies.push_back(producer);
                    expectedEdges.push_back(Graphics::GpuTaskDependencyEdge{
                        .producer = producer,
                        .consumer = { static_cast<u32>(consumerIndex), generation },
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

