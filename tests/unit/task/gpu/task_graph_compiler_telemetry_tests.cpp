// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_lifecycle_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_compiler_telemetry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, ExportsInferredEvidenceAndQueueAssignments){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddHazardDomain(
        graph,
        Name("tests/task_graph/telemetry_resource"),
        "Telemetry Resource"
    );
    ASSERT_TRUE(resource.valid());
    const Graphics::GpuTaskResourceUse writerUse{
        .resource = resource,
        .range = {},
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskResourceUse readerUse{
        .resource = resource,
        .range = {},
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    const Graphics::GpuTaskId writer = AddTask(
        graph,
        Name("tests/task_graph/telemetry_writer"),
        "Telemetry Writer",
        nullptr,
        0u,
        &writerUse,
        1u
    );
    const Graphics::GpuTaskId reader = AddTask(
        graph,
        Name("tests/task_graph/telemetry_reader"),
        "Telemetry Reader",
        &writer,
        1u,
        &readerUse,
        1u
    );
    ASSERT_TRUE(writer.valid());
    ASSERT_TRUE(reader.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingEdges(testArena.arena);
    Telemetry::FrameGraphBuilder builder(nodes, edges, pendingEdges);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments));
    const Graphics::GpuTaskGraphTelemetryOptions telemetryOptions{
        .queueAssignments = &assignments,
        .compiledPlan = nullptr,
        .queueAssignmentTelemetry = nullptr,
    };
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

    ASSERT_TRUE(declarations.appendFrameGraphTelemetry(builder, analysis, scratchArena, telemetryOptions));

    const u8 expectedFlags =
        Graphics::GpuTaskGraphTelemetryEdgeFlag::ExplicitDependency
        | Graphics::GpuTaskGraphTelemetryEdgeFlag::InferredDependency
    ;
    bool foundDependency = false;
    for(const Telemetry::FrameGraphEdgeDesc& edge : edges){
        if(edge.kind != Telemetry::FrameGraphEdgeKind::DependsOn)
            continue;
        foundDependency = true;
        EXPECT_EQ(edge.flags, expectedFlags);
    }
    EXPECT_TRUE(foundDependency);

    ASSERT_EQ(nodes.size(), 3u);
    EXPECT_EQ(
        nodes[1u].flags,
        Graphics::GpuTaskGraphTelemetryNodeFlag::AssignedGraphicsQueue
    );
    EXPECT_EQ(
        nodes[2u].flags,
        Graphics::GpuTaskGraphTelemetryNodeFlag::AssignedGraphicsQueue
    );
}

TEST(GpuTaskGraph, ExportsDetailedQueueAssignmentTelemetry){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        true,
        true,
    };
    const Graphics::GpuQueueRequest transferRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Transfer,
        true,
        true,
    };
    const Graphics::GpuQueueRequest computeFallbackRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Transfer,
        true,
        true,
    };
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint sameClassScheduling;
    sameClassScheduling.allowSameClassQueueRouting = true;

    const Graphics::GpuTaskId compute = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/telemetry_dedicated_compute"),
        "Telemetry Dedicated Compute",
        computeRequest
    );
    const Graphics::GpuTaskId transfer = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/telemetry_dedicated_transfer"),
        "Telemetry Dedicated Transfer",
        transferRequest
    );
    const Graphics::GpuTaskId computeFallback = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/telemetry_compute_fallback"),
        "Telemetry Compute Fallback",
        computeFallbackRequest
    );
    const Graphics::GpuTaskId sameClassPrimary = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/telemetry_same_class_primary"),
        "Telemetry Same Class Primary",
        graphicsRequest,
        sameClassScheduling
    );
    const Graphics::GpuTaskId sameClassRouted = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/telemetry_same_class_routed"),
        "Telemetry Same Class Routed",
        graphicsRequest,
        sameClassScheduling
    );
    Graphics::GpuTaskSchedulingHint tinyScheduling;
    tinyScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    const Graphics::GpuTaskId compilerOverride = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/telemetry_compiler_override"),
        "Telemetry Compiler Override",
        transferRequest,
        tinyScheduling
    );
    ASSERT_TRUE(compute.valid());
    ASSERT_TRUE(transfer.valid());
    ASSERT_TRUE(computeFallback.valid());
    ASSERT_TRUE(sameClassPrimary.valid());
    ASSERT_TRUE(sameClassRouted.valid());
    ASSERT_TRUE(compilerOverride.valid());

    Graphics::GpuPhysicalQueueInfo secondaryGraphicsQueue = GraphicsQueue(3u);
    secondaryGraphicsQueue.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        secondaryGraphicsQueue,
        DedicatedComputeQueue(),
        DedicatedTransferQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments));
    const Graphics::GpuTaskQueueAssignment* const computeAssignment = assignments.find(compute);
    const Graphics::GpuTaskQueueAssignment* const transferAssignment = assignments.find(transfer);
    const Graphics::GpuTaskQueueAssignment* const fallbackAssignment = assignments.find(computeFallback);
    const Graphics::GpuTaskQueueAssignment* const primaryAssignment = assignments.find(sameClassPrimary);
    const Graphics::GpuTaskQueueAssignment* const routedAssignment = assignments.find(sameClassRouted);
    const Graphics::GpuTaskQueueAssignment* const overrideAssignment = assignments.find(compilerOverride);
    ASSERT_NE(computeAssignment, nullptr);
    ASSERT_NE(transferAssignment, nullptr);
    ASSERT_NE(fallbackAssignment, nullptr);
    ASSERT_NE(primaryAssignment, nullptr);
    ASSERT_NE(routedAssignment, nullptr);
    ASSERT_NE(overrideAssignment, nullptr);
    EXPECT_EQ(fallbackAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::Fallback);
    EXPECT_EQ(primaryAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_TRUE(primaryAssignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::SameClassLoadBalance);
    EXPECT_EQ(routedAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_FALSE(routedAssignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::SameClassLoadBalance);
    EXPECT_EQ(overrideAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::CompilerOverride);
    EXPECT_EQ(overrideAssignment->initialQueue, overrideAssignment->queue);

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingEdges(testArena.arena);
    Telemetry::FrameGraphBuilder builder(nodes, edges, pendingEdges);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    const Graphics::GpuTaskGraphTelemetryOptions telemetryOptions{
        .queueAssignments = &assignments,
        .compiledPlan = nullptr,
        .queueAssignmentTelemetry = nullptr,
    };
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

    ASSERT_TRUE(declarations.appendFrameGraphTelemetry(builder, analysis, scratchArena, telemetryOptions));

    ASSERT_EQ(nodes.size(), 6u);
    EXPECT_EQ(
        nodes[0u].flags,
        Graphics::GpuTaskGraphTelemetryNodeFlag::AssignedComputeQueue
        | Graphics::GpuTaskGraphTelemetryNodeFlag::AssignedDedicatedQueue
    );
    EXPECT_EQ(
        nodes[1u].flags,
        Graphics::GpuTaskGraphTelemetryNodeFlag::AssignedTransferQueue
        | Graphics::GpuTaskGraphTelemetryNodeFlag::AssignedDedicatedQueue
    );
    EXPECT_EQ(
        nodes[2u].flags,
        Graphics::GpuTaskGraphTelemetryNodeFlag::AssignedGraphicsQueue
        | Graphics::GpuTaskGraphTelemetryNodeFlag::QueueAssignmentFallback
    );
    EXPECT_EQ(
        nodes[3u].flags,
        Graphics::GpuTaskGraphTelemetryNodeFlag::AssignedGraphicsQueue
        | Graphics::GpuTaskGraphTelemetryNodeFlag::QueueAssignmentSameClassRouting
    );
    EXPECT_EQ(nodes[4u].flags, Graphics::GpuTaskGraphTelemetryNodeFlag::AssignedGraphicsQueue);
    EXPECT_EQ(
        nodes[5u].flags,
        Graphics::GpuTaskGraphTelemetryNodeFlag::AssignedGraphicsQueue
        | Graphics::GpuTaskGraphTelemetryNodeFlag::QueueAssignmentCompilerOverride
    );
    ExpectPlannedQueueAssignmentTelemetry(
        *computeAssignment,
        nodes[compute.index].queueAssignment,
        Telemetry::FrameGraphQueueClass::Compute,
        Telemetry::FrameGraphQueueAssignmentReason::DedicatedCompute
    );
    ExpectPlannedQueueAssignmentTelemetry(
        *transferAssignment,
        nodes[transfer.index].queueAssignment,
        Telemetry::FrameGraphQueueClass::Transfer,
        Telemetry::FrameGraphQueueAssignmentReason::DedicatedTransfer
    );
    ExpectPlannedQueueAssignmentTelemetry(
        *fallbackAssignment,
        nodes[computeFallback.index].queueAssignment,
        Telemetry::FrameGraphQueueClass::Graphics,
        Telemetry::FrameGraphQueueAssignmentReason::Fallback
    );
    ExpectPlannedQueueAssignmentTelemetry(
        *primaryAssignment,
        nodes[sameClassPrimary.index].queueAssignment,
        Telemetry::FrameGraphQueueClass::Graphics,
        Telemetry::FrameGraphQueueAssignmentReason::RequiredGraphics
    );
    ExpectPlannedQueueAssignmentTelemetry(
        *routedAssignment,
        nodes[sameClassRouted.index].queueAssignment,
        Telemetry::FrameGraphQueueClass::Graphics,
        Telemetry::FrameGraphQueueAssignmentReason::RequiredGraphics
    );
    ExpectPlannedQueueAssignmentTelemetry(
        *overrideAssignment,
        nodes[compilerOverride.index].queueAssignment,
        Telemetry::FrameGraphQueueClass::Graphics,
        Telemetry::FrameGraphQueueAssignmentReason::CompilerOverride
    );
}

TEST(GpuTaskGraph, ExportsCompiledPacketMembershipAndPacketizationDecisions){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuTaskSchedulingHint prefixScheduling;
    prefixScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/telemetry_compiled_prefix"))
        .setMarkerLabel("Telemetry Compiled Prefix")
        .setScheduling(prefixScheduling)
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    Graphics::GpuTaskSchedulingHint suffixScheduling;
    suffixScheduling.allowPacketMerge = true;
    suffixScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc suffixDesc;
    suffixDesc
        .setIdentity(Name("tests/task_graph/telemetry_compiled_suffix"))
        .setMarkerLabel("Telemetry Compiled Suffix")
        .setScheduling(suffixScheduling)
        .setDependencies(&prefix, 1u)
    ;
    const Graphics::GpuTaskId suffix = graph.addTask(suffixDesc);
    ASSERT_TRUE(suffix.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingEdges(testArena.arena);
    Telemetry::FrameGraphBuilder builder(nodes, edges, pendingEdges);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    u64 planGeneration = 0u;
    {
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);
        const Graphics::GpuTaskGraphTelemetryOptions telemetryOptions{
            .queueAssignments = nullptr,
            .compiledPlan = &reads.compiled,
            .queueAssignmentTelemetry = nullptr,
        };

        ASSERT_TRUE(reads.valid());
        ASSERT_EQ(reads.compiled.packetCount(), 1u);
        ASSERT_TRUE(reads.declarations.appendFrameGraphTelemetry(builder, analysis, scratchArena, telemetryOptions));
        planGeneration = reads.compiled.planGeneration();
    }

    ASSERT_EQ(nodes.size(), 2u);
    EXPECT_FALSE(nodes[prefix.index].queueAssignment.present);
    ASSERT_TRUE(nodes[prefix.index].compiledTask.present);
    ASSERT_TRUE(nodes[suffix.index].compiledTask.present);
    EXPECT_EQ(nodes[prefix.index].compiledTask.planGeneration, planGeneration);
    EXPECT_EQ(nodes[suffix.index].compiledTask.planGeneration, planGeneration);
    EXPECT_EQ(nodes[prefix.index].compiledTask.packetIndex, 0u);
    EXPECT_EQ(nodes[suffix.index].compiledTask.packetIndex, 0u);
    EXPECT_EQ(
        nodes[prefix.index].compiledTask.packetizationDecision,
        Telemetry::FrameGraphTaskPacketizationDecision::FirstTask
    );
    EXPECT_EQ(
        nodes[suffix.index].compiledTask.packetizationDecision,
        Telemetry::FrameGraphTaskPacketizationDecision::MergedExplicit
    );

    Graphics::GpuTaskGraphQueueAssignments replacementAssignments(testArena.arena);
    Graphics::GpuCompiledGraph replacementCompiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, replacementAssignments, replacementCompiledGraph));
    {
        const Tests::GpuTaskGraphReadViews reads(graph, replacementCompiledGraph);
        const Graphics::GpuTaskGraphTelemetryOptions mismatchedOptions{
            .queueAssignments = &assignments,
            .compiledPlan = &reads.compiled,
            .queueAssignmentTelemetry = nullptr,
        };

        ASSERT_TRUE(reads.valid());
        EXPECT_FALSE(reads.declarations.appendFrameGraphTelemetry(builder, analysis, scratchArena, mismatchedOptions));
    }
}

TEST(GpuTaskGraph, PublishesDeclarationStructureStatisticsOnlyForAcceptedPlans){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId directResource = AddHazardDomain(
        graph,
        Name("tests/task_graph/declaration_direct_resource"),
        "Declaration Direct Resource"
    );
    const Graphics::GpuGraphResourceId setFirstResource = AddHazardDomain(
        graph,
        Name("tests/task_graph/declaration_set_first_resource"),
        "Declaration Set First Resource"
    );
    const Graphics::GpuGraphResourceId setSecondResource = AddHazardDomain(
        graph,
        Name("tests/task_graph/declaration_set_second_resource"),
        "Declaration Set Second Resource"
    );
    ASSERT_TRUE(directResource.valid());
    ASSERT_TRUE(setFirstResource.valid());
    ASSERT_TRUE(setSecondResource.valid());

    const Graphics::GpuGraphResourceId resourceSetMembers[] = { setFirstResource, setSecondResource };
    const Graphics::GpuGraphResourceSetId resourceSet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/declaration_resource_set"))
            .setMarkerLabel("Declaration Resource Set")
            .setMembers(resourceSetMembers, LengthOf(resourceSetMembers))
    );
    ASSERT_TRUE(resourceSet.valid());

    const Graphics::GpuTaskResourceUse directUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = directResource,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskResourceSetUse resourceSetUses[] = {
        Graphics::GpuTaskResourceSetUse{
            .resourceSet = resourceSet,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc typedTaskDesc;
    typedTaskDesc
        .setIdentity(Name("tests/task_graph/declaration_typed_task"))
        .setMarkerLabel("Declaration Typed Task")
        .setResourceUses(directUses, LengthOf(directUses))
        .setResourceSetUses(resourceSetUses, LengthOf(resourceSetUses))
    ;
    ASSERT_TRUE(graph.addTask<PacketLifecycleTask>(typedTaskDesc, PacketLifecycleTask::Payload{}).valid());

    Graphics::GpuTaskDesc recordTaskDesc;
    recordTaskDesc
        .setIdentity(Name("tests/task_graph/declaration_record_task"))
        .setMarkerLabel("Declaration Record Task")
    ;
    ASSERT_TRUE(graph.addTask<NativeRecordProbeTask>(recordTaskDesc, NativeRecordProbeTask::Payload{}).valid());

    ASSERT_TRUE(AddTask(
        graph,
        Name("tests/task_graph/declaration_metadata_task"),
        "Declaration Metadata Task"
    ).valid());

    const u8 firstUploadBytes[] = { 0x17u, 0x3au, 0x5cu };
    const u8 secondUploadBytes[] = { 0x8eu, 0xb1u, 0xc4u, 0xd9u, 0xefu };
    ASSERT_TRUE(graph.copyUploadData(firstUploadBytes, sizeof(firstUploadBytes), alignof(u8)).valid());
    ASSERT_TRUE(graph.copyUploadData(secondUploadBytes, sizeof(secondUploadBytes), alignof(u8)).valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        const Graphics::GpuTaskGraphCompileStatistics statistics = compiledPlan.compileStatistics();

        ASSERT_TRUE(statistics.valid());
        EXPECT_EQ(statistics.resourceSetCount, 1u);
        EXPECT_EQ(statistics.resourceSetMemberCount, LengthOf(resourceSetMembers));
        EXPECT_EQ(statistics.directResourceUseCount, LengthOf(directUses));
        EXPECT_EQ(statistics.declaredResourceSetUseCount, LengthOf(resourceSetUses));
        EXPECT_EQ(statistics.expandedResourceSetMemberUseCount, LengthOf(resourceSetMembers));
        EXPECT_EQ(statistics.resourceUseCount, LengthOf(directUses) + LengthOf(resourceSetMembers));
        EXPECT_EQ(statistics.payloadObjectCount, 2u);
        EXPECT_EQ(
            statistics.payloadObjectBytes,
            sizeof(PacketLifecycleTask::Payload) + sizeof(NativeRecordProbeTask::Payload)
        );
        EXPECT_EQ(statistics.uploadBlobCount, 2u);
        EXPECT_EQ(statistics.uploadBlobBytes, sizeof(firstUploadBytes) + sizeof(secondUploadBytes));
    }

    compiledGraph.reset();
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        const Graphics::GpuTaskGraphCompileStatistics resetStatistics = compiledPlan.compileStatistics();

        EXPECT_FALSE(resetStatistics.valid());
        EXPECT_EQ(resetStatistics.resourceSetCount, 0u);
        EXPECT_EQ(resetStatistics.resourceSetMemberCount, 0u);
        EXPECT_EQ(resetStatistics.directResourceUseCount, 0u);
        EXPECT_EQ(resetStatistics.declaredResourceSetUseCount, 0u);
        EXPECT_EQ(resetStatistics.expandedResourceSetMemberUseCount, 0u);
        EXPECT_EQ(resetStatistics.resourceUseCount, 0u);
        EXPECT_EQ(resetStatistics.payloadObjectCount, 0u);
        EXPECT_EQ(resetStatistics.payloadObjectBytes, 0u);
        EXPECT_EQ(resetStatistics.uploadBlobCount, 0u);
        EXPECT_EQ(resetStatistics.uploadBlobBytes, 0u);
        EXPECT_EQ(resetStatistics.validationSeconds, 0.0);
        EXPECT_EQ(resetStatistics.dependencyAnalysisSeconds, 0.0);
        EXPECT_EQ(resetStatistics.hazardAnalysisSeconds, 0.0);
        EXPECT_EQ(resetStatistics.topologicalOrderSeconds, 0.0);
        EXPECT_EQ(resetStatistics.packetizationSeconds, 0.0);
        EXPECT_EQ(resetStatistics.resourceStatePlanningSeconds, 0.0);
        EXPECT_EQ(resetStatistics.packetDependencyPlanningSeconds, 0.0);
    }

    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuTaskGraphQueueTopology invalidTopology{};
    EXPECT_FALSE(Compile(graph, analysis, invalidTopology, assignments, compiledGraph));
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        const Graphics::GpuTaskGraphCompileStatistics failedStatistics = compiledPlan.compileStatistics();

        EXPECT_FALSE(compiledPlan.valid());
        EXPECT_FALSE(failedStatistics.valid());
        EXPECT_EQ(failedStatistics.resourceSetCount, 0u);
        EXPECT_EQ(failedStatistics.resourceSetMemberCount, 0u);
        EXPECT_EQ(failedStatistics.directResourceUseCount, 0u);
        EXPECT_EQ(failedStatistics.declaredResourceSetUseCount, 0u);
        EXPECT_EQ(failedStatistics.expandedResourceSetMemberUseCount, 0u);
        EXPECT_EQ(failedStatistics.resourceUseCount, 0u);
        EXPECT_EQ(failedStatistics.payloadObjectCount, 0u);
        EXPECT_EQ(failedStatistics.payloadObjectBytes, 0u);
        EXPECT_EQ(failedStatistics.uploadBlobCount, 0u);
        EXPECT_EQ(failedStatistics.uploadBlobBytes, 0u);
        EXPECT_EQ(failedStatistics.validationSeconds, 0.0);
        EXPECT_EQ(failedStatistics.dependencyAnalysisSeconds, 0.0);
        EXPECT_EQ(failedStatistics.hazardAnalysisSeconds, 0.0);
        EXPECT_EQ(failedStatistics.topologicalOrderSeconds, 0.0);
        EXPECT_EQ(failedStatistics.packetizationSeconds, 0.0);
        EXPECT_EQ(failedStatistics.resourceStatePlanningSeconds, 0.0);
        EXPECT_EQ(failedStatistics.packetDependencyPlanningSeconds, 0.0);
    }
}

TEST(GpuTaskGraph, PublishesFiniteDeclarationTimingOnlyForAcceptedPlans){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    ASSERT_TRUE(AddTask(
        graph,
        Name("tests/task_graph/declaration_seconds"),
        "Declaration Seconds"
    ).valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    Graphics::GpuPhysicalQueueInfo idleQueue = GraphicsQueue(1u);
    idleQueue.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        queue,
        idleQueue,
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    Graphics::GpuTaskGraphCompileOptions options;

    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));
    u64 firstPlanGeneration = 0u;
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        const Graphics::GpuTaskGraphCompileStatistics statistics = compiledPlan.compileStatistics();
        const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics firstQueueCompileStatistics =
            compiledPlan.physicalQueueCompileStatistics(queue.id)
        ;
        const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics idleQueueCompileStatistics =
            compiledPlan.physicalQueueCompileStatistics(idleQueue.id)
        ;

        ASSERT_TRUE(statistics.valid());
        EXPECT_EQ(statistics.declarationSeconds, 0.0);
        ASSERT_TRUE(firstQueueCompileStatistics.valid());
        ASSERT_TRUE(idleQueueCompileStatistics.valid());
        EXPECT_EQ(firstQueueCompileStatistics.graphGeneration, compiledPlan.generation());
        EXPECT_EQ(firstQueueCompileStatistics.planGeneration, compiledPlan.planGeneration());
        EXPECT_EQ(firstQueueCompileStatistics.deviceGeneration, compiledPlan.deviceGeneration());
        EXPECT_EQ(firstQueueCompileStatistics.queue, queue.id);
        EXPECT_EQ(firstQueueCompileStatistics.queueClass, Graphics::CommandQueue::Graphics);
        EXPECT_EQ(firstQueueCompileStatistics.taskCount, 1u);
        EXPECT_EQ(firstQueueCompileStatistics.packetCount, 1u);
        EXPECT_EQ(idleQueueCompileStatistics.queue, idleQueue.id);
        EXPECT_EQ(idleQueueCompileStatistics.taskCount, 0u);
        EXPECT_EQ(idleQueueCompileStatistics.packetCount, 0u);
        EXPECT_EQ(idleQueueCompileStatistics.mergedTaskCount, 0u);
        EXPECT_EQ(idleQueueCompileStatistics.prologueBarrierCount, 0u);
        EXPECT_EQ(idleQueueCompileStatistics.epilogueBarrierCount, 0u);
        EXPECT_EQ(idleQueueCompileStatistics.ownershipReleaseBarrierCount, 0u);
        EXPECT_EQ(idleQueueCompileStatistics.ownershipAcquireBarrierCount, 0u);
        const Graphics::GpuPhysicalQueueId staleQueue{
            queue.id.index,
            static_cast<u16>(queue.id.deviceGeneration + 1u),
        };
        EXPECT_FALSE(compiledPlan.physicalQueueCompileStatistics(staleQueue).valid());
        const Graphics::GpuPhysicalQueueId nonPlanQueue{ 2u, compiledPlan.deviceGeneration() };
        ASSERT_TRUE(nonPlanQueue.valid());
        EXPECT_FALSE(compiledPlan.physicalQueueCompileStatistics(nonPlanQueue).valid());
        firstPlanGeneration = firstQueueCompileStatistics.planGeneration;
    }

    constexpr f64 s_DeclarationSeconds = 0.125;
    options.declarationSeconds = s_DeclarationSeconds;
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        const Graphics::GpuTaskGraphCompileStatistics acceptedStatistics = compiledPlan.compileStatistics();

        ASSERT_TRUE(acceptedStatistics.valid());
        EXPECT_EQ(acceptedStatistics.declarationSeconds, s_DeclarationSeconds);
    }

    compiledGraph.reset();
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        const Graphics::GpuTaskGraphCompileStatistics resetStatistics = compiledPlan.compileStatistics();

        EXPECT_FALSE(resetStatistics.valid());
        EXPECT_EQ(resetStatistics.declarationSeconds, 0.0);
        EXPECT_FALSE(compiledPlan.physicalQueueCompileStatistics(queue.id).valid());
    }

    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        const Graphics::GpuTaskGraphPhysicalQueueCompileStatistics recompiledQueueCompileStatistics =
            compiledPlan.physicalQueueCompileStatistics(queue.id)
        ;

        ASSERT_TRUE(recompiledQueueCompileStatistics.valid());
        EXPECT_NE(recompiledQueueCompileStatistics.planGeneration, firstPlanGeneration);
    }
    const Graphics::GpuTaskGraphQueueTopology invalidTopology{};
    EXPECT_FALSE(Compile(graph, analysis, invalidTopology, assignments, compiledGraph, options));
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        const Graphics::GpuTaskGraphCompileStatistics failedStatistics = compiledPlan.compileStatistics();

        EXPECT_FALSE(failedStatistics.valid());
        EXPECT_EQ(failedStatistics.declarationSeconds, 0.0);
        EXPECT_FALSE(compiledPlan.physicalQueueCompileStatistics(queue.id).valid());
    }

    const f64 invalidDeclarationSeconds[] = {
        -0.125,
        Limit<f64>::s_Infinity,
        Limit<f64>::s_QuietNaN,
    };
    for(const f64 declarationSeconds : invalidDeclarationSeconds){
        options.declarationSeconds = declarationSeconds;
        ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        const Graphics::GpuTaskGraphCompileStatistics invalidStatistics = compiledPlan.compileStatistics();

        EXPECT_TRUE(invalidStatistics.valid());
        EXPECT_EQ(invalidStatistics.declarationSeconds, 0.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

