// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_csg_gbuffer_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, PlansMergedCsgGbufferSpanBuildCombineAndOpaqueSampleUavDependencies){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId capBackNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_cap_back_normal"),
        "CSG Combine Cap Back Normal"
    );
    const Graphics::GpuGraphResourceId intervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_interval_depth"),
        "CSG Combine Interval Depth"
    );
    const Graphics::GpuGraphResourceId intervalId = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_interval_id"),
        "CSG Combine Interval Id"
    );
    const Graphics::GpuGraphResourceId receiverEventData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_span_receiver_event_data"),
        "CSG Span Receiver Event Data"
    );
    const Graphics::GpuGraphResourceId receiverEventCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_span_receiver_event_count"),
        "CSG Span Receiver Event Count"
    );
    const Graphics::GpuGraphResourceId receiverSpanData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_receiver_span_data"),
        "CSG Combine Receiver Span Data"
    );
    const Graphics::GpuGraphResourceId receiverSpanCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_receiver_span_count"),
        "CSG Combine Receiver Span Count"
    );
    const Graphics::GpuGraphResourceId removedIntervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_sample_removed_interval_depth"),
        "CSG Sample Removed Interval Depth"
    );
    const Graphics::GpuGraphResourceId removedIntervalCapNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_sample_removed_interval_cap_normal"),
        "CSG Sample Removed Interval Cap Normal"
    );
    const Graphics::GpuGraphResourceId removedIntervalData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_sample_removed_interval_data"),
        "CSG Sample Removed Interval Data"
    );
    const Graphics::GpuGraphResourceId removedIntervalCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_sample_removed_interval_count"),
        "CSG Sample Removed Interval Count"
    );
    const Graphics::GpuGraphResourceId materialGeometry = AddBufferMetadata(
        graph,
        Name("tests/task_graph/csg_sample_material_geometry"),
        "Opaque CSG Material Geometry"
    );
    ASSERT_TRUE(capBackNormal.valid());
    ASSERT_TRUE(intervalDepth.valid());
    ASSERT_TRUE(intervalId.valid());
    ASSERT_TRUE(receiverEventData.valid());
    ASSERT_TRUE(receiverEventCount.valid());
    ASSERT_TRUE(receiverSpanData.valid());
    ASSERT_TRUE(receiverSpanCount.valid());
    ASSERT_TRUE(removedIntervalDepth.valid());
    ASSERT_TRUE(removedIntervalCapNormal.valid());
    ASSERT_TRUE(removedIntervalData.valid());
    ASSERT_TRUE(removedIntervalCount.valid());
    ASSERT_TRUE(materialGeometry.valid());

    const Graphics::TextureSubresourceSet peelRange(0u, 1u, 0u, 4u);
    const Graphics::TextureSubresourceSet receiverEventRange(0u, 1u, 0u, 32u);
    const Graphics::TextureSubresourceSet receiverEventCountRange(0u, 1u, 0u, 1u);
    const Graphics::TextureSubresourceSet receiverSpanRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet receiverSpanCountRange(0u, 1u, 0u, 1u);
    const Graphics::TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const Graphics::GpuTaskResourceUse producerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = capBackNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalId,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse spanBuildUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse combineUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = capBackNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalId,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
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
    const Graphics::GpuTaskResourceUse sampleUses[] = {
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
    const Graphics::GpuGraphResourceSetId sampleMaterialGeometrySet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/csg_sample_material_geometry_set"))
            .setMarkerLabel("Opaque CSG Material Geometry")
            .setMembers(&materialGeometry, 1u)
    );
    ASSERT_TRUE(sampleMaterialGeometrySet.valid());
    const Graphics::GpuTaskResourceSetUse sampleMaterialGeometrySetUses[] = {
        Graphics::GpuTaskResourceSetUse{
            .resourceSet = sampleMaterialGeometrySet,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    producerScheduling.forceSubmissionBoundary = false;
    producerScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/csg_gbuffer"))
        .setMarkerLabel("Opaque CSG G-buffer")
        .setQueue(graphicsRequest)
        .setScheduling(producerScheduling)
        .setResourceUses(producerUses, LengthOf(producerUses))
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskSchedulingHint spanBuildScheduling;
    spanBuildScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    spanBuildScheduling.forceSubmissionBoundary = false;
    spanBuildScheduling.allowPacketMerge = true;
    spanBuildScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc spanBuildDesc;
    spanBuildDesc
        .setIdentity(Name("tests/task_graph/csg_receiver_span_build"))
        .setMarkerLabel("Opaque CSG Receiver Span Build")
        .setQueue(graphicsRequest)
        .setScheduling(spanBuildScheduling)
        .setDependencies(&producer, 1u)
        .setResourceUses(spanBuildUses, LengthOf(spanBuildUses))
    ;
    const Graphics::GpuTaskId spanBuild = graph.addTask(spanBuildDesc);
    ASSERT_TRUE(spanBuild.valid());

    Graphics::GpuTaskSchedulingHint combineScheduling;
    combineScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    combineScheduling.forceSubmissionBoundary = false;
    combineScheduling.allowPacketMerge = true;
    combineScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc combineDesc;
    combineDesc
        .setIdentity(Name("tests/task_graph/csg_interval_combine"))
        .setMarkerLabel("CSG Interval Combine")
        .setQueue(graphicsRequest)
        .setScheduling(combineScheduling)
        .setDependencies(&spanBuild, 1u)
        .setResourceUses(combineUses, LengthOf(combineUses))
    ;
    const Graphics::GpuTaskId combine = graph.addTask(combineDesc);
    ASSERT_TRUE(combine.valid());

    Graphics::GpuTaskSchedulingHint sampleScheduling;
    sampleScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    sampleScheduling.forceSubmissionBoundary = false;
    sampleScheduling.allowPacketMerge = true;
    sampleScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc sampleDesc;
    sampleDesc
        .setIdentity(Name("tests/task_graph/csg_interval_opaque_sample"))
        .setMarkerLabel("Opaque CSG Interval Sample")
        .setQueue(graphicsRequest)
        .setScheduling(sampleScheduling)
        .setDependencies(&combine, 1u)
        .setResourceUses(sampleUses, LengthOf(sampleUses))
        .setResourceSetUses(sampleMaterialGeometrySetUses, LengthOf(sampleMaterialGeometrySetUses))
    ;
    const Graphics::GpuTaskId sample = graph.addTask(sampleDesc);
    ASSERT_TRUE(sample.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
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

    ASSERT_NE(FindEdge(analysis, producer, spanBuild), nullptr);
    ASSERT_NE(FindEdge(analysis, producer, combine), nullptr);
    ASSERT_NE(FindEdge(analysis, spanBuild, combine), nullptr);
    ASSERT_NE(FindEdge(analysis, combine, sample), nullptr);
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        spanBuild,
        receiverEventData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        spanBuild,
        receiverEventCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        combine,
        capBackNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        combine,
        intervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        combine,
        intervalId,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        spanBuild,
        combine,
        receiverSpanData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        spanBuild,
        combine,
        receiverSpanCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        sample,
        removedIntervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        sample,
        removedIntervalCapNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        sample,
        removedIntervalData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        sample,
        removedIntervalCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    ASSERT_EQ(compiledPlan.packetCount(), 1u);
    const Graphics::GpuSubmissionPacketId producerPacket = compiledPlan.packetForTask(producer);
    const Graphics::GpuSubmissionPacketId spanBuildPacket = compiledPlan.packetForTask(spanBuild);
    const Graphics::GpuSubmissionPacketId combinePacket = compiledPlan.packetForTask(combine);
    const Graphics::GpuSubmissionPacketId samplePacket = compiledPlan.packetForTask(sample);
    ASSERT_TRUE(producerPacket.valid());
    ASSERT_TRUE(spanBuildPacket.valid());
    ASSERT_TRUE(combinePacket.valid());
    ASSERT_EQ(spanBuildPacket, producerPacket);
    ASSERT_EQ(combinePacket, producerPacket);
    ASSERT_EQ(samplePacket, combinePacket);
    const Graphics::GpuSubmissionPacket& packet = *compiledPlan.packet(combinePacket).plan;
    ASSERT_EQ(packet.taskCount, 4u);
    ASSERT_NE(compiledPlan.packet(combinePacket).tasks, nullptr);
    EXPECT_EQ(compiledPlan.packet(combinePacket).tasks[0u], producer);
    EXPECT_EQ(compiledPlan.packet(combinePacket).tasks[1u], spanBuild);
    EXPECT_EQ(compiledPlan.packet(combinePacket).tasks[2u], combine);
    EXPECT_EQ(compiledPlan.packet(combinePacket).tasks[3u], sample);
    EXPECT_EQ(packet.dependencyCount, 0u);

    const Graphics::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
    const Graphics::GpuCompiledTask* const compiledSpanBuild = compiledPlan.findTask(spanBuild).plan;
    const Graphics::GpuCompiledTask* const compiledCombine = compiledPlan.findTask(combine).plan;
    const Graphics::GpuCompiledTask* const compiledSample = compiledPlan.findTask(sample).plan;
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledSpanBuild, nullptr);
    ASSERT_NE(compiledCombine, nullptr);
    ASSERT_NE(compiledSample, nullptr);
    ASSERT_EQ(compiledProducer->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledSpanBuild->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledCombine->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledSample->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledProducer->prologueBarrierCount, 5u);
    ASSERT_EQ(compiledSpanBuild->prologueBarrierCount, 4u);
    ASSERT_EQ(compiledCombine->prologueBarrierCount, 9u);
    ASSERT_EQ(compiledSample->prologueBarrierCount, 5u);
    const Graphics::GpuCompiledBarrier* const producerBarriers = compiledPlan.findTask(producer).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const spanBuildBarriers = compiledPlan.findTask(spanBuild).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const combineBarriers = compiledPlan.findTask(combine).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const sampleBarriers = compiledPlan.findTask(sample).prologueBarriers;
    ASSERT_NE(producerBarriers, nullptr);
    ASSERT_NE(spanBuildBarriers, nullptr);
    ASSERT_NE(combineBarriers, nullptr);
    ASSERT_NE(sampleBarriers, nullptr);
    const auto hasProducerTransition = [&](const Graphics::GpuGraphResourceId resource, const Graphics::TextureSubresourceSet& range){
        for(u32 barrierIndex = 0u; barrierIndex < compiledProducer->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = producerBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::Common
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    const auto hasSpanBuildTransition = [&](const Graphics::GpuGraphResourceId resource, const Graphics::TextureSubresourceSet& range){
        for(u32 barrierIndex = 0u; barrierIndex < compiledSpanBuild->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = spanBuildBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::Common
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    const auto hasSpanBuildUav = [&](const Graphics::GpuGraphResourceId resource, const Graphics::TextureSubresourceSet& range){
        for(u32 barrierIndex = 0u; barrierIndex < compiledSpanBuild->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = spanBuildBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    const auto hasCombineTransition = [&](const Graphics::GpuGraphResourceId resource, const Graphics::TextureSubresourceSet& range){
        for(u32 barrierIndex = 0u; barrierIndex < compiledCombine->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = combineBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::Common
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    const auto hasCombineUav = [&](const Graphics::GpuGraphResourceId resource, const Graphics::TextureSubresourceSet& range){
        for(u32 barrierIndex = 0u; barrierIndex < compiledCombine->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = combineBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    const auto hasSampleUav = [&](const Graphics::GpuGraphResourceId resource, const Graphics::TextureSubresourceSet& range){
        for(u32 barrierIndex = 0u; barrierIndex < compiledSample->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = sampleBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    const auto hasSampleMaterialGeometryTransition = [&]{
        for(u32 barrierIndex = 0u; barrierIndex < compiledSample->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = sampleBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == materialGeometry
                && barrier.before == Graphics::ResourceStates::Common
                && barrier.after == Graphics::ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasProducerTransition(capBackNormal, peelRange));
    EXPECT_TRUE(hasProducerTransition(intervalDepth, peelRange));
    EXPECT_TRUE(hasProducerTransition(intervalId, peelRange));
    EXPECT_TRUE(hasProducerTransition(receiverEventData, receiverEventRange));
    EXPECT_TRUE(hasProducerTransition(receiverEventCount, receiverEventCountRange));
    EXPECT_TRUE(hasSpanBuildUav(receiverEventData, receiverEventRange));
    EXPECT_TRUE(hasSpanBuildUav(receiverEventCount, receiverEventCountRange));
    EXPECT_TRUE(hasSpanBuildTransition(receiverSpanData, receiverSpanRange));
    EXPECT_TRUE(hasSpanBuildTransition(receiverSpanCount, receiverSpanCountRange));
    EXPECT_TRUE(hasCombineUav(capBackNormal, peelRange));
    EXPECT_TRUE(hasCombineUav(intervalDepth, peelRange));
    EXPECT_TRUE(hasCombineUav(intervalId, peelRange));
    EXPECT_TRUE(hasCombineUav(receiverSpanData, receiverSpanRange));
    EXPECT_TRUE(hasCombineUav(receiverSpanCount, receiverSpanCountRange));
    EXPECT_TRUE(hasCombineTransition(removedIntervalDepth, removedIntervalRange));
    EXPECT_TRUE(hasCombineTransition(removedIntervalCapNormal, removedIntervalRange));
    EXPECT_TRUE(hasCombineTransition(removedIntervalData, removedIntervalRange));
    EXPECT_TRUE(hasCombineTransition(removedIntervalCount, removedIntervalCountRange));
    EXPECT_TRUE(hasSampleUav(removedIntervalDepth, removedIntervalRange));
    EXPECT_TRUE(hasSampleUav(removedIntervalCapNormal, removedIntervalRange));
    EXPECT_TRUE(hasSampleUav(removedIntervalData, removedIntervalRange));
    EXPECT_TRUE(hasSampleUav(removedIntervalCount, removedIntervalCountRange));
    EXPECT_TRUE(hasSampleMaterialGeometryTransition());
}

TEST(GpuTaskGraph, PlansCsgGbufferSpanBuildCombineAndSampleUavDependenciesAcrossForcedPacketSplit){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId capBackNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_split_cap_back_normal"),
        "CSG Combine Split Cap Back Normal"
    );
    const Graphics::GpuGraphResourceId intervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_split_interval_depth"),
        "CSG Combine Split Interval Depth"
    );
    const Graphics::GpuGraphResourceId intervalId = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_split_interval_id"),
        "CSG Combine Split Interval Id"
    );
    const Graphics::GpuGraphResourceId receiverEventData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_span_split_receiver_event_data"),
        "CSG Span Split Receiver Event Data"
    );
    const Graphics::GpuGraphResourceId receiverEventCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_span_split_receiver_event_count"),
        "CSG Span Split Receiver Event Count"
    );
    const Graphics::GpuGraphResourceId receiverSpanData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_split_receiver_span_data"),
        "CSG Combine Split Receiver Span Data"
    );
    const Graphics::GpuGraphResourceId receiverSpanCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_split_receiver_span_count"),
        "CSG Combine Split Receiver Span Count"
    );
    const Graphics::GpuGraphResourceId removedIntervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_sample_split_removed_interval_depth"),
        "CSG Sample Split Removed Interval Depth"
    );
    const Graphics::GpuGraphResourceId removedIntervalCapNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_sample_split_removed_interval_cap_normal"),
        "CSG Sample Split Removed Interval Cap Normal"
    );
    const Graphics::GpuGraphResourceId removedIntervalData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_sample_split_removed_interval_data"),
        "CSG Sample Split Removed Interval Data"
    );
    const Graphics::GpuGraphResourceId removedIntervalCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_sample_split_removed_interval_count"),
        "CSG Sample Split Removed Interval Count"
    );
    ASSERT_TRUE(capBackNormal.valid());
    ASSERT_TRUE(intervalDepth.valid());
    ASSERT_TRUE(intervalId.valid());
    ASSERT_TRUE(receiverEventData.valid());
    ASSERT_TRUE(receiverEventCount.valid());
    ASSERT_TRUE(receiverSpanData.valid());
    ASSERT_TRUE(receiverSpanCount.valid());
    ASSERT_TRUE(removedIntervalDepth.valid());
    ASSERT_TRUE(removedIntervalCapNormal.valid());
    ASSERT_TRUE(removedIntervalData.valid());
    ASSERT_TRUE(removedIntervalCount.valid());

    const Graphics::TextureSubresourceSet peelRange(0u, 1u, 0u, 4u);
    const Graphics::TextureSubresourceSet receiverEventRange(0u, 1u, 0u, 32u);
    const Graphics::TextureSubresourceSet receiverEventCountRange(0u, 1u, 0u, 1u);
    const Graphics::TextureSubresourceSet receiverSpanRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet receiverSpanCountRange(0u, 1u, 0u, 1u);
    const Graphics::TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const Graphics::GpuTaskResourceUse producerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = capBackNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalId,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse spanBuildUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse combineUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = capBackNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalId,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
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
    const Graphics::GpuTaskResourceUse sampleUses[] = {
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
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/csg_split_gbuffer"))
        .setMarkerLabel("Opaque CSG G-buffer")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(producerUses, LengthOf(producerUses))
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskDesc spanBuildDesc;
    spanBuildDesc
        .setIdentity(Name("tests/task_graph/csg_receiver_span_split_build"))
        .setMarkerLabel("Opaque CSG Receiver Span Build")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&producer, 1u)
        .setResourceUses(spanBuildUses, LengthOf(spanBuildUses))
    ;
    const Graphics::GpuTaskId spanBuild = graph.addTask(spanBuildDesc);
    ASSERT_TRUE(spanBuild.valid());

    Graphics::GpuTaskDesc combineDesc;
    combineDesc
        .setIdentity(Name("tests/task_graph/csg_interval_split_combine"))
        .setMarkerLabel("CSG Interval Combine")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&spanBuild, 1u)
        .setResourceUses(combineUses, LengthOf(combineUses))
    ;
    const Graphics::GpuTaskId combine = graph.addTask(combineDesc);
    ASSERT_TRUE(combine.valid());

    Graphics::GpuTaskDesc sampleDesc;
    sampleDesc
        .setIdentity(Name("tests/task_graph/csg_interval_split_sample"))
        .setMarkerLabel("Opaque CSG Interval Sample")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&combine, 1u)
        .setResourceUses(sampleUses, LengthOf(sampleUses))
    ;
    const Graphics::GpuTaskId sample = graph.addTask(sampleDesc);
    ASSERT_TRUE(sample.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
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

    ASSERT_NE(FindEdge(analysis, producer, spanBuild), nullptr);
    ASSERT_NE(FindEdge(analysis, producer, combine), nullptr);
    ASSERT_NE(FindEdge(analysis, spanBuild, combine), nullptr);
    ASSERT_NE(FindEdge(analysis, combine, sample), nullptr);
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        spanBuild,
        receiverEventData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        spanBuild,
        receiverEventCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        combine,
        capBackNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        combine,
        intervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        combine,
        intervalId,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        spanBuild,
        combine,
        receiverSpanData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        spanBuild,
        combine,
        receiverSpanCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        sample,
        removedIntervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        sample,
        removedIntervalCapNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        sample,
        removedIntervalData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        sample,
        removedIntervalCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    ASSERT_EQ(compiledPlan.packetCount(), 4u);
    const Graphics::GpuSubmissionPacketId producerPacket = compiledPlan.packetForTask(producer);
    const Graphics::GpuSubmissionPacketId spanBuildPacket = compiledPlan.packetForTask(spanBuild);
    const Graphics::GpuSubmissionPacketId combinePacket = compiledPlan.packetForTask(combine);
    const Graphics::GpuSubmissionPacketId samplePacket = compiledPlan.packetForTask(sample);
    ASSERT_TRUE(producerPacket.valid());
    ASSERT_TRUE(spanBuildPacket.valid());
    ASSERT_TRUE(combinePacket.valid());
    ASSERT_TRUE(samplePacket.valid());
    EXPECT_NE(producerPacket, spanBuildPacket);
    EXPECT_NE(producerPacket, combinePacket);
    EXPECT_NE(spanBuildPacket, combinePacket);
    EXPECT_NE(combinePacket, samplePacket);
    EXPECT_EQ(compiledPlan.packet(producerPacket).plan->taskCount, 1u);
    EXPECT_EQ(compiledPlan.packet(spanBuildPacket).plan->taskCount, 1u);
    EXPECT_EQ(compiledPlan.packet(combinePacket).plan->taskCount, 1u);
    EXPECT_EQ(compiledPlan.packet(samplePacket).plan->taskCount, 1u);
    ASSERT_EQ(compiledPlan.packet(spanBuildPacket).plan->dependencyCount, 1u);
    ASSERT_NE(compiledPlan.packet(spanBuildPacket).dependencies, nullptr);
    EXPECT_EQ(compiledPlan.packet(spanBuildPacket).dependencies[0u].producer, producerPacket);
    ASSERT_EQ(compiledPlan.packet(combinePacket).plan->dependencyCount, 2u);
    const Graphics::GpuPacketDependency* const combineDependencies = compiledPlan.packet(combinePacket).dependencies;
    ASSERT_NE(combineDependencies, nullptr);
    bool combineWaitsForProducer = false;
    bool combineWaitsForSpanBuild = false;
    for(u32 dependencyIndex = 0u; dependencyIndex < compiledPlan.packet(combinePacket).plan->dependencyCount; ++dependencyIndex){
        combineWaitsForProducer = combineWaitsForProducer || combineDependencies[dependencyIndex].producer == producerPacket;
        combineWaitsForSpanBuild = combineWaitsForSpanBuild || combineDependencies[dependencyIndex].producer == spanBuildPacket;
    }
    EXPECT_TRUE(combineWaitsForProducer);
    EXPECT_TRUE(combineWaitsForSpanBuild);
    ASSERT_EQ(compiledPlan.packet(samplePacket).plan->dependencyCount, 1u);
    ASSERT_NE(compiledPlan.packet(samplePacket).dependencies, nullptr);
    EXPECT_EQ(compiledPlan.packet(samplePacket).dependencies[0u].producer, combinePacket);

    const Graphics::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
    const Graphics::GpuCompiledTask* const compiledSpanBuild = compiledPlan.findTask(spanBuild).plan;
    const Graphics::GpuCompiledTask* const compiledCombine = compiledPlan.findTask(combine).plan;
    const Graphics::GpuCompiledTask* const compiledSample = compiledPlan.findTask(sample).plan;
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledSpanBuild, nullptr);
    ASSERT_NE(compiledCombine, nullptr);
    ASSERT_NE(compiledSample, nullptr);
    ASSERT_EQ(compiledProducer->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledProducer->prologueBarrierCount, 5u);
    ASSERT_EQ(compiledSpanBuild->prologueStateSeedCount, 2u);
    ASSERT_EQ(compiledSpanBuild->prologueBarrierCount, 4u);
    ASSERT_EQ(compiledCombine->prologueStateSeedCount, 5u);
    ASSERT_EQ(compiledCombine->prologueBarrierCount, 9u);
    ASSERT_EQ(compiledSample->prologueStateSeedCount, 4u);
    ASSERT_EQ(compiledSample->prologueBarrierCount, 4u);
    const Graphics::GpuPacketStateSeed* const spanBuildSeeds = compiledPlan.findTask(spanBuild).prologueStateSeeds;
    const Graphics::GpuPacketStateSeed* const combineSeeds = compiledPlan.findTask(combine).prologueStateSeeds;
    const Graphics::GpuPacketStateSeed* const sampleSeeds = compiledPlan.findTask(sample).prologueStateSeeds;
    const Graphics::GpuCompiledBarrier* const spanBuildBarriers = compiledPlan.findTask(spanBuild).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const combineBarriers = compiledPlan.findTask(combine).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const sampleBarriers = compiledPlan.findTask(sample).prologueBarriers;
    ASSERT_NE(spanBuildSeeds, nullptr);
    ASSERT_NE(combineSeeds, nullptr);
    ASSERT_NE(sampleSeeds, nullptr);
    ASSERT_NE(spanBuildBarriers, nullptr);
    ASSERT_NE(combineBarriers, nullptr);
    ASSERT_NE(sampleBarriers, nullptr);
    const auto hasStateSeed = [](
        const Graphics::GpuPacketStateSeed* const seeds,
        const u32 count,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::TextureSubresourceSet& range,
        const Graphics::GpuSubmissionPacketId sourcePacket
    ){
        for(u32 seedIndex = 0u; seedIndex < count; ++seedIndex){
            const Graphics::GpuPacketStateSeed& seed = seeds[seedIndex];
            if(
                seed.resource == resource
                && seed.range.textureSubresources == range
                && seed.sourcePacket == sourcePacket
            )
                return true;
        }
        return false;
    };
    const auto hasUav = [](
        const Graphics::GpuCompiledBarrier* const barriers,
        const u32 count,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::TextureSubresourceSet& range
    ){
        for(u32 barrierIndex = 0u; barrierIndex < count; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasStateSeed(
        spanBuildSeeds,
        compiledSpanBuild->prologueStateSeedCount,
        receiverEventData,
        receiverEventRange,
        producerPacket
    ));
    EXPECT_TRUE(hasStateSeed(
        spanBuildSeeds,
        compiledSpanBuild->prologueStateSeedCount,
        receiverEventCount,
        receiverEventCountRange,
        producerPacket
    ));
    EXPECT_TRUE(hasUav(
        spanBuildBarriers,
        compiledSpanBuild->prologueBarrierCount,
        receiverEventData,
        receiverEventRange
    ));
    EXPECT_TRUE(hasUav(
        spanBuildBarriers,
        compiledSpanBuild->prologueBarrierCount,
        receiverEventCount,
        receiverEventCountRange
    ));
    EXPECT_TRUE(hasStateSeed(
        combineSeeds,
        compiledCombine->prologueStateSeedCount,
        capBackNormal,
        peelRange,
        producerPacket
    ));
    EXPECT_TRUE(hasStateSeed(
        combineSeeds,
        compiledCombine->prologueStateSeedCount,
        intervalDepth,
        peelRange,
        producerPacket
    ));
    EXPECT_TRUE(hasStateSeed(
        combineSeeds,
        compiledCombine->prologueStateSeedCount,
        intervalId,
        peelRange,
        producerPacket
    ));
    EXPECT_TRUE(hasStateSeed(
        combineSeeds,
        compiledCombine->prologueStateSeedCount,
        receiverSpanData,
        receiverSpanRange,
        spanBuildPacket
    ));
    EXPECT_TRUE(hasStateSeed(
        combineSeeds,
        compiledCombine->prologueStateSeedCount,
        receiverSpanCount,
        receiverSpanCountRange,
        spanBuildPacket
    ));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, capBackNormal, peelRange));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, intervalDepth, peelRange));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, intervalId, peelRange));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, receiverSpanData, receiverSpanRange));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, receiverSpanCount, receiverSpanCountRange));
    EXPECT_TRUE(hasStateSeed(
        sampleSeeds,
        compiledSample->prologueStateSeedCount,
        removedIntervalDepth,
        removedIntervalRange,
        combinePacket
    ));
    EXPECT_TRUE(hasStateSeed(
        sampleSeeds,
        compiledSample->prologueStateSeedCount,
        removedIntervalCapNormal,
        removedIntervalRange,
        combinePacket
    ));
    EXPECT_TRUE(hasStateSeed(
        sampleSeeds,
        compiledSample->prologueStateSeedCount,
        removedIntervalData,
        removedIntervalRange,
        combinePacket
    ));
    EXPECT_TRUE(hasStateSeed(
        sampleSeeds,
        compiledSample->prologueStateSeedCount,
        removedIntervalCount,
        removedIntervalCountRange,
        combinePacket
    ));
    EXPECT_TRUE(hasUav(sampleBarriers, compiledSample->prologueBarrierCount, removedIntervalDepth, removedIntervalRange));
    EXPECT_TRUE(hasUav(sampleBarriers, compiledSample->prologueBarrierCount, removedIntervalCapNormal, removedIntervalRange));
    EXPECT_TRUE(hasUav(sampleBarriers, compiledSample->prologueBarrierCount, removedIntervalData, removedIntervalRange));
    EXPECT_TRUE(hasUav(sampleBarriers, compiledSample->prologueBarrierCount, removedIntervalCount, removedIntervalCountRange));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

