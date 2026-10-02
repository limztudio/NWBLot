// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <global/timer.h>


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
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    u64 graphGeneration = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        graphGeneration = declarations.generation();
    }
    Graphics::GraphicsVector<Graphics::GpuTaskId> tasks(testArena.arena);
    tasks.resize(taskCount);
    for(usize declarationIndex = 0u; declarationIndex < taskCount; ++declarationIndex){
        const usize orderIndex = taskCount - declarationIndex - 1u;
        Graphics::GpuTaskSchedulingHint scheduling;
        scheduling.mergeWithPrevious = orderIndex != 0u;
        const Graphics::GpuTaskId dependency{
            .generation = graphGeneration,
            .index = static_cast<u32>(declarationIndex + 1u),
        };
        char identityText[32u] = {};
        tasks[orderIndex] = AddTaskWithCommands(
            graph,
            DeriveName(Name("tests/compiled_query/task/"), FormatDecimal(declarationIndex, identityText)),
            "Compiled Query Task",
            GraphicsCommands(),
            scheduling,
            {},
            orderIndex != 0u ? &dependency : nullptr,
            orderIndex != 0u ? 1u : 0u
        );
        ASSERT_TRUE(tasks[orderIndex].valid());
    }
    const Graphics::GpuTaskId nextPacketTask = AddTaskWithCommands(
        graph,
        Name("tests/compiled_query/next_packet"),
        "Next Packet",
        GraphicsCommands(),
        {},
        {},
        &tasks.back(),
        1u
    );
    ASSERT_TRUE(nextPacketTask.valid());
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u };
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());
    ASSERT_EQ(views.compiled.packetCount(), 2u);
    EXPECT_TRUE(views.compiled.taskPrecedesInSamePacket(tasks.front(), tasks.back()));
    EXPECT_FALSE(views.compiled.taskPrecedesInSamePacket(tasks.back(), tasks.front()));
    EXPECT_FALSE(views.compiled.taskPrecedesInSamePacket(tasks.front(), tasks.front()));
    EXPECT_FALSE(views.compiled.taskPrecedesInSamePacket(tasks.front(), nextPacketTask));
    EXPECT_FALSE(views.compiled.taskPrecedesInSamePacket({}, tasks.back()));
    EXPECT_TRUE(views.compiled.tasksFormContiguousPacketSequence(tasks.data(), tasks.size()));
    EXPECT_TRUE(views.compiled.tasksFormContiguousPacketSequence(tasks.data() + 1u, tasks.size() - 2u));
    const Graphics::GpuTaskId reversed[] = { tasks[1u], tasks[0u] };
    const Graphics::GpuTaskId duplicated[] = { tasks[1u], tasks[1u] };
    const Graphics::GpuTaskId noncontiguous[] = { tasks.front(), tasks.back() };
    const Graphics::GpuTaskId crossesPackets[] = { tasks.back(), nextPacketTask };
    const Graphics::GpuTaskId stale{ .generation = graphGeneration + 1u, .index = tasks.front().index };
    EXPECT_FALSE(views.compiled.tasksFormContiguousPacketSequence(reversed, LengthOf(reversed)));
    EXPECT_FALSE(views.compiled.tasksFormContiguousPacketSequence(duplicated, LengthOf(duplicated)));
    EXPECT_FALSE(views.compiled.tasksFormContiguousPacketSequence(noncontiguous, LengthOf(noncontiguous)));
    EXPECT_FALSE(views.compiled.tasksFormContiguousPacketSequence(crossesPackets, LengthOf(crossesPackets)));
    EXPECT_FALSE(views.compiled.tasksFormContiguousPacketSequence(&stale, 1u));
    EXPECT_FALSE(views.compiled.tasksFormContiguousPacketSequence(nullptr, 1u));
    EXPECT_FALSE(views.compiled.tasksFormContiguousPacketSequence(tasks.data(), 0u));
    u64 minimumOrderNanoseconds = Limit<u64>::s_Max;
    u64 minimumSequenceNanoseconds = Limit<u64>::s_Max;
    for(usize iteration = 0u; iteration < 4u; ++iteration){
        bool orderValid = true;
        Timer begin = TimerNow();
        for(usize taskIndex = 1u; taskIndex < taskCount; ++taskIndex)
            orderValid &= views.compiled.taskPrecedesInSamePacket(tasks[taskIndex - 1u], tasks[taskIndex]);
        const u64 orderNanoseconds = DurationInNS<u64>(TimerNow(), begin);
        EXPECT_TRUE(orderValid);
        bool sequenceValid = true;
        begin = TimerNow();
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex)
            sequenceValid &= views.compiled.tasksFormContiguousPacketSequence(tasks.data() + taskIndex, 1u);
        const u64 sequenceNanoseconds = DurationInNS<u64>(TimerNow(), begin);
        EXPECT_TRUE(sequenceValid);
        if(iteration != 0u && orderNanoseconds < minimumOrderNanoseconds)
            minimumOrderNanoseconds = orderNanoseconds;
        if(iteration != 0u && sequenceNanoseconds < minimumSequenceNanoseconds)
            minimumSequenceNanoseconds = sequenceNanoseconds;
    }
    RecordUnsignedTestProperty("packet_query_task_count", taskCount);
    RecordUnsignedTestProperty("packet_order_query_ns", minimumOrderNanoseconds);
    RecordUnsignedTestProperty("packet_sequence_query_ns", minimumSequenceNanoseconds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuCompiledGraph, PacketQueriesFollowExecutionOrderAcrossReverseDeclarations){
    __hidden_compiled_graph_query_tests::CheckPacketQueries(32u);
}

TEST(GpuCompiledGraph, DISABLED_PacketQueryBenchmark4096Tasks){
    __hidden_compiled_graph_query_tests::CheckPacketQueries(4096u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

