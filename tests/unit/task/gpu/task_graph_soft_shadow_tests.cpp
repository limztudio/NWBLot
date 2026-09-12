// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_soft_shadow_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, PlansGraphOwnedSoftTransparentTraceEntryStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing =
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    ;
    const auto addBuffer = [&](const Name identity, const AStringView label){
        return AddBufferMetadata(
            graph,
            identity,
            label,
            Graphics::ResourceStates::Common,
            queueSharing
        );
    };
    const Graphics::GpuGraphResourceId shaderResources[] = {
        addBuffer(Name("tests/task_graph/soft_transparent_mesh_nodes"), "Software Mesh Nodes"),
        addBuffer(Name("tests/task_graph/soft_transparent_mesh_positions"), "Software Mesh Positions"),
        addBuffer(Name("tests/task_graph/soft_transparent_mesh_indices"), "Software Mesh Indices"),
        addBuffer(Name("tests/task_graph/soft_transparent_mesh_attributes"), "Software Mesh Attributes"),
        addBuffer(Name("tests/task_graph/soft_transparent_scene_bvh_nodes"), "Scene BVH Nodes"),
        addBuffer(Name("tests/task_graph/soft_transparent_scene_instances"), "Scene Instances"),
        addBuffer(Name("tests/task_graph/soft_transparent_instance_materials"), "Shadow Instance Materials"),
        addBuffer(Name("tests/task_graph/soft_transparent_material_typed"), "Shadow Typed Materials"),
        addBuffer(Name("tests/task_graph/soft_transparent_instances"), "Shadow Instances"),
        addBuffer(Name("tests/task_graph/soft_transparent_lights"), "Deferred Lights"),
    };
    const Graphics::GpuGraphResourceId constantResources[] = {
        addBuffer(Name("tests/task_graph/soft_transparent_material_context_slots"), "Ray-Trace Material Context Slots"),
        addBuffer(Name("tests/task_graph/soft_transparent_bindless_slots"), "Deferred Bindless Slots"),
        addBuffer(Name("tests/task_graph/soft_transparent_scene_shading"), "Scene Shading"),
    };
    for(const Graphics::GpuGraphResourceId resource : shaderResources)
        ASSERT_TRUE(resource.valid());
    for(const Graphics::GpuGraphResourceId resource : constantResources)
        ASSERT_TRUE(resource.valid());

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

    Graphics::GpuTaskResourceUse prefixUses[LengthOf(shaderResources) + LengthOf(constantResources)] = {};
    usize prefixUseCount = 0u;
    for(const Graphics::GpuGraphResourceId resource : shaderResources){
        prefixUses[prefixUseCount++] = {
            .resource = resource,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
    }
    for(const Graphics::GpuGraphResourceId resource : constantResources){
        prefixUses[prefixUseCount++] = {
            .resource = resource,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
    }
    ASSERT_EQ(prefixUseCount, LengthOf(prefixUses));
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/soft_transparent_prefix"))
        .setMarkerLabel("Transparent Shadow Prefix")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    const Graphics::GpuGraphResourceId softwareTraceGeometryMembers[] = {
        shaderResources[0u],
        shaderResources[1u],
        shaderResources[2u],
        shaderResources[3u],
    };
    const Graphics::GpuGraphResourceSetId softwareTraceGeometrySet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/soft_transparent_trace_geometry"))
            .setMarkerLabel("Transparent Shadow Trace Geometry")
            .setMembers(softwareTraceGeometryMembers, LengthOf(softwareTraceGeometryMembers))
    );
    ASSERT_TRUE(softwareTraceGeometrySet.valid());
    const Graphics::GpuTaskResourceSetUse softwareTraceGeometrySetUses[] = {
        {
            .resourceSet = softwareTraceGeometrySet,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskResourceUse traceUses[LengthOf(shaderResources) + LengthOf(constantResources)] = {};
    usize traceUseCount = 0u;
    for(usize resourceIndex = LengthOf(softwareTraceGeometryMembers); resourceIndex < LengthOf(shaderResources); ++resourceIndex){
        const Graphics::GpuGraphResourceId resource = shaderResources[resourceIndex];
        traceUses[traceUseCount++] = {
            .resource = resource,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        };
    }
    for(const Graphics::GpuGraphResourceId resource : constantResources){
        traceUses[traceUseCount++] = {
            .resource = resource,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        };
    }
    ASSERT_EQ(traceUseCount + LengthOf(softwareTraceGeometryMembers), LengthOf(traceUses));
    Graphics::GpuTaskDesc traceDesc;
    traceDesc
        .setIdentity(Name("tests/task_graph/graph_owned_soft_transparent_trace"))
        .setMarkerLabel("Transparent Shadow Trace")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(traceUses, traceUseCount)
        .setResourceSetUses(softwareTraceGeometrySetUses, LengthOf(softwareTraceGeometrySetUses))
    ;
    const Graphics::GpuTaskId trace = graph.addTask(traceDesc);
    ASSERT_TRUE(trace.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    const Graphics::GpuSubmissionPacketId prefixPacket = compiledPlan.packetForTask(prefix);
    const Graphics::GpuSubmissionPacketId tracePacket = compiledPlan.packetForTask(trace);
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(tracePacket.valid());
    EXPECT_NE(prefixPacket, tracePacket);

    const Graphics::GpuCompiledTask* const compiledTrace = compiledPlan.findTask(trace).plan;
    ASSERT_NE(compiledTrace, nullptr);
    EXPECT_EQ(compiledTrace->prologueStateSeedCount, LengthOf(traceUses));
    const Graphics::GpuPacketStateSeed* const traceSeeds = compiledPlan.findTask(trace).prologueStateSeeds;
    ASSERT_NE(traceSeeds, nullptr);
    const auto hasTraceSeed = [&](const Graphics::GpuGraphResourceId resource){
        for(usize seedIndex = 0u; seedIndex < compiledTrace->prologueStateSeedCount; ++seedIndex){
            if(traceSeeds[seedIndex].resource == resource && traceSeeds[seedIndex].sourcePacket == prefixPacket)
                return true;
        }
        return false;
    };
    for(const Graphics::GpuGraphResourceId resource : shaderResources)
        EXPECT_TRUE(hasTraceSeed(resource));
    for(const Graphics::GpuGraphResourceId resource : constantResources)
        EXPECT_TRUE(hasTraceSeed(resource));

    const Graphics::GpuCompiledBarrier* const traceBarriers = compiledPlan.findTask(trace).prologueBarriers;
    ASSERT_NE(traceBarriers, nullptr);
    const auto hasTraceBarrier = [&](const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledTrace->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = traceBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == Graphics::ResourceStates::CopyDest
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    for(const Graphics::GpuGraphResourceId resource : shaderResources)
        EXPECT_TRUE(hasTraceBarrier(resource, Graphics::ResourceStates::ShaderResource));
    for(const Graphics::GpuGraphResourceId resource : constantResources)
        EXPECT_TRUE(hasTraceBarrier(resource, Graphics::ResourceStates::ConstantBuffer));
    ASSERT_EQ(compiledPlan.packet(tracePacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(tracePacket).dependencies[0u].producer, prefixPacket);
}

TEST(GpuTaskGraph, MergesGraphOwnedSoftTransparentTraceAndResolveAfterOpaqueVisibility){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuExternalCompletionId priorHistoryTail = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/soft_transparent_fold_history_tail"))
            .setMarkerLabel("Prior Lagged Lighting History Tail")
    );
    ASSERT_TRUE(priorHistoryTail.valid());
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing =
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    ;
    // The retained A history preserves the established sampled-history route. Fresh split outputs begin Unknown and
    // must be accepted through their first Write declaration rather than a fabricated native initial state.
    const Graphics::GpuGraphResourceId shadowVisibility = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_shadow_visibility"),
        "Shadow Visibility",
        Graphics::ResourceStates::Unknown,
        queueSharing
    );
    const Graphics::GpuGraphResourceId opaqueSoftHalf = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_opaque_half"),
        "Opaque Shadow Soft Half",
        Graphics::ResourceStates::Unknown,
        queueSharing
    );
    const Graphics::GpuGraphResourceId opaqueWaveletHalf = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_opaque_wavelet_half"),
        "Opaque Shadow First Wavelet Half",
        Graphics::ResourceStates::Unknown,
        queueSharing
    );
    const Graphics::GpuGraphResourceId transparentSoftHalf = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_half"),
        "Transparent Shadow Soft Half",
        Graphics::ResourceStates::Unknown,
        queueSharing
    );
    const Graphics::GpuGraphResourceId transparentHistoryA = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_history_a"),
        "Transparent Shadow History A",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId transparentMomentsA = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_moments_a"),
        "Transparent Shadow Moments A",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId transparentHistoryB = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_history_b"),
        "Transparent Shadow History B",
        Graphics::ResourceStates::Unknown,
        queueSharing
    );
    const Graphics::GpuGraphResourceId transparentMomentsB = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_moments_b"),
        "Transparent Shadow Moments B",
        Graphics::ResourceStates::Unknown,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowSoftGeometry = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_geometry"),
        "Shadow Soft Geometry",
        Graphics::ResourceStates::Unknown,
        queueSharing
    );
    const Graphics::GpuGraphResourceId previousGeometry = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_geometry_previous"),
        "Previous Shadow Soft Geometry",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId worldPosition = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_world_position"),
        "World Position",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId normal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_normal"),
        "G-buffer Normal",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId depth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_depth"),
        "G-buffer Depth",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneShading = AddBufferMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_scene_shading"),
        "Scene Shading",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    ASSERT_TRUE(shadowVisibility.valid());
    ASSERT_TRUE(opaqueSoftHalf.valid());
    ASSERT_TRUE(opaqueWaveletHalf.valid());
    ASSERT_TRUE(transparentSoftHalf.valid());
    ASSERT_TRUE(transparentHistoryA.valid());
    ASSERT_TRUE(transparentMomentsA.valid());
    ASSERT_TRUE(transparentHistoryB.valid());
    ASSERT_TRUE(transparentMomentsB.valid());
    ASSERT_TRUE(shadowSoftGeometry.valid());
    ASSERT_TRUE(previousGeometry.valid());
    ASSERT_TRUE(worldPosition.valid());
    ASSERT_TRUE(normal.valid());
    ASSERT_TRUE(depth.valid());
    ASSERT_TRUE(sceneShading.valid());

    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint opaqueScheduling;
    opaqueScheduling.cost = Graphics::GpuTaskCostHint::Large;
    opaqueScheduling.forceSubmissionBoundary = false;
    opaqueScheduling.allowPacketMerge = true;
    opaqueScheduling.mergeWithPrevious = false;
    const Graphics::GpuTaskResourceUse opaqueUses[] = {
        {
            .resource = shadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        {
            .resource = shadowSoftGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        {
            .resource = opaqueSoftHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc opaqueDesc;
    opaqueDesc
        .setIdentity(Name("tests/task_graph/soft_transparent_fold_opaque"))
        .setMarkerLabel("Shadow Visibility Opaque")
        .setQueue(computeRequest)
        .setScheduling(opaqueScheduling)
        .setExternalDependencies(&priorHistoryTail, 1u)
        .setResourceUses(opaqueUses, LengthOf(opaqueUses))
    ;
    const Graphics::GpuTaskId opaque = graph.addTask(opaqueDesc);
    ASSERT_TRUE(opaque.valid());

    Graphics::GpuTaskSchedulingHint traceScheduling = opaqueScheduling;
    traceScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    traceScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse opaqueFirstWaveletUses[] = {
        {
            .resource = opaqueSoftHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = opaqueWaveletHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        {
            .resource = shadowSoftGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc opaqueFirstWaveletDesc;
    opaqueFirstWaveletDesc
        .setIdentity(Name("tests/task_graph/soft_transparent_opaque_first_wavelet"))
        .setMarkerLabel("Shadow Opaque First Wavelet")
        .setQueue(computeRequest)
        .setScheduling(traceScheduling)
        .setDependencies(&opaque, 1u)
        .setResourceUses(opaqueFirstWaveletUses, LengthOf(opaqueFirstWaveletUses))
    ;
    const Graphics::GpuTaskId opaqueFirstWavelet = graph.addTask(opaqueFirstWaveletDesc);
    ASSERT_TRUE(opaqueFirstWavelet.valid());

    const Graphics::GpuTaskResourceUse opaqueResolveUses[] = {
        {
            .resource = shadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        {
            .resource = shadowSoftGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = opaqueWaveletHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc opaqueResolveDesc;
    opaqueResolveDesc
        .setIdentity(Name("tests/task_graph/soft_transparent_opaque_resolve"))
        .setMarkerLabel("Shadow Opaque Soft Resolve")
        .setQueue(computeRequest)
        .setScheduling(traceScheduling)
        .setDependencies(&opaqueFirstWavelet, 1u)
        .setResourceUses(opaqueResolveUses, LengthOf(opaqueResolveUses))
    ;
    const Graphics::GpuTaskId opaqueResolve = graph.addTask(opaqueResolveDesc);
    ASSERT_TRUE(opaqueResolve.valid());

    const Graphics::GpuTaskResourceUse traceUses[] = {
        {
            .resource = transparentSoftHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc traceDesc;
    traceDesc
        .setIdentity(Name("tests/task_graph/soft_transparent_trace"))
        .setMarkerLabel("Shadow Transparent Soft Trace")
        .setQueue(computeRequest)
        .setScheduling(traceScheduling)
        .setDependencies(&opaqueResolve, 1u)
        .setResourceUses(traceUses, LengthOf(traceUses))
    ;
    const Graphics::GpuTaskId trace = graph.addTask(traceDesc);
    ASSERT_TRUE(trace.valid());

    const Graphics::GpuTaskResourceUse transparentTemporalMergeUses[] = {
        {
            .resource = transparentSoftHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = shadowSoftGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = previousGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = worldPosition,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = transparentHistoryA,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = transparentMomentsA,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = transparentHistoryB,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        {
            .resource = transparentMomentsB,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc transparentTemporalMergeDesc;
    transparentTemporalMergeDesc
        .setIdentity(Name("tests/task_graph/soft_transparent_temporal_merge"))
        .setMarkerLabel("Shadow Transparent Temporal Merge")
        .setQueue(computeRequest)
        .setScheduling(traceScheduling)
        .setDependencies(&trace, 1u)
        .setResourceUses(transparentTemporalMergeUses, LengthOf(transparentTemporalMergeUses))
    ;
    const Graphics::GpuTaskId transparentTemporalMerge = graph.addTask(transparentTemporalMergeDesc);
    ASSERT_TRUE(transparentTemporalMerge.valid());

    const Graphics::GpuTaskResourceUse transparentFirstWaveletUses[] = {
        {
            .resource = opaqueSoftHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        {
            .resource = shadowSoftGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = transparentHistoryB,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = transparentMomentsB,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc transparentFirstWaveletDesc;
    transparentFirstWaveletDesc
        .setIdentity(Name("tests/task_graph/soft_transparent_first_wavelet"))
        .setMarkerLabel("Shadow Transparent First Wavelet")
        .setQueue(computeRequest)
        .setScheduling(traceScheduling)
        .setDependencies(&transparentTemporalMerge, 1u)
        .setResourceUses(transparentFirstWaveletUses, LengthOf(transparentFirstWaveletUses))
    ;
    const Graphics::GpuTaskId transparentFirstWavelet = graph.addTask(transparentFirstWaveletDesc);
    ASSERT_TRUE(transparentFirstWavelet.valid());

    const Graphics::GpuTaskResourceUse foldUses[] = {
        {
            .resource = shadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = opaqueSoftHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = shadowSoftGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = worldPosition,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = normal,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = depth,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = sceneShading,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc foldDesc;
    foldDesc
        .setIdentity(Name("tests/task_graph/soft_transparent_fold"))
        .setMarkerLabel("Shadow Transparent Soft Fold")
        .setQueue(computeRequest)
        .setScheduling(traceScheduling)
        .setDependencies(&transparentFirstWavelet, 1u)
        .setResourceUses(foldUses, LengthOf(foldUses))
    ;
    const Graphics::GpuTaskId fold = graph.addTask(foldDesc);
    ASSERT_TRUE(fold.valid());

    Graphics::GpuTaskSchedulingHint lightingScheduling;
    lightingScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    lightingScheduling.forceSubmissionBoundary = true;
    lightingScheduling.allowPacketMerge = false;
    const Graphics::GpuTaskResourceUse lightingUses[] = {
        {
            .resource = shadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/task_graph/soft_transparent_fold_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(graphicsRequest)
        .setScheduling(lightingScheduling)
        .setDependencies(&fold, 1u)
        .setResourceUses(lightingUses, LengthOf(lightingUses))
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

    EXPECT_TRUE(analysis.hasExplicitEdge(opaque, opaqueFirstWavelet));
    EXPECT_TRUE(analysis.hasExplicitEdge(opaqueFirstWavelet, opaqueResolve));
    EXPECT_TRUE(analysis.hasExplicitEdge(opaqueResolve, trace));
    EXPECT_TRUE(analysis.hasExplicitEdge(trace, transparentTemporalMerge));
    EXPECT_TRUE(analysis.hasExplicitEdge(transparentTemporalMerge, transparentFirstWavelet));
    EXPECT_TRUE(analysis.hasExplicitEdge(transparentFirstWavelet, fold));
    EXPECT_TRUE(analysis.hasExplicitEdge(fold, lighting));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        opaque,
        opaqueFirstWavelet,
        shadowSoftGeometry,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        opaque,
        opaqueFirstWavelet,
        opaqueSoftHalf,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        opaqueFirstWavelet,
        opaqueResolve,
        opaqueWaveletHalf,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        trace,
        transparentTemporalMerge,
        transparentSoftHalf,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        transparentTemporalMerge,
        transparentFirstWavelet,
        transparentHistoryB,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        transparentTemporalMerge,
        transparentFirstWavelet,
        transparentMomentsB,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        transparentFirstWavelet,
        fold,
        opaqueSoftHalf,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    const Graphics::GpuSubmissionPacketId opaquePacket = compiledPlan.packetForTask(opaque);
    const Graphics::GpuSubmissionPacketId opaqueFirstWaveletPacket = compiledPlan.packetForTask(opaqueFirstWavelet);
    const Graphics::GpuSubmissionPacketId opaqueResolvePacket = compiledPlan.packetForTask(opaqueResolve);
    const Graphics::GpuSubmissionPacketId tracePacket = compiledPlan.packetForTask(trace);
    const Graphics::GpuSubmissionPacketId transparentTemporalMergePacket = compiledPlan.packetForTask(transparentTemporalMerge);
    const Graphics::GpuSubmissionPacketId transparentFirstWaveletPacket = compiledPlan.packetForTask(transparentFirstWavelet);
    const Graphics::GpuSubmissionPacketId foldPacket = compiledPlan.packetForTask(fold);
    const Graphics::GpuSubmissionPacketId lightingPacket = compiledPlan.packetForTask(lighting);
    ASSERT_TRUE(opaquePacket.valid());
    ASSERT_TRUE(opaqueFirstWaveletPacket.valid());
    ASSERT_TRUE(opaqueResolvePacket.valid());
    ASSERT_TRUE(tracePacket.valid());
    ASSERT_TRUE(transparentTemporalMergePacket.valid());
    ASSERT_TRUE(transparentFirstWaveletPacket.valid());
    ASSERT_TRUE(foldPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    EXPECT_EQ(opaquePacket, foldPacket);
    EXPECT_EQ(opaqueFirstWaveletPacket, foldPacket);
    EXPECT_EQ(opaqueResolvePacket, foldPacket);
    EXPECT_EQ(tracePacket, foldPacket);
    EXPECT_EQ(transparentTemporalMergePacket, foldPacket);
    EXPECT_EQ(transparentFirstWaveletPacket, foldPacket);
    EXPECT_NE(foldPacket, lightingPacket);
    ASSERT_EQ(compiledPlan.packetCount(), 2u);
    const Graphics::GpuSubmissionPacket& shadowPacket = *compiledPlan.packet(foldPacket).plan;
    ASSERT_EQ(shadowPacket.taskCount, 7u);
    const Graphics::GpuTaskId* const shadowTasks = compiledPlan.packet(foldPacket).tasks;
    ASSERT_NE(shadowTasks, nullptr);
    EXPECT_EQ(shadowTasks[0u], opaque);
    EXPECT_EQ(shadowTasks[1u], opaqueFirstWavelet);
    EXPECT_EQ(shadowTasks[2u], opaqueResolve);
    EXPECT_EQ(shadowTasks[3u], trace);
    EXPECT_EQ(shadowTasks[4u], transparentTemporalMerge);
    EXPECT_EQ(shadowTasks[5u], transparentFirstWavelet);
    EXPECT_EQ(shadowTasks[6u], fold);
    ASSERT_EQ(shadowPacket.externalDependencyCount, 1u);
    const Graphics::GpuExternalCompletionId* const shadowExternalDependencies = compiledPlan.packet(
        foldPacket
    ).externalDependencies;
    ASSERT_NE(shadowExternalDependencies, nullptr);
    EXPECT_EQ(shadowExternalDependencies[0u], priorHistoryTail);

    const Graphics::GpuCompiledTask* const compiledOpaqueFirstWavelet = compiledPlan.findTask(opaqueFirstWavelet).plan;
    ASSERT_NE(compiledOpaqueFirstWavelet, nullptr);
    const Graphics::GpuCompiledBarrier* const opaqueFirstWaveletBarriers = compiledPlan.findTask(opaqueFirstWavelet).prologueBarriers;
    ASSERT_NE(opaqueFirstWaveletBarriers, nullptr);
    const auto hasOpaqueFirstWaveletTransition = [&](const Graphics::GpuGraphResourceId resource){
        for(u32 barrierIndex = 0u; barrierIndex < compiledOpaqueFirstWavelet->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = opaqueFirstWaveletBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasOpaqueFirstWaveletTransition(opaqueSoftHalf));
    EXPECT_TRUE(hasOpaqueFirstWaveletTransition(shadowSoftGeometry));

    const Graphics::GpuCompiledTask* const compiledOpaqueResolve = compiledPlan.findTask(opaqueResolve).plan;
    ASSERT_NE(compiledOpaqueResolve, nullptr);
    const Graphics::GpuCompiledBarrier* const opaqueResolveBarriers = compiledPlan.findTask(opaqueResolve).prologueBarriers;
    ASSERT_NE(opaqueResolveBarriers, nullptr);
    bool hasOpaqueWaveletResolveTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledOpaqueResolve->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = opaqueResolveBarriers[barrierIndex];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == opaqueWaveletHalf
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        ){
            hasOpaqueWaveletResolveTransition = true;
            break;
        }
    }
    EXPECT_TRUE(hasOpaqueWaveletResolveTransition);

    const auto hasUnknownFirstWrite = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        if(!compiledTask || !barriers)
            return false;
        for(u32 barrierIndex = 0u; barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.before == Graphics::ResourceStates::Unknown
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
                && !barrier.isGraphInitialState
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasUnknownFirstWrite(opaque, shadowVisibility));
    EXPECT_TRUE(hasUnknownFirstWrite(opaque, opaqueSoftHalf));
    EXPECT_TRUE(hasUnknownFirstWrite(opaque, shadowSoftGeometry));
    EXPECT_TRUE(hasUnknownFirstWrite(opaqueFirstWavelet, opaqueWaveletHalf));
    EXPECT_TRUE(hasUnknownFirstWrite(trace, transparentSoftHalf));
    EXPECT_TRUE(hasUnknownFirstWrite(transparentTemporalMerge, transparentHistoryB));
    EXPECT_TRUE(hasUnknownFirstWrite(transparentTemporalMerge, transparentMomentsB));

    const Graphics::GpuCompiledTask* const compiledTransparentTemporalMerge = compiledPlan.findTask(transparentTemporalMerge).plan;
    ASSERT_NE(compiledTransparentTemporalMerge, nullptr);
    const Graphics::GpuCompiledBarrier* const transparentTemporalMergeBarriers =
        compiledPlan.findTask(transparentTemporalMerge).prologueBarriers
    ;
    ASSERT_NE(transparentTemporalMergeBarriers, nullptr);
    const auto hasTransparentTemporalMergeShaderResourceTransition = [&](const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before){
        for(u32 barrierIndex = 0u; barrierIndex < compiledTransparentTemporalMerge->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = transparentTemporalMergeBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == Graphics::ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTransparentTemporalMergeShaderResourceTransition(transparentSoftHalf, Graphics::ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransparentTemporalMergeShaderResourceTransition(transparentHistoryA, Graphics::ResourceStates::Common));
    EXPECT_TRUE(hasTransparentTemporalMergeShaderResourceTransition(transparentMomentsA, Graphics::ResourceStates::Common));
    EXPECT_TRUE(hasTransparentTemporalMergeShaderResourceTransition(previousGeometry, Graphics::ResourceStates::Common));
    EXPECT_TRUE(hasTransparentTemporalMergeShaderResourceTransition(worldPosition, Graphics::ResourceStates::Common));

    const Graphics::GpuCompiledTask* const compiledTransparentFirstWavelet = compiledPlan.findTask(transparentFirstWavelet).plan;
    ASSERT_NE(compiledTransparentFirstWavelet, nullptr);
    const Graphics::GpuCompiledBarrier* const transparentFirstWaveletBarriers =
        compiledPlan.findTask(transparentFirstWavelet).prologueBarriers
    ;
    ASSERT_NE(transparentFirstWaveletBarriers, nullptr);
    const auto hasTransparentFirstWaveletShaderResourceTransition = [&](const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before){
        for(u32 barrierIndex = 0u; barrierIndex < compiledTransparentFirstWavelet->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = transparentFirstWaveletBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == Graphics::ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTransparentFirstWaveletShaderResourceTransition(transparentHistoryB, Graphics::ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransparentFirstWaveletShaderResourceTransition(transparentMomentsB, Graphics::ResourceStates::UnorderedAccess));

    const Graphics::GpuCompiledTask* const compiledFold = compiledPlan.findTask(fold).plan;
    ASSERT_NE(compiledFold, nullptr);
    const Graphics::GpuCompiledBarrier* const foldBarriers = compiledPlan.findTask(fold).prologueBarriers;
    ASSERT_NE(foldBarriers, nullptr);
    const auto hasFoldShaderResourceTransition = [&](const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before){
        for(u32 barrierIndex = 0u; barrierIndex < compiledFold->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = foldBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == Graphics::ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasFoldShaderResourceTransition(opaqueSoftHalf, Graphics::ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasFoldShaderResourceTransition(normal, Graphics::ResourceStates::Common));
    EXPECT_TRUE(hasFoldShaderResourceTransition(depth, Graphics::ResourceStates::Common));
    bool hasSceneShadingTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledFold->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = foldBarriers[barrierIndex];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.resource == sceneShading
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::ConstantBuffer
        ){
            hasSceneShadingTransition = true;
            break;
        }
    }
    EXPECT_TRUE(hasSceneShadingTransition);

    const Graphics::GpuCompiledTask* const compiledLighting = compiledPlan.findTask(lighting).plan;
    ASSERT_NE(compiledLighting, nullptr);
    const Graphics::GpuCompiledBarrier* const lightingBarriers = compiledPlan.findTask(lighting).prologueBarriers;
    ASSERT_NE(lightingBarriers, nullptr);
    bool hasLightingTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledLighting->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = lightingBarriers[barrierIndex];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == shadowVisibility
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        ){
            hasLightingTransition = true;
            break;
        }
    }
    EXPECT_TRUE(hasLightingTransition);
}

TEST(GpuTaskGraph, GraphOwnsOpaqueSoftTemporalMergeHistoryStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId historyA = AddTextureMetadata(
        graph,
        Name("tests/task_graph/opaque_soft_history_a"),
        "Opaque Soft History A",
        Graphics::ResourceStates::UnorderedAccess
    );
    const Graphics::GpuGraphResourceId momentsA = AddTextureMetadata(
        graph,
        Name("tests/task_graph/opaque_soft_moments_a"),
        "Opaque Soft Moments A",
        Graphics::ResourceStates::UnorderedAccess
    );
    const Graphics::GpuGraphResourceId historyB = AddTextureMetadata(
        graph,
        Name("tests/task_graph/opaque_soft_history_b"),
        "Opaque Soft History B",
        Graphics::ResourceStates::UnorderedAccess
    );
    const Graphics::GpuGraphResourceId momentsB = AddTextureMetadata(
        graph,
        Name("tests/task_graph/opaque_soft_moments_b"),
        "Opaque Soft Moments B",
        Graphics::ResourceStates::UnorderedAccess
    );
    const Graphics::GpuGraphResourceId previousGeometry = AddTextureMetadata(
        graph,
        Name("tests/task_graph/opaque_soft_geometry_previous"),
        "Previous Opaque Soft Geometry",
        Graphics::ResourceStates::UnorderedAccess
    );
    const Graphics::GpuGraphResourceId worldPosition = AddTextureMetadata(
        graph,
        Name("tests/task_graph/opaque_soft_world_position"),
        "Opaque Soft World Position",
        Graphics::ResourceStates::UnorderedAccess
    );
    ASSERT_TRUE(historyA.valid());
    ASSERT_TRUE(momentsA.valid());
    ASSERT_TRUE(historyB.valid());
    ASSERT_TRUE(momentsB.valid());
    ASSERT_TRUE(previousGeometry.valid());
    ASSERT_TRUE(worldPosition.valid());

    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    const Graphics::GpuTaskResourceUse opaqueMergeUses[] = {
        {
            .resource = historyB,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = momentsB,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = previousGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = worldPosition,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = historyA,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        {
            .resource = momentsA,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc opaqueMergeDesc;
    opaqueMergeDesc
        .setIdentity(Name("tests/task_graph/opaque_soft_temporal_merge"))
        .setMarkerLabel("Opaque Soft Temporal Merge")
        .setQueue(computeRequest)
        .setResourceUses(opaqueMergeUses, LengthOf(opaqueMergeUses))
    ;
    const Graphics::GpuTaskId opaqueMerge = graph.addTask(opaqueMergeDesc);
    ASSERT_TRUE(opaqueMerge.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 1u);

    const Graphics::GpuCompiledTask* const compiledMerge = compiledPlan.findTask(opaqueMerge).plan;
    ASSERT_NE(compiledMerge, nullptr);
    const Graphics::GpuCompiledBarrier* const mergeBarriers = compiledPlan.findTask(opaqueMerge).prologueBarriers;
    ASSERT_NE(mergeBarriers, nullptr);
    const auto hasMergeInputTransition = [&](const Graphics::GpuGraphResourceId resource){
        for(u32 barrierIndex = 0u; barrierIndex < compiledMerge->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = mergeBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasMergeInputTransition(historyB));
    EXPECT_TRUE(hasMergeInputTransition(momentsB));
    EXPECT_TRUE(hasMergeInputTransition(previousGeometry));
    EXPECT_TRUE(hasMergeInputTransition(worldPosition));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

