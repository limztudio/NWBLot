// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/graphics/task_graph/compiler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_timing_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct TaskGraphTimingTestsTag>;
namespace Graphics = Core;

inline constexpr Name s_TaskGraphTimingScratchArena("tests/graphics/task_graph_timing_scratch");


[[nodiscard]] Graphics::GpuPhysicalQueueInfo GraphicsQueue(){
    return Graphics::GpuPhysicalQueueInfo{
        .id = Graphics::GpuPhysicalQueueId{ 0u, 1u },
        .queueClass = Graphics::CommandQueue::Graphics,
        .capabilities = static_cast<Graphics::GpuQueueCapability::Mask>(
            static_cast<u8>(Graphics::GpuQueueCapability::Graphics)
            | static_cast<u8>(Graphics::GpuQueueCapability::Compute)
            | static_cast<u8>(Graphics::GpuQueueCapability::Transfer)
        ),
        .familyIndex = 0u,
        .queueIndex = 0u,
        .dedicated = false,
    };
}

[[nodiscard]] Graphics::GpuTaskId AddTask(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const Graphics::GpuTaskId dependency = {},
    const Graphics::GpuTaskSchedulingHint& scheduling = {},
    const Graphics::GpuTaskTimingMetadata& timing = {}
){
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel("Task Graph Timing Test")
        .setScheduling(scheduling)
        .setTimingMetadata(timing)
    ;
    if(dependency.valid())
        desc.setDependencies(&dependency, 1u);
    return graph.addTask(desc);
}

[[nodiscard]] bool Compile(
    const Graphics::GpuTaskGraph& graph,
    Graphics::GpuTaskGraphAnalysis& analysis,
    Graphics::GpuTaskGraphQueueAssignments& assignments,
    Graphics::GpuCompiledGraph& compiledGraph,
    const Graphics::GpuTaskGraphCompileOptions& options = {}
){
    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{ .queues = &queue, .queueCount = 1u };
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphTimingScratchArena);
    const Graphics::GpuTaskGraphCompiler compiler;
    Graphics::GpuTaskGraphCompileOptions metadataOptions = options;
    metadataOptions.allowMetadataOnlyTasks = true;
    return compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena, metadataOptions);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphTiming, CompilesInclusivePacketEnvelopeWithoutChangingTaskPolicies){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskId prefix = AddTask(
        graph,
        Name("tests/task_graph_timing/envelope_prefix")
    );
    ASSERT_TRUE(prefix.valid());
    const Graphics::GpuTaskId firstPacketPrefix = AddTask(
        graph,
        Name("tests/task_graph_timing/envelope_first_packet_prefix"),
        prefix
    );
    ASSERT_TRUE(firstPacketPrefix.valid());

    Graphics::GpuTaskSchedulingHint mergedScheduling;
    mergedScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskId firstEndpoint = AddTask(
        graph,
        Name("tests/task_graph_timing/envelope_first_endpoint"),
        firstPacketPrefix,
        mergedScheduling
    );
    ASSERT_TRUE(firstEndpoint.valid());
    const Graphics::GpuTaskId middle = AddTask(
        graph,
        Name("tests/task_graph_timing/envelope_middle"),
        firstEndpoint
    );
    ASSERT_TRUE(middle.valid());
    const Graphics::GpuTaskId lastEndpoint = AddTask(
        graph,
        Name("tests/task_graph_timing/envelope_last_endpoint"),
        middle
    );
    ASSERT_TRUE(lastEndpoint.valid());
    const Graphics::GpuTaskId lastPacketSuffix = AddTask(
        graph,
        Name("tests/task_graph_timing/envelope_last_packet_suffix"),
        lastEndpoint,
        mergedScheduling
    );
    ASSERT_TRUE(lastPacketSuffix.valid());

    Graphics::GpuTaskTimingMetadata packetOnlyTiming;
    packetOnlyTiming.policy = Graphics::GpuTaskTimingPolicy::PacketOnly;
    const Graphics::GpuTaskId timedTail = AddTask(
        graph,
        Name("tests/task_graph_timing/envelope_timed_tail"),
        lastPacketSuffix,
        {},
        packetOnlyTiming
    );
    ASSERT_TRUE(timedTail.valid());

    Graphics::GpuTaskGraphCompileOptions options;
    options.packetTimingEnvelope.firstTask = firstEndpoint;
    options.packetTimingEnvelope.lastTask = lastEndpoint;
    ASSERT_TRUE(options.packetTimingEnvelope.enabled());
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, assignments, compiledGraph, options));
    ASSERT_TRUE(compiledGraph.validFor(graph));
    ASSERT_EQ(compiledGraph.packetCount(), 5u);

    const Graphics::GpuSubmissionPacketId prefixPacket = compiledGraph.packetForTask(prefix);
    const Graphics::GpuSubmissionPacketId firstPacket = compiledGraph.packetForTask(firstEndpoint);
    const Graphics::GpuSubmissionPacketId middlePacket = compiledGraph.packetForTask(middle);
    const Graphics::GpuSubmissionPacketId lastPacket = compiledGraph.packetForTask(lastEndpoint);
    const Graphics::GpuSubmissionPacketId tailPacket = compiledGraph.packetForTask(timedTail);
    EXPECT_EQ(compiledGraph.packetForTask(firstPacketPrefix), firstPacket);
    EXPECT_EQ(compiledGraph.packetForTask(lastPacketSuffix), lastPacket);
    const Graphics::GpuSubmissionPacketRange expectedRange = compiledGraph.packetRange(firstPacket, lastPacket);
    ASSERT_TRUE(expectedRange.valid());
    ASSERT_EQ(expectedRange.packetCount, 3u);
    const Graphics::GpuSubmissionPacketRange compiledRange = compiledGraph.packetTimingEnvelopeRange();
    EXPECT_EQ(compiledRange.first, expectedRange.first);
    EXPECT_EQ(compiledRange.packetCount, expectedRange.packetCount);

    EXPECT_FALSE(compiledGraph.packet(prefixPacket).recordsPacketEnvelopeTiming);
    EXPECT_FALSE(compiledGraph.packet(prefixPacket).recordsTiming);
    EXPECT_TRUE(compiledGraph.packet(firstPacket).recordsPacketEnvelopeTiming);
    EXPECT_TRUE(compiledGraph.packet(firstPacket).recordsTiming);
    EXPECT_TRUE(compiledGraph.packet(middlePacket).recordsPacketEnvelopeTiming);
    EXPECT_TRUE(compiledGraph.packet(middlePacket).recordsTiming);
    EXPECT_TRUE(compiledGraph.packet(lastPacket).recordsPacketEnvelopeTiming);
    EXPECT_TRUE(compiledGraph.packet(lastPacket).recordsTiming);
    EXPECT_FALSE(compiledGraph.packet(tailPacket).recordsPacketEnvelopeTiming);
    EXPECT_TRUE(compiledGraph.packet(tailPacket).recordsTiming);
    ASSERT_NE(compiledGraph.findTask(firstEndpoint), nullptr);
    ASSERT_NE(compiledGraph.findTask(timedTail), nullptr);
    EXPECT_EQ(compiledGraph.findTask(firstEndpoint)->timingPolicy, Graphics::GpuTaskTimingPolicy::None);
    EXPECT_EQ(compiledGraph.findTask(timedTail)->timingPolicy, Graphics::GpuTaskTimingPolicy::PacketOnly);

    const Graphics::GpuSubmissionPacketRange oldRange = compiledRange;
    const u64 oldPlanGeneration = compiledGraph.planGeneration();
    ASSERT_TRUE(Compile(graph, analysis, assignments, compiledGraph, options));
    EXPECT_NE(compiledGraph.planGeneration(), oldPlanGeneration);
    ASSERT_TRUE(compiledGraph.packetTimingEnvelopeRange().valid());
    EXPECT_EQ(compiledGraph.packetTimingEnvelopeRange().packetCount, expectedRange.packetCount);
    EXPECT_FALSE(compiledGraph.validPacketRange(oldRange));

    const u64 recompiledPlanGeneration = compiledGraph.planGeneration();
    ASSERT_TRUE(Compile(graph, analysis, assignments, compiledGraph));
    EXPECT_NE(compiledGraph.planGeneration(), recompiledPlanGeneration);
    EXPECT_FALSE(compiledGraph.packetTimingEnvelopeRange().valid());
    EXPECT_FALSE(compiledGraph.validPacketRange(oldRange));
    for(usize packetIndex = 0u; packetIndex < compiledGraph.packetCount(); ++packetIndex)
        EXPECT_FALSE(compiledGraph.packet(compiledGraph.packetIdAt(packetIndex)).recordsPacketEnvelopeTiming);
    EXPECT_TRUE(compiledGraph.packet(compiledGraph.packetForTask(timedTail)).recordsTiming);

    compiledGraph.reset();
    EXPECT_FALSE(compiledGraph.packetTimingEnvelopeRange().valid());
}

TEST(GpuTaskGraphTiming, RejectsIncompleteForeignAndReversedEnvelopeEndpoints){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskId first = AddTask(graph, Name("tests/task_graph_timing/invalid_first"));
    const Graphics::GpuTaskId second = AddTask(graph, Name("tests/task_graph_timing/invalid_second"), first);
    const Graphics::GpuTaskId third = AddTask(graph, Name("tests/task_graph_timing/invalid_third"), second);
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    ASSERT_TRUE(third.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    Graphics::GpuTaskGraphCompileOptions options;

    options.packetTimingEnvelope.firstTask = first;
    EXPECT_FALSE(options.packetTimingEnvelope.enabled());
    EXPECT_FALSE(Compile(graph, analysis, assignments, compiledGraph, options));
    EXPECT_FALSE(compiledGraph.valid());

    options.packetTimingEnvelope = {};
    options.packetTimingEnvelope.lastTask = third;
    EXPECT_FALSE(options.packetTimingEnvelope.enabled());
    EXPECT_FALSE(Compile(graph, analysis, assignments, compiledGraph, options));
    EXPECT_FALSE(compiledGraph.valid());

    Graphics::GpuTaskGraph foreignGraph(testArena.arena);
    const Graphics::GpuTaskId foreign = AddTask(
        foreignGraph,
        Name("tests/task_graph_timing/invalid_foreign")
    );
    ASSERT_TRUE(foreign.valid());
    options.packetTimingEnvelope = { .firstTask = first, .lastTask = foreign };
    ASSERT_TRUE(options.packetTimingEnvelope.enabled());
    EXPECT_FALSE(Compile(graph, analysis, assignments, compiledGraph, options));
    EXPECT_FALSE(compiledGraph.valid());

    options.packetTimingEnvelope = { .firstTask = third, .lastTask = first };
    EXPECT_FALSE(Compile(graph, analysis, assignments, compiledGraph, options));
    EXPECT_FALSE(compiledGraph.valid());

    options.packetTimingEnvelope = { .firstTask = second, .lastTask = second };
    ASSERT_TRUE(Compile(graph, analysis, assignments, compiledGraph, options));
    ASSERT_TRUE(compiledGraph.packetTimingEnvelopeRange().valid());
    EXPECT_EQ(compiledGraph.packetTimingEnvelopeRange().packetCount, 1u);

    Graphics::GpuTaskGraph mergedGraph(testArena.arena);
    const Graphics::GpuTaskId mergedFirst = AddTask(
        mergedGraph,
        Name("tests/task_graph_timing/invalid_merged_first")
    );
    Graphics::GpuTaskSchedulingHint mergedScheduling;
    mergedScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskId mergedLast = AddTask(
        mergedGraph,
        Name("tests/task_graph_timing/invalid_merged_last"),
        mergedFirst,
        mergedScheduling
    );
    ASSERT_TRUE(mergedFirst.valid());
    ASSERT_TRUE(mergedLast.valid());
    options.packetTimingEnvelope = { .firstTask = mergedLast, .lastTask = mergedFirst };
    EXPECT_FALSE(Compile(mergedGraph, analysis, assignments, compiledGraph, options));
    EXPECT_FALSE(compiledGraph.valid());

    Graphics::GpuTaskGraph staleGraph(testArena.arena);
    const Graphics::GpuTaskId staleEndpoint = AddTask(
        staleGraph,
        Name("tests/task_graph_timing/invalid_stale")
    );
    ASSERT_TRUE(staleEndpoint.valid());
    options.packetTimingEnvelope = { .firstTask = staleEndpoint, .lastTask = staleEndpoint };
    staleGraph.reset();
    EXPECT_FALSE(Compile(staleGraph, analysis, assignments, compiledGraph, options));
    EXPECT_FALSE(compiledGraph.valid());
}

TEST(GpuTaskGraphTiming, ResolvesEnvelopeByTopologicalPositionInsteadOfTaskIndex){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    const Graphics::GpuTaskId futureEarly{ 2u, graph.generation() };
    const Graphics::GpuTaskId late = AddTask(
        graph,
        Name("tests/task_graph_timing/topological_late"),
        futureEarly,
        scheduling
    );
    const Graphics::GpuTaskId prefix = AddTask(
        graph,
        Name("tests/task_graph_timing/topological_prefix"),
        {},
        scheduling
    );
    const Graphics::GpuTaskId early = AddTask(
        graph,
        Name("tests/task_graph_timing/topological_early"),
        prefix,
        scheduling
    );
    ASSERT_TRUE(late.valid());
    ASSERT_TRUE(prefix.valid());
    ASSERT_TRUE(early.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    Graphics::GpuTaskGraphCompileOptions options;
    options.packetTimingEnvelope = { .firstTask = early, .lastTask = late };
    ASSERT_TRUE(Compile(graph, analysis, assignments, compiledGraph, options));
    ASSERT_EQ(analysis.topologicalOrder().size(), 3u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], prefix);
    EXPECT_EQ(analysis.topologicalOrder()[1u], early);
    EXPECT_EQ(analysis.topologicalOrder()[2u], late);
    EXPECT_LT(compiledGraph.packetForTask(early).index, compiledGraph.packetForTask(late).index);
    ASSERT_TRUE(compiledGraph.packetTimingEnvelopeRange().valid());
    EXPECT_EQ(compiledGraph.packetTimingEnvelopeRange().packetCount, 2u);

    options.packetTimingEnvelope = { .firstTask = late, .lastTask = early };
    EXPECT_FALSE(Compile(graph, analysis, assignments, compiledGraph, options));
    EXPECT_FALSE(compiledGraph.valid());
}

TEST(GpuTaskGraphTiming, RejectsEnvelopeRangesAtOrAfterAcceptedQueueFrontierPackets){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskId first = AddTask(graph, Name("tests/task_graph_timing/frontier_first"));
    ASSERT_TRUE(first.valid());

    Graphics::GpuTaskSchedulingHint frontierScheduling;
    frontierScheduling.forceSubmissionBoundary = true;
    frontierScheduling.allowPacketMerge = false;
    frontierScheduling.joinsAcceptedQueueFrontier = true;
    const Graphics::GpuTaskId recovery = AddTask(
        graph,
        Name("tests/task_graph_timing/frontier_recovery"),
        {},
        frontierScheduling
    );
    ASSERT_TRUE(recovery.valid());
    const Graphics::GpuTaskId last = AddTask(
        graph,
        Name("tests/task_graph_timing/frontier_last"),
        recovery
    );
    ASSERT_TRUE(last.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    Graphics::GpuTaskGraphCompileOptions options;
    options.packetTimingEnvelope = { .firstTask = first, .lastTask = first };
    ASSERT_TRUE(Compile(graph, analysis, assignments, compiledGraph, options));
    ASSERT_TRUE(compiledGraph.packetTimingEnvelopeRange().valid());

    options.packetTimingEnvelope = { .firstTask = first, .lastTask = last };
    EXPECT_FALSE(Compile(graph, analysis, assignments, compiledGraph, options));
    EXPECT_FALSE(compiledGraph.valid());
    options.packetTimingEnvelope = { .firstTask = recovery, .lastTask = recovery };
    EXPECT_FALSE(Compile(graph, analysis, assignments, compiledGraph, options));
    EXPECT_FALSE(compiledGraph.valid());
    options.packetTimingEnvelope = { .firstTask = last, .lastTask = last };
    EXPECT_FALSE(Compile(graph, analysis, assignments, compiledGraph, options));
    EXPECT_FALSE(compiledGraph.valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

