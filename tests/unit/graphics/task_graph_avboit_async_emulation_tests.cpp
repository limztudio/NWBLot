// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_avboit_async_emulation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, OrdersAsyncAvboitExtinctionComputeEmulationPacketBetweenDepthWarpAndIntegration){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask concurrentSharing =
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    ;
    const Graphics::GpuGraphResourceId depthWarp = AddBufferMetadata(
        graph,
        Name("tests/task_graph/async_avboit_extinction_depth_warp"),
        "AVBOIT Depth Warp",
        Graphics::ResourceStates::Common,
        concurrentSharing
    );
    const Graphics::GpuGraphResourceId control = AddBufferMetadata(
        graph,
        Name("tests/task_graph/async_avboit_extinction_control"),
        "AVBOIT Control",
        Graphics::ResourceStates::Common,
        concurrentSharing
    );
    const Graphics::GpuGraphResourceId generatedVertex = AddBufferMetadata(
        graph,
        Name("tests/task_graph/async_avboit_extinction_generated_vertex"),
        "AVBOIT Extinction Generated Vertex",
        Graphics::ResourceStates::Common,
        concurrentSharing
    );
    const Graphics::GpuGraphResourceId extinction = AddBufferMetadata(
        graph,
        Name("tests/task_graph/async_avboit_extinction_output"),
        "AVBOIT Extinction",
        Graphics::ResourceStates::Common,
        concurrentSharing
    );
    const Graphics::GpuGraphResourceId extinctionOverflow = AddBufferMetadata(
        graph,
        Name("tests/task_graph/async_avboit_extinction_overflow"),
        "AVBOIT Extinction Overflow",
        Graphics::ResourceStates::Common,
        concurrentSharing
    );
    ASSERT_TRUE(depthWarp.valid());
    ASSERT_TRUE(control.valid());
    ASSERT_TRUE(generatedVertex.valid());
    ASSERT_TRUE(extinction.valid());
    ASSERT_TRUE(extinctionOverflow.valid());

    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    const Graphics::GpuQueueRequest graphicsComputeRequest{
        QueueCapabilities(
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueueCapability::Compute
        ),
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint computeScheduling;
    computeScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    computeScheduling.forceSubmissionBoundary = true;
    computeScheduling.allowPacketMerge = false;
    Graphics::GpuTaskSchedulingHint extinctionProducerScheduling;
    extinctionProducerScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    extinctionProducerScheduling.overlapPreferred = false;
    extinctionProducerScheduling.avoidQueueCrossing = true;
    extinctionProducerScheduling.forceSubmissionBoundary = false;
    extinctionProducerScheduling.allowPacketMerge = true;
    extinctionProducerScheduling.mergeWithPrevious = true;
    extinctionProducerScheduling.allowMergeAcrossConsumerFrontier = true;

    const Graphics::GpuTaskResourceUse depthWarpUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = depthWarp,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = control,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskId depthWarpTask = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/async_avboit_extinction_depth_warp"))
            .setMarkerLabel("AVBOIT Depth Warp")
            .setQueue(computeRequest)
            .setScheduling(computeScheduling)
            .setResourceUses(depthWarpUses, LengthOf(depthWarpUses))
    );
    ASSERT_TRUE(depthWarpTask.valid());

    const Graphics::GpuTaskResourceUse producerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = generatedVertex,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId producerTask = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/async_avboit_extinction_compute_emulation"))
            .setMarkerLabel("AVBOIT Extinction Compute Emulation")
            .setQueue(graphicsComputeRequest)
            .setScheduling(extinctionProducerScheduling)
            .setDependencies(&depthWarpTask, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses))
    );
    ASSERT_TRUE(producerTask.valid());

    const Graphics::GpuTaskResourceUse extinctionUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = depthWarp,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = control,
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
            .resource = extinction,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = extinctionOverflow,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskId extinctionTask = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/async_avboit_extinction_raster"))
            .setMarkerLabel("AVBOIT Extinction")
            .setQueue(graphicsRequest)
            .setScheduling(extinctionProducerScheduling)
            .setDependencies(&producerTask, 1u)
            .setResourceUses(extinctionUses, LengthOf(extinctionUses))
    );
    ASSERT_TRUE(extinctionTask.valid());

    const Graphics::GpuTaskResourceUse integrationUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = extinction,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = control,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = extinctionOverflow,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskId integrationTask = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/async_avboit_extinction_integration"))
            .setMarkerLabel("AVBOIT Integration")
            .setQueue(computeRequest)
            .setScheduling(computeScheduling)
            .setDependencies(&extinctionTask, 1u)
            .setResourceUses(integrationUses, LengthOf(integrationUses))
    );
    ASSERT_TRUE(integrationTask.valid());

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


    EXPECT_TRUE(analysis.hasExplicitEdge(depthWarpTask, producerTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(producerTask, extinctionTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(extinctionTask, integrationTask));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        depthWarpTask,
        extinctionTask,
        depthWarp,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        depthWarpTask,
        extinctionTask,
        control,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producerTask,
        extinctionTask,
        generatedVertex,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        extinctionTask,
        integrationTask,
        extinction,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        extinctionTask,
        integrationTask,
        extinctionOverflow,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_EQ(analysis.topologicalOrder().size(), 4u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], depthWarpTask);
    EXPECT_EQ(analysis.topologicalOrder()[1u], producerTask);
    EXPECT_EQ(analysis.topologicalOrder()[2u], extinctionTask);
    EXPECT_EQ(analysis.topologicalOrder()[3u], integrationTask);

    const auto expectAssignment = [&](const Graphics::GpuTaskId task, const Graphics::GpuPhysicalQueueId queue, const Graphics::CommandQueue::Enum queueClass){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue);
        EXPECT_EQ(assignment->queueClass, queueClass);
    };
    expectAssignment(depthWarpTask, queues[1u].id, Graphics::CommandQueue::Compute);
    expectAssignment(producerTask, queues[0u].id, Graphics::CommandQueue::Graphics);
    expectAssignment(extinctionTask, queues[0u].id, Graphics::CommandQueue::Graphics);
    expectAssignment(integrationTask, queues[1u].id, Graphics::CommandQueue::Compute);

    ASSERT_EQ(compiledPlan.packetCount(), 3u);
    const Graphics::GpuSubmissionPacketId depthWarpPacket = compiledPlan.packetForTask(depthWarpTask);
    const Graphics::GpuSubmissionPacketId extinctionPacket = compiledPlan.packetForTask(producerTask);
    const Graphics::GpuSubmissionPacketId integrationPacket = compiledPlan.packetForTask(integrationTask);
    ASSERT_TRUE(depthWarpPacket.valid());
    ASSERT_TRUE(extinctionPacket.valid());
    ASSERT_TRUE(integrationPacket.valid());
    EXPECT_EQ(extinctionPacket, compiledPlan.packetForTask(extinctionTask));
    EXPECT_NE(depthWarpPacket, extinctionPacket);
    EXPECT_NE(extinctionPacket, integrationPacket);
    EXPECT_EQ(compiledPlan.packetIdAt(0u), depthWarpPacket);
    EXPECT_EQ(compiledPlan.packetIdAt(1u), extinctionPacket);
    EXPECT_EQ(compiledPlan.packetIdAt(2u), integrationPacket);
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(depthWarpTask, producerTask));
    EXPECT_TRUE(compiledPlan.tasksSharePacket(producerTask, extinctionTask));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(extinctionTask, integrationTask));
    const Graphics::GpuSubmissionPacketRange packetRange = compiledPlan.packetRange(depthWarpPacket, integrationPacket);
    ASSERT_TRUE(packetRange.valid());
    EXPECT_EQ(packetRange.packetCount, 3u);
    const Graphics::GpuSubmissionPacket& depthWarpPacketPlan = *compiledPlan.packet(depthWarpPacket).plan;
    const Graphics::GpuSubmissionPacket& extinctionPacketPlan = *compiledPlan.packet(extinctionPacket).plan;
    const Graphics::GpuSubmissionPacket& integrationPacketPlan = *compiledPlan.packet(integrationPacket).plan;
    EXPECT_EQ(depthWarpPacketPlan.queue, queues[1u].id);
    EXPECT_EQ(extinctionPacketPlan.queue, queues[0u].id);
    EXPECT_EQ(integrationPacketPlan.queue, queues[1u].id);
    ASSERT_EQ(extinctionPacketPlan.taskCount, 2u);
    const Graphics::GpuTaskId* const extinctionPacketTasks = compiledPlan.packet(extinctionPacket).tasks;
    ASSERT_NE(extinctionPacketTasks, nullptr);
    EXPECT_EQ(extinctionPacketTasks[0u], producerTask);
    EXPECT_EQ(extinctionPacketTasks[1u], extinctionTask);
    ASSERT_EQ(extinctionPacketPlan.dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(extinctionPacket).dependencies[0u].producer, depthWarpPacket);
    // The raw Depth-Warp -> Integration resource edge remains diagnostic, while the scheduling reduction relies on
    // the already-required Depth-Warp -> Extinction -> Integration packet chain.
    EXPECT_NE(FindEdge(analysis, depthWarpTask, integrationTask), nullptr);
    ASSERT_EQ(integrationPacketPlan.dependencyCount, 1u);
    const Graphics::GpuPacketDependency* const integrationDependencies = compiledPlan.packet(integrationPacket).dependencies;
    ASSERT_NE(integrationDependencies, nullptr);
    EXPECT_EQ(integrationDependencies[0u].producer, extinctionPacket);

    const Graphics::GpuCompiledTask* const compiledDepthWarp = compiledPlan.findTask(depthWarpTask).plan;
    const Graphics::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producerTask).plan;
    const Graphics::GpuCompiledTask* const compiledExtinction = compiledPlan.findTask(extinctionTask).plan;
    const Graphics::GpuCompiledTask* const compiledIntegration = compiledPlan.findTask(integrationTask).plan;
    ASSERT_NE(compiledDepthWarp, nullptr);
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledExtinction, nullptr);
    ASSERT_NE(compiledIntegration, nullptr);
    EXPECT_EQ(compiledDepthWarp->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledProducer->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledExtinction->prologueStateSeedCount, 2u);
    ASSERT_EQ(compiledIntegration->prologueStateSeedCount, 3u);
    ASSERT_EQ(compiledDepthWarp->prologueBarrierCount, 2u);
    ASSERT_EQ(compiledProducer->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledExtinction->prologueBarrierCount, 5u);
    ASSERT_EQ(compiledIntegration->prologueBarrierCount, 2u);

    const auto hasStateSeed = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource, const Graphics::GpuSubmissionPacketId sourcePacket){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuPacketStateSeed* const seeds = compiledPlan.findTask(task).prologueStateSeeds;
        for(u32 seedIndex = 0u; compiledTask && seeds && seedIndex < compiledTask->prologueStateSeedCount; ++seedIndex){
            if(seeds[seedIndex].resource == resource && seeds[seedIndex].sourcePacket == sourcePacket)
                return true;
        }
        return false;
    };
    const auto hasBufferTransition = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after, const Graphics::GpuPhysicalQueueId sourceQueue, const Graphics::GpuPhysicalQueueId destinationQueue){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask && barriers && barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
                && barrier.sourceQueue == sourceQueue
                && barrier.destinationQueue == destinationQueue
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasStateSeed(extinctionTask, depthWarp, depthWarpPacket));
    EXPECT_TRUE(hasStateSeed(extinctionTask, control, depthWarpPacket));
    EXPECT_TRUE(hasStateSeed(integrationTask, extinction, extinctionPacket));
    EXPECT_TRUE(hasStateSeed(integrationTask, control, extinctionPacket));
    EXPECT_TRUE(hasStateSeed(integrationTask, extinctionOverflow, extinctionPacket));
    EXPECT_TRUE(hasBufferTransition(
        depthWarpTask,
        depthWarp,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess,
        queues[1u].id,
        queues[1u].id
    ));
    EXPECT_TRUE(hasBufferTransition(
        depthWarpTask,
        control,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess,
        queues[1u].id,
        queues[1u].id
    ));
    EXPECT_TRUE(hasBufferTransition(
        producerTask,
        generatedVertex,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess,
        queues[0u].id,
        queues[0u].id
    ));
    EXPECT_TRUE(hasBufferTransition(
        extinctionTask,
        depthWarp,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource,
        queues[1u].id,
        queues[0u].id
    ));
    EXPECT_TRUE(hasBufferTransition(
        extinctionTask,
        control,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource,
        queues[1u].id,
        queues[0u].id
    ));
    EXPECT_TRUE(hasBufferTransition(
        extinctionTask,
        generatedVertex,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::VertexBuffer,
        queues[0u].id,
        queues[0u].id
    ));
    EXPECT_TRUE(hasBufferTransition(
        extinctionTask,
        extinction,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess,
        queues[0u].id,
        queues[0u].id
    ));
    EXPECT_TRUE(hasBufferTransition(
        extinctionTask,
        extinctionOverflow,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess,
        queues[0u].id,
        queues[0u].id
    ));
    EXPECT_TRUE(hasBufferTransition(
        integrationTask,
        extinction,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource,
        queues[0u].id,
        queues[1u].id
    ));
    EXPECT_TRUE(hasBufferTransition(
        integrationTask,
        extinctionOverflow,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource,
        queues[0u].id,
        queues[1u].id
    ));
}

TEST(GpuTaskGraph, OrdersAsyncAvboitAccumulationComputeEmulationPacketBetweenIntegrationAndComposite){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask concurrentSharing =
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    ;
    const Graphics::GpuGraphResourceId integrationOutput = AddBufferMetadata(
        graph,
        Name("tests/task_graph/async_avboit_accumulation_integration_output"),
        "AVBOIT Integration Output",
        Graphics::ResourceStates::Common,
        concurrentSharing
    );
    const Graphics::GpuGraphResourceId materialStream = AddBufferMetadata(
        graph,
        Name("tests/task_graph/async_avboit_accumulation_material_stream"),
        "AVBOIT Accumulation Material Stream",
        Graphics::ResourceStates::Common,
        concurrentSharing
    );
    const Graphics::GpuGraphResourceId generatedVertexA = AddBufferMetadata(
        graph,
        Name("tests/task_graph/async_avboit_accumulation_generated_vertex_a"),
        "AVBOIT Accumulation Generated Vertex A",
        Graphics::ResourceStates::Common,
        concurrentSharing
    );
    const Graphics::GpuGraphResourceId generatedVertexB = AddBufferMetadata(
        graph,
        Name("tests/task_graph/async_avboit_accumulation_generated_vertex_b"),
        "AVBOIT Accumulation Generated Vertex B",
        Graphics::ResourceStates::Common,
        concurrentSharing
    );
    const Graphics::GpuGraphResourceId accumColor = AddTextureMetadata(
        graph,
        Name("tests/task_graph/async_avboit_accumulation_color"),
        "AVBOIT Accumulation Color",
        Graphics::ResourceStates::Common,
        concurrentSharing
    );
    const Graphics::GpuGraphResourceId accumExtinction = AddTextureMetadata(
        graph,
        Name("tests/task_graph/async_avboit_accumulation_extinction"),
        "AVBOIT Accumulation Extinction",
        Graphics::ResourceStates::Common,
        concurrentSharing
    );
    const Graphics::GpuGraphResourceId deferredDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/async_avboit_accumulation_deferred_depth"),
        "Deferred Depth",
        Graphics::ResourceStates::Common,
        concurrentSharing
    );
    ASSERT_TRUE(integrationOutput.valid());
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
            .setIdentity(Name("tests/task_graph/async_avboit_accumulation_generated_vertex_outputs"))
            .setMarkerLabel("AVBOIT Accumulation Generated Vertex Outputs")
            .setMembers(generatedVertexOutputs, LengthOf(generatedVertexOutputs))
    );
    ASSERT_TRUE(generatedVertexOutputSet.valid());

    const Graphics::GpuQueueRequest computeQueue{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
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
    const Graphics::GpuQueueRequest graphicsQueue{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint computeScheduling;
    computeScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    computeScheduling.forceSubmissionBoundary = true;
    computeScheduling.allowPacketMerge = false;
    Graphics::GpuTaskSchedulingHint graphicsScheduling;
    graphicsScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    graphicsScheduling.overlapPreferred = false;
    graphicsScheduling.avoidQueueCrossing = true;
    graphicsScheduling.forceSubmissionBoundary = false;
    graphicsScheduling.allowPacketMerge = true;
    graphicsScheduling.mergeWithPrevious = true;
    graphicsScheduling.allowMergeAcrossConsumerFrontier = true;

    const Graphics::GpuTaskResourceUse integrationUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = integrationOutput,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId integration = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/async_avboit_accumulation_integration"))
            .setMarkerLabel("AVBOIT Integration")
            .setQueue(computeQueue)
            .setScheduling(computeScheduling)
            .setResourceUses(integrationUses, LengthOf(integrationUses))
    );
    ASSERT_TRUE(integration.valid());

    const Graphics::GpuTaskResourceUse streamUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = materialStream,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId streamUpload = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/async_avboit_accumulation_material_upload"))
            .setMarkerLabel("AVBOIT Accumulation Material Upload")
            .setQueue(graphicsQueue)
            .setScheduling(graphicsScheduling)
            .setDependencies(&integration, 1u)
            .setResourceUses(streamUploadUses, LengthOf(streamUploadUses))
    );
    ASSERT_TRUE(streamUpload.valid());

    const Graphics::GpuTaskResourceUse producerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = integrationOutput,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
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
            .setIdentity(Name("tests/task_graph/async_avboit_accumulation_compute_emulation"))
            .setMarkerLabel("AVBOIT Accumulation Compute Emulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(graphicsScheduling)
            .setDependencies(&streamUpload, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses))
            .setResourceSetUses(&producerGeneratedVertexUse, 1u)
    );
    ASSERT_TRUE(producer.valid());

    const Graphics::GpuTaskResourceUse rasterUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = integrationOutput,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
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
            .setIdentity(Name("tests/task_graph/async_avboit_accumulation_raster"))
            .setMarkerLabel("AVBOIT Accumulation")
            .setQueue(graphicsQueue)
            .setScheduling(graphicsScheduling)
            .setDependencies(&producer, 1u)
            .setResourceUses(rasterUses, LengthOf(rasterUses))
            .setResourceSetUses(&rasterGeneratedVertexUse, 1u)
    );
    ASSERT_TRUE(raster.valid());

    const Graphics::GpuTaskResourceUse finalizeUses[] = {
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
            .setIdentity(Name("tests/task_graph/async_avboit_accumulation_finalize"))
            .setMarkerLabel("AVBOIT Accumulation Finalize")
            .setQueue(graphicsQueue)
            .setScheduling(graphicsScheduling)
            .setDependencies(&raster, 1u)
            .setResourceUses(finalizeUses, LengthOf(finalizeUses))
    );
    ASSERT_TRUE(finalizer.valid());

    const Graphics::GpuTaskResourceUse compositeUses[] = {
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
    };
    const Graphics::GpuTaskId composite = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/async_avboit_accumulation_composite"))
            .setMarkerLabel("Deferred Composite")
            .setQueue(computeQueue)
            .setScheduling(computeScheduling)
            .setDependencies(&finalizer, 1u)
            .setResourceUses(compositeUses, LengthOf(compositeUses))
    );
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


    EXPECT_TRUE(analysis.hasExplicitEdge(integration, streamUpload));
    EXPECT_TRUE(analysis.hasExplicitEdge(streamUpload, producer));
    EXPECT_TRUE(analysis.hasExplicitEdge(producer, raster));
    EXPECT_TRUE(analysis.hasExplicitEdge(raster, finalizer));
    EXPECT_TRUE(analysis.hasExplicitEdge(finalizer, composite));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        integration,
        producer,
        integrationOutput,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
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
    ASSERT_EQ(analysis.topologicalOrder().size(), 6u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], integration);
    EXPECT_EQ(analysis.topologicalOrder()[1u], streamUpload);
    EXPECT_EQ(analysis.topologicalOrder()[2u], producer);
    EXPECT_EQ(analysis.topologicalOrder()[3u], raster);
    EXPECT_EQ(analysis.topologicalOrder()[4u], finalizer);
    EXPECT_EQ(analysis.topologicalOrder()[5u], composite);

    const auto expectAssignment = [&](const Graphics::GpuTaskId task, const Graphics::GpuPhysicalQueueId queue, const Graphics::CommandQueue::Enum queueClass){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue);
        EXPECT_EQ(assignment->queueClass, queueClass);
    };
    expectAssignment(integration, queues[1u].id, Graphics::CommandQueue::Compute);
    expectAssignment(streamUpload, queues[0u].id, Graphics::CommandQueue::Graphics);
    expectAssignment(producer, queues[0u].id, Graphics::CommandQueue::Graphics);
    expectAssignment(raster, queues[0u].id, Graphics::CommandQueue::Graphics);
    expectAssignment(finalizer, queues[0u].id, Graphics::CommandQueue::Graphics);
    expectAssignment(composite, queues[1u].id, Graphics::CommandQueue::Compute);

    ASSERT_EQ(compiledPlan.packetCount(), 3u);
    const Graphics::GpuSubmissionPacketId integrationPacket = compiledPlan.packetForTask(integration);
    const Graphics::GpuSubmissionPacketId accumulationPacket = compiledPlan.packetForTask(producer);
    const Graphics::GpuSubmissionPacketId compositePacket = compiledPlan.packetForTask(composite);
    ASSERT_TRUE(integrationPacket.valid());
    ASSERT_TRUE(accumulationPacket.valid());
    ASSERT_TRUE(compositePacket.valid());
    EXPECT_EQ(accumulationPacket, compiledPlan.packetForTask(streamUpload));
    EXPECT_EQ(accumulationPacket, compiledPlan.packetForTask(raster));
    EXPECT_EQ(accumulationPacket, compiledPlan.packetForTask(finalizer));
    EXPECT_NE(integrationPacket, accumulationPacket);
    EXPECT_NE(accumulationPacket, compositePacket);
    EXPECT_EQ(compiledPlan.packetIdAt(0u), integrationPacket);
    EXPECT_EQ(compiledPlan.packetIdAt(1u), accumulationPacket);
    EXPECT_EQ(compiledPlan.packetIdAt(2u), compositePacket);
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(integration, streamUpload));
    EXPECT_TRUE(compiledPlan.tasksSharePacket(streamUpload, producer));
    EXPECT_TRUE(compiledPlan.tasksSharePacket(producer, raster));
    EXPECT_TRUE(compiledPlan.tasksSharePacket(raster, finalizer));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(finalizer, composite));

    const Graphics::GpuSubmissionPacket& accumulationPacketPlan = *compiledPlan.packet(accumulationPacket).plan;
    const Graphics::GpuSubmissionPacket& compositePacketPlan = *compiledPlan.packet(compositePacket).plan;
    EXPECT_EQ(accumulationPacketPlan.queue, queues[0u].id);
    EXPECT_EQ(compositePacketPlan.queue, queues[1u].id);
    ASSERT_EQ(accumulationPacketPlan.taskCount, 4u);
    const Graphics::GpuTaskId* const accumulationPacketTasks = compiledPlan.packet(accumulationPacket).tasks;
    ASSERT_NE(accumulationPacketTasks, nullptr);
    EXPECT_EQ(accumulationPacketTasks[0u], streamUpload);
    EXPECT_EQ(accumulationPacketTasks[1u], producer);
    EXPECT_EQ(accumulationPacketTasks[2u], raster);
    EXPECT_EQ(accumulationPacketTasks[3u], finalizer);
    ASSERT_EQ(accumulationPacketPlan.dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(accumulationPacket).dependencies[0u].producer, integrationPacket);
    ASSERT_EQ(compositePacketPlan.dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(compositePacket).dependencies[0u].producer, accumulationPacket);

    const auto hasStateSeed = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource, const Graphics::GpuSubmissionPacketId sourcePacket){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuPacketStateSeed* const seeds = compiledPlan.findTask(task).prologueStateSeeds;
        for(u32 seedIndex = 0u; compiledTask && seeds && seedIndex < compiledTask->prologueStateSeedCount; ++seedIndex){
            if(seeds[seedIndex].resource == resource && seeds[seedIndex].sourcePacket == sourcePacket)
                return true;
        }
        return false;
    };
    const auto hasBufferTransition = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after, const Graphics::GpuPhysicalQueueId sourceQueue, const Graphics::GpuPhysicalQueueId destinationQueue){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask && barriers && barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
                && barrier.sourceQueue == sourceQueue
                && barrier.destinationQueue == destinationQueue
            )
                return true;
        }
        return false;
    };
    const auto hasTextureTransition = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after, const Graphics::GpuPhysicalQueueId sourceQueue, const Graphics::GpuPhysicalQueueId destinationQueue){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask && barriers && barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
                && barrier.sourceQueue == sourceQueue
                && barrier.destinationQueue == destinationQueue
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasStateSeed(producer, integrationOutput, integrationPacket));
    EXPECT_TRUE(hasStateSeed(composite, accumColor, accumulationPacket));
    EXPECT_TRUE(hasStateSeed(composite, accumExtinction, accumulationPacket));
    EXPECT_TRUE(hasBufferTransition(
        integration,
        integrationOutput,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess,
        queues[1u].id,
        queues[1u].id
    ));
    EXPECT_TRUE(hasBufferTransition(
        streamUpload,
        materialStream,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest,
        queues[0u].id,
        queues[0u].id
    ));
    EXPECT_TRUE(hasBufferTransition(
        producer,
        integrationOutput,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource,
        queues[1u].id,
        queues[0u].id
    ));
    EXPECT_TRUE(hasBufferTransition(
        producer,
        materialStream,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ShaderResource,
        queues[0u].id,
        queues[0u].id
    ));
    for(const Graphics::GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(hasBufferTransition(
            producer,
            output,
            Graphics::ResourceStates::Common,
            Graphics::ResourceStates::UnorderedAccess,
            queues[0u].id,
            queues[0u].id
        ));
        EXPECT_TRUE(hasBufferTransition(
            raster,
            output,
            Graphics::ResourceStates::UnorderedAccess,
            Graphics::ResourceStates::VertexBuffer,
            queues[0u].id,
            queues[0u].id
        ));
    }
    EXPECT_TRUE(hasTextureTransition(
        raster,
        accumColor,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::RenderTarget,
        queues[0u].id,
        queues[0u].id
    ));
    EXPECT_TRUE(hasTextureTransition(
        raster,
        accumExtinction,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::RenderTarget,
        queues[0u].id,
        queues[0u].id
    ));
    EXPECT_TRUE(hasTextureTransition(
        raster,
        deferredDepth,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::DepthRead,
        queues[0u].id,
        queues[0u].id
    ));
    EXPECT_TRUE(hasTextureTransition(
        finalizer,
        accumColor,
        Graphics::ResourceStates::RenderTarget,
        Graphics::ResourceStates::ShaderResource,
        queues[0u].id,
        queues[0u].id
    ));
    EXPECT_TRUE(hasTextureTransition(
        finalizer,
        accumExtinction,
        Graphics::ResourceStates::RenderTarget,
        Graphics::ResourceStates::ShaderResource,
        queues[0u].id,
        queues[0u].id
    ));
    EXPECT_TRUE(hasTextureTransition(
        finalizer,
        deferredDepth,
        Graphics::ResourceStates::DepthRead,
        Graphics::ResourceStates::ShaderResource,
        queues[0u].id,
        queues[0u].id
    ));
}

TEST(GpuTaskGraph, OrdersAsyncAvboitOccupancyComputeEmulationPacketBeforeDepthWarp){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask concurrentSharing =
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    ;
    const Graphics::GpuGraphResourceId coverage = AddBufferMetadata(
        graph,
        Name("tests/task_graph/async_avboit_occupancy_coverage"),
        "AVBOIT Coverage",
        Graphics::ResourceStates::Common,
        concurrentSharing
    );
    const Graphics::GpuGraphResourceId generatedVertex = AddBufferMetadata(
        graph,
        Name("tests/task_graph/async_avboit_occupancy_generated_vertex"),
        "AVBOIT Occupancy Generated Vertex",
        Graphics::ResourceStates::Common,
        concurrentSharing
    );
    const Graphics::GpuGraphResourceId depthWarp = AddBufferMetadata(
        graph,
        Name("tests/task_graph/async_avboit_occupancy_depth_warp"),
        "AVBOIT Depth Warp",
        Graphics::ResourceStates::Common,
        concurrentSharing
    );
    ASSERT_TRUE(coverage.valid());
    ASSERT_TRUE(generatedVertex.valid());
    ASSERT_TRUE(depthWarp.valid());

    const Graphics::GpuQueueRequest graphicsComputeRequest{
        QueueCapabilities(
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueueCapability::Compute
        ),
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
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
    Graphics::GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    clearScheduling.forceSubmissionBoundary = false;
    clearScheduling.allowPacketMerge = true;
    clearScheduling.mergeWithPrevious = false;
    Graphics::GpuTaskSchedulingHint occupancyScheduling;
    occupancyScheduling.cost = Graphics::GpuTaskCostHint::Large;
    occupancyScheduling.overlapPreferred = false;
    occupancyScheduling.avoidQueueCrossing = true;
    occupancyScheduling.forceSubmissionBoundary = false;
    occupancyScheduling.allowPacketMerge = true;
    occupancyScheduling.mergeWithPrevious = true;
    // Depth Warp is a later dedicated-Compute consumer. Keep both sides of the generated-vertex handoff in Pre.
    occupancyScheduling.allowMergeAcrossConsumerFrontier = true;
    Graphics::GpuTaskSchedulingHint depthWarpScheduling;
    depthWarpScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    depthWarpScheduling.forceSubmissionBoundary = true;
    depthWarpScheduling.allowPacketMerge = false;

    const Graphics::GpuTaskResourceUse clearUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId clearTask = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/async_avboit_occupancy_clear"))
            .setMarkerLabel("AVBOIT Clear Coverage")
            .setQueue(graphicsRequest)
            .setScheduling(clearScheduling)
            .setResourceUses(clearUses, LengthOf(clearUses))
    );
    ASSERT_TRUE(clearTask.valid());

    const Graphics::GpuTaskResourceUse producerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = generatedVertex,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId producerTask = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/async_avboit_occupancy_compute_emulation"))
            .setMarkerLabel("AVBOIT Occupancy Compute Emulation")
            .setQueue(graphicsComputeRequest)
            .setScheduling(occupancyScheduling)
            .setDependencies(&clearTask, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses))
    );
    ASSERT_TRUE(producerTask.valid());

    const Graphics::GpuTaskResourceUse occupancyUses[] = {
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
    const Graphics::GpuTaskId occupancyTask = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/async_avboit_occupancy_raster"))
            .setMarkerLabel("AVBOIT Occupancy")
            .setQueue(graphicsComputeRequest)
            .setScheduling(occupancyScheduling)
            .setDependencies(&producerTask, 1u)
            .setResourceUses(occupancyUses, LengthOf(occupancyUses))
    );
    ASSERT_TRUE(occupancyTask.valid());

    const Graphics::GpuTaskResourceUse depthWarpUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = depthWarp,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId depthWarpTask = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/async_avboit_occupancy_depth_warp"))
            .setMarkerLabel("AVBOIT Depth Warp")
            .setQueue(computeRequest)
            .setScheduling(depthWarpScheduling)
            .setDependencies(&occupancyTask, 1u)
            .setResourceUses(depthWarpUses, LengthOf(depthWarpUses))
    );
    ASSERT_TRUE(depthWarpTask.valid());

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


    EXPECT_TRUE(analysis.hasExplicitEdge(clearTask, producerTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(producerTask, occupancyTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(occupancyTask, depthWarpTask));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        clearTask,
        occupancyTask,
        coverage,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producerTask,
        occupancyTask,
        generatedVertex,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        occupancyTask,
        depthWarpTask,
        coverage,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_EQ(analysis.topologicalOrder().size(), 4u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], clearTask);
    EXPECT_EQ(analysis.topologicalOrder()[1u], producerTask);
    EXPECT_EQ(analysis.topologicalOrder()[2u], occupancyTask);
    EXPECT_EQ(analysis.topologicalOrder()[3u], depthWarpTask);

    const auto expectAssignment = [&](const Graphics::GpuTaskId task, const Graphics::GpuPhysicalQueueId queue, const Graphics::CommandQueue::Enum queueClass){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue);
        EXPECT_EQ(assignment->queueClass, queueClass);
    };
    expectAssignment(clearTask, queues[0u].id, Graphics::CommandQueue::Graphics);
    expectAssignment(producerTask, queues[0u].id, Graphics::CommandQueue::Graphics);
    expectAssignment(occupancyTask, queues[0u].id, Graphics::CommandQueue::Graphics);
    expectAssignment(depthWarpTask, queues[1u].id, Graphics::CommandQueue::Compute);

    ASSERT_EQ(compiledPlan.packetCount(), 2u);
    const Graphics::GpuSubmissionPacketId prePacket = compiledPlan.packetForTask(clearTask);
    const Graphics::GpuSubmissionPacketId depthWarpPacket = compiledPlan.packetForTask(depthWarpTask);
    ASSERT_TRUE(prePacket.valid());
    ASSERT_TRUE(depthWarpPacket.valid());
    EXPECT_EQ(prePacket, compiledPlan.packetForTask(producerTask));
    EXPECT_EQ(prePacket, compiledPlan.packetForTask(occupancyTask));
    EXPECT_NE(prePacket, depthWarpPacket);
    EXPECT_EQ(compiledPlan.packetIdAt(0u), prePacket);
    EXPECT_EQ(compiledPlan.packetIdAt(1u), depthWarpPacket);
    EXPECT_TRUE(compiledPlan.tasksSharePacket(producerTask, occupancyTask));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(clearTask, producerTask));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(occupancyTask, depthWarpTask));
    const Graphics::GpuSubmissionPacket& prePacketPlan = *compiledPlan.packet(prePacket).plan;
    const Graphics::GpuSubmissionPacket& depthWarpPacketPlan = *compiledPlan.packet(depthWarpPacket).plan;
    EXPECT_EQ(prePacketPlan.queue, queues[0u].id);
    EXPECT_EQ(depthWarpPacketPlan.queue, queues[1u].id);
    ASSERT_EQ(prePacketPlan.taskCount, 3u);
    const Graphics::GpuTaskId* const prePacketTasks = compiledPlan.packet(prePacket).tasks;
    ASSERT_NE(prePacketTasks, nullptr);
    EXPECT_EQ(prePacketTasks[0u], clearTask);
    EXPECT_EQ(prePacketTasks[1u], producerTask);
    EXPECT_EQ(prePacketTasks[2u], occupancyTask);
    ASSERT_EQ(depthWarpPacketPlan.dependencyCount, 1u);
    ASSERT_NE(compiledPlan.packet(depthWarpPacket).dependencies, nullptr);
    EXPECT_EQ(compiledPlan.packet(depthWarpPacket).dependencies[0u].producer, prePacket);

    const Graphics::GpuCompiledTask* const compiledDepthWarp = compiledPlan.findTask(depthWarpTask).plan;
    ASSERT_NE(compiledDepthWarp, nullptr);
    ASSERT_EQ(compiledDepthWarp->prologueStateSeedCount, 1u);
    const Graphics::GpuPacketStateSeed* const depthWarpSeeds = compiledPlan.findTask(depthWarpTask).prologueStateSeeds;
    ASSERT_NE(depthWarpSeeds, nullptr);
    EXPECT_EQ(depthWarpSeeds[0u].resource, coverage);
    EXPECT_EQ(depthWarpSeeds[0u].sourcePacket, prePacket);

    const auto hasBufferTransition = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after, const Graphics::GpuPhysicalQueueId sourceQueue, const Graphics::GpuPhysicalQueueId destinationQueue){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask && barriers && barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
                && barrier.sourceQueue == sourceQueue
                && barrier.destinationQueue == destinationQueue
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasBufferTransition(
        clearTask,
        coverage,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest,
        queues[0u].id,
        queues[0u].id
    ));
    EXPECT_TRUE(hasBufferTransition(
        producerTask,
        generatedVertex,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess,
        queues[0u].id,
        queues[0u].id
    ));
    EXPECT_TRUE(hasBufferTransition(
        occupancyTask,
        generatedVertex,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::VertexBuffer,
        queues[0u].id,
        queues[0u].id
    ));
    EXPECT_TRUE(hasBufferTransition(
        occupancyTask,
        coverage,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess,
        queues[0u].id,
        queues[0u].id
    ));
    EXPECT_TRUE(hasBufferTransition(
        depthWarpTask,
        coverage,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource,
        queues[0u].id,
        queues[1u].id
    ));
    EXPECT_TRUE(hasBufferTransition(
        depthWarpTask,
        depthWarp,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess,
        queues[1u].id,
        queues[1u].id
    ));
}

TEST(GpuTaskGraph, KeepsAvboitUploadSequenceOutOfHardwareCausticsPacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuTaskSchedulingHint hardwareCausticsScheduling;
    hardwareCausticsScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc hardwareCausticsDesc;
    hardwareCausticsDesc
        .setIdentity(Name("tests/task_graph/hardware_caustics_tail"))
        .setMarkerLabel("Hardware Caustics Tail")
        .setScheduling(hardwareCausticsScheduling)
    ;
    const Graphics::GpuTaskId hardwareCaustics = graph.addTask(hardwareCausticsDesc);
    ASSERT_TRUE(hardwareCaustics.valid());

    // The first prepared AVBOIT upload begins a separately accepted/timed semantic sequence. It must remain
    // mergeable itself so the rest of the upload -> Pre chain can share that new packet.
    Graphics::GpuTaskSchedulingHint firstAvboitUploadScheduling;
    firstAvboitUploadScheduling.allowPacketMerge = true;
    firstAvboitUploadScheduling.mergeWithPrevious = false;
    Graphics::GpuTaskDesc firstAvboitUploadDesc;
    firstAvboitUploadDesc
        .setIdentity(Name("tests/task_graph/avboit_first_upload"))
        .setMarkerLabel("AVBOIT First Upload")
        .setScheduling(firstAvboitUploadScheduling)
        .setDependencies(&hardwareCaustics, 1u)
    ;
    const Graphics::GpuTaskId firstAvboitUpload = graph.addTask(firstAvboitUploadDesc);
    ASSERT_TRUE(firstAvboitUpload.valid());

    Graphics::GpuTaskSchedulingHint avboitTailScheduling;
    avboitTailScheduling.allowPacketMerge = true;
    avboitTailScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc secondAvboitUploadDesc;
    secondAvboitUploadDesc
        .setIdentity(Name("tests/task_graph/avboit_second_upload"))
        .setMarkerLabel("AVBOIT Second Upload")
        .setScheduling(avboitTailScheduling)
        .setDependencies(&firstAvboitUpload, 1u)
    ;
    const Graphics::GpuTaskId secondAvboitUpload = graph.addTask(secondAvboitUploadDesc);
    ASSERT_TRUE(secondAvboitUpload.valid());

    Graphics::GpuTaskDesc avboitPreDesc;
    avboitPreDesc
        .setIdentity(Name("tests/task_graph/avboit_pre"))
        .setMarkerLabel("AVBOIT Pre")
        .setScheduling(avboitTailScheduling)
        .setDependencies(&secondAvboitUpload, 1u)
    ;
    const Graphics::GpuTaskId avboitPre = graph.addTask(avboitPreDesc);
    ASSERT_TRUE(avboitPre.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_TRUE(compiledPlan.validFor(declarations));
    ASSERT_EQ(compiledPlan.packetCount(), 2u);

    const Graphics::GpuSubmissionPacketId hardwareCausticsPacket = compiledPlan.packetForTask(hardwareCaustics);
    const Graphics::GpuSubmissionPacketId firstAvboitUploadPacket = compiledPlan.packetForTask(firstAvboitUpload);
    const Graphics::GpuSubmissionPacketId secondAvboitUploadPacket = compiledPlan.packetForTask(secondAvboitUpload);
    const Graphics::GpuSubmissionPacketId avboitPrePacket = compiledPlan.packetForTask(avboitPre);
    ASSERT_TRUE(hardwareCausticsPacket.valid());
    ASSERT_TRUE(firstAvboitUploadPacket.valid());
    EXPECT_NE(hardwareCausticsPacket, firstAvboitUploadPacket);
    EXPECT_EQ(firstAvboitUploadPacket, secondAvboitUploadPacket);
    EXPECT_EQ(firstAvboitUploadPacket, avboitPrePacket);

    ASSERT_EQ(compiledPlan.packet(hardwareCausticsPacket).plan->taskCount, 1u);
    ASSERT_EQ(compiledPlan.packet(firstAvboitUploadPacket).plan->taskCount, 3u);
    ASSERT_NE(compiledPlan.packet(hardwareCausticsPacket).tasks, nullptr);
    ASSERT_NE(compiledPlan.packet(firstAvboitUploadPacket).tasks, nullptr);
    EXPECT_EQ(compiledPlan.packet(hardwareCausticsPacket).tasks[0u], hardwareCaustics);
    EXPECT_EQ(compiledPlan.packet(firstAvboitUploadPacket).tasks[0u], firstAvboitUpload);
    EXPECT_EQ(compiledPlan.packet(firstAvboitUploadPacket).tasks[1u], secondAvboitUpload);
    EXPECT_EQ(compiledPlan.packet(firstAvboitUploadPacket).tasks[2u], avboitPre);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

