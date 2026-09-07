// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_avboit_accumulation_emulation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, KeepsUnsplitAvboitAccumulationAliasFreeRegularComputeEmulationInPrePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId stateProbe = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_state_probe"),
        "AVBOIT Accumulation State Probe",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId materialStream = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_material_stream"),
        "AVBOIT Accumulation Material Stream",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertexA = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_generated_vertex_a"),
        "AVBOIT Accumulation Generated Vertex A",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertexB = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_generated_vertex_b"),
        "AVBOIT Accumulation Generated Vertex B",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId accumColor = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_color"),
        "AVBOIT Accumulation Color",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId accumExtinction = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_extinction"),
        "AVBOIT Accumulation Extinction",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId deferredDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_depth"),
        "Deferred Depth",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(stateProbe.valid());
    ASSERT_TRUE(materialStream.valid());
    ASSERT_TRUE(generatedVertexA.valid());
    ASSERT_TRUE(generatedVertexB.valid());
    ASSERT_TRUE(accumColor.valid());
    ASSERT_TRUE(accumExtinction.valid());
    ASSERT_TRUE(deferredDepth.valid());

    const Graphics::GpuGraphResourceId generatedVertexOutputs[] = {
        generatedVertexA,
        generatedVertexB,
    };
    const Graphics::GpuGraphResourceSetId generatedVertexOutputSet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_generated_vertex_outputs"))
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
            .setIdentity(Name("tests/task_graph/unsplit_avboit_pre"))
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
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_material_upload"))
            .setMarkerLabel("AVBOIT Accumulation Material Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&pre, 1u)
            .setResourceUses(streamUses, LengthOf(streamUses))
    );
    ASSERT_TRUE(streamUpload.valid());

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
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_compute_emulation"))
            .setMarkerLabel("AVBOIT Accumulation Compute Emulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&streamUpload, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses))
            .setResourceSetUses(&producerGeneratedVertexUse, 1u)
    );
    ASSERT_TRUE(producer.valid());

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
    const Graphics::GpuTaskResourceSetUse rasterGeneratedVertexUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = Graphics::ResourceStates::VertexBuffer,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    const Graphics::GpuTaskId raster = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_raster"))
            .setMarkerLabel("AVBOIT Accumulation")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&producer, 1u)
            .setResourceUses(rasterUses, LengthOf(rasterUses))
            .setResourceSetUses(&rasterGeneratedVertexUse, 1u)
    );
    ASSERT_TRUE(raster.valid());

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
    const Graphics::GpuTaskId finalizer = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_finalize"))
            .setMarkerLabel("AVBOIT Accumulation Finalize")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&raster, 1u)
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


    EXPECT_TRUE(analysis.hasExplicitEdge(pre, streamUpload));
    EXPECT_TRUE(analysis.hasExplicitEdge(streamUpload, producer));
    EXPECT_TRUE(analysis.hasExplicitEdge(producer, raster));
    EXPECT_TRUE(analysis.hasExplicitEdge(raster, finalizer));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        streamUpload,
        producer,
        materialStream,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    for(const Graphics::GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(HasInferredHazard(
            analysis,
            producer,
            raster,
            output,
            Graphics::GpuTaskHazardType::ReadAfterWrite
        ));
    }
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        raster,
        finalizer,
        accumColor,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        raster,
        finalizer,
        accumExtinction,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_EQ(analysis.topologicalOrder().size(), 5u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], pre);
    EXPECT_EQ(analysis.topologicalOrder()[1u], streamUpload);
    EXPECT_EQ(analysis.topologicalOrder()[2u], producer);
    EXPECT_EQ(analysis.topologicalOrder()[3u], raster);
    EXPECT_EQ(analysis.topologicalOrder()[4u], finalizer);

    for(const Graphics::GpuTaskId task : { pre, streamUpload, producer, raster, finalizer }){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue.id);
        EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Graphics);
    }
    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(pre);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetCount(), 1u);
    EXPECT_EQ(packet, compiledPlan.packetForTask(streamUpload));
    EXPECT_EQ(packet, compiledPlan.packetForTask(producer));
    EXPECT_EQ(packet, compiledPlan.packetForTask(raster));
    EXPECT_EQ(packet, compiledPlan.packetForTask(finalizer));
    EXPECT_TRUE(compiledPlan.tasksSharePacket(pre, finalizer));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(pre, streamUpload));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(streamUpload, producer));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(producer, raster));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(raster, finalizer));
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    EXPECT_EQ(compiledPacket.queue, queue.id);
    EXPECT_EQ(compiledPacket.dependencyCount, 0u);
    ASSERT_EQ(compiledPacket.taskCount, 5u);
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], pre);
    EXPECT_EQ(packetTasks[1u], streamUpload);
    EXPECT_EQ(packetTasks[2u], producer);
    EXPECT_EQ(packetTasks[3u], raster);
    EXPECT_EQ(packetTasks[4u], finalizer);

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
        streamUpload,
        materialStream,
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
            raster,
            output,
            Graphics::ResourceStates::UnorderedAccess,
            Graphics::ResourceStates::VertexBuffer
        ));
    }
    EXPECT_TRUE(hasTextureTransition(
        raster,
        accumColor,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::RenderTarget
    ));
    EXPECT_TRUE(hasTextureTransition(
        raster,
        accumExtinction,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::RenderTarget
    ));
    EXPECT_TRUE(hasTextureTransition(
        raster,
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

TEST(GpuTaskGraph, KeepsUnsplitAvboitAccumulationSharedOutputComputeEmulationPairsInPrePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId stateProbe = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_state_probe"),
        "AVBOIT Accumulation Shared Output State Probe",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId materialStream = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_material_stream"),
        "AVBOIT Accumulation Shared Output Material Stream",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertex = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_generated_vertex"),
        "AVBOIT Accumulation Shared Generated Vertex",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId accumColor = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_color"),
        "AVBOIT Accumulation Shared Output Color",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId accumExtinction = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_extinction"),
        "AVBOIT Accumulation Shared Output Extinction",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId deferredDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_depth"),
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
            .setIdentity(Name("tests/task_graph/unsplit_avboit_shared_output_pre"))
            .setMarkerLabel("AVBOIT Pre")
            .setQueue(graphicsQueue)
            .setScheduling(preScheduling)
            .setResourceUses(preUses, LengthOf(preUses))
    );
    ASSERT_TRUE(pre.valid());
    const Graphics::GpuTaskId stream = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_stream"))
            .setMarkerLabel("AVBOIT Accumulation Material Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&pre, 1u)
            .setResourceUses(streamUses, LengthOf(streamUses))
    );
    ASSERT_TRUE(stream.valid());
    const Graphics::GpuTaskId dispatchA = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_dispatch_a"))
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
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_raster_a"))
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
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_dispatch_b"))
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
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_raster_b"))
            .setMarkerLabel("AVBOIT Accumulation B")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(rasterBDependencies, LengthOf(rasterBDependencies))
            .setResourceUses(rasterUses, LengthOf(rasterUses))
    );
    ASSERT_TRUE(rasterB.valid());
    const Graphics::GpuTaskId finalizerDependencies[] = { rasterB };
    const Graphics::GpuTaskId finalizer = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_shared_output_finalize"))
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
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterB, finalizer));
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
        finalizer,
        accumColor,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        rasterB,
        finalizer,
        accumExtinction,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_EQ(analysis.topologicalOrder().size(), 7u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], pre);
    EXPECT_EQ(analysis.topologicalOrder()[1u], stream);
    EXPECT_EQ(analysis.topologicalOrder()[2u], dispatchA);
    EXPECT_EQ(analysis.topologicalOrder()[3u], rasterA);
    EXPECT_EQ(analysis.topologicalOrder()[4u], dispatchB);
    EXPECT_EQ(analysis.topologicalOrder()[5u], rasterB);
    EXPECT_EQ(analysis.topologicalOrder()[6u], finalizer);

    for(const Graphics::GpuTaskId task : { pre, stream, dispatchA, rasterA, dispatchB, rasterB, finalizer }){
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
    EXPECT_EQ(packet, compiledPlan.packetForTask(finalizer));
    EXPECT_TRUE(compiledPlan.tasksSharePacket(pre, finalizer));
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    EXPECT_EQ(compiledPacket.queue, queue.id);
    EXPECT_EQ(compiledPacket.dependencyCount, 0u);
    ASSERT_EQ(compiledPacket.taskCount, 7u);
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], pre);
    EXPECT_EQ(packetTasks[1u], stream);
    EXPECT_EQ(packetTasks[2u], dispatchA);
    EXPECT_EQ(packetTasks[3u], rasterA);
    EXPECT_EQ(packetTasks[4u], dispatchB);
    EXPECT_EQ(packetTasks[5u], rasterB);
    EXPECT_EQ(packetTasks[6u], finalizer);

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

