// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_compiled_graph_query_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


static void CheckPacketQueries(const usize taskCount){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    u64 graphGeneration = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        graphGeneration = declarations.generation();
    }
    Core::GraphicsVector<Core::GpuTaskId> tasks(testArena.arena);
    tasks.resize(taskCount);
    for(usize declarationIndex = 0u; declarationIndex < taskCount; ++declarationIndex){
        const usize orderIndex = taskCount - declarationIndex - 1u;
        Core::GpuTaskSchedulingHint scheduling;
        scheduling.mergeWithPrevious = orderIndex != 0u;
        const Core::GpuTaskId dependency{ .generation = graphGeneration, .index = static_cast<u32>(declarationIndex + 1u), };
        const auto derivedNameResult = IndexedName(Name("tests/compiled_query/task/"), declarationIndex);
        ASSERT_TRUE(derivedNameResult);
        const auto taskAdmission = AddTaskWithCommands(*derivedNameResult, "Compiled Query Task", GraphicsCommands(), scheduling, {}, orderIndex != 0u ? Span(&dependency, 1u) : Span<const Core::GpuTaskId>{}, graph);
        ASSERT_TRUE(taskAdmission);
        tasks[orderIndex] = *taskAdmission;
        ASSERT_TRUE(tasks[orderIndex].valid());
    }
    const auto nextPacketTaskResult = AddTaskWithCommands(Name("tests/compiled_query/next_packet"), "Next Packet", GraphicsCommands(), {}, {}, Span(&tasks.back(), 1u), graph);
    ASSERT_TRUE(nextPacketTaskResult);
    const Core::GpuTaskId nextPacketTask = *nextPacketTaskResult;
    ASSERT_TRUE(nextPacketTask.valid());
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    const Core::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u };
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());
    ASSERT_EQ(views.compiled.packetCount(), 2u);
    EXPECT_TRUE(views.compiled.taskPrecedesInSamePacket(tasks.front(), tasks.back()));
    EXPECT_FALSE(views.compiled.taskPrecedesInSamePacket(tasks.back(), tasks.front()));
    EXPECT_FALSE(views.compiled.taskPrecedesInSamePacket(tasks.front(), tasks.front()));
    EXPECT_FALSE(views.compiled.taskPrecedesInSamePacket(tasks.front(), nextPacketTask));
    EXPECT_FALSE(views.compiled.taskPrecedesInSamePacket({}, tasks.back()));
    EXPECT_TRUE(views.compiled.tasksFormContiguousPacketSequence(Span<const Core::GpuTaskId>(tasks.data(), tasks.size())));
    EXPECT_TRUE(views.compiled.tasksFormContiguousPacketSequence(Span<const Core::GpuTaskId>(tasks.data() + 1u, tasks.size() - 2u)));
    const Array<Core::GpuTaskId, 2> reversed = { tasks[1u], tasks[0u] };
    const Array<Core::GpuTaskId, 2> duplicated = { tasks[1u], tasks[1u] };
    const Array<Core::GpuTaskId, 2> noncontiguous = { tasks.front(), tasks.back() };
    const Array<Core::GpuTaskId, 2> crossesPackets = { tasks.back(), nextPacketTask };
    const Core::GpuTaskId stale{ .generation = graphGeneration + 1u, .index = tasks.front().index };
    EXPECT_FALSE(views.compiled.tasksFormContiguousPacketSequence(reversed));
    EXPECT_FALSE(views.compiled.tasksFormContiguousPacketSequence(duplicated));
    EXPECT_FALSE(views.compiled.tasksFormContiguousPacketSequence(noncontiguous));
    EXPECT_FALSE(views.compiled.tasksFormContiguousPacketSequence(crossesPackets));
    EXPECT_FALSE(views.compiled.tasksFormContiguousPacketSequence(Span<const Core::GpuTaskId>(&stale, 1u)));
    EXPECT_FALSE(views.compiled.tasksFormContiguousPacketSequence(Span<const Core::GpuTaskId>(static_cast<const Core::GpuTaskId*>(nullptr), 1u)));
    EXPECT_FALSE(views.compiled.tasksFormContiguousPacketSequence(Span<const Core::GpuTaskId>(tasks.data(), 0u)));

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuCompiledGraph, PacketQueriesFollowExecutionOrderAcrossReverseDeclarations){
    ASSERT_NO_FATAL_FAILURE(__hidden_compiled_graph_query_tests::CheckPacketQueries(32u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

