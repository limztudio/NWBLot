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
        ReversedPairs,
        Chains32,
        InterleavedChains32,
        ConnectedBranches,
        DenseLayers,
        TwoDenseComponents,
        FanOut,
        FanIn,
        MergedPairs,
        Independent,
        TotalOrder,
    };
};


void DeclareGraph(
    Graphics::GpuTaskGraph& graph,
    const usize taskCount,
    const Shape::Enum shape,
    Graphics::Alloc::ScratchArena& scratch){
    constexpr usize s_LayerSize = 16u;
    constexpr usize s_ChainSize = 32u;
    const bool interleaved = shape == Shape::InterleavedChains32;
    if(interleaved)
        ASSERT_EQ(taskCount % s_ChainSize, 0u);
    u64 generation = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
        generation = view.generation();
    }
    Vector<Graphics::GpuTaskId, Graphics::Alloc::ScratchArena> dependencies(scratch);
    dependencies.reserve(taskCount);
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        const usize chainCount = taskCount / s_ChainSize;
        const usize logicalIndex = interleaved ? taskIndex % chainCount * s_ChainSize + taskIndex / chainCount : taskIndex;
        dependencies.clear();
        const auto addDependency = [&](const usize index){
            const usize declarationIndex = interleaved ? index % s_ChainSize * chainCount + index / s_ChainSize : index;
            dependencies.push_back({ .generation = generation, .index = static_cast<u32>(declarationIndex) });
        };
        if(shape == Shape::ReversedPairs && taskIndex % 2u != 0u && taskIndex + 1u < taskCount)
            addDependency(taskIndex + 1u);
        else if(
            (shape == Shape::OneEdge && taskIndex == 1u)
            || ((shape == Shape::DisjointPairs || shape == Shape::MergedPairs) && taskIndex % 2u != 0u)
            || (shape == Shape::TotalOrder && taskIndex != 0u)
        )
            addDependency(taskIndex - 1u);
        else if((shape == Shape::Chains32 || interleaved) && logicalIndex % s_ChainSize != 0u)
            addDependency(logicalIndex - 1u);
        else if(shape == Shape::ConnectedBranches && taskIndex != 0u)
            addDependency((taskIndex - 1u) / 2u);
        else if(shape == Shape::FanOut && taskIndex != 0u)
            addDependency(0u);
        else if(shape == Shape::FanIn && taskIndex + 1u == taskCount){
            for(usize source = 0u; source < taskIndex; ++source)
                addDependency(source);
        }
        else if(shape == Shape::DenseLayers || shape == Shape::TwoDenseComponents){
            const usize componentSize = shape == Shape::TwoDenseComponents ? taskCount / 2u : taskCount;
            const usize local = taskIndex % componentSize;
            if(local >= s_LayerSize){
                const usize previousLayer = taskIndex - local + (local / s_LayerSize - 1u) * s_LayerSize;
                for(usize offset = 0u; offset < s_LayerSize; ++offset)
                    addDependency(previousLayer + offset);
            }
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
            dependencies.data(),
            dependencies.size()
        );
        ASSERT_TRUE(task.valid());
    }
}


template<usize taskCount = 130u>
void CheckIndexedScores(const Shape::Enum shape){
    constexpr usize s_TaskCount = taskCount;
    SCOPED_TRACE(shape);
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::Alloc::ScratchArena declarationScratch(s_TaskGraphScratchArena);
    DeclareGraph(graph, s_TaskCount, shape, declarationScratch);
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
    Vector<Array<bool, s_TaskCount>, Graphics::Alloc::ScratchArena> reachable(s_TaskCount, scratch);
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
    // Change all three queue masks incrementally, including tasks across bitset words.
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


void RecordUnsignedProperty(const AStringView key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(AInteropString(key), text);
}

void BenchmarkPartialDag(const usize taskCount, const Shape::Enum shape){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::Alloc::ScratchArena declarationScratch(s_TaskGraphScratchArena);
    DeclareGraph(graph, taskCount, shape, declarationScratch);
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
    RecordUnsignedProperty("median_assignment_ns", samples[LengthOf(samples) / 2u]);
    RecordUnsignedProperty("minimum_assignment_ns", samples[0u]);
    RecordUnsignedProperty("scratch_bytes", scratchBytes);
    RecordUnsignedProperty("task_count", taskCount);
    RecordUnsignedProperty("edge_count", analysis.schedulingEdges().size());
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

TEST(GpuTaskQueueScoring, CompactedPartialDagScoresPreserveMixedCostsAcrossMovesAndRebuilds){
    constexpr Shape::Enum s_Shapes[] = {
        Shape::OneEdge, Shape::DisjointPairs, Shape::ReversedPairs, Shape::DenseLayers, Shape::MergedPairs,
    };
    for(const Shape::Enum shape : s_Shapes)
        CheckIndexedScores<513u>(shape);
}

TEST(GpuTaskQueueScoring, SparsePartialRowsAndDenseThresholdsBoundPeakScratch){
    struct StorageCase{
        usize m_taskCount;
        Shape::Enum m_shape;
    };
    constexpr StorageCase s_Cases[] = {
        { 16u, Shape::DisjointPairs }, { 64u, Shape::DisjointPairs },
        { 130u, Shape::DisjointPairs }, { 256u, Shape::DisjointPairs }, { 257u, Shape::DisjointPairs },
        { 382u, Shape::FanOut }, { 382u, Shape::FanIn }, { 513u, Shape::ReversedPairs },
        { 4032u, Shape::DenseLayers }, { 4033u, Shape::DenseLayers }, { 4097u, Shape::DenseLayers },
    };
    constexpr usize s_BitsPerWord = sizeof(u64) * 8u;
    constexpr usize s_BoundsBudgetDivisor = 32u;
    constexpr usize s_ReachabilityVectorCount = 4u;
    usize emptyVectorBookkeepingBytes = 0u;
    {
        // Debug STL modes can allocate container bookkeeping through the arena even for empty vectors.
        Graphics::Alloc::ScratchArena bookkeepingScratch(s_TaskGraphScratchArena);
        const Vector<u64, Graphics::Alloc::ScratchArena> emptyVector(bookkeepingScratch);
        emptyVectorBookkeepingBytes = bookkeepingScratch.memoryStats().usedBytes;
    }
    const usize reachabilityBookkeepingBytes = s_ReachabilityVectorCount * emptyVectorBookkeepingBytes;
    for(const auto& testCase : s_Cases){
        SCOPED_TRACE(testCase.m_taskCount);
        SCOPED_TRACE(testCase.m_shape);
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::Alloc::ScratchArena declarationScratch(s_TaskGraphScratchArena);
        DeclareGraph(graph, testCase.m_taskCount, testCase.m_shape, declarationScratch);
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(graph, analysis));
        const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
        Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
        GpuTaskSchedulingReachability reachability(scratch);
        ASSERT_EQ(scratch.memoryStats().usedBytes, reachabilityBookkeepingBytes);
        ASSERT_TRUE(BuildGpuTaskSchedulingReachability(view, analysis, reachability));
        const usize wordsPerRow = (testCase.m_taskCount + s_BitsPerWord - 1u) / s_BitsPerWord;
        const usize matrixBytes = testCase.m_taskCount * wordsPerRow * sizeof(u64);
        const usize denseBytes = matrixBytes + testCase.m_taskCount * (sizeof(u32) + 2u * sizeof(usize));
        if(testCase.m_shape == Shape::ReversedPairs){
            // A partial last tile and backward cross-word edges must not allocate the full square matrix.
            EXPECT_LT(scratch.memoryStats().peakUsedBytes, matrixBytes);
            EXPECT_TRUE(reachability.reaches(view.taskAt(512u).id, view.taskAt(511u).id));
            EXPECT_TRUE(reachability.reaches(view.taskAt(64u).id, view.taskAt(63u).id));
            EXPECT_FALSE(reachability.reaches(view.taskAt(63u).id, view.taskAt(64u).id));
        }
        else{
            const usize boundsBudget = wordsPerRow >= s_BitsPerWord ? matrixBytes / s_BoundsBudgetDivisor : 0u;
            // Dense rows retain one temporary bounds vector at this threshold.
            const usize boundsBookkeepingBytes = wordsPerRow >= s_BitsPerWord ? emptyVectorBookkeepingBytes : 0u;
            const usize peakBookkeepingBytes = reachabilityBookkeepingBytes + boundsBookkeepingBytes;
            EXPECT_LE(scratch.memoryStats().peakUsedBytes, denseBytes + boundsBudget + peakBookkeepingBytes);
        }
    }
}

TEST(GpuTaskQueueScoring, RebuildingRelationsClearsCompactedDenseOrderedAndFailedState){
    constexpr usize s_TaskCount = 513u;
    constexpr usize s_LayerSize = 16u;
    constexpr Shape::Enum s_Shapes[] = {
        Shape::ReversedPairs, Shape::DenseLayers, Shape::OneEdge,
        Shape::Independent, Shape::TotalOrder, Shape::ReversedPairs,
    };
    TestArena testArena;
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    GpuTaskSchedulingReachability reachability(scratch);
    Graphics::GpuTaskId previousTask;
    for(const Shape::Enum shape : s_Shapes){
        SCOPED_TRACE(shape);
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::Alloc::ScratchArena declarationScratch(s_TaskGraphScratchArena);
        DeclareGraph(graph, s_TaskCount, shape, declarationScratch);
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(graph, analysis));
        const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
        ASSERT_TRUE(BuildGpuTaskSchedulingReachability(view, analysis, reachability));
        const auto expectedReach = [&](const usize source, const usize destination){
            if(source == destination)
                return false;
            switch(shape){
            case Shape::ReversedPairs: return source != 0u && source % 2u == 0u && destination + 1u == source;
            case Shape::DenseLayers: return source / s_LayerSize < destination / s_LayerSize;
            case Shape::OneEdge: return source == 0u && destination == 1u;
            case Shape::TotalOrder: return source < destination;
            default: return false;
            }
        };
        for(usize source = 0u; source < s_TaskCount; ++source){
            for(usize destination = 0u; destination < s_TaskCount; ++destination){
                const auto from = view.taskAt(source).id;
                const auto to = view.taskAt(destination).id;
                EXPECT_EQ(reachability.reaches(from, to), expectedReach(source, destination));
                EXPECT_EQ(
                    reachability.transitivelyIndependent(from, to),
                    source != destination && !expectedReach(source, destination) && !expectedReach(destination, source)
                );
            }
        }
        EXPECT_FALSE(reachability.reaches(previousTask, view.taskAt(0u).id));
        EXPECT_FALSE(reachability.transitivelyIndependent(previousTask, view.taskAt(0u).id));
        previousTask = view.taskAt(0u).id;
    }
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::Alloc::ScratchArena declarationScratch(s_TaskGraphScratchArena);
    DeclareGraph(graph, s_TaskCount, Shape::ReversedPairs, declarationScratch);
    const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
    Graphics::GpuTaskGraphAnalysis invalidAnalysis(testArena.arena);
    EXPECT_FALSE(BuildGpuTaskSchedulingReachability(view, invalidAnalysis, reachability));
    EXPECT_FALSE(reachability.reaches(view.taskAt(512u).id, view.taskAt(511u).id));
    EXPECT_FALSE(reachability.transitivelyIndependent(view.taskAt(0u).id, view.taskAt(1u).id));
}

TEST(GpuTaskQueueScoring, PartialAssignmentsExcludeRelatedTasksWithoutRequiringOwnAssignment){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::Alloc::ScratchArena declarationScratch(s_TaskGraphScratchArena);
    DeclareGraph(graph, 4u, Shape::OneEdge, declarationScratch);
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


TEST(GpuTaskQueueScoring, DISABLED_DisjointPairsBenchmark16384Tasks){
    BenchmarkPartialDag(16384u, Shape::DisjointPairs);
}

TEST(GpuTaskQueueScoring, DISABLED_ReversedPairsBenchmark16384Tasks){
    BenchmarkPartialDag(16384u, Shape::ReversedPairs);
}

TEST(GpuTaskQueueScoring, DISABLED_Chains32Benchmark16384Tasks){
    BenchmarkPartialDag(16384u, Shape::Chains32);
}

TEST(GpuTaskQueueScoring, DISABLED_InterleavedChains32Benchmark16384Tasks){
    BenchmarkPartialDag(16384u, Shape::InterleavedChains32);
}

TEST(GpuTaskQueueScoring, DISABLED_TwoDenseComponentsBenchmark4096Tasks){
    BenchmarkPartialDag(4096u, Shape::TwoDenseComponents);
}

TEST(GpuTaskQueueScoring, DISABLED_FanOutBenchmark1024Tasks){
    BenchmarkPartialDag(1024u, Shape::FanOut);
}

TEST(GpuTaskQueueScoring, DISABLED_FanOutBenchmark16384Tasks){
    BenchmarkPartialDag(16384u, Shape::FanOut);
}

TEST(GpuTaskQueueScoring, DISABLED_FanInBenchmark1024Tasks){
    BenchmarkPartialDag(1024u, Shape::FanIn);
}

TEST(GpuTaskQueueScoring, DISABLED_FanInBenchmark16384Tasks){
    BenchmarkPartialDag(16384u, Shape::FanIn);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

