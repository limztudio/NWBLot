// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_packet_runtime_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, ClampsReadyFrontierWorkerUtilization){
    Graphics::GpuTaskGraphRecordingStatistics statistics;
    EXPECT_EQ(statistics.readyFrontierWorkerUtilization(), 0.0);

    statistics.readyFrontierWorkerBusySeconds = 0.25;
    statistics.readyFrontierWorkerCapacitySeconds = 1.0;
    EXPECT_EQ(statistics.readyFrontierWorkerUtilization(), 0.25);

    statistics.readyFrontierWorkerBusySeconds = 2.0;
    EXPECT_EQ(statistics.readyFrontierWorkerUtilization(), 1.0);

    statistics.readyFrontierWorkerBusySeconds = -1.0;
    EXPECT_EQ(statistics.readyFrontierWorkerUtilization(), 0.0);
}

TEST(GpuTaskGraph, RecreatesPacketRecordingStateAfterRecompile){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);

    const Graphics::GpuTaskId firstTask = AddTask(
        graph,
        Name("tests/task_graph/recreate_recording_first"),
        "Recreate Recording First"
    );
    ASSERT_TRUE(firstTask.valid());
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    Graphics::GpuSubmissionPacketId firstPacket;
    Graphics::GpuPhysicalQueueId firstQueue;
    u64 firstCompiledGeneration = 0u;
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        firstPacket = compiledPlan.packetForTask(firstTask);
        ASSERT_TRUE(firstPacket.valid());
        firstQueue = compiledPlan.packet(firstPacket).plan->queue;
        ASSERT_TRUE(firstQueue.valid());
        firstCompiledGeneration = compiledPlan.generation();
    }

    Graphics::GpuRecordedGraph recordedGraph(testArena.arena);
    Graphics::GpuGraphSubmissionTransaction transaction(testArena.arena);
    recordedGraph.reset(compiledGraph);
    transaction.reset(compiledGraph);
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        ASSERT_TRUE(recordedGraph.validFor(compiledGraph, compiledPlan));
        ASSERT_TRUE(transaction.validFor(compiledPlan));
        const Graphics::GpuTaskGraphPhysicalQueueRecordingStatistics firstQueueStatistics =
            recordedGraph.physicalQueueRecordingStatistics(compiledGraph, compiledPlan, firstQueue)
        ;
        ASSERT_TRUE(firstQueueStatistics.valid());
        EXPECT_EQ(firstQueueStatistics.graphGeneration, compiledPlan.generation());
        EXPECT_EQ(firstQueueStatistics.planGeneration, compiledPlan.planGeneration());
        EXPECT_EQ(firstQueueStatistics.recordingAttemptGeneration, 0u);
        EXPECT_EQ(firstQueueStatistics.deviceGeneration, compiledPlan.deviceGeneration());
        EXPECT_EQ(firstQueueStatistics.queue, firstQueue);
        EXPECT_EQ(firstQueueStatistics.queueClass, Graphics::CommandQueue::Graphics);
        EXPECT_EQ(firstQueueStatistics.packetCount, 0u);
        EXPECT_EQ(firstQueueStatistics.taskCount, 0u);
        EXPECT_EQ(firstQueueStatistics.commandListCount, 0u);
        EXPECT_EQ(firstQueueStatistics.barrierCount, 0u);
        EXPECT_EQ(firstQueueStatistics.workerRoutedPacketCount, 0u);
        EXPECT_EQ(firstQueueStatistics.parallelPacketCount, 0u);
        EXPECT_EQ(firstQueueStatistics.commandListAcquisitionSeconds, 0.0);
        EXPECT_EQ(firstQueueStatistics.graphBarrierRecordingSeconds, 0.0);
        EXPECT_EQ(firstQueueStatistics.taskRecordSeconds, 0.0);
        EXPECT_EQ(firstQueueStatistics.recordingSeconds, 0.0);
        const Graphics::GpuPhysicalQueueId staleFirstQueue{
            firstQueue.index,
            static_cast<u16>(
                firstQueue.deviceGeneration == Limit<u16>::s_Max
                    ? 1u
                    : firstQueue.deviceGeneration + 1u
            ),
        };
        EXPECT_FALSE(
            recordedGraph.physicalQueueRecordingStatistics(compiledGraph, compiledPlan, staleFirstQueue).valid()
        );
        // A current-generation ID is still invalid when the compiled plan has no matching physical topology entry.
        const Graphics::GpuPhysicalQueueId nonPlanQueue{ 3u, compiledPlan.deviceGeneration() };
        EXPECT_TRUE(nonPlanQueue.valid());
        EXPECT_FALSE(recordedGraph.physicalQueueRecordingStatistics(compiledGraph, compiledPlan, nonPlanQueue).valid());
    }

    graph.reset();
    const Graphics::GpuTaskId secondTask = AddTask(
        graph,
        Name("tests/task_graph/recreate_recording_second"),
        "Recreate Recording Second"
    );
    ASSERT_TRUE(secondTask.valid());
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    Graphics::GpuSubmissionPacketId secondPacket;
    Graphics::GpuPhysicalQueueId secondQueue;
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        ASSERT_NE(compiledPlan.generation(), firstCompiledGeneration);
        secondPacket = compiledPlan.packetForTask(secondTask);
        ASSERT_TRUE(secondPacket.valid());
        secondQueue = compiledPlan.packet(secondPacket).plan->queue;
        ASSERT_TRUE(secondQueue.valid());
        EXPECT_FALSE(recordedGraph.validFor(compiledGraph, compiledPlan));
        EXPECT_FALSE(transaction.validFor(compiledPlan));
        EXPECT_FALSE(recordedGraph.physicalQueueRecordingStatistics(compiledGraph, compiledPlan, secondQueue).valid());
    }

    // reset() releases old packet-owned command-list handles and reconstructs the serial/per-packet recording
    // scratch for the new immutable graph generation before a future command arena can be leased again.
    recordedGraph.reset(compiledGraph);
    transaction.reset(compiledGraph);
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    EXPECT_TRUE(recordedGraph.validFor(compiledGraph, compiledPlan));
    EXPECT_TRUE(transaction.validFor(compiledPlan));
    const Graphics::GpuTaskGraphPhysicalQueueRecordingStatistics secondQueueStatistics =
        recordedGraph.physicalQueueRecordingStatistics(compiledGraph, compiledPlan, secondQueue)
    ;
    ASSERT_TRUE(secondQueueStatistics.valid());
    EXPECT_EQ(secondQueueStatistics.graphGeneration, compiledPlan.generation());
    EXPECT_EQ(secondQueueStatistics.planGeneration, compiledPlan.planGeneration());
    EXPECT_EQ(secondQueueStatistics.recordingAttemptGeneration, 0u);
    EXPECT_EQ(secondQueueStatistics.deviceGeneration, compiledPlan.deviceGeneration());
    EXPECT_EQ(secondQueueStatistics.queue, secondQueue);
    EXPECT_EQ(secondQueueStatistics.queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(secondQueueStatistics.packetCount, 0u);
    EXPECT_EQ(secondQueueStatistics.taskCount, 0u);
    EXPECT_EQ(secondQueueStatistics.commandListCount, 0u);
    EXPECT_EQ(secondQueueStatistics.barrierCount, 0u);
    EXPECT_EQ(secondQueueStatistics.workerRoutedPacketCount, 0u);
    EXPECT_EQ(secondQueueStatistics.parallelPacketCount, 0u);
    EXPECT_EQ(secondQueueStatistics.commandListAcquisitionSeconds, 0.0);
    EXPECT_EQ(secondQueueStatistics.graphBarrierRecordingSeconds, 0.0);
    EXPECT_EQ(secondQueueStatistics.taskRecordSeconds, 0.0);
    EXPECT_EQ(secondQueueStatistics.recordingSeconds, 0.0);
}

TEST(GpuTaskGraph, InvalidatesPacketRuntimeAndCaptureForSameGraphRecompile){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        true,
        true,
    };
    const Graphics::GpuTaskId task = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/same_graph_recompile"),
        "Same Graph Recompile",
        computeRequest
    );
    ASSERT_TRUE(task.valid());

    const Graphics::GpuPhysicalQueueInfo graphicsOnly[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology firstTopology{
        .queues = graphicsOnly,
        .queueCount = LengthOf(graphicsOnly),
    };
    const Graphics::GpuPhysicalQueueInfo graphicsAndCompute[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology secondTopology{
        .queues = graphicsAndCompute,
        .queueCount = LengthOf(graphicsAndCompute),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, firstTopology, assignments, compiledGraph));
    u64 graphGeneration = 0u;
    u64 firstPlanGeneration = 0u;
    Graphics::GpuSubmissionPacketId firstPacket;
    Graphics::GpuPhysicalQueueId firstQueue;
    {
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);

        ASSERT_TRUE(reads.valid());
        ASSERT_EQ(reads.compiled.packetCount(), 1u);
        graphGeneration = reads.declarations.generation();
        firstPlanGeneration = reads.compiled.planGeneration();
        firstPacket = reads.compiled.packetForTask(task);
        ASSERT_TRUE(firstPacket.valid());
        EXPECT_EQ(firstPacket.generation, firstPlanGeneration);
        firstQueue = reads.compiled.packet(firstPacket).plan->queue;
        ASSERT_TRUE(firstQueue.valid());
    }

    Graphics::GpuRecordedGraph recordedGraph(testArena.arena);
    Graphics::GpuGraphSubmissionTransaction transaction(testArena.arena);
    recordedGraph.reset(compiledGraph);
    transaction.reset(compiledGraph);
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        ASSERT_TRUE(recordedGraph.validFor(compiledGraph, compiledPlan));
        ASSERT_TRUE(transaction.validFor(compiledPlan));
    }

    Graphics::GpuCommandIrCapture capture(testArena.arena);
    ASSERT_TRUE(capture.captureClearBuffer(
        task,
        firstPacket,
        firstQueue,
        Graphics::GpuGraphResourceId{ 0u, graphGeneration },
        0xdecafbadU
    ));
    ASSERT_EQ(capture.planGeneration(), firstPlanGeneration);

    ASSERT_TRUE(Compile(graph, analysis, secondTopology, assignments, compiledGraph));
    const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);

    ASSERT_TRUE(reads.valid());
    ASSERT_EQ(reads.compiled.generation(), graphGeneration);
    ASSERT_EQ(reads.compiled.packetCount(), 1u);
    const Graphics::GpuSubmissionPacketId secondPacket = reads.compiled.packetForTask(task);
    ASSERT_TRUE(secondPacket.valid());
    EXPECT_NE(reads.compiled.planGeneration(), firstPlanGeneration);
    EXPECT_NE(secondPacket, firstPacket);
    EXPECT_FALSE(reads.compiled.validPacket(firstPacket));
    EXPECT_FALSE(recordedGraph.validFor(compiledGraph, reads.compiled));
    EXPECT_FALSE(transaction.validFor(reads.compiled));

    const Graphics::GpuCommandIrReplayResult staleCapture = Graphics::PreflightGpuCommandIrPacket(
        capture.commandBytes(),
        reads.declarations,
        reads.compiled,
        secondPacket
    );
    EXPECT_EQ(staleCapture.error, Graphics::GpuCommandIrReplayError::PlanGenerationMismatch);
    EXPECT_TRUE(staleCapture.streamValidation.valid());
}

TEST(GpuTaskGraph, CompiledTaskLookupRejectsOutOfRangeStaleAndUncompiledHandles){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskId first = AddTask(
        graph,
        Name("tests/task_graph/compiled_lookup_first"),
        "Compiled Lookup First"
    );
    ASSERT_TRUE(first.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    usize graphTaskCount = 0u;
    u64 graphGeneration = 0u;
    {
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);

        ASSERT_TRUE(reads.valid());
        ASSERT_NE(reads.compiled.findTask(first).plan, nullptr);
        graphTaskCount = reads.declarations.taskCount();
        graphGeneration = reads.declarations.generation();
    }

    const auto expectMissingTask = [&assignments](
        const Graphics::GpuCompiledGraph::ReadView& compiledPlan,
        const Graphics::GpuTaskId task
    ){
        EXPECT_EQ(assignments.find(task), nullptr);
        EXPECT_EQ(compiledPlan.findTask(task).plan, nullptr);
        EXPECT_FALSE(compiledPlan.packetForTask(task).valid());
        EXPECT_EQ(
            compiledPlan.packetizationDecisionForTask(task),
            Graphics::GpuTaskPacketizationDecision::Unknown
        );
    };
    const Graphics::GpuTaskId onePastCompiledTasks{
        static_cast<u32>(graphTaskCount),
        graphGeneration,
    };
    const Graphics::GpuTaskId largeSameGenerationTask{
        Limit<u32>::s_Max - 1u,
        graphGeneration,
    };
    ASSERT_TRUE(onePastCompiledTasks.valid());
    ASSERT_TRUE(largeSameGenerationTask.valid());
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        expectMissingTask(compiledPlan, onePastCompiledTasks);
        expectMissingTask(compiledPlan, largeSameGenerationTask);
    }

    Graphics::GpuTaskGraph foreignGraph(testArena.arena);
    const Graphics::GpuTaskId foreign = AddTask(
        foreignGraph,
        Name("tests/task_graph/compiled_lookup_foreign"),
        "Compiled Lookup Foreign"
    );
    ASSERT_TRUE(foreign.valid());
    ASSERT_NE(foreign.generation, graphGeneration);
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        expectMissingTask(compiledPlan, foreign);
    }

    const Graphics::GpuTaskId appended = AddTask(
        graph,
        Name("tests/task_graph/compiled_lookup_appended"),
        "Compiled Lookup Appended"
    );
    ASSERT_TRUE(appended.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        EXPECT_FALSE(compiledPlan.validFor(declarations));
        expectMissingTask(compiledPlan, appended);
    }

    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    {
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);

        ASSERT_TRUE(reads.valid());
        ASSERT_NE(assignments.find(first), nullptr);
        ASSERT_NE(assignments.find(appended), nullptr);
        ASSERT_NE(reads.compiled.findTask(first).plan, nullptr);
        ASSERT_NE(reads.compiled.findTask(appended).plan, nullptr);
    }

    graph.reset();
    const Graphics::GpuTaskId replacement = AddTask(
        graph,
        Name("tests/task_graph/compiled_lookup_replacement"),
        "Compiled Lookup Replacement"
    );
    ASSERT_TRUE(replacement.valid());
    ASSERT_NE(replacement.generation, first.generation);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    {
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);

        ASSERT_TRUE(reads.valid());
        expectMissingTask(reads.compiled, first);
        expectMissingTask(reads.compiled, appended);
        ASSERT_NE(assignments.find(replacement), nullptr);
        ASSERT_NE(reads.compiled.findTask(replacement).plan, nullptr);
        EXPECT_TRUE(reads.compiled.packetForTask(replacement).valid());
    }
}

TEST(GpuTaskGraph, CompiledTaskLookupScalesAcrossDenseTaskIds){
    constexpr usize s_TaskCount = 4096u;
    constexpr usize s_QuerySweepCount = 16u;

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Core::Alloc::ScratchArena lookupScratchArena(Name("tests/graphics/task_graph_lookup_scale_scratch"));
    Vector<Graphics::GpuTaskId, Core::Alloc::ScratchArena> tasks(lookupScratchArena);
    tasks.reserve(s_TaskCount);
    const Name taskBaseName("tests/task_graph/compiled_lookup_scale_task_");
    char taskIndexBuffer[32u] = {};
    for(usize taskIndex = 0u; taskIndex < s_TaskCount; ++taskIndex){
        const Graphics::GpuTaskId task = AddTask(
            graph,
            DeriveName(taskBaseName, FormatDecimal(taskIndex, taskIndexBuffer)),
            "Compiled Lookup Scale Task"
        );
        ASSERT_TRUE(task.valid());
        tasks.push_back(task);
    }

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.taskCount(), s_TaskCount);

    u64 assignmentChecksum = 0u;
    u64 packetChecksum = 0u;
    for(usize sweepIndex = 0u; sweepIndex < s_QuerySweepCount; ++sweepIndex){
        for(usize queryIndex = 0u; queryIndex < s_TaskCount; ++queryIndex){
            const usize taskIndex = (queryIndex * 4051u + sweepIndex) % s_TaskCount;
            const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(tasks[taskIndex]);
            if(!assignment || assignment->task != tasks[taskIndex]){
                ADD_FAILURE() << "Dense queue-assignment lookup failed at task " << taskIndex;
                return;
            }
            const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(tasks[taskIndex]).plan;
            if(!compiledTask || compiledTask->task != tasks[taskIndex] || !compiledTask->packet.valid()){
                ADD_FAILURE() << "Dense compiled-task lookup failed at task " << taskIndex;
                return;
            }
            assignmentChecksum += static_cast<u64>(assignment->task.index) + 1u;
            packetChecksum += static_cast<u64>(compiledTask->packet.index) + 1u;
        }
    }
    const u64 expectedSweepChecksum = static_cast<u64>(s_TaskCount) * (s_TaskCount + 1u) / 2u;
    EXPECT_EQ(assignmentChecksum, expectedSweepChecksum * s_QuerySweepCount);
    EXPECT_EQ(packetChecksum, expectedSweepChecksum * s_QuerySweepCount);
}

TEST(GpuTaskGraph, RejectsRecoverySubmissionWithoutAcceptedQueueFrontierRole){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskSchedulingHint recoveryScheduling;
    recoveryScheduling.forceSubmissionBoundary = true;
    recoveryScheduling.allowPacketMerge = false;
    recoveryScheduling.isRecoverySubmission = true;
    Graphics::GpuTaskDesc recoveryDesc;
    recoveryDesc
        .setIdentity(Name("tests/task_graph/recovery_without_frontier"))
        .setMarkerLabel("Recovery Without Frontier")
        .setScheduling(recoveryScheduling)
    ;
    const Graphics::GpuTaskId recovery = graph.addTask(recoveryDesc);
    ASSERT_TRUE(recovery.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, analysis));
    EXPECT_FALSE(analysis.valid());
    EXPECT_EQ(
        analysis.diagnostic().status,
        Graphics::GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask
    );
    EXPECT_EQ(analysis.diagnostic().task, recovery);
    EXPECT_FALSE(analysis.diagnostic().relatedTask.valid());
    EXPECT_FALSE(analysis.diagnostic().resource.valid());
}

TEST(GpuTaskGraph, RejectsAcceptedQueueFrontierTasksWithPrerequisites){
    TestArena testArena;
    Graphics::GpuTaskSchedulingHint frontierScheduling;
    frontierScheduling.forceSubmissionBoundary = true;
    frontierScheduling.allowPacketMerge = false;
    frontierScheduling.joinsAcceptedQueueFrontier = true;

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuTaskId predecessor = AddTask(
            graph,
            Name("tests/task_graph/frontier_explicit_predecessor"),
            "Frontier Explicit Predecessor"
        );
        ASSERT_TRUE(predecessor.valid());
        Graphics::GpuTaskDesc recoveryDesc;
        recoveryDesc
            .setIdentity(Name("tests/task_graph/frontier_explicit_recovery"))
            .setMarkerLabel("Frontier Explicit Recovery")
            .setScheduling(frontierScheduling)
            .setDependencies(&predecessor, 1u)
        ;
        const Graphics::GpuTaskId recovery = graph.addTask(recoveryDesc);
        ASSERT_TRUE(recovery.valid());

        Graphics::GpuTaskGraph validGraph(testArena.arena);
        ASSERT_TRUE(AddTask(
            validGraph,
            Name("tests/task_graph/frontier_stale_assignment_source"),
            "Frontier Stale Assignment Source"
        ).valid());
        const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
        const Graphics::GpuTaskGraphQueueTopology topology{
            .queues = &queue,
            .queueCount = 1u,
        };
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(Compile(validGraph, analysis, topology, assignments, compiledGraph));
        ASSERT_TRUE(assignments.valid());
        {
            const Tests::GpuTaskGraphReadViews reads(validGraph, compiledGraph);

            ASSERT_TRUE(reads.valid());
        }

        EXPECT_FALSE(Compile(graph, analysis, topology, assignments, compiledGraph));
        EXPECT_FALSE(analysis.valid());
        EXPECT_FALSE(assignments.valid());
        {
            const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

            EXPECT_FALSE(compiledPlan.valid());
        }
        EXPECT_EQ(
            analysis.diagnostic().status,
            Graphics::GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask
        );
        EXPECT_EQ(analysis.diagnostic().task, recovery);
        EXPECT_EQ(analysis.diagnostic().relatedTask, predecessor);
        EXPECT_FALSE(analysis.diagnostic().resource.valid());
    }

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuExternalCompletionId completion = graph.importExternalCompletion(
            Graphics::GpuExternalCompletionDesc{}
                .setIdentity(Name("tests/task_graph/frontier_external_completion"))
                .setMarkerLabel("Frontier External Completion")
        );
        ASSERT_TRUE(completion.valid());
        Graphics::GpuTaskDesc recoveryDesc;
        recoveryDesc
            .setIdentity(Name("tests/task_graph/frontier_external_recovery"))
            .setMarkerLabel("Frontier External Recovery")
            .setScheduling(frontierScheduling)
            .setExternalDependencies(&completion, 1u)
        ;
        const Graphics::GpuTaskId recovery = graph.addTask(recoveryDesc);
        ASSERT_TRUE(recovery.valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(graph, analysis));
        EXPECT_EQ(
            analysis.diagnostic().status,
            Graphics::GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask
        );
        EXPECT_EQ(analysis.diagnostic().task, recovery);
    }

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::CommandListResourceStateHandoff externalStateSource(testArena.arena);
        ASSERT_FALSE(externalStateSource.valid());
        const Graphics::GpuTaskExternalStateSource externalStateSources[] = {
            Graphics::GpuTaskExternalStateSource{ .states = &externalStateSource },
        };
        Graphics::GpuTaskDesc recoveryDesc;
        recoveryDesc
            .setIdentity(Name("tests/task_graph/frontier_external_state_recovery"))
            .setMarkerLabel("Frontier External State Recovery")
            .setScheduling(frontierScheduling)
            .setExternalStateSources(externalStateSources, LengthOf(externalStateSources))
        ;
        EXPECT_FALSE(graph.addTask(recoveryDesc).valid());
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.taskCount(), 0u);
    }

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId recoveryDomain = AddHazardDomain(
            graph,
            Name("tests/task_graph/frontier_inferred_domain"),
            "Frontier Inferred Domain"
        );
        ASSERT_TRUE(recoveryDomain.valid());
        const Graphics::GpuTaskResourceUse predecessorUses[] = {
            Graphics::GpuTaskResourceUse{
                .resource = recoveryDomain,
                .range = {},
                .requiredState = Graphics::ResourceStates::Common,
                .access = Graphics::GpuTaskResourceAccess::Write,
            },
        };
        const Graphics::GpuTaskResourceUse recoveryUses[] = {
            Graphics::GpuTaskResourceUse{
                .resource = recoveryDomain,
                .range = {},
                .requiredState = Graphics::ResourceStates::Common,
                .access = Graphics::GpuTaskResourceAccess::Read,
            },
        };
        const Graphics::GpuTaskId predecessor = AddTask(
            graph,
            Name("tests/task_graph/frontier_inferred_predecessor"),
            "Frontier Inferred Predecessor",
            nullptr,
            0u,
            predecessorUses,
            LengthOf(predecessorUses)
        );
        ASSERT_TRUE(predecessor.valid());
        Graphics::GpuTaskDesc recoveryDesc;
        recoveryDesc
            .setIdentity(Name("tests/task_graph/frontier_inferred_recovery"))
            .setMarkerLabel("Frontier Inferred Recovery")
            .setScheduling(frontierScheduling)
            .setResourceUses(recoveryUses, LengthOf(recoveryUses))
        ;
        const Graphics::GpuTaskId recovery = graph.addTask(recoveryDesc);
        ASSERT_TRUE(recovery.valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(graph, analysis));
        EXPECT_EQ(
            analysis.diagnostic().status,
            Graphics::GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask
        );
        EXPECT_EQ(analysis.diagnostic().task, recovery);
        EXPECT_EQ(analysis.diagnostic().relatedTask, predecessor);
        EXPECT_EQ(analysis.diagnostic().resource, recoveryDomain);
    }
}

TEST(GpuTaskGraph, RejectsAcceptedQueueFrontierTasksWithConcreteResources){
    struct ResourceCase{
        Graphics::GpuGraphResourceType::Enum type;
        Name identity;
        AStringView markerLabel;
    };

    TestArena testArena;
    const ResourceCase resourceCases[] = {
        ResourceCase{
            .type = Graphics::GpuGraphResourceType::Texture,
            .identity = Name("tests/task_graph/frontier_texture"),
            .markerLabel = "Frontier Texture",
        },
        ResourceCase{
            .type = Graphics::GpuGraphResourceType::Buffer,
            .identity = Name("tests/task_graph/frontier_buffer"),
            .markerLabel = "Frontier Buffer",
        },
        ResourceCase{
            .type = Graphics::GpuGraphResourceType::AccelStruct,
            .identity = Name("tests/task_graph/frontier_accel_struct"),
            .markerLabel = "Frontier Accel Struct",
        },
    };
    Graphics::GpuTaskSchedulingHint frontierScheduling;
    frontierScheduling.forceSubmissionBoundary = true;
    frontierScheduling.allowPacketMerge = false;
    frontierScheduling.joinsAcceptedQueueFrontier = true;

    for(const ResourceCase& resourceCase : resourceCases){
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = graph.importResource(
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(resourceCase.identity)
                .setMarkerLabel(resourceCase.markerLabel)
                .setType(resourceCase.type)
                .setInitialState(Graphics::ResourceStates::Common)
        );
        ASSERT_TRUE(resource.valid());
        const Graphics::GpuTaskResourceUse recoveryUses[] = {
            Graphics::GpuTaskResourceUse{
                .resource = resource,
                .range = {},
                .requiredState = Graphics::ResourceStates::Common,
                .access = Graphics::GpuTaskResourceAccess::Read,
            },
        };
        Graphics::GpuTaskDesc recoveryDesc;
        recoveryDesc
            .setIdentity(Name("tests/task_graph/frontier_concrete_recovery"))
            .setMarkerLabel("Frontier Concrete Recovery")
            .setScheduling(frontierScheduling)
            .setResourceUses(recoveryUses, LengthOf(recoveryUses))
        ;
        const Graphics::GpuTaskId recovery = graph.addTask(recoveryDesc);
        ASSERT_TRUE(recovery.valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(graph, analysis));
        EXPECT_EQ(
            analysis.diagnostic().status,
            Graphics::GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask
        );
        EXPECT_EQ(analysis.diagnostic().task, recovery);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
    }
}

TEST(GpuTaskGraph, CompilesOnlyIndependentAcceptedQueueFrontierTasks){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId recoveryDomain = AddHazardDomain(
        graph,
        Name("tests/task_graph/independent_frontier_domain"),
        "Independent Frontier Domain"
    );
    ASSERT_TRUE(recoveryDomain.valid());
    Graphics::GpuTaskSchedulingHint frontierScheduling;
    frontierScheduling.forceSubmissionBoundary = true;
    frontierScheduling.allowPacketMerge = false;
    frontierScheduling.joinsAcceptedQueueFrontier = true;

    Graphics::GpuTaskDesc noUseRecoveryDesc;
    noUseRecoveryDesc
        .setIdentity(Name("tests/task_graph/independent_frontier_no_use"))
        .setMarkerLabel("Independent Frontier No Use")
        .setScheduling(frontierScheduling)
    ;
    const Graphics::GpuTaskId noUseRecovery = graph.addTask(noUseRecoveryDesc);
    ASSERT_TRUE(noUseRecovery.valid());

    const Graphics::GpuTaskResourceUse recoveryUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = recoveryDomain,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc domainRecoveryDesc;
    domainRecoveryDesc
        .setIdentity(Name("tests/task_graph/independent_frontier_domain_use"))
        .setMarkerLabel("Independent Frontier Domain Use")
        .setScheduling(frontierScheduling)
        .setResourceUses(recoveryUses, LengthOf(recoveryUses))
    ;
    const Graphics::GpuTaskId domainRecovery = graph.addTask(domainRecoveryDesc);
    ASSERT_TRUE(domainRecovery.valid());

    const Graphics::GpuTaskId consumerDependencies[] = { domainRecovery };
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/independent_frontier_consumer"))
        .setMarkerLabel("Independent Frontier Consumer")
        .setDependencies(consumerDependencies, LengthOf(consumerDependencies))
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(consumer.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_TRUE(compiledPlan.validFor(declarations));
    ASSERT_NE(FindEdge(analysis, domainRecovery, consumer), nullptr);

    const Graphics::GpuTaskId recoveryTasks[] = { noUseRecovery, domainRecovery };
    for(const Graphics::GpuTaskId recovery : recoveryTasks){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(recovery).plan;
        ASSERT_NE(compiledTask, nullptr);
        ASSERT_TRUE(compiledTask->packet.valid());
        const Graphics::GpuSubmissionPacket& packet = *compiledPlan.packet(compiledTask->packet).plan;
        EXPECT_EQ(packet.taskCount, 1u);
        EXPECT_EQ(packet.dependencyCount, 0u);
        EXPECT_EQ(packet.externalDependencyCount, 0u);
        EXPECT_TRUE(packet.joinsAcceptedQueueFrontier);
        EXPECT_FALSE(packet.isRecoverySubmission);
        EXPECT_EQ(compiledTask->prologueStateSeedCount, 0u);
        EXPECT_EQ(compiledTask->prologueBarrierCount, 0u);
        EXPECT_EQ(compiledTask->epilogueBarrierCount, 0u);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

