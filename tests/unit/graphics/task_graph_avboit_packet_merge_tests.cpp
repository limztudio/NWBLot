// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_avboit_packet_merge_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, MergesAliasFreeAvboitExtinctionGeneratedVertexHandoffWithRaster){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId generatedVertexA = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_extinction_generated_vertex_a"),
        "AVBOIT Extinction Generated Vertex A",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertexB = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_extinction_generated_vertex_b"),
        "AVBOIT Extinction Generated Vertex B",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(generatedVertexA.valid());
    ASSERT_TRUE(generatedVertexB.valid());
    EXPECT_NE(generatedVertexA, generatedVertexB);

    const Graphics::GpuGraphResourceId generatedVertexOutputs[] = {
        generatedVertexA,
        generatedVertexB,
    };
    const Graphics::GpuGraphResourceSetId generatedVertexOutputSet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/avboit_extinction_generated_vertex_outputs"))
            .setMarkerLabel("AVBOIT Extinction Generated Vertex Outputs")
            .setMembers(generatedVertexOutputs, LengthOf(generatedVertexOutputs))
    );
    ASSERT_TRUE(generatedVertexOutputSet.valid());

    const Graphics::GpuQueueRequest graphicsComputeQueue{
        QueueCapabilities(
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueueCapability::Compute
        ),
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    producerScheduling.overlapPreferred = false;
    producerScheduling.avoidQueueCrossing = true;
    producerScheduling.forceSubmissionBoundary = false;
    producerScheduling.allowPacketMerge = true;
    producerScheduling.mergeWithPrevious = false;
    const Graphics::GpuTaskResourceSetUse outputUavSetUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskId producer = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/avboit_extinction_generated_vertex_producer"))
            .setMarkerLabel("AVBOIT Extinction Generated Vertex Producer")
            .setQueue(graphicsComputeQueue)
            .setScheduling(producerScheduling)
            .setResourceSetUses(&outputUavSetUse, 1u)
    );
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskSchedulingHint rasterScheduling = producerScheduling;
    rasterScheduling.mergeWithPrevious = true;
    rasterScheduling.allowMergeAcrossConsumerFrontier = true;
    const Graphics::GpuTaskResourceSetUse outputVertexBufferSetUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = Graphics::ResourceStates::VertexBuffer,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    const Graphics::GpuTaskId raster = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/avboit_extinction_generated_vertex_raster"))
            .setMarkerLabel("AVBOIT Extinction Generated Vertex Raster")
            .setQueue(graphicsComputeQueue)
            .setScheduling(rasterScheduling)
            .setDependencies(&producer, 1u)
            .setResourceSetUses(&outputVertexBufferSetUse, 1u)
    );
    ASSERT_TRUE(raster.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    EXPECT_TRUE(analysis.hasExplicitEdge(producer, raster));
    EXPECT_TRUE(analysis.hasInferredEdge(producer, raster));
    for(const Graphics::GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(HasInferredHazard(
            analysis,
            producer,
            raster,
            output,
            Graphics::GpuTaskHazardType::ReadAfterWrite
        ));
    }
    ASSERT_EQ(analysis.topologicalOrder().size(), 2u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], producer);
    EXPECT_EQ(analysis.topologicalOrder()[1u], raster);

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


    for(const Graphics::GpuTaskId task : { producer, raster }){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue.id);
        EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Graphics);
    }
    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(producer);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetCount(), 1u);
    EXPECT_EQ(packet, compiledPlan.packetForTask(raster));
    EXPECT_TRUE(compiledPlan.tasksSharePacket(producer, raster));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(producer, raster));
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    EXPECT_EQ(compiledPacket.queue, queue.id);
    EXPECT_EQ(compiledPacket.dependencyCount, 0u);
    ASSERT_EQ(compiledPacket.taskCount, 2u);
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], producer);
    EXPECT_EQ(packetTasks[1u], raster);

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
    for(const Graphics::GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(hasBufferTransition(
            producer,
            output,
            Graphics::ResourceStates::Common,
            Graphics::ResourceStates::UnorderedAccess
        ));
        EXPECT_TRUE(hasBufferTransition(
            raster,
            output,
            Graphics::ResourceStates::UnorderedAccess,
            Graphics::ResourceStates::VertexBuffer
        ));
    }
}

TEST(GpuTaskGraph, MergesAliasFreeAvboitAccumulationGeneratedVertexHandoffWithRaster){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId generatedVertexA = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_accumulation_generated_vertex_a"),
        "AVBOIT Accumulation Generated Vertex A",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertexB = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_accumulation_generated_vertex_b"),
        "AVBOIT Accumulation Generated Vertex B",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(generatedVertexA.valid());
    ASSERT_TRUE(generatedVertexB.valid());
    EXPECT_NE(generatedVertexA, generatedVertexB);

    const Graphics::GpuGraphResourceId generatedVertexOutputs[] = {
        generatedVertexA,
        generatedVertexB,
    };
    const Graphics::GpuGraphResourceSetId generatedVertexOutputSet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/avboit_accumulation_generated_vertex_outputs"))
            .setMarkerLabel("AVBOIT Accumulation Generated Vertex Outputs")
            .setMembers(generatedVertexOutputs, LengthOf(generatedVertexOutputs))
    );
    ASSERT_TRUE(generatedVertexOutputSet.valid());

    const Graphics::GpuQueueRequest graphicsComputeQueue{
        QueueCapabilities(
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueueCapability::Compute
        ),
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    producerScheduling.overlapPreferred = false;
    producerScheduling.avoidQueueCrossing = true;
    producerScheduling.forceSubmissionBoundary = false;
    producerScheduling.allowPacketMerge = true;
    producerScheduling.mergeWithPrevious = false;
    const Graphics::GpuTaskResourceSetUse outputUavSetUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskId producer = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/avboit_accumulation_generated_vertex_producer"))
            .setMarkerLabel("AVBOIT Accumulation Generated Vertex Producer")
            .setQueue(graphicsComputeQueue)
            .setScheduling(producerScheduling)
            .setResourceSetUses(&outputUavSetUse, 1u)
    );
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskSchedulingHint rasterScheduling = producerScheduling;
    rasterScheduling.mergeWithPrevious = true;
    rasterScheduling.allowMergeAcrossConsumerFrontier = true;
    const Graphics::GpuTaskResourceSetUse outputVertexBufferSetUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = Graphics::ResourceStates::VertexBuffer,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    const Graphics::GpuTaskId raster = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/avboit_accumulation_generated_vertex_raster"))
            .setMarkerLabel("AVBOIT Accumulation Generated Vertex Raster")
            .setQueue(graphicsComputeQueue)
            .setScheduling(rasterScheduling)
            .setDependencies(&producer, 1u)
            .setResourceSetUses(&outputVertexBufferSetUse, 1u)
    );
    ASSERT_TRUE(raster.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    EXPECT_TRUE(analysis.hasExplicitEdge(producer, raster));
    EXPECT_TRUE(analysis.hasInferredEdge(producer, raster));
    for(const Graphics::GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(HasInferredHazard(
            analysis,
            producer,
            raster,
            output,
            Graphics::GpuTaskHazardType::ReadAfterWrite
        ));
    }
    ASSERT_EQ(analysis.topologicalOrder().size(), 2u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], producer);
    EXPECT_EQ(analysis.topologicalOrder()[1u], raster);

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


    for(const Graphics::GpuTaskId task : { producer, raster }){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue.id);
        EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Graphics);
    }
    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(producer);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetCount(), 1u);
    EXPECT_EQ(packet, compiledPlan.packetForTask(raster));
    EXPECT_TRUE(compiledPlan.tasksSharePacket(producer, raster));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(producer, raster));
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    EXPECT_EQ(compiledPacket.queue, queue.id);
    EXPECT_EQ(compiledPacket.dependencyCount, 0u);
    ASSERT_EQ(compiledPacket.taskCount, 2u);
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], producer);
    EXPECT_EQ(packetTasks[1u], raster);

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
    for(const Graphics::GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(hasBufferTransition(
            producer,
            output,
            Graphics::ResourceStates::Common,
            Graphics::ResourceStates::UnorderedAccess
        ));
        EXPECT_TRUE(hasBufferTransition(
            raster,
            output,
            Graphics::ResourceStates::UnorderedAccess,
            Graphics::ResourceStates::VertexBuffer
        ));
    }
}

TEST(GpuTaskGraph, MergesAliasFreeAvboitOccupancyGeneratedVertexHandoffWithRaster){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId generatedVertexA = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_occupancy_generated_vertex_a"),
        "AVBOIT Occupancy Generated Vertex A",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertexB = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_occupancy_generated_vertex_b"),
        "AVBOIT Occupancy Generated Vertex B",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(generatedVertexA.valid());
    ASSERT_TRUE(generatedVertexB.valid());
    EXPECT_NE(generatedVertexA, generatedVertexB);

    const Graphics::GpuGraphResourceId generatedVertexOutputs[] = {
        generatedVertexA,
        generatedVertexB,
    };
    const Graphics::GpuGraphResourceSetId generatedVertexOutputSet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/avboit_occupancy_generated_vertex_outputs"))
            .setMarkerLabel("AVBOIT Occupancy Generated Vertex Outputs")
            .setMembers(generatedVertexOutputs, LengthOf(generatedVertexOutputs))
    );
    ASSERT_TRUE(generatedVertexOutputSet.valid());

    const Graphics::GpuQueueRequest graphicsComputeQueue{
        QueueCapabilities(
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueueCapability::Compute
        ),
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    producerScheduling.overlapPreferred = false;
    producerScheduling.avoidQueueCrossing = true;
    producerScheduling.forceSubmissionBoundary = false;
    producerScheduling.allowPacketMerge = true;
    producerScheduling.mergeWithPrevious = false;
    const Graphics::GpuTaskResourceSetUse outputUavSetUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskId producer = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/avboit_occupancy_generated_vertex_producer"))
            .setMarkerLabel("AVBOIT Occupancy Generated Vertex Producer")
            .setQueue(graphicsComputeQueue)
            .setScheduling(producerScheduling)
            .setResourceSetUses(&outputUavSetUse, 1u)
    );
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskSchedulingHint rasterScheduling = producerScheduling;
    rasterScheduling.mergeWithPrevious = true;
    rasterScheduling.allowMergeAcrossConsumerFrontier = true;
    const Graphics::GpuTaskResourceSetUse outputVertexBufferSetUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = Graphics::ResourceStates::VertexBuffer,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    const Graphics::GpuTaskId raster = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/avboit_occupancy_generated_vertex_raster"))
            .setMarkerLabel("AVBOIT Occupancy Generated Vertex Raster")
            .setQueue(graphicsComputeQueue)
            .setScheduling(rasterScheduling)
            .setDependencies(&producer, 1u)
            .setResourceSetUses(&outputVertexBufferSetUse, 1u)
    );
    ASSERT_TRUE(raster.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    EXPECT_TRUE(analysis.hasExplicitEdge(producer, raster));
    EXPECT_TRUE(analysis.hasInferredEdge(producer, raster));
    for(const Graphics::GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(HasInferredHazard(
            analysis,
            producer,
            raster,
            output,
            Graphics::GpuTaskHazardType::ReadAfterWrite
        ));
    }
    ASSERT_EQ(analysis.topologicalOrder().size(), 2u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], producer);
    EXPECT_EQ(analysis.topologicalOrder()[1u], raster);

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


    for(const Graphics::GpuTaskId task : { producer, raster }){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue.id);
        EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Graphics);
    }
    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(producer);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetCount(), 1u);
    EXPECT_EQ(packet, compiledPlan.packetForTask(raster));
    EXPECT_TRUE(compiledPlan.tasksSharePacket(producer, raster));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(producer, raster));
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    EXPECT_EQ(compiledPacket.queue, queue.id);
    EXPECT_EQ(compiledPacket.dependencyCount, 0u);
    ASSERT_EQ(compiledPacket.taskCount, 2u);
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], producer);
    EXPECT_EQ(packetTasks[1u], raster);

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
    for(const Graphics::GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(hasBufferTransition(
            producer,
            output,
            Graphics::ResourceStates::Common,
            Graphics::ResourceStates::UnorderedAccess
        ));
        EXPECT_TRUE(hasBufferTransition(
            raster,
            output,
            Graphics::ResourceStates::UnorderedAccess,
            Graphics::ResourceStates::VertexBuffer
        ));
    }
}

TEST(GpuTaskGraph, PlacesNaturalAvboitStagesAcrossCollapsedHybridAndSplitPackets){
    struct PlacementCase{
        bool depthWarpOnCompute;
        bool integrationOnCompute;
        u32 expectedPacketIndices[5u];
        usize expectedPacketCount;
    };
    const PlacementCase testCases[] = {
        PlacementCase{
            .depthWarpOnCompute = false,
            .integrationOnCompute = false,
            .expectedPacketIndices = { 0u, 0u, 0u, 0u, 0u },
            .expectedPacketCount = 1u,
        },
        PlacementCase{
            .depthWarpOnCompute = true,
            .integrationOnCompute = false,
            .expectedPacketIndices = { 0u, 1u, 2u, 2u, 2u },
            .expectedPacketCount = 3u,
        },
        PlacementCase{
            .depthWarpOnCompute = false,
            .integrationOnCompute = true,
            .expectedPacketIndices = { 0u, 0u, 0u, 1u, 2u },
            .expectedPacketCount = 3u,
        },
        PlacementCase{
            .depthWarpOnCompute = true,
            .integrationOnCompute = true,
            .expectedPacketIndices = { 0u, 1u, 2u, 3u, 4u },
            .expectedPacketCount = 5u,
        },
    };

    for(const PlacementCase& testCase : testCases){
        SCOPED_TRACE(testCase.expectedPacketCount);
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuQueueRequest graphicsRequest{
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueuePreference::Graphics,
            false,
            false,
        };
        const Graphics::GpuQueueRequest naturalComputeRequest{
            Graphics::GpuQueueCapability::Compute,
            Graphics::GpuQueuePreference::Compute,
            true,
            true,
        };

        Graphics::GpuTaskSchedulingHint firstScheduling;
        firstScheduling.allowPacketMerge = true;
        Graphics::GpuTaskSchedulingHint successorScheduling;
        successorScheduling.allowPacketMerge = true;
        successorScheduling.mergeWithPrevious = true;
        successorScheduling.allowMergeAcrossConsumerFrontier = true;
        Graphics::GpuTaskSchedulingHint computeStageScheduling = successorScheduling;
        computeStageScheduling.allowSameClassQueueRouting = true;
        computeStageScheduling.allowCrossFamilySameClassQueueRouting = true;
        computeStageScheduling.allowTimingFeedbackRouting = true;
        computeStageScheduling.allowCrossClassTimingFeedbackRouting = true;
        const Name depthWarpIdentity("tests/task_graph/natural_avboit_depth_warp");
        const Name integrationIdentity("tests/task_graph/natural_avboit_integration");

        const Graphics::GpuTaskId pre = AddTaskWithQueue(
            graph,
            Name("tests/task_graph/natural_avboit_pre"),
            "AVBOIT Pre",
            graphicsRequest,
            firstScheduling
        );
        const Graphics::GpuTaskId depthWarp = AddTaskWithQueue(
            graph,
            depthWarpIdentity,
            "AVBOIT Depth Warp",
            naturalComputeRequest,
            computeStageScheduling,
            {},
            &pre,
            1u
        );
        const Graphics::GpuTaskId extinction = AddTaskWithQueue(
            graph,
            Name("tests/task_graph/natural_avboit_extinction"),
            "AVBOIT Extinction",
            graphicsRequest,
            successorScheduling,
            {},
            &depthWarp,
            1u
        );
        const Graphics::GpuTaskId integration = AddTaskWithQueue(
            graph,
            integrationIdentity,
            "AVBOIT Integration",
            naturalComputeRequest,
            computeStageScheduling,
            {},
            &extinction,
            1u
        );
        const Graphics::GpuTaskId accumulation = AddTaskWithQueue(
            graph,
            Name("tests/task_graph/natural_avboit_accumulation"),
            "AVBOIT Accumulation",
            graphicsRequest,
            successorScheduling,
            {},
            &integration,
            1u
        );
        const Graphics::GpuTaskId tasks[] = { pre, depthWarp, extinction, integration, accumulation };
        for(const Graphics::GpuTaskId task : tasks)
            ASSERT_TRUE(task.valid());

        const Graphics::GpuPhysicalQueueInfo queues[] = {
            GraphicsQueue(),
            DedicatedComputeQueue(),
        };
        const Graphics::GpuTaskGraphQueueTopology topology{
            .queues = queues,
            .queueCount = LengthOf(queues),
        };
        const Graphics::GpuTaskTimingKey depthWarpGraphicsKey{
            .task = depthWarpIdentity,
            .queue = Graphics::CommandQueue::Graphics,
        };
        const Graphics::GpuTaskTimingKey depthWarpComputeKey{
            .task = depthWarpIdentity,
            .queue = Graphics::CommandQueue::Compute,
        };
        const Graphics::GpuTaskTimingKey integrationGraphicsKey{
            .task = integrationIdentity,
            .queue = Graphics::CommandQueue::Graphics,
        };
        const Graphics::GpuTaskTimingKey integrationComputeKey{
            .task = integrationIdentity,
            .queue = Graphics::CommandQueue::Compute,
        };
        const f64 depthWarpGraphicsSeconds = testCase.depthWarpOnCompute ? 0.010 : 0.001;
        const f64 depthWarpComputeSeconds = testCase.depthWarpOnCompute ? 0.001 : 0.010;
        const f64 integrationGraphicsSeconds = testCase.integrationOnCompute ? 0.010 : 0.001;
        const f64 integrationComputeSeconds = testCase.integrationOnCompute ? 0.001 : 0.010;
        Graphics::GpuTaskTimingHistoryStore timingHistory(testArena.arena);
        ASSERT_TRUE(timingHistory.recordSample(depthWarpComputeKey, queues[1u].id, depthWarpComputeSeconds, 1u));
        ASSERT_TRUE(timingHistory.recordSample(depthWarpGraphicsKey, queues[0u].id, depthWarpGraphicsSeconds, 2u));
        ASSERT_TRUE(timingHistory.recordSample(integrationComputeKey, queues[1u].id, integrationComputeSeconds, 1u));
        ASSERT_TRUE(timingHistory.recordSample(integrationGraphicsKey, queues[0u].id, integrationGraphicsSeconds, 2u));
        Graphics::GpuTaskTimingHistorySnapshot timingSnapshot(testArena.arena);
        timingHistory.snapshot(timingSnapshot);
        ASSERT_TRUE(timingSnapshot.valid());
        Graphics::GpuTaskTimingFeedbackPolicy timingPolicy;
        timingPolicy.enabled = true;
        timingPolicy.minimumSampleCount = 1u;
        timingPolicy.calibrationIntervalFrames = 0u;
        timingPolicy.minimumAbsoluteBenefitSeconds = 0.001;
        timingPolicy.minimumRelativeBenefit = 0.1;
        timingPolicy.minimumFramesBetweenSwitches = 0u;
        Graphics::GpuTaskGraphCompileOptions compileOptions;
        compileOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
        compileOptions.queueAssignmentOptions.timingHistory = &timingSnapshot;
        compileOptions.queueAssignmentOptions.timingFeedbackPolicy = timingPolicy;
        compileOptions.queueAssignmentOptions.timingFrameIndex = 20u;
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, compileOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


        ASSERT_EQ(analysis.topologicalOrder().size(), LengthOf(tasks));
        ASSERT_EQ(compiledPlan.packetCount(), testCase.expectedPacketCount);
        const Graphics::CommandQueue::Enum expectedQueueClasses[] = {
            Graphics::CommandQueue::Graphics,
            testCase.depthWarpOnCompute ? Graphics::CommandQueue::Compute : Graphics::CommandQueue::Graphics,
            Graphics::CommandQueue::Graphics,
            testCase.integrationOnCompute ? Graphics::CommandQueue::Compute : Graphics::CommandQueue::Graphics,
            Graphics::CommandQueue::Graphics,
        };
        const Graphics::GpuTaskQueueAssignmentReason::Enum expectedAssignmentReasons[] = {
            Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics,
            Graphics::GpuTaskQueueAssignmentReason::CompilerOverride,
            Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics,
            Graphics::GpuTaskQueueAssignmentReason::CompilerOverride,
            Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics,
        };
        const bool expectedTimingFeedback[] = {
            false,
            testCase.depthWarpOnCompute,
            false,
            testCase.integrationOnCompute,
            false,
        };
        for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
            EXPECT_EQ(analysis.topologicalOrder()[taskIndex], tasks[taskIndex]);
            const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(tasks[taskIndex]);
            ASSERT_NE(assignment, nullptr);
            EXPECT_EQ(assignment->queueClass, expectedQueueClasses[taskIndex]);
            EXPECT_EQ(assignment->reason, expectedAssignmentReasons[taskIndex]);
            EXPECT_EQ(
                assignment->queue,
                expectedQueueClasses[taskIndex] == Graphics::CommandQueue::Compute
                    ? queues[1u].id
                    : queues[0u].id
            );
            EXPECT_EQ(
                bool(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback),
                expectedTimingFeedback[taskIndex]
            );
            if(taskIndex == 1u || taskIndex == 3u)
                EXPECT_EQ(assignment->initialQueue, queues[0u].id);

            const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(tasks[taskIndex]);
            ASSERT_TRUE(packet.valid());
            EXPECT_EQ(packet.index, testCase.expectedPacketIndices[taskIndex]);
        }

        for(usize firstTaskIndex = 0u; firstTaskIndex < LengthOf(tasks); ++firstTaskIndex){
            for(usize secondTaskIndex = firstTaskIndex + 1u; secondTaskIndex < LengthOf(tasks); ++secondTaskIndex){
                const bool sharesPacket = testCase.expectedPacketIndices[firstTaskIndex]
                    == testCase.expectedPacketIndices[secondTaskIndex]
                ;
                EXPECT_EQ(compiledPlan.tasksSharePacket(tasks[firstTaskIndex], tasks[secondTaskIndex]), sharesPacket);
                EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(tasks[firstTaskIndex], tasks[secondTaskIndex]));
                EXPECT_EQ(
                    compiledPlan.taskPrecedesOrSharesPacket(tasks[secondTaskIndex], tasks[firstTaskIndex]),
                    sharesPacket
                );
                EXPECT_EQ(
                    compiledPlan.taskPrecedesInSamePacket(tasks[firstTaskIndex], tasks[secondTaskIndex]),
                    sharesPacket
                );
                EXPECT_FALSE(compiledPlan.taskPrecedesInSamePacket(tasks[secondTaskIndex], tasks[firstTaskIndex]));
            }
        }
        EXPECT_EQ(
            compiledPlan.tasksFormContiguousPacketSequence(tasks, LengthOf(tasks)),
            testCase.expectedPacketCount == 1u
        );

        const Graphics::GpuSubmissionPacketRange avboitRange = compiledPlan.packetRangeForTasks(pre, accumulation);
        ASSERT_TRUE(avboitRange.valid());
        EXPECT_TRUE(compiledPlan.validPacketRange(avboitRange));
        EXPECT_EQ(avboitRange.first, compiledPlan.packetForTask(pre));
        EXPECT_EQ(avboitRange.packetCount, testCase.expectedPacketCount);
    }
}

TEST(GpuTaskGraph, MergesExtinctionUploadChainIntoAsyncAvboitPacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId avboitWorking = AddTextureMetadata(
        graph,
        Name("tests/task_graph/extinction_upload_working"),
        "AVBOIT Extinction Working",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId materialInstances = AddBufferMetadata(
        graph,
        Name("tests/task_graph/extinction_upload_instances"),
        "AVBOIT Extinction Material Instances",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId materialTyped = AddBufferMetadata(
        graph,
        Name("tests/task_graph/extinction_upload_typed"),
        "AVBOIT Extinction Material Typed",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(avboitWorking.valid());
    ASSERT_TRUE(materialInstances.valid());
    ASSERT_TRUE(materialTyped.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;
    Graphics::GpuTaskSchedulingHint mergeScheduling;
    mergeScheduling.allowPacketMerge = true;
    mergeScheduling.mergeWithPrevious = true;

    const Graphics::GpuTaskResourceUse workingReadWrite[] = {
        Graphics::GpuTaskResourceUse{
            .resource = avboitWorking,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc preDesc;
    preDesc
        .setIdentity(Name("tests/task_graph/extinction_upload_pre"))
        .setMarkerLabel("AVBOIT Pre")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(workingReadWrite, LengthOf(workingReadWrite))
    ;
    const Graphics::GpuTaskId pre = graph.addTask(preDesc);
    ASSERT_TRUE(pre.valid());

    const Graphics::GpuTaskId preDependency[] = { pre };
    Graphics::GpuTaskDesc depthWarpDesc;
    depthWarpDesc
        .setIdentity(Name("tests/task_graph/extinction_upload_depth_warp"))
        .setMarkerLabel("AVBOIT Depth Warp")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(preDependency, LengthOf(preDependency))
        .setResourceUses(workingReadWrite, LengthOf(workingReadWrite))
    ;
    const Graphics::GpuTaskId depthWarp = graph.addTask(depthWarpDesc);
    ASSERT_TRUE(depthWarp.valid());

    const Graphics::GpuTaskResourceUse instanceUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = materialInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId depthWarpDependency[] = { depthWarp };
    Graphics::GpuTaskDesc instanceUploadDesc;
    instanceUploadDesc
        .setIdentity(Name("tests/task_graph/extinction_upload_instances"))
        .setMarkerLabel("AVBOIT Extinction Material Instances Upload")
        .setQueue(graphicsRequest)
        .setScheduling(mergeScheduling)
        .setDependencies(depthWarpDependency, LengthOf(depthWarpDependency))
        .setResourceUses(instanceUploadUses, LengthOf(instanceUploadUses))
    ;
    const Graphics::GpuTaskId instanceUpload = graph.addTask(instanceUploadDesc);
    ASSERT_TRUE(instanceUpload.valid());

    const Graphics::GpuTaskResourceUse typedUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = materialTyped,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId instanceUploadDependency[] = { instanceUpload };
    Graphics::GpuTaskDesc typedUploadDesc;
    typedUploadDesc
        .setIdentity(Name("tests/task_graph/extinction_upload_typed"))
        .setMarkerLabel("AVBOIT Extinction Material Typed Upload")
        .setQueue(graphicsRequest)
        .setScheduling(mergeScheduling)
        .setDependencies(instanceUploadDependency, LengthOf(instanceUploadDependency))
        .setResourceUses(typedUploadUses, LengthOf(typedUploadUses))
    ;
    const Graphics::GpuTaskId typedUpload = graph.addTask(typedUploadDesc);
    ASSERT_TRUE(typedUpload.valid());

    const Graphics::GpuTaskResourceUse extinctionUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = avboitWorking,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialTyped,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskId typedUploadDependency[] = { typedUpload };
    Graphics::GpuTaskDesc extinctionDesc;
    extinctionDesc
        .setIdentity(Name("tests/task_graph/extinction_upload_native"))
        .setMarkerLabel("AVBOIT Extinction")
        .setQueue(graphicsRequest)
        .setScheduling(mergeScheduling)
        .setDependencies(typedUploadDependency, LengthOf(typedUploadDependency))
        .setResourceUses(extinctionUses, LengthOf(extinctionUses))
    ;
    const Graphics::GpuTaskId extinction = graph.addTask(extinctionDesc);
    ASSERT_TRUE(extinction.valid());

    const Graphics::GpuTaskId extinctionDependency[] = { extinction };
    Graphics::GpuTaskDesc integrationDesc;
    integrationDesc
        .setIdentity(Name("tests/task_graph/extinction_upload_integration"))
        .setMarkerLabel("AVBOIT Integration")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(extinctionDependency, LengthOf(extinctionDependency))
        .setResourceUses(workingReadWrite, LengthOf(workingReadWrite))
    ;
    const Graphics::GpuTaskId integration = graph.addTask(integrationDesc);
    ASSERT_TRUE(integration.valid());

    const Graphics::GpuTaskId integrationDependency[] = { integration };
    Graphics::GpuTaskDesc accumulationDesc;
    accumulationDesc
        .setIdentity(Name("tests/task_graph/extinction_upload_accumulation"))
        .setMarkerLabel("AVBOIT Accumulation")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(integrationDependency, LengthOf(integrationDependency))
        .setResourceUses(workingReadWrite, LengthOf(workingReadWrite))
    ;
    const Graphics::GpuTaskId accumulation = graph.addTask(accumulationDesc);
    ASSERT_TRUE(accumulation.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 5u);

    const Graphics::GpuSubmissionPacketId prePacket = compiledPlan.packetForTask(pre);
    const Graphics::GpuSubmissionPacketId depthWarpPacket = compiledPlan.packetForTask(depthWarp);
    const Graphics::GpuSubmissionPacketId instanceUploadPacket = compiledPlan.packetForTask(instanceUpload);
    const Graphics::GpuSubmissionPacketId typedUploadPacket = compiledPlan.packetForTask(typedUpload);
    const Graphics::GpuSubmissionPacketId extinctionPacket = compiledPlan.packetForTask(extinction);
    const Graphics::GpuSubmissionPacketId integrationPacket = compiledPlan.packetForTask(integration);
    const Graphics::GpuSubmissionPacketId accumulationPacket = compiledPlan.packetForTask(accumulation);
    ASSERT_TRUE(prePacket.valid());
    ASSERT_TRUE(depthWarpPacket.valid());
    ASSERT_TRUE(instanceUploadPacket.valid());
    ASSERT_TRUE(typedUploadPacket.valid());
    ASSERT_TRUE(extinctionPacket.valid());
    ASSERT_TRUE(integrationPacket.valid());
    ASSERT_TRUE(accumulationPacket.valid());
    EXPECT_NE(prePacket, depthWarpPacket);
    EXPECT_NE(depthWarpPacket, extinctionPacket);
    EXPECT_EQ(instanceUploadPacket, extinctionPacket);
    EXPECT_EQ(typedUploadPacket, extinctionPacket);
    EXPECT_NE(extinctionPacket, integrationPacket);
    EXPECT_NE(integrationPacket, accumulationPacket);
    EXPECT_EQ(compiledPlan.packet(extinctionPacket).plan->taskCount, 3u);

    const Graphics::GpuSubmissionPacketRange avboitRange = compiledPlan.packetRange(prePacket, accumulationPacket);
    ASSERT_TRUE(avboitRange.valid());
    EXPECT_EQ(avboitRange.packetCount, 5u);
    ASSERT_EQ(compiledPlan.packet(integrationPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(integrationPacket).dependencies[0u].producer, extinctionPacket);
}

TEST(GpuTaskGraph, MergesAccumulationUploadChainIntoAsyncAvboitPacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId avboitWorking = AddTextureMetadata(
        graph,
        Name("tests/task_graph/accumulation_upload_working"),
        "AVBOIT Accumulation Working",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId materialInstances = AddBufferMetadata(
        graph,
        Name("tests/task_graph/accumulation_upload_instances"),
        "AVBOIT Accumulation Material Instances",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId materialTyped = AddBufferMetadata(
        graph,
        Name("tests/task_graph/accumulation_upload_typed"),
        "AVBOIT Accumulation Material Typed",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId csgReceiverRanges = AddBufferMetadata(
        graph,
        Name("tests/task_graph/accumulation_upload_receiver_ranges"),
        "AVBOIT Accumulation CSG Receiver Ranges",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId csgCutters = AddBufferMetadata(
        graph,
        Name("tests/task_graph/accumulation_upload_cutters"),
        "AVBOIT Accumulation CSG Cutters",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId csgClipContext = AddBufferMetadata(
        graph,
        Name("tests/task_graph/accumulation_upload_clip_context"),
        "AVBOIT Accumulation CSG Clip Context",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    // The interval producer owns this input. Accumulation consumes but must not re-upload it.
    const Graphics::GpuGraphResourceId csgIntervalSampleState = AddBufferMetadata(
        graph,
        Name("tests/task_graph/accumulation_interval_sample_state"),
        "AVBOIT CSG Interval Sample State",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(avboitWorking.valid());
    ASSERT_TRUE(materialInstances.valid());
    ASSERT_TRUE(materialTyped.valid());
    ASSERT_TRUE(csgReceiverRanges.valid());
    ASSERT_TRUE(csgCutters.valid());
    ASSERT_TRUE(csgClipContext.valid());
    ASSERT_TRUE(csgIntervalSampleState.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;
    Graphics::GpuTaskSchedulingHint mergeScheduling;
    mergeScheduling.allowPacketMerge = true;
    mergeScheduling.mergeWithPrevious = true;

    const Graphics::GpuTaskResourceUse workingReadWrite[] = {
        Graphics::GpuTaskResourceUse{
            .resource = avboitWorking,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc preDesc;
    preDesc
        .setIdentity(Name("tests/task_graph/accumulation_upload_pre"))
        .setMarkerLabel("AVBOIT Pre")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(workingReadWrite, LengthOf(workingReadWrite))
    ;
    const Graphics::GpuTaskId pre = graph.addTask(preDesc);
    ASSERT_TRUE(pre.valid());

    const Graphics::GpuTaskId preDependency[] = { pre };
    Graphics::GpuTaskDesc depthWarpDesc;
    depthWarpDesc
        .setIdentity(Name("tests/task_graph/accumulation_upload_depth_warp"))
        .setMarkerLabel("AVBOIT Depth Warp")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(preDependency, LengthOf(preDependency))
        .setResourceUses(workingReadWrite, LengthOf(workingReadWrite))
    ;
    const Graphics::GpuTaskId depthWarp = graph.addTask(depthWarpDesc);
    ASSERT_TRUE(depthWarp.valid());

    const Graphics::GpuTaskId depthWarpDependency[] = { depthWarp };
    Graphics::GpuTaskDesc extinctionDesc;
    extinctionDesc
        .setIdentity(Name("tests/task_graph/accumulation_upload_extinction"))
        .setMarkerLabel("AVBOIT Extinction")
        .setQueue(graphicsRequest)
        .setScheduling(mergeScheduling)
        .setDependencies(depthWarpDependency, LengthOf(depthWarpDependency))
        .setResourceUses(workingReadWrite, LengthOf(workingReadWrite))
    ;
    const Graphics::GpuTaskId extinction = graph.addTask(extinctionDesc);
    ASSERT_TRUE(extinction.valid());

    const Graphics::GpuTaskId extinctionDependency[] = { extinction };
    Graphics::GpuTaskDesc integrationDesc;
    integrationDesc
        .setIdentity(Name("tests/task_graph/accumulation_upload_integration"))
        .setMarkerLabel("AVBOIT Integration")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(extinctionDependency, LengthOf(extinctionDependency))
        .setResourceUses(workingReadWrite, LengthOf(workingReadWrite))
    ;
    const Graphics::GpuTaskId integration = graph.addTask(integrationDesc);
    ASSERT_TRUE(integration.valid());

    const auto addAccumulationUpload = [&](
        const Name& identity,
        const AStringView label,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::GpuTaskId dependency
    ){
        const Graphics::GpuTaskResourceUse uploadUse[] = {
            Graphics::GpuTaskResourceUse{
                .resource = resource,
                .range = {},
                .requiredState = Graphics::ResourceStates::CopyDest,
                .access = Graphics::GpuTaskResourceAccess::Write,
            },
        };
        Graphics::GpuTaskDesc uploadDesc;
        uploadDesc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(graphicsRequest)
            .setScheduling(mergeScheduling)
            .setDependencies(&dependency, 1u)
            .setResourceUses(uploadUse, LengthOf(uploadUse))
        ;
        return graph.addTask(uploadDesc);
    };

    const Graphics::GpuTaskId instanceUpload = addAccumulationUpload(
        Name("tests/task_graph/accumulation_upload_instances"),
        "AVBOIT Accumulation Material Instances Upload",
        materialInstances,
        integration
    );
    ASSERT_TRUE(instanceUpload.valid());
    const Graphics::GpuTaskId typedUpload = addAccumulationUpload(
        Name("tests/task_graph/accumulation_upload_typed"),
        "AVBOIT Accumulation Material Typed Upload",
        materialTyped,
        instanceUpload
    );
    ASSERT_TRUE(typedUpload.valid());
    const Graphics::GpuTaskId receiverRangesUpload = addAccumulationUpload(
        Name("tests/task_graph/accumulation_upload_receiver_ranges"),
        "AVBOIT Accumulation CSG Receiver Ranges Upload",
        csgReceiverRanges,
        typedUpload
    );
    ASSERT_TRUE(receiverRangesUpload.valid());
    const Graphics::GpuTaskId cuttersUpload = addAccumulationUpload(
        Name("tests/task_graph/accumulation_upload_cutters"),
        "AVBOIT Accumulation CSG Cutters Upload",
        csgCutters,
        receiverRangesUpload
    );
    ASSERT_TRUE(cuttersUpload.valid());
    const Graphics::GpuTaskId clipContextUpload = addAccumulationUpload(
        Name("tests/task_graph/accumulation_upload_clip_context"),
        "AVBOIT Accumulation CSG Clip Context Slots Upload",
        csgClipContext,
        cuttersUpload
    );
    ASSERT_TRUE(clipContextUpload.valid());

    const Graphics::GpuTaskResourceUse accumulationUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = avboitWorking,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialTyped,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = csgReceiverRanges,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = csgCutters,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = csgClipContext,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = csgIntervalSampleState,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskId clipContextUploadDependency[] = { clipContextUpload };
    Graphics::GpuTaskDesc accumulationDesc;
    accumulationDesc
        .setIdentity(Name("tests/task_graph/accumulation_upload_native"))
        .setMarkerLabel("AVBOIT Accumulation")
        .setQueue(graphicsRequest)
        .setScheduling(mergeScheduling)
        .setDependencies(clipContextUploadDependency, LengthOf(clipContextUploadDependency))
        .setResourceUses(accumulationUses, LengthOf(accumulationUses))
    ;
    const Graphics::GpuTaskId accumulation = graph.addTask(accumulationDesc);
    ASSERT_TRUE(accumulation.valid());

    // The final consumer owns the outgoing Graphics -> Compute edge so FrontierSafe can still merge the uploads.
    const Graphics::GpuTaskId accumulationDependency[] = { accumulation };
    Graphics::GpuTaskDesc compositeDesc;
    compositeDesc
        .setIdentity(Name("tests/task_graph/accumulation_upload_composite"))
        .setMarkerLabel("Deferred Composite")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(accumulationDependency, LengthOf(accumulationDependency))
        .setResourceUses(workingReadWrite, LengthOf(workingReadWrite))
    ;
    const Graphics::GpuTaskId composite = graph.addTask(compositeDesc);
    ASSERT_TRUE(composite.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 6u);

    const Graphics::GpuSubmissionPacketId prePacket = compiledPlan.packetForTask(pre);
    const Graphics::GpuSubmissionPacketId depthWarpPacket = compiledPlan.packetForTask(depthWarp);
    const Graphics::GpuSubmissionPacketId extinctionPacket = compiledPlan.packetForTask(extinction);
    const Graphics::GpuSubmissionPacketId integrationPacket = compiledPlan.packetForTask(integration);
    const Graphics::GpuSubmissionPacketId instanceUploadPacket = compiledPlan.packetForTask(instanceUpload);
    const Graphics::GpuSubmissionPacketId typedUploadPacket = compiledPlan.packetForTask(typedUpload);
    const Graphics::GpuSubmissionPacketId receiverRangesUploadPacket = compiledPlan.packetForTask(receiverRangesUpload);
    const Graphics::GpuSubmissionPacketId cuttersUploadPacket = compiledPlan.packetForTask(cuttersUpload);
    const Graphics::GpuSubmissionPacketId clipContextUploadPacket = compiledPlan.packetForTask(clipContextUpload);
    const Graphics::GpuSubmissionPacketId accumulationPacket = compiledPlan.packetForTask(accumulation);
    const Graphics::GpuSubmissionPacketId compositePacket = compiledPlan.packetForTask(composite);
    ASSERT_TRUE(prePacket.valid());
    ASSERT_TRUE(depthWarpPacket.valid());
    ASSERT_TRUE(extinctionPacket.valid());
    ASSERT_TRUE(integrationPacket.valid());
    ASSERT_TRUE(instanceUploadPacket.valid());
    ASSERT_TRUE(typedUploadPacket.valid());
    ASSERT_TRUE(receiverRangesUploadPacket.valid());
    ASSERT_TRUE(cuttersUploadPacket.valid());
    ASSERT_TRUE(clipContextUploadPacket.valid());
    ASSERT_TRUE(accumulationPacket.valid());
    ASSERT_TRUE(compositePacket.valid());
    EXPECT_NE(prePacket, depthWarpPacket);
    EXPECT_NE(depthWarpPacket, extinctionPacket);
    EXPECT_NE(extinctionPacket, integrationPacket);
    EXPECT_NE(integrationPacket, accumulationPacket);
    EXPECT_EQ(instanceUploadPacket, accumulationPacket);
    EXPECT_EQ(typedUploadPacket, accumulationPacket);
    EXPECT_EQ(receiverRangesUploadPacket, accumulationPacket);
    EXPECT_EQ(cuttersUploadPacket, accumulationPacket);
    EXPECT_EQ(clipContextUploadPacket, accumulationPacket);
    EXPECT_EQ(compiledPlan.packet(accumulationPacket).plan->taskCount, 6u);
    ASSERT_EQ(compiledPlan.packet(compositePacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(compositePacket).dependencies[0u].producer, accumulationPacket);

    const Graphics::GpuSubmissionPacketRange avboitRange = compiledPlan.packetRange(prePacket, accumulationPacket);
    ASSERT_TRUE(avboitRange.valid());
    EXPECT_EQ(avboitRange.packetCount, 5u);
}

TEST(GpuTaskGraph, MergesAccumulationTailIntoGraphicsAvboitPacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId materialInstances = AddBufferMetadata(
        graph,
        Name("tests/task_graph/graphics_accumulation_instances"),
        "AVBOIT Graphics Accumulation Material Instances"
    );
    ASSERT_TRUE(materialInstances.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.allowPacketMerge = true;
    Graphics::GpuTaskSchedulingHint mergeScheduling;
    mergeScheduling.allowPacketMerge = true;
    mergeScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    Graphics::GpuTaskDesc preDesc;
    preDesc
        .setIdentity(Name("tests/task_graph/graphics_accumulation_pre"))
        .setMarkerLabel("AVBOIT Pre")
        .setQueue(graphicsRequest)
        .setScheduling(firstScheduling)
    ;
    const Graphics::GpuTaskId pre = graph.addTask(preDesc);
    ASSERT_TRUE(pre.valid());

    const auto addMergedGraphicsTask = [&](const Name& identity, const AStringView label, const Graphics::GpuTaskId dependency){
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(graphicsRequest)
            .setScheduling(mergeScheduling)
            .setDependencies(&dependency, 1u)
        ;
        return graph.addTask(desc);
    };

    const Graphics::GpuTaskId occupancy = addMergedGraphicsTask(
        Name("tests/task_graph/graphics_accumulation_occupancy"),
        "AVBOIT Occupancy",
        pre
    );
    ASSERT_TRUE(occupancy.valid());
    const Graphics::GpuTaskId extinction = addMergedGraphicsTask(
        Name("tests/task_graph/graphics_accumulation_extinction"),
        "AVBOIT Extinction",
        occupancy
    );
    ASSERT_TRUE(extinction.valid());

    const Graphics::GpuTaskResourceUse uploadUse[] = {
        Graphics::GpuTaskResourceUse{
            .resource = materialInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc uploadDesc;
    uploadDesc
        .setIdentity(Name("tests/task_graph/graphics_accumulation_instances_upload"))
        .setMarkerLabel("AVBOIT Accumulation Material Instances Upload")
        .setQueue(graphicsRequest)
        .setScheduling(mergeScheduling)
        .setDependencies(&extinction, 1u)
        .setResourceUses(uploadUse, LengthOf(uploadUse))
    ;
    const Graphics::GpuTaskId instanceUpload = graph.addTask(uploadDesc);
    ASSERT_TRUE(instanceUpload.valid());

    const Graphics::GpuTaskResourceUse accumulationUse[] = {
        Graphics::GpuTaskResourceUse{
            .resource = materialInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc accumulationDesc;
    accumulationDesc
        .setIdentity(Name("tests/task_graph/graphics_accumulation_native"))
        .setMarkerLabel("AVBOIT Accumulation")
        .setQueue(graphicsRequest)
        .setScheduling(mergeScheduling)
        .setDependencies(&instanceUpload, 1u)
        .setResourceUses(accumulationUse, LengthOf(accumulationUse))
    ;
    const Graphics::GpuTaskId accumulation = graph.addTask(accumulationDesc);
    ASSERT_TRUE(accumulation.valid());

    const Graphics::GpuTaskId accumulationDependency[] = { accumulation };
    Graphics::GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/task_graph/graphics_accumulation_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(accumulationDependency, LengthOf(accumulationDependency))
    ;
    const Graphics::GpuTaskId lighting = graph.addTask(lightingDesc);
    ASSERT_TRUE(lighting.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 2u);

    const Graphics::GpuSubmissionPacketId prePacket = compiledPlan.packetForTask(pre);
    const Graphics::GpuSubmissionPacketId occupancyPacket = compiledPlan.packetForTask(occupancy);
    const Graphics::GpuSubmissionPacketId extinctionPacket = compiledPlan.packetForTask(extinction);
    const Graphics::GpuSubmissionPacketId uploadPacket = compiledPlan.packetForTask(instanceUpload);
    const Graphics::GpuSubmissionPacketId accumulationPacket = compiledPlan.packetForTask(accumulation);
    const Graphics::GpuSubmissionPacketId lightingPacket = compiledPlan.packetForTask(lighting);
    ASSERT_TRUE(prePacket.valid());
    ASSERT_TRUE(occupancyPacket.valid());
    ASSERT_TRUE(extinctionPacket.valid());
    ASSERT_TRUE(uploadPacket.valid());
    ASSERT_TRUE(accumulationPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    EXPECT_EQ(occupancyPacket, prePacket);
    EXPECT_EQ(extinctionPacket, prePacket);
    EXPECT_EQ(uploadPacket, prePacket);
    EXPECT_EQ(accumulationPacket, prePacket);
    EXPECT_NE(lightingPacket, prePacket);
    ASSERT_EQ(compiledPlan.packet(lightingPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(lightingPacket).dependencies[0u].producer, accumulationPacket);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

