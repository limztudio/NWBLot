// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <tests/common/gpu_task_graph_read_views.h>
#include "task_graph_resource_version_test_utils.h"

#include <gtest/gtest.h>

#include <core/task/gpu/compiler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_timing_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct TaskGraphTimingTestsTag>;
namespace Graphics = Core;

inline constexpr Name s_TaskGraphTimingScratchArena("tests/task/gpu/timing_scratch");

using TaskGraphResourceVersionTestUtils::GraphicsQueue;


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
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    return compiler.compile(declarations, analysis, topology, assignments, compiledGraph, scratchArena, metadataOptions);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskTimingHistoryStore, ResetInvalidatesOwnedSnapshot){
    TestArena testArena;
    Graphics::GpuTaskTimingHistoryStore history(testArena.arena);
    Graphics::GpuTaskTimingHistorySnapshot snapshot(testArena.arena);
    const Graphics::GpuPhysicalQueueId queue{ .index = 1u, .deviceGeneration = 21u };
    const Graphics::GpuTaskTimingKey key{
        .task = Name("tests/task_graph/timing_snapshot_reset"),
        .queue = Graphics::CommandQueue::Compute,
    };

    history.resetForDeviceGeneration(queue.deviceGeneration);
    ASSERT_TRUE(history.recordSample(key, queue, 0.002, 4u));
    history.snapshot(snapshot);
    ASSERT_TRUE(snapshot.valid());
    ASSERT_NE(snapshot.find(key, queue), nullptr);

    history.reset(snapshot);
    EXPECT_FALSE(snapshot.valid());
    EXPECT_EQ(snapshot.deviceGeneration(), 0u);
    EXPECT_EQ(snapshot.find(key, queue), nullptr);
    EXPECT_EQ(history.deviceGeneration(), 0u);
    EXPECT_EQ(history.historyCount(), 0u);
}


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
    Graphics::GpuSubmissionPacketRange expectedRange;
    Graphics::GpuSubmissionPacketRange oldRange;
    u64 oldPlanGeneration = 0u;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        ASSERT_EQ(views.compiled.packetCount(), 5u);

        const Graphics::GpuSubmissionPacketId prefixPacket = views.compiled.packetForTask(prefix);
        const Graphics::GpuSubmissionPacketId firstPacket = views.compiled.packetForTask(firstEndpoint);
        const Graphics::GpuSubmissionPacketId middlePacket = views.compiled.packetForTask(middle);
        const Graphics::GpuSubmissionPacketId lastPacket = views.compiled.packetForTask(lastEndpoint);
        const Graphics::GpuSubmissionPacketId tailPacket = views.compiled.packetForTask(timedTail);
        EXPECT_EQ(views.compiled.packetForTask(firstPacketPrefix), firstPacket);
        EXPECT_EQ(views.compiled.packetForTask(lastPacketSuffix), lastPacket);
        expectedRange = views.compiled.packetRange(firstPacket, lastPacket);
        ASSERT_TRUE(expectedRange.valid());
        ASSERT_EQ(expectedRange.packetCount, 3u);
        const Graphics::GpuSubmissionPacketRange compiledRange = views.compiled.packetTimingEnvelopeRange();
        EXPECT_EQ(compiledRange.first, expectedRange.first);
        EXPECT_EQ(compiledRange.packetCount, expectedRange.packetCount);

        const Graphics::GpuCompiledPacketView prefixPacketView = views.compiled.packet(prefixPacket);
        const Graphics::GpuCompiledPacketView firstPacketView = views.compiled.packet(firstPacket);
        const Graphics::GpuCompiledPacketView middlePacketView = views.compiled.packet(middlePacket);
        const Graphics::GpuCompiledPacketView lastPacketView = views.compiled.packet(lastPacket);
        const Graphics::GpuCompiledPacketView tailPacketView = views.compiled.packet(tailPacket);
        ASSERT_TRUE(prefixPacketView.valid());
        ASSERT_TRUE(firstPacketView.valid());
        ASSERT_TRUE(middlePacketView.valid());
        ASSERT_TRUE(lastPacketView.valid());
        ASSERT_TRUE(tailPacketView.valid());
        EXPECT_FALSE(prefixPacketView.plan->recordsPacketEnvelopeTiming);
        EXPECT_FALSE(prefixPacketView.plan->recordsTiming);
        EXPECT_TRUE(firstPacketView.plan->recordsPacketEnvelopeTiming);
        EXPECT_TRUE(firstPacketView.plan->recordsTiming);
        EXPECT_TRUE(middlePacketView.plan->recordsPacketEnvelopeTiming);
        EXPECT_TRUE(middlePacketView.plan->recordsTiming);
        EXPECT_TRUE(lastPacketView.plan->recordsPacketEnvelopeTiming);
        EXPECT_TRUE(lastPacketView.plan->recordsTiming);
        EXPECT_FALSE(tailPacketView.plan->recordsPacketEnvelopeTiming);
        EXPECT_TRUE(tailPacketView.plan->recordsTiming);
        const Graphics::GpuCompiledTaskView firstEndpointView = views.compiled.findTask(firstEndpoint);
        const Graphics::GpuCompiledTaskView timedTailView = views.compiled.findTask(timedTail);
        ASSERT_TRUE(firstEndpointView.valid());
        ASSERT_TRUE(timedTailView.valid());
        EXPECT_EQ(firstEndpointView.plan->timingPolicy, Graphics::GpuTaskTimingPolicy::None);
        EXPECT_EQ(timedTailView.plan->timingPolicy, Graphics::GpuTaskTimingPolicy::PacketOnly);

        oldRange = compiledRange;
        oldPlanGeneration = views.compiled.planGeneration();
    }
    ASSERT_TRUE(Compile(graph, analysis, assignments, compiledGraph, options));
    u64 recompiledPlanGeneration = 0u;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        EXPECT_NE(views.compiled.planGeneration(), oldPlanGeneration);
        const Graphics::GpuSubmissionPacketRange recompiledRange = views.compiled.packetTimingEnvelopeRange();
        ASSERT_TRUE(recompiledRange.valid());
        EXPECT_EQ(recompiledRange.packetCount, expectedRange.packetCount);
        EXPECT_FALSE(views.compiled.validPacketRange(oldRange));
        recompiledPlanGeneration = views.compiled.planGeneration();
    }
    ASSERT_TRUE(Compile(graph, analysis, assignments, compiledGraph));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        EXPECT_NE(views.compiled.planGeneration(), recompiledPlanGeneration);
        EXPECT_FALSE(views.compiled.packetTimingEnvelopeRange().valid());
        EXPECT_FALSE(views.compiled.validPacketRange(oldRange));
        for(usize packetIndex = 0u; packetIndex < views.compiled.packetCount(); ++packetIndex){
            const Graphics::GpuCompiledPacketView packet = views.compiled.packet(views.compiled.packetIdAt(packetIndex));
            ASSERT_TRUE(packet.valid());
            EXPECT_FALSE(packet.plan->recordsPacketEnvelopeTiming);
        }
        const Graphics::GpuCompiledPacketView tailPacket = views.compiled.packet(views.compiled.packetForTask(timedTail));
        ASSERT_TRUE(tailPacket.valid());
        EXPECT_TRUE(tailPacket.plan->recordsTiming);
    }

    compiledGraph.reset();
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        EXPECT_FALSE(views.compiled.packetTimingEnvelopeRange().valid());
    }
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
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        EXPECT_FALSE(views.compiled.valid());
    }

    options.packetTimingEnvelope = {};
    options.packetTimingEnvelope.lastTask = third;
    EXPECT_FALSE(options.packetTimingEnvelope.enabled());
    EXPECT_FALSE(Compile(graph, analysis, assignments, compiledGraph, options));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        EXPECT_FALSE(views.compiled.valid());
    }

    Graphics::GpuTaskGraph foreignGraph(testArena.arena);
    const Graphics::GpuTaskId foreign = AddTask(
        foreignGraph,
        Name("tests/task_graph_timing/invalid_foreign")
    );
    ASSERT_TRUE(foreign.valid());
    options.packetTimingEnvelope = { .firstTask = first, .lastTask = foreign };
    ASSERT_TRUE(options.packetTimingEnvelope.enabled());
    EXPECT_FALSE(Compile(graph, analysis, assignments, compiledGraph, options));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        EXPECT_FALSE(views.compiled.valid());
    }

    options.packetTimingEnvelope = { .firstTask = third, .lastTask = first };
    EXPECT_FALSE(Compile(graph, analysis, assignments, compiledGraph, options));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        EXPECT_FALSE(views.compiled.valid());
    }

    options.packetTimingEnvelope = { .firstTask = second, .lastTask = second };
    ASSERT_TRUE(Compile(graph, analysis, assignments, compiledGraph, options));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        const Graphics::GpuSubmissionPacketRange range = views.compiled.packetTimingEnvelopeRange();
        ASSERT_TRUE(range.valid());
        EXPECT_EQ(range.packetCount, 1u);
    }

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
    {
        const GpuTaskGraphReadViews views(mergedGraph, compiledGraph);
        EXPECT_FALSE(views.compiled.valid());
    }

    Graphics::GpuTaskGraph staleGraph(testArena.arena);
    const Graphics::GpuTaskId staleEndpoint = AddTask(
        staleGraph,
        Name("tests/task_graph_timing/invalid_stale")
    );
    ASSERT_TRUE(staleEndpoint.valid());
    options.packetTimingEnvelope = { .firstTask = staleEndpoint, .lastTask = staleEndpoint };
    staleGraph.reset();
    EXPECT_FALSE(Compile(staleGraph, analysis, assignments, compiledGraph, options));
    {
        const GpuTaskGraphReadViews views(staleGraph, compiledGraph);
        EXPECT_FALSE(views.compiled.valid());
    }
}

TEST(GpuTaskGraphTiming, ResolvesEnvelopeByTopologicalPositionInsteadOfTaskIndex){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    u64 graphGeneration = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        graphGeneration = declarations.generation();
    }
    const Graphics::GpuTaskId futureEarly{ 2u, graphGeneration };
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
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        EXPECT_LT(views.compiled.packetForTask(early).index, views.compiled.packetForTask(late).index);
        const Graphics::GpuSubmissionPacketRange range = views.compiled.packetTimingEnvelopeRange();
        ASSERT_TRUE(range.valid());
        EXPECT_EQ(range.packetCount, 2u);
    }

    options.packetTimingEnvelope = { .firstTask = late, .lastTask = early };
    EXPECT_FALSE(Compile(graph, analysis, assignments, compiledGraph, options));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        EXPECT_FALSE(views.compiled.valid());
    }
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
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        ASSERT_TRUE(views.compiled.packetTimingEnvelopeRange().valid());
    }

    options.packetTimingEnvelope = { .firstTask = first, .lastTask = last };
    EXPECT_FALSE(Compile(graph, analysis, assignments, compiledGraph, options));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        EXPECT_FALSE(views.compiled.valid());
    }
    options.packetTimingEnvelope = { .firstTask = recovery, .lastTask = recovery };
    EXPECT_FALSE(Compile(graph, analysis, assignments, compiledGraph, options));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        EXPECT_FALSE(views.compiled.valid());
    }
    options.packetTimingEnvelope = { .firstTask = last, .lastTask = last };
    EXPECT_FALSE(Compile(graph, analysis, assignments, compiledGraph, options));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        EXPECT_FALSE(views.compiled.valid());
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

