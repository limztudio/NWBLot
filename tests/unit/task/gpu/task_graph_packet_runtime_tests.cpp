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
    Core::GpuTaskGraphRecordingStatistics statistics;
    EXPECT_EQ(statistics.readyFrontierWorkerUtilization(), 0.0);

    statistics.readyFrontierWorkerCapacitySeconds = 1.0;

    statistics.readyFrontierWorkerBusySeconds = 2.0;
    EXPECT_EQ(statistics.readyFrontierWorkerUtilization(), 1.0);

    statistics.readyFrontierWorkerBusySeconds = -1.0;
    EXPECT_EQ(statistics.readyFrontierWorkerUtilization(), 0.0);
}

TEST(GpuTaskGraph, RecreatesPacketRecordingStateAfterRecompile){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);

    const auto firstTaskResult = AddTask(Name("tests/task_graph/recreate_recording_first"), "Recreate Recording First", {}, {}, {}, {}, {}, graph);
    ASSERT_TRUE(firstTaskResult);
    const Core::GpuTaskId firstTask = *firstTaskResult;
    ASSERT_TRUE(firstTask.valid());
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    Core::GpuSubmissionPacketId firstPacket;
    Core::GpuPhysicalQueueId firstQueue;
    u64 firstCompiledGeneration = 0u;
    {
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        firstPacket = compiledPlan.packetForTask(firstTask);
        ASSERT_TRUE(firstPacket.valid());
        firstQueue = compiledPlan.packet(firstPacket).plan->queue;
        ASSERT_TRUE(firstQueue.valid());
        firstCompiledGeneration = compiledPlan.generation();
    }

    Core::GpuRecordedGraph recordedGraph(testArena.arena);
    Core::GpuGraphSubmissionTransaction transaction(testArena.arena);
    recordedGraph.reset(compiledGraph);
    transaction.reset(compiledGraph);
    {
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        ASSERT_TRUE(recordedGraph.validFor(compiledGraph, compiledPlan));
        ASSERT_TRUE(transaction.validFor(compiledPlan));
        const Core::GpuTaskGraphPhysicalQueueRecordingStatistics firstQueueStatistics = recordedGraph.physicalQueueRecordingStatistics(compiledGraph, compiledPlan, firstQueue);
        ASSERT_TRUE(firstQueueStatistics.valid());
        const Core::GpuPhysicalQueueId staleFirstQueue{ .index = firstQueue.index, .deviceGeneration = static_cast<u16>(firstQueue.deviceGeneration == Limit<u16>::s_Max ? 1u : firstQueue.deviceGeneration + 1u), };
        EXPECT_FALSE(recordedGraph.physicalQueueRecordingStatistics(compiledGraph, compiledPlan, staleFirstQueue).valid());
        // A current-generation ID is still invalid when the compiled plan has no matching physical topology entry.
        const Core::GpuPhysicalQueueId nonPlanQueue{ .index = 3u, .deviceGeneration = compiledPlan.deviceGeneration() };
        EXPECT_TRUE(nonPlanQueue.valid());
        EXPECT_FALSE(recordedGraph.physicalQueueRecordingStatistics(compiledGraph, compiledPlan, nonPlanQueue).valid());
    }

    graph.reset();
    const auto secondTaskResult = AddTask(Name("tests/task_graph/recreate_recording_second"), "Recreate Recording Second", {}, {}, {}, {}, {}, graph);
    ASSERT_TRUE(secondTaskResult);
    const Core::GpuTaskId secondTask = *secondTaskResult;
    ASSERT_TRUE(secondTask.valid());
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    Core::GpuSubmissionPacketId secondPacket;
    Core::GpuPhysicalQueueId secondQueue;
    {
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        ASSERT_NE(compiledPlan.generation(), firstCompiledGeneration);
        secondPacket = compiledPlan.packetForTask(secondTask);
        ASSERT_TRUE(secondPacket.valid());
        secondQueue = compiledPlan.packet(secondPacket).plan->queue;
        ASSERT_TRUE(secondQueue.valid());
        EXPECT_FALSE(recordedGraph.validFor(compiledGraph, compiledPlan));
        EXPECT_FALSE(transaction.validFor(compiledPlan));
        EXPECT_FALSE(recordedGraph.physicalQueueRecordingStatistics(compiledGraph, compiledPlan, secondQueue).valid());
    }

    // Reset releases old command lists and rebuilds packet scratch for the new graph.
    recordedGraph.reset(compiledGraph);
    transaction.reset(compiledGraph);
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    EXPECT_TRUE(recordedGraph.validFor(compiledGraph, compiledPlan));
    EXPECT_TRUE(transaction.validFor(compiledPlan));
    const Core::GpuTaskGraphPhysicalQueueRecordingStatistics secondQueueStatistics = recordedGraph.physicalQueueRecordingStatistics(compiledGraph, compiledPlan, secondQueue);
    ASSERT_TRUE(secondQueueStatistics.valid());
}

TEST(GpuTaskGraph, InvalidatesPacketRuntimeAndCaptureForSameGraphRecompile){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::GpuTaskCommandRequirements computeCommands{ Core::GpuQueueCapability::Compute };
    const auto taskResult = AddTaskWithCommands(Name("tests/task_graph/same_graph_recompile"), "Same Graph Recompile", computeCommands, {}, {}, {}, graph);
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());

    const Array<Core::GpuPhysicalQueueInfo, 1u> graphicsOnly = { GraphicsQueue() };
    const Core::GpuPhysicalQueueTopology firstTopology{ .queues = graphicsOnly.data(), .queueCount = LengthOf(graphicsOnly), };
    const Array<Core::GpuPhysicalQueueInfo, 2u> graphicsAndCompute = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Core::GpuPhysicalQueueTopology secondTopology{ .queues = graphicsAndCompute.data(), .queueCount = LengthOf(graphicsAndCompute), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, firstTopology));
    u64 graphGeneration = 0u;
    u64 firstPlanGeneration = 0u;
    Core::GpuSubmissionPacketId firstPacket;
    Core::GpuPhysicalQueueId firstQueue;
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

    Core::GpuRecordedGraph recordedGraph(testArena.arena);
    Core::GpuGraphSubmissionTransaction transaction(testArena.arena);
    recordedGraph.reset(compiledGraph);
    transaction.reset(compiledGraph);
    {
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        ASSERT_TRUE(recordedGraph.validFor(compiledGraph, compiledPlan));
        ASSERT_TRUE(transaction.validFor(compiledPlan));
    }

    Core::GpuCommandIrCapture capture(testArena.arena);
    ASSERT_TRUE(capture.captureClearBuffer(task, firstPacket, firstQueue, Core::GpuGraphResourceId{ .generation = graphGeneration, .index = 0u }, 0xdecafbadU));
    ASSERT_EQ(capture.planGeneration(), firstPlanGeneration);

    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, secondTopology));
    const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);

    ASSERT_TRUE(reads.valid());
    ASSERT_EQ(reads.compiled.generation(), graphGeneration);
    ASSERT_EQ(reads.compiled.packetCount(), 1u);
    const Core::GpuSubmissionPacketId secondPacket = reads.compiled.packetForTask(task);
    ASSERT_TRUE(secondPacket.valid());
    EXPECT_NE(reads.compiled.planGeneration(), firstPlanGeneration);
    EXPECT_NE(secondPacket, firstPacket);
    EXPECT_FALSE(reads.compiled.validPacket(firstPacket));
    EXPECT_FALSE(recordedGraph.validFor(compiledGraph, reads.compiled));
    EXPECT_FALSE(transaction.validFor(reads.compiled));

    const Core::GpuCommandIrReplayResult staleCapture = Core::PreflightGpuCommandIrPacket(capture.commandBytes(), reads.declarations, reads.compiled, secondPacket);
    EXPECT_EQ(staleCapture.error, Core::GpuCommandIrReplayError::PlanGenerationMismatch);
    EXPECT_TRUE(staleCapture.streamValidation.valid());
}

TEST(GpuTaskGraph, CompiledTaskLookupRejectsOutOfRangeStaleAndUncompiledHandles){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto firstResult = AddTask(Name("tests/task_graph/compiled_lookup_first"), "Compiled Lookup First", {}, {}, {}, {}, {}, graph);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());

    const Core::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    usize graphTaskCount = 0u;
    u64 graphGeneration = 0u;
    {
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);

        ASSERT_TRUE(reads.valid());
        ASSERT_NE(reads.compiled.findTask(first).plan, nullptr);
        graphTaskCount = reads.declarations.taskCount();
        graphGeneration = reads.declarations.generation();
    }

    const auto expectMissingTask = [&assignments](const Core::GpuCompiledGraph::ReadView& compiledPlan, const Core::GpuTaskId task){
        EXPECT_EQ(assignments.find(task), nullptr);
        EXPECT_EQ(compiledPlan.findTask(task).plan, nullptr);
        EXPECT_FALSE(compiledPlan.packetForTask(task).valid());
        EXPECT_EQ(compiledPlan.packetizationDecisionForTask(task), Core::GpuTaskPacketizationDecision::Unknown);
    };
    const Core::GpuTaskId onePastCompiledTasks{ .generation = graphGeneration, .index = static_cast<u32>(graphTaskCount), };
    const Core::GpuTaskId largeSameGenerationTask{ .generation = graphGeneration, .index = static_cast<u32>(Limit<u32>::s_Max - 1u), };
    ASSERT_TRUE(onePastCompiledTasks.valid());
    ASSERT_TRUE(largeSameGenerationTask.valid());
    {
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        expectMissingTask(compiledPlan, onePastCompiledTasks);
        expectMissingTask(compiledPlan, largeSameGenerationTask);
    }

    Core::GpuTaskGraph foreignGraph(testArena.arena);
    const auto foreignResult = AddTask(Name("tests/task_graph/compiled_lookup_foreign"), "Compiled Lookup Foreign", {}, {}, {}, {}, {}, foreignGraph);
    ASSERT_TRUE(foreignResult);
    const Core::GpuTaskId foreign = *foreignResult;
    ASSERT_TRUE(foreign.valid());
    ASSERT_NE(foreign.generation, graphGeneration);
    {
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        expectMissingTask(compiledPlan, foreign);
    }

    const auto appendedResult = AddTask(Name("tests/task_graph/compiled_lookup_appended"), "Compiled Lookup Appended", {}, {}, {}, {}, {}, graph);
    ASSERT_TRUE(appendedResult);
    const Core::GpuTaskId appended = *appendedResult;
    ASSERT_TRUE(appended.valid());
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        EXPECT_FALSE(compiledPlan.validFor(declarations));
        expectMissingTask(compiledPlan, appended);
    }

    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    {
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);

        ASSERT_TRUE(reads.valid());
        ASSERT_NE(assignments.find(first), nullptr);
        ASSERT_NE(assignments.find(appended), nullptr);
        ASSERT_NE(reads.compiled.findTask(first).plan, nullptr);
        ASSERT_NE(reads.compiled.findTask(appended).plan, nullptr);
    }

    graph.reset();
    const auto replacementResult = AddTask(Name("tests/task_graph/compiled_lookup_replacement"), "Compiled Lookup Replacement", {}, {}, {}, {}, {}, graph);
    ASSERT_TRUE(replacementResult);
    const Core::GpuTaskId replacement = *replacementResult;
    ASSERT_TRUE(replacement.valid());
    ASSERT_NE(replacement.generation, first.generation);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
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

TEST(GpuTaskGraph, RejectsRecoverySubmissionWithoutAcceptedQueueFrontierRole){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuTaskSchedulingHint recoveryScheduling;
    recoveryScheduling.forceSubmissionBoundary = true;
    recoveryScheduling.allowPacketMerge = false;
    recoveryScheduling.isRecoverySubmission = true;
    Core::GpuTaskDesc recoveryDesc;
    recoveryDesc.setIdentity(Name("tests/task_graph/recovery_without_frontier")).setMarkerLabel("Recovery Without Frontier").setScheduling(recoveryScheduling);
    const auto recoveryResult = graph.addTask(recoveryDesc);
    ASSERT_TRUE(recoveryResult);
    const Core::GpuTaskId recovery = *recoveryResult;
    ASSERT_TRUE(recovery.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, operationScratchArena, analysis));
    EXPECT_FALSE(analysis.valid());
    EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask);
    EXPECT_EQ(analysis.diagnostic().task, recovery);
    EXPECT_FALSE(analysis.diagnostic().relatedTask.valid());
    EXPECT_FALSE(analysis.diagnostic().resource.valid());
}

TEST(GpuTaskGraph, RejectsAcceptedQueueFrontierTasksWithPrerequisites){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskSchedulingHint frontierScheduling;
    frontierScheduling.forceSubmissionBoundary = true;
    frontierScheduling.allowPacketMerge = false;
    frontierScheduling.joinsAcceptedQueueFrontier = true;

    {
        Core::GpuTaskGraph graph(testArena.arena);
        const auto predecessorResult = AddTask(Name("tests/task_graph/frontier_explicit_predecessor"), "Frontier Explicit Predecessor", {}, {}, {}, {}, {}, graph);
        ASSERT_TRUE(predecessorResult);
        const Core::GpuTaskId predecessor = *predecessorResult;
        ASSERT_TRUE(predecessor.valid());
        Core::GpuTaskDesc recoveryDesc;
        recoveryDesc.setIdentity(Name("tests/task_graph/frontier_explicit_recovery")).setMarkerLabel("Frontier Explicit Recovery").setScheduling(frontierScheduling).setDependencies(Span(&predecessor, 1u));
        const auto recoveryResult = graph.addTask(recoveryDesc);
        ASSERT_TRUE(recoveryResult);
        const Core::GpuTaskId recovery = *recoveryResult;
        ASSERT_TRUE(recovery.valid());

        Core::GpuTaskGraph validGraph(testArena.arena);
        {
            const auto gpuTaskAdmission = AddTask(Name("tests/task_graph/frontier_stale_assignment_source"), "Frontier Stale Assignment Source", {}, {}, {}, {}, {}, validGraph);
            ASSERT_TRUE(gpuTaskAdmission);
            ASSERT_TRUE(gpuTaskAdmission->valid());
        }
        const Core::GpuPhysicalQueueInfo queue = GraphicsQueue();
        const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Core::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, validGraph, topology));
        ASSERT_TRUE(assignments.valid());
        {
            const Tests::GpuTaskGraphReadViews reads(validGraph, compiledGraph);

            ASSERT_TRUE(reads.valid());
        }

        EXPECT_FALSE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
        EXPECT_FALSE(analysis.valid());
        EXPECT_FALSE(assignments.valid());
        {
            const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

            EXPECT_FALSE(compiledPlan.valid());
        }
        EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask);
        EXPECT_EQ(analysis.diagnostic().task, recovery);
        EXPECT_EQ(analysis.diagnostic().relatedTask, predecessor);
        EXPECT_FALSE(analysis.diagnostic().resource.valid());
    }

    {
        Core::GpuTaskGraph graph(testArena.arena);
        const auto completionResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name("tests/task_graph/frontier_external_completion")).setMarkerLabel("Frontier External Completion"));
        ASSERT_TRUE(completionResult);
        const Core::GpuExternalCompletionId completion = *completionResult;
        ASSERT_TRUE(completion.valid());
        Core::GpuTaskDesc recoveryDesc;
        recoveryDesc.setIdentity(Name("tests/task_graph/frontier_external_recovery")).setMarkerLabel("Frontier External Recovery").setScheduling(frontierScheduling).setExternalDependencies(Span(&completion, 1u));
        const auto recoveryResult = graph.addTask(recoveryDesc);
        ASSERT_TRUE(recoveryResult);
        const Core::GpuTaskId recovery = *recoveryResult;
        ASSERT_TRUE(recovery.valid());

        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(graph, operationScratchArena, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask);
        EXPECT_EQ(analysis.diagnostic().task, recovery);
    }

    {
        Core::GpuTaskGraph graph(testArena.arena);
        Core::CommandListResourceStateHandoff externalStateSource(testArena.arena);
        ASSERT_FALSE(externalStateSource.valid());
        const Array<Core::GpuTaskExternalStateSource, 1u> externalStateSources = { Core::GpuTaskExternalStateSource{ .states = &externalStateSource }, };
        Core::GpuTaskDesc recoveryDesc;
        recoveryDesc
            .setIdentity(Name("tests/task_graph/frontier_external_state_recovery"))
            .setMarkerLabel("Frontier External State Recovery")
            .setScheduling(frontierScheduling)
            .setExternalStateSources(externalStateSources)
        ;
        EXPECT_FALSE(graph.addTask(recoveryDesc).has_value());
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.taskCount(), 0u);
    }

    {
        Core::GpuTaskGraph graph(testArena.arena);
        const auto recoveryDomainResult = AddHazardDomain(graph, Name("tests/task_graph/frontier_inferred_domain"), "Frontier Inferred Domain");
        ASSERT_TRUE(recoveryDomainResult);
        const Core::GpuGraphResourceId recoveryDomain = *recoveryDomainResult;
        ASSERT_TRUE(recoveryDomain.valid());
        const Array<Core::GpuTaskResourceUse, 1u> predecessorUses = { Core::GpuTaskResourceUse{ .resource = recoveryDomain, .range = {}, .requiredState = Core::ResourceStates::Common, .access = Core::GpuTaskResourceAccess::Write, }, };
        const Array<Core::GpuTaskResourceUse, 1u> recoveryUses = { Core::GpuTaskResourceUse{ .resource = recoveryDomain, .range = {}, .requiredState = Core::ResourceStates::Common, .access = Core::GpuTaskResourceAccess::Read, }, };
        const auto predecessorResult = AddTask(Name("tests/task_graph/frontier_inferred_predecessor"), "Frontier Inferred Predecessor", {}, Span(predecessorUses), {}, {}, {}, graph);
        ASSERT_TRUE(predecessorResult);
        const Core::GpuTaskId predecessor = *predecessorResult;
        ASSERT_TRUE(predecessor.valid());
        Core::GpuTaskDesc recoveryDesc;
        recoveryDesc.setIdentity(Name("tests/task_graph/frontier_inferred_recovery")).setMarkerLabel("Frontier Inferred Recovery").setScheduling(frontierScheduling).setResourceUses(recoveryUses);
        const auto recoveryResult = graph.addTask(recoveryDesc);
        ASSERT_TRUE(recoveryResult);
        const Core::GpuTaskId recovery = *recoveryResult;
        ASSERT_TRUE(recovery.valid());

        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(graph, operationScratchArena, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask);
        EXPECT_EQ(analysis.diagnostic().task, recovery);
        EXPECT_EQ(analysis.diagnostic().relatedTask, predecessor);
        EXPECT_EQ(analysis.diagnostic().resource, recoveryDomain);
    }
}

TEST(GpuTaskGraph, RejectsAcceptedQueueFrontierTasksWithConcreteResources){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    struct ResourceCase{
        Core::GpuGraphResourceType::Enum type;
        Name identity;
        AStringView markerLabel;
    };

    TestArena testArena;
    const Array<ResourceCase, 3u> resourceCases = {
        ResourceCase{ .type = Core::GpuGraphResourceType::Texture, .identity = Name("tests/task_graph/frontier_texture"), .markerLabel = "Frontier Texture", },
        ResourceCase{ .type = Core::GpuGraphResourceType::Buffer, .identity = Name("tests/task_graph/frontier_buffer"), .markerLabel = "Frontier Buffer", },
        ResourceCase{ .type = Core::GpuGraphResourceType::AccelStruct, .identity = Name("tests/task_graph/frontier_accel_struct"), .markerLabel = "Frontier Accel Struct", },
    };
    Core::GpuTaskSchedulingHint frontierScheduling;
    frontierScheduling.forceSubmissionBoundary = true;
    frontierScheduling.allowPacketMerge = false;
    frontierScheduling.joinsAcceptedQueueFrontier = true;

    for(const ResourceCase& resourceCase : resourceCases){
        Core::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = graph.importResource(Core::GpuGraphResourceDesc{}.setIdentity(resourceCase.identity).setMarkerLabel(resourceCase.markerLabel).setType(resourceCase.type).setInitialState(Core::ResourceStates::Common));
        ASSERT_TRUE(resourceResult);
        const Core::GpuGraphResourceId resource = *resourceResult;
        ASSERT_TRUE(resource.valid());
        const Array<Core::GpuTaskResourceUse, 1u> recoveryUses = { Core::GpuTaskResourceUse{ .resource = resource, .range = {}, .requiredState = Core::ResourceStates::Common, .access = Core::GpuTaskResourceAccess::Read, }, };
        Core::GpuTaskDesc recoveryDesc;
        recoveryDesc.setIdentity(Name("tests/task_graph/frontier_concrete_recovery")).setMarkerLabel("Frontier Concrete Recovery").setScheduling(frontierScheduling).setResourceUses(recoveryUses);
        const auto recoveryResult = graph.addTask(recoveryDesc);
        ASSERT_TRUE(recoveryResult);
        const Core::GpuTaskId recovery = *recoveryResult;
        ASSERT_TRUE(recovery.valid());

        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(graph, operationScratchArena, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask);
        EXPECT_EQ(analysis.diagnostic().task, recovery);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

