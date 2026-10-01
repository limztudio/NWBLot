// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_queue_partial_dag_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;
using namespace Graphics::GpuTaskGraphCompilerDetail;

namespace Shape{
    enum Enum : u8{
        OneEdge,
        DisjointPairs,
        ConnectedBranches,
        DenseLayers,
        MergedPairs,
    };
};


void DeclareGraph(Graphics::GpuTaskGraph& graph, const usize taskCount, const Shape::Enum shape){
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        Graphics::GpuTaskId dependencies[16u] = {};
        usize dependencyCount = 0u;
        const auto addDependency = [&](const usize index){
            const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
            dependencies[dependencyCount++] = view.taskAt(index).id;
        };
        if(
            (shape == Shape::OneEdge && taskIndex == 1u)
            || ((shape == Shape::DisjointPairs || shape == Shape::MergedPairs) && taskIndex % 2u != 0u)
        )
            addDependency(taskIndex - 1u);
        else if(shape == Shape::ConnectedBranches && taskIndex != 0u)
            addDependency((taskIndex - 1u) / 2u);
        else if(shape == Shape::DenseLayers && taskIndex >= LengthOf(dependencies)){
            const usize previousLayer = (taskIndex / LengthOf(dependencies) - 1u) * LengthOf(dependencies);
            for(usize offset = 0u; offset < LengthOf(dependencies); ++offset)
                addDependency(previousLayer + offset);
        }
        Graphics::GpuTaskSchedulingHint scheduling;
        scheduling.cost = static_cast<Graphics::GpuTaskCostHint::Enum>(taskIndex % Graphics::GpuTaskCostHint::kCount);
        scheduling.mergeWithPrevious = shape == Shape::MergedPairs && taskIndex % 2u != 0u;
        const bool graphicsTask = shape == Shape::MergedPairs ? (taskIndex / 2u) % 2u == 0u : taskIndex % 2u == 0u;
        char identityText[32u] = {};
        const Graphics::GpuTaskId task = AddTaskWithCommands(
            graph,
            DeriveName(Name("tests/queue_partial_dag/"), FormatDecimal(taskIndex, identityText)),
            "Partial DAG Task",
            graphicsTask ? GraphicsCommands() : ComputeCommands(),
            scheduling,
            {},
            dependencies,
            dependencyCount
        );
        ASSERT_TRUE(task.valid());
    }
}


void CheckIndexedScores(const Shape::Enum shape){
    constexpr usize s_TaskCount = 130u;
    SCOPED_TRACE(shape);
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    DeclareGraph(graph, s_TaskCount, shape);
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    Graphics::GpuPhysicalQueueInfo auxiliary = GraphicsQueue(311u);
    auxiliary.queueIndex = 1u;
    Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(17u), DedicatedComputeQueue(29u), auxiliary };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
    ASSERT_EQ(view.taskCount(), s_TaskCount);
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    GpuTaskSchedulingReachability reachability(scratch);
    ASSERT_TRUE(BuildGpuTaskSchedulingReachability(view, analysis, reachability));
    GpuTaskQueueScoringData scoringData(view, analysis, reachability, {}, scratch);
    Graphics::GraphicsVector<Graphics::GpuTaskQueueAssignment> assignments(testArena.arena);
    Graphics::GraphicsVector<u32> indices(s_TaskCount, Limit<u32>::s_Max, testArena.arena);
    for(const Graphics::GpuTaskId task : analysis.topologicalOrder()){
        indices[task.index] = static_cast<u32>(assignments.size());
        const auto queue = queues[task.index % LengthOf(queues)].id;
        assignments.push_back({ .task = task, .initialQueue = queue, .queue = queue, .score = {} });
    }
    scoringData.rebuildAssignmentLoads(assignments, topology);
    Vector<GpuTaskQueuePlacementGroup, Graphics::Alloc::ScratchArena> groups(scratch);
    Graphics::GpuTaskQueueAssignmentDiagnostic diagnostic;
    ASSERT_TRUE(BuildQueuePlacementGroups(view, analysis, topology, {}, groups, diagnostic, scratch));

    // Scalar Floyd-Warshall is independent of packed rows, word bounds, and queue-cost membership.
    bool reachable[s_TaskCount][s_TaskCount] = {};
    for(const auto& edge : analysis.schedulingEdges())
        reachable[edge.producer.index][edge.consumer.index] = true;
    for(usize intermediate = 0u; intermediate < s_TaskCount; ++intermediate)
        for(usize source = 0u; source < s_TaskCount; ++source)
            for(usize destination = 0u; destination < s_TaskCount; ++destination)
                reachable[source][destination] = reachable[source][destination]
                    || (reachable[source][intermediate] && reachable[intermediate][destination]);
    for(usize source = 0u; source < s_TaskCount; ++source){
        for(usize destination = 0u; destination < s_TaskCount; ++destination){
            EXPECT_EQ(reachability.reaches(view.taskAt(source).id, view.taskAt(destination).id), reachable[source][destination]);
            EXPECT_EQ(
                reachability.transitivelyIndependent(view.taskAt(source).id, view.taskAt(destination).id),
                source != destination && !reachable[source][destination] && !reachable[destination][source]
            );
        }
    }
    const auto referenceOverlap = [&](const GpuTaskQueuePlacementGroup& group, const Graphics::GpuPhysicalQueueId queue){
        u64 overlap = 0u;
        for(usize row = 0u; row < assignments.size(); ++row){
            if(row >= group.assignmentOffset && row - group.assignmentOffset < group.assignmentCount)
                continue;
            const auto& other = assignments[row];
            if(other.queue == queue)
                continue;
            bool independent = true;
            for(usize offset = 0u; offset < group.assignmentCount; ++offset){
                const usize member = assignments[group.assignmentOffset + offset].task.index;
                independent = independent && !reachable[member][other.task.index] && !reachable[other.task.index][member];
            }
            if(independent)
                overlap += QueueCostWeight(view.taskAt(other.task.index).scheduling.cost);
        }
        return static_cast<i32>(overlap);
    };
    const auto expectScores = [&](){
        for(const auto& candidate : queues){
            for(usize row = 0u; row < assignments.size(); ++row){
                const auto& task = assignments[row].task;
                const Graphics::GpuQueueAssignmentScore score = BuildQueueAssignmentScore(
                    view,
                    analysis,
                    assignments,
                    indices,
                    topology,
                    reachability,
                    scoringData,
                    view.taskAt(task.index),
                    candidate
                );
                GpuTaskQueuePlacementGroup singleton;
                singleton.assignmentOffset = row;
                singleton.assignmentCount = 1u;
                EXPECT_EQ(score.overlap, referenceOverlap(singleton, candidate.id));
            }
            for(const auto& group : groups){
                const Graphics::GpuQueueAssignmentScore score = BuildQueuePlacementGroupScore(
                    view,
                    analysis,
                    assignments,
                    indices,
                    topology,
                    reachability,
                    scoringData,
                    group,
                    candidate
                );
                EXPECT_EQ(score.overlap, referenceOverlap(group, candidate.id));
            }
        }
    };
    expectScores();
    // Change all three queue masks incrementally, including tasks in each of the three bitset words.
    for(usize row = 0u; row < assignments.size(); row += 5u){
        auto& assignment = assignments[row];
        const auto destination = queues[(row + 1u) % LengthOf(queues)].id;
        scoringData.updateAssignmentLoads(assignment.task, assignment.queue, destination);
        assignment.queue = destination;
    }
    expectScores();
    Swap(queues[0u], queues[2u]);
    scoringData.rebuildAssignmentLoads(assignments, topology);
    expectScores();
    Graphics::GraphicsVector<Graphics::GpuTaskQueueAssignment> empty(testArena.arena);
    scoringData.rebuildAssignmentLoads(empty, topology);
    EXPECT_EQ(scoringData.m_totalAssignedCost, 0u);
    scoringData.rebuildAssignmentLoads(assignments, topology);
    expectScores();
}


void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

void BenchmarkPartialDag(const usize taskCount, const Shape::Enum shape){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    DeclareGraph(graph, taskCount, shape);
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
    ASSERT_EQ(view.taskCount(), taskCount);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    const Graphics::GpuTaskGraphCompiler compiler;
    u64 samples[5u] = {};
    usize scratchBytes = 0u;
    for(usize sample = 0u; sample < LengthOf(samples) + 1u; ++sample){
        Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
        const Timer begin = TimerNow();
        const bool assigned = compiler.assignQueues(view, analysis, topology, assignments, scratch);
        const u64 elapsed = DurationInNS<u64>(TimerNow(), begin);
        ASSERT_TRUE(assigned);
        ASSERT_TRUE(assignments.validFor(view));
        if(sample != 0u)
            samples[sample - 1u] = elapsed;
        scratchBytes = scratch.memoryStats().peakUsedBytes;
    }
    Sort(samples, samples + LengthOf(samples));
    RecordUnsignedProperty(NotNull<const char*>("median_assignment_ns"), samples[LengthOf(samples) / 2u]);
    RecordUnsignedProperty(NotNull<const char*>("minimum_assignment_ns"), samples[0u]);
    RecordUnsignedProperty(NotNull<const char*>("scratch_bytes"), scratchBytes);
    RecordUnsignedProperty(NotNull<const char*>("task_count"), taskCount);
    RecordUnsignedProperty(NotNull<const char*>("edge_count"), analysis.schedulingEdges().size());
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        const auto* assignment = assignments.find(view.taskAt(taskIndex).id);
        ASSERT_NE(assignment, nullptr);
        const auto* queue = FindPhysicalQueueInfo(topology, assignment->queue);
        ASSERT_NE(queue, nullptr);
        EXPECT_TRUE(IsLegalQueueAssignmentCandidate(view, topology, view.taskAt(taskIndex), *queue));
    }
}


TEST(GpuTaskQueueScoring, PartialDagMultiwordScoresPreserveIndependentCostsAfterMoves){
    constexpr Shape::Enum s_Shapes[] = { Shape::OneEdge, Shape::DisjointPairs, Shape::ConnectedBranches, Shape::DenseLayers, Shape::MergedPairs };
    for(const Shape::Enum shape : s_Shapes)
        CheckIndexedScores(shape);
}

TEST(GpuTaskQueueScoring, PartialAssignmentsExcludeRelatedTasksWithoutRequiringOwnAssignment){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    DeclareGraph(graph, 4u, Shape::OneEdge);
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    GpuTaskSchedulingReachability reachability(scratch);
    ASSERT_TRUE(BuildGpuTaskSchedulingReachability(view, analysis, reachability));
    GpuTaskQueueScoringData scoringData(view, analysis, reachability, {}, scratch);
    Graphics::GraphicsVector<Graphics::GpuTaskQueueAssignment> assignments(testArena.arena);
    Graphics::GraphicsVector<u32> indices(4u, Limit<u32>::s_Max, testArena.arena);
    constexpr usize s_AssignedTasks[] = { 0u, 3u };
    for(const usize taskIndex : s_AssignedTasks){
        indices[taskIndex] = static_cast<u32>(assignments.size());
        assignments.push_back({
            .task = view.taskAt(taskIndex).id,
            .initialQueue = queues[1u].id,
            .queue = queues[1u].id,
            .score = {},
        });
    }
    scoringData.rebuildAssignmentLoads(assignments, topology);
    const auto score = [&](const usize taskIndex, const GpuTaskQueueScoreExclusions exclusions = {}){
        return BuildQueueAssignmentScore(
            view,
            analysis,
            assignments,
            indices,
            topology,
            reachability,
            scoringData,
            view.taskAt(taskIndex),
            queues[0u],
            exclusions
        );
    };
    EXPECT_EQ(score(1u).overlap, 8); // Assigned producer zero is related; task three is independent.
    EXPECT_EQ(score(2u).overlap, 9); // Both assigned tasks are independent of this absent task.
    EXPECT_EQ(score(2u, { .assignmentOffset = 1u, .assignmentCount = 1u, .totalCost = 8u }).overlap, 1);
}


TEST(GpuTaskQueueScoring, OrderedAndUnusedReachabilityAvoidMaskAllocationsAcrossQueueLoadChanges){
    constexpr usize s_TaskCount = 129u;
    constexpr bool s_BuildReachabilityCases[] = { false, true };
    for(const bool buildReachability : s_BuildReachabilityCases){
        SCOPED_TRACE(buildReachability);
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::GpuTaskId preceding;
        for(usize index = 0u; index < s_TaskCount; ++index){
            char identityText[32u] = {};
            Graphics::GpuTaskSchedulingHint scheduling;
            scheduling.cost = Graphics::GpuTaskCostHint::Large;
            preceding = AddTaskWithCommands(
                graph,
                DeriveName(Name("tests/queue_no_masks/"), FormatDecimal(index, identityText)),
                "Ordered Queue Task",
                ComputeCommands(),
                scheduling,
                {},
                preceding.valid() ? &preceding : nullptr,
                preceding.valid() ? 1u : 0u
            );
            ASSERT_TRUE(preceding.valid());
        }
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(graph, analysis));
        const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
        Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
        GpuTaskSchedulingReachability reachability(scratch);
        if(buildReachability){
            ASSERT_TRUE(BuildGpuTaskSchedulingReachability(view, analysis, reachability));
            ASSERT_FALSE(reachability.mayContainIndependentTasks());
        }
        const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue(77u) };
        const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = buildReachability ? 2u : 1u };
        GpuTaskQueueScoringData scoringData(view, analysis, reachability, {}, scratch);
        Graphics::GraphicsVector<Graphics::GpuTaskQueueAssignment> assignments(testArena.arena);
        Graphics::GraphicsVector<u32> indices(testArena.arena);
        assignments.reserve(s_TaskCount);
        indices.reserve(s_TaskCount);
        for(usize index = 0u; index < s_TaskCount; ++index){
            const auto queue = queues[index % topology.queueCount].id;
            assignments.push_back({ .task = view.taskAt(index).id, .initialQueue = queue, .queue = queue, .score = {} });
            indices.push_back(static_cast<u32>(index));
        }
        const auto expectScoresWithoutMasks = [&](){
            // Zero capacity proves neither optional index allocated storage, rather than merely clearing its contents.
            EXPECT_EQ(scoringData.m_taskCostGroups.capacity(), 0u);
            EXPECT_EQ(scoringData.m_assignedCostWords.capacity(), 0u);
            for(usize candidateIndex = 0u; candidateIndex < topology.queueCount; ++candidateIndex){
                const auto& candidate = queues[candidateIndex];
                for(usize index = 0u; index < s_TaskCount; ++index){
                    i32 expectedLoad = 0;
                    for(usize other = 0u; other < s_TaskCount; ++other){
                        if(other != index && assignments[other].queue == candidate.id)
                            expectedLoad += 8;
                    }
                    const auto score = BuildQueueAssignmentScore(
                        view,
                        analysis,
                        assignments,
                        indices,
                        topology,
                        reachability,
                        scoringData,
                        view.taskAt(index),
                        candidate
                    );
                    EXPECT_EQ(score.queueLoad, expectedLoad);
                    EXPECT_EQ(score.overlap, 0);
                    EXPECT_EQ(score.incomingCrossings, index != 0u && assignments[index - 1u].queue != candidate.id ? 1 : 0);
                    EXPECT_EQ(
                        score.outgoingCrossings,
                        index + 1u < s_TaskCount && assignments[index + 1u].queue != candidate.id ? 1 : 0
                    );
                    EXPECT_EQ(score.ownershipTransfers, 0);
                }
            }
        };
        scoringData.rebuildAssignmentLoads(assignments, topology);
        expectScoresWithoutMasks();
        auto& moved = assignments[s_TaskCount / 2u];
        const auto destination = queues[topology.queueCount - 1u].id;
        scoringData.updateAssignmentLoads(moved.task, moved.queue, destination);
        moved.queue = destination;
        expectScoresWithoutMasks();
        scoringData.rebuildAssignmentLoads(assignments, topology);
        expectScoresWithoutMasks();
    }
}


TEST(GpuTaskQueueScoring, DISABLED_OneEdgeBenchmark1024Tasks){
    BenchmarkPartialDag(1024u, Shape::OneEdge);
}

TEST(GpuTaskQueueScoring, DISABLED_OneEdgeBenchmark4096Tasks){
    BenchmarkPartialDag(4096u, Shape::OneEdge);
}

TEST(GpuTaskQueueScoring, DISABLED_DisjointPairsBenchmark1024Tasks){
    BenchmarkPartialDag(1024u, Shape::DisjointPairs);
}

TEST(GpuTaskQueueScoring, DISABLED_DisjointPairsBenchmark4096Tasks){
    BenchmarkPartialDag(4096u, Shape::DisjointPairs);
}

TEST(GpuTaskQueueScoring, DISABLED_ConnectedBranchesBenchmark1024Tasks){
    BenchmarkPartialDag(1024u, Shape::ConnectedBranches);
}

TEST(GpuTaskQueueScoring, DISABLED_ConnectedBranchesBenchmark4096Tasks){
    BenchmarkPartialDag(4096u, Shape::ConnectedBranches);
}

TEST(GpuTaskQueueScoring, DISABLED_DenseLayersBenchmark1024Tasks){
    BenchmarkPartialDag(1024u, Shape::DenseLayers);
}

TEST(GpuTaskQueueScoring, DISABLED_DenseLayersBenchmark4096Tasks){
    BenchmarkPartialDag(4096u, Shape::DenseLayers);
}

TEST(GpuTaskQueueScoring, DISABLED_MergedPairsBenchmark1024Tasks){
    BenchmarkPartialDag(1024u, Shape::MergedPairs);
}

TEST(GpuTaskQueueScoring, DISABLED_MergedPairsBenchmark4096Tasks){
    BenchmarkPartialDag(4096u, Shape::MergedPairs);
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

