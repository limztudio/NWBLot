// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_opaque_emulation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, MergesSharedOpaqueComputeEmulationDispatchRasterPairsIntoOnePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId generatedVertexBuffer = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shared_generated_vertex_buffer"),
        "Shared Generated Vertex Buffer",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(generatedVertexBuffer.valid());

    const Graphics::GpuQueueRequest graphicsComputeQueue{
        QueueCapabilities(
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueueCapability::Compute
        ),
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest graphicsRasterQueue{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint dispatchScheduling;
    dispatchScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    dispatchScheduling.overlapPreferred = false;
    dispatchScheduling.avoidQueueCrossing = true;
    dispatchScheduling.forceSubmissionBoundary = false;
    dispatchScheduling.allowPacketMerge = true;
    dispatchScheduling.mergeWithPrevious = false;
    const Graphics::GpuTaskResourceUse dispatchUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = generatedVertexBuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse rasterUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = generatedVertexBuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::VertexBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskId dispatchA = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/shared_opaque_compute_dispatch_a"))
            .setMarkerLabel("Shared Opaque Compute Dispatch A")
            .setQueue(graphicsComputeQueue)
            .setScheduling(dispatchScheduling)
            .setResourceUses(dispatchUses, LengthOf(dispatchUses))
    );
    ASSERT_TRUE(dispatchA.valid());

    Graphics::GpuTaskSchedulingHint pairTailScheduling = dispatchScheduling;
    pairTailScheduling.mergeWithPrevious = true;
    // Every callback is an explicit immediate successor, so the complete alias sequence must stay in G-buffer's
    // one accepting Graphics packet even under the conservative FrontierSafe policy.
    pairTailScheduling.allowMergeAcrossConsumerFrontier = true;
    const Graphics::GpuTaskId rasterADependencies[] = { dispatchA };
    const Graphics::GpuTaskId rasterA = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/shared_opaque_compute_raster_a"))
            .setMarkerLabel("Shared Opaque Compute Raster A")
            .setQueue(graphicsRasterQueue)
            .setScheduling(pairTailScheduling)
            .setDependencies(rasterADependencies, LengthOf(rasterADependencies))
            .setResourceUses(rasterUses, LengthOf(rasterUses))
    );
    ASSERT_TRUE(rasterA.valid());

    const Graphics::GpuTaskId dispatchBDependencies[] = { rasterA };
    const Graphics::GpuTaskId dispatchB = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/shared_opaque_compute_dispatch_b"))
            .setMarkerLabel("Shared Opaque Compute Dispatch B")
            .setQueue(graphicsComputeQueue)
            .setScheduling(pairTailScheduling)
            .setDependencies(dispatchBDependencies, LengthOf(dispatchBDependencies))
            .setResourceUses(dispatchUses, LengthOf(dispatchUses))
    );
    ASSERT_TRUE(dispatchB.valid());

    const Graphics::GpuTaskId rasterBDependencies[] = { dispatchB };
    const Graphics::GpuTaskId rasterB = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/shared_opaque_compute_raster_b"))
            .setMarkerLabel("Shared Opaque Compute Raster B")
            .setQueue(graphicsRasterQueue)
            .setScheduling(pairTailScheduling)
            .setDependencies(rasterBDependencies, LengthOf(rasterBDependencies))
            .setResourceUses(rasterUses, LengthOf(rasterUses))
    );
    ASSERT_TRUE(rasterB.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchA, rasterA));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterA, dispatchB));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchB, rasterB));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        dispatchA,
        rasterA,
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        dispatchA,
        dispatchB,
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        rasterA,
        dispatchB,
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::WriteAfterRead
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        dispatchB,
        rasterB,
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_EQ(analysis.topologicalOrder().size(), 4u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], dispatchA);
    EXPECT_EQ(analysis.topologicalOrder()[1u], rasterA);
    EXPECT_EQ(analysis.topologicalOrder()[2u], dispatchB);
    EXPECT_EQ(analysis.topologicalOrder()[3u], rasterB);

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuTaskQueueAssignment* const dispatchAAssignment = assignments.find(dispatchA);
    const Graphics::GpuTaskQueueAssignment* const rasterAAssignment = assignments.find(rasterA);
    const Graphics::GpuTaskQueueAssignment* const dispatchBAssignment = assignments.find(dispatchB);
    const Graphics::GpuTaskQueueAssignment* const rasterBAssignment = assignments.find(rasterB);
    ASSERT_NE(dispatchAAssignment, nullptr);
    ASSERT_NE(rasterAAssignment, nullptr);
    ASSERT_NE(dispatchBAssignment, nullptr);
    ASSERT_NE(rasterBAssignment, nullptr);
    EXPECT_EQ(dispatchAAssignment->queue, queue.id);
    EXPECT_EQ(rasterAAssignment->queue, queue.id);
    EXPECT_EQ(dispatchBAssignment->queue, queue.id);
    EXPECT_EQ(rasterBAssignment->queue, queue.id);

    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(dispatchA);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetCount(), 1u);
    EXPECT_EQ(packet, compiledPlan.packetForTask(rasterA));
    EXPECT_EQ(packet, compiledPlan.packetForTask(dispatchB));
    EXPECT_EQ(packet, compiledPlan.packetForTask(rasterB));
    EXPECT_TRUE(compiledPlan.tasksSharePacket(dispatchA, rasterB));
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    EXPECT_EQ(compiledPacket.queue, queue.id);
    EXPECT_EQ(compiledPacket.dependencyCount, 0u);
    ASSERT_EQ(compiledPacket.taskCount, 4u);
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], dispatchA);
    EXPECT_EQ(packetTasks[1u], rasterA);
    EXPECT_EQ(packetTasks[2u], dispatchB);
    EXPECT_EQ(packetTasks[3u], rasterB);
    const Graphics::GpuTaskId firstPair[] = { dispatchA, rasterA };
    const Graphics::GpuTaskId fullSequence[] = { dispatchA, rasterA, dispatchB, rasterB };
    const Graphics::GpuTaskId nonContiguousSequence[] = { dispatchA, dispatchB };
    const Graphics::GpuTaskId reversedPair[] = { rasterA, dispatchA };
    EXPECT_TRUE(compiledPlan.taskPrecedesInSamePacket(dispatchA, rasterB));
    EXPECT_FALSE(compiledPlan.taskPrecedesInSamePacket(rasterB, dispatchA));
    EXPECT_TRUE(compiledPlan.tasksFormContiguousPacketSequence(firstPair, LengthOf(firstPair)));
    EXPECT_TRUE(compiledPlan.tasksFormContiguousPacketSequence(fullSequence, LengthOf(fullSequence)));
    EXPECT_FALSE(compiledPlan.tasksFormContiguousPacketSequence(
        nonContiguousSequence,
        LengthOf(nonContiguousSequence)
    ));
    EXPECT_FALSE(compiledPlan.tasksFormContiguousPacketSequence(reversedPair, LengthOf(reversedPair)));

    const auto expectTransition = [&](const Graphics::GpuTaskId task, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        ASSERT_NE(compiledTask, nullptr);
        ASSERT_EQ(compiledTask->prologueBarrierCount, 1u);
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        ASSERT_NE(barriers, nullptr);
        const Graphics::GpuCompiledBarrier& barrier = barriers[0u];
        EXPECT_EQ(barrier.type, Graphics::GpuCompiledBarrierType::BufferTransition);
        EXPECT_EQ(barrier.resource, generatedVertexBuffer);
        EXPECT_EQ(barrier.before, before);
        EXPECT_EQ(barrier.after, after);
        EXPECT_EQ(barrier.sourceQueue, queue.id);
        EXPECT_EQ(barrier.destinationQueue, queue.id);
    };
    expectTransition(dispatchA, Graphics::ResourceStates::Common, Graphics::ResourceStates::UnorderedAccess);
    expectTransition(rasterA, Graphics::ResourceStates::UnorderedAccess, Graphics::ResourceStates::VertexBuffer);
    expectTransition(dispatchB, Graphics::ResourceStates::VertexBuffer, Graphics::ResourceStates::UnorderedAccess);
    expectTransition(rasterB, Graphics::ResourceStates::UnorderedAccess, Graphics::ResourceStates::VertexBuffer);
}

TEST(GpuTaskGraph, MergesSharedOpaqueComputeEmulationDispatchRasterTriplesIntoOnePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId generatedVertexBuffer = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shared_generated_vertex_buffer_triple"),
        "Shared Generated Vertex Buffer Triple",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(generatedVertexBuffer.valid());

    const Graphics::GpuQueueRequest graphicsComputeQueue{
        QueueCapabilities(
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueueCapability::Compute
        ),
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest graphicsRasterQueue{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint dispatchScheduling;
    dispatchScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    dispatchScheduling.overlapPreferred = false;
    dispatchScheduling.avoidQueueCrossing = true;
    dispatchScheduling.forceSubmissionBoundary = false;
    dispatchScheduling.allowPacketMerge = true;
    dispatchScheduling.mergeWithPrevious = false;
    Graphics::GpuTaskSchedulingHint sequenceTailScheduling = dispatchScheduling;
    sequenceTailScheduling.mergeWithPrevious = true;
    // Every callback has an explicit immediate predecessor, so the complete sequence remains in one accepting
    // Graphics packet even when FrontierSafe detects a later cross-queue consumer frontier.
    sequenceTailScheduling.allowMergeAcrossConsumerFrontier = true;
    const Graphics::GpuTaskResourceUse dispatchUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = generatedVertexBuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse rasterUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = generatedVertexBuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::VertexBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Name identities[] = {
        Name("tests/task_graph/shared_opaque_compute_dispatch_a_triple"),
        Name("tests/task_graph/shared_opaque_compute_raster_a_triple"),
        Name("tests/task_graph/shared_opaque_compute_dispatch_b_triple"),
        Name("tests/task_graph/shared_opaque_compute_raster_b_triple"),
        Name("tests/task_graph/shared_opaque_compute_dispatch_c_triple"),
        Name("tests/task_graph/shared_opaque_compute_raster_c_triple"),
    };
    const AStringView markers[] = {
        "Shared Opaque Compute Dispatch A Triple",
        "Shared Opaque Compute Raster A Triple",
        "Shared Opaque Compute Dispatch B Triple",
        "Shared Opaque Compute Raster B Triple",
        "Shared Opaque Compute Dispatch C Triple",
        "Shared Opaque Compute Raster C Triple",
    };
    Graphics::GpuTaskId tasks[LengthOf(identities)] = {};
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
        const bool isRaster = taskIndex % 2u != 0u;
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(identities[taskIndex])
            .setMarkerLabel(markers[taskIndex])
            .setQueue(isRaster ? graphicsRasterQueue : graphicsComputeQueue)
            .setScheduling(taskIndex == 0u ? dispatchScheduling : sequenceTailScheduling)
            .setResourceUses(
                isRaster ? rasterUses : dispatchUses,
                isRaster ? LengthOf(rasterUses) : LengthOf(dispatchUses)
            )
        ;
        if(taskIndex != 0u)
            desc.setDependencies(&tasks[taskIndex - 1u], 1u);
        tasks[taskIndex] = graph.addTask(desc);
        ASSERT_TRUE(tasks[taskIndex].valid());
    }

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    for(usize taskIndex = 1u; taskIndex < LengthOf(tasks); ++taskIndex)
        EXPECT_TRUE(analysis.hasExplicitEdge(tasks[taskIndex - 1u], tasks[taskIndex]));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[0u],
        tasks[1u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[1u],
        tasks[2u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::WriteAfterRead
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[2u],
        tasks[3u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[3u],
        tasks[4u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::WriteAfterRead
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[4u],
        tasks[5u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[0u],
        tasks[2u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[2u],
        tasks[4u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    ASSERT_EQ(analysis.topologicalOrder().size(), LengthOf(tasks));
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex)
        EXPECT_EQ(analysis.topologicalOrder()[taskIndex], tasks[taskIndex]);

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(tasks[0u]);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetCount(), 1u);
    for(const Graphics::GpuTaskId task : tasks){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue.id);
        EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Graphics);
        EXPECT_EQ(compiledPlan.packetForTask(task), packet);
    }
    EXPECT_TRUE(compiledPlan.tasksSharePacket(tasks[0u], tasks[5u]));
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    EXPECT_EQ(compiledPacket.queue, queue.id);
    EXPECT_EQ(compiledPacket.dependencyCount, 0u);
    ASSERT_EQ(compiledPacket.taskCount, LengthOf(tasks));
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex)
        EXPECT_EQ(packetTasks[taskIndex], tasks[taskIndex]);

    const auto expectTransition = [&](const usize taskIndex, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(tasks[taskIndex]).plan;
        ASSERT_NE(compiledTask, nullptr);
        ASSERT_EQ(compiledTask->prologueBarrierCount, 1u);
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(tasks[taskIndex]).prologueBarriers;
        ASSERT_NE(barriers, nullptr);
        const Graphics::GpuCompiledBarrier& barrier = barriers[0u];
        EXPECT_EQ(barrier.type, Graphics::GpuCompiledBarrierType::BufferTransition);
        EXPECT_EQ(barrier.resource, generatedVertexBuffer);
        EXPECT_EQ(barrier.before, before);
        EXPECT_EQ(barrier.after, after);
        EXPECT_EQ(barrier.sourceQueue, queue.id);
        EXPECT_EQ(barrier.destinationQueue, queue.id);
    };
    expectTransition(0u, Graphics::ResourceStates::Common, Graphics::ResourceStates::UnorderedAccess);
    expectTransition(1u, Graphics::ResourceStates::UnorderedAccess, Graphics::ResourceStates::VertexBuffer);
    expectTransition(2u, Graphics::ResourceStates::VertexBuffer, Graphics::ResourceStates::UnorderedAccess);
    expectTransition(3u, Graphics::ResourceStates::UnorderedAccess, Graphics::ResourceStates::VertexBuffer);
    expectTransition(4u, Graphics::ResourceStates::VertexBuffer, Graphics::ResourceStates::UnorderedAccess);
    expectTransition(5u, Graphics::ResourceStates::UnorderedAccess, Graphics::ResourceStates::VertexBuffer);
}

TEST(GpuTaskGraph, MergesSharedOpaqueComputeEmulationDispatchRasterQuintuplesIntoOnePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId generatedVertexBuffer = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shared_generated_vertex_buffer_quint"),
        "Shared Generated Vertex Buffer Quintuple",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(generatedVertexBuffer.valid());

    const Graphics::GpuQueueRequest graphicsComputeQueue{
        QueueCapabilities(
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueueCapability::Compute
        ),
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest graphicsRasterQueue{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint dispatchScheduling;
    dispatchScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    dispatchScheduling.overlapPreferred = false;
    dispatchScheduling.avoidQueueCrossing = true;
    dispatchScheduling.forceSubmissionBoundary = false;
    dispatchScheduling.allowPacketMerge = true;
    dispatchScheduling.mergeWithPrevious = false;
    Graphics::GpuTaskSchedulingHint sequenceTailScheduling = dispatchScheduling;
    sequenceTailScheduling.mergeWithPrevious = true;
    // Every callback has an explicit immediate predecessor, so the complete sequence remains in one accepting
    // Graphics packet even when FrontierSafe detects a later cross-queue consumer frontier.
    sequenceTailScheduling.allowMergeAcrossConsumerFrontier = true;
    const Graphics::GpuTaskResourceUse dispatchUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = generatedVertexBuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse rasterUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = generatedVertexBuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::VertexBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Name identities[] = {
        Name("tests/task_graph/shared_opaque_compute_dispatch_a_quint"),
        Name("tests/task_graph/shared_opaque_compute_raster_a_quint"),
        Name("tests/task_graph/shared_opaque_compute_dispatch_b_quint"),
        Name("tests/task_graph/shared_opaque_compute_raster_b_quint"),
        Name("tests/task_graph/shared_opaque_compute_dispatch_c_quint"),
        Name("tests/task_graph/shared_opaque_compute_raster_c_quint"),
        Name("tests/task_graph/shared_opaque_compute_dispatch_d_quint"),
        Name("tests/task_graph/shared_opaque_compute_raster_d_quint"),
        Name("tests/task_graph/shared_opaque_compute_dispatch_e_quint"),
        Name("tests/task_graph/shared_opaque_compute_raster_e_quint"),
    };
    const AStringView markers[] = {
        "Shared Opaque Compute Dispatch A Quintuple",
        "Shared Opaque Compute Raster A Quintuple",
        "Shared Opaque Compute Dispatch B Quintuple",
        "Shared Opaque Compute Raster B Quintuple",
        "Shared Opaque Compute Dispatch C Quintuple",
        "Shared Opaque Compute Raster C Quintuple",
        "Shared Opaque Compute Dispatch D Quintuple",
        "Shared Opaque Compute Raster D Quintuple",
        "Shared Opaque Compute Dispatch E Quintuple",
        "Shared Opaque Compute Raster E Quintuple",
    };
    Graphics::GpuTaskId tasks[LengthOf(identities)] = {};
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
        const bool isRaster = taskIndex % 2u != 0u;
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(identities[taskIndex])
            .setMarkerLabel(markers[taskIndex])
            .setQueue(isRaster ? graphicsRasterQueue : graphicsComputeQueue)
            .setScheduling(taskIndex == 0u ? dispatchScheduling : sequenceTailScheduling)
            .setResourceUses(
                isRaster ? rasterUses : dispatchUses,
                isRaster ? LengthOf(rasterUses) : LengthOf(dispatchUses)
            )
        ;
        if(taskIndex != 0u)
            desc.setDependencies(&tasks[taskIndex - 1u], 1u);
        tasks[taskIndex] = graph.addTask(desc);
        ASSERT_TRUE(tasks[taskIndex].valid());
    }

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    for(usize taskIndex = 1u; taskIndex < LengthOf(tasks); ++taskIndex)
        EXPECT_TRUE(analysis.hasExplicitEdge(tasks[taskIndex - 1u], tasks[taskIndex]));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[0u],
        tasks[1u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[1u],
        tasks[2u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::WriteAfterRead
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[2u],
        tasks[3u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[3u],
        tasks[4u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::WriteAfterRead
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[4u],
        tasks[5u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[5u],
        tasks[6u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::WriteAfterRead
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[6u],
        tasks[7u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[7u],
        tasks[8u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::WriteAfterRead
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[8u],
        tasks[9u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[0u],
        tasks[2u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[2u],
        tasks[4u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[4u],
        tasks[6u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        tasks[6u],
        tasks[8u],
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    ASSERT_EQ(analysis.topologicalOrder().size(), LengthOf(tasks));
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex)
        EXPECT_EQ(analysis.topologicalOrder()[taskIndex], tasks[taskIndex]);

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(tasks[0u]);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetCount(), 1u);
    for(const Graphics::GpuTaskId task : tasks){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue.id);
        EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Graphics);
        EXPECT_EQ(compiledPlan.packetForTask(task), packet);
    }
    EXPECT_TRUE(compiledPlan.tasksSharePacket(tasks[0u], tasks[9u]));
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    EXPECT_EQ(compiledPacket.queue, queue.id);
    EXPECT_EQ(compiledPacket.dependencyCount, 0u);
    ASSERT_EQ(compiledPacket.taskCount, LengthOf(tasks));
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex)
        EXPECT_EQ(packetTasks[taskIndex], tasks[taskIndex]);

    const auto expectTransition = [&](const usize taskIndex, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(tasks[taskIndex]).plan;
        ASSERT_NE(compiledTask, nullptr);
        ASSERT_EQ(compiledTask->prologueBarrierCount, 1u);
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(tasks[taskIndex]).prologueBarriers;
        ASSERT_NE(barriers, nullptr);
        const Graphics::GpuCompiledBarrier& barrier = barriers[0u];
        EXPECT_EQ(barrier.type, Graphics::GpuCompiledBarrierType::BufferTransition);
        EXPECT_EQ(barrier.resource, generatedVertexBuffer);
        EXPECT_EQ(barrier.before, before);
        EXPECT_EQ(barrier.after, after);
        EXPECT_EQ(barrier.sourceQueue, queue.id);
        EXPECT_EQ(barrier.destinationQueue, queue.id);
    };
    expectTransition(0u, Graphics::ResourceStates::Common, Graphics::ResourceStates::UnorderedAccess);
    expectTransition(1u, Graphics::ResourceStates::UnorderedAccess, Graphics::ResourceStates::VertexBuffer);
    expectTransition(2u, Graphics::ResourceStates::VertexBuffer, Graphics::ResourceStates::UnorderedAccess);
    expectTransition(3u, Graphics::ResourceStates::UnorderedAccess, Graphics::ResourceStates::VertexBuffer);
    expectTransition(4u, Graphics::ResourceStates::VertexBuffer, Graphics::ResourceStates::UnorderedAccess);
    expectTransition(5u, Graphics::ResourceStates::UnorderedAccess, Graphics::ResourceStates::VertexBuffer);
    expectTransition(6u, Graphics::ResourceStates::VertexBuffer, Graphics::ResourceStates::UnorderedAccess);
    expectTransition(7u, Graphics::ResourceStates::UnorderedAccess, Graphics::ResourceStates::VertexBuffer);
    expectTransition(8u, Graphics::ResourceStates::VertexBuffer, Graphics::ResourceStates::UnorderedAccess);
    expectTransition(9u, Graphics::ResourceStates::UnorderedAccess, Graphics::ResourceStates::VertexBuffer);
}

TEST(GpuTaskGraph, MergesOpaqueCsgReceiverComputeProducerIntoGbufferPacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId csgReceiverEvent = AddBufferMetadata(
        graph,
        Name("tests/task_graph/csg_receiver_event"),
        "CSG Receiver Event",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId csgClipContext = AddBufferMetadata(
        graph,
        Name("tests/task_graph/csg_clip_context"),
        "CSG Clip Context",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertexBuffer = AddBufferMetadata(
        graph,
        Name("tests/task_graph/csg_receiver_generated_vertex"),
        "CSG Receiver Generated Vertex",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(csgReceiverEvent.valid());
    ASSERT_TRUE(csgClipContext.valid());
    ASSERT_TRUE(generatedVertexBuffer.valid());

    const Graphics::GpuQueueRequest graphicsComputeQueue{
        QueueCapabilities(
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueueCapability::Compute
        ),
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    clearScheduling.overlapPreferred = false;
    clearScheduling.avoidQueueCrossing = true;
    clearScheduling.forceSubmissionBoundary = false;
    clearScheduling.allowPacketMerge = true;
    clearScheduling.mergeWithPrevious = false;
    const Graphics::GpuTaskResourceUse clearUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = csgReceiverEvent,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId csgClear = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/csg_receiver_clear"))
            .setMarkerLabel("CSG Receiver Clear")
            .setQueue(graphicsComputeQueue)
            .setScheduling(clearScheduling)
            .setResourceUses(clearUses, LengthOf(clearUses))
    );
    ASSERT_TRUE(csgClear.valid());

    Graphics::GpuTaskSchedulingHint producerScheduling = clearScheduling;
    producerScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    producerScheduling.mergeWithPrevious = true;
    producerScheduling.allowMergeAcrossConsumerFrontier = true;
    const Graphics::GpuTaskResourceUse producerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = csgClipContext,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = generatedVertexBuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId producer = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/csg_receiver_compute_emulation"))
            .setMarkerLabel("CSG Receiver Compute Emulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(producerScheduling)
            .setDependencies(&csgClear, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses))
    );
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskSchedulingHint gbufferScheduling = producerScheduling;
    const Graphics::GpuTaskResourceUse gbufferUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = csgReceiverEvent,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = generatedVertexBuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::VertexBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskId gbuffer = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/csg_receiver_gbuffer"))
            .setMarkerLabel("CSG Receiver G-buffer")
            .setQueue(graphicsComputeQueue)
            .setScheduling(gbufferScheduling)
            .setDependencies(&producer, 1u)
            .setResourceUses(gbufferUses, LengthOf(gbufferUses))
    );
    ASSERT_TRUE(gbuffer.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    EXPECT_TRUE(analysis.hasExplicitEdge(csgClear, producer));
    EXPECT_TRUE(analysis.hasExplicitEdge(producer, gbuffer));
    EXPECT_TRUE(analysis.hasInferredEdge(csgClear, gbuffer));
    EXPECT_TRUE(analysis.hasInferredEdge(producer, gbuffer));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        csgClear,
        gbuffer,
        csgReceiverEvent,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        gbuffer,
        generatedVertexBuffer,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 1u);
    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(csgClear);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(packet, compiledPlan.packetForTask(producer));
    EXPECT_EQ(packet, compiledPlan.packetForTask(gbuffer));
    EXPECT_TRUE(compiledPlan.tasksSharePacket(csgClear, gbuffer));
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    EXPECT_EQ(compiledPacket.queue, queue.id);
    ASSERT_EQ(compiledPacket.taskCount, 3u);
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], csgClear);
    EXPECT_EQ(packetTasks[1u], producer);
    EXPECT_EQ(packetTasks[2u], gbuffer);

    const auto hasTransition = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        if(!compiledTask)
            return false;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(usize barrierIndex = 0u; barriers && barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTransition(
        csgClear,
        csgReceiverEvent,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasTransition(
        producer,
        csgClipContext,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::ConstantBuffer
    ));
    EXPECT_TRUE(hasTransition(
        producer,
        generatedVertexBuffer,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasTransition(
        gbuffer,
        csgReceiverEvent,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasTransition(
        gbuffer,
        generatedVertexBuffer,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::VertexBuffer
    ));
}

TEST(GpuTaskGraph, MergesAliasFreeOpaqueCsgIntervalSampleComputeEmulationWithRaster){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId removedInterval = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_interval_sample_removed_interval"),
        "Opaque CSG Removed Interval"
    );
    const Graphics::GpuGraphResourceId generatedVertexA = AddBufferMetadata(
        graph,
        Name("tests/task_graph/csg_interval_sample_generated_vertex_a"),
        "Opaque CSG Interval-Sample Generated Vertex A",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertexB = AddBufferMetadata(
        graph,
        Name("tests/task_graph/csg_interval_sample_generated_vertex_b"),
        "Opaque CSG Interval-Sample Generated Vertex B",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(removedInterval.valid());
    ASSERT_TRUE(generatedVertexA.valid());
    ASSERT_TRUE(generatedVertexB.valid());
    EXPECT_NE(generatedVertexA, generatedVertexB);

    const Graphics::GpuGraphResourceId generatedVertexOutputs[] = {
        generatedVertexA,
        generatedVertexB,
    };
    const Graphics::GpuGraphResourceSetId generatedVertexOutputSet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/csg_interval_sample_generated_vertex_outputs"))
            .setMarkerLabel("Opaque CSG Interval-Sample Generated Vertex Outputs")
            .setMembers(generatedVertexOutputs, LengthOf(generatedVertexOutputs))
    );
    ASSERT_TRUE(generatedVertexOutputSet.valid());

    const Graphics::TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 1u);
    const Graphics::GpuTaskResourceUse combineUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = removedInterval,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse computeEmulationUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = removedInterval,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskResourceUse sampleUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = removedInterval,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskResourceSetUse outputUavSetUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskResourceSetUse outputVertexBufferSetUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = Graphics::ResourceStates::VertexBuffer,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };

    const Graphics::GpuQueueRequest graphicsComputeQueue{
        QueueCapabilities(
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueueCapability::Compute
        ),
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint combineScheduling;
    combineScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    combineScheduling.overlapPreferred = false;
    combineScheduling.avoidQueueCrossing = true;
    combineScheduling.forceSubmissionBoundary = false;
    combineScheduling.allowPacketMerge = true;
    combineScheduling.mergeWithPrevious = false;
    const Graphics::GpuTaskId combine = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/csg_interval_sample_combine"))
            .setMarkerLabel("Opaque CSG Interval Combine")
            .setQueue(graphicsComputeQueue)
            .setScheduling(combineScheduling)
            .setResourceUses(combineUses, LengthOf(combineUses))
    );
    ASSERT_TRUE(combine.valid());

    Graphics::GpuTaskSchedulingHint handoffScheduling = combineScheduling;
    handoffScheduling.mergeWithPrevious = true;
    handoffScheduling.allowMergeAcrossConsumerFrontier = true;
    const Graphics::GpuTaskId computeEmulation = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/csg_interval_sample_compute_emulation"))
            .setMarkerLabel("Opaque CSG Interval-Sample Compute Emulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(handoffScheduling)
            .setDependencies(&combine, 1u)
            .setResourceUses(computeEmulationUses, LengthOf(computeEmulationUses))
            .setResourceSetUses(&outputUavSetUse, 1u)
    );
    ASSERT_TRUE(computeEmulation.valid());

    const Graphics::GpuTaskId sample = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/csg_interval_sample_raster"))
            .setMarkerLabel("Opaque CSG Interval Sample")
            .setQueue(graphicsComputeQueue)
            .setScheduling(handoffScheduling)
            .setDependencies(&computeEmulation, 1u)
            .setResourceUses(sampleUses, LengthOf(sampleUses))
            .setResourceSetUses(&outputVertexBufferSetUse, 1u)
    );
    ASSERT_TRUE(sample.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    EXPECT_TRUE(analysis.hasExplicitEdge(combine, computeEmulation));
    EXPECT_TRUE(analysis.hasExplicitEdge(computeEmulation, sample));
    EXPECT_TRUE(analysis.hasInferredEdge(combine, computeEmulation));
    EXPECT_TRUE(analysis.hasInferredEdge(combine, sample));
    EXPECT_TRUE(analysis.hasInferredEdge(computeEmulation, sample));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        computeEmulation,
        removedInterval,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        computeEmulation,
        sample,
        generatedVertexA,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        computeEmulation,
        sample,
        generatedVertexB,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_EQ(analysis.topologicalOrder().size(), 3u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], combine);
    EXPECT_EQ(analysis.topologicalOrder()[1u], computeEmulation);
    EXPECT_EQ(analysis.topologicalOrder()[2u], sample);

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    for(const Graphics::GpuTaskId task : { combine, computeEmulation, sample }){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue.id);
        EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Graphics);
    }
    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(combine);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetCount(), 1u);
    EXPECT_EQ(packet, compiledPlan.packetForTask(computeEmulation));
    EXPECT_EQ(packet, compiledPlan.packetForTask(sample));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(combine, computeEmulation));
    EXPECT_TRUE(compiledPlan.tasksSharePacket(computeEmulation, sample));
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    EXPECT_EQ(compiledPacket.queue, queue.id);
    EXPECT_EQ(compiledPacket.dependencyCount, 0u);
    ASSERT_EQ(compiledPacket.taskCount, 3u);
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], combine);
    EXPECT_EQ(packetTasks[1u], computeEmulation);
    EXPECT_EQ(packetTasks[2u], sample);

    const auto hasTextureBarrier = [&](const Graphics::GpuTaskId task, const Graphics::GpuCompiledBarrierType::Enum type, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        if(!compiledTask)
            return false;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; barriers && barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == type
                && barrier.resource == removedInterval
                && barrier.range.textureSubresources == removedIntervalRange
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    const auto hasBufferTransition = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        if(!compiledTask)
            return false;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; barriers && barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
                && barrier.sourceQueue == queue.id
                && barrier.destinationQueue == queue.id
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTextureBarrier(
        combine,
        Graphics::GpuCompiledBarrierType::TextureTransition,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasTextureBarrier(
        computeEmulation,
        Graphics::GpuCompiledBarrierType::TextureUav,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::UnorderedAccess
    ));
    for(const Graphics::GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(hasBufferTransition(
            computeEmulation,
            output,
            Graphics::ResourceStates::Common,
            Graphics::ResourceStates::UnorderedAccess
        ));
        EXPECT_TRUE(hasBufferTransition(
            sample,
            output,
            Graphics::ResourceStates::UnorderedAccess,
            Graphics::ResourceStates::VertexBuffer
        ));
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

