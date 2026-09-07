// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_avboit_occupancy_emulation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, KeepsUnsplitAvboitOccupancyAliasFreeRegularComputeEmulationInPrePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId stateProbe = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_state_probe"),
        "AVBOIT Occupancy State Probe",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId materialStream = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_material_stream"),
        "AVBOIT Occupancy Material Stream",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId coverage = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_coverage"),
        "AVBOIT Coverage",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertexA = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_generated_vertex_a"),
        "AVBOIT Occupancy Generated Vertex A",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertexB = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_generated_vertex_b"),
        "AVBOIT Occupancy Generated Vertex B",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(stateProbe.valid());
    ASSERT_TRUE(materialStream.valid());
    ASSERT_TRUE(coverage.valid());
    ASSERT_TRUE(generatedVertexA.valid());
    ASSERT_TRUE(generatedVertexB.valid());
    EXPECT_NE(generatedVertexA, generatedVertexB);

    const Graphics::GpuGraphResourceId generatedVertexOutputs[] = {
        generatedVertexA,
        generatedVertexB,
    };
    const Graphics::GpuGraphResourceSetId generatedVertexOutputSet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_generated_vertex_outputs"))
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
    const Graphics::GpuQueueRequest graphicsQueue{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint preScheduling;
    preScheduling.cost = Graphics::GpuTaskCostHint::Large;
    preScheduling.overlapPreferred = false;
    preScheduling.avoidQueueCrossing = true;
    preScheduling.forceSubmissionBoundary = false;
    preScheduling.allowPacketMerge = true;
    preScheduling.mergeWithPrevious = false;
    Graphics::GpuTaskSchedulingHint packetTailScheduling = preScheduling;
    packetTailScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    packetTailScheduling.mergeWithPrevious = true;
    packetTailScheduling.allowMergeAcrossConsumerFrontier = true;

    const Graphics::GpuTaskResourceUse preUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskId pre = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_pre"))
            .setMarkerLabel("AVBOIT Pre")
            .setQueue(graphicsQueue)
            .setScheduling(preScheduling)
            .setResourceUses(preUses, LengthOf(preUses))
    );
    ASSERT_TRUE(pre.valid());

    const Graphics::GpuTaskResourceUse streamUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialStream,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId streamUpload = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_material_upload"))
            .setMarkerLabel("AVBOIT Occupancy Material Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&pre, 1u)
            .setResourceUses(streamUses, LengthOf(streamUses))
    );
    ASSERT_TRUE(streamUpload.valid());

    const Graphics::GpuTaskResourceUse clearUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId clear = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_clear"))
            .setMarkerLabel("AVBOIT Clear Coverage")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&streamUpload, 1u)
            .setResourceUses(clearUses, LengthOf(clearUses))
    );
    ASSERT_TRUE(clear.valid());

    const Graphics::GpuTaskResourceUse producerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialStream,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskResourceSetUse producerGeneratedVertexUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskId producer = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_compute_emulation"))
            .setMarkerLabel("AVBOIT Occupancy Compute Emulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&clear, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses))
            .setResourceSetUses(&producerGeneratedVertexUse, 1u)
    );
    ASSERT_TRUE(producer.valid());

    const Graphics::GpuTaskResourceUse occupancyUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialStream,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskResourceSetUse occupancyGeneratedVertexUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = Graphics::ResourceStates::VertexBuffer,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    const Graphics::GpuTaskId occupancy = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_raster"))
            .setMarkerLabel("AVBOIT Occupancy")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&producer, 1u)
            .setResourceUses(occupancyUses, LengthOf(occupancyUses))
            .setResourceSetUses(&occupancyGeneratedVertexUse, 1u)
    );
    ASSERT_TRUE(occupancy.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    EXPECT_TRUE(analysis.hasExplicitEdge(pre, streamUpload));
    EXPECT_TRUE(analysis.hasExplicitEdge(streamUpload, clear));
    EXPECT_TRUE(analysis.hasExplicitEdge(clear, producer));
    EXPECT_TRUE(analysis.hasExplicitEdge(producer, occupancy));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        streamUpload,
        producer,
        materialStream,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        clear,
        occupancy,
        coverage,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    for(const Graphics::GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(HasInferredHazard(
            analysis,
            producer,
            occupancy,
            output,
            Graphics::GpuTaskHazardType::ReadAfterWrite
        ));
    }
    ASSERT_EQ(analysis.topologicalOrder().size(), 5u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], pre);
    EXPECT_EQ(analysis.topologicalOrder()[1u], streamUpload);
    EXPECT_EQ(analysis.topologicalOrder()[2u], clear);
    EXPECT_EQ(analysis.topologicalOrder()[3u], producer);
    EXPECT_EQ(analysis.topologicalOrder()[4u], occupancy);

    for(const Graphics::GpuTaskId task : { pre, streamUpload, clear, producer, occupancy }){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue.id);
        EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Graphics);
    }
    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(pre);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetCount(), 1u);
    EXPECT_EQ(packet, compiledPlan.packetForTask(streamUpload));
    EXPECT_EQ(packet, compiledPlan.packetForTask(clear));
    EXPECT_EQ(packet, compiledPlan.packetForTask(producer));
    EXPECT_EQ(packet, compiledPlan.packetForTask(occupancy));
    EXPECT_TRUE(compiledPlan.tasksSharePacket(pre, occupancy));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(pre, streamUpload));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(streamUpload, clear));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(clear, producer));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(producer, occupancy));
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    EXPECT_EQ(compiledPacket.queue, queue.id);
    EXPECT_EQ(compiledPacket.dependencyCount, 0u);
    ASSERT_EQ(compiledPacket.taskCount, 5u);
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], pre);
    EXPECT_EQ(packetTasks[1u], streamUpload);
    EXPECT_EQ(packetTasks[2u], clear);
    EXPECT_EQ(packetTasks[3u], producer);
    EXPECT_EQ(packetTasks[4u], occupancy);

    const auto hasBufferTransition = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask && barriers && barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
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
    EXPECT_TRUE(hasBufferTransition(
        streamUpload,
        materialStream,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        clear,
        coverage,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        producer,
        materialStream,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ShaderResource
    ));
    for(const Graphics::GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(hasBufferTransition(
            producer,
            output,
            Graphics::ResourceStates::Common,
            Graphics::ResourceStates::UnorderedAccess
        ));
        EXPECT_TRUE(hasBufferTransition(
            occupancy,
            output,
            Graphics::ResourceStates::UnorderedAccess,
            Graphics::ResourceStates::VertexBuffer
        ));
    }
    EXPECT_TRUE(hasBufferTransition(
        occupancy,
        coverage,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
}

TEST(GpuTaskGraph, KeepsUnsplitAvboitOccupancySharedOutputComputeEmulationPairsInPrePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId stateProbe = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_state_probe"),
        "AVBOIT Occupancy Shared Output State Probe",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId materialStream = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_material_stream"),
        "AVBOIT Occupancy Shared Output Material Stream",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId coverage = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_coverage"),
        "AVBOIT Coverage",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertex = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_generated_vertex"),
        "AVBOIT Occupancy Shared Generated Vertex",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(stateProbe.valid());
    ASSERT_TRUE(materialStream.valid());
    ASSERT_TRUE(coverage.valid());
    ASSERT_TRUE(generatedVertex.valid());

    const Graphics::GpuQueueRequest graphicsComputeQueue{
        QueueCapabilities(
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueueCapability::Compute
        ),
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest graphicsQueue{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint preScheduling;
    preScheduling.cost = Graphics::GpuTaskCostHint::Large;
    preScheduling.overlapPreferred = false;
    preScheduling.avoidQueueCrossing = true;
    preScheduling.forceSubmissionBoundary = false;
    preScheduling.allowPacketMerge = true;
    preScheduling.mergeWithPrevious = false;
    Graphics::GpuTaskSchedulingHint packetTailScheduling = preScheduling;
    packetTailScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    packetTailScheduling.mergeWithPrevious = true;
    packetTailScheduling.allowMergeAcrossConsumerFrontier = true;

    const Graphics::GpuTaskResourceUse preUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskResourceUse streamUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialStream,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse clearUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse dispatchUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialStream,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = generatedVertex,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse rasterUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialStream,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = generatedVertex,
            .range = {},
            .requiredState = Graphics::ResourceStates::VertexBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskResourceUse depthWarpUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };

    const Graphics::GpuTaskId pre = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_pre"))
            .setMarkerLabel("AVBOIT Pre")
            .setQueue(graphicsQueue)
            .setScheduling(preScheduling)
            .setResourceUses(preUses, LengthOf(preUses))
    );
    ASSERT_TRUE(pre.valid());
    const Graphics::GpuTaskId stream = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_stream"))
            .setMarkerLabel("AVBOIT Occupancy Material Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&pre, 1u)
            .setResourceUses(streamUses, LengthOf(streamUses))
    );
    ASSERT_TRUE(stream.valid());
    const Graphics::GpuTaskId clear = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_clear"))
            .setMarkerLabel("AVBOIT Clear Coverage")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&stream, 1u)
            .setResourceUses(clearUses, LengthOf(clearUses))
    );
    ASSERT_TRUE(clear.valid());
    const Graphics::GpuTaskId dispatchA = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_dispatch_a"))
            .setMarkerLabel("AVBOIT Occupancy Shared Compute Emulation A")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&clear, 1u)
            .setResourceUses(dispatchUses, LengthOf(dispatchUses))
    );
    ASSERT_TRUE(dispatchA.valid());
    const Graphics::GpuTaskId rasterADependencies[] = { dispatchA };
    const Graphics::GpuTaskId rasterA = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_raster_a"))
            .setMarkerLabel("AVBOIT Occupancy Shared A")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(rasterADependencies, LengthOf(rasterADependencies))
            .setResourceUses(rasterUses, LengthOf(rasterUses))
    );
    ASSERT_TRUE(rasterA.valid());
    const Graphics::GpuTaskId dispatchBDependencies[] = { rasterA };
    const Graphics::GpuTaskId dispatchB = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_dispatch_b"))
            .setMarkerLabel("AVBOIT Occupancy Shared Compute Emulation B")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(dispatchBDependencies, LengthOf(dispatchBDependencies))
            .setResourceUses(dispatchUses, LengthOf(dispatchUses))
    );
    ASSERT_TRUE(dispatchB.valid());
    const Graphics::GpuTaskId rasterBDependencies[] = { dispatchB };
    const Graphics::GpuTaskId rasterB = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_raster_b"))
            .setMarkerLabel("AVBOIT Occupancy Shared B")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(rasterBDependencies, LengthOf(rasterBDependencies))
            .setResourceUses(rasterUses, LengthOf(rasterUses))
    );
    ASSERT_TRUE(rasterB.valid());
    const Graphics::GpuTaskId depthWarpDependencies[] = { rasterB };
    const Graphics::GpuTaskId depthWarp = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_depth_warp"))
            .setMarkerLabel("AVBOIT Depth Warp")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(depthWarpDependencies, LengthOf(depthWarpDependencies))
            .setResourceUses(depthWarpUses, LengthOf(depthWarpUses))
    );
    ASSERT_TRUE(depthWarp.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    EXPECT_TRUE(analysis.hasExplicitEdge(pre, stream));
    EXPECT_TRUE(analysis.hasExplicitEdge(stream, clear));
    EXPECT_TRUE(analysis.hasExplicitEdge(clear, dispatchA));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchA, rasterA));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterA, dispatchB));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchB, rasterB));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterB, depthWarp));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        stream,
        dispatchA,
        materialStream,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        clear,
        rasterA,
        coverage,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        dispatchA,
        rasterA,
        generatedVertex,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        rasterA,
        dispatchB,
        generatedVertex,
        Graphics::GpuTaskHazardType::WriteAfterRead
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        dispatchB,
        rasterB,
        generatedVertex,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        rasterB,
        depthWarp,
        coverage,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_EQ(analysis.topologicalOrder().size(), 8u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], pre);
    EXPECT_EQ(analysis.topologicalOrder()[1u], stream);
    EXPECT_EQ(analysis.topologicalOrder()[2u], clear);
    EXPECT_EQ(analysis.topologicalOrder()[3u], dispatchA);
    EXPECT_EQ(analysis.topologicalOrder()[4u], rasterA);
    EXPECT_EQ(analysis.topologicalOrder()[5u], dispatchB);
    EXPECT_EQ(analysis.topologicalOrder()[6u], rasterB);
    EXPECT_EQ(analysis.topologicalOrder()[7u], depthWarp);

    for(const Graphics::GpuTaskId task : { pre, stream, clear, dispatchA, rasterA, dispatchB, rasterB, depthWarp }){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue.id);
        EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Graphics);
    }
    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(pre);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetCount(), 1u);
    EXPECT_EQ(packet, compiledPlan.packetForTask(stream));
    EXPECT_EQ(packet, compiledPlan.packetForTask(clear));
    EXPECT_EQ(packet, compiledPlan.packetForTask(dispatchA));
    EXPECT_EQ(packet, compiledPlan.packetForTask(rasterA));
    EXPECT_EQ(packet, compiledPlan.packetForTask(dispatchB));
    EXPECT_EQ(packet, compiledPlan.packetForTask(rasterB));
    EXPECT_EQ(packet, compiledPlan.packetForTask(depthWarp));
    EXPECT_TRUE(compiledPlan.tasksSharePacket(pre, depthWarp));
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    EXPECT_EQ(compiledPacket.queue, queue.id);
    EXPECT_EQ(compiledPacket.dependencyCount, 0u);
    ASSERT_EQ(compiledPacket.taskCount, 8u);
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], pre);
    EXPECT_EQ(packetTasks[1u], stream);
    EXPECT_EQ(packetTasks[2u], clear);
    EXPECT_EQ(packetTasks[3u], dispatchA);
    EXPECT_EQ(packetTasks[4u], rasterA);
    EXPECT_EQ(packetTasks[5u], dispatchB);
    EXPECT_EQ(packetTasks[6u], rasterB);
    EXPECT_EQ(packetTasks[7u], depthWarp);

    const auto hasBufferTransition = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask && barriers && barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
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
    EXPECT_TRUE(hasBufferTransition(
        stream,
        materialStream,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        clear,
        coverage,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        dispatchA,
        materialStream,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        dispatchA,
        generatedVertex,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterA,
        generatedVertex,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::VertexBuffer
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterA,
        coverage,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        dispatchB,
        generatedVertex,
        Graphics::ResourceStates::VertexBuffer,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterB,
        generatedVertex,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::VertexBuffer
    ));
}

TEST(GpuTaskGraph, KeepsUnsplitAvboitOccupancySharedOutputComputeEmulationTriplesInPrePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId stateProbe = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_triple_state_probe"),
        "AVBOIT Occupancy Shared Output State Probe",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId materialStream = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_triple_material_stream"),
        "AVBOIT Occupancy Shared Output Material Stream",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId coverage = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_triple_coverage"),
        "AVBOIT Coverage",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertex = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_triple_generated_vertex"),
        "AVBOIT Occupancy Shared Generated Vertex",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(stateProbe.valid());
    ASSERT_TRUE(materialStream.valid());
    ASSERT_TRUE(coverage.valid());
    ASSERT_TRUE(generatedVertex.valid());

    const Graphics::GpuQueueRequest graphicsComputeQueue{
        QueueCapabilities(
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueueCapability::Compute
        ),
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest graphicsQueue{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint preScheduling;
    preScheduling.cost = Graphics::GpuTaskCostHint::Large;
    preScheduling.overlapPreferred = false;
    preScheduling.avoidQueueCrossing = true;
    preScheduling.forceSubmissionBoundary = false;
    preScheduling.allowPacketMerge = true;
    preScheduling.mergeWithPrevious = false;
    Graphics::GpuTaskSchedulingHint packetTailScheduling = preScheduling;
    packetTailScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    packetTailScheduling.mergeWithPrevious = true;
    packetTailScheduling.allowMergeAcrossConsumerFrontier = true;

    const Graphics::GpuTaskResourceUse preUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskResourceUse streamUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialStream,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse clearUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse dispatchUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialStream,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = generatedVertex,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse rasterUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialStream,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = generatedVertex,
            .range = {},
            .requiredState = Graphics::ResourceStates::VertexBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskResourceUse depthWarpUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };

    const Graphics::GpuTaskId pre = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_triple_pre"))
            .setMarkerLabel("AVBOIT Pre")
            .setQueue(graphicsQueue)
            .setScheduling(preScheduling)
            .setResourceUses(preUses, LengthOf(preUses))
    );
    ASSERT_TRUE(pre.valid());
    const Graphics::GpuTaskId stream = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_triple_stream"))
            .setMarkerLabel("AVBOIT Occupancy Material Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&pre, 1u)
            .setResourceUses(streamUses, LengthOf(streamUses))
    );
    ASSERT_TRUE(stream.valid());
    const Graphics::GpuTaskId clear = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_triple_clear"))
            .setMarkerLabel("AVBOIT Clear Coverage")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&stream, 1u)
            .setResourceUses(clearUses, LengthOf(clearUses))
    );
    ASSERT_TRUE(clear.valid());
    const Graphics::GpuTaskId dispatchA = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_triple_dispatch_a"))
            .setMarkerLabel("AVBOIT Occupancy Shared Compute Emulation A")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&clear, 1u)
            .setResourceUses(dispatchUses, LengthOf(dispatchUses))
    );
    ASSERT_TRUE(dispatchA.valid());
    const Graphics::GpuTaskId rasterADependencies[] = { dispatchA };
    const Graphics::GpuTaskId rasterA = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_triple_raster_a"))
            .setMarkerLabel("AVBOIT Occupancy Shared A")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(rasterADependencies, LengthOf(rasterADependencies))
            .setResourceUses(rasterUses, LengthOf(rasterUses))
    );
    ASSERT_TRUE(rasterA.valid());
    const Graphics::GpuTaskId dispatchBDependencies[] = { rasterA };
    const Graphics::GpuTaskId dispatchB = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_triple_dispatch_b"))
            .setMarkerLabel("AVBOIT Occupancy Shared Compute Emulation B")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(dispatchBDependencies, LengthOf(dispatchBDependencies))
            .setResourceUses(dispatchUses, LengthOf(dispatchUses))
    );
    ASSERT_TRUE(dispatchB.valid());
    const Graphics::GpuTaskId rasterBDependencies[] = { dispatchB };
    const Graphics::GpuTaskId rasterB = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_triple_raster_b"))
            .setMarkerLabel("AVBOIT Occupancy Shared B")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(rasterBDependencies, LengthOf(rasterBDependencies))
            .setResourceUses(rasterUses, LengthOf(rasterUses))
    );
    ASSERT_TRUE(rasterB.valid());
    const Graphics::GpuTaskId dispatchCDependencies[] = { rasterB };
    const Graphics::GpuTaskId dispatchC = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_triple_dispatch_c"))
            .setMarkerLabel("AVBOIT Occupancy Shared Compute Emulation C")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(dispatchCDependencies, LengthOf(dispatchCDependencies))
            .setResourceUses(dispatchUses, LengthOf(dispatchUses))
    );
    ASSERT_TRUE(dispatchC.valid());
    const Graphics::GpuTaskId rasterCDependencies[] = { dispatchC };
    const Graphics::GpuTaskId rasterC = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_triple_raster_c"))
            .setMarkerLabel("AVBOIT Occupancy Shared C")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(rasterCDependencies, LengthOf(rasterCDependencies))
            .setResourceUses(rasterUses, LengthOf(rasterUses))
    );
    ASSERT_TRUE(rasterC.valid());
    const Graphics::GpuTaskId depthWarpDependencies[] = { rasterC };
    const Graphics::GpuTaskId depthWarp = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_triple_depth_warp"))
            .setMarkerLabel("AVBOIT Depth Warp")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(depthWarpDependencies, LengthOf(depthWarpDependencies))
            .setResourceUses(depthWarpUses, LengthOf(depthWarpUses))
    );
    ASSERT_TRUE(depthWarp.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    EXPECT_TRUE(analysis.hasExplicitEdge(pre, stream));
    EXPECT_TRUE(analysis.hasExplicitEdge(stream, clear));
    EXPECT_TRUE(analysis.hasExplicitEdge(clear, dispatchA));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchA, rasterA));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterA, dispatchB));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchB, rasterB));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterB, dispatchC));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchC, rasterC));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterC, depthWarp));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        stream,
        dispatchA,
        materialStream,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        clear,
        rasterA,
        coverage,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        dispatchA,
        rasterA,
        generatedVertex,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        rasterA,
        dispatchB,
        generatedVertex,
        Graphics::GpuTaskHazardType::WriteAfterRead
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        dispatchB,
        rasterB,
        generatedVertex,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        rasterB,
        dispatchC,
        generatedVertex,
        Graphics::GpuTaskHazardType::WriteAfterRead
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        dispatchC,
        rasterC,
        generatedVertex,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        rasterC,
        depthWarp,
        coverage,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_EQ(analysis.topologicalOrder().size(), 10u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], pre);
    EXPECT_EQ(analysis.topologicalOrder()[1u], stream);
    EXPECT_EQ(analysis.topologicalOrder()[2u], clear);
    EXPECT_EQ(analysis.topologicalOrder()[3u], dispatchA);
    EXPECT_EQ(analysis.topologicalOrder()[4u], rasterA);
    EXPECT_EQ(analysis.topologicalOrder()[5u], dispatchB);
    EXPECT_EQ(analysis.topologicalOrder()[6u], rasterB);
    EXPECT_EQ(analysis.topologicalOrder()[7u], dispatchC);
    EXPECT_EQ(analysis.topologicalOrder()[8u], rasterC);
    EXPECT_EQ(analysis.topologicalOrder()[9u], depthWarp);

    for(const Graphics::GpuTaskId task : { pre, stream, clear, dispatchA, rasterA, dispatchB, rasterB, dispatchC, rasterC, depthWarp }){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue.id);
        EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Graphics);
    }
    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(pre);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetCount(), 1u);
    EXPECT_EQ(packet, compiledPlan.packetForTask(stream));
    EXPECT_EQ(packet, compiledPlan.packetForTask(clear));
    EXPECT_EQ(packet, compiledPlan.packetForTask(dispatchA));
    EXPECT_EQ(packet, compiledPlan.packetForTask(rasterA));
    EXPECT_EQ(packet, compiledPlan.packetForTask(dispatchB));
    EXPECT_EQ(packet, compiledPlan.packetForTask(rasterB));
    EXPECT_EQ(packet, compiledPlan.packetForTask(dispatchC));
    EXPECT_EQ(packet, compiledPlan.packetForTask(rasterC));
    EXPECT_EQ(packet, compiledPlan.packetForTask(depthWarp));
    EXPECT_TRUE(compiledPlan.tasksSharePacket(pre, depthWarp));
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    EXPECT_EQ(compiledPacket.queue, queue.id);
    EXPECT_EQ(compiledPacket.dependencyCount, 0u);
    ASSERT_EQ(compiledPacket.taskCount, 10u);
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], pre);
    EXPECT_EQ(packetTasks[1u], stream);
    EXPECT_EQ(packetTasks[2u], clear);
    EXPECT_EQ(packetTasks[3u], dispatchA);
    EXPECT_EQ(packetTasks[4u], rasterA);
    EXPECT_EQ(packetTasks[5u], dispatchB);
    EXPECT_EQ(packetTasks[6u], rasterB);
    EXPECT_EQ(packetTasks[7u], dispatchC);
    EXPECT_EQ(packetTasks[8u], rasterC);
    EXPECT_EQ(packetTasks[9u], depthWarp);

    const auto hasBufferTransition = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask && barriers && barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
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
    EXPECT_TRUE(hasBufferTransition(
        stream,
        materialStream,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        clear,
        coverage,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        dispatchA,
        materialStream,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        dispatchA,
        generatedVertex,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterA,
        generatedVertex,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::VertexBuffer
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterA,
        coverage,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        dispatchB,
        generatedVertex,
        Graphics::ResourceStates::VertexBuffer,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterB,
        generatedVertex,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::VertexBuffer
    ));
    EXPECT_TRUE(hasBufferTransition(
        dispatchC,
        generatedVertex,
        Graphics::ResourceStates::VertexBuffer,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterC,
        generatedVertex,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::VertexBuffer
    ));
}

TEST(GpuTaskGraph, KeepsUnsplitAvboitOccupancySharedOutputComputeEmulationQuintuplesInPrePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId stateProbe = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_quint_state_probe"),
        "AVBOIT Occupancy Shared Output State Probe",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId materialStream = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_quint_material_stream"),
        "AVBOIT Occupancy Shared Output Material Stream",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId coverage = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_quint_coverage"),
        "AVBOIT Coverage",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertex = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_quint_generated_vertex"),
        "AVBOIT Occupancy Shared Generated Vertex",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(stateProbe.valid());
    ASSERT_TRUE(materialStream.valid());
    ASSERT_TRUE(coverage.valid());
    ASSERT_TRUE(generatedVertex.valid());

    const Graphics::GpuQueueRequest graphicsComputeQueue{
        QueueCapabilities(
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueueCapability::Compute
        ),
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest graphicsQueue{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint preScheduling;
    preScheduling.cost = Graphics::GpuTaskCostHint::Large;
    preScheduling.overlapPreferred = false;
    preScheduling.avoidQueueCrossing = true;
    preScheduling.forceSubmissionBoundary = false;
    preScheduling.allowPacketMerge = true;
    preScheduling.mergeWithPrevious = false;
    Graphics::GpuTaskSchedulingHint packetTailScheduling = preScheduling;
    packetTailScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    packetTailScheduling.mergeWithPrevious = true;
    packetTailScheduling.allowMergeAcrossConsumerFrontier = true;

    const Graphics::GpuTaskResourceUse preUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskResourceUse streamUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialStream,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse clearUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse dispatchUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialStream,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = generatedVertex,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse rasterUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialStream,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = generatedVertex,
            .range = {},
            .requiredState = Graphics::ResourceStates::VertexBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskResourceUse depthWarpUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };

    const Graphics::GpuTaskId pre = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_quint_pre"))
            .setMarkerLabel("AVBOIT Pre")
            .setQueue(graphicsQueue)
            .setScheduling(preScheduling)
            .setResourceUses(preUses, LengthOf(preUses))
    );
    ASSERT_TRUE(pre.valid());
    const Graphics::GpuTaskId stream = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_quint_stream"))
            .setMarkerLabel("AVBOIT Occupancy Material Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&pre, 1u)
            .setResourceUses(streamUses, LengthOf(streamUses))
    );
    ASSERT_TRUE(stream.valid());
    const Graphics::GpuTaskId clear = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_quint_clear"))
            .setMarkerLabel("AVBOIT Clear Coverage")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&stream, 1u)
            .setResourceUses(clearUses, LengthOf(clearUses))
    );
    ASSERT_TRUE(clear.valid());

    const Name sharedPhaseIdentities[] = {
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_quint_dispatch_a"),
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_quint_raster_a"),
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_quint_dispatch_b"),
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_quint_raster_b"),
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_quint_dispatch_c"),
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_quint_raster_c"),
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_quint_dispatch_d"),
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_quint_raster_d"),
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_quint_dispatch_e"),
        Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_quint_raster_e"),
    };
    const AStringView sharedPhaseMarkers[] = {
        "AVBOIT Occupancy Shared Compute Emulation A",
        "AVBOIT Occupancy Shared A",
        "AVBOIT Occupancy Shared Compute Emulation B",
        "AVBOIT Occupancy Shared B",
        "AVBOIT Occupancy Shared Compute Emulation C",
        "AVBOIT Occupancy Shared C",
        "AVBOIT Occupancy Shared Compute Emulation D",
        "AVBOIT Occupancy Shared D",
        "AVBOIT Occupancy Shared Compute Emulation E",
        "AVBOIT Occupancy Shared E",
    };
    Graphics::GpuTaskId sharedPhaseTasks[LengthOf(sharedPhaseIdentities)] = {};
    for(usize phaseIndex = 0u; phaseIndex < LengthOf(sharedPhaseTasks); ++phaseIndex){
        const bool isRaster = phaseIndex % 2u != 0u;
        const Graphics::GpuTaskId dependency = phaseIndex == 0u ? clear : sharedPhaseTasks[phaseIndex - 1u];
        sharedPhaseTasks[phaseIndex] = graph.addTask(
            Graphics::GpuTaskDesc{}
                .setIdentity(sharedPhaseIdentities[phaseIndex])
                .setMarkerLabel(sharedPhaseMarkers[phaseIndex])
                .setQueue(isRaster ? graphicsQueue : graphicsComputeQueue)
                .setScheduling(packetTailScheduling)
                .setDependencies(&dependency, 1u)
                .setResourceUses(
                    isRaster ? rasterUses : dispatchUses,
                    isRaster ? LengthOf(rasterUses) : LengthOf(dispatchUses)
                )
        );
        ASSERT_TRUE(sharedPhaseTasks[phaseIndex].valid());
    }
    const Graphics::GpuTaskId dispatchA = sharedPhaseTasks[0u];
    const Graphics::GpuTaskId rasterA = sharedPhaseTasks[1u];
    const Graphics::GpuTaskId dispatchB = sharedPhaseTasks[2u];
    const Graphics::GpuTaskId rasterB = sharedPhaseTasks[3u];
    const Graphics::GpuTaskId dispatchC = sharedPhaseTasks[4u];
    const Graphics::GpuTaskId rasterC = sharedPhaseTasks[5u];
    const Graphics::GpuTaskId dispatchD = sharedPhaseTasks[6u];
    const Graphics::GpuTaskId rasterD = sharedPhaseTasks[7u];
    const Graphics::GpuTaskId dispatchE = sharedPhaseTasks[8u];
    const Graphics::GpuTaskId rasterE = sharedPhaseTasks[9u];
    const Graphics::GpuTaskId depthWarpDependencies[] = { rasterE };
    const Graphics::GpuTaskId depthWarp = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_shared_output_quint_depth_warp"))
            .setMarkerLabel("AVBOIT Depth Warp")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(depthWarpDependencies, LengthOf(depthWarpDependencies))
            .setResourceUses(depthWarpUses, LengthOf(depthWarpUses))
    );
    ASSERT_TRUE(depthWarp.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    EXPECT_TRUE(analysis.hasExplicitEdge(pre, stream));
    EXPECT_TRUE(analysis.hasExplicitEdge(stream, clear));
    EXPECT_TRUE(analysis.hasExplicitEdge(clear, dispatchA));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchA, rasterA));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterA, dispatchB));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchB, rasterB));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterB, dispatchC));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchC, rasterC));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterC, dispatchD));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchD, rasterD));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterD, dispatchE));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchE, rasterE));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterE, depthWarp));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        stream,
        dispatchA,
        materialStream,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        clear,
        rasterA,
        coverage,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        dispatchA,
        rasterA,
        generatedVertex,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        rasterA,
        dispatchB,
        generatedVertex,
        Graphics::GpuTaskHazardType::WriteAfterRead
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        dispatchB,
        rasterB,
        generatedVertex,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        rasterB,
        dispatchC,
        generatedVertex,
        Graphics::GpuTaskHazardType::WriteAfterRead
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        dispatchC,
        rasterC,
        generatedVertex,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        rasterC,
        dispatchD,
        generatedVertex,
        Graphics::GpuTaskHazardType::WriteAfterRead
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        dispatchD,
        rasterD,
        generatedVertex,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        rasterD,
        dispatchE,
        generatedVertex,
        Graphics::GpuTaskHazardType::WriteAfterRead
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        dispatchE,
        rasterE,
        generatedVertex,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        rasterE,
        depthWarp,
        coverage,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    const Graphics::GpuTaskId tasks[] = {
        pre,
        stream,
        clear,
        dispatchA,
        rasterA,
        dispatchB,
        rasterB,
        dispatchC,
        rasterC,
        dispatchD,
        rasterD,
        dispatchE,
        rasterE,
        depthWarp,
    };
    ASSERT_EQ(analysis.topologicalOrder().size(), LengthOf(tasks));
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex)
        EXPECT_EQ(analysis.topologicalOrder()[taskIndex], tasks[taskIndex]);
    for(const Graphics::GpuTaskId task : tasks){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue.id);
        EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Graphics);
    }
    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(pre);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetCount(), 1u);
    for(const Graphics::GpuTaskId task : tasks)
        EXPECT_EQ(packet, compiledPlan.packetForTask(task));
    EXPECT_TRUE(compiledPlan.tasksSharePacket(pre, depthWarp));
    for(usize taskIndex = 0u; taskIndex + 1u < LengthOf(tasks); ++taskIndex)
        EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(tasks[taskIndex], tasks[taskIndex + 1u]));
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    EXPECT_EQ(compiledPacket.queue, queue.id);
    EXPECT_EQ(compiledPacket.dependencyCount, 0u);
    ASSERT_EQ(compiledPacket.taskCount, LengthOf(tasks));
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex)
        EXPECT_EQ(packetTasks[taskIndex], tasks[taskIndex]);

    const auto hasBufferTransition = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask && barriers && barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
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
    const auto hasBufferUav = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask && barriers && barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferUav
                && barrier.resource == resource
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
                && barrier.sourceQueue == queue.id
                && barrier.destinationQueue == queue.id
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasBufferTransition(
        stream,
        materialStream,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        clear,
        coverage,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        dispatchA,
        materialStream,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        dispatchA,
        generatedVertex,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterA,
        generatedVertex,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::VertexBuffer
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterA,
        coverage,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        dispatchB,
        generatedVertex,
        Graphics::ResourceStates::VertexBuffer,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterB,
        generatedVertex,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::VertexBuffer
    ));
    EXPECT_TRUE(hasBufferTransition(
        dispatchC,
        generatedVertex,
        Graphics::ResourceStates::VertexBuffer,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterC,
        generatedVertex,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::VertexBuffer
    ));
    EXPECT_TRUE(hasBufferTransition(
        dispatchD,
        generatedVertex,
        Graphics::ResourceStates::VertexBuffer,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterD,
        generatedVertex,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::VertexBuffer
    ));
    EXPECT_TRUE(hasBufferTransition(
        dispatchE,
        generatedVertex,
        Graphics::ResourceStates::VertexBuffer,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterE,
        generatedVertex,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::VertexBuffer
    ));
    EXPECT_TRUE(hasBufferUav(depthWarp, coverage));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

