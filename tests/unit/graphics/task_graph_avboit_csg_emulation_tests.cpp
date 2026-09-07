// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_avboit_csg_emulation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, KeepsUnsplitAvboitOccupancyAliasFreeCsgComputeEmulationInPrePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId csgStream = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_csg_stream"),
        "AVBOIT Occupancy CSG Stream",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId coverage = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_csg_coverage"),
        "AVBOIT Coverage",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId removedIntervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_csg_removed_interval_depth"),
        "AVBOIT Occupancy CSG Removed Interval Depth"
    );
    const Graphics::GpuGraphResourceId removedIntervalCapNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_csg_removed_interval_cap_normal"),
        "AVBOIT Occupancy CSG Removed Interval Cap Normal"
    );
    const Graphics::GpuGraphResourceId removedIntervalData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_csg_removed_interval_data"),
        "AVBOIT Occupancy CSG Removed Interval Data"
    );
    const Graphics::GpuGraphResourceId removedIntervalCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_csg_removed_interval_count"),
        "AVBOIT Occupancy CSG Removed Interval Count"
    );
    const Graphics::GpuGraphResourceId generatedVertexA = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_csg_generated_vertex_a"),
        "AVBOIT Occupancy CSG Generated Vertex A",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertexB = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_occupancy_csg_generated_vertex_b"),
        "AVBOIT Occupancy CSG Generated Vertex B",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(csgStream.valid());
    ASSERT_TRUE(coverage.valid());
    ASSERT_TRUE(removedIntervalDepth.valid());
    ASSERT_TRUE(removedIntervalCapNormal.valid());
    ASSERT_TRUE(removedIntervalData.valid());
    ASSERT_TRUE(removedIntervalCount.valid());
    ASSERT_TRUE(generatedVertexA.valid());
    ASSERT_TRUE(generatedVertexB.valid());
    EXPECT_NE(generatedVertexA, generatedVertexB);

    const Graphics::GpuGraphResourceId removedIntervals[] = {
        removedIntervalDepth,
        removedIntervalCapNormal,
        removedIntervalData,
        removedIntervalCount,
    };
    const Graphics::GpuGraphResourceId generatedVertexOutputs[] = {
        generatedVertexA,
        generatedVertexB,
    };
    const Graphics::GpuGraphResourceSetId generatedVertexOutputSet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_csg_generated_vertex_outputs"))
            .setMarkerLabel("AVBOIT Occupancy CSG Generated Vertex Outputs")
            .setMembers(generatedVertexOutputs, LengthOf(generatedVertexOutputs))
    );
    ASSERT_TRUE(generatedVertexOutputSet.valid());

    const Graphics::TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 1u);
    const Graphics::GpuTaskResourceUse combineUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse streamUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = csgStream,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse clearUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse producerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = csgStream,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskResourceUse occupancyUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = csgStream,
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
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
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
    const Graphics::GpuQueueRequest graphicsQueue{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint combineScheduling;
    combineScheduling.cost = Graphics::GpuTaskCostHint::Large;
    combineScheduling.overlapPreferred = false;
    combineScheduling.avoidQueueCrossing = true;
    combineScheduling.forceSubmissionBoundary = false;
    combineScheduling.allowPacketMerge = true;
    combineScheduling.mergeWithPrevious = false;
    Graphics::GpuTaskSchedulingHint packetTailScheduling = combineScheduling;
    packetTailScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    packetTailScheduling.mergeWithPrevious = true;
    packetTailScheduling.allowMergeAcrossConsumerFrontier = true;

    const Graphics::GpuTaskId combine = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_csg_combine"))
            .setMarkerLabel("Transparent CSG Interval Combine")
            .setQueue(graphicsComputeQueue)
            .setScheduling(combineScheduling)
            .setResourceUses(combineUses, LengthOf(combineUses))
    );
    ASSERT_TRUE(combine.valid());

    const Graphics::GpuTaskId streamUpload = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_csg_stream_upload"))
            .setMarkerLabel("AVBOIT Occupancy CSG Stream Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&combine, 1u)
            .setResourceUses(streamUses, LengthOf(streamUses))
    );
    ASSERT_TRUE(streamUpload.valid());

    const Graphics::GpuTaskId clear = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_csg_clear"))
            .setMarkerLabel("AVBOIT Clear Coverage")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&streamUpload, 1u)
            .setResourceUses(clearUses, LengthOf(clearUses))
    );
    ASSERT_TRUE(clear.valid());

    const Graphics::GpuTaskId producer = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_csg_compute_emulation"))
            .setMarkerLabel("AVBOIT Occupancy CSG Compute Emulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&clear, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses))
            .setResourceSetUses(&outputUavSetUse, 1u)
    );
    ASSERT_TRUE(producer.valid());

    const Graphics::GpuTaskId occupancy = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_occupancy_csg_raster"))
            .setMarkerLabel("AVBOIT Occupancy")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&producer, 1u)
            .setResourceUses(occupancyUses, LengthOf(occupancyUses))
            .setResourceSetUses(&outputVertexBufferSetUse, 1u)
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


    EXPECT_TRUE(analysis.hasExplicitEdge(combine, streamUpload));
    EXPECT_TRUE(analysis.hasExplicitEdge(streamUpload, clear));
    EXPECT_TRUE(analysis.hasExplicitEdge(clear, producer));
    EXPECT_TRUE(analysis.hasExplicitEdge(producer, occupancy));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        streamUpload,
        producer,
        csgStream,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        clear,
        occupancy,
        coverage,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    for(const Graphics::GpuGraphResourceId removedInterval : removedIntervals){
        EXPECT_TRUE(HasInferredHazard(
            analysis,
            combine,
            producer,
            removedInterval,
            Graphics::GpuTaskHazardType::ReadAfterWrite
        ));
        EXPECT_TRUE(HasInferredHazard(
            analysis,
            combine,
            occupancy,
            removedInterval,
            Graphics::GpuTaskHazardType::ReadAfterWrite
        ));
    }
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
    EXPECT_EQ(analysis.topologicalOrder()[0u], combine);
    EXPECT_EQ(analysis.topologicalOrder()[1u], streamUpload);
    EXPECT_EQ(analysis.topologicalOrder()[2u], clear);
    EXPECT_EQ(analysis.topologicalOrder()[3u], producer);
    EXPECT_EQ(analysis.topologicalOrder()[4u], occupancy);

    for(const Graphics::GpuTaskId task : { combine, streamUpload, clear, producer, occupancy }){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue.id);
        EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Graphics);
    }
    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(combine);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetCount(), 1u);
    EXPECT_EQ(packet, compiledPlan.packetForTask(streamUpload));
    EXPECT_EQ(packet, compiledPlan.packetForTask(clear));
    EXPECT_EQ(packet, compiledPlan.packetForTask(producer));
    EXPECT_EQ(packet, compiledPlan.packetForTask(occupancy));
    EXPECT_TRUE(compiledPlan.tasksSharePacket(combine, occupancy));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(combine, streamUpload));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(streamUpload, clear));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(clear, producer));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(producer, occupancy));
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    EXPECT_EQ(compiledPacket.queue, queue.id);
    EXPECT_EQ(compiledPacket.dependencyCount, 0u);
    ASSERT_EQ(compiledPacket.taskCount, 5u);
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], combine);
    EXPECT_EQ(packetTasks[1u], streamUpload);
    EXPECT_EQ(packetTasks[2u], clear);
    EXPECT_EQ(packetTasks[3u], producer);
    EXPECT_EQ(packetTasks[4u], occupancy);

    const auto hasTextureBarrier = [&](const Graphics::GpuTaskId task,
                                       const Graphics::GpuCompiledBarrierType::Enum type,
                                       const Graphics::GpuGraphResourceId resource,
                                       const Graphics::ResourceStates::Mask before,
                                       const Graphics::ResourceStates::Mask after){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask && barriers && barrierIndex < compiledTask->prologueBarrierCount;
            ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == type
                && barrier.resource == resource
                && barrier.range.textureSubresources == removedIntervalRange
                && barrier.before == before
                && barrier.after == after
                && barrier.sourceQueue == queue.id
                && barrier.destinationQueue == queue.id
            )
                return true;
        }
        return false;
    };
    const auto hasBufferTransition = [&](const Graphics::GpuTaskId task,
                                         const Graphics::GpuGraphResourceId resource,
                                         const Graphics::ResourceStates::Mask before,
                                         const Graphics::ResourceStates::Mask after){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask && barriers && barrierIndex < compiledTask->prologueBarrierCount;
            ++barrierIndex){
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
    for(const Graphics::GpuGraphResourceId removedInterval : removedIntervals){
        EXPECT_TRUE(hasTextureBarrier(
            combine,
            Graphics::GpuCompiledBarrierType::TextureTransition,
            removedInterval,
            Graphics::ResourceStates::Common,
            Graphics::ResourceStates::UnorderedAccess
        ));
        EXPECT_TRUE(hasTextureBarrier(
            producer,
            Graphics::GpuCompiledBarrierType::TextureUav,
            removedInterval,
            Graphics::ResourceStates::UnorderedAccess,
            Graphics::ResourceStates::UnorderedAccess
        ));
    }
    EXPECT_TRUE(hasBufferTransition(
        producer,
        csgStream,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        occupancy,
        coverage,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
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
}

TEST(GpuTaskGraph, KeepsUnsplitAvboitExtinctionAliasFreeCsgComputeEmulationInPrePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId preState = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_csg_pre_state"),
        "AVBOIT Extinction CSG Pre State",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId receiverRanges = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_csg_receiver_ranges"),
        "AVBOIT Extinction CSG Receiver Ranges",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId cutters = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_csg_cutters"),
        "AVBOIT Extinction CSG Cutters",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId clipContextSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_csg_clip_context_slots"),
        "AVBOIT Extinction CSG Clip Context Slots",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId intervalSampleState = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_csg_interval_sample_state"),
        "AVBOIT Extinction CSG Interval Sample State",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId removedIntervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_csg_removed_interval_depth"),
        "AVBOIT Extinction CSG Removed Interval Depth"
    );
    const Graphics::GpuGraphResourceId removedIntervalCapNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_csg_removed_interval_cap_normal"),
        "AVBOIT Extinction CSG Removed Interval Cap Normal"
    );
    const Graphics::GpuGraphResourceId removedIntervalData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_csg_removed_interval_data"),
        "AVBOIT Extinction CSG Removed Interval Data"
    );
    const Graphics::GpuGraphResourceId removedIntervalCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_csg_removed_interval_count"),
        "AVBOIT Extinction CSG Removed Interval Count"
    );
    const Graphics::GpuGraphResourceId generatedVertexA = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_csg_generated_vertex_a"),
        "AVBOIT Extinction CSG Generated Vertex A",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertexB = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_csg_generated_vertex_b"),
        "AVBOIT Extinction CSG Generated Vertex B",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId extinctionOutput = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_csg_output"),
        "AVBOIT Extinction",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId extinctionOverflow = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_csg_overflow"),
        "AVBOIT Extinction Overflow",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId transmittance = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_extinction_csg_transmittance"),
        "AVBOIT Transmittance",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(preState.valid());
    ASSERT_TRUE(receiverRanges.valid());
    ASSERT_TRUE(cutters.valid());
    ASSERT_TRUE(clipContextSlots.valid());
    ASSERT_TRUE(intervalSampleState.valid());
    ASSERT_TRUE(removedIntervalDepth.valid());
    ASSERT_TRUE(removedIntervalCapNormal.valid());
    ASSERT_TRUE(removedIntervalData.valid());
    ASSERT_TRUE(removedIntervalCount.valid());
    ASSERT_TRUE(generatedVertexA.valid());
    ASSERT_TRUE(generatedVertexB.valid());
    ASSERT_TRUE(extinctionOutput.valid());
    ASSERT_TRUE(extinctionOverflow.valid());
    ASSERT_TRUE(transmittance.valid());
    EXPECT_NE(generatedVertexA, generatedVertexB);

    const Graphics::GpuGraphResourceId removedIntervals[] = {
        removedIntervalDepth,
        removedIntervalCapNormal,
        removedIntervalData,
        removedIntervalCount,
    };
    const Graphics::GpuGraphResourceId generatedVertexOutputs[] = {
        generatedVertexA,
        generatedVertexB,
    };
    const Graphics::GpuGraphResourceSetId generatedVertexOutputSet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_csg_generated_vertex_outputs"))
            .setMarkerLabel("AVBOIT Extinction CSG Generated Vertex Outputs")
            .setMembers(generatedVertexOutputs, LengthOf(generatedVertexOutputs))
    );
    ASSERT_TRUE(generatedVertexOutputSet.valid());

    const Graphics::TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const Graphics::GpuTaskResourceUse preUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = preState,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskResourceUse combineUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse receiverRangesUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = receiverRanges,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse cuttersUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = cutters,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse clipContextSlotsUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = clipContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse producerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = receiverRanges,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = cutters,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = clipContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalSampleState,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskResourceUse extinctionUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = receiverRanges,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = cutters,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = clipContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalSampleState,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = extinctionOutput,
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
            .resource = extinctionOutput,
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

    const Graphics::GpuTaskId pre = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_csg_pre"))
            .setMarkerLabel("AVBOIT Pre")
            .setQueue(graphicsQueue)
            .setScheduling(preScheduling)
            .setResourceUses(preUses, LengthOf(preUses))
    );
    ASSERT_TRUE(pre.valid());
    const Graphics::GpuTaskId combine = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_csg_combine"))
            .setMarkerLabel("Transparent CSG Interval Combine")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&pre, 1u)
            .setResourceUses(combineUses, LengthOf(combineUses))
    );
    ASSERT_TRUE(combine.valid());
    const Graphics::GpuTaskId receiverRangesUpload = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_csg_receiver_ranges_upload"))
            .setMarkerLabel("AVBOIT Extinction CSG Receiver Ranges Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&combine, 1u)
            .setResourceUses(receiverRangesUploadUses, LengthOf(receiverRangesUploadUses))
    );
    ASSERT_TRUE(receiverRangesUpload.valid());
    const Graphics::GpuTaskId cuttersUpload = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_csg_cutters_upload"))
            .setMarkerLabel("AVBOIT Extinction CSG Cutters Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&receiverRangesUpload, 1u)
            .setResourceUses(cuttersUploadUses, LengthOf(cuttersUploadUses))
    );
    ASSERT_TRUE(cuttersUpload.valid());
    const Graphics::GpuTaskId clipContextSlotsUpload = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_csg_clip_context_slots_upload"))
            .setMarkerLabel("AVBOIT Extinction CSG Clip Context Slots Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&cuttersUpload, 1u)
            .setResourceUses(clipContextSlotsUploadUses, LengthOf(clipContextSlotsUploadUses))
    );
    ASSERT_TRUE(clipContextSlotsUpload.valid());
    const Graphics::GpuTaskId producer = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_csg_compute_emulation"))
            .setMarkerLabel("AVBOIT Extinction CSG Compute Emulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&clipContextSlotsUpload, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses))
            .setResourceSetUses(&outputUavSetUse, 1u)
    );
    ASSERT_TRUE(producer.valid());
    const Graphics::GpuTaskId extinction = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_csg_raster"))
            .setMarkerLabel("AVBOIT Extinction")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&producer, 1u)
            .setResourceUses(extinctionUses, LengthOf(extinctionUses))
            .setResourceSetUses(&outputVertexBufferSetUse, 1u)
    );
    ASSERT_TRUE(extinction.valid());
    const Graphics::GpuTaskId integration = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_extinction_csg_integration"))
            .setMarkerLabel("AVBOIT Integration")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&extinction, 1u)
            .setResourceUses(integrationUses, LengthOf(integrationUses))
    );
    ASSERT_TRUE(integration.valid());

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


    EXPECT_TRUE(analysis.hasExplicitEdge(pre, combine));
    EXPECT_TRUE(analysis.hasExplicitEdge(combine, receiverRangesUpload));
    EXPECT_TRUE(analysis.hasExplicitEdge(receiverRangesUpload, cuttersUpload));
    EXPECT_TRUE(analysis.hasExplicitEdge(cuttersUpload, clipContextSlotsUpload));
    EXPECT_TRUE(analysis.hasExplicitEdge(clipContextSlotsUpload, producer));
    EXPECT_TRUE(analysis.hasExplicitEdge(producer, extinction));
    EXPECT_TRUE(analysis.hasExplicitEdge(extinction, integration));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        receiverRangesUpload,
        producer,
        receiverRanges,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        cuttersUpload,
        producer,
        cutters,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        clipContextSlotsUpload,
        producer,
        clipContextSlots,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    for(const Graphics::GpuGraphResourceId removedInterval : removedIntervals){
        EXPECT_TRUE(HasInferredHazard(
            analysis,
            combine,
            producer,
            removedInterval,
            Graphics::GpuTaskHazardType::ReadAfterWrite
        ));
        EXPECT_TRUE(HasInferredHazard(
            analysis,
            combine,
            extinction,
            removedInterval,
            Graphics::GpuTaskHazardType::ReadAfterWrite
        ));
    }
    for(const Graphics::GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(HasInferredHazard(
            analysis,
            producer,
            extinction,
            output,
            Graphics::GpuTaskHazardType::ReadAfterWrite
        ));
    }
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        extinction,
        integration,
        extinctionOutput,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        extinction,
        integration,
        extinctionOverflow,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    const Graphics::GpuTaskId tasks[] = {
        pre,
        combine,
        receiverRangesUpload,
        cuttersUpload,
        clipContextSlotsUpload,
        producer,
        extinction,
        integration,
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
    EXPECT_TRUE(compiledPlan.tasksSharePacket(producer, extinction));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(producer, extinction));
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    EXPECT_EQ(compiledPacket.queue, queue.id);
    EXPECT_EQ(compiledPacket.dependencyCount, 0u);
    ASSERT_EQ(compiledPacket.taskCount, LengthOf(tasks));
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex)
        EXPECT_EQ(packetTasks[taskIndex], tasks[taskIndex]);
    EXPECT_EQ(packetTasks[5u], producer);
    EXPECT_EQ(packetTasks[6u], extinction);

    const auto hasBufferTransition = [&](const Graphics::GpuTaskId task,
                                         const Graphics::GpuGraphResourceId resource,
                                         const Graphics::ResourceStates::Mask before,
                                         const Graphics::ResourceStates::Mask after){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask && barriers && barrierIndex < compiledTask->prologueBarrierCount;
            ++barrierIndex){
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
    const auto hasTextureUav = [&](const Graphics::GpuTaskId task,
                                   const Graphics::GpuGraphResourceId resource,
                                   const Graphics::TextureSubresourceSet& range){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask && barriers && barrierIndex < compiledTask->prologueBarrierCount;
            ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
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
        receiverRangesUpload,
        receiverRanges,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        cuttersUpload,
        cutters,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        clipContextSlotsUpload,
        clipContextSlots,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        producer,
        receiverRanges,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        producer,
        cutters,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        producer,
        clipContextSlots,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ConstantBuffer
    ));
    EXPECT_TRUE(hasBufferTransition(
        producer,
        intervalSampleState,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::ConstantBuffer
    ));
    EXPECT_TRUE(hasTextureUav(producer, removedIntervalDepth, removedIntervalRange));
    EXPECT_TRUE(hasTextureUav(producer, removedIntervalCapNormal, removedIntervalRange));
    EXPECT_TRUE(hasTextureUav(producer, removedIntervalData, removedIntervalRange));
    EXPECT_TRUE(hasTextureUav(producer, removedIntervalCount, removedIntervalCountRange));
    for(const Graphics::GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(hasBufferTransition(
            producer,
            output,
            Graphics::ResourceStates::Common,
            Graphics::ResourceStates::UnorderedAccess
        ));
        EXPECT_TRUE(hasBufferTransition(
            extinction,
            output,
            Graphics::ResourceStates::UnorderedAccess,
            Graphics::ResourceStates::VertexBuffer
        ));
    }
    EXPECT_TRUE(hasBufferTransition(
        extinction,
        extinctionOutput,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        extinction,
        extinctionOverflow,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        integration,
        extinctionOutput,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        integration,
        extinctionOverflow,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
}

TEST(GpuTaskGraph, KeepsUnsplitAvboitAccumulationAliasFreeCsgComputeEmulationInPrePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId preState = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_csg_pre_state"),
        "AVBOIT Accumulation CSG Pre State",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId receiverRanges = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_csg_receiver_ranges"),
        "AVBOIT Accumulation CSG Receiver Ranges",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId cutters = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_csg_cutters"),
        "AVBOIT Accumulation CSG Cutters",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId clipContextSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_csg_clip_context_slots"),
        "AVBOIT Accumulation CSG Clip Context Slots",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId intervalSampleState = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_csg_interval_sample_state"),
        "AVBOIT Accumulation CSG Interval Sample State",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId removedIntervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_csg_removed_interval_depth"),
        "AVBOIT Accumulation CSG Removed Interval Depth"
    );
    const Graphics::GpuGraphResourceId removedIntervalCapNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_csg_removed_interval_cap_normal"),
        "AVBOIT Accumulation CSG Removed Interval Cap Normal"
    );
    const Graphics::GpuGraphResourceId removedIntervalData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_csg_removed_interval_data"),
        "AVBOIT Accumulation CSG Removed Interval Data"
    );
    const Graphics::GpuGraphResourceId removedIntervalCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_csg_removed_interval_count"),
        "AVBOIT Accumulation CSG Removed Interval Count"
    );
    const Graphics::GpuGraphResourceId generatedVertexA = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_csg_generated_vertex_a"),
        "AVBOIT Accumulation CSG Generated Vertex A",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId generatedVertexB = AddBufferMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_csg_generated_vertex_b"),
        "AVBOIT Accumulation CSG Generated Vertex B",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId accumColor = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_csg_color"),
        "AVBOIT Accumulation Color",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId accumExtinction = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_csg_extinction"),
        "AVBOIT Accumulation Extinction",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    const Graphics::GpuGraphResourceId deferredDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/unsplit_avboit_accumulation_csg_depth"),
        "Deferred Depth",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_TRUE(preState.valid());
    ASSERT_TRUE(receiverRanges.valid());
    ASSERT_TRUE(cutters.valid());
    ASSERT_TRUE(clipContextSlots.valid());
    ASSERT_TRUE(intervalSampleState.valid());
    ASSERT_TRUE(removedIntervalDepth.valid());
    ASSERT_TRUE(removedIntervalCapNormal.valid());
    ASSERT_TRUE(removedIntervalData.valid());
    ASSERT_TRUE(removedIntervalCount.valid());
    ASSERT_TRUE(generatedVertexA.valid());
    ASSERT_TRUE(generatedVertexB.valid());
    ASSERT_TRUE(accumColor.valid());
    ASSERT_TRUE(accumExtinction.valid());
    ASSERT_TRUE(deferredDepth.valid());
    EXPECT_NE(generatedVertexA, generatedVertexB);

    const Graphics::GpuGraphResourceId removedIntervals[] = {
        removedIntervalDepth,
        removedIntervalCapNormal,
        removedIntervalData,
        removedIntervalCount,
    };
    const Graphics::GpuGraphResourceId generatedVertexOutputs[] = {
        generatedVertexA,
        generatedVertexB,
    };
    const Graphics::GpuGraphResourceSetId generatedVertexOutputSet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_csg_generated_vertex_outputs"))
            .setMarkerLabel("AVBOIT Accumulation CSG Generated Vertex Outputs")
            .setMembers(generatedVertexOutputs, LengthOf(generatedVertexOutputs))
    );
    ASSERT_TRUE(generatedVertexOutputSet.valid());

    const Graphics::TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const Graphics::GpuTaskResourceUse preUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = preState,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskResourceUse combineUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse receiverRangesUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = receiverRanges,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse cuttersUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = cutters,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse clipContextSlotsUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = clipContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse producerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = receiverRanges,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = cutters,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = clipContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalSampleState,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskResourceUse accumulationUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = receiverRanges,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = cutters,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = clipContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalSampleState,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
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
            .resource = preState,
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

    const Graphics::GpuTaskId pre = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_csg_pre"))
            .setMarkerLabel("AVBOIT Pre")
            .setQueue(graphicsQueue)
            .setScheduling(preScheduling)
            .setResourceUses(preUses, LengthOf(preUses))
    );
    ASSERT_TRUE(pre.valid());
    const Graphics::GpuTaskId combine = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_csg_combine"))
            .setMarkerLabel("Transparent CSG Interval Combine")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&pre, 1u)
            .setResourceUses(combineUses, LengthOf(combineUses))
    );
    ASSERT_TRUE(combine.valid());
    const Graphics::GpuTaskId receiverRangesUpload = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_csg_receiver_ranges_upload"))
            .setMarkerLabel("AVBOIT Accumulation CSG Receiver Ranges Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&combine, 1u)
            .setResourceUses(receiverRangesUploadUses, LengthOf(receiverRangesUploadUses))
    );
    ASSERT_TRUE(receiverRangesUpload.valid());
    const Graphics::GpuTaskId cuttersUpload = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_csg_cutters_upload"))
            .setMarkerLabel("AVBOIT Accumulation CSG Cutters Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&receiverRangesUpload, 1u)
            .setResourceUses(cuttersUploadUses, LengthOf(cuttersUploadUses))
    );
    ASSERT_TRUE(cuttersUpload.valid());
    const Graphics::GpuTaskId clipContextSlotsUpload = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_csg_clip_context_slots_upload"))
            .setMarkerLabel("AVBOIT Accumulation CSG Clip Context Slots Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&cuttersUpload, 1u)
            .setResourceUses(clipContextSlotsUploadUses, LengthOf(clipContextSlotsUploadUses))
    );
    ASSERT_TRUE(clipContextSlotsUpload.valid());
    const Graphics::GpuTaskId producer = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_csg_compute_emulation"))
            .setMarkerLabel("AVBOIT Accumulation CSG Compute Emulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&clipContextSlotsUpload, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses))
            .setResourceSetUses(&outputUavSetUse, 1u)
    );
    ASSERT_TRUE(producer.valid());
    const Graphics::GpuTaskId accumulation = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_csg_raster"))
            .setMarkerLabel("AVBOIT Accumulation")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&producer, 1u)
            .setResourceUses(accumulationUses, LengthOf(accumulationUses))
            .setResourceSetUses(&outputVertexBufferSetUse, 1u)
    );
    ASSERT_TRUE(accumulation.valid());
    const Graphics::GpuTaskId finalizer = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/unsplit_avboit_accumulation_csg_finalize"))
            .setMarkerLabel("AVBOIT Accumulation Finalize")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&accumulation, 1u)
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


    EXPECT_TRUE(analysis.hasExplicitEdge(pre, combine));
    EXPECT_TRUE(analysis.hasExplicitEdge(combine, receiverRangesUpload));
    EXPECT_TRUE(analysis.hasExplicitEdge(receiverRangesUpload, cuttersUpload));
    EXPECT_TRUE(analysis.hasExplicitEdge(cuttersUpload, clipContextSlotsUpload));
    EXPECT_TRUE(analysis.hasExplicitEdge(clipContextSlotsUpload, producer));
    EXPECT_TRUE(analysis.hasExplicitEdge(producer, accumulation));
    EXPECT_TRUE(analysis.hasExplicitEdge(accumulation, finalizer));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        receiverRangesUpload,
        producer,
        receiverRanges,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        cuttersUpload,
        producer,
        cutters,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        clipContextSlotsUpload,
        producer,
        clipContextSlots,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    for(const Graphics::GpuGraphResourceId removedInterval : removedIntervals){
        EXPECT_TRUE(HasInferredHazard(
            analysis,
            combine,
            producer,
            removedInterval,
            Graphics::GpuTaskHazardType::ReadAfterWrite
        ));
        EXPECT_TRUE(HasInferredHazard(
            analysis,
            combine,
            accumulation,
            removedInterval,
            Graphics::GpuTaskHazardType::ReadAfterWrite
        ));
    }
    for(const Graphics::GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(HasInferredHazard(
            analysis,
            producer,
            accumulation,
            output,
            Graphics::GpuTaskHazardType::ReadAfterWrite
        ));
    }
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        accumulation,
        finalizer,
        accumColor,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        accumulation,
        finalizer,
        accumExtinction,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    const Graphics::GpuTaskId tasks[] = {
        pre,
        combine,
        receiverRangesUpload,
        cuttersUpload,
        clipContextSlotsUpload,
        producer,
        accumulation,
        finalizer,
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
    EXPECT_TRUE(compiledPlan.tasksSharePacket(producer, accumulation));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(producer, accumulation));
    EXPECT_TRUE(compiledPlan.taskPrecedesOrSharesPacket(accumulation, finalizer));
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    EXPECT_EQ(compiledPacket.queue, queue.id);
    EXPECT_EQ(compiledPacket.dependencyCount, 0u);
    ASSERT_EQ(compiledPacket.taskCount, LengthOf(tasks));
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex)
        EXPECT_EQ(packetTasks[taskIndex], tasks[taskIndex]);
    EXPECT_EQ(packetTasks[5u], producer);
    EXPECT_EQ(packetTasks[6u], accumulation);
    EXPECT_EQ(packetTasks[7u], finalizer);

    const auto hasBufferTransition = [&](const Graphics::GpuTaskId task,
                                         const Graphics::GpuGraphResourceId resource,
                                         const Graphics::ResourceStates::Mask before,
                                         const Graphics::ResourceStates::Mask after){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask && barriers && barrierIndex < compiledTask->prologueBarrierCount;
            ++barrierIndex){
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
    const auto hasTextureTransition = [&](const Graphics::GpuTaskId task,
                                          const Graphics::GpuGraphResourceId resource,
                                          const Graphics::ResourceStates::Mask before,
                                          const Graphics::ResourceStates::Mask after){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask && barriers && barrierIndex < compiledTask->prologueBarrierCount;
            ++barrierIndex){
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
    const auto hasTextureUav = [&](const Graphics::GpuTaskId task,
                                   const Graphics::GpuGraphResourceId resource,
                                   const Graphics::TextureSubresourceSet& range){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask && barriers && barrierIndex < compiledTask->prologueBarrierCount;
            ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
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
        receiverRangesUpload,
        receiverRanges,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        cuttersUpload,
        cutters,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        clipContextSlotsUpload,
        clipContextSlots,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        producer,
        receiverRanges,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        producer,
        cutters,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        producer,
        clipContextSlots,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ConstantBuffer
    ));
    EXPECT_TRUE(hasBufferTransition(
        producer,
        intervalSampleState,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::ConstantBuffer
    ));
    EXPECT_TRUE(hasTextureUav(producer, removedIntervalDepth, removedIntervalRange));
    EXPECT_TRUE(hasTextureUav(producer, removedIntervalCapNormal, removedIntervalRange));
    EXPECT_TRUE(hasTextureUav(producer, removedIntervalData, removedIntervalRange));
    EXPECT_TRUE(hasTextureUav(producer, removedIntervalCount, removedIntervalCountRange));
    for(const Graphics::GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(hasBufferTransition(
            producer,
            output,
            Graphics::ResourceStates::Common,
            Graphics::ResourceStates::UnorderedAccess
        ));
        EXPECT_TRUE(hasBufferTransition(
            accumulation,
            output,
            Graphics::ResourceStates::UnorderedAccess,
            Graphics::ResourceStates::VertexBuffer
        ));
    }
    EXPECT_TRUE(hasTextureTransition(
        accumulation,
        accumColor,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::RenderTarget
    ));
    EXPECT_TRUE(hasTextureTransition(
        accumulation,
        accumExtinction,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::RenderTarget
    ));
    EXPECT_TRUE(hasTextureTransition(
        accumulation,
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

