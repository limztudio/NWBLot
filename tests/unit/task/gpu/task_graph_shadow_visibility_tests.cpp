// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_shadow_visibility_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, PlansGraphOwnedShadowVisibilityEntryStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing =
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    ;
    const Graphics::GpuGraphResourceId worldPosition = AddTextureMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_world_position"),
        "Shadow Visibility World Position",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId normal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_normal"),
        "Shadow Visibility Normal",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId depth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_depth"),
        "Shadow Visibility Depth",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowVisibility = AddTextureMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_output"),
        "Shadow Visibility",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowSoftHalfA = AddTextureMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_soft_half_a"),
        "Shadow Soft Half A",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowCoarseTransmittance = AddTextureMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_coarse_transmittance"),
        "Shadow Coarse Transmittance",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowSoftGeometry = AddTextureMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_soft_geometry"),
        "Shadow Soft Geometry",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId currentBindlessSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_bindless_slots"),
        "Deferred Bindless Slots",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneShading = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_scene_shading"),
        "Scene Shading",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId lights = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_lights"),
        "Lights",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId materialContextSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_material_context_slots"),
        "Ray-Trace Material Context Slots",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId softwareMeshNodes = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_software_mesh_nodes"),
        "Software Shadow Mesh Nodes",
        Graphics::ResourceStates::UnorderedAccess,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneBvhNodes = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_scene_bvh_nodes"),
        "Scene BVH Nodes",
        Graphics::ResourceStates::UnorderedAccess,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowInstanceMaterials = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_instance_materials"),
        "Shadow Instance Materials",
        Graphics::ResourceStates::UnorderedAccess,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowMaterialTyped = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_material_typed"),
        "Shadow Typed Materials",
        Graphics::ResourceStates::UnorderedAccess,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowInstances = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_instances"),
        "Shadow Instances",
        Graphics::ResourceStates::UnorderedAccess,
        queueSharing
    );
    const Graphics::GpuGraphResourceId edgeStatistics = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_edge_statistics"),
        "Shadow Edge Statistics",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneTlas = AddAccelStructMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_tlas"),
        "Scene TLAS",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneTlasBacking = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_tlas_backing"),
        "Scene TLAS Backing",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    ASSERT_TRUE(worldPosition.valid());
    ASSERT_TRUE(normal.valid());
    ASSERT_TRUE(depth.valid());
    ASSERT_TRUE(shadowVisibility.valid());
    ASSERT_TRUE(shadowSoftHalfA.valid());
    ASSERT_TRUE(shadowCoarseTransmittance.valid());
    ASSERT_TRUE(shadowSoftGeometry.valid());
    ASSERT_TRUE(currentBindlessSlots.valid());
    ASSERT_TRUE(sceneShading.valid());
    ASSERT_TRUE(lights.valid());
    ASSERT_TRUE(materialContextSlots.valid());
    ASSERT_TRUE(softwareMeshNodes.valid());
    ASSERT_TRUE(sceneBvhNodes.valid());
    ASSERT_TRUE(shadowInstanceMaterials.valid());
    ASSERT_TRUE(shadowMaterialTyped.valid());
    ASSERT_TRUE(shadowInstances.valid());
    ASSERT_TRUE(edgeStatistics.valid());
    ASSERT_TRUE(sceneTlas.valid());
    ASSERT_TRUE(sceneTlasBacking.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint prepareScheduling;
    prepareScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    prepareScheduling.forceSubmissionBoundary = true;
    prepareScheduling.allowPacketMerge = false;
    const Graphics::GpuTaskResourceUse shadowPrepareUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneTlas,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneTlasBacking,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc shadowPrepareDesc;
    shadowPrepareDesc
        .setIdentity(Name("tests/task_graph/graph_owned_shadow_prepare"))
        .setMarkerLabel("Shadow Preparation")
        .setQueue(graphicsRequest)
        .setScheduling(prepareScheduling)
        .setResourceUses(shadowPrepareUses, LengthOf(shadowPrepareUses))
    ;
    const Graphics::GpuTaskId shadowPrepare = graph.addTask(shadowPrepareDesc);
    ASSERT_TRUE(shadowPrepare.valid());

    const Graphics::GpuTaskResourceUse prefixUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = worldPosition,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = normal,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = depth,
            .range = {},
            .requiredState = Graphics::ResourceStates::DepthWrite,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneShading,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = lights,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskSchedulingHint prefixScheduling = prepareScheduling;
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/graph_owned_shadow_prefix"))
        .setMarkerLabel("G-Buffer Prefix")
        .setQueue(graphicsRequest)
        .setScheduling(prefixScheduling)
        .setDependencies(&shadowPrepare, 1u)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    const Graphics::GpuGraphResourceSetId softwareTraceGeometrySet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/shadow_visibility_trace_geometry"))
            .setMarkerLabel("Shadow Visibility Trace Geometry")
            .setMembers(&softwareMeshNodes, 1u)
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
    const Graphics::GpuTaskResourceUse shadowVisibilityUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = worldPosition,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = normal,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        // Depth is sampled through the bindless descriptor heap. Keep this ShaderResource rather than DepthRead:
        // Vulkan maps the two states to distinct depth-read-only and shader-read-only image layouts.
        Graphics::GpuTaskResourceUse{
            .resource = depth,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowSoftHalfA,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowCoarseTransmittance,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowSoftGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneShading,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = lights,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneBvhNodes,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowInstanceMaterials,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowMaterialTyped,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = edgeStatistics,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneTlas,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneTlasBacking,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint shadowScheduling;
    shadowScheduling.cost = Graphics::GpuTaskCostHint::Large;
    shadowScheduling.forceSubmissionBoundary = true;
    shadowScheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc shadowVisibilityDesc;
    shadowVisibilityDesc
        .setIdentity(Name("tests/task_graph/graph_owned_shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(computeRequest)
        .setScheduling(shadowScheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(shadowVisibilityUses, LengthOf(shadowVisibilityUses))
        .setResourceSetUses(softwareTraceGeometrySetUses, LengthOf(softwareTraceGeometrySetUses))
    ;
    const Graphics::GpuTaskId shadowVisibilityTask = graph.addTask(shadowVisibilityDesc);
    ASSERT_TRUE(shadowVisibilityTask.valid());

    const Graphics::GpuTaskResourceUse lightingUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = shadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskSchedulingHint lightingScheduling = shadowScheduling;
    Graphics::GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/task_graph/graph_owned_shadow_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(computeRequest)
        .setScheduling(lightingScheduling)
        .setDependencies(&shadowVisibilityTask, 1u)
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

    ASSERT_TRUE(HasInferredHazard(
        analysis,
        shadowVisibilityTask,
        lighting,
        shadowVisibility,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    const Graphics::GpuCompiledTask* const compiledShadowVisibility = compiledPlan.findTask(shadowVisibilityTask).plan;
    const Graphics::GpuCompiledTask* const compiledLighting = compiledPlan.findTask(lighting).plan;
    ASSERT_NE(compiledShadowVisibility, nullptr);
    ASSERT_NE(compiledLighting, nullptr);
    const Graphics::GpuSubmissionPacketId shadowPreparePacket = compiledPlan.packetForTask(shadowPrepare);
    const Graphics::GpuSubmissionPacketId prefixPacket = compiledPlan.packetForTask(prefix);
    const Graphics::GpuSubmissionPacketId shadowPacket = compiledPlan.packetForTask(shadowVisibilityTask);
    const Graphics::GpuSubmissionPacketId lightingPacket = compiledPlan.packetForTask(lighting);
    ASSERT_TRUE(shadowPreparePacket.valid());
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(shadowPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    EXPECT_NE(shadowPreparePacket, prefixPacket);
    EXPECT_NE(prefixPacket, shadowPacket);
    EXPECT_NE(shadowPacket, lightingPacket);
    const Graphics::GpuCompiledTask* const compiledShadowPrepare = compiledPlan.findTask(shadowPrepare).plan;
    ASSERT_NE(compiledShadowPrepare, nullptr);
    const Graphics::GpuCompiledBarrier* const shadowPrepareBarriers = compiledPlan.findTask(shadowPrepare).prologueBarriers;
    ASSERT_NE(shadowPrepareBarriers, nullptr);
    const auto hasShadowPrepareBarrier = [&](
        const Graphics::GpuCompiledBarrierType::Enum type,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::ResourceStates::Mask before,
        const Graphics::ResourceStates::Mask after
    ){
        for(usize barrierIndex = 0u; barrierIndex < compiledShadowPrepare->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = shadowPrepareBarriers[barrierIndex];
            if(
                barrier.type == type
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasShadowPrepareBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        currentBindlessSlots,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::ConstantBuffer
    ));
    EXPECT_TRUE(hasShadowPrepareBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        materialContextSlots,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::ConstantBuffer
    ));
    EXPECT_TRUE(hasShadowPrepareBarrier(
        Graphics::GpuCompiledBarrierType::AccelStructTransition,
        sceneTlas,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::AccelStructRead
    ));
    EXPECT_TRUE(hasShadowPrepareBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        sceneTlasBacking,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::AccelStructRead
    ));
    const Graphics::GpuPacketStateSeed* const shadowSeeds = compiledPlan.findTask(shadowVisibilityTask).prologueStateSeeds;
    ASSERT_NE(shadowSeeds, nullptr);
    const auto hasShadowSeed = [&](const Graphics::GpuGraphResourceId resource, const Graphics::GpuSubmissionPacketId sourcePacket){
        for(usize seedIndex = 0u; seedIndex < compiledShadowVisibility->prologueStateSeedCount; ++seedIndex){
            if(shadowSeeds[seedIndex].resource == resource && shadowSeeds[seedIndex].sourcePacket == sourcePacket)
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasShadowSeed(worldPosition, prefixPacket));
    EXPECT_TRUE(hasShadowSeed(normal, prefixPacket));
    EXPECT_TRUE(hasShadowSeed(depth, prefixPacket));
    EXPECT_TRUE(hasShadowSeed(shadowVisibility, prefixPacket));
    EXPECT_TRUE(hasShadowSeed(sceneShading, prefixPacket));
    EXPECT_TRUE(hasShadowSeed(lights, prefixPacket));
    EXPECT_TRUE(hasShadowSeed(currentBindlessSlots, shadowPreparePacket));
    EXPECT_TRUE(hasShadowSeed(materialContextSlots, shadowPreparePacket));
    EXPECT_TRUE(hasShadowSeed(sceneTlasBacking, shadowPreparePacket));
    const auto shadowPacketWaitsFor = [&](const Graphics::GpuSubmissionPacketId producer){
        const Graphics::GpuSubmissionPacket& packet = *compiledPlan.packet(shadowPacket).plan;
        const Graphics::GpuPacketDependency* const dependencies = compiledPlan.packet(shadowPacket).dependencies;
        if(packet.dependencyCount != 0u && !dependencies)
            return false;
        for(usize dependencyIndex = 0u; dependencyIndex < packet.dependencyCount; ++dependencyIndex){
            if(dependencies[dependencyIndex].producer == producer)
                return true;
        }
        return false;
    };
    EXPECT_TRUE(shadowPacketWaitsFor(shadowPreparePacket));
    EXPECT_TRUE(shadowPacketWaitsFor(prefixPacket));
    const Graphics::GpuCompiledBarrier* const shadowBarriers = compiledPlan.findTask(shadowVisibilityTask).prologueBarriers;
    ASSERT_NE(shadowBarriers, nullptr);
    const auto hasShadowBarrier = [&](
        const Graphics::GpuCompiledBarrierType::Enum type,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::ResourceStates::Mask before,
        const Graphics::ResourceStates::Mask after,
        const bool forceMemoryDependency
    ){
        for(usize barrierIndex = 0u; barrierIndex < compiledShadowVisibility->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = shadowBarriers[barrierIndex];
            if(
                barrier.type == type
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
                && barrier.forceMemoryDependency == forceMemoryDependency
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        worldPosition,
        Graphics::ResourceStates::RenderTarget,
        Graphics::ResourceStates::ShaderResource,
        false
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        normal,
        Graphics::ResourceStates::RenderTarget,
        Graphics::ResourceStates::ShaderResource,
        false
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        depth,
        Graphics::ResourceStates::DepthWrite,
        Graphics::ResourceStates::ShaderResource,
        false
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        shadowVisibility,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess,
        false
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        shadowSoftHalfA,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess,
        false
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        shadowCoarseTransmittance,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess,
        false
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        shadowSoftGeometry,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess,
        false
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        sceneShading,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ConstantBuffer,
        false
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        lights,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ShaderResource,
        false
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        softwareMeshNodes,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource,
        false
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        sceneBvhNodes,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource,
        false
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        shadowInstanceMaterials,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource,
        false
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        shadowMaterialTyped,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource,
        false
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        shadowInstances,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource,
        false
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        edgeStatistics,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess,
        false
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::AccelStructTransition,
        sceneTlas,
        Graphics::ResourceStates::AccelStructRead,
        Graphics::ResourceStates::AccelStructRead,
        true
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        sceneTlasBacking,
        Graphics::ResourceStates::AccelStructRead,
        Graphics::ResourceStates::AccelStructRead,
        true
    ));
    ASSERT_EQ(compiledLighting->prologueStateSeedCount, 1u);
    const Graphics::GpuPacketStateSeed* const lightingSeed = compiledPlan.findTask(lighting).prologueStateSeeds;
    ASSERT_NE(lightingSeed, nullptr);
    EXPECT_EQ(lightingSeed[0u].resource, shadowVisibility);
    EXPECT_EQ(lightingSeed[0u].sourcePacket, shadowPacket);
    ASSERT_EQ(compiledLighting->prologueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const lightingBarrier = compiledPlan.findTask(lighting).prologueBarriers;
    ASSERT_NE(lightingBarrier, nullptr);
    EXPECT_EQ(lightingBarrier[0u].type, Graphics::GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(lightingBarrier[0u].resource, shadowVisibility);
    EXPECT_EQ(lightingBarrier[0u].before, Graphics::ResourceStates::UnorderedAccess);
    EXPECT_EQ(lightingBarrier[0u].after, Graphics::ResourceStates::ShaderResource);
    ASSERT_EQ(compiledPlan.packet(lightingPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(lightingPacket).dependencies[0u].producer, shadowPacket);
}

TEST(GpuTaskGraph, MergesGraphOwnedAdaptiveShadowPrimitivesIntoShadowVisibilityPacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing =
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    ;
    const Graphics::GpuGraphResourceId edgeStats = AddBufferMetadata(
        graph,
        Name("tests/task_graph/adaptive_shadow_edge_stats"),
        "Adaptive Shadow Edge Statistics",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId edgeCounter = AddBufferMetadata(
        graph,
        Name("tests/task_graph/adaptive_shadow_edge_counter"),
        "Adaptive Shadow Edge Counter",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId statsReadback = AddBufferMetadata(
        graph,
        Name("tests/task_graph/adaptive_shadow_edge_stats_readback"),
        "Adaptive Shadow Edge Statistics Readback",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowVisibility = AddTextureMetadata(
        graph,
        Name("tests/task_graph/adaptive_shadow_visibility"),
        "Adaptive Shadow Visibility",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    ASSERT_TRUE(edgeStats.valid());
    ASSERT_TRUE(edgeCounter.valid());
    ASSERT_TRUE(statsReadback.valid());
    ASSERT_TRUE(shadowVisibility.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest computeTransferRequest{
        QueueCapabilities(
            Graphics::GpuQueueCapability::Compute,
            Graphics::GpuQueueCapability::Transfer
        ),
        Graphics::GpuQueuePreference::Compute,
        true,
        false,
    };
    Graphics::GpuTaskSchedulingHint prefixScheduling;
    prefixScheduling.cost = Graphics::GpuTaskCostHint::Large;
    prefixScheduling.forceSubmissionBoundary = true;
    prefixScheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/adaptive_shadow_prefix"))
        .setMarkerLabel("Adaptive Shadow Prefix")
        .setQueue(graphicsRequest)
        .setScheduling(prefixScheduling)
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    Graphics::GpuTaskSchedulingHint primitiveScheduling;
    primitiveScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    primitiveScheduling.allowPacketMerge = true;
    const Graphics::GpuTaskResourceUse statsClearUses[] = {
        {
            .resource = edgeStats,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc statsClearDesc;
    statsClearDesc
        .setIdentity(Name("tests/task_graph/adaptive_shadow_stats_clear"))
        .setMarkerLabel("Adaptive Shadow Statistics Clear")
        .setQueue(computeTransferRequest)
        .setScheduling(primitiveScheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(statsClearUses, LengthOf(statsClearUses))
    ;
    const Graphics::GpuTaskId statsClear = graph.addTask(statsClearDesc);
    ASSERT_TRUE(statsClear.valid());

    Graphics::GpuTaskSchedulingHint counterClearScheduling = primitiveScheduling;
    counterClearScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse counterClearUses[] = {
        {
            .resource = edgeCounter,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc counterClearDesc;
    counterClearDesc
        .setIdentity(Name("tests/task_graph/adaptive_shadow_counter_clear"))
        .setMarkerLabel("Adaptive Shadow Counter Clear")
        .setQueue(computeTransferRequest)
        .setScheduling(counterClearScheduling)
        .setDependencies(&statsClear, 1u)
        .setResourceUses(counterClearUses, LengthOf(counterClearUses))
    ;
    const Graphics::GpuTaskId counterClear = graph.addTask(counterClearDesc);
    ASSERT_TRUE(counterClear.valid());

    Graphics::GpuTaskSchedulingHint shadowScheduling;
    shadowScheduling.cost = Graphics::GpuTaskCostHint::Large;
    shadowScheduling.allowPacketMerge = true;
    shadowScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse shadowUses[] = {
        {
            .resource = edgeStats,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = edgeCounter,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = shadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc shadowDesc;
    shadowDesc
        .setIdentity(Name("tests/task_graph/adaptive_shadow_visibility"))
        .setMarkerLabel("Adaptive Shadow Visibility")
        .setQueue(computeTransferRequest)
        .setScheduling(shadowScheduling)
        .setDependencies(&counterClear, 1u)
        .setResourceUses(shadowUses, LengthOf(shadowUses))
    ;
    const Graphics::GpuTaskId shadow = graph.addTask(shadowDesc);
    ASSERT_TRUE(shadow.valid());

    Graphics::GpuTaskSchedulingHint statsReadbackScheduling = primitiveScheduling;
    statsReadbackScheduling.mergeWithPrevious = true;
    statsReadbackScheduling.allowMergeAcrossConsumerFrontier = true;
    const Graphics::GpuTaskResourceUse statsReadbackUses[] = {
        {
            .resource = edgeStats,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopySource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = statsReadback,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc statsReadbackDesc;
    statsReadbackDesc
        .setIdentity(Name("tests/task_graph/adaptive_shadow_stats_readback"))
        .setMarkerLabel("Adaptive Shadow Statistics Readback")
        .setQueue(computeTransferRequest)
        .setScheduling(statsReadbackScheduling)
        .setDependencies(&shadow, 1u)
        .setResourceUses(statsReadbackUses, LengthOf(statsReadbackUses))
    ;
    const Graphics::GpuTaskId statsCopy = graph.addTask(statsReadbackDesc);
    ASSERT_TRUE(statsCopy.valid());

    Graphics::GpuTaskSchedulingHint lightingScheduling;
    lightingScheduling.cost = Graphics::GpuTaskCostHint::Large;
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
        .setIdentity(Name("tests/task_graph/adaptive_shadow_lighting"))
        .setMarkerLabel("Adaptive Shadow Lighting")
        .setQueue(graphicsRequest)
        .setScheduling(lightingScheduling)
        .setDependencies(&shadow, 1u)
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


    const Graphics::GpuSubmissionPacketId statsClearPacket = compiledPlan.packetForTask(statsClear);
    const Graphics::GpuSubmissionPacketId counterClearPacket = compiledPlan.packetForTask(counterClear);
    const Graphics::GpuSubmissionPacketId shadowPacket = compiledPlan.packetForTask(shadow);
    const Graphics::GpuSubmissionPacketId statsCopyPacket = compiledPlan.packetForTask(statsCopy);
    const Graphics::GpuSubmissionPacketId lightingPacket = compiledPlan.packetForTask(lighting);
    ASSERT_TRUE(shadowPacket.valid());
    EXPECT_EQ(statsClearPacket, shadowPacket);
    EXPECT_EQ(counterClearPacket, shadowPacket);
    EXPECT_EQ(statsCopyPacket, shadowPacket);
    EXPECT_NE(lightingPacket, shadowPacket);
    ASSERT_EQ(compiledPlan.packetCount(), 3u);
    const Graphics::GpuSubmissionPacketRange shadowRange = compiledPlan.packetRangeForTasks(shadow, shadow);
    ASSERT_TRUE(shadowRange.valid());
    EXPECT_EQ(shadowRange.packetCount, 1u);
    EXPECT_EQ(shadowRange.first, shadowPacket);
    const Graphics::GpuSubmissionPacket& shadowPacketDesc = *compiledPlan.packet(shadowPacket).plan;
    ASSERT_EQ(shadowPacketDesc.taskCount, 4u);
    const Graphics::GpuTaskId* const shadowPacketTasks = compiledPlan.packet(shadowPacket).tasks;
    ASSERT_NE(shadowPacketTasks, nullptr);
    EXPECT_EQ(shadowPacketTasks[0u], statsClear);
    EXPECT_EQ(shadowPacketTasks[1u], counterClear);
    EXPECT_EQ(shadowPacketTasks[2u], shadow);
    EXPECT_EQ(shadowPacketTasks[3u], statsCopy);

    const auto hasBufferTransition = [&](
        const Graphics::GpuTaskId task,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::ResourceStates::Mask before,
        const Graphics::ResourceStates::Mask after
    ){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        if(!compiledTask)
            return false;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        if(!barriers)
            return false;
        for(usize barrierIndex = 0u; barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasBufferTransition(
        statsClear,
        edgeStats,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        counterClear,
        edgeCounter,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        shadow,
        edgeStats,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        shadow,
        edgeCounter,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        statsCopy,
        edgeStats,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::CopySource
    ));
    EXPECT_TRUE(hasBufferTransition(
        statsCopy,
        statsReadback,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest
    ));
}

TEST(GpuTaskGraph, MergesGraphOwnedShadowVisibilityAllLitClearIntoMonolithicPacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuExternalCompletionId priorHistoryTail = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/monolithic_shadow_visibility_history_tail"))
            .setMarkerLabel("Prior Lagged Lighting History Tail")
    );
    ASSERT_TRUE(priorHistoryTail.valid());
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing =
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    ;
    const Graphics::GpuGraphResourceId shadowVisibility = AddTextureMetadata(
        graph,
        Name("tests/task_graph/monolithic_shadow_visibility_all_lit"),
        "Monolithic Shadow Visibility",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    ASSERT_TRUE(shadowVisibility.valid());
    const Graphics::GpuGraphResourceId shadowSoftHalfA = AddTextureMetadata(
        graph,
        Name("tests/task_graph/monolithic_shadow_visibility_soft_half_a"),
        "Monolithic Shadow Soft Half A",
        Graphics::ResourceStates::Unknown,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowSoftHalfB = AddTextureMetadata(
        graph,
        Name("tests/task_graph/monolithic_shadow_visibility_soft_half_b"),
        "Monolithic Shadow Soft Half B",
        Graphics::ResourceStates::Unknown,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowSoftGeometry = AddTextureMetadata(
        graph,
        Name("tests/task_graph/monolithic_shadow_visibility_soft_geometry"),
        "Monolithic Shadow Soft Geometry",
        Graphics::ResourceStates::Unknown,
        queueSharing
    );
    ASSERT_TRUE(shadowSoftHalfA.valid());
    ASSERT_TRUE(shadowSoftHalfB.valid());
    ASSERT_TRUE(shadowSoftGeometry.valid());

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
        false,
    };
    const Graphics::GpuQueueRequest computeTransferRequest{
        QueueCapabilities(
            Graphics::GpuQueueCapability::Compute,
            Graphics::GpuQueueCapability::Transfer
        ),
        Graphics::GpuQueuePreference::Compute,
        true,
        false,
    };
    Graphics::GpuTaskSchedulingHint prefixScheduling;
    prefixScheduling.cost = Graphics::GpuTaskCostHint::Large;
    prefixScheduling.forceSubmissionBoundary = true;
    prefixScheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/monolithic_shadow_visibility_prefix"))
        .setMarkerLabel("Monolithic Shadow Prefix")
        .setQueue(graphicsRequest)
        .setScheduling(prefixScheduling)
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    Graphics::GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    clearScheduling.allowPacketMerge = true;
    const Graphics::GpuTaskResourceUse allLitClearUses[] = {
        {
            .resource = shadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc allLitClearDesc;
    allLitClearDesc
        .setIdentity(Name("tests/task_graph/monolithic_shadow_visibility_all_lit_clear"))
        .setMarkerLabel("Shadow Visibility All-Lit Clear")
        .setQueue(computeRequest)
        .setScheduling(clearScheduling)
        .setDependencies(&prefix, 1u)
        .setExternalDependencies(&priorHistoryTail, 1u)
        .setResourceUses(allLitClearUses, LengthOf(allLitClearUses))
    ;
    const Graphics::GpuTaskId allLitClear = graph.addTask(allLitClearDesc);
    ASSERT_TRUE(allLitClear.valid());

    Graphics::GpuTaskSchedulingHint shadowScheduling;
    shadowScheduling.cost = Graphics::GpuTaskCostHint::Large;
    shadowScheduling.allowPacketMerge = true;
    shadowScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse shadowUses[] = {
        {
            .resource = shadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = shadowSoftHalfA,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        {
            .resource = shadowSoftHalfB,
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
    };
    Graphics::GpuTaskDesc shadowDesc;
    shadowDesc
        .setIdentity(Name("tests/task_graph/monolithic_shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(computeTransferRequest)
        .setScheduling(shadowScheduling)
        .setDependencies(&allLitClear, 1u)
        .setResourceUses(shadowUses, LengthOf(shadowUses))
    ;
    const Graphics::GpuTaskId shadow = graph.addTask(shadowDesc);
    ASSERT_TRUE(shadow.valid());

    Graphics::GpuTaskSchedulingHint lightingScheduling = prefixScheduling;
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
        .setIdentity(Name("tests/task_graph/monolithic_shadow_visibility_lighting"))
        .setMarkerLabel("Monolithic Shadow Lighting")
        .setQueue(graphicsRequest)
        .setScheduling(lightingScheduling)
        .setDependencies(&shadow, 1u)
        .setResourceUses(lightingUses, LengthOf(lightingUses))
    ;
    const Graphics::GpuTaskId lighting = graph.addTask(lightingDesc);
    ASSERT_TRUE(lighting.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
        DedicatedTransferQueue(),
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


    const Graphics::GpuSubmissionPacketId prefixPacket = compiledPlan.packetForTask(prefix);
    const Graphics::GpuSubmissionPacketId clearPacket = compiledPlan.packetForTask(allLitClear);
    const Graphics::GpuSubmissionPacketId shadowPacket = compiledPlan.packetForTask(shadow);
    const Graphics::GpuSubmissionPacketId lightingPacket = compiledPlan.packetForTask(lighting);
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(clearPacket.valid());
    ASSERT_TRUE(shadowPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    const Graphics::GpuPhysicalQueueInfo* const clearQueue = compiledPlan.queueInfoForTask(allLitClear);
    const Graphics::GpuPhysicalQueueInfo* const shadowQueue = compiledPlan.queueInfoForTask(shadow);
    ASSERT_NE(clearQueue, nullptr);
    ASSERT_NE(shadowQueue, nullptr);
    EXPECT_EQ(clearQueue->id, queues[1u].id);
    EXPECT_EQ(shadowQueue->id, queues[1u].id);
    EXPECT_EQ(clearQueue->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(shadowQueue->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_TRUE(clearQueue->dedicated);
    EXPECT_TRUE(shadowQueue->dedicated);
    EXPECT_NE(prefixPacket, shadowPacket);
    EXPECT_EQ(clearPacket, shadowPacket);
    EXPECT_NE(lightingPacket, shadowPacket);
    ASSERT_EQ(compiledPlan.packetCount(), 3u);
    const Graphics::GpuSubmissionPacketRange shadowRange = compiledPlan.packetRangeForTasks(shadow, shadow);
    ASSERT_TRUE(shadowRange.valid());
    EXPECT_EQ(shadowRange.packetCount, 1u);
    EXPECT_EQ(shadowRange.first, shadowPacket);
    const Graphics::GpuSubmissionPacket& shadowPacketDesc = *compiledPlan.packet(shadowPacket).plan;
    ASSERT_EQ(shadowPacketDesc.taskCount, 2u);
    const Graphics::GpuTaskId* const shadowPacketTasks = compiledPlan.packet(shadowPacket).tasks;
    ASSERT_NE(shadowPacketTasks, nullptr);
    EXPECT_EQ(shadowPacketTasks[0u], allLitClear);
    EXPECT_EQ(shadowPacketTasks[1u], shadow);
    ASSERT_EQ(shadowPacketDesc.externalDependencyCount, 1u);
    const Graphics::GpuExternalCompletionId* const shadowExternalDependencies = compiledPlan.packet(
        shadowPacket
    ).externalDependencies;
    ASSERT_NE(shadowExternalDependencies, nullptr);
    EXPECT_EQ(shadowExternalDependencies[0u], priorHistoryTail);

    const auto hasTextureTransition = [&](
        const Graphics::GpuTaskId task,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::ResourceStates::Mask before,
        const Graphics::ResourceStates::Mask after
    ){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        if(!compiledTask)
            return false;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        if(!barriers)
            return false;
        for(usize barrierIndex = 0u; barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTextureTransition(
        allLitClear,
        shadowVisibility,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasTextureTransition(
        shadow,
        shadowVisibility,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasTextureTransition(
        lighting,
        shadowVisibility,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    const auto hasUnknownFirstWrite = [&](const Graphics::GpuGraphResourceId resource){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(shadow).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(shadow).prologueBarriers;
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
    EXPECT_TRUE(hasUnknownFirstWrite(shadowSoftHalfA));
    EXPECT_TRUE(hasUnknownFirstWrite(shadowSoftHalfB));
    EXPECT_TRUE(hasUnknownFirstWrite(shadowSoftGeometry));
    ASSERT_EQ(compiledPlan.packet(shadowPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(shadowPacket).dependencies[0u].producer, prefixPacket);
    ASSERT_EQ(compiledPlan.packet(lightingPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(lightingPacket).dependencies[0u].producer, shadowPacket);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

