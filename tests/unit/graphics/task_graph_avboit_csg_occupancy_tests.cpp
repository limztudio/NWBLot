// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_avboit_csg_occupancy_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, PlansAvboitCsgIntervalProducerSpanBuildCombineToOccupancyUavDependencies){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId capBackNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_cap_back_normal"),
        "AVBOIT CSG Cap Back Normal"
    );
    const Graphics::GpuGraphResourceId intervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_interval_depth"),
        "AVBOIT CSG Interval Depth"
    );
    const Graphics::GpuGraphResourceId intervalId = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_interval_id"),
        "AVBOIT CSG Interval Id"
    );
    const Graphics::GpuGraphResourceId receiverEventData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_receiver_event_data"),
        "AVBOIT CSG Receiver Event Data"
    );
    const Graphics::GpuGraphResourceId receiverEventCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_receiver_event_count"),
        "AVBOIT CSG Receiver Event Count"
    );
    const Graphics::GpuGraphResourceId receiverSpanData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_receiver_span_data"),
        "AVBOIT CSG Receiver Span Data"
    );
    const Graphics::GpuGraphResourceId receiverSpanCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_receiver_span_count"),
        "AVBOIT CSG Receiver Span Count"
    );
    const Graphics::GpuGraphResourceId removedIntervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_removed_interval_depth"),
        "AVBOIT CSG Removed Interval Depth"
    );
    const Graphics::GpuGraphResourceId removedIntervalCapNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_removed_interval_cap_normal"),
        "AVBOIT CSG Removed Interval Cap Normal"
    );
    const Graphics::GpuGraphResourceId removedIntervalData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_removed_interval_data"),
        "AVBOIT CSG Removed Interval Data"
    );
    const Graphics::GpuGraphResourceId removedIntervalCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_removed_interval_count"),
        "AVBOIT CSG Removed Interval Count"
    );
    const Graphics::GpuGraphResourceId coverage = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_coverage"),
        "AVBOIT Coverage"
    );
    const Graphics::GpuGraphResourceId materialGeometry = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_material_geometry"),
        "Transparent CSG Material Geometry"
    );
    const Graphics::GpuGraphResourceId occupancyMaterialGeometry = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_occupancy_material_geometry"),
        "AVBOIT Occupancy Material Geometry"
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
    ASSERT_TRUE(coverage.valid());
    ASSERT_TRUE(materialGeometry.valid());
    ASSERT_TRUE(occupancyMaterialGeometry.valid());

    const Graphics::TextureSubresourceSet peelRange(0u, 1u, 0u, 4u);
    const Graphics::TextureSubresourceSet receiverEventRange(0u, 1u, 0u, 32u);
    const Graphics::TextureSubresourceSet receiverEventCountRange(0u, 1u, 0u, 1u);
    const Graphics::TextureSubresourceSet receiverSpanRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet receiverSpanCountRange(0u, 1u, 0u, 1u);
    const Graphics::TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const Graphics::GpuTaskResourceUse preUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = capBackNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalId,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuGraphResourceSetId preMaterialGeometrySet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/avboit_csg_material_geometry_set"))
            .setMarkerLabel("Transparent CSG Material Geometry")
            .setMembers(&materialGeometry, 1u)
    );
    ASSERT_TRUE(preMaterialGeometrySet.valid());
    const Graphics::GpuTaskResourceSetUse preMaterialGeometrySetUses[] = {
        Graphics::GpuTaskResourceSetUse{
            .resourceSet = preMaterialGeometrySet,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
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
    const Graphics::GpuTaskResourceUse clearUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse occupancyUses[] = {
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
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuGraphResourceSetId occupancyMaterialGeometrySet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/avboit_occupancy_material_geometry_set"))
            .setMarkerLabel("AVBOIT Occupancy Material Geometry")
            .setMembers(&occupancyMaterialGeometry, 1u)
    );
    ASSERT_TRUE(occupancyMaterialGeometrySet.valid());
    const Graphics::GpuTaskResourceSetUse occupancyMaterialGeometrySetUses[] = {
        Graphics::GpuTaskResourceSetUse{
            .resourceSet = occupancyMaterialGeometrySet,
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
    Graphics::GpuTaskSchedulingHint preScheduling;
    preScheduling.cost = Graphics::GpuTaskCostHint::Large;
    preScheduling.forceSubmissionBoundary = false;
    preScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc preDesc;
    preDesc
        .setIdentity(Name("tests/task_graph/avboit_transparent_csg_pre"))
        .setMarkerLabel("Transparent CSG Pre")
        .setQueue(graphicsRequest)
        .setScheduling(preScheduling)
        .setResourceUses(preUses, LengthOf(preUses))
        .setResourceSetUses(preMaterialGeometrySetUses, LengthOf(preMaterialGeometrySetUses))
    ;
    const Graphics::GpuTaskId pre = graph.addTask(preDesc);
    ASSERT_TRUE(pre.valid());

    Graphics::GpuTaskSchedulingHint spanBuildScheduling = preScheduling;
    spanBuildScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    spanBuildScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc spanBuildDesc;
    spanBuildDesc
        .setIdentity(Name("tests/task_graph/avboit_transparent_csg_receiver_span"))
        .setMarkerLabel("Transparent CSG Receiver Span")
        .setQueue(graphicsRequest)
        .setScheduling(spanBuildScheduling)
        .setDependencies(&pre, 1u)
        .setResourceUses(spanBuildUses, LengthOf(spanBuildUses))
    ;
    const Graphics::GpuTaskId spanBuild = graph.addTask(spanBuildDesc);
    ASSERT_TRUE(spanBuild.valid());

    Graphics::GpuTaskSchedulingHint combineScheduling = preScheduling;
    combineScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    combineScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc combineDesc;
    combineDesc
        .setIdentity(Name("tests/task_graph/avboit_transparent_csg_combine"))
        .setMarkerLabel("Transparent CSG Interval Combine")
        .setQueue(graphicsRequest)
        .setScheduling(combineScheduling)
        .setDependencies(&spanBuild, 1u)
        .setResourceUses(combineUses, LengthOf(combineUses))
    ;
    const Graphics::GpuTaskId combine = graph.addTask(combineDesc);
    ASSERT_TRUE(combine.valid());

    Graphics::GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    clearScheduling.forceSubmissionBoundary = false;
    clearScheduling.allowPacketMerge = true;
    clearScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc clearDesc;
    clearDesc
        .setIdentity(Name("tests/task_graph/avboit_clear"))
        .setMarkerLabel("AVBOIT Clear")
        .setQueue(graphicsRequest)
        .setScheduling(clearScheduling)
        .setDependencies(&combine, 1u)
        .setResourceUses(clearUses, LengthOf(clearUses))
    ;
    const Graphics::GpuTaskId clear = graph.addTask(clearDesc);
    ASSERT_TRUE(clear.valid());

    Graphics::GpuTaskSchedulingHint occupancyScheduling;
    occupancyScheduling.cost = Graphics::GpuTaskCostHint::Large;
    occupancyScheduling.forceSubmissionBoundary = false;
    occupancyScheduling.allowPacketMerge = true;
    occupancyScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc occupancyDesc;
    occupancyDesc
        .setIdentity(Name("tests/task_graph/avboit_occupancy"))
        .setMarkerLabel("AVBOIT Occupancy")
        .setQueue(graphicsRequest)
        .setScheduling(occupancyScheduling)
        .setDependencies(&clear, 1u)
        .setResourceUses(occupancyUses, LengthOf(occupancyUses))
        .setResourceSetUses(occupancyMaterialGeometrySetUses, LengthOf(occupancyMaterialGeometrySetUses))
    ;
    const Graphics::GpuTaskId occupancy = graph.addTask(occupancyDesc);
    ASSERT_TRUE(occupancy.valid());

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

    ASSERT_NE(FindEdge(analysis, pre, spanBuild), nullptr);
    ASSERT_NE(FindEdge(analysis, pre, combine), nullptr);
    ASSERT_NE(FindEdge(analysis, spanBuild, combine), nullptr);
    ASSERT_NE(FindEdge(analysis, combine, clear), nullptr);
    ASSERT_NE(FindEdge(analysis, combine, occupancy), nullptr);
    ASSERT_NE(FindEdge(analysis, clear, occupancy), nullptr);
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
        spanBuild,
        receiverEventData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
        spanBuild,
        receiverEventCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
        combine,
        capBackNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
        combine,
        intervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
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
        occupancy,
        removedIntervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        occupancy,
        removedIntervalCapNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        occupancy,
        removedIntervalData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        occupancy,
        removedIntervalCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        clear,
        occupancy,
        coverage,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    ASSERT_EQ(compiledPlan.packetCount(), 1u);
    const Graphics::GpuSubmissionPacketId prePacket = compiledPlan.packetForTask(pre);
    const Graphics::GpuSubmissionPacketId spanBuildPacket = compiledPlan.packetForTask(spanBuild);
    const Graphics::GpuSubmissionPacketId combinePacket = compiledPlan.packetForTask(combine);
    const Graphics::GpuSubmissionPacketId clearPacket = compiledPlan.packetForTask(clear);
    const Graphics::GpuSubmissionPacketId occupancyPacket = compiledPlan.packetForTask(occupancy);
    ASSERT_TRUE(prePacket.valid());
    EXPECT_EQ(spanBuildPacket, prePacket);
    EXPECT_EQ(combinePacket, prePacket);
    EXPECT_EQ(clearPacket, prePacket);
    EXPECT_EQ(occupancyPacket, prePacket);
    const Graphics::GpuSubmissionPacket& packet = *compiledPlan.packet(prePacket).plan;
    ASSERT_EQ(packet.taskCount, 5u);
    ASSERT_NE(compiledPlan.packet(prePacket).tasks, nullptr);
    EXPECT_EQ(compiledPlan.packet(prePacket).tasks[0u], pre);
    EXPECT_EQ(compiledPlan.packet(prePacket).tasks[1u], spanBuild);
    EXPECT_EQ(compiledPlan.packet(prePacket).tasks[2u], combine);
    EXPECT_EQ(compiledPlan.packet(prePacket).tasks[3u], clear);
    EXPECT_EQ(compiledPlan.packet(prePacket).tasks[4u], occupancy);
    EXPECT_EQ(packet.dependencyCount, 0u);

    const Graphics::GpuCompiledTask* const compiledPre = compiledPlan.findTask(pre).plan;
    const Graphics::GpuCompiledTask* const compiledSpanBuild = compiledPlan.findTask(spanBuild).plan;
    const Graphics::GpuCompiledTask* const compiledCombine = compiledPlan.findTask(combine).plan;
    const Graphics::GpuCompiledTask* const compiledClear = compiledPlan.findTask(clear).plan;
    const Graphics::GpuCompiledTask* const compiledOccupancy = compiledPlan.findTask(occupancy).plan;
    ASSERT_NE(compiledPre, nullptr);
    ASSERT_NE(compiledSpanBuild, nullptr);
    ASSERT_NE(compiledCombine, nullptr);
    ASSERT_NE(compiledClear, nullptr);
    ASSERT_NE(compiledOccupancy, nullptr);
    EXPECT_EQ(compiledPre->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledSpanBuild->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledCombine->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledClear->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledOccupancy->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledPre->prologueBarrierCount, 6u);
    EXPECT_EQ(compiledSpanBuild->prologueBarrierCount, 4u);
    EXPECT_EQ(compiledCombine->prologueBarrierCount, 9u);
    EXPECT_EQ(compiledClear->prologueBarrierCount, 1u);
    EXPECT_EQ(compiledOccupancy->prologueBarrierCount, 6u);
    const Graphics::GpuCompiledBarrier* const preBarriers = compiledPlan.findTask(pre).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const spanBuildBarriers = compiledPlan.findTask(spanBuild).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const combineBarriers = compiledPlan.findTask(combine).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const clearBarriers = compiledPlan.findTask(clear).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const occupancyBarriers = compiledPlan.findTask(occupancy).prologueBarriers;
    ASSERT_NE(preBarriers, nullptr);
    ASSERT_NE(spanBuildBarriers, nullptr);
    ASSERT_NE(combineBarriers, nullptr);
    ASSERT_NE(clearBarriers, nullptr);
    ASSERT_NE(occupancyBarriers, nullptr);
    const auto hasTextureTransition = [](
        const Graphics::GpuCompiledBarrier* const barriers,
        const u32 count,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::TextureSubresourceSet& range
    ){
        for(u32 barrierIndex = 0u; barrierIndex < count; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
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
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre->prologueBarrierCount, capBackNormal, peelRange));
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre->prologueBarrierCount, intervalDepth, peelRange));
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre->prologueBarrierCount, intervalId, peelRange));
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre->prologueBarrierCount, receiverEventData, receiverEventRange));
    EXPECT_TRUE(hasTextureTransition(
        preBarriers,
        compiledPre->prologueBarrierCount,
        receiverEventCount,
        receiverEventCountRange
    ));
    bool materialGeometryTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledPre->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = preBarriers[barrierIndex];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.resource == materialGeometry
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::ShaderResource
        )
            materialGeometryTransition = true;
    }
    EXPECT_TRUE(materialGeometryTransition);
    EXPECT_TRUE(hasUav(spanBuildBarriers, compiledSpanBuild->prologueBarrierCount, receiverEventData, receiverEventRange));
    EXPECT_TRUE(hasUav(
        spanBuildBarriers,
        compiledSpanBuild->prologueBarrierCount,
        receiverEventCount,
        receiverEventCountRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        spanBuildBarriers,
        compiledSpanBuild->prologueBarrierCount,
        receiverSpanData,
        receiverSpanRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        spanBuildBarriers,
        compiledSpanBuild->prologueBarrierCount,
        receiverSpanCount,
        receiverSpanCountRange
    ));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, capBackNormal, peelRange));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, intervalDepth, peelRange));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, intervalId, peelRange));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, receiverSpanData, receiverSpanRange));
    EXPECT_TRUE(hasUav(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        receiverSpanCount,
        receiverSpanCountRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        removedIntervalDepth,
        removedIntervalRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        removedIntervalCapNormal,
        removedIntervalRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        removedIntervalData,
        removedIntervalRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        removedIntervalCount,
        removedIntervalCountRange
    ));
    EXPECT_TRUE(hasUav(occupancyBarriers, compiledOccupancy->prologueBarrierCount, removedIntervalDepth, removedIntervalRange));
    EXPECT_TRUE(hasUav(
        occupancyBarriers,
        compiledOccupancy->prologueBarrierCount,
        removedIntervalCapNormal,
        removedIntervalRange
    ));
    EXPECT_TRUE(hasUav(occupancyBarriers, compiledOccupancy->prologueBarrierCount, removedIntervalData, removedIntervalRange));
    EXPECT_TRUE(hasUav(
        occupancyBarriers,
        compiledOccupancy->prologueBarrierCount,
        removedIntervalCount,
        removedIntervalCountRange
    ));
    EXPECT_EQ(clearBarriers[0u].type, Graphics::GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(clearBarriers[0u].resource, coverage);
    EXPECT_EQ(clearBarriers[0u].before, Graphics::ResourceStates::Common);
    EXPECT_EQ(clearBarriers[0u].after, Graphics::ResourceStates::CopyDest);
    bool coverageTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledOccupancy->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = occupancyBarriers[barrierIndex];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.resource == coverage
            && barrier.before == Graphics::ResourceStates::CopyDest
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
        )
            coverageTransition = true;
    }
    bool occupancyMaterialGeometryTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledOccupancy->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = occupancyBarriers[barrierIndex];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.resource == occupancyMaterialGeometry
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::ShaderResource
        )
            occupancyMaterialGeometryTransition = true;
    }
    EXPECT_TRUE(coverageTransition);
    EXPECT_TRUE(occupancyMaterialGeometryTransition);
}

TEST(GpuTaskGraph, SeedsSplitAvboitCsgReceiverSpanBuildCombineAndOccupancyUavDependencies){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId capBackNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_cap_back_normal"),
        "AVBOIT CSG Split Cap Back Normal"
    );
    const Graphics::GpuGraphResourceId intervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_interval_depth"),
        "AVBOIT CSG Split Interval Depth"
    );
    const Graphics::GpuGraphResourceId intervalId = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_interval_id"),
        "AVBOIT CSG Split Interval Id"
    );
    const Graphics::GpuGraphResourceId receiverEventData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_receiver_event_data"),
        "AVBOIT CSG Split Receiver Event Data"
    );
    const Graphics::GpuGraphResourceId receiverEventCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_receiver_event_count"),
        "AVBOIT CSG Split Receiver Event Count"
    );
    const Graphics::GpuGraphResourceId receiverSpanData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_receiver_span_data"),
        "AVBOIT CSG Split Receiver Span Data"
    );
    const Graphics::GpuGraphResourceId receiverSpanCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_receiver_span_count"),
        "AVBOIT CSG Split Receiver Span Count"
    );
    const Graphics::GpuGraphResourceId removedIntervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_removed_interval_depth"),
        "AVBOIT CSG Split Removed Interval Depth"
    );
    const Graphics::GpuGraphResourceId removedIntervalCapNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_removed_interval_cap_normal"),
        "AVBOIT CSG Split Removed Interval Cap Normal"
    );
    const Graphics::GpuGraphResourceId removedIntervalData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_removed_interval_data"),
        "AVBOIT CSG Split Removed Interval Data"
    );
    const Graphics::GpuGraphResourceId removedIntervalCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_removed_interval_count"),
        "AVBOIT CSG Split Removed Interval Count"
    );
    const Graphics::GpuGraphResourceId coverage = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_coverage"),
        "AVBOIT CSG Split Coverage"
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
    ASSERT_TRUE(coverage.valid());

    const Graphics::TextureSubresourceSet peelRange(0u, 1u, 0u, 4u);
    const Graphics::TextureSubresourceSet receiverEventRange(0u, 1u, 0u, 32u);
    const Graphics::TextureSubresourceSet receiverEventCountRange(0u, 1u, 0u, 1u);
    const Graphics::TextureSubresourceSet receiverSpanRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet receiverSpanCountRange(0u, 1u, 0u, 1u);
    const Graphics::TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const auto textureUse = [](
        const Graphics::GpuGraphResourceId resource,
        const Graphics::TextureSubresourceSet& range,
        const Graphics::GpuTaskResourceAccess::Enum access
    ){
        return Graphics::GpuTaskResourceUse{
            .resource = resource,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = range },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = access,
        };
    };
    const Graphics::GpuTaskResourceUse preUses[] = {
        textureUse(capBackNormal, peelRange, Graphics::GpuTaskResourceAccess::ReadWrite),
        textureUse(intervalDepth, peelRange, Graphics::GpuTaskResourceAccess::ReadWrite),
        textureUse(intervalId, peelRange, Graphics::GpuTaskResourceAccess::ReadWrite),
        textureUse(receiverEventData, receiverEventRange, Graphics::GpuTaskResourceAccess::ReadWrite),
        textureUse(receiverEventCount, receiverEventCountRange, Graphics::GpuTaskResourceAccess::ReadWrite),
    };
    const Graphics::GpuTaskResourceUse spanBuildUses[] = {
        textureUse(receiverEventData, receiverEventRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(receiverEventCount, receiverEventCountRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(receiverSpanData, receiverSpanRange, Graphics::GpuTaskResourceAccess::Write),
        textureUse(receiverSpanCount, receiverSpanCountRange, Graphics::GpuTaskResourceAccess::Write),
    };
    const Graphics::GpuTaskResourceUse combineUses[] = {
        textureUse(capBackNormal, peelRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(intervalDepth, peelRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(intervalId, peelRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(receiverSpanData, receiverSpanRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(receiverSpanCount, receiverSpanCountRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(removedIntervalDepth, removedIntervalRange, Graphics::GpuTaskResourceAccess::Write),
        textureUse(removedIntervalCapNormal, removedIntervalRange, Graphics::GpuTaskResourceAccess::Write),
        textureUse(removedIntervalData, removedIntervalRange, Graphics::GpuTaskResourceAccess::Write),
        textureUse(removedIntervalCount, removedIntervalCountRange, Graphics::GpuTaskResourceAccess::Write),
    };
    const Graphics::GpuTaskResourceUse clearUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse occupancyUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        textureUse(removedIntervalDepth, removedIntervalRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(removedIntervalCapNormal, removedIntervalRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(removedIntervalData, removedIntervalRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(removedIntervalCount, removedIntervalCountRange, Graphics::GpuTaskResourceAccess::Read),
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
    const auto addTask = [&](const Name identity,
                             const char* const label,
                             const Graphics::GpuTaskId* const dependencies,
                             const u32 dependencyCount,
                             const Graphics::GpuTaskResourceUse* const uses,
                             const u32 useCount){
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(graphicsRequest)
            .setScheduling(boundaryScheduling)
            .setDependencies(dependencies, dependencyCount)
            .setResourceUses(uses, useCount)
        ;
        return graph.addTask(desc);
    };
    const Graphics::GpuTaskId pre = addTask(
        Name("tests/task_graph/avboit_csg_split_pre"),
        "Transparent CSG Pre",
        nullptr,
        0u,
        preUses,
        LengthOf(preUses)
    );
    ASSERT_TRUE(pre.valid());
    const Graphics::GpuTaskId spanBuild = addTask(
        Name("tests/task_graph/avboit_csg_split_receiver_span"),
        "Transparent CSG Receiver Span",
        &pre,
        1u,
        spanBuildUses,
        LengthOf(spanBuildUses)
    );
    ASSERT_TRUE(spanBuild.valid());
    const Graphics::GpuTaskId combine = addTask(
        Name("tests/task_graph/avboit_csg_split_combine"),
        "Transparent CSG Interval Combine",
        &spanBuild,
        1u,
        combineUses,
        LengthOf(combineUses)
    );
    ASSERT_TRUE(combine.valid());
    const Graphics::GpuTaskId clear = addTask(
        Name("tests/task_graph/avboit_csg_split_clear"),
        "AVBOIT Clear",
        &combine,
        1u,
        clearUses,
        LengthOf(clearUses)
    );
    ASSERT_TRUE(clear.valid());
    const Graphics::GpuTaskId occupancy = addTask(
        Name("tests/task_graph/avboit_csg_split_occupancy"),
        "AVBOIT CSG Occupancy",
        &clear,
        1u,
        occupancyUses,
        LengthOf(occupancyUses)
    );
    ASSERT_TRUE(occupancy.valid());

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

    ASSERT_NE(FindEdge(analysis, pre, spanBuild), nullptr);
    ASSERT_NE(FindEdge(analysis, pre, combine), nullptr);
    ASSERT_NE(FindEdge(analysis, spanBuild, combine), nullptr);
    ASSERT_NE(FindEdge(analysis, combine, clear), nullptr);
    ASSERT_NE(FindEdge(analysis, combine, occupancy), nullptr);
    ASSERT_NE(FindEdge(analysis, clear, occupancy), nullptr);
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
        spanBuild,
        receiverEventData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
        spanBuild,
        receiverEventCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
        combine,
        capBackNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
        combine,
        intervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
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
        occupancy,
        removedIntervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        occupancy,
        removedIntervalCapNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        occupancy,
        removedIntervalData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        occupancy,
        removedIntervalCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        clear,
        occupancy,
        coverage,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_EQ(compiledPlan.packetCount(), 5u);
    const Graphics::GpuSubmissionPacketId prePacket = compiledPlan.packetForTask(pre);
    const Graphics::GpuSubmissionPacketId spanBuildPacket = compiledPlan.packetForTask(spanBuild);
    const Graphics::GpuSubmissionPacketId combinePacket = compiledPlan.packetForTask(combine);
    const Graphics::GpuSubmissionPacketId clearPacket = compiledPlan.packetForTask(clear);
    const Graphics::GpuSubmissionPacketId occupancyPacket = compiledPlan.packetForTask(occupancy);
    ASSERT_TRUE(prePacket.valid());
    ASSERT_TRUE(spanBuildPacket.valid());
    ASSERT_TRUE(combinePacket.valid());
    ASSERT_TRUE(clearPacket.valid());
    ASSERT_TRUE(occupancyPacket.valid());
    EXPECT_NE(prePacket, spanBuildPacket);
    EXPECT_NE(prePacket, combinePacket);
    EXPECT_NE(spanBuildPacket, combinePacket);
    EXPECT_NE(combinePacket, clearPacket);
    EXPECT_NE(clearPacket, occupancyPacket);
    EXPECT_EQ(compiledPlan.packet(prePacket).plan->taskCount, 1u);
    EXPECT_EQ(compiledPlan.packet(spanBuildPacket).plan->taskCount, 1u);
    EXPECT_EQ(compiledPlan.packet(combinePacket).plan->taskCount, 1u);
    EXPECT_EQ(compiledPlan.packet(clearPacket).plan->taskCount, 1u);
    EXPECT_EQ(compiledPlan.packet(occupancyPacket).plan->taskCount, 1u);
    const auto packetWaitsFor = [&](const Graphics::GpuSubmissionPacketId packet,
                                    const Graphics::GpuSubmissionPacketId producer){
        const Graphics::GpuSubmissionPacket& packetPlan = *compiledPlan.packet(packet).plan;
        const Graphics::GpuPacketDependency* const dependencies = compiledPlan.packet(packet).dependencies;
        for(u32 dependencyIndex = 0u; dependencyIndex < packetPlan.dependencyCount; ++dependencyIndex){
            if(dependencies[dependencyIndex].producer == producer)
                return true;
        }
        return false;
    };
    EXPECT_TRUE(packetWaitsFor(spanBuildPacket, prePacket));
    EXPECT_TRUE(packetWaitsFor(combinePacket, prePacket));
    EXPECT_TRUE(packetWaitsFor(combinePacket, spanBuildPacket));
    EXPECT_TRUE(packetWaitsFor(clearPacket, combinePacket));
    EXPECT_TRUE(packetWaitsFor(occupancyPacket, combinePacket));
    EXPECT_TRUE(packetWaitsFor(occupancyPacket, clearPacket));

    const Graphics::GpuCompiledTask* const compiledPre = compiledPlan.findTask(pre).plan;
    const Graphics::GpuCompiledTask* const compiledSpanBuild = compiledPlan.findTask(spanBuild).plan;
    const Graphics::GpuCompiledTask* const compiledCombine = compiledPlan.findTask(combine).plan;
    const Graphics::GpuCompiledTask* const compiledClear = compiledPlan.findTask(clear).plan;
    const Graphics::GpuCompiledTask* const compiledOccupancy = compiledPlan.findTask(occupancy).plan;
    ASSERT_NE(compiledPre, nullptr);
    ASSERT_NE(compiledSpanBuild, nullptr);
    ASSERT_NE(compiledCombine, nullptr);
    ASSERT_NE(compiledClear, nullptr);
    ASSERT_NE(compiledOccupancy, nullptr);
    EXPECT_EQ(compiledPre->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledPre->prologueBarrierCount, 5u);
    EXPECT_EQ(compiledSpanBuild->prologueStateSeedCount, 2u);
    EXPECT_EQ(compiledSpanBuild->prologueBarrierCount, 4u);
    EXPECT_EQ(compiledCombine->prologueStateSeedCount, 5u);
    EXPECT_EQ(compiledCombine->prologueBarrierCount, 9u);
    EXPECT_EQ(compiledClear->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledClear->prologueBarrierCount, 1u);
    EXPECT_EQ(compiledOccupancy->prologueStateSeedCount, 5u);
    EXPECT_EQ(compiledOccupancy->prologueBarrierCount, 5u);
    const Graphics::GpuPacketStateSeed* const spanBuildSeeds = compiledPlan.findTask(spanBuild).prologueStateSeeds;
    const Graphics::GpuPacketStateSeed* const combineSeeds = compiledPlan.findTask(combine).prologueStateSeeds;
    const Graphics::GpuPacketStateSeed* const occupancySeeds = compiledPlan.findTask(occupancy).prologueStateSeeds;
    const Graphics::GpuCompiledBarrier* const preBarriers = compiledPlan.findTask(pre).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const spanBuildBarriers = compiledPlan.findTask(spanBuild).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const combineBarriers = compiledPlan.findTask(combine).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const clearBarriers = compiledPlan.findTask(clear).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const occupancyBarriers = compiledPlan.findTask(occupancy).prologueBarriers;
    ASSERT_NE(spanBuildSeeds, nullptr);
    ASSERT_NE(combineSeeds, nullptr);
    ASSERT_NE(occupancySeeds, nullptr);
    ASSERT_NE(preBarriers, nullptr);
    ASSERT_NE(spanBuildBarriers, nullptr);
    ASSERT_NE(combineBarriers, nullptr);
    ASSERT_NE(clearBarriers, nullptr);
    ASSERT_NE(occupancyBarriers, nullptr);
    const auto hasTextureTransition = [](
        const Graphics::GpuCompiledBarrier* const barriers,
        const u32 count,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::TextureSubresourceSet& range
    ){
        for(u32 barrierIndex = 0u; barrierIndex < count; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
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
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre->prologueBarrierCount, capBackNormal, peelRange));
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre->prologueBarrierCount, intervalDepth, peelRange));
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre->prologueBarrierCount, intervalId, peelRange));
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre->prologueBarrierCount, receiverEventData, receiverEventRange));
    EXPECT_TRUE(hasTextureTransition(
        preBarriers,
        compiledPre->prologueBarrierCount,
        receiverEventCount,
        receiverEventCountRange
    ));
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
        prePacket
    ));
    EXPECT_TRUE(hasUav(spanBuildBarriers, compiledSpanBuild->prologueBarrierCount, receiverEventData, receiverEventRange));
    EXPECT_TRUE(hasUav(
        spanBuildBarriers,
        compiledSpanBuild->prologueBarrierCount,
        receiverEventCount,
        receiverEventCountRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        spanBuildBarriers,
        compiledSpanBuild->prologueBarrierCount,
        receiverSpanData,
        receiverSpanRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        spanBuildBarriers,
        compiledSpanBuild->prologueBarrierCount,
        receiverSpanCount,
        receiverSpanCountRange
    ));
    EXPECT_TRUE(hasStateSeed(
        spanBuildSeeds,
        compiledSpanBuild->prologueStateSeedCount,
        receiverEventCount,
        receiverEventCountRange,
        prePacket
    ));
    EXPECT_TRUE(hasStateSeed(combineSeeds, compiledCombine->prologueStateSeedCount, capBackNormal, peelRange, prePacket));
    EXPECT_TRUE(hasStateSeed(combineSeeds, compiledCombine->prologueStateSeedCount, intervalDepth, peelRange, prePacket));
    EXPECT_TRUE(hasStateSeed(combineSeeds, compiledCombine->prologueStateSeedCount, intervalId, peelRange, prePacket));
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
    EXPECT_TRUE(hasUav(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        receiverSpanCount,
        receiverSpanCountRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        removedIntervalDepth,
        removedIntervalRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        removedIntervalCapNormal,
        removedIntervalRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        removedIntervalData,
        removedIntervalRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        removedIntervalCount,
        removedIntervalCountRange
    ));
    EXPECT_TRUE(hasStateSeed(
        occupancySeeds,
        compiledOccupancy->prologueStateSeedCount,
        removedIntervalDepth,
        removedIntervalRange,
        combinePacket
    ));
    EXPECT_TRUE(hasStateSeed(
        occupancySeeds,
        compiledOccupancy->prologueStateSeedCount,
        removedIntervalCapNormal,
        removedIntervalRange,
        combinePacket
    ));
    EXPECT_TRUE(hasStateSeed(
        occupancySeeds,
        compiledOccupancy->prologueStateSeedCount,
        removedIntervalData,
        removedIntervalRange,
        combinePacket
    ));
    EXPECT_TRUE(hasStateSeed(
        occupancySeeds,
        compiledOccupancy->prologueStateSeedCount,
        removedIntervalCount,
        removedIntervalCountRange,
        combinePacket
    ));
    bool coverageSeededByClear = false;
    for(u32 seedIndex = 0u; seedIndex < compiledOccupancy->prologueStateSeedCount; ++seedIndex){
        const Graphics::GpuPacketStateSeed& seed = occupancySeeds[seedIndex];
        coverageSeededByClear = coverageSeededByClear || (seed.resource == coverage && seed.sourcePacket == clearPacket);
    }
    EXPECT_TRUE(coverageSeededByClear);
    EXPECT_TRUE(hasUav(occupancyBarriers, compiledOccupancy->prologueBarrierCount, removedIntervalDepth, removedIntervalRange));
    EXPECT_TRUE(hasUav(
        occupancyBarriers,
        compiledOccupancy->prologueBarrierCount,
        removedIntervalCapNormal,
        removedIntervalRange
    ));
    EXPECT_TRUE(hasUav(occupancyBarriers, compiledOccupancy->prologueBarrierCount, removedIntervalData, removedIntervalRange));
    EXPECT_TRUE(hasUav(
        occupancyBarriers,
        compiledOccupancy->prologueBarrierCount,
        removedIntervalCount,
        removedIntervalCountRange
    ));
    ASSERT_EQ(clearBarriers[0u].type, Graphics::GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(clearBarriers[0u].resource, coverage);
    EXPECT_EQ(clearBarriers[0u].before, Graphics::ResourceStates::Common);
    EXPECT_EQ(clearBarriers[0u].after, Graphics::ResourceStates::CopyDest);
    bool coverageTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledOccupancy->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = occupancyBarriers[barrierIndex];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.resource == coverage
            && barrier.before == Graphics::ResourceStates::CopyDest
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
        )
            coverageTransition = true;
    }
    EXPECT_TRUE(coverageTransition);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

