// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_avboit_extinction_shared_output_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, KeepsUnsplitAvboitExtinctionSharedOutputRegularPairsWithTypedIntegrationInPrePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId stateProbe = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_state_probe"),
        "AVBOIT Extinction Shared Output State Probe",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId coverage = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_coverage"),
        "AVBOIT Coverage",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId depthWarp = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_depth_warp"),
        "AVBOIT Depth Warp",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId control = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_control"),
        "AVBOIT Control",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId materialStream = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_material_stream"),
        "AVBOIT Extinction Material Stream",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertex = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_generated_vertex"),
        "AVBOIT Extinction Shared Generated Vertex",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId lowRaster = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_low_raster"),
        "AVBOIT Low Raster",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId extinction = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_extinction"),
        "AVBOIT Extinction",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId extinctionOverflow = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_overflow"),
        "AVBOIT Extinction Overflow",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId transmittance = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_transmittance"),
        "AVBOIT Transmittance",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(stateProbe.valid());
    ASSERT_TRUE(coverage.valid());
    ASSERT_TRUE(depthWarp.valid());
    ASSERT_TRUE(control.valid());
    ASSERT_TRUE(materialStream.valid());
    ASSERT_TRUE(generatedVertex.valid());
    ASSERT_TRUE(lowRaster.valid());
    ASSERT_TRUE(extinction.valid());
    ASSERT_TRUE(extinctionOverflow.valid());
    ASSERT_TRUE(transmittance.valid());

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
    const Graphics::GpuTaskResourceUse occupancyUses[] = {
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
            .resource = lowRaster,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
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
    const Graphics::GpuTaskResourceUse integrationUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
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
        Graphics::GpuTaskResourceUse{
            .resource = transmittance,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskResourceUse accumulationUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = transmittance,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };

    const Graphics::GpuTaskId pre = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_shared_output_pre"))
            .setMarkerLabel("AVBOIT Pre")
            .setQueue(graphicsQueue)
            .setScheduling(preScheduling)
            .setResourceUses(preUses, LengthOf(preUses))
    );
    ASSERT_TRUE(pre.valid());
    const Graphics::GpuTaskId occupancy = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_shared_output_occupancy"))
            .setMarkerLabel("AVBOIT Occupancy")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&pre, 1u)
            .setResourceUses(occupancyUses, LengthOf(occupancyUses))
    );
    ASSERT_TRUE(occupancy.valid());
    const Graphics::GpuTaskId depthWarpTask = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_shared_output_depth_warp"))
            .setMarkerLabel("AVBOIT Depth Warp")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&occupancy, 1u)
            .setResourceUses(depthWarpUses, LengthOf(depthWarpUses))
    );
    ASSERT_TRUE(depthWarpTask.valid());
    const Graphics::GpuTaskId streamTask = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_shared_output_material_upload"))
            .setMarkerLabel("AVBOIT Extinction Material Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&depthWarpTask, 1u)
            .setResourceUses(streamUses, LengthOf(streamUses))
    );
    ASSERT_TRUE(streamTask.valid());

    const Name sharedPhaseIdentities[] = {
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_dispatch_a"),
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_raster_a"),
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_dispatch_b"),
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_raster_b"),
    };
    const AStringView sharedPhaseMarkers[] = {
        "AVBOIT Extinction Shared Compute Emulation Generate A",
        "AVBOIT Extinction Shared Compute Emulation Raster A",
        "AVBOIT Extinction Shared Compute Emulation Generate B",
        "AVBOIT Extinction Shared Compute Emulation Raster B",
    };
    Graphics::GpuTaskId sharedPhaseTasks[LengthOf(sharedPhaseIdentities)] = {};
    for(usize phaseIndex = 0u; phaseIndex < LengthOf(sharedPhaseTasks); ++phaseIndex){
        const bool isRaster = phaseIndex % 2u != 0u;
        const Graphics::GpuTaskId dependency = phaseIndex == 0u ? streamTask : sharedPhaseTasks[phaseIndex - 1u];
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
    const Graphics::GpuTaskId integrationTask = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_shared_output_integration"))
            .setMarkerLabel("AVBOIT Integration")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&rasterB, 1u)
            .setResourceUses(integrationUses, LengthOf(integrationUses))
    );
    ASSERT_TRUE(integrationTask.valid());
    const Graphics::GpuTaskId accumulationTask = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_shared_output_accumulation"))
            .setMarkerLabel("AVBOIT Accumulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&integrationTask, 1u)
            .setResourceUses(accumulationUses, LengthOf(accumulationUses))
    );
    ASSERT_TRUE(accumulationTask.valid());

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


    EXPECT_TRUE(analysis.hasExplicitEdge(pre, occupancy));
    EXPECT_TRUE(analysis.hasExplicitEdge(occupancy, depthWarpTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(depthWarpTask, streamTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(streamTask, dispatchA));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchA, rasterA));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterA, dispatchB));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchB, rasterB));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterB, integrationTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(integrationTask, accumulationTask));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        occupancy,
        depthWarpTask,
        coverage,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        depthWarpTask,
        rasterA,
        depthWarp,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        depthWarpTask,
        rasterA,
        control,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        streamTask,
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
        integrationTask,
        extinction,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        rasterB,
        integrationTask,
        extinctionOverflow,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        integrationTask,
        accumulationTask,
        transmittance,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    const Graphics::GpuTaskId tasks[] = {
        pre,
        occupancy,
        depthWarpTask,
        streamTask,
        dispatchA,
        rasterA,
        dispatchB,
        rasterB,
        integrationTask,
        accumulationTask,
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
        EXPECT_EQ(compiledPlan.packetForTask(task), packet);
    EXPECT_TRUE(compiledPlan.tasksSharePacket(pre, accumulationTask));
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
        occupancy,
        coverage,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferUav(depthWarpTask, coverage));
    EXPECT_TRUE(hasBufferTransition(
        depthWarpTask,
        depthWarp,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        depthWarpTask,
        control,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        streamTask,
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
        rasterA,
        depthWarp,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterA,
        control,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasTextureTransition(
        rasterA,
        lowRaster,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::RenderTarget
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterA,
        extinction,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterA,
        extinctionOverflow,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferUav(rasterB, extinction));
    EXPECT_TRUE(hasBufferUav(rasterB, extinctionOverflow));
    EXPECT_TRUE(hasBufferTransition(
        integrationTask,
        extinction,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        integrationTask,
        extinctionOverflow,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        integrationTask,
        transmittance,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        accumulationTask,
        transmittance,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
}

TEST(GpuTaskGraph, KeepsUnsplitAvboitExtinctionSharedOutputRegularTriplesWithTypedIntegrationInPrePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId stateProbe = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_state_probe"),
        "AVBOIT Extinction Shared Output State Probe",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId coverage = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_coverage"),
        "AVBOIT Coverage",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId depthWarp = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_depth_warp"),
        "AVBOIT Depth Warp",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId control = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_control"),
        "AVBOIT Control",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId materialStream = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_material_stream"),
        "AVBOIT Extinction Material Stream",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertex = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_generated_vertex"),
        "AVBOIT Extinction Shared Generated Vertex",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId lowRaster = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_low_raster"),
        "AVBOIT Low Raster",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId extinction = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_extinction"),
        "AVBOIT Extinction",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId extinctionOverflow = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_overflow"),
        "AVBOIT Extinction Overflow",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId transmittance = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_transmittance"),
        "AVBOIT Transmittance",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(stateProbe.valid());
    ASSERT_TRUE(coverage.valid());
    ASSERT_TRUE(depthWarp.valid());
    ASSERT_TRUE(control.valid());
    ASSERT_TRUE(materialStream.valid());
    ASSERT_TRUE(generatedVertex.valid());
    ASSERT_TRUE(lowRaster.valid());
    ASSERT_TRUE(extinction.valid());
    ASSERT_TRUE(extinctionOverflow.valid());
    ASSERT_TRUE(transmittance.valid());

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
    const Graphics::GpuTaskResourceUse occupancyUses[] = {
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
            .resource = lowRaster,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
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
    const Graphics::GpuTaskResourceUse integrationUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
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
        Graphics::GpuTaskResourceUse{
            .resource = transmittance,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskResourceUse accumulationUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = stateProbe,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = transmittance,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };

    const Graphics::GpuTaskId pre = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_pre"))
            .setMarkerLabel("AVBOIT Pre")
            .setQueue(graphicsQueue)
            .setScheduling(preScheduling)
            .setResourceUses(preUses, LengthOf(preUses))
    );
    ASSERT_TRUE(pre.valid());
    const Graphics::GpuTaskId occupancy = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_occupancy"))
            .setMarkerLabel("AVBOIT Occupancy")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&pre, 1u)
            .setResourceUses(occupancyUses, LengthOf(occupancyUses))
    );
    ASSERT_TRUE(occupancy.valid());
    const Graphics::GpuTaskId depthWarpTask = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_depth_warp"))
            .setMarkerLabel("AVBOIT Depth Warp")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&occupancy, 1u)
            .setResourceUses(depthWarpUses, LengthOf(depthWarpUses))
    );
    ASSERT_TRUE(depthWarpTask.valid());
    const Graphics::GpuTaskId streamTask = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_material_upload"))
            .setMarkerLabel("AVBOIT Extinction Material Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&depthWarpTask, 1u)
            .setResourceUses(streamUses, LengthOf(streamUses))
    );
    ASSERT_TRUE(streamTask.valid());

    const Name sharedPhaseIdentities[] = {
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_dispatch_a"),
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_raster_a"),
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_dispatch_b"),
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_raster_b"),
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_dispatch_c"),
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_raster_c"),
    };
    const AStringView sharedPhaseMarkers[] = {
        "AVBOIT Extinction Shared Compute Emulation Generate A",
        "AVBOIT Extinction Shared Compute Emulation Raster A",
        "AVBOIT Extinction Shared Compute Emulation Generate B",
        "AVBOIT Extinction Shared Compute Emulation Raster B",
        "AVBOIT Extinction Shared Compute Emulation Generate C",
        "AVBOIT Extinction Shared Compute Emulation Raster C",
    };
    Graphics::GpuTaskId sharedPhaseTasks[LengthOf(sharedPhaseIdentities)] = {};
    for(usize phaseIndex = 0u; phaseIndex < LengthOf(sharedPhaseTasks); ++phaseIndex){
        const bool isRaster = phaseIndex % 2u != 0u;
        const Graphics::GpuTaskId dependency = phaseIndex == 0u ? streamTask : sharedPhaseTasks[phaseIndex - 1u];
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
    const Graphics::GpuTaskId integrationTask = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_integration"))
            .setMarkerLabel("AVBOIT Integration")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&rasterC, 1u)
            .setResourceUses(integrationUses, LengthOf(integrationUses))
    );
    ASSERT_TRUE(integrationTask.valid());
    const Graphics::GpuTaskId accumulationTask = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_shared_output_triple_accumulation"))
            .setMarkerLabel("AVBOIT Accumulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&integrationTask, 1u)
            .setResourceUses(accumulationUses, LengthOf(accumulationUses))
    );
    ASSERT_TRUE(accumulationTask.valid());

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


    EXPECT_TRUE(analysis.hasExplicitEdge(pre, occupancy));
    EXPECT_TRUE(analysis.hasExplicitEdge(occupancy, depthWarpTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(depthWarpTask, streamTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(streamTask, dispatchA));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchA, rasterA));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterA, dispatchB));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchB, rasterB));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterB, dispatchC));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchC, rasterC));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterC, integrationTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(integrationTask, accumulationTask));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        occupancy,
        depthWarpTask,
        coverage,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        depthWarpTask,
        rasterA,
        depthWarp,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        depthWarpTask,
        rasterA,
        control,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        streamTask,
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
        integrationTask,
        extinction,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        rasterC,
        integrationTask,
        extinctionOverflow,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        integrationTask,
        accumulationTask,
        transmittance,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    const Graphics::GpuTaskId tasks[] = {
        pre,
        occupancy,
        depthWarpTask,
        streamTask,
        dispatchA,
        rasterA,
        dispatchB,
        rasterB,
        dispatchC,
        rasterC,
        integrationTask,
        accumulationTask,
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
        EXPECT_EQ(compiledPlan.packetForTask(task), packet);
    EXPECT_TRUE(compiledPlan.tasksSharePacket(pre, accumulationTask));
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
        occupancy,
        coverage,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferUav(depthWarpTask, coverage));
    EXPECT_TRUE(hasBufferTransition(
        depthWarpTask,
        depthWarp,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        depthWarpTask,
        control,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        streamTask,
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
        rasterA,
        depthWarp,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterA,
        control,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasTextureTransition(
        rasterA,
        lowRaster,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::RenderTarget
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterA,
        extinction,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterA,
        extinctionOverflow,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferUav(rasterB, extinction));
    EXPECT_TRUE(hasBufferUav(rasterB, extinctionOverflow));
    EXPECT_TRUE(hasBufferUav(rasterC, extinction));
    EXPECT_TRUE(hasBufferUav(rasterC, extinctionOverflow));
    EXPECT_TRUE(hasBufferTransition(
        integrationTask,
        extinction,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        integrationTask,
        extinctionOverflow,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        integrationTask,
        transmittance,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        accumulationTask,
        transmittance,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
}

TEST(GpuTaskGraph, KeepsUnsplitAvboitExtinctionSharedOutputRegularQuintuplesWithTypedIntegrationInPrePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId stateProbe = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_state_probe"),
        "AVBOIT Extinction Shared Output State Probe",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId materialStream = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_material_stream"),
        "AVBOIT Extinction Material Stream",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertex = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_generated_vertex"),
        "AVBOIT Extinction Shared Generated Vertex",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId extinction = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_extinction"),
        "AVBOIT Extinction",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId transmittance = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_transmittance"),
        "AVBOIT Transmittance",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(stateProbe.valid());
    ASSERT_TRUE(materialStream.valid());
    ASSERT_TRUE(generatedVertex.valid());
    ASSERT_TRUE(extinction.valid());
    ASSERT_TRUE(transmittance.valid());

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
    Graphics::GpuTaskSchedulingHint tailScheduling = preScheduling;
    tailScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    tailScheduling.mergeWithPrevious = true;
    tailScheduling.allowMergeAcrossConsumerFrontier = true;

    const Graphics::GpuTaskResourceUse readProbeUses[] = {
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
            .resource = extinction,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskResourceUse integrationUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = extinction,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = transmittance,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskResourceUse accumulationUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = transmittance,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };

    const Graphics::GpuTaskId pre = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_pre"))
            .setMarkerLabel("AVBOIT Pre")
            .setQueue(graphicsQueue)
            .setScheduling(preScheduling)
            .setResourceUses(readProbeUses, LengthOf(readProbeUses))
    );
    ASSERT_TRUE(pre.valid());
    const Graphics::GpuTaskId occupancy = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_occupancy"))
            .setMarkerLabel("AVBOIT Occupancy")
            .setQueue(graphicsComputeQueue)
            .setScheduling(tailScheduling)
            .setDependencies(&pre, 1u)
            .setResourceUses(readProbeUses, LengthOf(readProbeUses))
    );
    ASSERT_TRUE(occupancy.valid());
    const Graphics::GpuTaskId depthWarp = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_depth_warp"))
            .setMarkerLabel("AVBOIT Depth Warp")
            .setQueue(graphicsComputeQueue)
            .setScheduling(tailScheduling)
            .setDependencies(&occupancy, 1u)
            .setResourceUses(readProbeUses, LengthOf(readProbeUses))
    );
    ASSERT_TRUE(depthWarp.valid());
    const Graphics::GpuTaskId stream = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_material_upload"))
            .setMarkerLabel("AVBOIT Extinction Material Upload")
            .setQueue(graphicsQueue)
            .setScheduling(tailScheduling)
            .setDependencies(&depthWarp, 1u)
            .setResourceUses(streamUses, LengthOf(streamUses))
    );
    ASSERT_TRUE(stream.valid());

    const Name phaseIdentities[] = {
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_dispatch_a"),
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_raster_a"),
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_dispatch_b"),
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_raster_b"),
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_dispatch_c"),
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_raster_c"),
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_dispatch_d"),
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_raster_d"),
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_dispatch_e"),
        Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_raster_e"),
    };
    const AStringView phaseMarkers[] = {
        "AVBOIT Extinction Shared Compute Emulation Generate A",
        "AVBOIT Extinction Shared Compute Emulation Raster A",
        "AVBOIT Extinction Shared Compute Emulation Generate B",
        "AVBOIT Extinction Shared Compute Emulation Raster B",
        "AVBOIT Extinction Shared Compute Emulation Generate C",
        "AVBOIT Extinction Shared Compute Emulation Raster C",
        "AVBOIT Extinction Shared Compute Emulation Generate D",
        "AVBOIT Extinction Shared Compute Emulation Raster D",
        "AVBOIT Extinction Shared Compute Emulation Generate E",
        "AVBOIT Extinction Shared Compute Emulation Raster E",
    };
    Graphics::GpuTaskId phases[LengthOf(phaseIdentities)] = {};
    for(usize phaseIndex = 0u; phaseIndex < LengthOf(phases); ++phaseIndex){
        const bool raster = phaseIndex % 2u != 0u;
        const Graphics::GpuTaskId dependency = phaseIndex == 0u ? stream : phases[phaseIndex - 1u];
        phases[phaseIndex] = graph.addTask(
            Graphics::GpuTaskDesc{}
                .setIdentity(phaseIdentities[phaseIndex])
                .setMarkerLabel(phaseMarkers[phaseIndex])
                .setQueue(raster ? graphicsQueue : graphicsComputeQueue)
                .setScheduling(tailScheduling)
                .setDependencies(&dependency, 1u)
                .setResourceUses(
                    raster ? rasterUses : dispatchUses,
                    raster ? LengthOf(rasterUses) : LengthOf(dispatchUses)
                )
        );
        ASSERT_TRUE(phases[phaseIndex].valid());
    }
    const Graphics::GpuTaskId integration = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_integration"))
            .setMarkerLabel("AVBOIT Integration")
            .setQueue(graphicsComputeQueue)
            .setScheduling(tailScheduling)
            .setDependencies(&phases[LengthOf(phases) - 1u], 1u)
            .setResourceUses(integrationUses, LengthOf(integrationUses))
    );
    ASSERT_TRUE(integration.valid());
    const Graphics::GpuTaskId accumulation = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_shared_output_quint_accumulation"))
            .setMarkerLabel("AVBOIT Accumulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(tailScheduling)
            .setDependencies(&integration, 1u)
            .setResourceUses(accumulationUses, LengthOf(accumulationUses))
    );
    ASSERT_TRUE(accumulation.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    EXPECT_TRUE(analysis.hasExplicitEdge(stream, phases[0u]));
    for(usize phaseIndex = 1u; phaseIndex < LengthOf(phases); ++phaseIndex)
        EXPECT_TRUE(analysis.hasExplicitEdge(phases[phaseIndex - 1u], phases[phaseIndex]));
    EXPECT_TRUE(analysis.hasExplicitEdge(phases[9u], integration));
    EXPECT_TRUE(analysis.hasExplicitEdge(integration, accumulation));
    EXPECT_TRUE(HasInferredHazard(
        analysis, phases[7u], phases[8u], generatedVertex, Graphics::GpuTaskHazardType::WriteAfterRead
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis, phases[8u], phases[9u], generatedVertex, Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis, phases[9u], integration, extinction, Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis, integration, accumulation, transmittance, Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    const Graphics::GpuTaskId tasks[] = {
        pre, occupancy, depthWarp, stream,
        phases[0u], phases[1u], phases[2u], phases[3u],
        phases[4u], phases[5u], phases[6u], phases[7u], phases[8u], phases[9u],
        integration, accumulation,
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
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    ASSERT_EQ(compiledPacket.taskCount, LengthOf(tasks));
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
        EXPECT_EQ(compiledPlan.packetForTask(tasks[taskIndex]), packet);
        EXPECT_EQ(packetTasks[taskIndex], tasks[taskIndex]);
    }

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
        phases[0u], generatedVertex, Graphics::ResourceStates::Common, Graphics::ResourceStates::UnorderedAccess
    ));
    for(usize phaseIndex = 1u; phaseIndex < LengthOf(phases); ++phaseIndex){
        const Graphics::ResourceStates::Mask before = phaseIndex % 2u == 0u
            ? Graphics::ResourceStates::VertexBuffer
            : Graphics::ResourceStates::UnorderedAccess;
        const Graphics::ResourceStates::Mask after = phaseIndex % 2u == 0u
            ? Graphics::ResourceStates::UnorderedAccess
            : Graphics::ResourceStates::VertexBuffer;
        EXPECT_TRUE(hasBufferTransition(phases[phaseIndex], generatedVertex, before, after));
    }
    EXPECT_TRUE(hasBufferUav(phases[9u], extinction));
    EXPECT_TRUE(hasBufferTransition(
        integration, extinction, Graphics::ResourceStates::UnorderedAccess, Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        integration, transmittance, Graphics::ResourceStates::Common, Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        accumulation, transmittance, Graphics::ResourceStates::UnorderedAccess, Graphics::ResourceStates::ShaderResource
    ));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

