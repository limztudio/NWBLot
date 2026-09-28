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
namespace Graphics = Core;
using namespace Core::GpuTaskGraphCompilerDetail;

constexpr usize s_TaskCount = 8u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static i32 SaturateReferenceTerm(const u64 value){
    return value > static_cast<u64>(Limit<i32>::s_Max) ? Limit<i32>::s_Max : static_cast<i32>(value);
}


TEST(GpuTaskQueueScoring, CachedLoadsAndScoresMatchIndependentReferenceAfterMovesAndRebuilds){
    const bool dependencyCases[] = { false, true };
    const Graphics::GpuTaskCostHint::Enum hints[] = {
        Graphics::GpuTaskCostHint::Tiny, Graphics::GpuTaskCostHint::Small,
        Graphics::GpuTaskCostHint::Medium, Graphics::GpuTaskCostHint::Large,
        Graphics::GpuTaskCostHint::Tiny, Graphics::GpuTaskCostHint::Large,
        Graphics::GpuTaskCostHint::Small, Graphics::GpuTaskCostHint::Medium,
    };
    const u64 costs[] = { 1u, 2u, 4u, 8u, 1u, 8u, 2u, 4u };
    const u8 parents[s_TaskCount][3u] = { {}, { 0u }, {}, { 0u, 2u }, { 1u }, {}, { 3u }, { 4u, 6u, 0u } };
    const usize parentCounts[] = { 0u, 1u, 0u, 2u, 1u, 0u, 1u, 3u };
    const usize assignmentOrder[] = { 5u, 0u, 7u, 2u, 6u, 1u, 4u, 3u };
    const usize ignoredRanges[][2u] = { { 0u, 0u }, { 0u, 1u }, { 3u, 1u }, { 1u, 3u }, { 0u, s_TaskCount } };
    for(const bool hasDependencies : dependencyCases){
        SCOPED_TRACE(hasDependencies);
        TestArena testArena;
        Core::Alloc::ScratchArena scratchArena(Name("tests/task/gpu/queue_scoring"));
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::GpuTaskId tasks[s_TaskCount] = {};
        for(usize taskIndex = 0u; taskIndex < s_TaskCount; ++taskIndex){
            Graphics::GpuTaskId dependencies[3u] = {};
            const usize dependencyCount = hasDependencies ? parentCounts[taskIndex] : 0u;
            for(usize dependencyIndex = 0u; dependencyIndex < dependencyCount; ++dependencyIndex)
                dependencies[dependencyIndex] = tasks[parents[taskIndex][dependencyIndex]];
            char indexBuffer[32u] = {};
            Graphics::GpuTaskDesc desc;
            desc
                .setIdentity(DeriveName(Name("tests/queue_scoring/task_"), FormatDecimal(taskIndex, indexBuffer)))
                .setMarkerLabel("Queue Scoring Task")
                .setDependencies(dependencies, dependencyCount)
                .setScheduling({ .cost = hints[taskIndex], .overlapPreferred = taskIndex != 1u, .avoidQueueCrossing = taskIndex == 6u })
            ;
            tasks[taskIndex] = graph.addTask(desc, { .requiredCapabilities = Graphics::GpuQueueCapability::Transfer });
            ASSERT_TRUE(tasks[taskIndex].valid());
        }
        const Graphics::GpuPhysicalQueueInfo queues[] = {
            { .familyIndex = 1u, .id = { .index = 17u, .deviceGeneration = 23u }, .queueClass = Graphics::CommandQueue::Transfer, .capabilities = Graphics::GpuQueueCapability::Transfer },
            { .familyIndex = 2u, .id = { .index = 311u, .deviceGeneration = 23u }, .queueClass = Graphics::CommandQueue::Transfer, .capabilities = Graphics::GpuQueueCapability::Transfer },
            { .familyIndex = 3u, .id = { .index = 1024u, .deviceGeneration = 23u }, .queueClass = Graphics::CommandQueue::Transfer, .capabilities = Graphics::GpuQueueCapability::Transfer },
        };
        const Graphics::GpuTaskGraphQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
        const Graphics::GpuTaskQueueLoad externalLoads[] = {
            { .queue = queues[1u].id, .estimatedCost = 13u },
            { .queue = queues[2u].id, .estimatedCost = Limit<u64>::s_Max },
        };
        Graphics::GpuTaskGraphQueueAssignmentOptions options;
        options.queueLoads = externalLoads;
        options.queueLoadCount = LengthOf(externalLoads);
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Graphics::GpuTaskGraphCompiler compiler;
        ASSERT_TRUE(compiler.analyze(declarations, analysis, scratchArena));
        GpuTaskSchedulingReachability reachability(scratchArena);
        ASSERT_TRUE(BuildGpuTaskSchedulingReachability(declarations, analysis, reachability));
        GpuTaskQueueScoringData scoringData(declarations, analysis, options, scratchArena);
        bool reachable[s_TaskCount][s_TaskCount] = {};
        for(const Graphics::GpuTaskDependencyEdge& edge : analysis.edges())
            reachable[edge.producer.index][edge.consumer.index] = true;
        // Plain Floyd-Warshall closure is independent of the packed scheduling representation.
        for(usize intermediate = 0u; intermediate < s_TaskCount; ++intermediate)
            for(usize producer = 0u; producer < s_TaskCount; ++producer)
                for(usize consumer = 0u; consumer < s_TaskCount; ++consumer)
                    reachable[producer][consumer] = reachable[producer][consumer] || (reachable[producer][intermediate] && reachable[intermediate][consumer]);

        Graphics::GraphicsVector<Graphics::GpuTaskQueueAssignment> assignments(testArena.arena);
        Graphics::GraphicsVector<u32> assignmentIndices(testArena.arena);
        assignments.reserve(s_TaskCount);
        assignmentIndices.resize(s_TaskCount, Limit<u32>::s_Max);
        for(usize row = 0u; row < s_TaskCount; ++row){
            const usize taskIndex = assignmentOrder[row];
            const Graphics::GpuPhysicalQueueId queue = queues[row % LengthOf(queues)].id;
            assignments.push_back({ .task = tasks[taskIndex], .initialQueue = queue, .queue = queue, .score = {} });
            assignmentIndices[taskIndex] = static_cast<u32>(row);
        }
        scoringData.rebuildAssignmentLoads(assignments, topology);
        const auto expectScores = [&](){
            u64 totalCost = 0u;
            for(const Graphics::GpuTaskQueueAssignment& assignment : assignments)
                totalCost += costs[assignment.task.index];
            EXPECT_EQ(scoringData.totalAssignedCost, totalCost);
            for(const Graphics::GpuPhysicalQueueInfo& candidate : queues){
                u64 assignedCost = 0u;
                for(const Graphics::GpuTaskQueueAssignment& assignment : assignments)
                    if(assignment.queue == candidate.id)
                        assignedCost += costs[assignment.task.index];
                EXPECT_EQ(scoringData.assignedQueueLoad(candidate.id), assignedCost);
                for(const auto& range : ignoredRanges){
                    GpuTaskQueueScoreExclusions exclusions{ .assignmentOffset = range[0u], .assignmentCount = range[1u] };
                    const auto ignored = [&](const usize row){ return row >= range[0u] && row - range[0u] < range[1u]; };
                    for(usize row = 0u; row < assignments.size(); ++row){
                        if(!ignored(row))
                            continue;
                        const Graphics::GpuTaskQueueAssignment& assignment = assignments[row];
                        exclusions.totalCost += costs[assignment.task.index];
                        if(assignment.queue == candidate.id)
                            exclusions.candidateQueueCost += costs[assignment.task.index];
                    }
                    for(usize taskIndex = 0u; taskIndex < s_TaskCount; ++taskIndex){
                        const Graphics::GpuTaskGraphTaskView task = declarations.taskAt(taskIndex);
                        u64 queueLoad = 0u;
                        u64 overlap = 0u;
                        for(usize row = 0u; row < assignments.size(); ++row){
                            const Graphics::GpuTaskQueueAssignment& assignment = assignments[row];
                            if(ignored(row) || assignment.task == task.id)
                                continue;
                            if(assignment.queue == candidate.id)
                                queueLoad += costs[assignment.task.index];
                            else if(
                                range[1u] <= 1u
                                && task.scheduling.overlapPreferred
                                && !task.scheduling.avoidQueueCrossing
                                && !reachable[taskIndex][assignment.task.index]
                                && !reachable[assignment.task.index][taskIndex]
                            )
                                overlap += costs[assignment.task.index];
                        }
                        const u64 external = candidate.id == queues[1u].id ? 13u : candidate.id == queues[2u].id ? Limit<u64>::s_Max : 0u;
                        queueLoad = queueLoad > Limit<u64>::s_Max - external ? Limit<u64>::s_Max : queueLoad + external;
                        i32 incomingCrossings = 0;
                        i32 outgoingCrossings = 0;
                        for(const Graphics::GpuTaskDependencyEdge& edge : analysis.schedulingEdges()){
                            const u32 producerRow = assignmentIndices[edge.producer.index];
                            const u32 consumerRow = assignmentIndices[edge.consumer.index];
                            if(edge.consumer == task.id && !ignored(producerRow) && assignments[producerRow].queue != candidate.id)
                                ++incomingCrossings;
                            if(edge.producer == task.id && !ignored(consumerRow) && assignments[consumerRow].queue != candidate.id)
                                ++outgoingCrossings;
                        }
                        const Graphics::GpuQueueAssignmentScore score = BuildQueueAssignmentScore(
                            declarations,
                            analysis,
                            assignments,
                            assignmentIndices,
                            topology,
                            reachability,
                            scoringData,
                            task,
                            candidate,
                            exclusions
                        );
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
        const usize moves[][2u] = { { 0u, 1u }, { 3u, 1u }, { 6u, 1u }, { 0u, 2u }, { 5u, 0u }, { 0u, 2u } };
        for(const auto& move : moves){
            Graphics::GpuTaskQueueAssignment& assignment = assignments[move[0u]];
            scoringData.moveAssignedTask(assignment.task, assignment.queue, queues[move[1u]].id);
            assignment.queue = queues[move[1u]].id;
            expectScores();
        }
        // Exercise zero eligible off-queue cost, including cancellation by self and a singleton exclusion.
        for(Graphics::GpuTaskQueueAssignment& assignment : assignments){
            scoringData.moveAssignedTask(assignment.task, assignment.queue, queues[0u].id);
            assignment.queue = queues[0u].id;
        }
        expectScores();
        const usize offQueueRows[] = { 0u, 3u };
        for(const usize row : offQueueRows){
            Graphics::GpuTaskQueueAssignment& assignment = assignments[row];
            scoringData.moveAssignedTask(assignment.task, assignment.queue, queues[1u].id);
            assignment.queue = queues[1u].id;
            expectScores();
        }
        Graphics::GraphicsVector<Graphics::GpuTaskQueueAssignment> emptyAssignments(testArena.arena);
        scoringData.rebuildAssignmentLoads(emptyAssignments, topology);
        EXPECT_EQ(scoringData.totalAssignedCost, 0u);
        for(const Graphics::GpuPhysicalQueueInfo& queue : queues)
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

