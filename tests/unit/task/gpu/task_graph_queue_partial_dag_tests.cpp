// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_queue_partial_dag_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;
using namespace Core::GpuTaskGraphCompilerDetail;

namespace Shape{
    enum Enum : u8{
        OneEdge,
        DisjointPairs,
        ReversedPairs,
        ConnectedBranches,
        DenseLayers,
        FanOut,
        FanIn,
        MergedPairs,
        Independent,
        TotalOrder,
    };
};


void DeclareGraph(Core::GpuTaskGraph& graph, const usize taskCount, const Shape::Enum shape, Core::Alloc::ScratchArena& scratch){
    constexpr usize s_LayerSize = 16u;
    u64 generation = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView view(graph);
        generation = view.generation();
    }
    Vector<Core::GpuTaskId, Core::Alloc::ScratchArena> dependencies(scratch);
    dependencies.reserve(taskCount);
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        dependencies.clear();
        const auto addDependency = [&](const usize index)noexcept{
            dependencies.push_back({ .generation = generation, .index = static_cast<u32>(index) });
        };
        if(shape == Shape::ReversedPairs && taskIndex % 2u != 0u && taskIndex + 1u < taskCount)
            addDependency(taskIndex + 1u);
        else if((shape == Shape::OneEdge && taskIndex == 1u) || ((shape == Shape::DisjointPairs || shape == Shape::MergedPairs) && taskIndex % 2u != 0u) || (shape == Shape::TotalOrder && taskIndex != 0u))
            addDependency(taskIndex - 1u);
        else if(shape == Shape::ConnectedBranches && taskIndex != 0u)
            addDependency((taskIndex - 1u) / 2u);
        else if(shape == Shape::FanOut && taskIndex != 0u)
            addDependency(0u);
        else if(shape == Shape::FanIn && taskIndex + 1u == taskCount){
            for(usize source = 0u; source < taskIndex; ++source)
                addDependency(source);
        }
        else if(shape == Shape::DenseLayers && taskIndex >= s_LayerSize){
            const usize previousLayer = (taskIndex / s_LayerSize - 1u) * s_LayerSize;
            for(usize offset = 0u; offset < s_LayerSize; ++offset)
                addDependency(previousLayer + offset);
        }
        Core::GpuTaskSchedulingHint scheduling;
        scheduling.cost = static_cast<Core::GpuTaskCostHint::Enum>(taskIndex % Core::GpuTaskCostHint::Count);
        scheduling.mergeWithPrevious = shape == Shape::MergedPairs && taskIndex % 2u != 0u;
        const b8 graphicsTask = shape == Shape::MergedPairs ? (taskIndex / 2u) % 2u == 0u : taskIndex % 2u == 0u;
        const auto derivedNameResult = IndexedName(Name("tests/queue_partial_dag/"), taskIndex);
        ASSERT_TRUE(derivedNameResult);
        const auto taskResult = AddTaskWithCommands(*derivedNameResult, "Partial DAG Task", graphicsTask ? GraphicsCommands() : ComputeCommands(), scheduling, {}, Span(dependencies.data(), dependencies.size()), graph);
        ASSERT_TRUE(taskResult);
        const Core::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());
    }
}


template<usize TaskCount = 130u>
void CheckIndexedScores(const Shape::Enum shape){
    constexpr usize s_TaskCount = TaskCount;
    SCOPED_TRACE(shape);
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::Alloc::ScratchArena declarationScratch(s_TaskGraphScratchArena);
    ASSERT_NO_FATAL_FAILURE(DeclareGraph(graph, s_TaskCount, shape, declarationScratch));
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, declarationScratch, analysis));
    Core::GpuPhysicalQueueInfo auxiliary = GraphicsQueue(311u);
    auxiliary.queueIndex = 1u;
    Array<Core::GpuPhysicalQueueInfo, 3u> queues = { GraphicsQueue(17u), DedicatedComputeQueue(29u), auxiliary };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues) };
    const Core::GpuTaskGraph::DeclarationReadView view(graph);
    ASSERT_EQ(view.taskCount(), s_TaskCount);
    Core::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    GpuTaskSchedulingReachability reachability(scratch);
    ASSERT_TRUE(BuildGpuTaskSchedulingReachability(view, analysis, reachability));
    GpuTaskQueueScoringData scoringData(view, analysis, reachability, {}, scratch);
    Core::GraphicsVector<Core::GpuTaskQueueAssignment> assignments(testArena.arena);
    Core::GraphicsVector<u32> indices(s_TaskCount, Limit<u32>::s_Max, testArena.arena);
    for(const Core::GpuTaskId task : analysis.topologicalOrder()){
        indices[task.index] = static_cast<u32>(assignments.size());
        const auto queue = queues[task.index % LengthOf(queues)].id;
        assignments.push_back({ .task = task, .initialQueue = queue, .queue = queue, .score = {} });
    }
    scoringData.rebuildAssignmentLoads(assignments, topology);
    Vector<GpuTaskQueuePlacementGroup, Core::Alloc::ScratchArena> groups(scratch);
    ASSERT_TRUE(BuildQueuePlacementGroups(view, analysis, topology, scratch, groups));

    // Scalar Floyd-Warshall is independent of packed rows, word bounds, and queue-cost membership.
    Vector<Array<b8, s_TaskCount>, Core::Alloc::ScratchArena> reachable(s_TaskCount, scratch);
    for(const auto& edge : analysis.schedulingEdges())
        reachable[edge.producer.index][edge.consumer.index] = true;
    for(usize intermediate = 0u; intermediate < s_TaskCount; ++intermediate)
        for(usize source = 0u; source < s_TaskCount; ++source)
            for(usize destination = 0u; destination < s_TaskCount; ++destination)
                reachable[source][destination] = reachable[source][destination] || (reachable[source][intermediate] && reachable[intermediate][destination]);
    for(usize source = 0u; source < s_TaskCount; ++source){
        for(usize destination = 0u; destination < s_TaskCount; ++destination){
            EXPECT_EQ(reachability.reaches(view.taskAt(source).id, view.taskAt(destination).id), reachable[source][destination]);
            EXPECT_EQ(reachability.transitivelyIndependent(view.taskAt(source).id, view.taskAt(destination).id), source != destination && !reachable[source][destination] && !reachable[destination][source]);
        }
    }
    const auto referenceOverlap = [&](const GpuTaskQueuePlacementGroup& group, const Core::GpuPhysicalQueueId queue)noexcept{
        u64 overlap = 0u;
        for(usize row = 0u; row < assignments.size(); ++row){
            if(row >= group.assignmentOffset && row - group.assignmentOffset < group.assignmentCount)
                continue;
            const auto& other = assignments[row];
            if(other.queue == queue)
                continue;
            b8 independent = true;
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
                const Core::GpuQueueAssignmentScore score = BuildQueueAssignmentScore(view, analysis, assignments, indices, topology, reachability, scoringData, view.taskAt(task.index), candidate);
                GpuTaskQueuePlacementGroup singleton;
                singleton.assignmentOffset = row;
                singleton.assignmentCount = 1u;
                EXPECT_EQ(score.overlap, referenceOverlap(singleton, candidate.id));
            }
            for(const auto& group : groups){
                const Core::GpuQueueAssignmentScore score = BuildQueuePlacementGroupScore(view, analysis, assignments, indices, topology, reachability, scoringData, group, candidate);
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
    Core::GraphicsVector<Core::GpuTaskQueueAssignment> empty(testArena.arena);
    scoringData.rebuildAssignmentLoads(empty, topology);
    EXPECT_EQ(scoringData.totalAssignedCost, 0u);
    scoringData.rebuildAssignmentLoads(assignments, topology);
    expectScores();
}


TEST(GpuTaskQueueScoring, PartialDagMultiwordScoresPreserveIndependentCostsAfterMoves){
    constexpr Array<Shape::Enum, 5u> s_Shapes = { Shape::OneEdge, Shape::DisjointPairs, Shape::ConnectedBranches, Shape::DenseLayers, Shape::MergedPairs };
    for(const Shape::Enum shape : s_Shapes)
        ASSERT_NO_FATAL_FAILURE(CheckIndexedScores(shape));
}

TEST(GpuTaskQueueScoring, CompactedPartialDagScoresPreserveMixedCostsAcrossMovesAndRebuilds){
    constexpr Array<Shape::Enum, 5u> s_Shapes = { Shape::OneEdge, Shape::DisjointPairs, Shape::ReversedPairs, Shape::DenseLayers, Shape::MergedPairs, };
    for(const Shape::Enum shape : s_Shapes)
        ASSERT_NO_FATAL_FAILURE(CheckIndexedScores<513u>(shape));
}

TEST(GpuTaskQueueScoring, SparsePartialRowsAndDenseThresholdsBoundPeakScratch){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    struct StorageCase{
        usize taskCount;
        Shape::Enum shape;
    };
    constexpr Array<StorageCase, 11u> s_Cases = {{
        { 16u, Shape::DisjointPairs }, { 64u, Shape::DisjointPairs },
        { 130u, Shape::DisjointPairs }, { 256u, Shape::DisjointPairs }, { 257u, Shape::DisjointPairs },
        { 382u, Shape::FanOut }, { 382u, Shape::FanIn }, { 513u, Shape::ReversedPairs },
        { 4032u, Shape::DenseLayers }, { 4033u, Shape::DenseLayers }, { 4097u, Shape::DenseLayers },
    }};
    constexpr usize s_BitsPerWord = sizeof(u64) * 8u;
    constexpr usize s_BoundsBudgetDivisor = 32u;
    constexpr usize s_ReachabilityVectorCount = 4u;
    usize emptyVectorBookkeepingBytes = 0u;
    {
        // Debug STL modes can allocate container bookkeeping through the arena even for empty vectors.
        Core::Alloc::ScratchArena bookkeepingScratch(s_TaskGraphScratchArena);
        const Vector<u64, Core::Alloc::ScratchArena> emptyVector(bookkeepingScratch);
        emptyVectorBookkeepingBytes = bookkeepingScratch.memoryStats().usedBytes;
    }
    const usize reachabilityBookkeepingBytes = s_ReachabilityVectorCount * emptyVectorBookkeepingBytes;
    for(const auto& testCase : s_Cases){
        SCOPED_TRACE(testCase.taskCount);
        SCOPED_TRACE(testCase.shape);
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        Core::Alloc::ScratchArena declarationScratch(s_TaskGraphScratchArena);
        ASSERT_NO_FATAL_FAILURE(DeclareGraph(graph, testCase.taskCount, testCase.shape, declarationScratch));
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
        const Core::GpuTaskGraph::DeclarationReadView view(graph);
        Core::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
        GpuTaskSchedulingReachability reachability(scratch);
        ASSERT_EQ(scratch.memoryStats().usedBytes, reachabilityBookkeepingBytes);
        ASSERT_TRUE(BuildGpuTaskSchedulingReachability(view, analysis, reachability));
        const usize wordsPerRow = (testCase.taskCount + s_BitsPerWord - 1u) / s_BitsPerWord;
        const usize matrixBytes = testCase.taskCount * wordsPerRow * sizeof(u64);
        const usize denseBytes = matrixBytes + testCase.taskCount * (sizeof(u32) + 2u * sizeof(usize));
        if(testCase.shape == Shape::ReversedPairs){
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
    constexpr Array<Shape::Enum, 6u> s_Shapes = { Shape::ReversedPairs, Shape::DenseLayers, Shape::OneEdge, Shape::Independent, Shape::TotalOrder, Shape::ReversedPairs, };
    TestArena testArena;
    Core::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    GpuTaskSchedulingReachability reachability(scratch);
    Core::GpuTaskId previousTask;
    for(const Shape::Enum shape : s_Shapes){
        SCOPED_TRACE(shape);
        Core::GpuTaskGraph graph(testArena.arena);
        Core::Alloc::ScratchArena declarationScratch(s_TaskGraphScratchArena);
        ASSERT_NO_FATAL_FAILURE(DeclareGraph(graph, s_TaskCount, shape, declarationScratch));
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(graph, scratch, analysis));
        const Core::GpuTaskGraph::DeclarationReadView view(graph);
        ASSERT_TRUE(BuildGpuTaskSchedulingReachability(view, analysis, reachability));
        const auto expectedReach = [&](const usize source, const usize destination)noexcept{
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
                EXPECT_EQ(reachability.transitivelyIndependent(from, to), source != destination && !expectedReach(source, destination) && !expectedReach(destination, source));
            }
        }
        EXPECT_FALSE(reachability.reaches(previousTask, view.taskAt(0u).id));
        EXPECT_FALSE(reachability.transitivelyIndependent(previousTask, view.taskAt(0u).id));
        previousTask = view.taskAt(0u).id;
    }
    Core::GpuTaskGraph graph(testArena.arena);
    Core::Alloc::ScratchArena declarationScratch(s_TaskGraphScratchArena);
    ASSERT_NO_FATAL_FAILURE(DeclareGraph(graph, s_TaskCount, Shape::ReversedPairs, declarationScratch));
    const Core::GpuTaskGraph::DeclarationReadView view(graph);
    Core::GpuTaskGraphAnalysis invalidAnalysis(testArena.arena);
    EXPECT_FALSE(BuildGpuTaskSchedulingReachability(view, invalidAnalysis, reachability));
    EXPECT_FALSE(reachability.reaches(view.taskAt(512u).id, view.taskAt(511u).id));
    EXPECT_FALSE(reachability.transitivelyIndependent(view.taskAt(0u).id, view.taskAt(1u).id));
}

TEST(GpuTaskQueueScoring, PartialAssignmentsExcludeRelatedTasksWithoutRequiringOwnAssignment){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::Alloc::ScratchArena declarationScratch(s_TaskGraphScratchArena);
    ASSERT_NO_FATAL_FAILURE(DeclareGraph(graph, 4u, Shape::OneEdge, declarationScratch));
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, declarationScratch, analysis));
    const Core::GpuTaskGraph::DeclarationReadView view(graph);
    const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), DedicatedComputeQueue() };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues) };
    Core::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    GpuTaskSchedulingReachability reachability(scratch);
    ASSERT_TRUE(BuildGpuTaskSchedulingReachability(view, analysis, reachability));
    GpuTaskQueueScoringData scoringData(view, analysis, reachability, {}, scratch);
    Core::GraphicsVector<Core::GpuTaskQueueAssignment> assignments(testArena.arena);
    Core::GraphicsVector<u32> indices(4u, Limit<u32>::s_Max, testArena.arena);
    constexpr Array<usize, 2u> s_AssignedTasks = { 0u, 3u };
    for(const usize taskIndex : s_AssignedTasks){
        indices[taskIndex] = static_cast<u32>(assignments.size());
        assignments.push_back({ .task = view.taskAt(taskIndex).id, .initialQueue = queues[1u].id, .queue = queues[1u].id, .score = {}, });
    }
    scoringData.rebuildAssignmentLoads(assignments, topology);
    const auto score = [&](const usize taskIndex, const GpuTaskQueueScoreExclusions exclusions = {})noexcept{
        return BuildQueueAssignmentScore(view, analysis, assignments, indices, topology, reachability, scoringData, view.taskAt(taskIndex), queues[0u], exclusions);
    };
    EXPECT_EQ(score(1u).overlap, 8);
    EXPECT_EQ(score(2u).overlap, 9);
    EXPECT_EQ(score(2u, { .assignmentOffset = 1u, .assignmentCount = 1u, .totalCost = 8u }).overlap, 1);
}


TEST(GpuTaskQueueScoring, OrderedAndUnusedReachabilityAvoidMaskAllocationsAcrossQueueLoadChanges){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    constexpr usize s_TaskCount = 129u;
    constexpr Array<b8, 2u> s_BuildReachabilityCases = { false, true };
    for(const b8 buildReachability : s_BuildReachabilityCases){
        SCOPED_TRACE(buildReachability);
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        Core::GpuTaskId preceding;
        for(usize index = 0u; index < s_TaskCount; ++index){
            Core::GpuTaskSchedulingHint scheduling;
            scheduling.cost = Core::GpuTaskCostHint::Large;
            const auto derivedNameResult = IndexedName(Name("tests/queue_no_masks/"), index);
            ASSERT_TRUE(derivedNameResult);
            const auto precedingTaskAdmission = AddTaskWithCommands(*derivedNameResult, "Ordered Queue Task", ComputeCommands(), scheduling, {}, preceding.valid() ? Span(&preceding, 1u) : Span<const Core::GpuTaskId>{}, graph);
            ASSERT_TRUE(precedingTaskAdmission);
            preceding = *precedingTaskAdmission;
            ASSERT_TRUE(preceding.valid());
        }
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
        const Core::GpuTaskGraph::DeclarationReadView view(graph);
        Core::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
        GpuTaskSchedulingReachability reachability(scratch);
        if(buildReachability){
            ASSERT_TRUE(BuildGpuTaskSchedulingReachability(view, analysis, reachability));
            ASSERT_FALSE(reachability.mayContainIndependentTasks());
        }
        const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), DedicatedComputeQueue(77u) };
        const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = buildReachability ? 2u : 1u };
        GpuTaskQueueScoringData scoringData(view, analysis, reachability, {}, scratch);
        Core::GraphicsVector<Core::GpuTaskQueueAssignment> assignments(testArena.arena);
        Core::GraphicsVector<u32> indices(testArena.arena);
        assignments.reserve(s_TaskCount);
        indices.reserve(s_TaskCount);
        for(usize index = 0u; index < s_TaskCount; ++index){
            const auto queue = queues[index % topology.queueCount].id;
            assignments.push_back({ .task = view.taskAt(index).id, .initialQueue = queue, .queue = queue, .score = {} });
            indices.push_back(static_cast<u32>(index));
        }
        const auto expectScoresWithoutMasks = [&](){
            // Zero capacity proves neither optional index allocated storage, rather than merely clearing its contents.
            EXPECT_EQ(scoringData.taskCostGroups.capacity(), 0u);
            EXPECT_EQ(scoringData.assignedCostWords.capacity(), 0u);
            for(usize candidateIndex = 0u; candidateIndex < topology.queueCount; ++candidateIndex){
                const auto& candidate = queues[candidateIndex];
                for(usize index = 0u; index < s_TaskCount; ++index){
                    i32 expectedLoad = 0;
                    for(usize other = 0u; other < s_TaskCount; ++other){
                        if(other != index && assignments[other].queue == candidate.id)
                            expectedLoad += 8;
                    }
                    const auto score = BuildQueueAssignmentScore(view, analysis, assignments, indices, topology, reachability, scoringData, view.taskAt(index), candidate);
                    EXPECT_EQ(score.queueLoad, expectedLoad);
                    EXPECT_EQ(score.overlap, 0);
                    EXPECT_EQ(score.incomingCrossings, index != 0u && assignments[index - 1u].queue != candidate.id ? 1 : 0);
                    EXPECT_EQ(score.outgoingCrossings, index + 1u < s_TaskCount && assignments[index + 1u].queue != candidate.id ? 1 : 0);
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

