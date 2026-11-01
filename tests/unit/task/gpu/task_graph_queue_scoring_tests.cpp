// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <core/task/gpu/compiler_internal.h>

#include <global/text_utils.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_queue_scoring_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct TaskGraphQueueScoringTestsTag>;
using namespace Core::GpuTaskGraphCompilerDetail;

constexpr usize s_TaskCount = 8u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static i32 SaturateReferenceTerm(const u64 value)noexcept{
    return value > static_cast<u64>(Limit<i32>::s_Max) ? Limit<i32>::s_Max : static_cast<i32>(value);
}


TEST(GpuTaskQueueScoring, CachedLoadsAndScoresMatchIndependentReferenceAfterMovesAndRebuilds){
    const Array<b8, 2u> dependencyCases = { false, true };
    const Array<Core::GpuTaskCostHint::Enum, s_TaskCount> hints = {
        Core::GpuTaskCostHint::Tiny, Core::GpuTaskCostHint::Small,
        Core::GpuTaskCostHint::Medium, Core::GpuTaskCostHint::Large,
        Core::GpuTaskCostHint::Tiny, Core::GpuTaskCostHint::Large,
        Core::GpuTaskCostHint::Small, Core::GpuTaskCostHint::Medium,
    };
    const Array<u64, s_TaskCount> costs = { 1u, 2u, 4u, 8u, 1u, 8u, 2u, 4u };
    const Array<Array<u8, 3u>, s_TaskCount> parents = { { {}, { 0u }, {}, { 0u, 2u }, { 1u }, {}, { 3u }, { 4u, 6u, 0u } } };
    const Array<usize, s_TaskCount> parentCounts = { 0u, 1u, 0u, 2u, 1u, 0u, 1u, 3u };
    const Array<usize, s_TaskCount> assignmentOrder = { 5u, 0u, 7u, 2u, 6u, 1u, 4u, 3u };
    const Array<Array<usize, 2u>, 5u> ignoredRanges = { { { 0u, 0u }, { 0u, 1u }, { 3u, 1u }, { 1u, 3u }, { 0u, s_TaskCount } } };
    for(const b8 hasDependencies : dependencyCases){
        SCOPED_TRACE(hasDependencies);
        TestArena testArena;
        Core::Alloc::ScratchArena scratchArena(Name("tests/task/gpu/queue_scoring"));
        Core::GpuTaskGraph graph(testArena.arena);
        Array<Core::GpuTaskId, s_TaskCount> tasks = {};
        for(usize taskIndex = 0u; taskIndex < s_TaskCount; ++taskIndex){
            Array<Core::GpuTaskId, 3u> dependencies = {};
            const usize dependencyCount = hasDependencies ? parentCounts[taskIndex] : 0u;
            for(usize dependencyIndex = 0u; dependencyIndex < dependencyCount; ++dependencyIndex)
                dependencies[dependencyIndex] = tasks[parents[taskIndex][dependencyIndex]];
            Array<char, 32u> indexBuffer = {};
            Core::GpuTaskDesc desc;
            const auto derivedNameResult = DeriveName(Name("tests/queue_scoring/task_"), FormatDecimal(taskIndex, indexBuffer));
            ASSERT_TRUE(derivedNameResult);
            desc
                .setIdentity(*derivedNameResult)
                .setMarkerLabel("Queue Scoring Task")
                .setDependencies(Span(dependencies.data(), dependencyCount))
                .setScheduling({ .cost = hints[taskIndex], .overlapPreferred = taskIndex != 1u, .avoidQueueCrossing = taskIndex == 6u })
            ;
            const auto taskAdmission = graph.addTask(desc, { .requiredCapabilities = Core::GpuQueueCapability::Transfer });
            ASSERT_TRUE(taskAdmission);
            tasks[taskIndex] = *taskAdmission;
            ASSERT_TRUE(tasks[taskIndex].valid());
        }
        const Array<Core::GpuPhysicalQueueInfo, 3u> queues = {
            Core::GpuPhysicalQueueInfo{ .familyIndex = 1u, .id = { .index = 17u, .deviceGeneration = 23u }, .queueClass = Core::CommandQueue::Transfer, .capabilities = Core::GpuQueueCapability::Transfer },
            Core::GpuPhysicalQueueInfo{ .familyIndex = 2u, .id = { .index = 311u, .deviceGeneration = 23u }, .queueClass = Core::CommandQueue::Transfer, .capabilities = Core::GpuQueueCapability::Transfer },
            Core::GpuPhysicalQueueInfo{ .familyIndex = 3u, .id = { .index = 1024u, .deviceGeneration = 23u }, .queueClass = Core::CommandQueue::Transfer, .capabilities = Core::GpuQueueCapability::Transfer },
        };
        const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues) };
        const Array<Core::GpuTaskQueueLoad, 2u> externalLoads = {
            Core::GpuTaskQueueLoad{ .queue = queues[1u].id, .estimatedCost = 13u },
            Core::GpuTaskQueueLoad{ .queue = queues[2u].id, .estimatedCost = Limit<u64>::s_Max },
        };
        Core::GpuTaskGraphQueueAssignmentOptions options;
        options.queueLoads = externalLoads.data();
        options.queueLoadCount = LengthOf(externalLoads);
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Core::GpuTaskGraphCompiler compiler;
        ASSERT_TRUE(compiler.analyze(declarations, scratchArena, analysis));
        GpuTaskSchedulingReachability reachability(scratchArena);
        ASSERT_TRUE(BuildGpuTaskSchedulingReachability(declarations, analysis, reachability));
        GpuTaskQueueScoringData scoringData(declarations, analysis, reachability, options, scratchArena);
        Array<Array<b8, s_TaskCount>, s_TaskCount> reachable = {};
        for(const Core::GpuTaskDependencyEdge& edge : analysis.edges())
            reachable[edge.producer.index][edge.consumer.index] = true;
        // Plain Floyd-Warshall closure is independent of the packed scheduling representation.
        for(usize intermediate = 0u; intermediate < s_TaskCount; ++intermediate)
            for(usize producer = 0u; producer < s_TaskCount; ++producer)
                for(usize consumer = 0u; consumer < s_TaskCount; ++consumer)
                    reachable[producer][consumer] = reachable[producer][consumer] || (reachable[producer][intermediate] && reachable[intermediate][consumer]);

        Core::GraphicsVector<Core::GpuTaskQueueAssignment> assignments(testArena.arena);
        Core::GraphicsVector<u32> assignmentIndices(testArena.arena);
        assignments.reserve(s_TaskCount);
        assignmentIndices.resize(s_TaskCount, Limit<u32>::s_Max);
        for(usize row = 0u; row < s_TaskCount; ++row){
            const usize taskIndex = assignmentOrder[row];
            const Core::GpuPhysicalQueueId queue = queues[row % LengthOf(queues)].id;
            assignments.push_back({ .task = tasks[taskIndex], .initialQueue = queue, .queue = queue, .score = {} });
            assignmentIndices[taskIndex] = static_cast<u32>(row);
        }
        scoringData.rebuildAssignmentLoads(assignments, topology);
        const auto expectScores = [&](){
            u64 totalCost = 0u;
            for(const Core::GpuTaskQueueAssignment& assignment : assignments)
                totalCost += costs[assignment.task.index];
            EXPECT_EQ(scoringData.m_totalAssignedCost, totalCost);
            for(const Core::GpuPhysicalQueueInfo& candidate : queues){
                u64 assignedCost = 0u;
                for(const Core::GpuTaskQueueAssignment& assignment : assignments)
                    if(assignment.queue == candidate.id)
                        assignedCost += costs[assignment.task.index];
                EXPECT_EQ(scoringData.assignedQueueLoad(candidate.id), assignedCost);
                for(const auto& range : ignoredRanges){
                    GpuTaskQueueScoreExclusions exclusions{ .assignmentOffset = range[0u], .assignmentCount = range[1u] };
                    const auto ignored = [&](const usize row)noexcept{ return row >= range[0u] && row - range[0u] < range[1u]; };
                    for(usize row = 0u; row < assignments.size(); ++row){
                        if(!ignored(row))
                            continue;
                        const Core::GpuTaskQueueAssignment& assignment = assignments[row];
                        exclusions.totalCost += costs[assignment.task.index];
                        if(assignment.queue == candidate.id)
                            exclusions.candidateQueueCost += costs[assignment.task.index];
                    }
                    for(usize taskIndex = 0u; taskIndex < s_TaskCount; ++taskIndex){
                        const Core::GpuTaskGraphTaskView task = declarations.taskAt(taskIndex);
                        u64 queueLoad = 0u;
                        u64 overlap = 0u;
                        for(usize row = 0u; row < assignments.size(); ++row){
                            const Core::GpuTaskQueueAssignment& assignment = assignments[row];
                            if(ignored(row) || assignment.task == task.id)
                                continue;
                            if(assignment.queue == candidate.id)
                                queueLoad += costs[assignment.task.index];
                            else if(range[1u] <= 1u && task.scheduling.overlapPreferred && !task.scheduling.avoidQueueCrossing && !reachable[taskIndex][assignment.task.index] && !reachable[assignment.task.index][taskIndex])
                                overlap += costs[assignment.task.index];
                        }
                        const u64 external = candidate.id == queues[1u].id ? 13u : candidate.id == queues[2u].id ? Limit<u64>::s_Max : 0u;
                        queueLoad = queueLoad > Limit<u64>::s_Max - external ? Limit<u64>::s_Max : queueLoad + external;
                        i32 incomingCrossings = 0;
                        i32 outgoingCrossings = 0;
                        for(const Core::GpuTaskDependencyEdge& edge : analysis.schedulingEdges()){
                            const u32 producerRow = assignmentIndices[edge.producer.index];
                            const u32 consumerRow = assignmentIndices[edge.consumer.index];
                            if(edge.consumer == task.id && !ignored(producerRow) && assignments[producerRow].queue != candidate.id)
                                ++incomingCrossings;
                            if(edge.producer == task.id && !ignored(consumerRow) && assignments[consumerRow].queue != candidate.id)
                                ++outgoingCrossings;
                        }
                        const Core::GpuQueueAssignmentScore score = BuildQueueAssignmentScore(declarations, analysis, assignments, assignmentIndices, topology, reachability, scoringData, task, candidate, exclusions);
                        EXPECT_EQ(score.queueLoad, SaturateReferenceTerm(queueLoad));
                        EXPECT_EQ(score.overlap, SaturateReferenceTerm(overlap));
                        EXPECT_EQ(score.incomingCrossings, incomingCrossings);
                        EXPECT_EQ(score.outgoingCrossings, outgoingCrossings);
                        EXPECT_EQ(score.ownershipTransfers, 0);
                    }
                }
            }
        };
        expectScores();
        const Array<Array<usize, 2u>, 6u> moves = { { { 0u, 1u }, { 3u, 1u }, { 6u, 1u }, { 0u, 2u }, { 5u, 0u }, { 0u, 2u } } };
        for(const auto& move : moves){
            Core::GpuTaskQueueAssignment& assignment = assignments[move[0u]];
            scoringData.updateAssignmentLoads(assignment.task, assignment.queue, queues[move[1u]].id);
            assignment.queue = queues[move[1u]].id;
            expectScores();
        }
        // Exercise zero eligible off-queue cost, including cancellation by self and a singleton exclusion.
        for(Core::GpuTaskQueueAssignment& assignment : assignments){
            scoringData.updateAssignmentLoads(assignment.task, assignment.queue, queues[0u].id);
            assignment.queue = queues[0u].id;
        }
        expectScores();
        const Array<usize, 2u> offQueueRows = { 0u, 3u };
        for(const usize row : offQueueRows){
            Core::GpuTaskQueueAssignment& assignment = assignments[row];
            scoringData.updateAssignmentLoads(assignment.task, assignment.queue, queues[1u].id);
            assignment.queue = queues[1u].id;
            expectScores();
        }
        Core::GraphicsVector<Core::GpuTaskQueueAssignment> emptyAssignments(testArena.arena);
        scoringData.rebuildAssignmentLoads(emptyAssignments, topology);
        EXPECT_EQ(scoringData.m_totalAssignedCost, 0u);
        for(const Core::GpuPhysicalQueueInfo& queue : queues)
            EXPECT_EQ(scoringData.assignedQueueLoad(queue.id), 0u);
        scoringData.rebuildAssignmentLoads(assignments, topology);
        expectScores();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

