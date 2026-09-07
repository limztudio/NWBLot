// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_avboit_csg_sample_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, PlansAvboitCsgIntervalProducerToExtinctionSampleAcrossAsyncGap){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId removedIntervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_extinction_removed_interval_depth"),
        "AVBOIT Extinction Removed Interval Depth"
    );
    const Graphics::GpuGraphResourceId removedIntervalCapNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_extinction_removed_interval_cap_normal"),
        "AVBOIT Extinction Removed Interval Cap Normal"
    );
    const Graphics::GpuGraphResourceId removedIntervalData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_extinction_removed_interval_data"),
        "AVBOIT Extinction Removed Interval Data"
    );
    const Graphics::GpuGraphResourceId removedIntervalCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_extinction_removed_interval_count"),
        "AVBOIT Extinction Removed Interval Count"
    );
    const Graphics::GpuGraphResourceId extinctionMaterialGeometry = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_extinction_material_geometry"),
        "AVBOIT Extinction Material Geometry"
    );
    ASSERT_TRUE(removedIntervalDepth.valid());
    ASSERT_TRUE(removedIntervalCapNormal.valid());
    ASSERT_TRUE(removedIntervalData.valid());
    ASSERT_TRUE(removedIntervalCount.valid());
    ASSERT_TRUE(extinctionMaterialGeometry.valid());

    const Graphics::TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const Graphics::GpuTaskResourceUse intervalUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskResourceUse extinctionUses[] = {
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
    const Graphics::GpuGraphResourceSetId extinctionMaterialGeometrySet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/avboit_extinction_material_geometry_set"))
            .setMarkerLabel("AVBOIT Extinction Material Geometry")
            .setMembers(&extinctionMaterialGeometry, 1u)
    );
    ASSERT_TRUE(extinctionMaterialGeometrySet.valid());
    const Graphics::GpuTaskResourceSetUse extinctionMaterialGeometrySetUses[] = {
        Graphics::GpuTaskResourceSetUse{
            .resourceSet = extinctionMaterialGeometrySet,
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
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        true,
        true,
    };
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = Graphics::GpuTaskCostHint::Large;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    Graphics::GpuTaskDesc intervalDesc;
    intervalDesc
        .setIdentity(Name("tests/task_graph/avboit_extinction_intervals"))
        .setMarkerLabel("Transparent CSG Intervals")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(intervalUses, LengthOf(intervalUses))
    ;
    const Graphics::GpuTaskId intervals = graph.addTask(intervalDesc);
    ASSERT_TRUE(intervals.valid());

    Graphics::GpuTaskDesc depthWarpDesc;
    depthWarpDesc
        .setIdentity(Name("tests/task_graph/avboit_extinction_depth_warp"))
        .setMarkerLabel("AVBOIT Depth Warp")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&intervals, 1u)
    ;
    const Graphics::GpuTaskId depthWarp = graph.addTask(depthWarpDesc);
    ASSERT_TRUE(depthWarp.valid());

    Graphics::GpuTaskDesc extinctionDesc;
    extinctionDesc
        .setIdentity(Name("tests/task_graph/avboit_extinction_csg_sample"))
        .setMarkerLabel("AVBOIT Extinction")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&depthWarp, 1u)
        .setResourceUses(extinctionUses, LengthOf(extinctionUses))
        .setResourceSetUses(extinctionMaterialGeometrySetUses, LengthOf(extinctionMaterialGeometrySetUses))
    ;
    const Graphics::GpuTaskId extinction = graph.addTask(extinctionDesc);
    ASSERT_TRUE(extinction.valid());

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

    EXPECT_TRUE(HasInferredHazard(
        analysis,
        intervals,
        extinction,
        removedIntervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        intervals,
        extinction,
        removedIntervalCapNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        intervals,
        extinction,
        removedIntervalData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        intervals,
        extinction,
        removedIntervalCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    ASSERT_EQ(compiledPlan.packetCount(), 3u);
    const Graphics::GpuSubmissionPacketId intervalsPacket = compiledPlan.packetForTask(intervals);
    const Graphics::GpuSubmissionPacketId depthWarpPacket = compiledPlan.packetForTask(depthWarp);
    const Graphics::GpuSubmissionPacketId extinctionPacket = compiledPlan.packetForTask(extinction);
    ASSERT_TRUE(intervalsPacket.valid());
    ASSERT_TRUE(depthWarpPacket.valid());
    ASSERT_TRUE(extinctionPacket.valid());
    EXPECT_NE(intervalsPacket, depthWarpPacket);
    EXPECT_NE(depthWarpPacket, extinctionPacket);
    EXPECT_NE(intervalsPacket, extinctionPacket);
    const Graphics::GpuSubmissionPacket& extinctionPacketPlan = *compiledPlan.packet(extinctionPacket).plan;
    ASSERT_EQ(extinctionPacketPlan.dependencyCount, 2u);
    const Graphics::GpuPacketDependency* const extinctionDependencies = compiledPlan.packet(extinctionPacket).dependencies;
    ASSERT_NE(extinctionDependencies, nullptr);
    bool waitsForIntervals = false;
    bool waitsForDepthWarp = false;
    for(u32 dependencyIndex = 0u; dependencyIndex < extinctionPacketPlan.dependencyCount; ++dependencyIndex){
        waitsForIntervals = waitsForIntervals || extinctionDependencies[dependencyIndex].producer == intervalsPacket;
        waitsForDepthWarp = waitsForDepthWarp || extinctionDependencies[dependencyIndex].producer == depthWarpPacket;
    }
    EXPECT_TRUE(waitsForIntervals);
    EXPECT_TRUE(waitsForDepthWarp);

    const Graphics::GpuCompiledTask* const compiledIntervals = compiledPlan.findTask(intervals).plan;
    const Graphics::GpuCompiledTask* const compiledExtinction = compiledPlan.findTask(extinction).plan;
    ASSERT_NE(compiledIntervals, nullptr);
    ASSERT_NE(compiledExtinction, nullptr);
    ASSERT_EQ(compiledIntervals->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledIntervals->prologueBarrierCount, 4u);
    ASSERT_EQ(compiledExtinction->prologueStateSeedCount, 4u);
    ASSERT_EQ(compiledExtinction->prologueBarrierCount, 5u);
    const Graphics::GpuPacketStateSeed* const extinctionSeeds = compiledPlan.findTask(extinction).prologueStateSeeds;
    const Graphics::GpuCompiledBarrier* const extinctionBarriers = compiledPlan.findTask(extinction).prologueBarriers;
    ASSERT_NE(extinctionSeeds, nullptr);
    ASSERT_NE(extinctionBarriers, nullptr);
    const auto hasExtinctionStateSeed = [&](const Graphics::GpuGraphResourceId resource){
        for(u32 seedIndex = 0u; seedIndex < compiledExtinction->prologueStateSeedCount; ++seedIndex){
            const Graphics::GpuPacketStateSeed& seed = extinctionSeeds[seedIndex];
            if(seed.resource == resource && seed.sourcePacket == intervalsPacket)
                return true;
        }
        return false;
    };
    const auto hasExtinctionUav = [&](const Graphics::GpuGraphResourceId resource, const Graphics::TextureSubresourceSet& range){
        for(u32 barrierIndex = 0u; barrierIndex < compiledExtinction->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = extinctionBarriers[barrierIndex];
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
    EXPECT_TRUE(hasExtinctionStateSeed(removedIntervalDepth));
    EXPECT_TRUE(hasExtinctionStateSeed(removedIntervalCapNormal));
    EXPECT_TRUE(hasExtinctionStateSeed(removedIntervalData));
    EXPECT_TRUE(hasExtinctionStateSeed(removedIntervalCount));
    bool extinctionMaterialGeometryTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledExtinction->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = extinctionBarriers[barrierIndex];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.resource == extinctionMaterialGeometry
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::ShaderResource
        )
            extinctionMaterialGeometryTransition = true;
    }
    EXPECT_TRUE(hasExtinctionUav(removedIntervalDepth, removedIntervalRange));
    EXPECT_TRUE(hasExtinctionUav(removedIntervalCapNormal, removedIntervalRange));
    EXPECT_TRUE(hasExtinctionUav(removedIntervalData, removedIntervalRange));
    EXPECT_TRUE(hasExtinctionUav(removedIntervalCount, removedIntervalCountRange));
    EXPECT_TRUE(extinctionMaterialGeometryTransition);
}

TEST(GpuTaskGraph, PlansAvboitCsgIntervalProducerToAccumulationSampleAcrossIntegrationGap){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId removedIntervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_accumulation_removed_interval_depth"),
        "AVBOIT Accumulation Removed Interval Depth"
    );
    const Graphics::GpuGraphResourceId removedIntervalCapNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_accumulation_removed_interval_cap_normal"),
        "AVBOIT Accumulation Removed Interval Cap Normal"
    );
    const Graphics::GpuGraphResourceId removedIntervalData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_accumulation_removed_interval_data"),
        "AVBOIT Accumulation Removed Interval Data"
    );
    const Graphics::GpuGraphResourceId removedIntervalCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_accumulation_removed_interval_count"),
        "AVBOIT Accumulation Removed Interval Count"
    );
    const Graphics::GpuGraphResourceId accumulationMaterialGeometry = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_accumulation_material_geometry"),
        "AVBOIT Accumulation Material Geometry"
    );
    ASSERT_TRUE(removedIntervalDepth.valid());
    ASSERT_TRUE(removedIntervalCapNormal.valid());
    ASSERT_TRUE(removedIntervalData.valid());
    ASSERT_TRUE(removedIntervalCount.valid());
    ASSERT_TRUE(accumulationMaterialGeometry.valid());

    const Graphics::TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const Graphics::GpuTaskResourceUse intervalUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskResourceUse accumulationUses[] = {
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
    const Graphics::GpuGraphResourceSetId accumulationMaterialGeometrySet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/avboit_accumulation_material_geometry_set"))
            .setMarkerLabel("AVBOIT Accumulation Material Geometry")
            .setMembers(&accumulationMaterialGeometry, 1u)
    );
    ASSERT_TRUE(accumulationMaterialGeometrySet.valid());
    const Graphics::GpuTaskResourceSetUse accumulationMaterialGeometrySetUses[] = {
        Graphics::GpuTaskResourceSetUse{
            .resourceSet = accumulationMaterialGeometrySet,
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
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        true,
        true,
    };
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = Graphics::GpuTaskCostHint::Large;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    Graphics::GpuTaskDesc intervalDesc;
    intervalDesc
        .setIdentity(Name("tests/task_graph/avboit_accumulation_intervals"))
        .setMarkerLabel("Transparent CSG Intervals")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(intervalUses, LengthOf(intervalUses))
    ;
    const Graphics::GpuTaskId intervals = graph.addTask(intervalDesc);
    ASSERT_TRUE(intervals.valid());

    Graphics::GpuTaskDesc depthWarpDesc;
    depthWarpDesc
        .setIdentity(Name("tests/task_graph/avboit_accumulation_depth_warp"))
        .setMarkerLabel("AVBOIT Depth Warp")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&intervals, 1u)
    ;
    const Graphics::GpuTaskId depthWarp = graph.addTask(depthWarpDesc);
    ASSERT_TRUE(depthWarp.valid());

    Graphics::GpuTaskDesc extinctionDesc;
    extinctionDesc
        .setIdentity(Name("tests/task_graph/avboit_accumulation_extinction"))
        .setMarkerLabel("AVBOIT Extinction")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&depthWarp, 1u)
    ;
    const Graphics::GpuTaskId extinction = graph.addTask(extinctionDesc);
    ASSERT_TRUE(extinction.valid());

    Graphics::GpuTaskDesc integrationDesc;
    integrationDesc
        .setIdentity(Name("tests/task_graph/avboit_accumulation_integration"))
        .setMarkerLabel("AVBOIT Integration")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&extinction, 1u)
    ;
    const Graphics::GpuTaskId integration = graph.addTask(integrationDesc);
    ASSERT_TRUE(integration.valid());

    Graphics::GpuTaskDesc accumulationDesc;
    accumulationDesc
        .setIdentity(Name("tests/task_graph/avboit_accumulation_csg_sample"))
        .setMarkerLabel("AVBOIT Accumulation")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&integration, 1u)
        .setResourceUses(accumulationUses, LengthOf(accumulationUses))
        .setResourceSetUses(accumulationMaterialGeometrySetUses, LengthOf(accumulationMaterialGeometrySetUses))
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

    EXPECT_TRUE(HasInferredHazard(
        analysis,
        intervals,
        accumulation,
        removedIntervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        intervals,
        accumulation,
        removedIntervalCapNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        intervals,
        accumulation,
        removedIntervalData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        intervals,
        accumulation,
        removedIntervalCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    ASSERT_EQ(compiledPlan.packetCount(), 5u);
    const Graphics::GpuSubmissionPacketId intervalsPacket = compiledPlan.packetForTask(intervals);
    const Graphics::GpuSubmissionPacketId depthWarpPacket = compiledPlan.packetForTask(depthWarp);
    const Graphics::GpuSubmissionPacketId extinctionPacket = compiledPlan.packetForTask(extinction);
    const Graphics::GpuSubmissionPacketId integrationPacket = compiledPlan.packetForTask(integration);
    const Graphics::GpuSubmissionPacketId accumulationPacket = compiledPlan.packetForTask(accumulation);
    ASSERT_TRUE(intervalsPacket.valid());
    ASSERT_TRUE(depthWarpPacket.valid());
    ASSERT_TRUE(extinctionPacket.valid());
    ASSERT_TRUE(integrationPacket.valid());
    ASSERT_TRUE(accumulationPacket.valid());
    EXPECT_NE(intervalsPacket, depthWarpPacket);
    EXPECT_NE(depthWarpPacket, extinctionPacket);
    EXPECT_NE(extinctionPacket, integrationPacket);
    EXPECT_NE(integrationPacket, accumulationPacket);
    const Graphics::GpuSubmissionPacket& accumulationPacketPlan = *compiledPlan.packet(accumulationPacket).plan;
    ASSERT_EQ(accumulationPacketPlan.dependencyCount, 2u);
    const Graphics::GpuPacketDependency* const accumulationDependencies = compiledPlan.packet(accumulationPacket).dependencies;
    ASSERT_NE(accumulationDependencies, nullptr);
    bool waitsForIntervals = false;
    bool waitsForIntegration = false;
    for(u32 dependencyIndex = 0u; dependencyIndex < accumulationPacketPlan.dependencyCount; ++dependencyIndex){
        waitsForIntervals = waitsForIntervals || accumulationDependencies[dependencyIndex].producer == intervalsPacket;
        waitsForIntegration = waitsForIntegration || accumulationDependencies[dependencyIndex].producer == integrationPacket;
    }
    EXPECT_TRUE(waitsForIntervals);
    EXPECT_TRUE(waitsForIntegration);

    const Graphics::GpuCompiledTask* const compiledIntervals = compiledPlan.findTask(intervals).plan;
    const Graphics::GpuCompiledTask* const compiledAccumulation = compiledPlan.findTask(accumulation).plan;
    ASSERT_NE(compiledIntervals, nullptr);
    ASSERT_NE(compiledAccumulation, nullptr);
    ASSERT_EQ(compiledIntervals->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledIntervals->prologueBarrierCount, 4u);
    ASSERT_EQ(compiledAccumulation->prologueStateSeedCount, 4u);
    ASSERT_EQ(compiledAccumulation->prologueBarrierCount, 5u);
    const Graphics::GpuPacketStateSeed* const accumulationSeeds = compiledPlan.findTask(accumulation).prologueStateSeeds;
    const Graphics::GpuCompiledBarrier* const accumulationBarriers = compiledPlan.findTask(accumulation).prologueBarriers;
    ASSERT_NE(accumulationSeeds, nullptr);
    ASSERT_NE(accumulationBarriers, nullptr);
    const auto hasAccumulationStateSeed = [&](const Graphics::GpuGraphResourceId resource){
        for(u32 seedIndex = 0u; seedIndex < compiledAccumulation->prologueStateSeedCount; ++seedIndex){
            const Graphics::GpuPacketStateSeed& seed = accumulationSeeds[seedIndex];
            if(seed.resource == resource && seed.sourcePacket == intervalsPacket)
                return true;
        }
        return false;
    };
    const auto hasAccumulationUav = [&](const Graphics::GpuGraphResourceId resource, const Graphics::TextureSubresourceSet& range){
        for(u32 barrierIndex = 0u; barrierIndex < compiledAccumulation->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = accumulationBarriers[barrierIndex];
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
    EXPECT_TRUE(hasAccumulationStateSeed(removedIntervalDepth));
    EXPECT_TRUE(hasAccumulationStateSeed(removedIntervalCapNormal));
    EXPECT_TRUE(hasAccumulationStateSeed(removedIntervalData));
    EXPECT_TRUE(hasAccumulationStateSeed(removedIntervalCount));
    bool accumulationMaterialGeometryTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledAccumulation->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = accumulationBarriers[barrierIndex];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.resource == accumulationMaterialGeometry
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::ShaderResource
        )
            accumulationMaterialGeometryTransition = true;
    }
    EXPECT_TRUE(hasAccumulationUav(removedIntervalDepth, removedIntervalRange));
    EXPECT_TRUE(hasAccumulationUav(removedIntervalCapNormal, removedIntervalRange));
    EXPECT_TRUE(hasAccumulationUav(removedIntervalData, removedIntervalRange));
    EXPECT_TRUE(hasAccumulationUav(removedIntervalCount, removedIntervalCountRange));
    EXPECT_TRUE(accumulationMaterialGeometryTransition);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

