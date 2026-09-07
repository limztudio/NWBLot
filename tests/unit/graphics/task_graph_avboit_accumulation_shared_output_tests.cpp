// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_avboit_accumulation_shared_output_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, KeepsUnsplitAvboitAccumulationSharedOutputComputeEmulationTriplesInPrePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId stateProbe = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_triple_state_probe"),
        "AVBOIT Accumulation Shared Output State Probe",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId materialStream = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_triple_material_stream"),
        "AVBOIT Accumulation Shared Output Material Stream",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertex = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_triple_generated_vertex"),
        "AVBOIT Accumulation Shared Generated Vertex",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId accumColor = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_triple_color"),
        "AVBOIT Accumulation Shared Output Color",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId accumExtinction = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_triple_extinction"),
        "AVBOIT Accumulation Shared Output Extinction",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId deferredDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_triple_depth"),
        "AVBOIT Accumulation Shared Output Depth",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(stateProbe.valid());
    ASSERT_TRUE(materialStream.valid());
    ASSERT_TRUE(generatedVertex.valid());
    ASSERT_TRUE(accumColor.valid());
    ASSERT_TRUE(accumExtinction.valid());
    ASSERT_TRUE(deferredDepth.valid());

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
            .resource = accumColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = accumExtinction,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = deferredDepth,
            .range = {},
            .requiredState = Graphics::ResourceStates::DepthRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskResourceUse finalizerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = accumColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = accumExtinction,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = deferredDepth,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };

    const Graphics::GpuTaskId pre = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_triple_pre"))
            .setMarkerLabel("AVBOIT Pre")
            .setQueue(graphicsQueue)
            .setScheduling(preScheduling)
            .setResourceUses(preUses, LengthOf(preUses))
    );
    ASSERT_TRUE(pre.valid());
    const Graphics::GpuTaskId stream = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_triple_stream"))
            .setMarkerLabel("AVBOIT Accumulation Material Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&pre, 1u)
            .setResourceUses(streamUses, LengthOf(streamUses))
    );
    ASSERT_TRUE(stream.valid());
    const Graphics::GpuTaskId dispatchA = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_triple_dispatch_a"))
            .setMarkerLabel("AVBOIT Accumulation Compute Emulation A")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&stream, 1u)
            .setResourceUses(dispatchUses, LengthOf(dispatchUses))
    );
    ASSERT_TRUE(dispatchA.valid());
    const Graphics::GpuTaskId rasterADependencies[] = { dispatchA };
    const Graphics::GpuTaskId rasterA = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_triple_raster_a"))
            .setMarkerLabel("AVBOIT Accumulation A")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(rasterADependencies, LengthOf(rasterADependencies))
            .setResourceUses(rasterUses, LengthOf(rasterUses))
    );
    ASSERT_TRUE(rasterA.valid());
    const Graphics::GpuTaskId dispatchBDependencies[] = { rasterA };
    const Graphics::GpuTaskId dispatchB = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_triple_dispatch_b"))
            .setMarkerLabel("AVBOIT Accumulation Compute Emulation B")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(dispatchBDependencies, LengthOf(dispatchBDependencies))
            .setResourceUses(dispatchUses, LengthOf(dispatchUses))
    );
    ASSERT_TRUE(dispatchB.valid());
    const Graphics::GpuTaskId rasterBDependencies[] = { dispatchB };
    const Graphics::GpuTaskId rasterB = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_triple_raster_b"))
            .setMarkerLabel("AVBOIT Accumulation B")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(rasterBDependencies, LengthOf(rasterBDependencies))
            .setResourceUses(rasterUses, LengthOf(rasterUses))
    );
    ASSERT_TRUE(rasterB.valid());
    const Graphics::GpuTaskId dispatchCDependencies[] = { rasterB };
    const Graphics::GpuTaskId dispatchC = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_triple_dispatch_c"))
            .setMarkerLabel("AVBOIT Accumulation Compute Emulation C")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(dispatchCDependencies, LengthOf(dispatchCDependencies))
            .setResourceUses(dispatchUses, LengthOf(dispatchUses))
    );
    ASSERT_TRUE(dispatchC.valid());
    const Graphics::GpuTaskId rasterCDependencies[] = { dispatchC };
    const Graphics::GpuTaskId rasterC = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_triple_raster_c"))
            .setMarkerLabel("AVBOIT Accumulation C")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(rasterCDependencies, LengthOf(rasterCDependencies))
            .setResourceUses(rasterUses, LengthOf(rasterUses))
    );
    ASSERT_TRUE(rasterC.valid());
    const Graphics::GpuTaskId finalizerDependencies[] = { rasterC };
    const Graphics::GpuTaskId finalizer = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_triple_finalize"))
            .setMarkerLabel("AVBOIT Accumulation Finalize")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(finalizerDependencies, LengthOf(finalizerDependencies))
            .setResourceUses(finalizerUses, LengthOf(finalizerUses))
    );
    ASSERT_TRUE(finalizer.valid());

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
    EXPECT_TRUE(analysis.hasExplicitEdge(stream, dispatchA));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchA, rasterA));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterA, dispatchB));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchB, rasterB));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterB, dispatchC));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchC, rasterC));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterC, finalizer));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        stream,
        dispatchA,
        materialStream,
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
        finalizer,
        accumColor,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        rasterC,
        finalizer,
        accumExtinction,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_EQ(analysis.topologicalOrder().size(), 9u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], pre);
    EXPECT_EQ(analysis.topologicalOrder()[1u], stream);
    EXPECT_EQ(analysis.topologicalOrder()[2u], dispatchA);
    EXPECT_EQ(analysis.topologicalOrder()[3u], rasterA);
    EXPECT_EQ(analysis.topologicalOrder()[4u], dispatchB);
    EXPECT_EQ(analysis.topologicalOrder()[5u], rasterB);
    EXPECT_EQ(analysis.topologicalOrder()[6u], dispatchC);
    EXPECT_EQ(analysis.topologicalOrder()[7u], rasterC);
    EXPECT_EQ(analysis.topologicalOrder()[8u], finalizer);

    for(const Graphics::GpuTaskId task : { pre, stream, dispatchA, rasterA, dispatchB, rasterB, dispatchC, rasterC, finalizer }){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue.id);
        EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Graphics);
    }
    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(pre);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetCount(), 1u);
    EXPECT_EQ(packet, compiledPlan.packetForTask(stream));
    EXPECT_EQ(packet, compiledPlan.packetForTask(dispatchA));
    EXPECT_EQ(packet, compiledPlan.packetForTask(rasterA));
    EXPECT_EQ(packet, compiledPlan.packetForTask(dispatchB));
    EXPECT_EQ(packet, compiledPlan.packetForTask(rasterB));
    EXPECT_EQ(packet, compiledPlan.packetForTask(dispatchC));
    EXPECT_EQ(packet, compiledPlan.packetForTask(rasterC));
    EXPECT_EQ(packet, compiledPlan.packetForTask(finalizer));
    EXPECT_TRUE(compiledPlan.tasksSharePacket(pre, finalizer));
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    EXPECT_EQ(compiledPacket.queue, queue.id);
    EXPECT_EQ(compiledPacket.dependencyCount, 0u);
    ASSERT_EQ(compiledPacket.taskCount, 9u);
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], pre);
    EXPECT_EQ(packetTasks[1u], stream);
    EXPECT_EQ(packetTasks[2u], dispatchA);
    EXPECT_EQ(packetTasks[3u], rasterA);
    EXPECT_EQ(packetTasks[4u], dispatchB);
    EXPECT_EQ(packetTasks[5u], rasterB);
    EXPECT_EQ(packetTasks[6u], dispatchC);
    EXPECT_EQ(packetTasks[7u], rasterC);
    EXPECT_EQ(packetTasks[8u], finalizer);

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
    const auto hasTextureTransition = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask && barriers && barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
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
    EXPECT_TRUE(hasTextureTransition(
        rasterA,
        accumColor,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::RenderTarget
    ));
    EXPECT_TRUE(hasTextureTransition(
        rasterA,
        accumExtinction,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::RenderTarget
    ));
    EXPECT_TRUE(hasTextureTransition(
        rasterA,
        deferredDepth,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::DepthRead
    ));
    EXPECT_TRUE(hasTextureTransition(
        finalizer,
        accumColor,
        Graphics::ResourceStates::RenderTarget,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasTextureTransition(
        finalizer,
        accumExtinction,
        Graphics::ResourceStates::RenderTarget,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasTextureTransition(
        finalizer,
        deferredDepth,
        Graphics::ResourceStates::DepthRead,
        Graphics::ResourceStates::ShaderResource
    ));
}

TEST(GpuTaskGraph, KeepsUnsplitAvboitAccumulationSharedOutputComputeEmulationQuintuplesInPrePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId stateProbe = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_quint_state_probe"),
        "AVBOIT Accumulation Shared Output State Probe",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId materialStream = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_quint_material_stream"),
        "AVBOIT Accumulation Shared Output Material Stream",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertex = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_quint_generated_vertex"),
        "AVBOIT Accumulation Shared Generated Vertex",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId accumColor = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_quint_color"),
        "AVBOIT Accumulation Shared Output Color",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId accumExtinction = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_quint_extinction"),
        "AVBOIT Accumulation Shared Output Extinction",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId deferredDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_quint_depth"),
        "AVBOIT Accumulation Shared Output Depth",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(stateProbe.valid());
    ASSERT_TRUE(materialStream.valid());
    ASSERT_TRUE(generatedVertex.valid());
    ASSERT_TRUE(accumColor.valid());
    ASSERT_TRUE(accumExtinction.valid());
    ASSERT_TRUE(deferredDepth.valid());

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
            .resource = accumColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = accumExtinction,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = deferredDepth,
            .range = {},
            .requiredState = Graphics::ResourceStates::DepthRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskResourceUse finalizerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = accumColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = accumExtinction,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = deferredDepth,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };

    const Graphics::GpuTaskId pre = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_quint_pre"))
            .setMarkerLabel("AVBOIT Pre")
            .setQueue(graphicsQueue)
            .setScheduling(preScheduling)
            .setResourceUses(preUses, LengthOf(preUses))
    );
    ASSERT_TRUE(pre.valid());
    const Graphics::GpuTaskId stream = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_quint_stream"))
            .setMarkerLabel("AVBOIT Accumulation Material Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&pre, 1u)
            .setResourceUses(streamUses, LengthOf(streamUses))
    );
    ASSERT_TRUE(stream.valid());
    const Graphics::GpuTaskId dispatchA = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_quint_dispatch_a"))
            .setMarkerLabel("AVBOIT Accumulation Compute Emulation A")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&stream, 1u)
            .setResourceUses(dispatchUses, LengthOf(dispatchUses))
    );
    ASSERT_TRUE(dispatchA.valid());
    const Graphics::GpuTaskId rasterADependencies[] = { dispatchA };
    const Graphics::GpuTaskId rasterA = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_quint_raster_a"))
            .setMarkerLabel("AVBOIT Accumulation A")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(rasterADependencies, LengthOf(rasterADependencies))
            .setResourceUses(rasterUses, LengthOf(rasterUses))
    );
    ASSERT_TRUE(rasterA.valid());
    const Graphics::GpuTaskId dispatchBDependencies[] = { rasterA };
    const Graphics::GpuTaskId dispatchB = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_quint_dispatch_b"))
            .setMarkerLabel("AVBOIT Accumulation Compute Emulation B")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(dispatchBDependencies, LengthOf(dispatchBDependencies))
            .setResourceUses(dispatchUses, LengthOf(dispatchUses))
    );
    ASSERT_TRUE(dispatchB.valid());
    const Graphics::GpuTaskId rasterBDependencies[] = { dispatchB };
    const Graphics::GpuTaskId rasterB = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_quint_raster_b"))
            .setMarkerLabel("AVBOIT Accumulation B")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(rasterBDependencies, LengthOf(rasterBDependencies))
            .setResourceUses(rasterUses, LengthOf(rasterUses))
    );
    ASSERT_TRUE(rasterB.valid());
    const Graphics::GpuTaskId dispatchCDependencies[] = { rasterB };
    const Graphics::GpuTaskId dispatchC = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_quint_dispatch_c"))
            .setMarkerLabel("AVBOIT Accumulation Compute Emulation C")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(dispatchCDependencies, LengthOf(dispatchCDependencies))
            .setResourceUses(dispatchUses, LengthOf(dispatchUses))
    );
    ASSERT_TRUE(dispatchC.valid());
    const Graphics::GpuTaskId rasterCDependencies[] = { dispatchC };
    const Graphics::GpuTaskId rasterC = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_quint_raster_c"))
            .setMarkerLabel("AVBOIT Accumulation C")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(rasterCDependencies, LengthOf(rasterCDependencies))
            .setResourceUses(rasterUses, LengthOf(rasterUses))
    );
    ASSERT_TRUE(rasterC.valid());
    const Graphics::GpuTaskId dispatchDDependencies[] = { rasterC };
    const Graphics::GpuTaskId dispatchD = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_quint_dispatch_d"))
            .setMarkerLabel("AVBOIT Accumulation Compute Emulation D")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(dispatchDDependencies, LengthOf(dispatchDDependencies))
            .setResourceUses(dispatchUses, LengthOf(dispatchUses))
    );
    ASSERT_TRUE(dispatchD.valid());
    const Graphics::GpuTaskId rasterDDependencies[] = { dispatchD };
    const Graphics::GpuTaskId rasterD = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_quint_raster_d"))
            .setMarkerLabel("AVBOIT Accumulation D")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(rasterDDependencies, LengthOf(rasterDDependencies))
            .setResourceUses(rasterUses, LengthOf(rasterUses))
    );
    ASSERT_TRUE(rasterD.valid());
    const Graphics::GpuTaskId dispatchEDependencies[] = { rasterD };
    const Graphics::GpuTaskId dispatchE = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_quint_dispatch_e"))
            .setMarkerLabel("AVBOIT Accumulation Compute Emulation E")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(dispatchEDependencies, LengthOf(dispatchEDependencies))
            .setResourceUses(dispatchUses, LengthOf(dispatchUses))
    );
    ASSERT_TRUE(dispatchE.valid());
    const Graphics::GpuTaskId rasterEDependencies[] = { dispatchE };
    const Graphics::GpuTaskId rasterE = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_quint_raster_e"))
            .setMarkerLabel("AVBOIT Accumulation E")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(rasterEDependencies, LengthOf(rasterEDependencies))
            .setResourceUses(rasterUses, LengthOf(rasterUses))
    );
    ASSERT_TRUE(rasterE.valid());
    const Graphics::GpuTaskId finalizerDependencies[] = { rasterE };
    const Graphics::GpuTaskId finalizer = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_quint_finalize"))
            .setMarkerLabel("AVBOIT Accumulation Finalize")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(finalizerDependencies, LengthOf(finalizerDependencies))
            .setResourceUses(finalizerUses, LengthOf(finalizerUses))
    );
    ASSERT_TRUE(finalizer.valid());

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
    EXPECT_TRUE(analysis.hasExplicitEdge(stream, dispatchA));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchA, rasterA));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterA, dispatchB));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchB, rasterB));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterB, dispatchC));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchC, rasterC));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterC, dispatchD));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchD, rasterD));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterD, dispatchE));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchE, rasterE));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterE, finalizer));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        stream,
        dispatchA,
        materialStream,
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
        finalizer,
        accumColor,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        rasterE,
        finalizer,
        accumExtinction,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_EQ(analysis.topologicalOrder().size(), 13u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], pre);
    EXPECT_EQ(analysis.topologicalOrder()[1u], stream);
    EXPECT_EQ(analysis.topologicalOrder()[2u], dispatchA);
    EXPECT_EQ(analysis.topologicalOrder()[3u], rasterA);
    EXPECT_EQ(analysis.topologicalOrder()[4u], dispatchB);
    EXPECT_EQ(analysis.topologicalOrder()[5u], rasterB);
    EXPECT_EQ(analysis.topologicalOrder()[6u], dispatchC);
    EXPECT_EQ(analysis.topologicalOrder()[7u], rasterC);
    EXPECT_EQ(analysis.topologicalOrder()[8u], dispatchD);
    EXPECT_EQ(analysis.topologicalOrder()[9u], rasterD);
    EXPECT_EQ(analysis.topologicalOrder()[10u], dispatchE);
    EXPECT_EQ(analysis.topologicalOrder()[11u], rasterE);
    EXPECT_EQ(analysis.topologicalOrder()[12u], finalizer);

    for(const Graphics::GpuTaskId task : {
        pre, stream, dispatchA, rasterA, dispatchB, rasterB, dispatchC, rasterC, dispatchD, rasterD,
        dispatchE, rasterE, finalizer
    }){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue.id);
        EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Graphics);
    }
    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(pre);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetCount(), 1u);
    EXPECT_EQ(packet, compiledPlan.packetForTask(stream));
    EXPECT_EQ(packet, compiledPlan.packetForTask(dispatchA));
    EXPECT_EQ(packet, compiledPlan.packetForTask(rasterA));
    EXPECT_EQ(packet, compiledPlan.packetForTask(dispatchB));
    EXPECT_EQ(packet, compiledPlan.packetForTask(rasterB));
    EXPECT_EQ(packet, compiledPlan.packetForTask(dispatchC));
    EXPECT_EQ(packet, compiledPlan.packetForTask(rasterC));
    EXPECT_EQ(packet, compiledPlan.packetForTask(dispatchD));
    EXPECT_EQ(packet, compiledPlan.packetForTask(rasterD));
    EXPECT_EQ(packet, compiledPlan.packetForTask(dispatchE));
    EXPECT_EQ(packet, compiledPlan.packetForTask(rasterE));
    EXPECT_EQ(packet, compiledPlan.packetForTask(finalizer));
    EXPECT_TRUE(compiledPlan.tasksSharePacket(pre, finalizer));
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    EXPECT_EQ(compiledPacket.queue, queue.id);
    EXPECT_EQ(compiledPacket.dependencyCount, 0u);
    ASSERT_EQ(compiledPacket.taskCount, 13u);
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], pre);
    EXPECT_EQ(packetTasks[1u], stream);
    EXPECT_EQ(packetTasks[2u], dispatchA);
    EXPECT_EQ(packetTasks[3u], rasterA);
    EXPECT_EQ(packetTasks[4u], dispatchB);
    EXPECT_EQ(packetTasks[5u], rasterB);
    EXPECT_EQ(packetTasks[6u], dispatchC);
    EXPECT_EQ(packetTasks[7u], rasterC);
    EXPECT_EQ(packetTasks[8u], dispatchD);
    EXPECT_EQ(packetTasks[9u], rasterD);
    EXPECT_EQ(packetTasks[10u], dispatchE);
    EXPECT_EQ(packetTasks[11u], rasterE);
    EXPECT_EQ(packetTasks[12u], finalizer);

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
    const auto hasTextureTransition = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask && barriers && barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
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
    EXPECT_TRUE(hasTextureTransition(
        rasterA,
        accumColor,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::RenderTarget
    ));
    EXPECT_TRUE(hasTextureTransition(
        rasterA,
        accumExtinction,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::RenderTarget
    ));
    EXPECT_TRUE(hasTextureTransition(
        rasterA,
        deferredDepth,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::DepthRead
    ));
    EXPECT_TRUE(hasTextureTransition(
        finalizer,
        accumColor,
        Graphics::ResourceStates::RenderTarget,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasTextureTransition(
        finalizer,
        accumExtinction,
        Graphics::ResourceStates::RenderTarget,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasTextureTransition(
        finalizer,
        deferredDepth,
        Graphics::ResourceStates::DepthRead,
        Graphics::ResourceStates::ShaderResource
    ));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

