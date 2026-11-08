// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_task_graph_read_views.h"
#include "task_graph_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>
#include <tests/common/test_context.h>

#include <core/task/gpu/compiler_internal.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_timing_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct TaskGraphTimingTestsTag>;


[[nodiscard]] Expected<Core::GpuTaskId> AddTask(Core::GpuTaskGraph& graph, const Name& identity, const Core::GpuTaskId dependency = {}, const Core::GpuTaskSchedulingHint& scheduling = {}, const Core::GpuTaskTimingMetadata& timing = {}){
    Core::GpuTaskDesc desc;
    desc.setIdentity(identity).setMarkerLabel("Task Graph Timing Test").setScheduling(scheduling).setTiming(timing);
    if(dependency.valid())
        desc.setDependencies(Span(&dependency, 1u));
    return graph.addTask(desc);
}

[[nodiscard]] b8 Compile(
    Core::GpuTaskGraphAnalysis& analysis,
    Core::GpuTaskGraphQueueAssignments& assignments,
    Core::GpuCompiledGraph& compiledGraph,
    Core::Alloc::ScratchArena& scratchArena,
    const Core::GpuTaskGraph& graph,
    const Core::GpuTaskGraphCompileOptions& options = {}
){
    const Core::GpuPhysicalQueueInfo queue = TaskGraphTestUtils::GraphicsQueue();
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u };
    return TaskGraphTestUtils::Compile(analysis, assignments, compiledGraph, scratchArena, graph, topology, options);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphTiming, CompilesInclusivePacketEnvelopeWithoutChangingTaskPolicies){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto prefixResult = AddTask(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingEnvelopePrefix));
    ASSERT_TRUE(prefixResult);
    const Core::GpuTaskId prefix = *prefixResult;
    ASSERT_TRUE(prefix.valid());
    const auto firstPacketPrefixResult = AddTask(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingEnvelopeFirstPacketPrefix), prefix);
    ASSERT_TRUE(firstPacketPrefixResult);
    const Core::GpuTaskId firstPacketPrefix = *firstPacketPrefixResult;
    ASSERT_TRUE(firstPacketPrefix.valid());

    Core::GpuTaskSchedulingHint mergedScheduling;
    mergedScheduling.mergeWithPrevious = true;
    const auto firstEndpointResult = AddTask(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingEnvelopeFirstEndpoint), firstPacketPrefix, mergedScheduling);
    ASSERT_TRUE(firstEndpointResult);
    const Core::GpuTaskId firstEndpoint = *firstEndpointResult;
    ASSERT_TRUE(firstEndpoint.valid());
    const auto middleResult = AddTask(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingEnvelopeMiddle), firstEndpoint);
    ASSERT_TRUE(middleResult);
    const Core::GpuTaskId middle = *middleResult;
    ASSERT_TRUE(middle.valid());
    const auto lastEndpointResult = AddTask(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingEnvelopeLastEndpoint), middle);
    ASSERT_TRUE(lastEndpointResult);
    const Core::GpuTaskId lastEndpoint = *lastEndpointResult;
    ASSERT_TRUE(lastEndpoint.valid());
    const auto lastPacketSuffixResult = AddTask(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingEnvelopeLastPacketSuffix), lastEndpoint, mergedScheduling);
    ASSERT_TRUE(lastPacketSuffixResult);
    const Core::GpuTaskId lastPacketSuffix = *lastPacketSuffixResult;
    ASSERT_TRUE(lastPacketSuffix.valid());

    Core::GpuTaskTimingMetadata packetOnlyTiming;
    packetOnlyTiming.policy = Core::GpuTaskTimingPolicy::PacketOnly;
    const auto timedTailResult = AddTask(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingEnvelopeTimedTail), lastPacketSuffix, {}, packetOnlyTiming);
    ASSERT_TRUE(timedTailResult);
    const Core::GpuTaskId timedTail = *timedTailResult;
    ASSERT_TRUE(timedTail.valid());

    Core::GpuTaskGraphCompileOptions options;
    options.packetTimingEnvelope.firstTask = firstEndpoint;
    options.packetTimingEnvelope.lastTask = lastEndpoint;
    ASSERT_TRUE(options.packetTimingEnvelope.enabled());
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, options));
    Core::GpuSubmissionPacketRange expectedRange;
    Core::GpuSubmissionPacketRange oldRange;
    u64 oldPlanGeneration = 0u;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        ASSERT_EQ(views.compiled.packetCount(), 5u);

        const Core::GpuSubmissionPacketId prefixPacket = views.compiled.packetForTask(prefix);
        const Core::GpuSubmissionPacketId firstPacket = views.compiled.packetForTask(firstEndpoint);
        const Core::GpuSubmissionPacketId middlePacket = views.compiled.packetForTask(middle);
        const Core::GpuSubmissionPacketId lastPacket = views.compiled.packetForTask(lastEndpoint);
        const Core::GpuSubmissionPacketId tailPacket = views.compiled.packetForTask(timedTail);
        EXPECT_EQ(views.compiled.packetForTask(firstPacketPrefix), firstPacket);
        EXPECT_EQ(views.compiled.packetForTask(lastPacketSuffix), lastPacket);
        expectedRange = views.compiled.packetRange(firstPacket, lastPacket);
        ASSERT_TRUE(expectedRange.valid());
        ASSERT_EQ(expectedRange.packetCount, 3u);
        const Core::GpuSubmissionPacketRange compiledRange = views.compiled.packetTimingEnvelopeRange();
        EXPECT_EQ(compiledRange.first, expectedRange.first);
        EXPECT_EQ(compiledRange.packetCount, expectedRange.packetCount);

        const Core::GpuCompiledPacketView prefixPacketView = views.compiled.packet(prefixPacket);
        const Core::GpuCompiledPacketView firstPacketView = views.compiled.packet(firstPacket);
        const Core::GpuCompiledPacketView middlePacketView = views.compiled.packet(middlePacket);
        const Core::GpuCompiledPacketView lastPacketView = views.compiled.packet(lastPacket);
        const Core::GpuCompiledPacketView tailPacketView = views.compiled.packet(tailPacket);
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
        const Core::GpuCompiledTaskView firstEndpointView = views.compiled.findTask(firstEndpoint);
        const Core::GpuCompiledTaskView timedTailView = views.compiled.findTask(timedTail);
        ASSERT_TRUE(firstEndpointView.valid());
        ASSERT_TRUE(timedTailView.valid());
        EXPECT_EQ(firstEndpointView.plan->timingPolicy, Core::GpuTaskTimingPolicy::None);
        EXPECT_EQ(timedTailView.plan->timingPolicy, Core::GpuTaskTimingPolicy::PacketOnly);

        oldRange = compiledRange;
        oldPlanGeneration = views.compiled.planGeneration();
    }
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, options));
    u64 recompiledPlanGeneration = 0u;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        EXPECT_NE(views.compiled.planGeneration(), oldPlanGeneration);
        const Core::GpuSubmissionPacketRange recompiledRange = views.compiled.packetTimingEnvelopeRange();
        ASSERT_TRUE(recompiledRange.valid());
        EXPECT_EQ(recompiledRange.packetCount, expectedRange.packetCount);
        EXPECT_FALSE(views.compiled.validPacketRange(oldRange));
        recompiledPlanGeneration = views.compiled.planGeneration();
    }
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        EXPECT_NE(views.compiled.planGeneration(), recompiledPlanGeneration);
        EXPECT_FALSE(views.compiled.packetTimingEnvelopeRange().valid());
        EXPECT_FALSE(views.compiled.validPacketRange(oldRange));
        for(usize packetIndex = 0u; packetIndex < views.compiled.packetCount(); ++packetIndex){
            const Core::GpuCompiledPacketView packet = views.compiled.packet(views.compiled.packetIdAt(packetIndex));
            ASSERT_TRUE(packet.valid());
            EXPECT_FALSE(packet.plan->recordsPacketEnvelopeTiming);
        }
        const Core::GpuCompiledPacketView tailPacket = views.compiled.packet(views.compiled.packetForTask(timedTail));
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
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto firstResult = AddTask(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingInvalidFirst));
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    const auto secondResult = AddTask(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingInvalidSecond), first);
    ASSERT_TRUE(secondResult);
    const Core::GpuTaskId second = *secondResult;
    const auto thirdResult = AddTask(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingInvalidThird), second);
    ASSERT_TRUE(thirdResult);
    const Core::GpuTaskId third = *thirdResult;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    ASSERT_TRUE(third.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    Core::GpuTaskGraphCompileOptions options;

    options.packetTimingEnvelope.firstTask = first;
    EXPECT_FALSE(options.packetTimingEnvelope.enabled());
    EXPECT_FALSE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, options));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        EXPECT_FALSE(views.compiled.valid());
    }

    options.packetTimingEnvelope = {};
    options.packetTimingEnvelope.lastTask = third;
    EXPECT_FALSE(options.packetTimingEnvelope.enabled());
    EXPECT_FALSE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, options));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        EXPECT_FALSE(views.compiled.valid());
    }

    Core::GpuTaskGraph foreignGraph(testArena.arena);
    const auto foreignResult = AddTask(foreignGraph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingInvalidForeign));
    ASSERT_TRUE(foreignResult);
    const Core::GpuTaskId foreign = *foreignResult;
    ASSERT_TRUE(foreign.valid());
    options.packetTimingEnvelope = { .firstTask = first, .lastTask = foreign };
    ASSERT_TRUE(options.packetTimingEnvelope.enabled());
    EXPECT_FALSE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, options));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        EXPECT_FALSE(views.compiled.valid());
    }

    options.packetTimingEnvelope = { .firstTask = third, .lastTask = first };
    EXPECT_FALSE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, options));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        EXPECT_FALSE(views.compiled.valid());
    }

    options.packetTimingEnvelope = { .firstTask = second, .lastTask = second };
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, options));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        const Core::GpuSubmissionPacketRange range = views.compiled.packetTimingEnvelopeRange();
        ASSERT_TRUE(range.valid());
        EXPECT_EQ(range.packetCount, 1u);
    }

    Core::GpuTaskGraph mergedGraph(testArena.arena);
    const auto mergedFirstResult = AddTask(mergedGraph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingInvalidMergedFirst));
    ASSERT_TRUE(mergedFirstResult);
    const Core::GpuTaskId mergedFirst = *mergedFirstResult;
    Core::GpuTaskSchedulingHint mergedScheduling;
    mergedScheduling.mergeWithPrevious = true;
    const auto mergedLastResult = AddTask(mergedGraph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingInvalidMergedLast), mergedFirst, mergedScheduling);
    ASSERT_TRUE(mergedLastResult);
    const Core::GpuTaskId mergedLast = *mergedLastResult;
    ASSERT_TRUE(mergedFirst.valid());
    ASSERT_TRUE(mergedLast.valid());
    options.packetTimingEnvelope = { .firstTask = mergedLast, .lastTask = mergedFirst };
    EXPECT_FALSE(Compile(analysis, assignments, compiledGraph, operationScratchArena, mergedGraph, options));
    {
        const GpuTaskGraphReadViews views(mergedGraph, compiledGraph);
        EXPECT_FALSE(views.compiled.valid());
    }

    Core::GpuTaskGraph staleGraph(testArena.arena);
    const auto staleEndpointResult = AddTask(staleGraph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingInvalidStale));
    ASSERT_TRUE(staleEndpointResult);
    const Core::GpuTaskId staleEndpoint = *staleEndpointResult;
    ASSERT_TRUE(staleEndpoint.valid());
    options.packetTimingEnvelope = { .firstTask = staleEndpoint, .lastTask = staleEndpoint };
    staleGraph.reset();
    EXPECT_FALSE(Compile(analysis, assignments, compiledGraph, operationScratchArena, staleGraph, options));
    {
        const GpuTaskGraphReadViews views(staleGraph, compiledGraph);
        EXPECT_FALSE(views.compiled.valid());
    }
}

TEST(GpuTaskGraphTiming, ResolvesEnvelopeByTopologicalPositionInsteadOfTaskIndex){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    u64 graphGeneration = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        graphGeneration = declarations.generation();
    }
    const Core::GpuTaskId futureEarly{ .generation = graphGeneration, .index = 2u };
    const auto lateResult = AddTask(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingTopologicalLate), futureEarly, scheduling);
    ASSERT_TRUE(lateResult);
    const Core::GpuTaskId late = *lateResult;
    const auto prefixResult = AddTask(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingTopologicalPrefix), {}, scheduling);
    ASSERT_TRUE(prefixResult);
    const Core::GpuTaskId prefix = *prefixResult;
    const auto earlyResult = AddTask(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingTopologicalEarly), prefix, scheduling);
    ASSERT_TRUE(earlyResult);
    const Core::GpuTaskId early = *earlyResult;
    ASSERT_TRUE(late.valid());
    ASSERT_TRUE(prefix.valid());
    ASSERT_TRUE(early.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    Core::GpuTaskGraphCompileOptions options;
    options.packetTimingEnvelope = { .firstTask = early, .lastTask = late };
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, options));
    ASSERT_EQ(analysis.topologicalOrder().size(), 3u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], prefix);
    EXPECT_EQ(analysis.topologicalOrder()[1u], early);
    EXPECT_EQ(analysis.topologicalOrder()[2u], late);
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        EXPECT_LT(views.compiled.packetForTask(early).index, views.compiled.packetForTask(late).index);
        const Core::GpuSubmissionPacketRange range = views.compiled.packetTimingEnvelopeRange();
        ASSERT_TRUE(range.valid());
        EXPECT_EQ(range.packetCount, 2u);
    }

    options.packetTimingEnvelope = { .firstTask = late, .lastTask = early };
    EXPECT_FALSE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, options));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        EXPECT_FALSE(views.compiled.valid());
    }
}

TEST(GpuTaskGraphTiming, RejectsEnvelopeRangesAtOrAfterAcceptedQueueFrontierPackets){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto firstResult = AddTask(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingFrontierFirst));
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());

    Core::GpuTaskSchedulingHint frontierScheduling;
    frontierScheduling.forceSubmissionBoundary = true;
    frontierScheduling.allowPacketMerge = false;
    frontierScheduling.joinsAcceptedQueueFrontier = true;
    const auto recoveryResult = AddTask(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingFrontierRecovery), {}, frontierScheduling);
    ASSERT_TRUE(recoveryResult);
    const Core::GpuTaskId recovery = *recoveryResult;
    ASSERT_TRUE(recovery.valid());
    const auto lastResult = AddTask(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingFrontierLast), recovery);
    ASSERT_TRUE(lastResult);
    const Core::GpuTaskId last = *lastResult;
    ASSERT_TRUE(last.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    Core::GpuTaskGraphCompileOptions options;
    options.packetTimingEnvelope = { .firstTask = first, .lastTask = first };
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, options));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        ASSERT_TRUE(views.compiled.packetTimingEnvelopeRange().valid());
    }

    options.packetTimingEnvelope = { .firstTask = first, .lastTask = last };
    EXPECT_FALSE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, options));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        EXPECT_FALSE(views.compiled.valid());
    }
    options.packetTimingEnvelope = { .firstTask = recovery, .lastTask = recovery };
    EXPECT_FALSE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, options));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        EXPECT_FALSE(views.compiled.valid());
    }
    options.packetTimingEnvelope = { .firstTask = last, .lastTask = last };
    EXPECT_FALSE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, options));
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

